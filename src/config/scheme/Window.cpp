/*
    The window family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"
#include <src/state/workspace/Resolver.hpp>
#include <src/layout/supplementary/WorkspaceAlgoMatcher.hpp>
#include <src/workspace/RegularWorkspace.hpp>
#include <src/state/WorkspaceState.hpp>
#include <src/state/MonitorState.hpp>
#include <src/managers/fullscreen/FullscreenTypes.hpp>
#include <src/managers/fullscreen/FullscreenController.hpp>
#include <src/desktop/view/window/WindowFullscreenPolicy.hpp>
#include <src/desktop/view/window/WindowPresentation.hpp>
#include <src/desktop/view/window/WindowGroupMembership.hpp>
#include <src/desktop/view/window/WindowSwallowController.hpp>
#include <src/desktop/history/WindowHistoryTracker.hpp>
#include <src/desktop/state/ViewState.hpp>
#include <src/desktop/state/WindowState.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <fstream>

#include <src/helpers/math/Direction.hpp>
#include <src/keybinds/Manager.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/desktop/view/Group.hpp>
#include <src/layout/algorithm/tiled/master/MasterAlgorithm.hpp>
#include <src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp>
#include <src/protocols/types/ContentType.hpp>
#include <src/desktop/view/window/Window.hpp>
#include <src/workspace/query/Query.hpp>
#include <src/layout/algorithm/Algorithm.hpp>

namespace Config::Scheme {

    using namespace Internals;

    // called from Scheme as a gsubr: focused window title, or #f
    static SCM hlSchemeActiveTitle() {
        if (!g_up)
            return SCM_BOOL_F;

        const auto window = Desktop::focusState()->window();
        if (!window)
            return SCM_BOOL_F;

        const auto title = window->metadata().title();
        return scm_from_utf8_stringn(title.c_str(), title.size());
    }

    // called from Scheme as a gsubr when the guardian yields a
    // dead handle record: runs the weak ref's destructor (unregisters the
    // observer from the object's control block)
    static SCM hlSchemeActiveWindow() {
        if (!g_up)
            return SCM_BOOL_F;

        const auto window = Desktop::focusState()->window();
        if (!window)
            return SCM_BOOL_F;

        return hl::windowHandle(window);
    }

    static SCM hlSchemeWindowIds() {
        if (!g_up)
            return SCM_BOOL_F;

        std::vector<SCM> ids;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w->mapped())
                continue;
            const auto id = hl::windowHandle(w);
            ids.push_back(id);
        }

        return ids.empty() ? SCM_BOOL_F : hl::listOf(ids);
    }

    static SCM hlSchemeWindowTitle(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;

        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;

        const auto title = window->metadata().title();
        return scm_from_utf8_stringn(title.c_str(), title.size());
    }

    static int hlSchemeWindowAlive(SCM id) {
        if (!g_up)
            return 0;

        return hl::windowOf(id) ? 1 : 0;
    }

    static int hlSchemeWindowClose(SCM id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!scm_is_false(id) && !window)
            return -1;
        if (!window)
            return -1;

        return Config::Actions::closeWindow(window) ? 0 : -2;
    }

    static SCM hlSchemeWindowClass(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        {
            std::ofstream pr("/tmp/hs-sel-debug", std::ios::app);
            auto w = hl::windowOf(id);
            pr << "class(" << id << ") -> " << (w ? w->metadata().appID() : "NULL") << "\n";
        }

        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;

        const auto s = window->metadata().appID();
        return scm_from_utf8_stringn(s.c_str(), s.size());
    }

    static SCM hlSchemeWindowWorkspaceId(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;

        const auto window = hl::windowOf(id);
        if (!window || !window->m_workspace)
            return SCM_BOOL_F;

        const auto wsId = hl::workspaceHandle(window->m_workspace);
        return wsId;
    }

    static SCM hlSchemeWindowMonitorId(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;

        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;

        const auto monitor = State::monitorState()->query().id(window->monitorID()).run();
        if (!monitor)
            return SCM_BOOL_F;

        const auto monId = hl::monitorHandle(monitor);
        return monId;
    }

    static int hlSchemeWindowFloating(SCM id) {
        if (!g_up)
            return 0;

        const auto window = hl::windowOf(id);
        return (window && window->isFloating()) ? 1 : 0;
    }

    static SCM hlSchemeWindowSize(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;

        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;

        const auto sz = window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        return scm_cons(scm_from_int64((int)sz.x), scm_from_int64((int)sz.y));   // (w . h)
    }

    static int hlSchemeWindowPid(SCM id) {
        if (!g_up)
            return -1;

        const auto window = hl::windowOf(id);
        if (!window)
            return -1;

        return (int)window->backend().pid();
    }

    // ---- window read-side fields (LuaWindow.cpp field parity, 2026-09-21) ----
    // upstream's is_master / perc_master / perc_size / index / index_in_column
    // / column all live inside ONE `layout` table — mirrored as the single
    // hl-window-layout plist getter, not five functions.
    static int hlWindowFocusHistoryId(PHLWINDOW wnd) {
        const auto& history = Desktop::History::windowTracker()->fullHistory();
        for (size_t i = 0; i < history.size(); ++i) {
            if (history[i].lock() == wnd)
                return sc<int>(history.size() - i - 1); // reverse order, upstream parity
        }
        return -1;
    }

    static SCM hlSchemeWindowContentType(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        const auto ct = NContentType::toString(window->getContentType());
        return scm_from_utf8_stringn(ct.c_str(), ct.size());
    }

    static SCM hlSchemeWindowStableId(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        const auto sid = std::format("{:x}", window->metadata().stableID());
        return scm_from_utf8_stringn(sid.c_str(), sid.size());
    }

    static SCM hlSchemeWindowTags(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        SCM l = SCM_EOL;
        for (const auto& tag : window->m_ruleApplicator->m_tagKeeper.getTags())
            l = scm_cons(scm_from_utf8_stringn(tag.c_str(), tag.size()), l);
        return l;
    }

    static SCM hlSchemeWindowSwallowingId(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        const auto swallowee = window->swallowing().swallowee();
        if (!swallowee)
            return SCM_BOOL_F;
        const auto winId = hl::windowHandle(swallowee);
        return winId;
    }

    static SCM hlSchemeWindowXdgTag(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        const auto tag = window->backend().metadata().tag;
        if (!tag)
            return SCM_BOOL_F;
        return scm_from_utf8_stringn(tag->c_str(), tag->size());
    }

    static SCM hlSchemeWindowXdgDescription(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        const auto desc = window->backend().metadata().description;
        if (!desc)
            return SCM_BOOL_F;
        return scm_from_utf8_stringn(desc->c_str(), desc->size());
    }

    // upstream's `layout` window field: {name} for plain algos, plus
    // is_master/perc_master/perc_size under master, and a nested column
    // table {index width windows} + index_in_column under scrolling.
    // Mirrored as a plist: (name "master" 'is-master #f 'perc-master 0.5
    // 'perc-size 1.0) | (name "scrolling" 'column (index n width f
    // windows (…)) 'index-in-column n). Stale handle / no algo -> #f.
    static SCM hlSchemeWindowLayout(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;

        const auto target = window->layoutTarget();
        if (!target || target->floating() || !window->m_workspace || !window->m_workspace->space())
            return SCM_BOOL_F;
        const auto& algo = window->m_workspace->space()->algorithm();
        if (!algo || !algo->tiledAlgo())
            return SCM_BOOL_F;
        const auto& tiledAlgo = algo->tiledAlgo();

        const std::string name = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(tiledAlgo.get());
        SCM l = scm_cons(scm_from_locale_symbol("name"), scm_cons(scm_from_utf8_stringn(name.c_str(), name.size()), SCM_EOL));

        if (const auto* master = dynamic_cast<Layout::Tiled::CMasterAlgorithm*>(tiledAlgo.get())) {
            const auto node = master->getNodeFromTarget(target);
            if (node) {
                l = scm_cons(scm_from_locale_symbol("is-master"),
                     scm_cons(node->isMaster ? SCM_BOOL_T : SCM_BOOL_F, l));
                l = scm_cons(scm_from_locale_symbol("perc-master"),
                     scm_cons(scm_from_double(node->percMaster), l));
                l = scm_cons(scm_from_locale_symbol("perc-size"),
                     scm_cons(scm_from_double(node->percSize), l));
            }
        } else if (auto* scrolling = dynamic_cast<Layout::Tiled::CScrollingAlgorithm*>(tiledAlgo.get())) {
            const auto data = scrolling->dataFor(target);
            if (data) {
                const auto col = data->column.lock();
                if (col) {
                    const auto scrollingData = col->scrollingData.lock();
                    SCM column = SCM_EOL;
                    if (scrollingData)
                        column = scm_cons(scm_from_locale_symbol("index"),
                                   scm_cons(scm_from_int64((int)scrollingData->idx(col)), column));
                    column = scm_cons(scm_from_locale_symbol("width"),
                               scm_cons(scm_from_double(col->getColumnWidth()), column));
                    SCM windows = SCM_EOL;
                    for (const auto& td : col->targetDatas) {
                        const auto t = td->target.lock();
                        if (!t)
                            continue;
                        const auto win = t->window();
                        if (!win)
                            continue;
                        const auto winId = hl::windowHandle(win);
                        windows = scm_cons(winId, windows);
                    }
                    column = scm_cons(scm_from_locale_symbol("windows"),
                               scm_cons(windows, column));
                    l = scm_cons(scm_from_locale_symbol("index-in-column"),
                          scm_cons(scm_from_int64((int)col->idx(target)), l));
                    l = scm_cons(scm_from_locale_symbol("column"), scm_cons(column, l));
                }
            }
        }
        return l;
    }

    static int hlSchemeWindowFocus(SCM id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::focus(*window) ? 0 : -2;
    }

    static int hlSchemeWindowFloat(SCM id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_TOGGLE, *window) ? 0 : -2;
    }

    static int hlSchemeWindowMoveToWorkspace(SCM id, const char* name) {
        if (!g_up || !name)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;
        const PHLWINDOW w = *window;

        const auto target = State::Workspace::resolver()->getWorkspaceTargetFromString(name);
        if (!target.valid())
            return -2;

        auto ws = State::Workspace::state()->find(target);
        if (!ws) {
            // create missing workspaces on the window's own monitor
            // (isEmpty=false, like Lua's resolveWorkspaceStr — an empty-flagged
            // workspace gets swept before the window lands in it)
            const auto mon = w->m_workspace ? w->m_workspace->m_monitor.lock() : Desktop::focusState()->monitor();
            ws             = State::Workspace::state()->create(target, mon, false);
        }
        if (!ws)
            return -2;

        return Config::Actions::moveToWorkspace(ws, false, w) ? 0 : -2;
    }

    // toggles the given fullscreen mode (FSMODE_FULLSCREEN=2, FSMODE_MAXIMIZED=1),
    // mirroring the toggle logic in Lua's dsp_fullscreenWindowWithAction
    // true set: 0 = none, 1 = maximized, 2 = fullscreen — regardless of the
    // current state (the toggle above exits when already in the mode)
    static int hlSchemeWindowFullscreenSet(SCM id, int modeRaw) {
        if (!g_up)
            return -1;
        const auto window = actionWindow(id).value_or(nullptr);
        if (!window)
            return -1;
        const auto mode = sc<Fullscreen::eFullscreenMode>(modeRaw);
        return Config::Actions::fullscreenWindow(mode, false, window) ? 0 : -2;
    }

    static int hlSchemeWindowFullscreenToggle(SCM id, int modeRaw) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;
        const PHLWINDOW w = *window;

        const auto mode = sc<Fullscreen::eFullscreenMode>(modeRaw);
        if (Fullscreen::controller()->isFullscreen(w, mode))
            return Config::Actions::fullscreenWindow(Fullscreen::FSMODE_NONE, false, w) ? 0 : -2;

        return Config::Actions::fullscreenWindow(mode, false, w) ? 0 : -2;
    }

    static int hlSchemeWindowFullscreenMode(SCM id) {
        if (!g_up)
            return -1;

        const auto window = hl::windowOf(id);
        if (!window)
            return -1;

        return sc<int>(Fullscreen::controller()->getFullscreenModes(window).internal);
    }

    static int hlSchemeWindowHidden(SCM id) {
        if (!g_up)
            return 0;

        const auto window = hl::windowOf(id);
        return (window && window->isHidden()) ? 1 : 0;
    }

    static int hlSchemeWindowPinned(SCM id) {
        if (!g_up)
            return 0;

        const auto window = hl::windowOf(id);
        return (window && (window->m_state & Desktop::View::WINDOW_STATE_PINNED)) ? 1 : 0;
    }

    static int hlSchemeWindowPseudoQuery(SCM id) {
        if (!g_up)
            return 0;
        const auto window = windowOrFocused(id);
        return (window && window->layoutTarget()->isPseudo()) ? 1 : 0;
    }

    static int hlSchemeWindowMaximizedQuery(SCM id) {
        if (!g_up)
            return 0;
        const auto window = windowOrFocused(id);
        return (window && Fullscreen::controller()->getFullscreenModes(window).internal == Fullscreen::FSMODE_MAXIMIZED) ? 1 : 0;
    }

    static int hlSchemeWindowInGroup(SCM id) {
        if (!g_up)
            return 0;
        const auto window = windowOrFocused(id);
        return (window && window->grouping().group()) ? 1 : 0;
    }

    static int hlSchemeWindowGroupDenied(SCM id) {
        if (!g_up)
            return 0;
        const auto window = windowOrFocused(id);
        if (!window)
            return 0;
        const auto group = window->grouping().group();
        return (group && group->denied()) ? 1 : 0;
    }

    static int hlSchemeWindowGroupLocked(SCM id) {
        if (!g_up)
            return 0;
        const auto window = windowOrFocused(id);
        if (!window)
            return 0;
        const auto group = window->grouping().group();
        return (group && group->locked()) ? 1 : 0;
    }

    // window-scoped group lock: toggle/set the lock on the group of the given
    // window (id 0 → focused). Mirrors upstream lockActiveGroup with the
    // target window explicit instead of always-focused.
    static int hlSchemeWindowGroupLock(SCM id, int act) {
        if (!g_up)
            return -1;
        const auto window = windowOrFocused(id);
        if (!window)
            return -1;
        const auto group = window->grouping().group();
        if (!group)
            return -1;
        switch (act) {
            case 0: group->setLocked(!group->locked()); break;
            case 1: group->setLocked(true); break;
            default: group->setLocked(false); break;
        }
        window->presentation().refreshValues();
        return 0;
    }

    // read back the dynamic window props setProp writes: effective values
    // (defaults included), as #t/#f for booleans, numbers for opacities and
    // border/rounding, and #f for unknown/unsupported prop names.
    static SCM hlSchemeWindowPropGet(SCM id, const char* prop) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = windowOrFocused(id);
        if (!window || !prop || !*prop)
            return SCM_BOOL_F;
        const std::string p = prop;
        auto&             A = *window->m_ruleApplicator;
        if (p == "opacity")
            return scm_from_double(A.alpha().value().alpha);
        if (p == "opacity_inactive")
            return scm_from_double(A.alphaInactive().value().alpha);
        if (p == "opacity_fullscreen")
            return scm_from_double(A.alphaFullscreen().value().alpha);
        if (p == "border_size")
            return scm_from_int64(A.borderSize().value());
        if (p == "rounding")
            return scm_from_int64(A.rounding().value());
#define HL_READ_BOOL(NAME, CNAME)  \
    if (p == NAME)                 \
        return A.CNAME().value() ? SCM_BOOL_T : SCM_BOOL_F;
        HL_READ_BOOL("allows_input", allowsInput)
        HL_READ_BOOL("decorate", decorate)
        HL_READ_BOOL("focus_on_activate", focusOnActivate)
        HL_READ_BOOL("keep_aspect_ratio", keepAspectRatio)
        HL_READ_BOOL("nearest_neighbor", nearestNeighbor)
        HL_READ_BOOL("no_anim", noAnim)
        HL_READ_BOOL("no_blur", noBlur)
        HL_READ_BOOL("no_dim", noDim)
        HL_READ_BOOL("no_focus", noFocus)
        HL_READ_BOOL("no_max_size", noMaxSize)
        HL_READ_BOOL("no_shadow", noShadow)
        HL_READ_BOOL("no_glow", noGlow)
        HL_READ_BOOL("no_wobble", noWobble)
        HL_READ_BOOL("no_shortcuts_inhibit", noShortcutsInhibit)
        HL_READ_BOOL("opaque", opaque)
        HL_READ_BOOL("dim_around", dimAround)
        HL_READ_BOOL("force_rgbx", RGBX)
        HL_READ_BOOL("sync_fullscreen", syncFullscreen)
        HL_READ_BOOL("immediate", tearing)
        HL_READ_BOOL("xray", xray)
        HL_READ_BOOL("render_unfocused", renderUnfocused)
        HL_READ_BOOL("no_follow_mouse", noFollowMouse)
        HL_READ_BOOL("no_screen_share", noScreenShare)
        HL_READ_BOOL("no_vrr", noVRR)
        HL_READ_BOOL("no_auto_hdr", noAutoHDR)
        HL_READ_BOOL("persistent_size", persistentSize)
        HL_READ_BOOL("stay_focused", stayFocused)
        HL_READ_BOOL("no_xdg_drags", noXdgDrags)
#undef HL_READ_BOOL
        return SCM_BOOL_F;
    }

    std::optional<PHLWINDOW> actionWindow(SCM v) {
        if (scm_is_false(v))
            return Desktop::focusState()->window();
        return std::optional<PHLWINDOW>{hl::windowOf(v)};
    }

    // a window argument: a handle, or #f for the focused window (the API's
    // "active window" convention, in one place)
    PHLWINDOW windowOrFocused(SCM v) {
        return scm_is_false(v) ? Desktop::focusState()->window() : hl::windowOf(v);
    }

    static SCM hlSchemeWindowAddress(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        const auto addr = std::format("0x{:x}", reinterpret_cast<uintptr_t>(window.get()));
        return scm_from_utf8_stringn(addr.c_str(), addr.size());
    }

    static int hlSchemeWindowMapped(SCM id) {
        if (!g_up)
            return 0;
        const auto window = hl::windowOf(id);
        return (window && window->mapped()) ? 1 : 0;
    }

    static int hlSchemeWindowVisible(SCM id) {
        if (!g_up)
            return 0;
        const auto window = hl::windowOf(id);
        return (window && window->mapped() && window->acceptsInput() && window->alphaNonZero()) ? 1 : 0;
    }

    static int hlSchemeWindowAcceptsInput(SCM id) {
        if (!g_up)
            return 0;
        const auto window = hl::windowOf(id);
        return (window && window->acceptsInput()) ? 1 : 0;
    }

    static SCM hlSchemeWindowPosition(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        const auto pos = window->position(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        return scm_cons(scm_from_int64((int)pos.x), scm_from_int64((int)pos.y)); // (x . y)
    }

    static int hlSchemeWindowPinFullscreened(SCM id) {
        if (!g_up)
            return 0;
        const auto window = hl::windowOf(id);
        return (window && window->fullscreenPolicy().pinFullscreened()) ? 1 : 0;
    }

    static int hlSchemeWindowAllowedOverFullscreen(SCM id) {
        if (!g_up)
            return 0;
        const auto window = hl::windowOf(id);
        return (window && window->fullscreenPolicy().allowedOverFullscreen()) ? 1 : 0;
    }

    static int hlSchemeWindowTearingHint(SCM id) {
        if (!g_up)
            return 0;
        const auto window = hl::windowOf(id);
        return (window && (window->m_hints & Desktop::View::WINDOW_HINT_TEAR)) ? 1 : 0;
    }

    static int hlSchemeWindowInhibitingIdle(SCM id) {
        if (!g_up)
            return 0;
        const auto window = hl::windowOf(id);
        return (window && g_pInputManager && g_pInputManager->isWindowInhibiting(window, false)) ? 1 : 0;
    }

    static SCM hlSchemeWindowFocusHistoryId(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        return scm_from_int64(hlWindowFocusHistoryId(window));
    }

    static int hlSchemeWindowMoveDirection(SCM id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-move-direction", Config::Actions::moveInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapDirection(SCM id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-direction", Config::Actions::swapInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapNext(SCM id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-next", Config::Actions::swapNext(prev == 0, actionWindow(id)));
    }

    static int hlSchemeWindowSwapWith(SCM id, SCM otherId) {
        if (!g_up)
            return -1;
        const auto other = hl::windowOf(otherId);
        if (!other)
            return -1;
        return actionResult("window-swap-with", Config::Actions::swapWith(other, actionWindow(id)));
    }

    // filter: 0 = all, 1 = tiled only, 2 = floating only
    static int hlSchemeWindowCycle(SCM id, int next, int filter) {
        if (!g_up)
            return -1;
        std::optional<bool> tiled, floating;
        if (filter == 1)
            tiled = true;
        else if (filter == 2)
            floating = true;
        return actionResult("window-cycle", Config::Actions::cycleNext(next != 0, tiled, floating, actionWindow(id)));
    }

    static int hlSchemeWindowCenter(SCM id) {
        if (!g_up)
            return -1;
        return actionResult("window-center", Config::Actions::center(actionWindow(id)));
    }

    static int hlSchemeWindowResizePx(SCM id, double w, double h, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-resize", Config::Actions::resize(Vector2D{w, h}, relative != 0, actionWindow(id)));
    }

    static int hlSchemeWindowMovePx(SCM id, double x, double y, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-move", Config::Actions::move(Vector2D{x, y}, relative != 0, actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowFloatAct(SCM id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-float", Config::Actions::floatWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowPinAct(SCM id, int act) {        if (!g_up)
            return -1;
        return actionResult("window-pin", Config::Actions::pinWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowPseudo(SCM id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-pseudo", Config::Actions::pseudoWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowKill(SCM id) {
        if (!g_up)
            return -1;
        return actionResult("window-kill", Config::Actions::killWindow(actionWindow(id)));
    }

    static int hlSchemeWindowSignal(SCM id, int sig) {
        if (!g_up)
            return -1;
        return actionResult("window-signal", Config::Actions::signalWindow(sig, actionWindow(id)));
    }

    static int hlSchemeWindowZOrder(SCM id, const char* mode) {
        if (!g_up)
            return -1;
        return actionResult("window-zorder", Config::Actions::alterZOrder(std::string(mode ? mode : ""), actionWindow(id)));
    }

    static int hlSchemeWindowSetProp(SCM id, const char* prop, const char* val) {
        if (!g_up)
            return -1;
        return actionResult("window-set-prop", Config::Actions::setProp(std::string(prop ? prop : ""), std::string(val ? val : ""), actionWindow(id)));
    }

    static int hlSchemeWindowTag(SCM id, const char* tag) {
        if (!g_up)
            return -1;
        return actionResult("window-tag", Config::Actions::tag(std::string(tag ? tag : ""), actionWindow(id)));
    }

    static int hlSchemeWindowClearTags(SCM id) {
        if (!g_up)
            return -1;
        return actionResult("window-clear-tags", Config::Actions::clearTags(actionWindow(id)));
    }

    static int hlSchemeToggleSwallow() {
        if (!g_up)
            return -1;
        return actionResult("toggle-swallow", Config::Actions::toggleSwallow());
    }

    static int hlSchemeWindowIntoGroup(SCM id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-group", Config::Actions::moveIntoGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowOutOfGroup(SCM id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-out-of-group", Config::Actions::moveOutOfGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowIntoOrCreateGroup(SCM id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-or-create-group", Config::Actions::moveIntoOrCreateGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowDenyFromGroup(SCM id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-deny-from-group", Config::Actions::denyWindowFromGroup(sc<Config::Actions::eTogglableAction>(act)));
    }

    // explicit fullscreen: internal and client modes, layout-aware flag
    static int hlSchemeWindowFullscreenState(SCM id, int internalMode, int clientMode, int layoutAware) {
        if (!g_up)
            return -1;
        const auto w = actionWindow(id);
        if (!scm_is_false(id) && !w)
            return -1;
        return actionResult("window-fullscreen-state",
            Config::Actions::fullscreenWindow(sc<Fullscreen::eFullscreenMode>(internalMode), sc<Fullscreen::eFullscreenMode>(clientMode), layoutAware != 0, w));
    }

    static SCM hlWindowFullscreenHandler(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;
        const auto name = Fullscreen::controller()->getFullscreenHandlerNameAsString(window);
        return scm_from_utf8_stringn(name.c_str(), name.size());
    }

    static SCM hlSchemeWindowInitialClass(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;

        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;

        const auto s = window->metadata().initialAppID();
        return scm_from_utf8_stringn(s.c_str(), s.size());
    }

    static SCM hlSchemeWindowInitialTitle(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;

        const auto window = hl::windowOf(id);
        if (!window)
            return SCM_BOOL_F;

        const auto s = window->metadata().initialTitle();
        return scm_from_utf8_stringn(s.c_str(), s.size());
    }

    static int hlSchemeWindowX11(SCM id) {
        if (!g_up)
            return 0;

        const auto window = hl::windowOf(id);
        return (window && window->backend().isX11()) ? 1 : 0;
    }

    // identity, mirroring Lua's windowEq: two handles are the same window
    // iff they lock to the same underlying object. Dead handles are never
    // "the same" as anything.
    static int hlSchemeWindowSame(SCM idA, SCM idB) {
        if (!g_up)
            return 0;

        const auto a = hl::windowOf(idA);
        const auto b = hl::windowOf(idB);
        return (a && b && a.get() == b.get()) ? 1 : 0;
    }

    // this family's Scheme-visible surface
    void registerWindow() {
        hl::bind<hlSchemeActiveTitle>("hl--c-active-title");
        hl::bind<hlSchemeActiveWindow>("hl--c-active-window");
        hl::bind<hlSchemeWindowIds>("hl--c-window-ids");
        hl::bind<hlSchemeWindowTitle>("hl--c-window-title");
        hl::bind<hlSchemeWindowAlive>("hl--c-window-alive");
        hl::bind<hlSchemeWindowClose>("hl--c-window-close");
        hl::bind<hlSchemeWindowClass>("hl--c-window-class");
        hl::bind<hlSchemeWindowWorkspaceId>("hl--c-window-workspace-id");
        hl::bind<hlSchemeWindowMonitorId>("hl--c-window-monitor-id");
        hl::bind<hlSchemeWindowFloating>("hl--c-window-floating");
        hl::bind<hlSchemeWindowSize>("hl--c-window-size");
        hl::bind<hlSchemeWindowPid>("hl--c-window-pid");
        hl::bind<hlSchemeWindowAddress>("hl--c-window-address");
        hl::bind<hlSchemeWindowMapped>("hl--c-window-mapped");
        hl::bind<hlSchemeWindowVisible>("hl--c-window-visible");
        hl::bind<hlSchemeWindowAcceptsInput>("hl--c-window-accepts-input");
        hl::bind<hlSchemeWindowPosition>("hl--c-window-position");
        hl::bind<hlSchemeWindowPinFullscreened>("hl--c-window-pin-fullscreened");
        hl::bind<hlSchemeWindowAllowedOverFullscreen>("hl--c-window-allowed-over-fullscreen");
        hl::bind<hlSchemeWindowTearingHint>("hl--c-window-tearing-hint");
        hl::bind<hlSchemeWindowInhibitingIdle>("hl--c-window-inhibiting-idle");
        hl::bind<hlSchemeWindowFocusHistoryId>("hl--c-window-focus-history-id");
        hl::bind<hlSchemeWindowContentType>("hl--c-window-content-type");
        hl::bind<hlSchemeWindowStableId>("hl--c-window-stable-id");
        hl::bind<hlSchemeWindowTags>("hl--c-window-tags");
        hl::bind<hlSchemeWindowSwallowingId>("hl--c-window-swallowing-id");
        hl::bind<hlSchemeWindowXdgTag>("hl--c-window-xdg-tag");
        hl::bind<hlSchemeWindowXdgDescription>("hl--c-window-xdg-description");
        hl::bind<hlSchemeWindowLayout>("hl--c-window-layout");
        hl::bind<hlSchemeWindowFocus>("hl--c-window-focus");
        hl::bind<hlSchemeWindowFloat>("hl--c-window-float");
        hl::bind<hlSchemeWindowMoveToWorkspace>("hl--c-window-move-to-workspace");
        hl::bind<hlSchemeWindowFullscreenSet>("hl--c-window-fullscreen-set");
        hl::bind<hlSchemeWindowFullscreenToggle>("hl--c-window-fullscreen-toggle");
        hl::bind<hlSchemeWindowFullscreenMode>("hl--c-window-fullscreen-mode");
        hl::bind<hlSchemeWindowHidden>("hl--c-window-hidden");
        hl::bind<hlSchemeWindowPinned>("hl--c-window-pinned");
        hl::bind<hlSchemeWindowPseudoQuery>("hl--c-window-pseudo-query");
        hl::bind<hlSchemeWindowMaximizedQuery>("hl--c-window-maximized-query");
        hl::bind<hlSchemeWindowInGroup>("hl--c-window-in-group");
        hl::bind<hlSchemeWindowGroupDenied>("hl--c-window-group-denied");
        hl::bind<hlSchemeWindowGroupLocked>("hl--c-window-group-locked");
        hl::bind<hlSchemeWindowGroupLock>("hl--c-window-group-lock");
        hl::bind<hlSchemeWindowPropGet>("hl--c-window-prop");
        hl::bind<hlSchemeWindowSame>("hl--c-window-same");
        hl::bind<hlSchemeWindowX11>("hl--c-window-x11");
        hl::bind<hlSchemeWindowInitialClass>("hl--c-window-initial-class");
        hl::bind<hlSchemeWindowInitialTitle>("hl--c-window-initial-title");
        hl::bind<hlSchemeWindowFullscreenState>("hl--c-window-fullscreen-state");
        hl::bind<hlWindowFullscreenHandler>("hl--c-window-fullscreen-handler");
        hl::bind<hlSchemeToggleSwallow>("hl--c-toggle-swallow");
        hl::bind<hlSchemeWindowIntoGroup>("hl--c-window-into-group");
        hl::bind<hlSchemeWindowOutOfGroup>("hl--c-window-out-of-group");
        hl::bind<hlSchemeWindowIntoOrCreateGroup>("hl--c-window-into-or-create-group");
        hl::bind<hlSchemeWindowDenyFromGroup>("hl--c-window-deny-from-group");
        hl::bind<hlSchemeWindowMoveDirection>("hl--c-window-move-direction");
        hl::bind<hlSchemeWindowSwapDirection>("hl--c-window-swap-direction");
        hl::bind<hlSchemeWindowSwapNext>("hl--c-window-swap-next");
        hl::bind<hlSchemeWindowSwapWith>("hl--c-window-swap-with");
        hl::bind<hlSchemeWindowCycle>("hl--c-window-cycle");
        hl::bind<hlSchemeWindowCenter>("hl--c-window-center");
        hl::bind<hlSchemeWindowResizePx>("hl--c-window-resize-px");
        hl::bind<hlSchemeWindowMovePx>("hl--c-window-move-px");
        hl::bind<hlSchemeWindowFloatAct>("hl--c-window-float-act");
        hl::bind<hlSchemeWindowPinAct>("hl--c-window-pin-act");
        hl::bind<hlSchemeWindowPseudo>("hl--c-window-pseudo");
        hl::bind<hlSchemeWindowKill>("hl--c-window-kill");
        hl::bind<hlSchemeWindowSignal>("hl--c-window-signal");
        hl::bind<hlSchemeWindowZOrder>("hl--c-window-zorder");
        hl::bind<hlSchemeWindowSetProp>("hl--c-window-set-prop");
        hl::bind<hlSchemeWindowTag>("hl--c-window-tag");
        hl::bind<hlSchemeWindowClearTags>("hl--c-window-clear-tags");
    }

} // namespace Config::Scheme
