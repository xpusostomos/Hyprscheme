#include "Guile.hpp"
#include "Bindings.hpp"
#include "SThunkRef.hpp"
#include "SchemeLayout.hpp"

#include "SchemeInternals.hpp"
#include "Handles.hpp"

extern "C" {

}

#include <src/debug/log/Logger.hpp>
#include <src/layout/algorithm/Algorithm.hpp>
#include <src/layout/space/Space.hpp>
#include <src/layout/supplementary/WorkspaceAlgoMatcher.hpp>
#include <src/layout/target/Target.hpp>
#include <src/notification/NotificationOverlay.hpp>
#include <src/event/EventBus.hpp>
#include <src/config/supplementary/propRefresher/PropRefresher.hpp>

#include <algorithm>
#include <cmath>

using namespace Config::Scheme;

namespace Config::Scheme::Layouts {

    static std::vector<SP<SSchemeLayoutProvider>> g_layouts;
    static std::vector<Hyprutils::Signal::CHyprSignalListener> g_layoutEventListeners;

    static SCM dispatchRecalculate(const WP<Layout::CAlgorithm>& parent, SP<SSchemeLayoutProvider> provider,
                                   const std::vector<SP<Layout::ITarget>>& targets);
    static SCM dispatchResize(const WP<Layout::CAlgorithm>& parent, SP<SSchemeLayoutProvider> provider,
                              const std::vector<SP<Layout::ITarget>>& targets, const Vector2D& delta, int corner);
    static bool applyBoxes(const std::vector<SP<Layout::ITarget>>& targets, SCM result);

    static SCM schemeSpec(SP<SSchemeLayoutProvider>& p) { return p->spec.obj; }
    static SCM callScheme1(const char* fn, SCM spec, SCM a) {
        return hl::call2(hl::globalRef(fn), spec, a);
    }

    static void fireSchemeLayoutWindowOpen(SP<SSchemeLayoutProvider> provider, PHLWINDOW window) {
        if (!provider || !provider->active || !window)
            return;
        callScheme1("hl--layout-window-open", schemeSpec(provider), hl::windowHandle(window));
    }

    static void fireSchemeLayoutWindowClose(SP<SSchemeLayoutProvider> provider, PHLWINDOW window) {
        if (!provider || !provider->active || !window)
            return;
        callScheme1("hl--layout-window-close", schemeSpec(provider), hl::windowHandle(window));
    }

    static std::string normalizeName(std::string name) {
        if (!name.starts_with("scheme:"))
            name = std::format("scheme:{}", name);
        return name;
    }

    // (name, spec) -> 0 ok / -1 rejected: registers a custom layout
    static int layoutAdd(const char* name, SCM spec) {
            if (!Internals::g_up || !name || !*name)
                return -1;

            const auto full = normalizeName(name);
            for (const auto& l : g_layouts) {
                if (l->name == full)
                    return -1;
            }

            auto provider  = makeShared<SSchemeLayoutProvider>();
            provider->name = full;
            provider->spec.set(spec);

            if (!Layout::Supplementary::algoMatcher()->registerTiledAlgo(full, &typeid(CSchemeTiledAlgorithm),
                    [provider] { return makeUnique<CSchemeTiledAlgorithm>(provider); })) {
                LOG(Log::ERR, "[scheme] layout {} failed to register", full);
                return -1;
            }

            g_layouts.emplace_back(provider);

            // at registration time workspaces may exist without attached
            // monitors/spaces yet, in which case updateWorkspaceLayouts skips
            // them; a scheduled refresh re-runs it once the world is assembled
            Config::Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_LAYOUTS);

            g_layoutEventListeners.emplace_back(Event::bus()->m_events.window.openLate.listen([provider](PHLWINDOW w) { fireSchemeLayoutWindowOpen(provider, w); }));
            g_layoutEventListeners.emplace_back(Event::bus()->m_events.window.close.listen([provider](PHLWINDOW w) { fireSchemeLayoutWindowClose(provider, w); }));

            return 0;
    }

    void registerSymbols() {
        hl::bind<layoutAdd>("hl--c-layout-add");
    }

    void clear() {
        if (g_layouts.empty())
            return;

        for (const auto& l : g_layouts)
            l->active = false;
        for (const auto& l : g_layouts)
            Layout::Supplementary::algoMatcher()->unregisterAlgo(l->name);

        g_layouts.clear();
        g_layoutEventListeners.clear();

        Config::Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_LAYOUTS);
    }

    CSchemeTiledAlgorithm::CSchemeTiledAlgorithm(SP<SSchemeLayoutProvider> provider) : m_provider(std::move(provider)) {
    }

    void CSchemeTiledAlgorithm::newTarget(SP<Layout::ITarget> target) {
        m_targets.emplace_back(target);
        recalculate();
    }

    void CSchemeTiledAlgorithm::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint) {
        newTarget(target);
    }

    void CSchemeTiledAlgorithm::removeTarget(SP<Layout::ITarget> target) {
        std::erase_if(m_targets, [&target](const auto& t) { return !t || t.lock() == target; });
        recalculate();
    }

    void CSchemeTiledAlgorithm::resizeTarget(const Vector2D& delta, SP<Layout::ITarget> target, Layout::eRectCorner corner) {
        auto targets = liveTargets();
        if (targets.empty())
            return;

        // a resize callback adjusts its state and returns boxes, or returns
        // #f meaning "state updated, re-run recalculate"
        const SCM r = dispatchResize(m_parent, m_provider, targets, delta, sc<int>(corner));
        if (r == SCM_BOOL_F) {
            const SCM r2 = dispatchRecalculate(m_parent, m_provider, targets);
            if (r2 == SCM_BOOL_F || !applyBoxes(targets, r2))
                applyDefaultGrid(targets);
            return;
        }
        if (!applyBoxes(targets, r))
            applyDefaultGrid(targets);
    }

    void CSchemeTiledAlgorithm::swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) {
        auto ia = std::ranges::find_if(m_targets, [&a](const auto& t) { return t.lock() == a; });
        auto ib = std::ranges::find_if(m_targets, [&b](const auto& t) { return t.lock() == b; });

        if (ia != m_targets.end() && ib != m_targets.end())
            std::iter_swap(ia, ib);
        else {
            if (ia != m_targets.end())
                *ia = b;
            if (ib != m_targets.end())
                *ib = a;
        }

        recalculate();
    }

    void CSchemeTiledAlgorithm::moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection dir, bool silent) {
        auto it = std::ranges::find_if(m_targets, [&t](const auto& target) { return target.lock() == t; });
        if (it == m_targets.end())
            return;

        if ((dir == Math::DIRECTION_LEFT || dir == Math::DIRECTION_UP) && it != m_targets.begin())
            std::iter_swap(it, std::prev(it));
        else if ((dir == Math::DIRECTION_RIGHT || dir == Math::DIRECTION_DOWN) && std::next(it) != m_targets.end())
            std::iter_swap(it, std::next(it));
        else
            return;

        recalculate();
    }

    SP<Layout::ITarget> CSchemeTiledAlgorithm::getNextCandidate(SP<Layout::ITarget> old) {
        auto targets = liveTargets();
        if (targets.empty())
            return nullptr;

        auto it = std::ranges::find(targets, old);
        if (it == targets.end())
            return targets.back();

        if (targets.size() == 1)
            return nullptr;

        auto next = std::next(it);
        if (next == targets.end())
            next = targets.begin();

        return *next;
    }

    std::optional<std::string> CSchemeTiledAlgorithm::layoutName() const {
        if (!m_provider)
            return std::nullopt;
        return m_provider->name;
    }

    std::vector<SP<Layout::ITarget>> CSchemeTiledAlgorithm::liveTargets() {
        std::erase_if(m_targets, [](const auto& target) { return !target || !target.lock(); });

        std::vector<SP<Layout::ITarget>> result;
        for (const auto& target : m_targets) {
            if (const auto locked = target.lock(); locked)
                result.emplace_back(locked);
        }
        return result;
    }

    /*
        BOXES. A box is a rectangle in GLOBAL coordinates — four numbers, the
        same thing the compositor's CBox is. The Scheme side has a record for
        it (hl-box, in (hyprscheme core)) so that a layout reads and writes
        `(hl-box-x b)` rather than list positions, and a wrong-shaped value is
        an error naming the function instead of a silent misread.

        C++ reads the record directly — one lookup of the record type, then
        slot reads, which are non-allocating so no GC can move anything
        mid-read. Writing goes through the public constructor.
    */
    static bool boxRtd(SCM& out) {
        // the record type, defined by (hyprscheme core); looked up, not cached:
        // a reload rebuilds the module, and with it the record type
        out = hl::globalRefOrFalse("hl-box-rtd");
        return !scm_is_false(out);
    }

    // SCM_STRUCTP, NOT scm_struct_vtable_p: the latter answers "is this a
    // vtable", which a box instance is not — it is a struct whose vtable is
    // the record type. (Getting this backwards made every returned box fail
    // the shape check, so layouts silently fell back to the default grid.)
    static bool isBox(SCM v, SCM rtd) {
        return SCM_STRUCTP(v) && scm_is_eq(SCM_STRUCT_VTABLE(v), rtd);
    }

    // A layout divides the work area, so it produces rationals (Scheme's /).
    // Refusing those — and falling back to a default grid — was a trap: the
    // natural way to write a layout failed silently. Any real is accepted now,
    // and rounded to a whole pixel.
    static bool boxNumber(SCM v, double& out) {
        if (!scm_is_number(v))
            return false;
        out = scm_to_double(v);
        return true;
    }

    // WHOLE PIXELS, as EXACT integers. A box is a pixel rectangle, and we round
    // what a layout returns — so rounding what we hand it keeps the two ends
    // symmetric. Handing back an inexact 331.5 instead is a trap: dividing the
    // work area is the first thing a layout does, and `quotient` (the exact
    // integer division such code reaches for) refuses an inexact argument.
    static SCM boxToScheme(const CBox& b) {
        SCM ctor = hl::globalRefOrFalse("hl-box");
        if (scm_is_false(ctor))
            return SCM_BOOL_F;
        return hl::call4(ctor, scm_from_long(std::lround(b.x)), scm_from_long(std::lround(b.y)),
                         scm_from_long(std::lround(b.w)), scm_from_long(std::lround(b.h)));
    }

    // a placement: (window . box). #f when the value is not one.
    static bool readPlacement(SCM pair, SCM rtd, SCM& window, CBox& box) {
        if (!scm_is_pair(pair)) {
            return false;
        }
        window = scm_car(pair);
        const SCM b = scm_cdr(pair);
        if (!isBox(b, rtd))
            return false;

        double v[4];
        for (size_t i = 0; i < 4; ++i) {
            if (!boxNumber(scm_struct_ref(b, scm_from_size_t(i)), v[i]))
                return false;
        }
        box = CBox(std::lround(v[0]), std::lround(v[1]), std::lround(v[2]), std::lround(v[3])).noNegativeSize();
        return true;
    }

    /*
        The two geometry callbacks. Both take (area placements) and both return
        the same shape; resize adds the gesture at the end.

          area       — an hl-box: the work area, in GLOBAL coordinates
          placements — ((window . box) ...), each box being where that window
                       is NOW

        A target with no window is SKIPPED: there is nothing to place in it, and
        it exists only for the moment between a window dying and the layout
        dropping the slot. That is also what retires the dead-handle convention
        the old positional payload needed.
    */
    static SCM placementList(const std::vector<SP<Layout::ITarget>>& targets, SCM rtd, std::vector<SCM>& roots) {
        std::vector<SCM> vals;
        const auto       push = [&](SCM v) {
            hl::pin(v, roots);
            vals.push_back(v);
        };
        for (const auto& t : targets) {
            const auto window = t->window();
            if (!window)
                continue; // nothing to place
            const SCM box = boxToScheme(t->position());
            if (scm_is_false(box))
                continue;
            push(scm_cons(hl::windowHandle(window), box));
        }
        return hl::listOf(vals);
    }

    static SCM dispatchRecalculate(const WP<Layout::CAlgorithm>& parent, SP<SSchemeLayoutProvider> provider,
                                   const std::vector<SP<Layout::ITarget>>& targets) {
        auto space = parent.lock() ? parent.lock()->space() : nullptr;
        if (!space)
            return SCM_BOOL_F;

        SCM rtd;
        if (!boxRtd(rtd))
            return SCM_BOOL_F;

        std::vector<SCM> roots;
        const SCM        area = boxToScheme(space->workArea());
        if (scm_is_false(area))
            return SCM_BOOL_F;

        const SCM placements = placementList(targets, rtd, roots);
        const SCM payload    = hl::listOf({area, placements});
        hl::unpin(roots);

        return callScheme1("hl--layout-recalculate", schemeSpec(provider), payload);
    }

    static SCM dispatchResize(const WP<Layout::CAlgorithm>& parent, SP<SSchemeLayoutProvider> provider,
                              const std::vector<SP<Layout::ITarget>>& targets, const Vector2D& delta, int corner) {
        auto space = parent.lock() ? parent.lock()->space() : nullptr;
        if (!space)
            return SCM_BOOL_F;

        SCM rtd;
        if (!boxRtd(rtd))
            return SCM_BOOL_F;

        std::vector<SCM> roots;
        const SCM        area = boxToScheme(space->workArea());
        if (scm_is_false(area))
            return SCM_BOOL_F;

        const SCM placements = placementList(targets, rtd, roots);
        const SCM dx         = scm_from_long(std::lround(delta.x));
        const SCM dy         = scm_from_long(std::lround(delta.y));
        const SCM payload    = hl::listOf({area, placements, dx, dy, scm_from_int(corner)});
        hl::unpin(roots);

        return callScheme1("hl--layout-resize", schemeSpec(provider), payload);
    }

    /*
        Apply a returned placement list. Each pair names a WINDOW — the handle
        the callback was given — and the box is where to put it; the target is
        found by matching that window against the layout's own targets, so a
        callback that reorders the list, or returns only part of it, is either
        correct or reported. That replaces the old positional contract, where
        box i was assumed to belong to target i and a reordering placed every
        window silently wrong.

        A window that is not in this layout is REJECTED, not mis-placed.
    */
    static bool applyBoxes(const std::vector<SP<Layout::ITarget>>& targets, SCM result) {
        if (result == SCM_BOOL_F || !scm_is_pair(result))
            return result == SCM_EOL; // an empty list is a valid "change nothing"

        SCM rtd;
        if (!boxRtd(rtd)) {
            return false;
        }

        for (SCM l = result; scm_is_pair(l); l = scm_cdr(l)) {
            SCM  handle = SCM_BOOL_F;
            CBox box;
            if (!readPlacement(scm_car(l), rtd, handle, box))
                return false;

            const auto window = hl::isWindow(handle) ? hl::windowOf(handle) : PHLWINDOW{};
            if (!window) {
                return false; // not a window, or a handle whose window has gone
            }


            const auto it = std::ranges::find_if(targets, [&window](const auto& t) { return t->window() == window; });
            if (it == targets.end()) {
                LOG(Log::ERR, "[scheme] a layout placed a window that is not in it; ignoring the result");
                return false;
            }

            (*it)->setPositionGlobal(box);
        }

        return true;
    }

    bool CSchemeTiledAlgorithm::callLayout(const std::vector<SP<Layout::ITarget>>& targets) {
        if (!m_provider || !m_provider->active)
            return false;

        const SCM r = dispatchRecalculate(m_parent, m_provider, targets);
        if (r == SCM_BOOL_F || !applyBoxes(targets, r)) {
            reportError("layout callback failed or returned a malformed box list");
            return false;
        }

        return true;
    }

    void CSchemeTiledAlgorithm::applyDefaultGrid(const std::vector<SP<Layout::ITarget>>& targets) {
        auto parent = m_parent.lock();
        auto space  = parent ? parent->space() : nullptr;
        if (!space || targets.empty())
            return;

        const auto AREA = space->workArea();
        const int  cols = std::max(1, sc<int>(std::ceil(std::sqrt(sc<double>(targets.size())))));
        const int  rows = std::max(1, sc<int>(std::ceil(sc<double>(targets.size()) / sc<double>(cols))));

        for (size_t i = 0; i < targets.size(); ++i) {
            const int col = sc<int>(i) % cols;
            const int row = sc<int>(i) / cols;
            targets[i]->setPositionGlobal(CBox{AREA.x + AREA.w * col / cols, AREA.y + AREA.h * row / rows, AREA.w / cols, AREA.h / rows}.noNegativeSize());
        }
    }

    void CSchemeTiledAlgorithm::reportError(const std::string& message) {
        if (!m_provider)
            return;

        LOG(Log::ERR, "[scheme] layout {} error: {}", m_provider->name, message);

        if (m_provider->didError)
            return;

        m_provider->didError = true;

        Notification::overlay()->addNotification(std::format("[scheme] layout {} error: {}", m_provider->name, message), CHyprColor(0), 5000, ICON_WARNING);
    }

    void CSchemeTiledAlgorithm::recalculate(Layout::eRecalculateReason reason) {
        auto targets = liveTargets();
        if (targets.empty())
            return;

        if (!callLayout(targets))
            applyDefaultGrid(targets);
    }

    // hyprctl dispatch layoutmsg <msg> -> the provider's layout-msg callback.
    // callback returns: #f = rejected, string = error with that message,
    // anything else = accepted (state may have changed; we recalculate).
    Config::ErrorResult CSchemeTiledAlgorithm::layoutMsg(const std::string_view& sv) {
        if (!m_provider || !m_provider->active)
            return {};

        const SCM r = callScheme1("hl--layout-msg", schemeSpec(m_provider), scm_from_utf8_stringn(sv.data(), sv.size()));

        std::string out;
        if (scm_is_string(r))
            out = hl::toStdString(r);

        if (!out.empty())
            return Config::configError(std::format("layout {}: {}", m_provider->name, out), Config::eConfigErrorLevel::ERROR, Config::eConfigErrorCode::INVALID_ARGUMENT);

        recalculate();
        return {};
    }
}
