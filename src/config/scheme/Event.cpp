/*
    The event family.

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
#include <src/managers/eventLoop/EventLoopManager.hpp>
#include <src/desktop/view/window/Window.hpp>
#include <src/desktop/rule/Rule.hpp>
#include <src/workspace/HLWorkspace.hpp>

namespace Config::Scheme {

    using namespace Internals;

    // lifecycle: the start event is dispatched by an init-time listener
    // (covers the plugin-auto-loaded-at-startup path, where the config load
    // precedes the first render frame).
    static bool                                        g_startSeen      = false;

    static std::vector<SThunkRef>                      g_pendingStart;
    static std::vector<Hyprutils::Signal::CHyprSignalListener> g_lifecycleListeners;
    template <typename T>
    static SCM schemeIntList(const std::vector<T>& vals) {
        if (vals.empty())
            return SCM_EOL;
        SCM l = SCM_EOL;
        for (auto it = vals.rbegin(); it != vals.rend(); ++it)
            l = scm_cons(scm_from_int64(static_cast<long long>(*it)), l);
        return l;
    }

    // fires a handler registered for id with no payload; errors contained
    // numeric payloads (e.g. live gesture update): a real list of numbers
    static SCM schemeIntList(std::initializer_list<int> vals) {
        return schemeIntList(std::vector<int>(vals));
    }

    void fireScheme(SCM record) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        hl::call1(hl::globalRef("hl--event-fire"), record);
        watchdogExit();
    }

    // fires a handler registered for id with a string payload; all errors are
    // contained inside hl--fire-str's guard
    void fireSchemeStr(SCM record, const std::string& arg) {
        if (!g_up)
            return;

        watchdogEnter("handler");
        hl::call2(hl::globalRef("hl--fire-str-rec"), record, scm_from_utf8_stringn(arg.c_str(), arg.size()));
        watchdogExit();
    }

    // fires a handler registered for id with a boolean payload (#t/#f); all
    // errors are contained inside hl--fire-bool's guard
    static void fireSchemeBool(SCM record, bool arg) {
        if (!g_up)
            return;

        watchdogEnter("handler");
        hl::call2(hl::globalRef("hl--fire-bool-rec"), record, arg ? SCM_BOOL_T : SCM_BOOL_F);
        watchdogExit();
    }

    // events carrying window payloads: the window crosses as a fresh handle
    static void fireSchemeWin(SCM record, PHLWINDOW window) {
        if (!g_up || !window)
            return;

        const auto handle = hl::windowHandle(window);

        watchdogEnter("handler");
        hl::call2(hl::globalRef("hl--fire-win-rec"), record, handle);
        watchdogExit();
    }

    // events carrying workspace/monitor payloads: the object crosses as a
    // fresh handle id; a null object crosses as #f. The Ref variant stores
    // the weak ref as-is without locking — used by workspace.removed, which
    // fires mid-destruction (that handle is born dead; see the comment at
    // the listener).
    static void fireSchemeWs(SCM record, PHLWORKSPACE ws) {
        if (!g_up)
            return;

        const auto handle = ws ? hl::workspaceHandle(ws) : SCM_BOOL_F;

        watchdogEnter("handler");
        hl::call2(hl::globalRef("hl--fire-ws-rec"), record, handle);
        watchdogExit();
    }

    static void fireSchemeWsRef(SCM record, PHLWORKSPACEREF ws) {
        if (!g_up)
            return;

        const auto handle = hl::workspaceHandle(ws);

        watchdogEnter("handler");
        hl::call2(hl::globalRef("hl--fire-ws-rec"), record, handle);
        watchdogExit();
    }

    static void fireSchemeMon(SCM record, PHLMONITOR mon) {
        if (!g_up)
            return;

        const auto handle = mon ? hl::monitorHandle(mon) : SCM_BOOL_F;

        watchdogEnter("handler");
        hl::call2(hl::globalRef("hl--fire-mon-rec"), record, handle);
        watchdogExit();
    }

    // two-handle payloads (workspace, monitor); a null object crosses as #f
    static void fireSchemeWsMon(SCM record, PHLWORKSPACE ws, PHLMONITOR mon) {
        if (!g_up)
            return;

        const auto wsHandle  = ws ? hl::workspaceHandle(ws) : SCM_BOOL_F;
        const auto monHandle = mon ? hl::monitorHandle(mon) : SCM_BOOL_F;

        watchdogEnter("handler");
        hl::call3(hl::globalRef("hl--fire-ws-mon-rec"), record, wsHandle, monHandle);
        watchdogExit();
    }

    // bare-thunk/record list fire (gestures, screenshare, keyboard-key)
    static void fireSchemeListRec(SCM record, SCM lst) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        hl::call2(hl::globalRef("hl--fire-list-rec"), record, lst);
        watchdogExit();
    }

    static int hlSchemeWindowEventListen(SCM record, int which) {
        if (!g_up)
            return -1;

        switch (which) {
            case 0: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.openLate.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 1: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.close.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 2: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.title.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 3: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.class_.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 4: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.urgent.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 5: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.pin.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 6: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.fullscreen.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 7: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.moveToWorkspace.listen([ref = SThunkRef(record)](PHLWINDOW w, PHLWORKSPACE ws) { fireSchemeWin(ref.obj, w); })); break;
            case 8: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.active.listen([ref = SThunkRef(record)](PHLWINDOW w, Desktop::eFocusReason) { fireSchemeWin(ref.obj, w); })); break;
            case 9: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.openEarly.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 10: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.kill.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 11: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.bell.listen([ref = SThunkRef(record)](PHLWINDOW w, Event::SCallbackInfo&) { fireSchemeWin(ref.obj, w); })); break;
            case 12: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.updateRules.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            default: break;
        }

        return 0;
    }

    // minimize fires with (window, state): pass the bool as a second arg
    static int hlSchemeWindowMinimizeListen(SCM record) {
        if (!g_up)
            return -1;

        SThunkRef ref(record);
        g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.minimize.listen([ref](PHLWINDOW w, bool state) {
            if (!g_up || !w)
                return;
            const auto winId = hl::windowHandle(w);
            hl::call3(hl::globalRef("hl--fire-win-state-rec"), ref.obj, winId, state ? SCM_BOOL_T : SCM_BOOL_F);
        }));
        return 0;
    }

    // lifecycle: 0 = start (session's first render frame), 1 = shutdown
    // (the exit action). Matches upstream: a handler registered after start
    // already fired (only possible when the plugin itself loaded before the
    // first frame) runs on the next loop pass.
    static int hlSchemeLifecycleListen(SCM record, int which) {
        if (!g_up)
            return -1;

        SThunkRef ref(record);

        if (which == 0) {
            if (g_startSeen) {
                // the handler is registered by Scheme only AFTER this call
                // returns, so the immediate fire must wait for the next pass
                if (g_pEventLoopManager)
                    g_pEventLoopManager->doLater([ref] { fireScheme(ref.obj); });
            } else
                g_pendingStart.emplace_back(record);
        } else
            g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.exit.listen([ref] { fireScheme(ref.obj); }));

        return 0;
    }

    static int hlSchemeMonitorListen(SCM record, int which) {
        if (!g_up)
            return -1;
        switch (which) {
            case 0: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.monitor.added.listen([ref = SThunkRef(record)](PHLMONITOR m) { fireSchemeMon(ref.obj, m); })); break;
            case 1: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.monitor.removed.listen([ref = SThunkRef(record)](PHLMONITOR m) { fireSchemeMon(ref.obj, m); })); break;
            case 2: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.monitor.focused.listen([ref = SThunkRef(record)](PHLMONITOR m) { fireSchemeMon(ref.obj, m); })); break;
            case 3: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.monitor.layoutChanged.listen([ref = SThunkRef(record)] { fireScheme(ref.obj); })); break;
            default: break;
        }
        return 0;
    }

    static int hlSchemeWorkspaceListen(SCM record, int which) {
        if (!g_up)
            return -1;
        switch (which) {
            case 0: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.workspace.created.listen([ref = SThunkRef(record)](PHLWORKSPACEREF ws) { auto w = ws.lock(); if (w) fireSchemeWs(ref.obj, w); })); break;
            // removed fires from ~CHLWorkspace: the payload cannot be locked
            // (hyprutils marks the impl "destroying"), but the data pointer
            // stays valid until the destructor returns, so the name COULD be
            // read through it — the same members the destructor itself just
            // formatted into its IPC events.
            //
            // Decision (2026-09-18): this event delivers a born-dead handle
            // whose getters all return #f, exactly matching upstream Lua (an
            // expired object reads all-nil). The name is retrievable at this
            // instant — a later enhancement could snapshot it into the handle
            // at fire time and expose it through (hl-workspace-name), but that
            // would go beyond what Lua offers, so it is deliberately not done.
            case 1: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.workspace.removed.listen([ref = SThunkRef(record)](PHLWORKSPACEREF ws) { fireSchemeWsRef(ref.obj, ws); })); break;
            // fires (ws mon) handles; ws is #f when no special workspace is
            // open on the monitor (upstream crosses nil the same way)
            case 2: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.workspace.specialActive.listen([ref = SThunkRef(record)](PHLWORKSPACE ws, PHLMONITOR mon) { fireSchemeWsMon(ref.obj, ws, mon); })); break;
            case 3: g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.workspace.moveToMonitor.listen([ref = SThunkRef(record)](PHLWORKSPACE ws, PHLMONITOR mon) { fireSchemeWsMon(ref.obj, ws, mon); })); break;
            default: break;
        }
        return 0;
    }

    static int hlSchemeConfigReloadedListen(SCM record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.config.reloaded.listen([ref = SThunkRef(record)] { fireScheme(ref.obj); }));
        return 0;
    }

    // config.preReload — upstream maps config.unload onto it
    static int hlSchemeConfigUnloadListen(SCM record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.config.preReload.listen([ref = SThunkRef(record)] { fireScheme(ref.obj); }));
        return 0;
    }

    // window.destroy: zero-argument callback (upstream delivers nil — the bus
    // event is Event<PHLWINDOWREF>; identity belongs to the window-close notification)
    static int hlSchemeWindowDestroyListen(SCM record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.window.destroy.listen([ref = SThunkRef(record)](PHLWINDOWREF) { fireScheme(ref.obj); }));
        return 0;
    }

    // screenshare.state — callbacks receive (active? type name); upstream
    // dispatches 3 positional args (LuaEventHandler.cpp:166)
    static int hlSchemeScreenshareListen(SCM record) {
        if (!g_up)
            return -1;
        g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.screenshare.state.listen([ref = SThunkRef(record)](bool state, uint8_t type, const std::string& name) {
            if (!g_up)
                return;
            std::vector<SCM> roots;
            SCM nm = scm_from_utf8_stringn(name.c_str(), name.size());
            hl::pin(nm, roots);
            SCM lst = scm_cons(state ? SCM_BOOL_T : SCM_BOOL_F, scm_cons(scm_from_int64((int)type), scm_cons(nm, SCM_EOL)));
            hl::pin(lst, roots);
            hl::unpin(roots);
            fireSchemeListRec(ref.obj, lst);   // (active? type name)
        }));
        return 0;
    }

    // input.keyboard.key — high-frequency (every key event); handlers must be
    // trivial. Observe-only: the bus event is Cancellable, Scheme listeners
    // never take the cancellation. keycode is +8 (libinput → xkb), as upstream.
    static int hlSchemeKeyboardKeyListen(SCM record) {
        if (!g_up)
            return -1;
        g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.input.keyboard.key.listen([ref = SThunkRef(record)](const IKeyboard::SKeyEvent& keyEvent, Event::SCallbackInfo& _) {
            if (!g_up)
                return;
            fireSchemeListRec(ref.obj, schemeIntList({(int)keyEvent.keycode + 8, (int)keyEvent.timeMs, (int)keyEvent.state}));
        }));
        return 0;
    }

    // layer.opened / layer.closed — callbacks receive the layer's namespace
    // handler receives #t when the prop refresh ran as scheduled, #f when it
    // was executed prematurely
    static int hlSchemePropsRefreshedListen(SCM record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.config.props_refreshed.listen([ref = SThunkRef(record)](const bool scheduled) { fireSchemeBool(ref.obj, scheduled); }));
        return 0;
    }

    static int hlSchemeWorkspaceActiveListen(SCM record) {
        if (!g_up)
            return -1;

        SThunkRef ref(record);
        g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.workspace.active.listen([ref](PHLWORKSPACE ws) {
            fireSchemeWs(ref.obj, ws);
        }));
        return 0;
    }

    /*
        The host's side of the event plumbing.

        The start dispatch is an init-time listener that fires exactly once, on
        the session's first render frame: handlers subscribed before then are
        held in g_pendingStart and released here (the plugin-auto-loaded-at-
        startup path, where the config load precedes that frame). Both the first
        attach and a soft re-attach install it, so it is one function rather
        than the same block written twice.
    */
    void registerStartDispatch() {
        g_lifecycleListeners.emplace_back(Event::bus()->m_events.start.listen([] {
            g_startSeen = true;
            auto pending = std::move(g_pendingStart);
            g_pendingStart.clear();
            for (const auto& ref : pending)
                fireScheme(ref.obj);
        }));
    }

    // the reload boundary: user handlers re-register with the new generation.
    // Wholesale BY DESIGN — each entry's destruction also unlocks its record.
    void dropEventHandlers() {
        g_eventConnections.clear();
    }

    // plugin teardown: also the host's own long-lived listeners, whose closures
    // point into this .so
    void shutdownEvents() {
        g_lifecycleListeners.clear();
        g_eventConnections.clear();
    }

    // this family's Scheme-visible surface
    void registerEvent() {
        hl::bind<hlSchemeWindowEventListen>("hl--c-window-event-listen");
        hl::bind<hlSchemeWindowMinimizeListen>("hl--c-window-minimize-listen");
        hl::bind<hlSchemeLifecycleListen>("hl--c-lifecycle-listen");
        hl::bind<hlSchemeMonitorListen>("hl--c-monitor-event-listen");
        hl::bind<hlSchemeWorkspaceListen>("hl--c-workspace-event-listen");
        hl::bind<hlSchemeConfigReloadedListen>("hl--c-config-reloaded-listen");
        hl::bind<hlSchemeConfigUnloadListen>("hl--c-config-unload-listen");
        hl::bind<hlSchemeWindowDestroyListen>("hl--c-window-destroy-listen");
        hl::bind<hlSchemeScreenshareListen>("hl--c-screenshare-listen");
        hl::bind<hlSchemeKeyboardKeyListen>("hl--c-keyboard-key-listen");
        hl::bind<hlSchemePropsRefreshedListen>("hl--c-config-props-refreshed-listen");
        hl::bind<hlSchemeWorkspaceActiveListen>("hl--c-workspace-active-listen");
    }

} // namespace Config::Scheme
