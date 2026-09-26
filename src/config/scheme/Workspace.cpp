/*
    The workspace family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"

#include <src/desktop/view/Group.hpp>
#include <src/desktop/view/window/Window.hpp>
#include <src/workspace/RegularWorkspace.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/state/WorkspaceState.hpp>
#include <src/state/workspace/Resolver.hpp>
#include <src/helpers/math/Direction.hpp>
#include <src/layout/supplementary/WorkspaceAlgoMatcher.hpp>
#include <src/desktop/view/window/WindowGroupMembership.hpp>
#include <src/layout/algorithm/Algorithm.hpp>
#include <src/layout/space/Space.hpp>
#include <src/managers/fullscreen/FullscreenTypes.hpp>
#include <src/managers/fullscreen/FullscreenController.hpp>
#include <src/state/MonitorState.hpp>
#include <src/desktop/state/WindowState.hpp>

namespace Config::Scheme {

    using namespace Internals;

    static SCM hlSchemeWorkspaceNames() {
        if (!g_up)
            return SCM_BOOL_F;

        // proper scheme data: a real list of ids, not a newline-joined string.
        // (Sinteger is an immediate — only the cons cells need rooting.)
        std::vector<SCM> ids;
        for (const auto& wsRef : State::Workspace::state()->workspaces()) {
            const auto ws = wsRef.lock();
            if (!ws)
                continue;
            ids.push_back(hl::workspaceHandle(wsRef));
        }

        return ids.empty() ? SCM_BOOL_F : hl::listOf(ids);
    }

    PHLWORKSPACE workspaceFromName(const char* name) {
        if (!name || !*name)
            return nullptr;
        return State::Workspace::state()->query().input(std::string(name)).run();
    }

    static int hlSchemeFocusWorkspace(const char* ws) {
        if (!g_up)
            return -1;
        return actionResult("focus-workspace", Config::Actions::changeWorkspace(std::string(ws ? ws : "")));
    }

    static int hlSchemeWorkspaceRename(const char* oldName, const char* newName) {
        if (!g_up)
            return -1;
        const auto ws = workspaceFromName(oldName);
        if (!ws) {
            LOG(Log::ERR, "[scheme] workspace-rename: no workspace named {}", oldName ? oldName : "");
            return -1;
        }
        return actionResult("workspace-rename", Config::Actions::renameWorkspace(ws, std::string(newName ? newName : "")));
    }

    static int hlSchemeWorkspaceMoveMonitor(const char* wsName, const char* monName) {
        if (!g_up)
            return -1;
        const auto ws  = workspaceFromName(wsName);
        const auto mon = monitorFromName(monName);
        if (!ws || !mon) {
            LOG(Log::ERR, "[scheme] workspace-move-to-monitor: no workspace named {} or monitor named {}", wsName ? wsName : "", monName ? monName : "");
            return -1;
        }
        return actionResult("workspace-move-to-monitor", Config::Actions::moveToMonitor(ws, mon));
    }

    static int hlSchemeWorkspaceToggleSpecial(const char* wsName) {
        if (!g_up)
            return -1;
        // a toggle must CREATE the special workspace when it does not exist
        // yet (upstream's dispatcher takes a bare name and creates on
        // demand) — resolving only would make first use impossible
        if (!wsName || !*wsName)
            return -1;
        const auto ws = specialWorkspaceFromName(wsName, Desktop::focusState()->monitor());
        if (!ws)
            return -1;
        return actionResult("workspace-toggle-special", Config::Actions::toggleSpecial(ws));
    }

    static int hlSchemeWorkspaceSwapMonitors(const char* mon1, const char* mon2) {
        if (!g_up)
            return -1;
        const auto a = monitorFromName(mon1);
        const auto b = monitorFromName(mon2);
        if (!a || !b) {
            LOG(Log::ERR, "[scheme] workspace-swap-monitors: no monitor named {} or {}", mon1 ? mon1 : "", mon2 ? mon2 : "");
            return -1;
        }
        return actionResult("workspace-swap-monitors", Config::Actions::swapActiveWorkspaces(a, b));
    }

    // create-or-find a special workspace by (prefixed) selector
    PHLWORKSPACE specialWorkspaceFromName(const std::string& wsName, const PHLMONITOR& mon) {
        std::string sel = wsName;
        if (!sel.starts_with("special:"))
            sel = "special:" + sel;
        auto ws = workspaceFromName(sel.c_str());
        if (!ws) {
            const auto target = State::Workspace::resolver()->getWorkspaceTargetFromString(sel);
            if (!target.valid())
                return nullptr;
            ws = State::Workspace::state()->create(target, mon);
        }
        return ws;
    }

    static SCM hlSchemeWorkspaceGroups(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ws = hl::workspaceOf(id);
        if (!ws)
            return SCM_BOOL_F;
        SCM                                 l = SCM_EOL;
        std::vector<const Desktop::View::CGroup*> pushed;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (w->m_workspace != ws || !w->grouping().group())
                continue;
            const auto* g = w->grouping().group().get();
            if (std::find(pushed.begin(), pushed.end(), g) != pushed.end())
                continue;
            pushed.push_back(g);
            l = scm_cons(hl::groupHandle(w->grouping().group()), l);
        }
        // members were collected head-first: reverse for document order
        SCM out = SCM_EOL;
        for (SCM p = l; scm_is_pair(p); p = scm_cdr(p))
            out = scm_cons(scm_car(p), out);
        return out;
    }

    static int hlWorkspaceChangeId(const char* wsName, double newId) {
        if (!g_up)
            return -1;
        const auto ws = workspaceFromName(wsName);
        if (!ws) {
            g_configError = "no workspace named " + std::string(wsName ? wsName : "");
            return -1;
        }
        return Config::Actions::changeWorkspaceID(ws, sc<int64_t>(newId)) ? 0 : -2;
    }

    // -- workspace getters
    static SCM hlWorkspaceName(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { const auto& s = ws->displayName(); return scm_from_utf8_stringn(s.c_str(), s.size()); });
    }

    static SCM hlWorkspaceAddressableName(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { const auto& s = ws->addressableName(); return scm_from_utf8_stringn(s.c_str(), s.size()); });
    }

    static SCM hlWorkspaceNumber(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM {
            const auto n = ws->numberedID();
            return n ? scm_from_int64(sc<int>(*n)) : SCM_BOOL_F;
        });
    }

    static SCM hlWorkspaceMonitor(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return monitorHandleResult(ws->m_monitor.lock()); });
    }

    static SCM hlWorkspaceSpecial(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return boolResult(ws->type() == Workspace::eWorkspaceType::SPECIAL); });
    }

    static SCM hlWorkspaceActive(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM {
            const auto mon = ws->m_monitor.lock();
            return boolResult(mon && (mon->m_activeWorkspace == ws || mon->m_activeSpecialWorkspace == ws));
        });
    }

    static SCM hlWorkspaceVisible(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return boolResult(ws->visible()); });
    }

    static SCM hlWorkspaceEmpty(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return boolResult(ws->getWindowCount() == 0); });
    }

    static SCM hlWorkspacePersistent(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM {
            const auto REGULAR = dynamicPointerCast<Workspace::CRegularWorkspace>(ws);
            return boolResult(REGULAR && REGULAR->isPersistent());
        });
    }

    static SCM hlWorkspaceHasUrgent(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return boolResult(ws->hasUrgentWindow()); });
    }

    static SCM hlWorkspaceHasFullscreen(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return boolResult(Fullscreen::controller()->hasFullscreen(ws)); });
    }

    static SCM hlWorkspaceFullscreenMode(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return scm_from_int64(sc<int>(Fullscreen::controller()->getFullscreenModes(ws).internal)); });
    }

    static SCM hlWorkspaceFullscreenWindow(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return windowHandleResult(Fullscreen::controller()->getFullscreenWindow(ws)); });
    }

    static SCM hlWorkspaceLastWindow(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return windowHandleResult(ws->getLastFocusedWindow()); });
    }

    static SCM hlWorkspaceWindowCount(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return scm_from_int64(ws->getWindowCount()); });
    }

    static SCM hlWorkspaceGroupCount(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM { return scm_from_int64(ws->getGroups()); });
    }

    static SCM hlWorkspaceTiledLayout(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM {
            std::string layoutName = "unknown";
            const auto  SPACE      = ws->space();
            if (SPACE && SPACE->algorithm() && SPACE->algorithm()->tiledAlgo())
                layoutName = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(SPACE->algorithm()->tiledAlgo().get());
            return scm_from_utf8_stringn(layoutName.c_str(), layoutName.size());
        });
    }

    static SCM hlWorkspaceAlive(SCM id) {
        return boolResult(g_up && hl::workspaceOf(id) != nullptr);
    }

    // identity, mirroring hl-window=?: true iff both handles lock to the same
    // live workspace; dead handles are never "the same" as anything
    static SCM hlWorkspaceSame(SCM a, SCM b) {
        const auto wa = g_up ? hl::workspaceOf(a) : nullptr;
        const auto wb = g_up ? hl::workspaceOf(b) : nullptr;
        return boolResult(wa && wb && wa.get() == wb.get());
    }

    // -- selector bridges: handles are accepted anywhere a selector string is,
    // resolved through the canonical selector exactly like upstream's
    // *SelectorOrObject helpers (LuaBindingsInternal.cpp)
    static SCM hlWorkspaceSelector(SCM id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> SCM {
            const auto s = Workspace::selector(*ws);
            return scm_from_utf8_stringn(s.c_str(), s.size());
        });
    }

    // workspace resolution via the resolver (the query() chain has proven
    // unreliable from the plugin; the resolver+find path is verified)
    PHLWORKSPACE workspaceFromSelector(const std::string& sel) {
        const auto target = State::Workspace::resolver()->getWorkspaceTargetFromString(sel);
        if (!target.valid())
            return nullptr;
        return State::Workspace::state()->find(target);
    }

    // windows on a workspace: newline-joined handle ids (like hl-windows)
    static SCM hlWorkspaceWindows(const char* sel) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ws = workspaceFromSelector(sel ? sel : "");
        if (!ws)
            return SCM_BOOL_F;
        std::vector<SCM> ids;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w->mapped() || w->m_workspace != ws)
                continue;
            ids.push_back(hl::windowHandle(w));
        }
        return ids.empty() ? SCM_BOOL_F : hl::listOf(ids);
    }

    // windows matching a selector: handle ids as a scheme list
    static SCM hlWindowsFrom(const char* sel) {
        if (!g_up)
            return SCM_BOOL_F;
        const std::string selector = sel ? sel : "";
        std::vector<SCM> ids;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!windowMatchesSelector(w, selector))
                continue;
            ids.push_back(hl::windowHandle(w));
        }
        return ids.empty() ? SCM_BOOL_F : hl::listOf(ids);
    }

    // this family's Scheme-visible surface
    void registerWorkspace() {
        hl::bind<hlSchemeWorkspaceNames>("hl--c-workspace-names");
        hl::bind<hlSchemeFocusWorkspace>("hl--c-focus-workspace");
        hl::bind<hlSchemeWorkspaceRename>("hl--c-workspace-rename");
        hl::bind<hlSchemeWorkspaceMoveMonitor>("hl--c-workspace-move-monitor");
        hl::bind<hlSchemeWorkspaceToggleSpecial>("hl--c-workspace-toggle-special");
        hl::bind<hlSchemeWorkspaceSwapMonitors>("hl--c-workspace-swap-monitors");
        hl::bind<hlSchemeWorkspaceGroups>("hl--c-workspace-groups");
        hl::bind<hlWorkspaceChangeId>("hl--c-workspace-change-id");
        hl::bind<hlWorkspaceName>("hl--c-workspace-name");
        hl::bind<hlWorkspaceAddressableName>("hl--c-workspace-addressable-name");
        hl::bind<hlWorkspaceNumber>("hl--c-workspace-number");
        hl::bind<hlWorkspaceMonitor>("hl--c-workspace-monitor");
        hl::bind<hlWorkspaceSpecial>("hl--c-workspace-special");
        hl::bind<hlWorkspaceActive>("hl--c-workspace-active");
        hl::bind<hlWorkspaceVisible>("hl--c-workspace-visible");
        hl::bind<hlWorkspaceEmpty>("hl--c-workspace-empty");
        hl::bind<hlWorkspacePersistent>("hl--c-workspace-persistent");
        hl::bind<hlWorkspaceHasUrgent>("hl--c-workspace-has-urgent");
        hl::bind<hlWorkspaceHasFullscreen>("hl--c-workspace-has-fullscreen");
        hl::bind<hlWorkspaceFullscreenMode>("hl--c-workspace-fullscreen-mode");
        hl::bind<hlWorkspaceFullscreenWindow>("hl--c-workspace-fullscreen-window");
        hl::bind<hlWorkspaceLastWindow>("hl--c-workspace-last-window");
        hl::bind<hlWorkspaceWindowCount>("hl--c-workspace-window-count");
        hl::bind<hlWorkspaceGroupCount>("hl--c-workspace-group-count");
        hl::bind<hlWorkspaceTiledLayout>("hl--c-workspace-tiled-layout");
        hl::bind<hlWorkspaceAlive>("hl--c-workspace-alive");
        hl::bind<hlWorkspaceSame>("hl--c-workspace-same");
        hl::bind<hlWorkspaceSelector>("hl--c-workspace-selector");
        hl::bind<hlWorkspaceWindows>("hl--c-workspace-windows");
        hl::bind<hlWindowsFrom>("hl--c-windows-from");
    }

} // namespace Config::Scheme
