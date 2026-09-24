#ifndef HYPRTHEME_SCHEME_H_SEEN
#define HYPRTHEME_SCHEME_H_SEEN
#include <scheme.h>
#endif
#include "SThunkRef.hpp"
#include "SchemeLayout.hpp"

#include "SchemeInternals.hpp"

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

    // FFI marshalling: build a real scheme list (the payload is all fixnums —
    // only the cons cells are heap objects, pinned with Slock_object while
    // stitching; see the marshalling notes in SchemeManager.cpp).
    template <typename T>
    static ptr schemeIntList(const std::vector<T>& vals) {
        if (vals.empty())
            return Snil;
        ptr l = Snil;
        for (auto it = vals.rbegin(); it != vals.rend(); ++it) {
            l = Scons(Sinteger(*it), l);
            Slock_object(l);
        }
        for (ptr p = l; Spairp(p); p = Scdr(p))
            Sunlock_object(p);
        return l;
    }

    static ptr dispatchRecalculate(const WP<Layout::CAlgorithm>& parent, SP<SSchemeLayoutProvider> provider,
                                   const std::vector<SP<Layout::ITarget>>& targets);
    static ptr dispatchResize(const WP<Layout::CAlgorithm>& parent, SP<SSchemeLayoutProvider> provider,
                              const std::vector<SP<Layout::ITarget>>& targets, const Vector2D& delta, int corner);
    static bool applyBoxes(const std::vector<SP<Layout::ITarget>>& targets, ptr result);

    static ptr schemeSpec(SP<SSchemeLayoutProvider>& p) { return p->spec.obj; }
    static ptr callScheme1(const char* fn, ptr spec, ptr a) {
        return Scall2(Stop_level_value(Sstring_to_symbol(fn)), spec, a);
    }

    static void fireSchemeLayoutWindowOpen(SP<SSchemeLayoutProvider> provider, PHLWINDOW window) {
        if (!provider || !provider->active || !window)
            return;
        callScheme1("hl--layout-window-open", schemeSpec(provider), Sinteger(Internals::mintWindowHandle(window)));
    }

    static void fireSchemeLayoutWindowClose(SP<SSchemeLayoutProvider> provider, PHLWINDOW window) {
        if (!provider || !provider->active || !window)
            return;
        callScheme1("hl--layout-window-close", schemeSpec(provider), Sinteger(Internals::mintWindowHandle(window)));
    }

    static std::string normalizeName(std::string name) {
        if (!name.starts_with("scheme:"))
            name = std::format("scheme:{}", name);
        return name;
    }

    void registerSymbols() {
        Sregister_symbol("hl-scheme-layout-add", (void*)+[](const char* name, ptr spec) -> int {
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
        });
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
        const ptr r = dispatchResize(m_parent, m_provider, targets, delta, sc<int>(corner));
        if (r == Sfalse) {
            const ptr r2 = dispatchRecalculate(m_parent, m_provider, targets);
            if (r2 == Sfalse || !applyBoxes(targets, r2))
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

    // reads a proper list of `count` proper lists of 4 exact integers.
    // all accesses are non-allocating, so no GC can move anything mid-read.
    static bool readBoxList(ptr list, size_t count, std::vector<CBox>& out) {
        for (size_t i = 0; i < count; ++i) {
            if (!Spairp(list))
                return false;

            ptr box = Scar(list);
            list    = Scdr(list);

            int v[4];
            for (int j = 0; j < 4; ++j) {
                if (!Spairp(box))
                    return false;
                const ptr val = Scar(box);
                box           = Scdr(box);
                if (!Sfixnump(val))
                    return false;
                v[j] = (int)Sfixnum_value(val);
            }
            if (box != Snil)
                return false;

            out.emplace_back(v[0], v[1], v[2], v[3]);
        }

        return list == Snil;
    }

    // one dispatcher per callback kind, each calling its own Scheme symbol
    // directly; ids are one real scheme list. returns the callback's return
    // ptr, or Sfalse when the callback is absent or failed.
    // ptr, or Sfalse when the callback is absent or failed.
    static ptr dispatchRecalculate(const WP<Layout::CAlgorithm>& parent, SP<SSchemeLayoutProvider> provider,
                                   const std::vector<SP<Layout::ITarget>>& targets) {
        auto space = parent.lock() ? parent.lock()->space() : nullptr;
        if (!space)
            return Sfalse;

        const auto AREA = space->workArea();

        // payload = (count W H id ...) — the Scheme entry's fixed shape
        std::vector<uintptr_t> vals = {(uintptr_t)targets.size(), (uintptr_t)AREA.w, (uintptr_t)AREA.h};
        for (const auto& t : targets) {
            const auto window = t->window();
            vals.push_back(window ? Internals::mintWindowHandle(window) : 0);
        }

        return callScheme1("hl--layout-recalculate", schemeSpec(provider), schemeIntList(vals));
    }

    static ptr dispatchResize(const WP<Layout::CAlgorithm>& parent, SP<SSchemeLayoutProvider> provider,
                              const std::vector<SP<Layout::ITarget>>& targets, const Vector2D& delta, int corner) {
        auto space = parent.lock() ? parent.lock()->space() : nullptr;
        if (!space)
            return Sfalse;

        const auto AREA = space->workArea();

        // payload = (count W H dx dy corner id ...) — the Scheme entry's fixed shape
        std::vector<uintptr_t> vals = {(uintptr_t)targets.size(), (uintptr_t)AREA.w, (uintptr_t)AREA.h,
                                       (uintptr_t)(int)delta.x, (uintptr_t)(int)delta.y, (uintptr_t)corner};
        for (const auto& t : targets) {
            const auto window = t->window();
            vals.push_back(window ? Internals::mintWindowHandle(window) : 0);
        }

        return callScheme1("hl--layout-resize", schemeSpec(provider), schemeIntList(vals));
    }

    static bool applyBoxes(const std::vector<SP<Layout::ITarget>>& targets, ptr result) {
        std::vector<CBox> boxes;
        if (result == Sfalse || !readBoxList(result, targets.size(), boxes))
            return false;

        for (size_t i = 0; i < targets.size(); ++i)
            targets[i]->setPositionGlobal(boxes[i].noNegativeSize());

        return true;
    }

    bool CSchemeTiledAlgorithm::callLayout(const std::vector<SP<Layout::ITarget>>& targets) {
        if (!m_provider || !m_provider->active)
            return false;

        const ptr r = dispatchRecalculate(m_parent, m_provider, targets);
        if (r == Sfalse || !applyBoxes(targets, r)) {
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

        const ptr r = callScheme1("hl--layout-msg", schemeSpec(m_provider), Sstring_utf8(sv.data(), sv.size()));

        std::string out;
        if (Sstringp(r)) {
            for (iptr i = 0; i < Sstring_length(r); ++i)
                out += (char)Sstring_ref(r, i);
        }

        if (!out.empty())
            return Config::configError(std::format("layout {}: {}", m_provider->name, out), Config::eConfigErrorLevel::ERROR, Config::eConfigErrorCode::INVALID_ARGUMENT);

        recalculate();
        return {};
    }
}
