/*
    The query family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"
#include <src/keybinds/Manager.hpp>
#include <src/pointer/PointerManager.hpp>
#include <cstring>
#include <regex>
#include <src/keybinds/InputState.hpp>
#include <src/state/WorkspaceState.hpp>
#include <src/state/MonitorState.hpp>
#include <src/desktop/history/WorkspaceHistoryTracker.hpp>
#include <src/desktop/history/WindowHistoryTracker.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/state/ViewState.hpp>
#include <src/desktop/state/WindowState.hpp>

#include <src/input/Keys.hpp>
#include <src/desktop/view/window/Window.hpp>
#include <src/plugins/PluginSystem.hpp>

namespace Config::Scheme {

    using namespace Internals;

    static SCM monitorIdResult(PHLMONITOR m); // defined below

    static SCM hlWindowFrom(const char* sel) {
        if (!g_up)
            return SCM_BOOL_F;
        const std::string selector = sel ? sel : "";
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!windowMatchesSelector(w, selector))
                continue;
            const auto id = hl::windowHandle(w);
            return id;
        }
        return SCM_BOOL_F;
    }

    static SCM hlUrgentWindow() {
        if (!g_up)
            return SCM_BOOL_F;
        const auto w = Desktop::viewState()->query().urgent().runWindow();
        if (!w)
            return SCM_BOOL_F;
        const auto id = hl::windowHandle(w);
        return id;
    }

    static SCM hlLastWindow() {
        if (!g_up)
            return SCM_BOOL_F;
        const auto current     = Desktop::focusState()->window();
        const auto& fullHistory = Desktop::History::windowTracker()->fullHistory();
        for (auto it = fullHistory.rbegin(); it != fullHistory.rend(); ++it) {
            const auto candidate = it->lock();
            if (!candidate || !candidate->mapped())
                continue;
            if (current && candidate == current)
                continue;
            const auto id = hl::windowHandle(candidate);
            return id;
        }
        return SCM_BOOL_F;
    }

    static SCM hlMonitorFrom(const char* sel) {
        if (!g_up)
            return SCM_BOOL_F;
        return monitorIdResult(State::monitorState()->query().configString(sel ? sel : "").run());
    }

    static SCM hlMonitorAt(double x, double y) {
        if (!g_up)
            return SCM_BOOL_F;
        return monitorIdResult(State::monitorState()->query().vec(Vector2D{x, y}).run());
    }

    static SCM hlMonitorAtCursor() {
        if (!g_up || !Pointer::mgr())
            return SCM_BOOL_F;
        const auto pos = Pointer::mgr()->untransformedPosition();
        return monitorIdResult(State::monitorState()->query().vec(pos).run());
    }

    static SCM hlActiveMonitor() {
        if (!g_up)
            return SCM_BOOL_F;
        return monitorIdResult(Desktop::focusState()->monitor());
    }

    static SCM hlActiveWorkspace() {
        if (!g_up)
            return SCM_BOOL_F;
        const auto mon = Desktop::focusState()->monitor();
        if (!mon || !mon->m_activeWorkspace)
            return SCM_BOOL_F;
        const auto id = hl::workspaceHandle(mon->m_activeWorkspace);
        return id;
    }

    static SCM hlActiveSpecialWorkspace() {
        if (!g_up)
            return SCM_BOOL_F;
        const auto mon = Desktop::focusState()->monitor();
        if (!mon || !mon->m_activeSpecialWorkspace)
            return SCM_BOOL_F;
        const auto id = hl::workspaceHandle(mon->m_activeSpecialWorkspace);
        return id;
    }

    static SCM hlLastWorkspace() {
        if (!g_up)
            return SCM_BOOL_F;
        const auto mon     = Desktop::focusState()->monitor();
        const auto current = mon ? mon->m_activeWorkspace : nullptr;
        if (!current)
            return SCM_BOOL_F;
        const auto previous = Desktop::History::workspaceTracker()->previousWorkspace(current);
        auto       ws       = previous.workspace.lock();
        if (!ws && previous.target.valid())
            ws = State::Workspace::state()->find(previous.target);
        if (!ws)
            return SCM_BOOL_F;
        const auto id = hl::workspaceHandle(ws);
        return id;
    }

    static int hlIsKeyDown(const char* key) {
        if (!g_up || !Keybinds::mgr())
            return -1;
        const auto sym = xkb_keysym_from_name(key ? key : "", XKB_KEYSYM_CASE_INSENSITIVE);
        if (!sym)
            return -1;
        return Keybinds::mgr()->inputState().isKeysymDown(sym) ? 1 : 0;
    }

    static SCM hlLoadedPlugins() {
        if (!g_up)
            return SCM_BOOL_F;
        SCM l = SCM_EOL;
        for (const auto& p : g_pPluginSystem->getAllPlugins()) {
            if (!p)
                continue;
            const std::string name = p->m_name;
            l = scm_cons(scm_from_utf8_stringn(name.c_str(), name.size()), l);
        }
        return l;
    }

    static SCM hlVersion() {
        return scm_from_utf8_stringn(HYPRLAND_VERSION, strlen(HYPRLAND_VERSION));
    }

    static SCM monitorIdResult(PHLMONITOR m) {
        if (!m)
            return SCM_BOOL_F;
        const auto id = hl::monitorHandle(m);
        return id;
    }

    // selector matching, implemented here: prefixes dispatch to a full-match
    // regex over class/initialclass/title/initialtitle, plus pid:/address:/
    // tag:/stableid: equality and the bare "floating"/"tiled"/"active" forms.
    bool windowMatchesSelector(const PHLWINDOW& w, const std::string& sel) {
        if (!w || !w->mapped())
            return false;
        if (sel.empty() || sel == "active")
            return Desktop::focusState()->window() == w;

        auto       body = sel;
        auto       isFloat = std::optional<bool>{};
        if (body.starts_with("floating")) {
            isFloat = true;
            body    = "active";
        } else if (body.starts_with("tiled")) {
            isFloat = false;
            body    = "active";
        }

        auto matchRegex = [](const std::string& text, const std::string& pattern) {
            try {
                std::regex re(pattern);
                return std::regex_match(text, re);
            } catch (...) { return false; }
        };

        std::string mode, arg;
        const auto& m = body;
        auto strip  = [&m](const std::string& pfx, std::string& out) { if (m.starts_with(pfx)) { out = m.substr(pfx.size()); return true; } return false; };
        if (strip("class:", arg)) { if (!matchRegex(w->metadata().appID(), arg)) return false; }
        else if (strip("initialclass:", arg)) { if (!matchRegex(w->metadata().initialAppID(), arg)) return false; }
        else if (strip("title:", arg)) { if (!matchRegex(w->metadata().title(), arg)) return false; }
        else if (strip("initialtitle:", arg)) { if (!matchRegex(w->metadata().initialTitle(), arg)) return false; }
        else if (strip("pid:", arg)) { if (std::to_string(w->backend().pid()) != arg) return false; }
        else if (strip("address:", arg)) { if (std::format("0x{:x}", rc<uintptr_t>(w.get())) != arg) return false; }
        else if (strip("tag:", arg)) {
            bool tagged = false;
            if (w->m_ruleApplicator)
                for (const auto& t : w->m_ruleApplicator->m_tagKeeper.getTags())
                    if (matchRegex(t, arg)) { tagged = true; break; }
            if (!tagged) return false;
        }

        if (isFloat && w->isFloating() != *isFloat)
            return false;
        return true;
    }

    // this family's Scheme-visible surface
    void registerQuery() {
        hl::bind<hlWindowFrom>("hl--c-window-from");
        hl::bind<hlUrgentWindow>("hl--c-urgent-window");
        hl::bind<hlLastWindow>("hl--c-last-window");
        hl::bind<hlMonitorFrom>("hl--c-monitor-from");
        hl::bind<hlMonitorAt>("hl--c-monitor-at");
        hl::bind<hlMonitorAtCursor>("hl--c-monitor-at-cursor");
        hl::bind<hlActiveMonitor>("hl--c-active-monitor");
        hl::bind<hlActiveWorkspace>("hl--c-active-workspace");
        hl::bind<hlActiveSpecialWorkspace>("hl--c-active-special-workspace");
        hl::bind<hlLastWorkspace>("hl--c-last-workspace");
        hl::bind<hlIsKeyDown>("hl--c-is-key-down");
        hl::bind<hlLoadedPlugins>("hl--c-loaded-plugins");
        hl::bind<hlVersion>("hl--c-version");
    }

} // namespace Config::Scheme
