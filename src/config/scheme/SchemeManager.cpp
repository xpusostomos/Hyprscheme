#ifndef HYPRTHEME_SCHEME_H_SEEN
#define HYPRTHEME_SCHEME_H_SEEN
#include <scheme.h>
#endif
#include "SThunkRef.hpp"
#include "SchemeManager.hpp"
#include "SchemeLayout.hpp"
#include "SchemeInternals.hpp"



#include <src/debug/log/Logger.hpp>
#include <src/debug/crash/CrashReporter.hpp>
#include <src/event/EventBus.hpp>
#include <src/helpers/memory/Memory.hpp>
#include <src/helpers/math/Direction.hpp>
#include <src/input/Keys.hpp>
#include <src/ipc/s1/S1.hpp>
#include <src/ipc/s2/S2.hpp>
#include <src/keybinds/Manager.hpp>
#include <src/keybinds/InputState.hpp>
#include <src/managers/eventLoop/EventLoopManager.hpp>
#include <src/managers/eventLoop/EventLoopTimer.hpp>
#include <src/managers/fullscreen/FullscreenController.hpp>
#include <src/managers/fullscreen/FullscreenTypes.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/pointer/PointerManager.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/state/WindowState.hpp>
#include <src/desktop/view/Group.hpp>
#include <src/desktop/view/LayerSurface.hpp>
#include <src/desktop/view/window/WindowPresentation.hpp>
#include <src/desktop/state/ViewState.hpp>
#include <src/desktop/history/WindowHistoryTracker.hpp>
#include <src/desktop/history/WorkspaceHistoryTracker.hpp>
#include <src/layout/algorithm/tiled/master/MasterAlgorithm.hpp>
#include <src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp>
#include <src/protocols/types/ContentType.hpp>
#include <src/desktop/view/window/Window.hpp>
#include <src/desktop/view/window/WindowFullscreenPolicy.hpp>
#include <src/desktop/view/window/WindowGroupMembership.hpp>
#include <src/desktop/view/window/WindowSwallowController.hpp>
#include <src/desktop/reserved/ReservedArea.hpp>
#include <src/desktop/rule/Engine.hpp>
#include <src/desktop/rule/Rule.hpp>
#include <src/desktop/rule/RuleWithEffects.hpp>
#include <src/desktop/rule/layerRule/LayerRule.hpp>
#include <src/desktop/rule/layerRule/LayerRuleApplicator.hpp>
#include <src/desktop/view/LayerSurface.hpp>
#include <src/notification/Notification.hpp>
#include <src/notification/NotificationOverlay.hpp>
#include <src/managers/input/trackpad/TrackpadGestures.hpp>
#include <src/managers/input/trackpad/gestures/ITrackpadGesture.hpp>
#include <src/managers/input/trackpad/gestures/WorkspaceSwipeGesture.hpp>
#include <src/managers/input/trackpad/gestures/MoveGesture.hpp>
#include <src/managers/input/trackpad/gestures/ResizeGesture.hpp>
#include <src/managers/input/trackpad/gestures/CloseGesture.hpp>
#include <src/managers/input/trackpad/gestures/FloatGesture.hpp>
#include <src/managers/input/trackpad/gestures/FullscreenGesture.hpp>
#include <src/managers/input/trackpad/gestures/SpecialWorkspaceGesture.hpp>
#include <src/managers/input/trackpad/gestures/CursorZoomGesture.hpp>
#include <src/managers/input/trackpad/gestures/ScrollMoveGesture.hpp>
#include <src/state/MonitorState.hpp>
#include <src/state/WorkspaceState.hpp>
#include <src/state/workspace/Resolver.hpp>
#include <src/workspace/HLWorkspace.hpp>
#include <src/workspace/RegularWorkspace.hpp>
#include <src/workspace/query/Query.hpp>
#include <src/layout/supplementary/WorkspaceAlgoMatcher.hpp>
#include <src/layout/space/Space.hpp>
#include <src/layout/algorithm/Algorithm.hpp>
#include <src/config/ConfigManager.hpp>
#include <src/config/lua/ConfigManager.hpp>
#include <src/config/lua/types/LuaConfigValue.hpp>
#include <aquamarine/backend/Backend.hpp>
#include <src/config/lua/types/LuaConfigBool.hpp>
#include <src/config/lua/types/LuaConfigCssGap.hpp>
#include <src/config/lua/types/LuaConfigFloat.hpp>
#include <src/config/lua/types/LuaConfigInt.hpp>
#include <src/config/lua/types/LuaConfigString.hpp>
#include <src/config/shared/actions/ConfigActions.hpp>
#include <src/config/shared/animation/AnimationTree.hpp>
#include <src/config/shared/monitor/Parser.hpp>
#include <src/config/shared/monitor/MonitorRuleManager.hpp>
#include <src/config/shared/workspace/WorkspaceRule.hpp>
#include <src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <src/config/supplementary/executor/Executor.hpp>
#include <src/config/supplementary/propRefresher/PropRefresher.hpp>
#include <src/config/lua/types/LuaConfigValue.hpp>
#include <src/animation/AnimationManager.hpp>
#include <src/pointer/PointerManager.hpp>
#include <src/plugins/PluginSystem.hpp>
#include <xkbcommon/xkbcommon.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstring>
#include <regex>
#include <format>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <vector>

using namespace Config::Scheme::Internals;
using namespace Hyprutils::OS;
using Hyprutils::OS::CFileDescriptor;

/*
    Bootstrap: the Scheme machinery (prelude + bootstrap) lives in real .scm
    files installed NEXT TO THE PLUGIN, located through the plugin's own load
    path (dladdr on a known symbol). Loaded in three phases:

      1. hyprscheme-prelude.scm, via bare load, unguarded. It only contains
         the error plumbing (verified, static); an error here means the
         embedded error handler exits.
      2. hyprscheme-defun.scm, through the prelude's guarded hl--load: the
         defun machinery (function metadata + describe). It must precede
         the bootstrap because converted API functions use `defun`.
      3. hyprscheme-bootstrap.scm, also through hl--load: the API itself.
         Any error prints and leaves hl--ready #f; the C side then disables
         scheme instead of continuing half-initialized.

    All phases evaluate into the PERSISTENT environment (the interpreter's
    interaction environment), which holds the API and the cross-reload
    state forever. Each config generation is then evaluated in its own
    FRESH environment — a copy made by hl--reset — so definitions from
    previous generations cannot leak into new ones (mirroring upstream's
    per-generation lua_State). Machinery variables the config may set!
    (hl--watchdog-ms) are read through the generation copy so overrides
    reach them; hl--state is the one deliberate cross-generation bridge.
*/



namespace Config::Scheme::Internals {
    bool g_up          = false; // interpreter + bootstrap ready
    static std::string g_configError; // last config error, read via c-hl-config-last-error

    // ---- object handles -------------------------------------------------------
    // A handle is a heap-allocated weak ref; the Scheme record carries its
    // address (an integer), and a guardian deletes the object when the
    // record dies — see the collect-request-handler install at the end of
    // the bootstrap. Stale == lock() == nullptr. There is NO registry: the
    // handle itself is the only state. Upstream parity: Lua userdata embed
    // the same weak ref and the Lua GC hook destructs it; our guardian is
    // that hook.
    struct IHandle {
        virtual ~IHandle() = default;
    };
    template <typename W>
    struct SHandle : IHandle {
        W wp;
        SHandle() = default;
        template <typename T>
        explicit SHandle(T o) : wp(o) {}
    };

    // shared with SchemeLayout.cpp (declared in SchemeInternals.hpp)
    uintptr_t mintWindowHandle(PHLWINDOW window) {
        return (uintptr_t)(new SHandle<PHLWINDOWREF>(window));
    }
}

namespace Config::Scheme {

    using namespace Internals;

    static std::string                g_configPath;
    // submap registration context: binds created while set are scoped to it
    static std::string                g_regSubmap;
    static std::string                g_regSubmapReset;
    static int                        g_watchFd       = -1;    // inotify fd; dup'd into event loop waiters
    // scheme timers: C++ owns the CEventLoopTimer, Scheme owns the closure
    // (the handler closures travel inside the connections, locked)
    // event subscriptions: C++ owns the listener handles (dropping one
    // unsubscribes); Scheme owns the handler closures by id
    // event connections: record address -> the subscription that keeps the
    // handler plugged into the bus (lost ptr = unregistered, per the listen
    // contract). Entries die three ways: hl-event-cancel!, the reload clear
    // (generation boundary = handler lifetime), plugin teardown.
    static std::unordered_map<uintptr_t, Hyprutils::Signal::CHyprSignalListener> g_eventConnections;

    // lifecycle: the start event is dispatched by an init-time listener
    // (covers the plugin-auto-loaded-at-startup path, where the config load
    // precedes the first render frame).
    static bool                                        g_startSeen      = false;
    static std::vector<SThunkRef>                      g_pendingStart;
    static std::vector<Hyprutils::Signal::CHyprSignalListener> g_lifecycleListeners;


    // ---- the callback watchdog --------------------------------------------------
    // A detector thread: scheme callbacks run on the main loop, so a hung one
    // freezes everything. We can't safely kill a Chez call, but we can SAY SO —
    // one loud log line + notification per overrun.

    static std::atomic<bool>        g_watchdogRun{false};
    static std::atomic<int64_t>     g_callbackStartMs{0};
    static std::atomic<const char*> g_callbackWhat{""};

    static int64_t watchdogNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    static void watchdogEnter(const char* what) {
        {
            std::ofstream pr("/tmp/hs-wd-probe", std::ios::app);
            pr << "enter " << what << "\n";
        }
        g_callbackWhat = what;
        g_callbackStartMs = watchdogNowMs();
    }

    static void watchdogExit() {
        g_callbackStartMs = 0;
    }

    static void startWatchdog() {
        if (g_watchdogRun.exchange(true))
            return;
        std::thread([] {
            bool reported = false;
            while (g_watchdogRun) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                const auto start = g_callbackStartMs.load();
                if (start == 0) {
                    reported = false;
                    continue;
                }
                const auto elapsed = watchdogNowMs() - start;
                if (elapsed > 5000 && !reported) {
                    reported = true;
                    // LOG ONLY — the notification overlay is not thread-safe and
                    // aborts when poked from a side thread (verified: signal 6)
                    LOG(Log::ERR, "[scheme] watchdog: callback '{}' has been running for {}ms — the compositor is likely frozen by it", g_callbackWhat.load(),
                        elapsed);
                }
            }
        }).detach();
    }

    // defined below (device/config section); used by the bind result reader
    static std::string schemeDatumToStr(ptr p);

    static Keybinds::SBindResult fireSchemeBind(int id) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        watchdogEnter("bind callback");
        // hl--bind-fire: #f = declined (thunk returned #f, or error/watchdog);
        // otherwise the normalized result plist (or a non-plist truthy value
        // for plain callbacks)
        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--bind-fire")), Sinteger(id));
        watchdogExit();
        if (r == Sfalse)
            return {.success = false, .error = "scheme keybind callback declined"};
        if (r == Strue || !Spairp(r) || !Ssymbolp(Scar(r)))
            return {}; // handled — 'ok defaults to true

        // result-table return (upstream dispatchResultFromLua parity):
        // {ok, pass-event, request-release, error}
        Keybinds::SBindResult res;
        ptr l = r;
        while (Spairp(l) && Spairp(Scdr(l))) {
            const std::string k = schemeDatumToStr(Scar(l));
            const ptr        v  = Scar(Scdr(l));
            if (k == "ok") {
                if (v == Sfalse)
                    res.success = false;
            } else if (k == "pass-event") {
                if (v == Strue)
                    res.passEvent = true;
            } else if (k == "request-release") {
                if (v == Strue)
                    res.followUp = Keybinds::BIND_FOLLOW_UP_TRIGGER_RELEASE;
            } else if (k == "error" && Sstringp(v)) {
                res.error = schemeDatumToStr(v);
            }
            l = Scdr(Scdr(l));
        }
        return res;
    }

    // fires a handler registered for id with no payload; errors contained
    // numeric payloads (e.g. live gesture update): a real list of numbers
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

    static ptr schemeIntList(std::initializer_list<int> vals) {
        return schemeIntList(std::vector<int>(vals));
    }

    // record+pin every heap object a call creates; release all when the value
    // is built (a moving GC can then never invalidate a pointer mid-build).
    static void marshRoot(ptr p, std::vector<ptr>& roots) {
        Slock_object(p);
        roots.push_back(p);
    }
    static void marshRelease(std::vector<ptr>& roots) {
        for (auto it = roots.rbegin(); it != roots.rend(); ++it)
            Sunlock_object(*it);
    }

    static void fireScheme(ptr record) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        Scall1(Stop_level_value(Sstring_to_symbol("hl--event-fire")), record);
        watchdogExit();
    }

    // called from Scheme via foreign-procedure; flags are raw eBindFlags bits,
    // assembled Scheme-side from the options plist
    // ---- bind handles ---------------------------------------------------------
    // A bind's Scheme handle is an hl-bind RECORD (token list + thunk). The
    // capture inside the CBind carries an SThunkRef to it (SchemeInternals.hpp):
    // locked while any copy lives, released when the bind is destroyed. There
    // is NO bind registry on our side: Hyprland's keybind registry is the only
    // list. The record's pinned address, stamped into the bind's argument
    // metadata at registration, identifies our binds for precise unbind and
    // plugin shutdown.
    static std::string bindTag(ptr record) {
        return "scheme:" + std::to_string(reinterpret_cast<uintptr_t>(record));
    }

    static Keybinds::SBindResult fireSchemeBindRec(ptr record); // defined below

    static int hlSchemeBind(ptr record, ptr tokens, int flags, const char* desc, ptr devices) {
        if (!g_up)
            return -1;

        SThunkRef ref(record); // lock FIRST: the record is a GC root from here on

        std::vector<std::string> keys;
        for (ptr p = tokens; Spairp(p) && p != Snil; p = Scdr(p)) {
            ptr elem = Scar(p);
            if (Sstringp(elem)) {
                std::string tok;
                for (iptr i = 0; i < Sstring_length(elem); ++i)
                    tok += Sstring_ref(elem, i);
                keys.emplace_back(std::move(tok));
            }
        }

        Keybinds::SExtraBindArgs args;
        std::string display_key;
        for (const auto& k : keys) {
            if (!display_key.empty()) display_key += ' ';
            display_key += k;
        }
        args.metadata.displayKey = display_key;
        args.metadata.argument   = bindTag(record);
        if (desc && *desc)
            args.metadata.description = desc;
        args.metadata.submap      = g_regSubmap;
        args.metadata.submapReset = g_regSubmapReset;
        // devices arrive as a real list of name strings; a non-list element
        // or a non-string device is rejected
        for (ptr p = devices; Spairp(p) && p != Snil; p = Scdr(p)) {
            if (!Sstringp(Scar(p))) {
                g_configError = "hl-bind-add!: 'devices must be a list of device name strings";
                return -1;
            }
            args.devices.emplace(schemeDatumToStr(Scar(p)));
        }

        auto bind = Keybinds::CBind::make(std::move(keys), sc<Keybinds::BindFlags>(flags), [ref] { return fireSchemeBindRec(ref.obj); }, std::move(args));
        if (!bind) {
            LOG(Log::ERR, "[scheme] bind failed: {}", bind.error());
            return -1;
        }

        // no registry of our own: the manager owns the bind from here; its
        // eventual destruction (unbind, reload clearBinds, shutdown) runs the
        // capture destructor, which releases the record lock
        (void)Keybinds::mgr()->addBind(std::move(*bind));
        return 0;
    }

    // fires a RECORD bind (the capture passes the locked record); the Scheme
    // trampoline extracts the thunk. Result contract identical to the id path.
    static Keybinds::SBindResult fireSchemeBindRec(ptr record) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        watchdogEnter("bind callback");
        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--bind-fire-rec")), record);
        watchdogExit();
        if (r == Sfalse)
            return {.success = false, .error = "scheme keybind callback declined"};
        if (r == Strue || !Spairp(r) || !Ssymbolp(Scar(r)))
            return {}; // handled — 'ok defaults to true

        Keybinds::SBindResult res;
        ptr l = r;
        while (Spairp(l) && Spairp(Scdr(l))) {
            const std::string k = schemeDatumToStr(Scar(l));
            const ptr        v  = Scar(Scdr(l));
            if (k == "ok") {
                if (v == Sfalse)
                    res.success = false;
            } else if (k == "pass-event") {
                if (v == Strue)
                    res.passEvent = true;
            } else if (k == "request-release") {
                if (v == Strue)
                    res.followUp = Keybinds::BIND_FOLLOW_UP_TRIGGER_RELEASE;
            } else if (k == "error" && Sstringp(v)) {
                res.error = schemeDatumToStr(v);
            }
            l = Scdr(Scdr(l));
        }
        return res;
    }

    // called from Scheme via foreign-procedure: remove one scheme bind
    static int hlSchemeUnbindKey(const char* key) {
        if (!g_up || !Keybinds::mgr() || !key || !*key)
            return -1;
        // coarse: removes EVERY bind whose display key matches (upstream
        // hl.unbind parity); precise removal goes through hlSchemeUnbindRec
        const auto removed = Keybinds::mgr()->removeBinds(key);
        return removed > 0 ? 0 : -1;
    }

    // precise: find OUR bind by the argument tag (the record's pinned
    // address) and removeBind it — exactly one match, however many same-key
    // siblings exist
    static int hlSchemeUnbindRec(ptr record) {
        if (!g_up || !Keybinds::mgr())
            return -1;
        const std::string tag = bindTag(record);
        for (const Keybinds::PBind& b : Keybinds::mgr()->registry().binds())
            if (b && b->metadata().argument == tag) {
                Keybinds::mgr()->removeBind(b);
                return 0;
            }
        return -1; // not registered (already unbound, or a stale handle)
    }

    // fires a handler registered for id with a string payload; all errors are
    // contained inside hl--fire-str's guard
    static void fireSchemeStr(ptr record, const std::string& arg) {
        if (!g_up)
            return;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-str-rec")), record, Sstring_utf8(arg.c_str(), arg.size()));
        watchdogExit();
    }

    // fires a handler registered for id with a boolean payload (#t/#f); all
    // errors are contained inside hl--fire-bool's guard
    static void fireSchemeBool(ptr record, bool arg) {
        if (!g_up)
            return;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-bool-rec")), record, arg ? Strue : Sfalse);
        watchdogExit();
    }

    // events carrying window payloads: the window crosses as a fresh handle id
    static void fireSchemeWin(ptr record, PHLWINDOW window) {
        if (!g_up || !window)
            return;

        const auto winId = (uintptr_t)(new SHandle<PHLWINDOWREF>(window));

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-win-rec")), record, Sinteger(winId));
        watchdogExit();
    }

    static PHLWORKSPACE workspaceFromId(long long id) {
        return reinterpret_cast<SHandle<PHLWORKSPACEREF>*>(id)->wp.lock();
    }

    static PHLMONITOR monitorFromId(long long id) {
        return reinterpret_cast<SHandle<PHLMONITORREF>*>(id)->wp.lock();
    }

    // events carrying workspace/monitor payloads: the object crosses as a
    // fresh handle id; a null object crosses as #f. The Ref variant stores
    // the weak ref as-is without locking — used by workspace.removed, which
    // fires mid-destruction (that handle is born dead; see the comment at
    // the listener).
    static void fireSchemeWs(ptr record, PHLWORKSPACE ws) {
        if (!g_up)
            return;

        const auto wsId = ws ? (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws)) : 0;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-ws-rec")), record, wsId ? Sinteger(wsId) : Sfalse);
        watchdogExit();
    }

    static void fireSchemeWsRef(ptr record, PHLWORKSPACEREF ws) {
        if (!g_up)
            return;

        const auto wsId = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws));

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-ws-rec")), record, Sinteger(wsId));
        watchdogExit();
    }

    static void fireSchemeMon(ptr record, PHLMONITOR mon) {
        if (!g_up)
            return;

        const auto monId = mon ? (uintptr_t)(new SHandle<PHLMONITORREF>(mon)) : 0;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-mon-rec")), record, monId ? Sinteger(monId) : Sfalse);
        watchdogExit();
    }

    // two-handle payloads (workspace, monitor); a null object crosses as #f
    static void fireSchemeWsMon(ptr record, PHLWORKSPACE ws, PHLMONITOR mon) {
        if (!g_up)
            return;

        const auto wsId  = ws ? (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws)) : 0;
        const auto monId = mon ? (uintptr_t)(new SHandle<PHLMONITORREF>(mon)) : 0;

        watchdogEnter("handler");
        Scall3(Stop_level_value(Sstring_to_symbol("hl--fire-ws-mon-rec")), record, wsId ? Sinteger(wsId) : Sfalse, monId ? Sinteger(monId) : Sfalse);
        watchdogExit();
    }

    // called from Scheme via foreign-procedure; repeat != 0 re-arms forever
    // (or until the callback errors, which stops zombie loops)
    // the timer index: record address -> live timer state. Entries erase
    // themselves — a one-shot removes its entry in its own completion
    // callback, reload tears the rest down — so the map only ever holds
    // timers that can still fire. No ids, no global timer list: the event
    // loop owns the CEventLoopTimer, the capture owns the record lock.
    struct STimerEntry {
        SP<CEventLoopTimer> timer;
        int                 repeat;
        uint64_t            ms; // current interval (set-timeout re-tunes it)
    };
    static std::unordered_map<uintptr_t, STimerEntry> g_timerIndex;

    // timer-record fire: extract (hl-timer-thunk b) and run it zero-arg
    // under the bind result protocol (repeating timers stop on ok #f)
    static Keybinds::SBindResult fireSchemeTimerRec(ptr record) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        watchdogEnter("bind callback");
        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--timer-fire")), record);
        watchdogExit();
        if (r == Sfalse)
            return {.success = false, .error = "scheme keybind callback declined"};
        if (r == Strue || !Spairp(r) || !Ssymbolp(Scar(r)))
            return {};
        Keybinds::SBindResult res;
        ptr l = r;
        while (Spairp(l) && Spairp(Scdr(l))) {
            const std::string k = schemeDatumToStr(Scar(l));
            const ptr        v  = Scar(Scdr(l));
            if (k == "ok") {
                if (v == Sfalse)
                    res.success = false;
            } else if (k == "pass-event") {
                if (v == Strue)
                    res.passEvent = true;
            } else if (k == "request-release") {
                if (v == Strue)
                    res.followUp = Keybinds::BIND_FOLLOW_UP_TRIGGER_RELEASE;
            } else if (k == "error" && Sstringp(v)) {
                res.error = schemeDatumToStr(v);
            }
            l = Scdr(Scdr(l));
        }
        return res;
    }

    static STimerEntry* timerByRecord(ptr record); // defined below (timer control)

    static int hlSchemeTimer(ptr record, int ms, int repeat) {
        if (!g_up || !g_pEventLoopManager || ms < 0)
            return -1;

        // the capture carries the record LOCKED (SThunkRef): the thunk stays
        // alive while the timer can fire; when the timer object is destroyed
        // (completion below, or reload teardown), the lock releases.
        SThunkRef ref(record);

        auto shared = makeShared<CEventLoopTimer>(std::chrono::milliseconds(ms),
            [ref, ms, repeat](SP<CEventLoopTimer> self, void*) {
                const auto result = fireSchemeTimerRec(ref.obj);

                if (repeat && result.success) {
                    // re-arm at the CURRENT interval — set-timeout re-tunes
                    // the index entry, the captured ms is only the initial one
                    uint64_t cur = ms;
                    if (const auto* e = timerByRecord(ref.obj))
                        cur = e->ms;
                    self->updateTimeout(std::chrono::milliseconds(sc<int64_t>(cur)));
                    return;
                }

                // one-shot done, or the callback failed: tear down.
                // onTimerFire dispatches over a copy of the timer list, so
                // removing ourselves here is safe.
                self->cancel();
                if (g_pEventLoopManager)
                    g_pEventLoopManager->removeTimer(self);
                g_timerIndex.erase(reinterpret_cast<uintptr_t>(ref.obj));
            },
            nullptr);

        g_pEventLoopManager->addTimer(shared);
        g_timerIndex.emplace(reinterpret_cast<uintptr_t>(record),
                             STimerEntry{shared, repeat, sc<uint64_t>(ms)});
        return 0;
    }

    // called from Scheme via foreign-procedure: focused window title, or #f
    static ptr hlSchemeActiveTitle() {
        if (!g_up)
            return Sfalse;

        const auto window = Desktop::focusState()->window();
        if (!window)
            return Sfalse;

        const auto title = window->metadata().title();
        return Sstring_utf8(title.c_str(), title.size());
    }

    // called from Scheme via foreign-procedure: newline-joined workspace
    // display names, or #f. split on the Scheme side.
    // ---- FFI marshalling: proper scheme data, no newline-string shovelling ----
    // Chez's GC runs at allocation points (Scons/Sstring allocate), and its
    // moving collector relocates heap objects. Building a list from C must
    // therefore root the objects held across allocations. Sinteger is an
    // immediate (immune); cons cells and strings/flonums are heap objects and
    // get Slock_object-pinned until the value is complete. Releasing just
    // before the return is safe: nothing allocates in between, and the FFI
    // return re-roots the result.

    static ptr hlSchemeWorkspaceNames() {
        if (!g_up)
            return Sfalse;

        // proper scheme data: a real list of ids, not a newline-joined string.
        // (Sinteger is an immediate — only the cons cells need rooting.)
        std::vector<uintptr_t> ids;
        for (const auto& wsRef : State::Workspace::state()->workspaces()) {
            const auto ws = wsRef.lock();
            if (!ws)
                continue;
            ids.push_back((uintptr_t)(new SHandle<PHLWORKSPACEREF>(wsRef)));
        }

        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    // called from Scheme via foreign-procedure: subscribe to submap changes
    static int hlSchemeSubmapListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.keybinds.submap.listen([ref = SThunkRef(record)](const std::string& name) {
            fireSchemeStr(ref.obj, name);
        }));
        return 0;
    }

    // resolves a handle id to the live window, or null if stale (dead or
    // unknown). lazily drops dead entries while we're here.
    static std::optional<PHLWINDOW> actionWindow(long long id);

    // called from Scheme via foreign-procedure when the guardian yields a
    // dead handle record: runs the weak ref's destructor (unregisters the
    // observer from the object's control block)
    static int hlHandleFree(unsigned long long addr) {
        // implausible addresses (corruption, truncation bugs) are skipped and
        // logged rather than crashed on; valid user-space pointers are < 2^47
        if ((addr >> 47) != 0) {
            LOG(Log::ERR, "[scheme] handle free: implausible address {:#x} — skipping", addr);
            return -1;
        }
        delete reinterpret_cast<IHandle*>(addr);
        return 0;
    }

    static PHLWINDOW windowFromId(long long id) {
        return reinterpret_cast<SHandle<PHLWINDOWREF>*>(id)->wp.lock();
    }

    static double hlSchemeActiveWindowId() {
        if (!g_up)
            return -1;

        const auto window = Desktop::focusState()->window();
        if (!window)
            return -1;

        return (double)(uintptr_t)(new SHandle<PHLWINDOWREF>(window));
    }

    static ptr hlSchemeWindowIds() {
        if (!g_up)
            return Sfalse;

        std::vector<uintptr_t> ids;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w->mapped())
                continue;
            const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
            ids.push_back(id);
        }

        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static ptr hlSchemeWindowTitle(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto title = window->metadata().title();
        return Sstring_utf8(title.c_str(), title.size());
    }

    static int hlSchemeWindowAlive(long long id) {
        if (!g_up)
            return 0;

        return windowFromId(id) ? 1 : 0;
    }

    static int hlSchemeWindowClose(long long id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (id >= 0 && !window)
            return -1;
        if (!window)
            return -1;

        return Config::Actions::closeWindow(window) ? 0 : -2;
    }

    static ptr hlSchemeWindowClass(long long id) {
        if (!g_up)
            return Sfalse;
        {
            std::ofstream pr("/tmp/hs-sel-debug", std::ios::app);
            auto w = windowFromId(id);
            pr << "class(" << id << ") -> " << (w ? w->metadata().appID() : "NULL") << "\n";
        }

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().appID();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static ptr hlSchemeWindowWorkspaceId(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window || !window->m_workspace)
            return Sfalse;

        const auto wsId = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(window->m_workspace));
        return Sinteger(wsId);
    }

    static ptr hlSchemeWindowMonitorId(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto monitor = State::monitorState()->query().id(window->monitorID()).run();
        if (!monitor)
            return Sfalse;

        const auto monId = (uintptr_t)(new SHandle<PHLMONITORREF>(monitor));
        return Sinteger(monId);
    }

    static int hlSchemeWindowFloating(long long id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->isFloating()) ? 1 : 0;
    }

    static ptr hlSchemeWindowSize(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto sz = window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        return Scons(Sinteger((int)sz.x), Sinteger((int)sz.y));   // (w . h)
    }

    static int hlSchemeWindowPid(long long id) {
        if (!g_up)
            return -1;

        const auto window = windowFromId(id);
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

    static ptr hlSchemeWindowAddress(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto addr = std::format("0x{:x}", reinterpret_cast<uintptr_t>(window.get()));
        return Sstring_utf8(addr.c_str(), addr.size());
    }

    static int hlSchemeWindowMapped(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->mapped()) ? 1 : 0;
    }

    static int hlSchemeWindowVisible(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->mapped() && window->acceptsInput() && window->alphaNonZero()) ? 1 : 0;
    }

    static int hlSchemeWindowAcceptsInput(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->acceptsInput()) ? 1 : 0;
    }

    static ptr hlSchemeWindowPosition(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto pos = window->position(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        return Scons(Sinteger((int)pos.x), Sinteger((int)pos.y)); // (x . y)
    }

    static int hlSchemeWindowPinFullscreened(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->fullscreenPolicy().pinFullscreened()) ? 1 : 0;
    }

    static int hlSchemeWindowAllowedOverFullscreen(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->fullscreenPolicy().allowedOverFullscreen()) ? 1 : 0;
    }

    static int hlSchemeWindowTearingHint(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && (window->m_hints & Desktop::View::WINDOW_HINT_TEAR)) ? 1 : 0;
    }

    static int hlSchemeWindowInhibitingIdle(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && g_pInputManager && g_pInputManager->isWindowInhibiting(window, false)) ? 1 : 0;
    }

    static ptr hlSchemeWindowFocusHistoryId(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        return Sinteger(hlWindowFocusHistoryId(window));
    }

    static ptr hlSchemeWindowContentType(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto ct = NContentType::toString(window->getContentType());
        return Sstring_utf8(ct.c_str(), ct.size());
    }

    static ptr hlSchemeWindowStableId(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto sid = std::format("{:x}", window->metadata().stableID());
        return Sstring_utf8(sid.c_str(), sid.size());
    }

    static ptr hlSchemeWindowTags(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        ptr l = Snil;
        for (const auto& tag : window->m_ruleApplicator->m_tagKeeper.getTags())
            l = Scons(Sstring_utf8(tag.c_str(), tag.size()), l);
        return l;
    }

    static ptr hlSchemeWindowSwallowingId(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto swallowee = window->swallowing().swallowee();
        if (!swallowee)
            return Sfalse;
        const auto winId = (uintptr_t)(new SHandle<PHLWINDOWREF>(swallowee));
        return Sinteger(winId);
    }

    static ptr hlSchemeWindowXdgTag(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto tag = window->backend().metadata().tag;
        if (!tag)
            return Sfalse;
        return Sstring_utf8(tag->c_str(), tag->size());
    }

    static ptr hlSchemeWindowXdgDescription(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto desc = window->backend().metadata().description;
        if (!desc)
            return Sfalse;
        return Sstring_utf8(desc->c_str(), desc->size());
    }

    // upstream's `layout` window field: {name} for plain algos, plus
    // is_master/perc_master/perc_size under master, and a nested column
    // table {index width windows} + index_in_column under scrolling.
    // Mirrored as a plist: (name "master" 'is-master #f 'perc-master 0.5
    // 'perc-size 1.0) | (name "scrolling" 'column (index n width f
    // windows (…)) 'index-in-column n). Stale handle / no algo -> #f.
    static ptr hlSchemeWindowLayout(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto target = window->layoutTarget();
        if (!target || target->floating() || !window->m_workspace || !window->m_workspace->space())
            return Sfalse;
        const auto& algo = window->m_workspace->space()->algorithm();
        if (!algo || !algo->tiledAlgo())
            return Sfalse;
        const auto& tiledAlgo = algo->tiledAlgo();

        const std::string name = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(tiledAlgo.get());
        ptr l = Scons(Sstring_to_symbol("name"), Scons(Sstring_utf8(name.c_str(), name.size()), Snil));

        if (const auto* master = dynamic_cast<Layout::Tiled::CMasterAlgorithm*>(tiledAlgo.get())) {
            const auto node = master->getNodeFromTarget(target);
            if (node) {
                l = Scons(Sstring_to_symbol("is-master"),
                     Scons(node->isMaster ? Strue : Sfalse, l));
                l = Scons(Sstring_to_symbol("perc-master"),
                     Scons(Sflonum(node->percMaster), l));
                l = Scons(Sstring_to_symbol("perc-size"),
                     Scons(Sflonum(node->percSize), l));
            }
        } else if (auto* scrolling = dynamic_cast<Layout::Tiled::CScrollingAlgorithm*>(tiledAlgo.get())) {
            const auto data = scrolling->dataFor(target);
            if (data) {
                const auto col = data->column.lock();
                if (col) {
                    const auto scrollingData = col->scrollingData.lock();
                    ptr column = Snil;
                    if (scrollingData)
                        column = Scons(Sstring_to_symbol("index"),
                                   Scons(Sinteger((int)scrollingData->idx(col)), column));
                    column = Scons(Sstring_to_symbol("width"),
                               Scons(Sflonum(col->getColumnWidth()), column));
                    ptr windows = Snil;
                    for (const auto& td : col->targetDatas) {
                        const auto t = td->target.lock();
                        if (!t)
                            continue;
                        const auto win = t->window();
                        if (!win)
                            continue;
                        const auto winId = (uintptr_t)(new SHandle<PHLWINDOWREF>(win));
                        windows = Scons(Sinteger(winId), windows);
                    }
                    column = Scons(Sstring_to_symbol("windows"),
                               Scons(windows, column));
                    l = Scons(Sstring_to_symbol("index-in-column"),
                          Scons(Sinteger((int)col->idx(target)), l));
                    l = Scons(Sstring_to_symbol("column"), Scons(column, l));
                }
            }
        }
        return l;
    }

    static int hlSchemeWindowFocus(long long id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::focus(*window) ? 0 : -2;
    }

    static int hlSchemeWindowFloat(long long id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_TOGGLE, *window) ? 0 : -2;
    }

    static int hlSchemeWindowMoveToWorkspace(long long id, const char* name) {
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
    static int hlSchemeWindowFullscreenSet(long long id, int modeRaw) {
        if (!g_up)
            return -1;
        const auto window = actionWindow(id).value_or(nullptr);
        if (!window)
            return -1;
        const auto mode = sc<Fullscreen::eFullscreenMode>(modeRaw);
        return Config::Actions::fullscreenWindow(mode, false, window) ? 0 : -2;
    }

    static int hlSchemeWindowFullscreenToggle(long long id, int modeRaw) {
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

    static int hlSchemeWindowFullscreenMode(long long id) {
        if (!g_up)
            return -1;

        const auto window = windowFromId(id);
        if (!window)
            return -1;

        return sc<int>(Fullscreen::controller()->getFullscreenModes(window).internal);
    }

    static int hlSchemeWindowHidden(long long id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->isHidden()) ? 1 : 0;
    }

    static int hlSchemeWindowPinned(long long id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && (window->m_state & Desktop::View::WINDOW_STATE_PINNED)) ? 1 : 0;
    }

    // ---- state queries for the toggle/set family -----------------------------
    // Each pair (hl-window-*-set!) has a matching hl-window-*-? reading the
    // effective state from the same source the setter writes.

    // windowHandle: scheme ids 0+ map to registry windows, -1 to the focused
    // window (same convention as actionWindow below)
    static PHLWINDOW windowFromSchemeId(long long id) {
        return id >= 0 ? windowFromId(id) : Desktop::focusState()->window();
    }

    static int hlSchemeWindowPseudoQuery(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        return (window && window->layoutTarget()->isPseudo()) ? 1 : 0;
    }

    static int hlSchemeWindowMaximizedQuery(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        return (window && Fullscreen::controller()->getFullscreenModes(window).internal == Fullscreen::FSMODE_MAXIMIZED) ? 1 : 0;
    }

    static int hlSchemeWindowInGroup(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        return (window && window->grouping().group()) ? 1 : 0;
    }

    static int hlSchemeWindowGroupDenied(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        if (!window)
            return 0;
        const auto group = window->grouping().group();
        return (group && group->denied()) ? 1 : 0;
    }

    static int hlSchemeWindowGroupLocked(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        if (!window)
            return 0;
        const auto group = window->grouping().group();
        return (group && group->locked()) ? 1 : 0;
    }

    static int hlSchemeGroupsLocked() {
        if (!g_up)
            return 0;
        return Desktop::windowState()->groupsLocked() ? 1 : 0;
    }

    // window-scoped group lock: toggle/set the lock on the group of the given
    // window (id 0 → focused). Mirrors upstream lockActiveGroup with the
    // target window explicit instead of always-focused.
    static int hlSchemeWindowGroupLock(long long id, int act) {
        if (!g_up)
            return -1;
        const auto window = windowFromSchemeId(id);
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
    static ptr hlSchemeWindowPropGet(long long id, const char* prop) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromSchemeId(id);
        if (!window || !prop || !*prop)
            return Sfalse;
        const std::string p = prop;
        auto&             A = *window->m_ruleApplicator;
        if (p == "opacity")
            return Sflonum(A.alpha().value().alpha);
        if (p == "opacity_inactive")
            return Sflonum(A.alphaInactive().value().alpha);
        if (p == "opacity_fullscreen")
            return Sflonum(A.alphaFullscreen().value().alpha);
        if (p == "border_size")
            return Sinteger(A.borderSize().value());
        if (p == "rounding")
            return Sinteger(A.rounding().value());
#define HL_READ_BOOL(NAME, CNAME)  \
    if (p == NAME)                 \
        return A.CNAME().value() ? Strue : Sfalse;
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
        return Sfalse;
    }

    // ---- actions: the dispatcher surface --------------------------------------
    // Each handler wraps one Config::Actions call — the same layer the Lua
    // hl.dsp.* dispatchers use. Window args take a scheme handle id, or -1
    // for the active window.

    static std::optional<PHLWINDOW> actionWindow(long long id) {
        if (id >= 0)
            return windowFromId(id);
        return Desktop::focusState()->window();
    }

    static int actionResult(const char* name, Config::Actions::ActionResult result) {
        if (result)
            return 0;
        LOG(Log::ERR, "[scheme] {} failed: {}", name, result.error().message);
        return -2;
    }

    static Math::eDirection actionDir(const char* s) {
        return Math::fromChar(s && *s ? s[0] : 'x');
    }

    static PHLMONITOR monitorFromName(const char* name) {
        if (!name || !*name)
            return nullptr;
        for (const auto& m : State::monitorState()->monitors())
            if (m->m_name == name)
                return m;
        return nullptr;
    }

    static PHLWORKSPACE workspaceFromName(const char* name) {
        if (!name || !*name)
            return nullptr;
        return State::Workspace::state()->query().input(std::string(name)).run();
    }

    static int hlSchemeFocusWorkspace(const char* ws) {
        if (!g_up)
            return -1;
        return actionResult("focus-workspace", Config::Actions::changeWorkspace(std::string(ws ? ws : "")));
    }

    static int hlSchemeFocusDirection(const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("focus-direction", Config::Actions::moveFocus(actionDir(dir)));
    }

    static int hlSchemeFocusMonitor(const char* name) {
        if (!g_up)
            return -1;
        const auto mon = monitorFromName(name);
        if (!mon) {
            LOG(Log::ERR, "[scheme] focus-monitor: no monitor named {}", name ? name : "");
            return -1;
        }
        return actionResult("focus-monitor", Config::Actions::focusMonitor(mon));
    }

    static int hlSchemeFocusLast() {
        if (!g_up)
            return -1;
        return actionResult("focus-last", Config::Actions::focusCurrentOrLast());
    }

    static int hlSchemeFocusUrgent() {
        if (!g_up)
            return -1;
        return actionResult("focus-urgent", Config::Actions::focusUrgentOrLast());
    }

    static int hlSchemeWindowMoveDirection(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-move-direction", Config::Actions::moveInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapDirection(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-direction", Config::Actions::swapInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapNext(long long id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-next", Config::Actions::swapNext(prev == 0, actionWindow(id)));
    }

    static int hlSchemeWindowSwapWith(long long id, long long otherId) {
        if (!g_up)
            return -1;
        const auto other = windowFromId(otherId);
        if (!other)
            return -1;
        return actionResult("window-swap-with", Config::Actions::swapWith(other, actionWindow(id)));
    }

    // filter: 0 = all, 1 = tiled only, 2 = floating only
    static int hlSchemeWindowCycle(long long id, int next, int filter) {
        if (!g_up)
            return -1;
        std::optional<bool> tiled, floating;
        if (filter == 1)
            tiled = true;
        else if (filter == 2)
            floating = true;
        return actionResult("window-cycle", Config::Actions::cycleNext(next != 0, tiled, floating, actionWindow(id)));
    }

    static int hlSchemeWindowCenter(long long id) {
        if (!g_up)
            return -1;
        return actionResult("window-center", Config::Actions::center(actionWindow(id)));
    }

    static int hlSchemeWindowResizePx(long long id, double w, double h, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-resize", Config::Actions::resize(Vector2D{w, h}, relative != 0, actionWindow(id)));
    }

    static int hlSchemeWindowMovePx(long long id, double x, double y, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-move", Config::Actions::move(Vector2D{x, y}, relative != 0, actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowFloatAct(long long id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-float", Config::Actions::floatWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowPinAct(long long id, int act) {        if (!g_up)
            return -1;
        return actionResult("window-pin", Config::Actions::pinWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowPseudo(long long id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-pseudo", Config::Actions::pseudoWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowKill(long long id) {
        if (!g_up)
            return -1;
        return actionResult("window-kill", Config::Actions::killWindow(actionWindow(id)));
    }

    static int hlSchemeWindowSignal(long long id, int sig) {
        if (!g_up)
            return -1;
        return actionResult("window-signal", Config::Actions::signalWindow(sig, actionWindow(id)));
    }

    static int hlSchemeWindowZOrder(long long id, const char* mode) {
        if (!g_up)
            return -1;
        return actionResult("window-zorder", Config::Actions::alterZOrder(std::string(mode ? mode : ""), actionWindow(id)));
    }

    static int hlSchemeWindowSetProp(long long id, const char* prop, const char* val) {
        if (!g_up)
            return -1;
        return actionResult("window-set-prop", Config::Actions::setProp(std::string(prop ? prop : ""), std::string(val ? val : ""), actionWindow(id)));
    }

    static int hlSchemeWindowTag(long long id, const char* tag) {
        if (!g_up)
            return -1;
        return actionResult("window-tag", Config::Actions::tag(std::string(tag ? tag : ""), actionWindow(id)));
    }

    static int hlSchemeWindowClearTags(long long id) {
        if (!g_up)
            return -1;
        return actionResult("window-clear-tags", Config::Actions::clearTags(actionWindow(id)));
    }

    static int hlSchemeToggleSwallow() {
        if (!g_up)
            return -1;
        return actionResult("toggle-swallow", Config::Actions::toggleSwallow());
    }

    static int hlSchemeGroupToggle(long long id) {
        if (!g_up)
            return -1;
        return actionResult("group-toggle", Config::Actions::toggleGroup(actionWindow(id)));
    }

    // explicit set: a no-op when the window is already in the requested
    // state (group() is null iff the window is not a member of a group)
    static int hlSchemeGroupSet(long long id, int on) {
        if (!g_up)
            return -1;
        const auto w = actionWindow(id).value_or(nullptr);
        if (!w)
            return -1;
        if ((w->grouping().group() != nullptr) == (on != 0))
            return 0;
        return actionResult("group-set", Config::Actions::toggleGroup(w));
    }

    static int hlSchemeGroupCycle(long long id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("group-cycle", Config::Actions::changeGroupActive(prev == 0, actionWindow(id)));
    }

    static int hlSchemeGroupIndex(long long id, int index) {
        if (!g_up)
            return -1;
        return actionResult("group-index", Config::Actions::setGroupActive(index, actionWindow(id)));
    }

    static int hlSchemeGroupMoveWindow(long long id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("group-move-window", Config::Actions::moveGroupWindow(prev == 0));
    }

    static int hlSchemeGroupLock(int act) {
        if (!g_up)
            return -1;
        return actionResult("group-lock", Config::Actions::lockGroups(sc<Config::Actions::eTogglableAction>(act)));
    }

    static int hlSchemeGroupLockActive(int act) {
        if (!g_up)
            return -1;
        return actionResult("group-lock-active", Config::Actions::lockActiveGroup(sc<Config::Actions::eTogglableAction>(act)));
    }

    static int hlSchemeWindowIntoGroup(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-group", Config::Actions::moveIntoGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowOutOfGroup(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-out-of-group", Config::Actions::moveOutOfGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowIntoOrCreateGroup(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-or-create-group", Config::Actions::moveIntoOrCreateGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowDenyFromGroup(long long id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-deny-from-group", Config::Actions::denyWindowFromGroup(sc<Config::Actions::eTogglableAction>(act)));
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

    // create-or-find a special workspace by (prefixed) selector
    static PHLWORKSPACE specialWorkspaceFromName(const std::string& wsName, const PHLMONITOR& mon) {
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

    // explicit set: opens the special workspace on the monitor (creating it
    // when missing); an empty name closes whatever is open there
    static int hlSchemeMonitorSetSpecial(const char* monSel, const char* wsName) {
        if (!g_up)
            return -1;
        const auto mon = State::monitorState()->query().configString(monSel ? monSel : "").run();
        if (!mon)
            return -1;
        PHLWORKSPACE ws = nullptr;
        if (wsName && *wsName) {
            ws = specialWorkspaceFromName(wsName, mon);
            if (!ws)
                return -1;
        }
        mon->setSpecialWorkspace(ws, true);
        return 0;
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

    static int hlSchemeCursorMove(double x, double y) {
        if (!g_up)
            return -1;
        return actionResult("cursor-move", Config::Actions::moveCursor(Vector2D{x, y}));
    }

    static int hlSchemeCursorCorner(long long id, int corner) {
        if (!g_up)
            return -1;
        return actionResult("cursor-move-to-corner", Config::Actions::moveCursorToCorner(corner, actionWindow(id)));
    }

    static int hlSchemeExit() {
        if (!g_up)
            return -1;
        return actionResult("exit", Config::Actions::exit());
    }

    static int hlSchemeReloadConfig() {
        if (!g_up)
            return -1;
        return actionResult("reload-config", Config::Actions::reloadConfig());
    }

    static int hlSchemeForceRendererReload() {
        if (!g_up)
            return -1;
        return actionResult("force-renderer-reload", Config::Actions::forceRendererReload());
    }

    static int hlSchemeDpms(int act, const char* monName) {
        if (!g_up)
            return -1;
        std::optional<PHLMONITOR> mon;
        if (monName && *monName) {
            mon = monitorFromName(monName);
            if (!mon) {
                LOG(Log::ERR, "[scheme] dpms: no monitor named {}", monName);
                return -1;
            }
        }
        return actionResult("dpms", Config::Actions::dpms(sc<Config::Actions::eTogglableAction>(act), mon));
    }

    static int hlSchemeForceIdle(double seconds) {
        if (!g_up)
            return -1;
        return actionResult("force-idle", Config::Actions::forceIdle(sc<float>(seconds)));
    }

    static int hlSchemeGlobal(const char* action) {
        if (!g_up)
            return -1;
        return actionResult("global", Config::Actions::global(std::string(action ? action : "")));
    }

    static int hlSchemeEvent(const char* data) {
        if (!g_up)
            return -1;
        return actionResult("event", Config::Actions::event(std::string(data ? data : "")));
    }

    static int hlSchemePass(long long id) {
        if (!g_up)
            return -1;
        return actionResult("pass", Config::Actions::pass(actionWindow(id)));
    }

    static std::optional<Input::ModifierMask> modsMaskFromTokens(ptr mods); // defined below

    // mods arrive as a LIST of modifier tokens (as built by hl-kbd/hl-key),
    // the same shape every mods-taking API takes; only the key is resolved
    // from its string name here
    static int hlSchemeSendShortcut(ptr mods, const char* key, long long id) {
        if (!g_up)
            return -1;
        const auto mask = modsMaskFromTokens(mods);
        if (!mask)
            return -1;
        const auto sym = xkb_keysym_from_name(key ? key : "", XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == 0)
            return -1;
        return actionResult("send-shortcut", Config::Actions::pass(*mask, sc<uint32_t>(sym), actionWindow(id)));
    }

    static int hlSchemeSendKeyState(ptr mods, const char* key, int state, long long id) {
        if (!g_up)
            return -1;
        const auto mask = modsMaskFromTokens(mods);
        if (!mask)
            return -1;
        const auto sym = xkb_keysym_from_name(key ? key : "", XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == 0)
            return -1;
        return actionResult("send-key-state", Config::Actions::sendKeyState(*mask, sc<uint32_t>(sym), sc<uint32_t>(state), actionWindow(id)));
    }

    static int hlSchemeMouse(const char* action) {
        if (!g_up)
            return -1;
        return actionResult("mouse", Config::Actions::mouse(std::string(action ? action : "")));
    }

    static int hlSchemeReleaseInputCapture() {
        if (!g_up)
            return -1;
        return actionResult("release-input-capture", Config::Actions::releaseInputCapture());
    }

    // explicit fullscreen: internal and client modes, layout-aware flag
    static int hlSchemeWindowFullscreenState(long long id, int internalMode, int clientMode, int layoutAware) {
        if (!g_up)
            return -1;
        const auto w = actionWindow(id);
        if (id >= 0 && !w)
            return -1;
        return actionResult("window-fullscreen-state",
            Config::Actions::fullscreenWindow(sc<Fullscreen::eFullscreenMode>(internalMode), sc<Fullscreen::eFullscreenMode>(clientMode), layoutAware != 0, w));
    }

    static int hlSchemeLayoutMessage(const char* msg) {
        if (!g_up)
            return -1;
        return actionResult("layout-msg", Config::Actions::layoutMessage(std::string(msg ? msg : "")));
    }

    // ---- config: setting config options from scheme ---------------------------
    // The values live in CConfigManager::m_configValues (dotted key →
    // ILuaConfigValue). Each value parses itself off a lua stack; we keep a
    // private scratch lua_State (the same liblua the compositor links) purely
    // as the typed front door — the config manager's interpreter is never
    // involved. Propagation is a plain prop-refresh, exactly like
    // hyprctl eval 'hl.config(...)'.

    static lua_State*  g_configScratch = nullptr;

    // (upstream hl.clear_crashed_lockscreen) — manual escape hatch for a
    // crashed lock screen: clears the session lock ONLY while no lock client
    // is attached (unlocking a genuinely locked machine is refused)
    static int hlSchemeClearCrashedLockscreen() {
        if (!g_up)
            return -1;
        if (!g_pSessionLockManager)
            g_configError = "hl-clear-crashed-lockscreen!: sessionLockMgr not init'd yet";
        else if (!g_pSessionLockManager->isSessionLocked())
            g_configError = "hl-clear-crashed-lockscreen!: session is not locked";
        else if (g_pSessionLockManager->clientLocked() || g_pSessionLockManager->clientDenied())
            g_configError = "hl-clear-crashed-lockscreen!: session is locked with a client, refusing to unlock";
        else {
            g_pSessionLockManager->forceUnlock();
            return 0;
        }
        return -1;
    }

    // (upstream hl.exec_scheduled_prop_refresh_immediately) — run the
    // prop refresher's pending scheduled refresh NOW instead of on its
    // next tick (config-time prop/rule changes become visible immediately)
    static int hlSchemeScheduledPropRefreshImmediately() {
        if (!g_up)
            return -1;
        return Config::Supplementary::refresher()->executeScheduledRefreshImmediately();
    }

    // (cmd, effects) → pid. The ONE exec: no effects → spawn(cmd) — the
    // compositor's async shell executor (the legacy "[rules] cmd" prefix is
    // still parsed by the C++ layer here); with effects → the effects plist
    // builds a one-shot CWindowRule (validated against the windowEffects
    // registry) and spawns via SExecRequest{.exec, .rule} — the executor
    // tags the spawned window by pid itself (upstream hl.exec_cmd(cmd,
    // ruleTable) parity; upstream's exec_raw is the same no-rule path, so
    // it folds in).
    static int hlSchemeExec(const char* cmd, ptr effects) {
        if (!g_up || !cmd || !*cmd)
            return -1;

        // walk the effects plist; empty → plain spawn, else build the rule
        auto rule = makeShared<Desktop::Rule::CWindowRule>();
        bool any  = false;
        for (ptr l = effects; Spairp(l); l = Scdr(Scdr(l))) {
            if (!Spairp(Scdr(l))) {
                g_configError = "hl-exec!: odd plist of rule effects";
                return -1;
            }
            const std::string effect = schemeDatumToStr(Scar(l));
            const auto        e      = Desktop::Rule::windowEffects()->get(std::string_view(effect));
            if (!e) {
                g_configError = std::format("hl-exec!: unknown rule effect '{}'", effect);
                return -1;
            }
            const auto res = rule->addEffect(*e, schemeDatumToStr(Scar(Scdr(l))));
            if (!res) {
                g_configError = std::format("hl-exec!: effect '{}': {}", effect, res.error());
                return -1;
            }
            any = true;
        }

        if (!any)
            return (int)Config::Supplementary::executor()->spawn(cmd).value_or(-1);
        return (int)Config::Supplementary::executor()
                   ->spawn(Config::Supplementary::SExecRequest{.exec = cmd, .rule = std::move(rule)})
                   .value_or(-1);
    }

    // ---- groups as objects (upstream HL.Group parity) --------------------------
    // groups dissolve behind our backs -> weak handles via the guardian (the
    // record-cell model); every getter returns #f when the group is gone
    using PHLGROUPREF = Hyprutils::Memory::CWeakPointer<Desktop::View::CGroup>;

    static ptr hlSchemeWorkspaceGroups(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ws = workspaceFromId(id);
        if (!ws)
            return Sfalse;
        ptr                                 l = Snil;
        std::vector<const Desktop::View::CGroup*> pushed;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (w->m_workspace != ws || !w->grouping().group())
                continue;
            const auto* g = w->grouping().group().get();
            if (std::find(pushed.begin(), pushed.end(), g) != pushed.end())
                continue;
            pushed.push_back(g);
            l = Scons(Sinteger((uintptr_t)(new SHandle<PHLGROUPREF>(w->grouping().group()))), l);
        }
        // members were collected head-first: reverse for document order
        ptr out = Snil;
        for (ptr p = l; Spairp(p); p = Scdr(p))
            out = Scons(Scar(p), out);
        return out;
    }

    static SP<Desktop::View::CGroup> groupFromHandle(long long id) {
        return reinterpret_cast<SHandle<PHLGROUPREF>*>(id)->wp.lock();
    }

    static int hlSchemeGroupAlive(long long id) {
        if (!g_up)
            return -1;
        return groupFromHandle(id) ? 1 : 0;
    }

    static int hlSchemeGroupSame(long long a, long long b) {
        if (!g_up)
            return -1;
        const auto ga = groupFromHandle(a);
        const auto gb = groupFromHandle(b);
        return (ga && gb && ga.get() == gb.get()) ? 1 : 0;
    }

    static ptr hlSchemeGroupMembers(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        ptr l = Snil;
        for (const auto& grouped : group->windows()) {
            const auto w = grouped.lock();
            if (!w)
                continue;
            l = Scons(Sinteger(Internals::mintWindowHandle(w)), l);
        }
        ptr out = Snil;
        for (ptr p = l; Spairp(p); p = Scdr(p))
            out = Scons(Scar(p), out);
        return out;
    }

    static ptr hlSchemeGroupCurrent(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        const auto current = group->current();
        if (!current)
            return Sfalse;
        return Sinteger(Internals::mintWindowHandle(current));
    }

    static ptr hlSchemeGroupCurrentIdx(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        return Sinteger(sc<int64_t>(group->getCurrentIdx()) + 1); // 1-based, upstream parity
    }

    static ptr hlSchemeGroupSize(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        return group ? Sinteger(sc<int64_t>(group->size())) : Sfalse;
    }

    static ptr hlSchemeGroupLocked(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        return group->locked() ? Strue : Sfalse;
    }

    static ptr hlSchemeGroupDenied(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        return group->denied() ? Strue : Sfalse;
    }

    // index crosses as -1 = append; 1-based otherwise (upstream parity)
    static int hlSchemeGroupAdd(long long id, long long winId, long long index) {
        if (!g_up)
            return -1;
        const auto group = groupFromHandle(id);
        if (!group)
            return -1;
        const auto window = windowFromId(winId);
        if (!window)
            return -1;
        if (window->grouping().group() == group)
            return 0; // already a member of THIS group: no-op (upstream parity)
        if (group->denied()) {
            g_configError = "hl-group-add!: target group is denied";
            return -2;
        }
        if (!window->grouping().canBeGroupedInto(group)) {
            g_configError = "hl-group-add!: window cannot be added to group";
            return -2;
        }
        group->add(window, index >= 0 ? std::optional<size_t>(sc<size_t>(index - 1)) : std::nullopt);
        return 0;
    }

    static int hlSchemeGroupRemove(long long id, long long winId) {
        if (!g_up)
            return -1;
        const auto group = groupFromHandle(id);
        if (!group)
            return -1;
        const auto window = windowFromId(winId);
        if (!window || !group->has(window)) {
            g_configError = "hl-group-remove!: window is not a group member";
            return -2;
        }
        group->remove(window);
        return 0;
    }


    static Config::Lua::ILuaConfigValue* configValueByKey(const char* key) {
        auto* mgr = sc<Lua::CConfigManager*>(Config::mgr().get());
        if (!mgr || !key || !*key)
            return nullptr;
        auto& vals = mgr->m_configValues;
        auto  it   = vals.find(std::string(key));
        if (it == vals.end()) {
            std::string k = key;
            std::ranges::replace(k, ':', '.');
            it = vals.find(k);
        }
        return it == vals.end() ? nullptr : it->second.get();
    }

    static lua_State* configScratch() {
        if (!g_configScratch)
            g_configScratch = luaL_newstate();
        return g_configScratch;
    }

    // clears the scratch stack: once at the start of each hl-config-add! value
    static int hlConfigBegin() {
        if (!g_up)
            return -1;
        lua_settop(configScratch(), 0);
        return 0;
    }

    static int hlConfigPushNum(double v) {
        if (!g_up)
            return -1;
        lua_pushnumber(configScratch(), v);
        return 0;
    }

    static int hlConfigPushInt(double v) {
        if (!g_up)
            return -1;
        lua_pushinteger(configScratch(), sc<long long>(v));
        return 0;
    }

    static int hlConfigPushBool(int v) {
        if (!g_up)
            return -1;
        lua_pushboolean(configScratch(), v != 0);
        return 0;
    }

    static int hlConfigPushStr(const char* v) {
        if (!g_up)
            return -1;
        lua_pushlstring(configScratch(), v ? v : "", v ? strlen(v) : 0);
        return 0;
    }

    static int hlConfigTblOpen(int isHash) {
        if (!g_up)
            return -1;
        lua_createtable(configScratch(), isHash ? 0 : 4, isHash ? 4 : 0);
        return 0;
    }

    static int hlConfigTblKey(const char* k) {
        if (!g_up)
            return -1;
        lua_pushlstring(configScratch(), k ? k : "", k ? strlen(k) : 0);
        return 0;
    }

    static int hlConfigTblSetHash() {
        if (!g_up)
            return -1;
        lua_rawset(configScratch(), -3); // pops key + value onto the table below
        return 0;
    }

    static int hlConfigTblSeti(int idx) {
        if (!g_up)
            return -1;
        lua_rawseti(configScratch(), -2, idx); // pops the value onto the table below
        return 0;
    }

    static int hlConfigSet(const char* key) {
        if (!g_up)
            return -1;
        auto* val = configValueByKey(key);
        if (!val) {
            g_configError = std::string("unknown config key '") + (key ? key : "") + "'";
            return -1;
        }
        lua_State*   L   = configScratch();
        const auto   err = val->parse(L);
        lua_settop(L, 0);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK) {
            g_configError = err.message.empty() ? "parse error" : err.message;
            return -2;
        }
        Supplementary::refresher()->scheduleRefresh(val->refreshBits());
        return 0;
    }

    static ptr hlConfigLastError() {
        return Sstring_utf8(g_configError.c_str(), g_configError.size());
    }

    // read side: marshal the value back as an encoded string
    // "b\n0|1" | "n\n<num>" | "s\n<str>" | "t\n(key\nvalue\n)*" ; #f = unknown

    // ---- per-device config (upstream hl.device parity) -----------------------
    // Lua's hl.device({ name, ... }) writes per-device input overrides;
    // mirrored here as (hl-device-add! NAME . FIELDS). Write-only by parity:
    // Lua exposes no device read/get, and a stored field cannot be unset
    // (insert-or-assign only).

    enum class eDeviceKind : uint8_t { BOOL, INT, FLOAT, STRING, VEC2 };

    struct SDeviceField {
        const char* name;
        eDeviceKind kind;
        double      lo, hi; // bounds for INT/FLOAT (unused otherwise)
    };

    // mirrored from DEVICE_FIELDS (LuaBindingsConfigRules.cpp)
    static const SDeviceField DEVICE_FIELDS[] = {
        {"sensitivity", eDeviceKind::FLOAT, -1, 1},      {"accel_profile", eDeviceKind::STRING, 0, 0},
        {"rotation", eDeviceKind::INT, 0, 359},          {"kb_file", eDeviceKind::STRING, 0, 0},
        {"kb_layout", eDeviceKind::STRING, 0, 0},        {"kb_variant", eDeviceKind::STRING, 0, 0},
        {"kb_options", eDeviceKind::STRING, 0, 0},       {"kb_rules", eDeviceKind::STRING, 0, 0},
        {"kb_model", eDeviceKind::STRING, 0, 0},         {"repeat_rate", eDeviceKind::INT, 0, 200},
        {"repeat_delay", eDeviceKind::INT, 0, 2000},     {"natural_scroll", eDeviceKind::BOOL, 0, 0},
        {"tap_button_map", eDeviceKind::STRING, 0, 0},   {"numlock_by_default", eDeviceKind::BOOL, 0, 0},
        {"resolve_binds_by_sym", eDeviceKind::BOOL, 0, 0}, {"disable_while_typing", eDeviceKind::BOOL, 0, 0},
        {"clickfinger_behavior", eDeviceKind::BOOL, 0, 0}, {"middle_button_emulation", eDeviceKind::BOOL, 0, 0},
        {"tap_to_click", eDeviceKind::BOOL, 0, 0},       {"tap_and_drag", eDeviceKind::BOOL, 0, 0},
        {"drag_lock", eDeviceKind::INT, 0, 2},           {"left_handed", eDeviceKind::BOOL, 0, 0},
        {"scroll_method", eDeviceKind::STRING, 0, 0},    {"scroll_button", eDeviceKind::INT, 0, 300},
        {"scroll_button_lock", eDeviceKind::BOOL, 0, 0}, {"scroll_points", eDeviceKind::STRING, 0, 0},
        {"scroll_factor", eDeviceKind::FLOAT, 0, 100},   {"transform", eDeviceKind::INT, 0, 0},
        {"output", eDeviceKind::STRING, 0, 0},           {"enabled", eDeviceKind::BOOL, 0, 0},
        {"region_position", eDeviceKind::VEC2, 0, 0},    {"absolute_region_position", eDeviceKind::BOOL, 0, 0},
        {"region_size", eDeviceKind::VEC2, 0, 0},        {"relative_input", eDeviceKind::BOOL, 0, 0},
        {"active_area_position", eDeviceKind::VEC2, 0, 0}, {"active_area_size", eDeviceKind::VEC2, 0, 0},
        {"flip_x", eDeviceKind::BOOL, 0, 0},             {"flip_y", eDeviceKind::BOOL, 0, 0},
        {"drag_3fg", eDeviceKind::INT, 0, 2},            {"keybinds", eDeviceKind::BOOL, 0, 0},
        {"share_states", eDeviceKind::INT, 0, 2},        {"release_pressed_on_close", eDeviceKind::BOOL, 0, 0},
        {"tags", eDeviceKind::STRING, 0, 0},
    };

    // minimal ILuaConfigValue holding one device value. parse/push are unused
    // by the plugin (values arrive as scheme objects); the as* readbacks feed
    // the input manager's getDeviceInt/Float/String.
    class CDeviceValue : public Config::Lua::ILuaConfigValue {
      public:
        CDeviceValue(bool b) : m_kind(eDeviceKind::BOOL), m_bool(b) { m_bSetByUser = true; }
        CDeviceValue(Config::INTEGER i) : m_kind(eDeviceKind::INT), m_int(i) { m_bSetByUser = true; }
        CDeviceValue(Config::FLOAT f) : m_kind(eDeviceKind::FLOAT), m_fl(f) { m_bSetByUser = true; }
        CDeviceValue(Config::STRING s) : m_kind(eDeviceKind::STRING), m_str(std::move(s)) { m_bSetByUser = true; }
        CDeviceValue(Config::VEC2 v) : m_kind(eDeviceKind::VEC2), m_vec(v) { m_bSetByUser = true; }

        virtual Config::Lua::SParseError parse(lua_State*) override { return {}; }
        virtual const std::type_info*    underlying() override {
            switch (m_kind) {
                case eDeviceKind::BOOL: return &typeid(bool);
                case eDeviceKind::INT: return &typeid(Config::INTEGER);
                case eDeviceKind::FLOAT: return &typeid(Config::FLOAT);
                case eDeviceKind::STRING: return &typeid(Config::STRING);
                case eDeviceKind::VEC2: return &typeid(Config::VEC2);
            }
            return &typeid(Config::VEC2);
        }
        virtual void const* data() override {
            switch (m_kind) {
                case eDeviceKind::BOOL: return &m_bool;
                case eDeviceKind::INT: return &m_int;
                case eDeviceKind::FLOAT: return &m_fl;
                case eDeviceKind::STRING: return &m_str;
                case eDeviceKind::VEC2: return &m_vec;
            }
            return nullptr;
        }
        virtual std::string toString() override {
            switch (m_kind) {
                case eDeviceKind::BOOL: return m_bool ? "true" : "false";
                case eDeviceKind::INT: return std::to_string(m_int);
                case eDeviceKind::FLOAT: {
                    char buf[64];
                    snprintf(buf, sizeof buf, "%.17g", m_fl);
                    return buf;
                }
                case eDeviceKind::STRING: return m_str;
                case eDeviceKind::VEC2: return std::format("{} x {}", (int)m_vec.x, (int)m_vec.y);
            }
            return "";
        }
        virtual void push(lua_State* L) override {
            switch (m_kind) {
                case eDeviceKind::BOOL: lua_pushboolean(L, m_bool); break;
                case eDeviceKind::INT: lua_pushinteger(L, m_int); break;
                case eDeviceKind::FLOAT: lua_pushnumber(L, m_fl); break;
                case eDeviceKind::STRING: lua_pushstring(L, m_str.c_str()); break;
                case eDeviceKind::VEC2:
                    lua_newtable(L);
                    lua_pushnumber(L, m_vec.x); lua_rawseti(L, -2, 1);
                    lua_pushnumber(L, m_vec.y); lua_rawseti(L, -2, 2);
                    break;
            }
        }
        virtual void reset() override { m_bSetByUser = false; }
        virtual Config::INTEGER asInt() override {
            switch (m_kind) {
                case eDeviceKind::BOOL: return m_bool ? 1 : 0;
                case eDeviceKind::INT: return m_int;
                case eDeviceKind::FLOAT: return (Config::INTEGER)m_fl;
                default: return 0;
            }
        }
        virtual Config::FLOAT asFloat() override {
            switch (m_kind) {
                case eDeviceKind::FLOAT: return m_fl;
                case eDeviceKind::INT: return (Config::FLOAT)m_int;
                default: return 0.F;
            }
        }
        virtual Config::VEC2 asVec2() override { return m_vec; }
        virtual Config::STRING asString() override {
            if (m_kind == eDeviceKind::STRING) return m_str;
            return toString();
        }

      private:
        eDeviceKind     m_kind = eDeviceKind::BOOL;
        bool            m_bool = false;
        Config::INTEGER m_int  = 0;
        Config::FLOAT   m_fl   = 0.F;
        Config::STRING  m_str;
        Config::VEC2    m_vec{0, 0};
    };

    static std::string schemeDatumToStr(ptr p) {
        if (Ssymbolp(p))
            p = Ssymbol_to_string(p);
        std::string s;
        for (iptr i = 0; i < Sstring_length(p); ++i)
            s += (char)Sstring_ref(p, i);
        return s;
    }

    // coerce a scheme value to the field's kind; sets g_configError on failure
    static std::optional<std::pair<std::string, UP<CDeviceValue>>> deviceValue(const SDeviceField& f, ptr v) {
        auto fail = [&](const char* why) -> std::optional<std::pair<std::string, UP<CDeviceValue>>> {
            g_configError = std::format("hl-device-add!: field '{}': {}", f.name, why);
            return std::nullopt;
        };

        if (f.kind == eDeviceKind::BOOL) {
            if (v == Strue) return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(true))};
            if (v == Sfalse) return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(false))};
            return fail("expected #t or #f");
        }
        if (f.kind == eDeviceKind::STRING) {
            if (!Sstringp(v))
                return fail("expected a string");
            return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(schemeDatumToStr(v)))};
        }
        if (f.kind == eDeviceKind::INT || f.kind == eDeviceKind::FLOAT) {
            double d = 0;
            if (Sfixnump(v)) d = (double)Sfixnum_value(v);
            else if (Sflonump(v)) d = Sflonum_value(v);
            else return fail("expected a number");
            if ((f.lo != 0 || f.hi != 0) && (d < f.lo || d > f.hi))
                return fail(std::format("out of range [{:.0g}, {:.0g}]", f.lo, f.hi).c_str());
            if (f.kind == eDeviceKind::INT)
                return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue((Config::INTEGER)d))};
            return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue((Config::FLOAT)d))};
        }
        // VEC2: (x . y) or (x y)
        if (!Spairp(v))
            return fail("expected a coordinate pair");
        auto asNum = [](ptr p, double& out) -> bool {
            if (Sfixnump(p)) { out = (double)Sfixnum_value(p); return true; }
            if (Sflonump(p)) { out = Sflonum_value(p); return true; }
            return false;
        };
        double x, y;
        ptr    tail = Scdr(v);
        if (Spairp(tail)) {
            if (!asNum(Scar(tail), y)) return fail("expected a coordinate pair");
        } else if (!asNum(tail, y))
            return fail("expected a coordinate pair");
        if (!asNum(Scar(v), x)) return fail("expected a coordinate pair");
        return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(Config::VEC2(x, y)))};
    }

    // (hl-device-add! NAME . FIELDS)
    static int hlSchemeDeviceAdd(const char* name, ptr fields) {
        if (!g_up || !name || !*name) {
            g_configError = "hl-device-add!: a device name is required";
            return -1;
        }
        if (!Spairp(fields)) {
            g_configError = "hl-device-add!: fields must be a plist, e.g. (hl-device-add! NAME 'enabled #t)";
            return -1;
        }

        std::string dev = name;
        std::replace(dev.begin(), dev.end(), ' ', '-');

        // validate + coerce every field first: a bad field writes nothing
        std::vector<std::pair<std::string, UP<CDeviceValue>>> values;
        ptr l = fields;
        while (Spairp(l)) {
            if (!Spairp(Scdr(l))) {
                g_configError = "hl-device-add!: odd plist of fields";
                return -1;
            }
            const std::string key = schemeDatumToStr(Scar(l));
            const SDeviceField* f  = nullptr;
            for (const auto& F : DEVICE_FIELDS) {
                if (key == F.name) { f = &F; break; }
            }
            if (!f) {
                g_configError = std::format("hl-device-add!: unknown field '{}'", key);
                return -1;
            }
            auto v = deviceValue(*f, Scar(Scdr(l)));
            if (!v)
                return -1;
            values.emplace_back(std::move(*v));
            l = Scdr(Scdr(l));
        }

        // Config::mgr() is the abstract interface; the device store lives on
        // the concrete manager (same cast used elsewhere in this file)
        auto* cmgr = sc<Config::Lua::CConfigManager*>(Config::mgr().get());
        auto& cfg  = cmgr->m_deviceConfigs[dev];
        for (auto& [k, val] : values)
            cfg.values.insert_or_assign(std::move(k), std::move(val));

        Config::Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_INPUT_DEVICES);
        return 0;
    }

    static ptr hlConfigGet(const char* key) {
        if (!g_up)
            return Sfalse;
        auto* val = configValueByKey(key);
        if (!val)
            return Sfalse;
        lua_State* L = configScratch();
        val->push(L);

        std::vector<ptr> roots;
        ptr              result = Sfalse;

        switch (lua_type(L, -1)) {
            case LUA_TNIL: break;
            case LUA_TBOOLEAN: result = lua_toboolean(L, -1) ? Strue : Sfalse; break;
            case LUA_TNUMBER: {
                const auto D = lua_tonumber(L, -1);
                result       = (long long)D == D ? Sinteger((long long)D) : Sflonum(D);
                break;
            }
            case LUA_TSTRING: {
                const char* s = lua_tostring(L, -1);
                result        = Sstring_utf8(s, strlen(s));
                break;
            }
            case LUA_TTABLE: {
                // tables come back as a PLIST (key value key value ...) — the
                // same shape hl-config-add! accepts going in
                std::vector<ptr> elems;
                lua_pushnil(L);
                while (lua_next(L, -2) != 0) {
                    if (lua_type(L, -2) == LUA_TSTRING) {
                        const char* k = lua_tostring(L, -2);
                        ptr         keySym = Sstring_to_symbol(k);
                        marshRoot(keySym, roots);
                        elems.push_back(keySym);
                    } else {
                        elems.push_back(Sinteger(lua_tointeger(L, -2)));
                    }

                    switch (lua_type(L, -1)) {
                        case LUA_TNUMBER: {
                            const auto D = lua_tonumber(L, -1);
                            ptr         v = (long long)D == D ? Sinteger((long long)D) : Sflonum(D);
                            marshRoot(v, roots);
                            elems.push_back(v);
                            break;
                        }
                        case LUA_TSTRING: {
                            const char* s = lua_tostring(L, -1);
                            ptr         v = Sstring_utf8(s, strlen(s));
                            marshRoot(v, roots);
                            elems.push_back(v);
                            break;
                        }
                        case LUA_TBOOLEAN: elems.push_back(lua_toboolean(L, -1) ? Strue : Sfalse); break;
                        default: break;
                    }
                    lua_pop(L, 1);
                }
                result = Snil;
                for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                    result = Scons(*it, result);
                    marshRoot(result, roots);
                }
                break;
            }
            default: break;
        }

        lua_settop(L, 0);
        marshRelease(roots);   // release just before returning; nothing allocates after
        return result;
    }

    // ---- monitor rules ---------------------------------------------------------
    // mirrors the lua hl.monitor: fields parse into a CMonitorRuleParser seeded
    // from the existing rule, then commit to the rule manager and refresh.
    static UP<Config::CMonitorRuleParser> g_monitorParser;

    static int hlMonitorBegin(const char* output) {
        if (!g_up)
            return -1;
        if (!output || !*output) {
            g_configError = "hl-monitor-rule-add!: output name required";
            return -1;
        }
        g_monitorParser      = makeUnique<Config::CMonitorRuleParser>(std::string(output));
        const auto& all      = Config::monitorRuleMgr()->all();
        const auto  existing = std::ranges::find_if(all, [&output](const auto& rule) { return rule.m_name == output; });
        if (existing != all.end())
            g_monitorParser->rule() = *existing;
        return 0;
    }

    static int hlMonitorFieldStr(const char* field, const char* value) {
        if (!g_up || !g_monitorParser)
            return -1;
        const std::string f = field ? field : "";
        const std::string v = value ? value : "";
        auto&             p = *g_monitorParser;
        bool              ok;
        if (f == "mode")
            ok = p.parseMode(v);
        else if (f == "position")
            ok = p.parsePosition(v);
        else if (f == "scale")
            ok = p.parseScale(v);
        else if (f == "mirror") {
            p.setMirror(v);
            ok = true;
        } else if (f == "cm")
            ok = p.parseCM(v);
        else if (f == "icc")
            ok = p.parseICC(v);
        else if (f == "sdr_eotf") {
            p.rule().m_sdrEotf = NTransferFunction::fromString(v);
            ok                 = true;
        } else {
            g_configError = "hl-monitor-rule-add!: unknown string field '" + f + "'";
            return -1;
        }
        if (!ok)
            g_configError = p.getError() ? *p.getError() : "invalid value for '" + f + "'";
        return ok ? 0 : -1;
    }

    static int hlMonitorFieldNum(const char* field, double v) {
        if (!g_up || !g_monitorParser)
            return -1;
        const std::string f = field ? field : "";
        auto&             p = *g_monitorParser;

        // typed validation with the same ranges the lua config uses
        std::unique_ptr<Config::Lua::ILuaConfigValue> val;
        if (f == "transform")
            val.reset(new Config::Lua::CLuaConfigInt(0, std::optional<Config::INTEGER>(0), std::optional<Config::INTEGER>(7)));
        else if (f == "bitdepth")
            val.reset(new Config::Lua::CLuaConfigInt(8));
        else if (f == "vrr")
            val.reset(new Config::Lua::CLuaConfigInt(-1, std::optional<Config::INTEGER>(-1), std::optional<Config::INTEGER>(3)));
        else if (f == "supports_wide_color" || f == "supports_hdr")
            val.reset(new Config::Lua::CLuaConfigInt(0, std::optional<Config::INTEGER>(-1), std::optional<Config::INTEGER>(1)));
        else if (f == "sdr_max_luminance")
            val.reset(new Config::Lua::CLuaConfigInt(80));
        else if (f == "max_luminance" || f == "max_avg_luminance")
            val.reset(new Config::Lua::CLuaConfigInt(-1));
        else if (f == "sdrbrightness")
            val.reset(new Config::Lua::CLuaConfigFloat(1.F));
        else if (f == "sdrsaturation")
            val.reset(new Config::Lua::CLuaConfigFloat(1.F));
        else if (f == "sdr_min_luminance")
            val.reset(new Config::Lua::CLuaConfigFloat(0.2F));
        else if (f == "min_luminance")
            val.reset(new Config::Lua::CLuaConfigFloat(-1.F));
        else {
            g_configError = "hl-monitor-rule-add!: unknown numeric field '" + f + "'";
            return -1;
        }

        lua_State*      L = configScratch();
        lua_settop(L, 0);
        const bool isFloat = (f == "sdrbrightness" || f == "sdrsaturation" || f == "sdr_min_luminance" || f == "min_luminance");
        if (isFloat)
            lua_pushnumber(L, v);
        else
            lua_pushinteger(L, sc<long long>(v));
        const auto err = val->parse(L);
        lua_settop(L, 0);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK) {
            g_configError = err.message.empty() ? "invalid value" : err.message;
            return -2;
        }

        auto& rule = p.rule();
        if (f == "transform")
            rule.m_transform = sc<wl_output_transform>(sc<int>(v));
        else if (f == "bitdepth")
            rule.m_enable10bit = sc<int>(v) == 10;
        else if (f == "vrr")
            rule.m_vrr = sc<int>(v) < 0 ? std::nullopt : std::optional(sc<int>(v));
        else if (f == "supports_wide_color")
            rule.m_supportsWideColor = sc<int>(v);
        else if (f == "supports_hdr")
            rule.m_supportsHDR = sc<int>(v);
        else if (f == "sdr_max_luminance")
            rule.m_sdrMaxLuminance = sc<int>(v);
        else if (f == "max_luminance")
            rule.m_maxLuminance = sc<int>(v);
        else if (f == "max_avg_luminance")
            rule.m_maxAvgLuminance = sc<int>(v);
        else if (f == "sdrbrightness")
            rule.m_sdrBrightness = sc<float>(v);
        else if (f == "sdrsaturation")
            rule.m_sdrSaturation = sc<float>(v);
        else if (f == "sdr_min_luminance")
            rule.m_sdrMinLuminance = sc<float>(v);
        else if (f == "min_luminance")
            rule.m_minLuminance = sc<float>(v);
        return 0;
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

    // gap fields (reserved / reserved_area): the value is pushed onto the
    // scratch stack by the config push helpers
    static int hlMonitorFieldGap(const char* field) {
        if (!g_up || !g_monitorParser)
            return -1;
        const std::string f = field ? field : "";
        if (f != "reserved" && f != "reserved_area") {
            g_configError = "hl-monitor-rule-add!: unknown gap field '" + f + "'";
            return -1;
        }
        Config::Lua::CLuaConfigCssGap gap(0);
        const auto                    err = gap.parse(configScratch());
        lua_settop(configScratch(), 0);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK) {
            g_configError = err.message.empty() ? "invalid reserved area" : err.message;
            return -2;
        }
        const auto& g = *sc<const Config::CCssGapData*>(gap.data());
        if (!g_monitorParser->setReserved(Desktop::CReservedArea(g.m_top, g.m_right, g.m_bottom, g.m_left))) {
            g_configError = "invalid reserved area";
            return -2;
        }
        return 0;
    }

    static int hlMonitorFieldBool(const char* field, int v) {
        if (!g_up || !g_monitorParser)
            return -1;
        const std::string f = field ? field : "";
        if (f != "disabled") {
            g_configError = "hl-monitor-rule-add!: unknown bool field '" + f + "'";
            return -1;
        }
        g_monitorParser->rule().m_disabled = (v != 0);
        return 0;
    }

    static int hlMonitorCommit() {
        if (!g_up || !g_monitorParser)
            return -1;
        Config::monitorRuleMgr()->add(std::move(g_monitorParser->rule()));
        g_monitorParser.reset();
        Supplementary::refresher()->scheduleRefresh(Supplementary::REFRESH_MONITOR_STATES);
        return 0;
    }

    // ---- curves and animations -------------------------------------------------

    static int hlCurveAdd(const char* name, int type, double a, double b, double c, double d) {
        if (!g_up)
            return -1;
        if (!name || !*name) {
            g_configError = "hl-curve-add!: name required";
            return -1;
        }
        if (type == 0)
            Animation::mgr()->addBezierWithName(name, Vector2D{a, b}, Vector2D{c, d});
        else if (type == 1) {
            if (a <= 0.5F || b <= 0.5F || c <= 0.5F) {
                g_configError = "hl-curve-add!: spring params must be >= 0.5";
                return -1;
            }
            Hyprutils::Animation::SSpringCurve curve;
            curve.stiffness = sc<float>(a);
            curve.damping   = sc<float>(b);
            curve.mass      = sc<float>(c);
            Animation::mgr()->addSpringWithName(name, curve);
        } else {
            g_configError = "hl-curve-add!: unknown type";
            return -1;
        }
        return 0;
    }

    static int hlAnimationSet(const char* leaf, int enabled, double speed, const char* curve, const char* style) {
        if (!g_up)
            return -1;
        if (!leaf || !*leaf) {
            g_configError = "hl-animation-add!: leaf required";
            return -1;
        }
        // an unknown leaf would be accepted here and crash the config
        // re-apply on the next reload — validate against the tree
        if (!Config::animationTree()->nodeExists(leaf)) {
            g_configError = "hl-animation-add!: unknown animation leaf '" + std::string(leaf) + "'";
            return -1;
        }
        const std::string cv = curve ? curve : "";
        const std::string sv = style ? style : "";
        if (!cv.empty() && !Animation::mgr()->bezierExists(cv) && !Animation::mgr()->springExists(cv)) {
            g_configError = "hl-animation-add!: curve '" + cv + "' is not defined (declare it with hl-curve-add!)";
            return -1;
        }
        if (!sv.empty()) {
            const auto err = Animation::mgr()->styleValidInConfigVar(leaf, sv);
            if (!err.empty()) {
                g_configError = err;
                return -1;
            }
        }
        Config::animationTree()->setConfigForNode(leaf, enabled != 0, sc<float>(speed), cv, sv);
        return 0;
    }

    // ---- permissions -----------------------------------------------------------
    // mirrors the lua hl.permission; only takes effect at first launch, like
    // upstream — permission rules require a compositor restart.
    static int hlPermissionAdd(const char* binary, const char* typeStr, const char* modeStr) {
        if (!g_up)
            return -1;
        auto* mgr = sc<Lua::CConfigManager*>(Config::mgr().get());
        if (!mgr || !mgr->isFirstLaunch()) {
            g_configError = "hl-permission-add!: permission rules only take effect at startup; set them in your config and restart";
            return -1;
        }
        if (!g_pDynamicPermissionManager) {
            g_configError = "hl-permission-add!: permission manager unavailable";
            return -1;
        }
        const std::string           t = typeStr ? typeStr : "";
        const std::string           m = modeStr ? modeStr : "";
        eDynamicPermissionType      type = PERMISSION_TYPE_UNKNOWN;
        eDynamicPermissionAllowMode mode = PERMISSION_RULE_ALLOW_MODE_UNKNOWN;
        if (t == "screencopy")
            type = PERMISSION_TYPE_SCREENCOPY;
        else if (t == "cursorpos")
            type = PERMISSION_TYPE_CURSOR_POS;
        else if (t == "plugin")
            type = PERMISSION_TYPE_PLUGIN;
        else if (t == "keyboard" || t == "keeb")
            type = PERMISSION_TYPE_KEYBOARD;
        else if (t == "input-capture")
            type = PERMISSION_TYPE_INPUT_CAPTURE;
        if (m == "ask")
            mode = PERMISSION_RULE_ALLOW_MODE_ASK;
        else if (m == "allow")
            mode = PERMISSION_RULE_ALLOW_MODE_ALLOW;
        else if (m == "deny")
            mode = PERMISSION_RULE_ALLOW_MODE_DENY;
        if (type == PERMISSION_TYPE_UNKNOWN || mode == PERMISSION_RULE_ALLOW_MODE_UNKNOWN) {
            g_configError = "hl-permission-add!: unknown type '" + t + "' or mode '" + m + "'";
            return -1;
        }
        g_pDynamicPermissionManager->addConfigPermissionRule(binary ? binary : "", type, mode);
        return 0;
    }

    // ---- rules: window, layer, workspace ---------------------------------------
    // mirrors the lua hl.window_rule / hl.layer_rule / hl.workspace_rule.
    // Named rules are reused across calls; anonymous rules are unregistered
    // when the config reloads (the reload itself clears the whole engine).

    static std::unordered_map<std::string, SP<Desktop::Rule::CWindowRule>> g_windowRules;
    static std::unordered_map<std::string, SP<Desktop::Rule::CLayerRule>>  g_layerRules;
    static std::vector<SP<Desktop::Rule::CWindowRule>>                     g_anonWindowRules;
    static std::vector<SP<Desktop::Rule::CLayerRule>>                      g_anonLayerRules;
    static SP<Desktop::Rule::CWindowRule>                                  g_curWindowRule;
    static SP<Desktop::Rule::CLayerRule>                                   g_curLayerRule;
    static std::optional<Config::CWorkspaceRule>                           g_curWorkspaceRule;
    // rule handles: record address -> the rule, with the record LOCKED in the
    // entry (SThunkRef) — no user capture keeps a rule handle alive, so the
    // index holds the lock. Entries erase at the generation boundary only:
    // rules live for their config generation, so that IS their lifetime.
    static std::unordered_map<uintptr_t, std::pair<SP<Desktop::Rule::IRule>, SThunkRef>> g_ruleIndex;

    // called from reloadScheme: the config reload cleared the engine's rules
    static void clearSchemeRules() {
        for (const auto& r : g_anonWindowRules)
            Desktop::Rule::ruleEngine()->unregisterRule(SP<Desktop::Rule::IRule>(r));
        for (const auto& r : g_anonLayerRules)
            Desktop::Rule::ruleEngine()->unregisterRule(SP<Desktop::Rule::IRule>(r));
        g_windowRules.clear();
        g_layerRules.clear();
        g_anonWindowRules.clear();
        g_anonLayerRules.clear();
        g_curWindowRule.reset();
        g_curLayerRule.reset();
        g_curWorkspaceRule.reset();
        g_ruleIndex.clear();
    }

    static int hlWindowRuleBegin(const char* name, int enabled) {
        if (!g_up)
            return -1;
        const std::string              n = name ? name : "";
        SP<Desktop::Rule::CWindowRule> rule;
        const auto                     it = g_windowRules.find(n);
        if (!n.empty() && it != g_windowRules.end())
            rule = it->second;
        else {
            rule = makeShared<Desktop::Rule::CWindowRule>(n);
            if (!n.empty())
                g_windowRules.emplace(n, rule);
            else
                g_anonWindowRules.emplace_back(rule);
            Desktop::Rule::ruleEngine()->registerRule(SP<Desktop::Rule::IRule>(rule));
        }
        rule->setEnabled(enabled != 0);
        g_curWindowRule = rule;
        return 0;
    }

    static int hlLayerRuleBegin(const char* name, int enabled) {
        if (!g_up)
            return -1;
        const std::string              n = name ? name : "";
        SP<Desktop::Rule::CLayerRule>  rule;
        const auto                     it = g_layerRules.find(n);
        if (!n.empty() && it != g_layerRules.end())
            rule = it->second;
        else {
            rule = makeShared<Desktop::Rule::CLayerRule>(n);
            if (!n.empty())
                g_layerRules.emplace(n, rule);
            else
                g_anonLayerRules.emplace_back(rule);
            Desktop::Rule::ruleEngine()->registerRule(SP<Desktop::Rule::IRule>(rule));
        }
        rule->setEnabled(enabled != 0);
        g_curLayerRule = rule;
        return 0;
    }

    static int hlRuleMatch(const char* prop, const char* value) {
        if (!g_up)
            return -1;
        const auto p = Desktop::Rule::matchPropFromString(prop ? prop : "");
        if (!p) {
            g_configError = std::string("unknown match property '") + (prop ? prop : "") + "'";
            return -1;
        }
        if (g_curWindowRule) {
            g_curWindowRule->registerMatch(*p, value ? value : "");
            return 0;
        }
        if (g_curLayerRule) {
            g_curLayerRule->registerMatch(*p, value ? value : "");
            return 0;
        }
        return -1;
    }

    static int hlWindowRuleEffect(const char* effect, const char* value) {
        if (!g_up || !g_curWindowRule)
            return -1;
        const auto e = Desktop::Rule::windowEffects()->get(std::string_view(effect ? effect : ""));
        if (!e) {
            g_configError = std::string("unknown effect '") + (effect ? effect : "") + "'";
            return -1;
        }
        const auto res = g_curWindowRule->addEffect(*e, value ? value : "");
        if (!res) {
            g_configError = res.error();
            return -2;
        }
        return 0;
    }

    static int hlLayerRuleEffect(const char* effect, const char* value) {
        if (!g_up || !g_curLayerRule)
            return -1;
        const auto e = Desktop::Rule::layerEffects()->get(std::string_view(effect ? effect : ""));
        if (!e) {
            g_configError = std::string("unknown layer effect '") + (effect ? effect : "") + "'";
            return -1;
        }
        const auto res = g_curLayerRule->addEffect(*e, value ? value : "");
        if (!res) {
            g_configError = res.error();
            return -2;
        }
        return 0;
    }

    static int hlWindowRuleCommit(ptr record) {
        if (!g_up || !g_curWindowRule)
            return -1;
        g_ruleIndex.emplace(reinterpret_cast<uintptr_t>(record),
                            std::make_pair(g_curWindowRule, SThunkRef(record)));
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_WINDOW_STATES);
        g_curWindowRule.reset();
        return 0;
    }

    static int hlLayerRuleCommit(ptr record) {
        if (!g_up || !g_curLayerRule)
            return -1;
        g_ruleIndex.emplace(reinterpret_cast<uintptr_t>(record),
                            std::make_pair(g_curLayerRule, SThunkRef(record)));
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_RULES);
        g_curLayerRule.reset();
        return 0;
    }

    static int hlRuleSetEnabled(ptr record, int enabled) {
        if (!g_up)
            return -1;
        const auto it = g_ruleIndex.find(reinterpret_cast<uintptr_t>(record));
        if (it == g_ruleIndex.end())
            return -1;
        it->second.first->setEnabled(enabled != 0);
        return 0;
    }

    static int hlRuleEnabled(ptr record) {
        const auto it = g_ruleIndex.find(reinterpret_cast<uintptr_t>(record));
        return (it != g_ruleIndex.end() && it->second.first->isEnabled()) ? 1 : 0;
    }

    // ---- workspace rules -------------------------------------------------------

    static int hlWorkspaceRuleBegin(const char* ws, int enabled) {
        if (!g_up)
            return -1;
        if (!ws || !*ws) {
            g_configError = "hl-workspace-rule-add!: workspace selector required";
            return -1;
        }
        g_curWorkspaceRule = Config::CWorkspaceRule{};
        g_curWorkspaceRule->m_workspaceString = ws;
        g_curWorkspaceRule->setEnabled(enabled != 0);
        return 0;
    }

    static int hlWorkspaceRuleStr(const char* field, const char* v) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        const std::string f = field ? field : "";
        auto&             r = *g_curWorkspaceRule;
        if (f == "monitor")
            r.m_monitor = v ? v : "";
        else if (f == "on_created_empty")
            r.m_onCreatedEmptyRunCmd = v ? v : "";
        else if (f == "default_name")
            r.m_defaultName = v ? v : "";
        else if (f == "layout")
            r.m_layout = v ? v : "";
        else if (f == "animation")
            r.m_animationStyle = v ? v : "";
        else {
            g_configError = "unknown workspace-rule field '" + f + "'";
            return -1;
        }
        return 0;
    }

    static int hlWorkspaceRuleNum(const char* field, double v) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        const std::string f = field ? field : "";
        if (f == "border_size")
            g_curWorkspaceRule->m_borderSize = sc<int64_t>(v);
        else {
            g_configError = "unknown workspace-rule numeric field '" + f + "'";
            return -1;
        }
        return 0;
    }

    static int hlWorkspaceRuleBool(const char* field, int v) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        const std::string f = field ? field : "";
        auto&             r = *g_curWorkspaceRule;
        if (f == "default")
            r.m_isDefault = (v != 0);
        else if (f == "persistent")
            r.m_isPersistent = (v != 0);
        else if (f == "no_border")
            r.m_noBorder = (v != 0);
        else if (f == "no_rounding")
            r.m_noRounding = (v != 0);
        else if (f == "decorate")
            r.m_decorate = (v != 0);
        else if (f == "no_shadow")
            r.m_noShadow = (v != 0);
        else {
            g_configError = "unknown workspace-rule bool field '" + f + "'";
            return -1;
        }
        return 0;
    }

    static int hlWorkspaceRuleGap(const char* field) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        const std::string f = field ? field : "";
        Config::Lua::CLuaConfigCssGap gap(0);
        const auto                    err = gap.parse(configScratch());
        lua_settop(configScratch(), 0);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK) {
            g_configError = err.message.empty() ? "invalid gaps" : err.message;
            return -2;
        }
        const auto& g = *sc<const Config::CCssGapData*>(gap.data());
        auto&       r = *g_curWorkspaceRule;
        if (f == "gaps_in")
            r.m_gapsIn = g;
        else if (f == "gaps_out")
            r.m_gapsOut = g;
        else if (f == "float_gaps")
            r.m_floatGaps = g;
        else {
            g_configError = "unknown workspace-rule gap field '" + f + "'";
            return -1;
        }
        return 0;
    }

    static int hlWorkspaceRuleLayoutOpt(const char* k, const char* v) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        g_curWorkspaceRule->m_layoutopts[k ? k : ""] = v ? v : "";
        return 0;
    }

    static int hlWorkspaceRuleCommit() {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        Config::workspaceRuleMgr()->replaceOrAdd(std::move(*g_curWorkspaceRule));
        g_curWorkspaceRule.reset();
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_MONITOR_STATES | Config::Supplementary::REFRESH_WINDOW_STATES);
        return 0;
    }

    // ---- queries ----------------------------------------------------------------

    // selector matching, implemented here: prefixes dispatch to a full-match
    // regex over class/initialclass/title/initialtitle, plus pid:/address:/
    // tag:/stableid: equality and the bare "floating"/"tiled"/"active" forms.
    static bool windowMatchesSelector(const PHLWINDOW& w, const std::string& sel) {
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

    static double hlWindowFrom(const char* sel) {
        if (!g_up)
            return -1;
        const std::string selector = sel ? sel : "";
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!windowMatchesSelector(w, selector))
                continue;
            const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
            return (double)id;
        }
        return -1;
    }

    static double hlUrgentWindow() {
        if (!g_up)
            return -1;
        const auto w = Desktop::viewState()->query().urgent().runWindow();
        if (!w)
            return -1;
        const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
        return (double)id;
    }

    static double hlLastWindow() {
        if (!g_up)
            return -1;
        const auto current     = Desktop::focusState()->window();
        const auto& fullHistory = Desktop::History::windowTracker()->fullHistory();
        for (auto it = fullHistory.rbegin(); it != fullHistory.rend(); ++it) {
            const auto candidate = it->lock();
            if (!candidate || !candidate->mapped())
                continue;
            if (current && candidate == current)
                continue;
            const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(candidate));
            return (double)id;
        }
        return -1;
    }

    static ptr monitorIdResult(PHLMONITOR m) {
        if (!m)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLMONITORREF>(m));
        return Sinteger(id);
    }

    static ptr hlMonitorFrom(const char* sel) {
        if (!g_up)
            return Sfalse;
        return monitorIdResult(State::monitorState()->query().configString(sel ? sel : "").run());
    }

    static ptr hlMonitorAt(double x, double y) {
        if (!g_up)
            return Sfalse;
        return monitorIdResult(State::monitorState()->query().vec(Vector2D{x, y}).run());
    }

    static ptr hlMonitorAtCursor() {
        if (!g_up || !Pointer::mgr())
            return Sfalse;
        const auto pos = Pointer::mgr()->untransformedPosition();
        return monitorIdResult(State::monitorState()->query().vec(pos).run());
    }

    static ptr hlActiveMonitor() {
        if (!g_up)
            return Sfalse;
        return monitorIdResult(Desktop::focusState()->monitor());
    }

    static ptr hlActiveWorkspace() {
        if (!g_up)
            return Sfalse;
        const auto mon = Desktop::focusState()->monitor();
        if (!mon || !mon->m_activeWorkspace)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(mon->m_activeWorkspace));
        return Sinteger(id);
    }

    static ptr hlActiveSpecialWorkspace() {
        if (!g_up)
            return Sfalse;
        const auto mon = Desktop::focusState()->monitor();
        if (!mon || !mon->m_activeSpecialWorkspace)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(mon->m_activeSpecialWorkspace));
        return Sinteger(id);
    }

    static ptr hlLastWorkspace() {
        if (!g_up)
            return Sfalse;
        const auto mon     = Desktop::focusState()->monitor();
        const auto current = mon ? mon->m_activeWorkspace : nullptr;
        if (!current)
            return Sfalse;
        const auto previous = Desktop::History::workspaceTracker()->previousWorkspace(current);
        auto       ws       = previous.workspace.lock();
        if (!ws && previous.target.valid())
            ws = State::Workspace::state()->find(previous.target);
        if (!ws)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws));
        return Sinteger(id);
    }

    // ---- workspace/monitor handle getters -------------------------------------
    // Mirror upstream Lua's workspace and monitor object fields 1:1 (see
    // LuaWorkspace.cpp / LuaMonitor.cpp). All getters resolve the handle
    // first; a stale or dead handle yields #f from every getter, like an
    // expired Lua object.

    static ptr boolResult(bool b) {
        return b ? Strue : Sfalse;
    }

    static ptr windowHandleResult(PHLWINDOW w) {
        if (!w)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
        return Sinteger(id);
    }

    static ptr workspaceHandleResult(PHLWORKSPACE ws) {
        if (!ws)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws));
        return Sinteger(id);
    }

    static ptr monitorHandleResult(PHLMONITOR mon) {
        if (!mon)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLMONITORREF>(mon));
        return Sinteger(id);
    }

    template <typename F>
    static ptr wsGet(long long id, F&& fn) {
        if (!g_up)
            return Sfalse;
        const auto ws = workspaceFromId(id);
        if (!ws)
            return Sfalse;
        return fn(ws);
    }

    template <typename F>
    static ptr monGet(long long id, F&& fn) {
        if (!g_up)
            return Sfalse;
        const auto mon = monitorFromId(id);
        if (!mon)
            return Sfalse;
        return fn(mon);
    }

    // -- workspace getters
    static ptr hlWorkspaceName(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { const auto& s = ws->displayName(); return Sstring_utf8(s.c_str(), s.size()); });
    }

    static ptr hlWorkspaceAddressableName(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { const auto& s = ws->addressableName(); return Sstring_utf8(s.c_str(), s.size()); });
    }

    static ptr hlWorkspaceNumber(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto n = ws->numberedID();
            return n ? Sinteger(sc<int>(*n)) : Sfalse;
        });
    }

    static ptr hlWorkspaceMonitor(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return monitorHandleResult(ws->m_monitor.lock()); });
    }

    static ptr hlWorkspaceSpecial(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->type() == Workspace::eWorkspaceType::SPECIAL); });
    }

    static ptr hlWorkspaceActive(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto mon = ws->m_monitor.lock();
            return boolResult(mon && (mon->m_activeWorkspace == ws || mon->m_activeSpecialWorkspace == ws));
        });
    }

    static ptr hlWorkspaceVisible(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->visible()); });
    }

    static ptr hlWorkspaceEmpty(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->getWindowCount() == 0); });
    }

    static ptr hlWorkspacePersistent(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto REGULAR = dynamicPointerCast<Workspace::CRegularWorkspace>(ws);
            return boolResult(REGULAR && REGULAR->isPersistent());
        });
    }

    static ptr hlWorkspaceHasUrgent(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->hasUrgentWindow()); });
    }

    static ptr hlWorkspaceHasFullscreen(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(Fullscreen::controller()->hasFullscreen(ws)); });
    }

    static ptr hlWorkspaceFullscreenMode(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return Sinteger(sc<int>(Fullscreen::controller()->getFullscreenModes(ws).internal)); });
    }

    static ptr hlWorkspaceFullscreenWindow(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return windowHandleResult(Fullscreen::controller()->getFullscreenWindow(ws)); });
    }

    static ptr hlWorkspaceLastWindow(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return windowHandleResult(ws->getLastFocusedWindow()); });
    }

    static ptr hlWorkspaceWindowCount(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return Sinteger(ws->getWindowCount()); });
    }

    static ptr hlWorkspaceGroupCount(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return Sinteger(ws->getGroups()); });
    }

    // windows on the workspace as newline-joined window-handle ids

    static ptr hlWorkspaceTiledLayout(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            std::string layoutName = "unknown";
            const auto  SPACE      = ws->space();
            if (SPACE && SPACE->algorithm() && SPACE->algorithm()->tiledAlgo())
                layoutName = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(SPACE->algorithm()->tiledAlgo().get());
            return Sstring_utf8(layoutName.c_str(), layoutName.size());
        });
    }

    static ptr hlWorkspaceAlive(long long id) {
        return boolResult(g_up && workspaceFromId(id) != nullptr);
    }

    // identity, mirroring hl-window=?: true iff both handles lock to the same
    // live workspace; dead handles are never "the same" as anything
    static ptr hlWorkspaceSame(long long a, long long b) {
        const auto wa = g_up ? workspaceFromId(a) : nullptr;
        const auto wb = g_up ? workspaceFromId(b) : nullptr;
        return boolResult(wa && wb && wa.get() == wb.get());
    }

    // -- monitor getters
    static ptr hlMonitorName(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sstring_utf8(mon->m_name.c_str(), mon->m_name.size()); });
    }

    static ptr hlMonitorDescription(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sstring_utf8(mon->m_description.c_str(), mon->m_description.size()); });
    }

    static ptr hlMonitorNumber(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_id)); });
    }

    static ptr hlMonitorEnabled(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_enabled); });
    }

    static ptr hlMonitorFocused(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(Desktop::focusState()->monitor() == mon); });
    }

    static ptr hlMonitorX(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_position.x)); });
    }

    static ptr hlMonitorY(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_position.y)); });
    }

    static ptr hlMonitorWidth(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_size.x)); });
    }

    static ptr hlMonitorHeight(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_size.y)); });
    }

    static ptr hlMonitorScale(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sflonum(sc<double>(mon->m_scale)); });
    }

    static ptr hlMonitorTransform(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_transform)); });
    }

    static ptr hlMonitorRefreshRate(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sflonum(sc<double>(mon->m_refreshRate)); });
    }

    static ptr hlMonitorMode(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            const auto s = std::format("{}x{}@{}", sc<int>(mon->m_size.x), sc<int>(mon->m_size.y), mon->m_refreshRate);
            return Sstring_utf8(s.c_str(), s.size());
        });
    }

    static ptr hlMonitorDpms(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_dpmsStatus); });
    }

    static ptr hlMonitorVrr(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_vrrActive != 0); });
    }

    static ptr hlMonitor10bit(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_enabled10bit); });
    }

    // reserved area; all-zero means unset
    // when nothing is reserved

    // ---- monitor hardware getters (upstream LuaMonitor parity: serial,
    // physical_width/height, available_modes, mirrors, hardware_details) ----

    static const char* monitorBackendName(Aquamarine::eBackendType t) {
        switch (t) {
            case Aquamarine::AQ_BACKEND_DRM: return "drm";
            case Aquamarine::AQ_BACKEND_WAYLAND: return "wayland";
            case Aquamarine::AQ_BACKEND_HEADLESS: return "headless";
            case Aquamarine::AQ_BACKEND_NULL: return "null";
            default: return "unknown";
        }
    }

    static ptr hlMonitorSerial(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            const auto& s = mon->m_output->serial;
            return Sstring_utf8(s.c_str(), s.size());
        });
    }

    // (physical-width . physical-height), in mm
    static ptr hlMonitorPhysicalSize(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            return Scons(Sinteger((int)mon->m_output->physicalSize.x),
                         Sinteger((int)mon->m_output->physicalSize.y));
        });
    }

    static ptr hlMonitorMirrors(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            std::vector<uintptr_t> ids;
            for (const auto& mirrorRef : mon->m_mirrors) {
                const auto mirror = mirrorRef.lock();
                if (!mirror)
                    continue;
                const auto mid = (uintptr_t)(new SHandle<PHLMONITORREF>(mirror));
                ids.push_back(mid);
            }
            return schemeIntList(ids);
        });
    }

    // list of per-mode plists: ((width w height h refresh-rate r preferred b) ...)
    static ptr hlMonitorAvailableModes(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            std::vector<ptr> roots, modes;
            for (const auto& mode : mon->m_output->modes) {
                if (!mode)
                    continue;
                std::vector<ptr> elems;
                ptr k = Sstring_to_symbol("width");
                marshRoot(k, roots); elems.push_back(k);
                elems.push_back(Sinteger((int)mode->pixelSize.x));
                k = Sstring_to_symbol("height");
                marshRoot(k, roots); elems.push_back(k);
                elems.push_back(Sinteger((int)mode->pixelSize.y));
                k = Sstring_to_symbol("refresh-rate");
                marshRoot(k, roots); elems.push_back(k);
                ptr r = Sflonum(mode->refreshRate / 1000.0);
                marshRoot(r, roots); elems.push_back(r);
                k = Sstring_to_symbol("preferred");
                marshRoot(k, roots); elems.push_back(k);
                elems.push_back(mode->preferred ? Strue : Sfalse);
                ptr m = Snil;
                for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                    m = Scons(*it, m);
                    marshRoot(m, roots);
                }
                modes.push_back(m);
            }
            ptr l = Snil;
            for (auto it = modes.rbegin(); it != modes.rend(); ++it) {
                l = Scons(*it, l);
                marshRoot(l, roots);
            }
            marshRelease(roots);
            return l;
        });
    }

    // plist: (backend "..." hdr b chroma b bt2020 b vrr-capable b)
    static ptr hlMonitorHardwareDetails(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            std::vector<ptr> roots, elems;
            const std::string backend = monitorBackendName(mon->m_output->getBackend()->type());
            ptr k = Sstring_to_symbol("backend");
            marshRoot(k, roots); elems.push_back(k);
            ptr b = Sstring_utf8(backend.c_str(), backend.size());
            marshRoot(b, roots); elems.push_back(b);
            k = Sstring_to_symbol("hdr");
            marshRoot(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->parsedEDID.hdrMetadata.has_value() ? Strue : Sfalse);
            k = Sstring_to_symbol("chroma");
            marshRoot(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->parsedEDID.chromaticityCoords.has_value() ? Strue : Sfalse);
            k = Sstring_to_symbol("bt2020");
            marshRoot(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->parsedEDID.supportsBT2020 ? Strue : Sfalse);
            k = Sstring_to_symbol("vrr-capable");
            marshRoot(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->vrrCapable ? Strue : Sfalse);
            ptr l = Snil;
            for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                l = Scons(*it, l);
                marshRoot(l, roots);
            }
            marshRelease(roots);
            return l;
        });
    }
    static ptr hlMonitorReserved(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            // a plist: (top n left n right n bottom n)
            const auto&         r = mon->m_reservedArea;
            std::vector<ptr>    roots;
            std::vector<ptr>    elems;
            const std::string   KEYS[] = {"top", "left", "right", "bottom"};
            const int           VALUES[] = {r.top(), r.left(), r.right(), r.bottom()};
            for (int i = 0; i < 4; ++i) {
                ptr k = Sstring_to_symbol(KEYS[i].c_str());
                marshRoot(k, roots);
                elems.push_back(k);
                elems.push_back(Sinteger(VALUES[i]));
            }
            ptr l = Snil;
            for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                l = Scons(*it, l);
                marshRoot(l, roots);
            }
            marshRelease(roots);
            return l;
        });
    }

    // the monitor this one mirrors, as a handle; #f when not a mirror
    static ptr hlMonitorMirrorOf(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return monitorHandleResult(mon->m_mirrorOf.lock()); });
    }

    static ptr hlMonitorActiveWorkspace(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return workspaceHandleResult(mon->m_activeWorkspace); });
    }

    static ptr hlMonitorActiveSpecialWorkspace(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return workspaceHandleResult(mon->m_activeSpecialWorkspace); });
    }

    static ptr hlMonitorAlive(long long id) {
        return boolResult(g_up && monitorFromId(id) != nullptr);
    }

    static ptr hlMonitorSame(long long a, long long b) {
        const auto ma = g_up ? monitorFromId(a) : nullptr;
        const auto mb = g_up ? monitorFromId(b) : nullptr;
        return boolResult(ma && mb && ma.get() == mb.get());
    }

    // -- selector bridges: handles are accepted anywhere a selector string is,
    // resolved through the canonical selector exactly like upstream's
    // *SelectorOrObject helpers (LuaBindingsInternal.cpp)
    static ptr hlWorkspaceSelector(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto s = Workspace::selector(*ws);
            return Sstring_utf8(s.c_str(), s.size());
        });
    }

    static ptr hlMonitorSelector(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sstring_utf8(mon->m_name.c_str(), mon->m_name.size()); });
    }

    // workspace resolution via the resolver (the query() chain has proven
    // unreliable from the plugin; the resolver+find path is verified)
    static PHLWORKSPACE workspaceFromSelector(const std::string& sel) {
        const auto target = State::Workspace::resolver()->getWorkspaceTargetFromString(sel);
        if (!target.valid())
            return nullptr;
        return State::Workspace::state()->find(target);
    }

    // windows on a workspace: newline-joined handle ids (like hl-windows)
    static ptr hlWorkspaceWindows(const char* sel) {
        if (!g_up)
            return Sfalse;
        const auto ws = workspaceFromSelector(sel ? sel : "");
        if (!ws)
            return Sfalse;
        std::vector<uintptr_t> ids;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w->mapped() || w->m_workspace != ws)
                continue;
            ids.push_back((uintptr_t)(new SHandle<PHLWINDOWREF>(PHLWINDOWREF(w))));
        }
        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    // bare-thunk/record list fire (gestures, screenshare, keyboard-key)
    static void fireSchemeListRec(ptr record, ptr lst) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-list-rec")), record, lst);
        watchdogExit();
    }

    static int hlIsKeyDown(const char* key) {
        if (!g_up || !Keybinds::mgr())
            return -1;
        const auto sym = xkb_keysym_from_name(key ? key : "", XKB_KEYSYM_CASE_INSENSITIVE);
        if (!sym)
            return -1;
        return Keybinds::mgr()->inputState().isKeysymDown(sym) ? 1 : 0;
    }

    static ptr hlLoadedPlugins() {
        if (!g_up)
            return Sfalse;
        ptr l = Snil;
        for (const auto& p : g_pPluginSystem->getAllPlugins()) {
            if (!p)
                continue;
            const std::string name = p->m_name;
            l = Scons(Sstring_utf8(name.c_str(), name.size()), l);
        }
        return l;
    }

    static ptr hlVersion() {
        return Sstring_utf8(HYPRLAND_VERSION, strlen(HYPRLAND_VERSION));
    }

    // windows matching a selector: handle ids as a scheme list
    static ptr hlWindowsFrom(const char* sel) {
        if (!g_up)
            return Sfalse;
        const std::string selector = sel ? sel : "";
        std::vector<uintptr_t> ids;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!windowMatchesSelector(w, selector))
                continue;
            ids.push_back((uintptr_t)(new SHandle<PHLWINDOWREF>(w)));
        }
        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static ptr hlWindowFullscreenHandler(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto name = Fullscreen::controller()->getFullscreenHandlerNameAsString(window);
        return Sstring_utf8(name.c_str(), name.size());
    }

    // ---- notifications ---------------------------------------------------------

    // ---- notifications: shared field parsing (used by hl-notify! AND the
    // live notification objects) ------------------------------------------------
    // icon names, mirroring the lua config's table
    static eIcons schemeIconFromStr(const std::string& ic, bool* ok = nullptr) {
        static const std::pair<const char*, eIcons> ICON_NAMES[] = {
            {"warning", ICON_WARNING}, {"warn", ICON_WARNING},     {"info", ICON_INFO},       {"hint", ICON_HINT},
            {"error", ICON_ERROR},     {"err", ICON_ERROR},       {"confused", ICON_CONFUSED},
            {"question", ICON_CONFUSED}, {"ok", ICON_OK},         {"none", ICON_NONE},
        };
        for (const auto& [n, i] : ICON_NAMES)
            if (ic == n)
                return i;
        if (ok)
            *ok = false;
        return ICON_NONE;
    }

    static eIcons schemeIconFromScheme(ptr v, bool* ok = nullptr) {
        if (Sstringp(v))
            return schemeIconFromStr(schemeDatumToStr(v), ok);
        if (Sfixnump(v)) {
            const auto raw = Sfixnum_value(v);
            if (raw >= ICON_WARNING && raw <= ICON_NONE)
                return sc<eIcons>(raw);
        }
        if (ok)
            *ok = false;
        return ICON_NONE;
    }

    // color: config hex form "0xAARRGGBB" (decimal digits also accepted)
    static std::optional<CHyprColor> schemeColorFromStr(const std::string& cs) {
        if (cs.empty())
            return CHyprColor(0);
        try {
            return CHyprColor(std::stoull(cs.starts_with("0x") || cs.starts_with("0X") ? cs.substr(2) : cs, nullptr, 16));
        } catch (...) {
            return std::nullopt;
        }
    }

    static ptr hlNotify(const char* text, double durationMs, const char* icon, const char* color, double fontSize) {
        if (!g_up)
            return Sfalse;

        eIcons     theIcon = ICON_NONE;
        const auto ic      = schemeIconFromStr(icon ? icon : "");
        if (icon && *icon) {
            bool ok = true;
            theIcon = schemeIconFromStr(icon, &ok);
            if (!ok) {
                g_configError = "hl-notify!: bad icon (expected none/warn/info/hint/error/confused/ok)";
                return Sfalse;
            }
        }
        const auto col = schemeColorFromStr(color ? color : "");
        if (!col) {
            g_configError = "hl-notify!: bad color (expected 0xAARRGGBB)";
            return Sfalse;
        }
        Notification::overlay()->addNotification(text ? text : "", *col, sc<float>(durationMs), theIcon, sc<float>(fontSize));
        return Strue;
    }

    // ---- live notification objects (upstream hl.notification parity) -----------
    // A notification handle is a guardian-managed weak ref; the per-handle
    // 'paused' bit rides in the handle (upstream's SNotificationRef.paused —
    // a paused handle releases its lock when the handle dies). Pause freezes
    // the timeout timer: the bubble stays on screen until dismissed.
    struct SNotificationHandle : IHandle {
        WP<Notification::CNotification> wp;
        bool                            paused = false;
        explicit SNotificationHandle(SP<Notification::CNotification> n) : wp(n) {}
        ~SNotificationHandle() override {
            if (paused)
                if (auto n = wp.lock())
                    n->unlock();
        }
    };

    static SP<Notification::CNotification> notificationFromHandle(long long id) {
        return reinterpret_cast<SNotificationHandle*>(id)->wp.lock();
    }

    static ptr hlNotificationAdd(ptr fields) {
        if (!g_up)
            return Sfalse;

        // walk the plist first: a bad field writes nothing (device-add parity)
        std::string text;
        double      timeout = -1, fontSize = 13.0;
        eIcons      icon = ICON_NONE;
        CHyprColor  color(0);
        ptr         l = fields;
        while (Spairp(l) && Spairp(Scdr(l))) {
            const std::string k = schemeDatumToStr(Scar(l));
            const ptr         v = Scar(Scdr(l));
            if (k == "text") {
                if (!Sstringp(v)) {
                    g_configError = "hl-notification-add!: 'text must be a string";
                    return Sfalse;
                }
                text = schemeDatumToStr(v);
            } else if (k == "timeout" || k == "duration" || k == "time") {
                if (!Sfixnump(v) && !Sflonump(v)) {
                    g_configError = "hl-notification-add!: 'timeout must be a number (ms)";
                    return Sfalse;
                }
                timeout = Sfixnump(v) ? sc<double>(Sfixnum_value(v)) : Sflonum_value(v);
                if (timeout < 0) {
                    g_configError = "hl-notification-add!: 'timeout must be >= 0";
                    return Sfalse;
                }
            } else if (k == "icon") {
                bool ok = true;
                icon = schemeIconFromScheme(v, &ok);
                if (!ok) {
                    g_configError = "hl-notification-add!: bad 'icon (expected none/warn/info/hint/error/confused/ok or an id 0-6)";
                    return Sfalse;
                }
            } else if (k == "color") {
                const auto c = schemeColorFromStr(Sstringp(v) ? schemeDatumToStr(v) : (Sfixnump(v) ? std::to_string(Sfixnum_value(v)) : ""));
                if (!c) {
                    g_configError = "hl-notification-add!: bad 'color (expected 0xAARRGGBB)";
                    return Sfalse;
                }
                color = *c;
            } else if (k == "font-size") {
                if (!Sfixnump(v) && !Sflonump(v)) {
                    g_configError = "hl-notification-add!: 'font-size must be a number";
                    return Sfalse;
                }
                fontSize = Sfixnump(v) ? sc<double>(Sfixnum_value(v)) : Sflonum_value(v);
                if (fontSize <= 0) {
                    g_configError = "hl-notification-add!: 'font-size must be > 0";
                    return Sfalse;
                }
            } else {
                g_configError = std::format("hl-notification-add!: unknown field '{}'", k);
                return Sfalse;
            }
            l = Scdr(Scdr(l));
        }
        if (text.empty()) {
            g_configError = "hl-notification-add!: 'text is required";
            return Sfalse;
        }
        if (timeout < 0) {
            g_configError = "hl-notification-add!: 'timeout is required";
            return Sfalse;
        }

        const auto n = Notification::overlay()->addNotification(text, color, sc<float>(timeout), icon, sc<float>(fontSize));
        if (!n)
            return Sfalse;
        return Sinteger((uintptr_t)(new SNotificationHandle(n)));
    }

    static ptr hlNotificationList() {
        if (!g_up)
            return Sfalse;
        std::vector<uintptr_t> ids;
        for (const auto& n : Notification::overlay()->getNotifications())
            ids.push_back((uintptr_t)(new SNotificationHandle(n)));
        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static ptr hlNotificationText(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        if (!n)
            return Sfalse;
        return Sstring_utf8(n->text().c_str(), n->text().size());
    }

    static ptr hlNotificationTimeout(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sflonum(n->timeMs()) : Sfalse;
    }

    static ptr hlNotificationColor(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sinteger(n->color().getAsHex()) : Sfalse;
    }

    static ptr hlNotificationIcon(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sinteger(sc<int>(n->icon())) : Sfalse;
    }

    static ptr hlNotificationFontSize(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sflonum(n->fontSize()) : Sfalse;
    }

    static ptr hlNotificationElapsed(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sflonum(n->timeElapsedMs()) : Sfalse;
    }

    static ptr hlNotificationAge(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sflonum(n->timeElapsedSinceCreationMs()) : Sfalse;
    }

    static ptr hlNotificationAlive(long long id) {
        if (!g_up)
            return Sfalse;
        return notificationFromHandle(id) ? Strue : Sfalse;
    }

    static ptr hlNotificationSame(long long a, long long b) {
        if (!g_up)
            return Sfalse;
        const auto na = notificationFromHandle(a);
        const auto nb = notificationFromHandle(b);
        return (na && nb && na.get() == nb.get()) ? Strue : Sfalse;
    }

    // expired handles mutate as silent no-ops (upstream parity)
    static int hlNotificationTextSet(long long id, const char* text) {
        if (!g_up)
            return -1;
        if (const auto n = notificationFromHandle(id))
            n->setText(std::string(text ? text : ""));
        return 0;
    }

    static int hlNotificationTimeoutSet(long long id, double ms) {
        if (!g_up)
            return -1;
        if (ms < 0) {
            g_configError = "hl-notification-timeout-set!: timeout must be >= 0";
            return -2;
        }
        if (const auto n = notificationFromHandle(id))
            n->resetTimeout(sc<float>(ms));
        return 0;
    }

    static int hlNotificationColorSet(long long id, const char* color) {
        if (!g_up)
            return -1;
        const auto c = schemeColorFromStr(color ? color : "");
        if (!c) {
            g_configError = "hl-notification-color-set!: bad color (expected 0xAARRGGBB)";
            return -2;
        }
        if (const auto n = notificationFromHandle(id))
            n->setColor(*c);
        return 0;
    }

    static int hlNotificationIconSet(long long id, ptr v) {
        if (!g_up)
            return -1;
        bool ok = true;
        const auto icon = schemeIconFromScheme(v, &ok);
        if (!ok) {
            g_configError = "hl-notification-icon-set!: bad icon (expected none/warn/info/hint/error/confused/ok or an id 0-6)";
            return -2;
        }
        if (const auto n = notificationFromHandle(id))
            n->setIcon(icon);
        return 0;
    }

    static int hlNotificationFontSizeSet(long long id, double size) {
        if (!g_up)
            return -1;
        if (size <= 0) {
            g_configError = "hl-notification-font-size-set!: font size must be > 0";
            return -2;
        }
        if (const auto n = notificationFromHandle(id))
            n->setFontSize(sc<float>(size));
        return 0;
    }

    static int hlNotificationPausedSet(long long id, int on) {
        if (!g_up)
            return -1;
        auto* h = reinterpret_cast<SNotificationHandle*>(id);
        if (const auto n = h->wp.lock()) {
            if (on && !h->paused) {
                n->lock();
                h->paused = true;
            } else if (!on && h->paused) {
                n->unlock();
                h->paused = false;
            }
        }
        return 0;
    }

    static ptr hlNotificationPausedQ(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        if (!n)
            return Sfalse;
        return n->isLocked() ? Strue : Sfalse;
    }

    static int hlNotificationDismiss(long long id) {
        if (!g_up)
            return -1;
        if (const auto n = notificationFromHandle(id))
            Notification::overlay()->dismissNotification(n);
        return 0;
    }

    // ---- timer handles ----------------------------------------------------------


    static STimerEntry* timerByRecord(ptr record) {
        const auto it = g_timerIndex.find(reinterpret_cast<uintptr_t>(record));
        return it == g_timerIndex.end() ? nullptr : &it->second;
    }

    static int hlTimerSetEnabled(ptr record, int enabled) {
        if (!g_up)
            return -1;
        auto* e = timerByRecord(record);
        if (!e)
            return -1;
        if (enabled != 0)
            e->timer->updateTimeout(std::chrono::milliseconds(sc<int64_t>(e->ms)));
        else
            e->timer->updateTimeout(std::nullopt);
        return 0;
    }

    static int hlTimerEnabled(ptr record) {
        const auto* e = timerByRecord(record);
        return (e && e->timer && e->timer->armed()) ? 1 : 0;
    }

    static int hlTimerSetTimeout(ptr record, double ms) {
        if (!g_up)
            return -1;
        auto* e = timerByRecord(record);
        if (!e || ms < 1)
            return -1;
        e->ms = sc<uint64_t>(ms);
        e->timer->updateTimeout(std::chrono::milliseconds(sc<int64_t>(ms)));
        return 0;
    }

    static int hlTimerCancel(ptr record) {
        if (!g_up || !g_pEventLoopManager)
            return -1;
        auto* e = timerByRecord(record);
        if (!e)
            return -1;
        e->timer->cancel();
        g_pEventLoopManager->removeTimer(e->timer);
        g_timerIndex.erase(reinterpret_cast<uintptr_t>(record));
        return 0;
    }

    // ---- gestures ---------------------------------------------------------------
    // A scheme thunk (or three, for live gestures) behind the trackpad gesture
    // system. Registered gestures are cleared by the config reload (the gesture
    // manager clears itself), so no extra bookkeeping is needed.

    // gesture event plist — upstream pushGestureEvent parity (LuaFunctionGesture
    // .cpp:42-122). The handler is APPLIED the plist fields, so callbacks
    // destructure them as normal lambda args:
    //   (lambda (phase direction type time-ms fingers delta . rest))   ; begin/update
    //   (lambda (phase direction type time-ms cancelled) ...)          ; end
    static ptr gestureEventPlist(std::vector<ptr>& roots, const char* phase, const std::string& dir,
                                 const char* type, uint32_t timeMs, std::optional<int> fingers, ptr deltaPair,
                                 ptr scale, ptr rotation, ptr cancelled) {
        std::vector<ptr> elems;
        auto push = [&](ptr p) { marshRoot(p, roots); elems.push_back(p); };
        push(Sstring_to_symbol("phase"));      push(Sstring_utf8(phase, strlen(phase)));
        push(Sstring_to_symbol("direction"));  push(Sstring_utf8(dir.c_str(), dir.size()));
        push(Sstring_to_symbol("type"));       push(Sstring_utf8(type, strlen(type)));
        push(Sstring_to_symbol("time-ms"));    elems.push_back(Sinteger((int)timeMs));
        if (fingers) { push(Sstring_to_symbol("fingers")); elems.push_back(Sinteger(*fingers)); }
        if (deltaPair) { push(Sstring_to_symbol("delta")); push(deltaPair); }
        if (scale && scale != Sfalse) { push(Sstring_to_symbol("scale")); push(scale); }
        if (rotation && rotation != Sfalse) { push(Sstring_to_symbol("rotation")); push(rotation); }
        if (cancelled && cancelled != Sfalse) { push(Sstring_to_symbol("cancelled")); push(cancelled); }
        ptr l = Snil;
        for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
            l = Scons(*it, l);
            marshRoot(l, roots);
        }
        return l;
    }

    // bare-thunk variant: gestures carry their callbacks directly (SThunkRef
    // members), so the fire passes the callable itself; the plist is APPLIED
    // to it (spread args)
    static void fireSchemeList(ptr thunk, ptr lst) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-list")), thunk, lst);
        watchdogExit();
    }

    template <typename E>
    static void fireSchemeGestureEvent(ptr thunk, const char* phase, const E& e, const std::string& dir) {
        if (!g_up)
            return;
        std::vector<ptr>      roots;
        constexpr bool        IS_END = std::is_same_v<E, ITrackpadGesture::STrackpadGestureEnd>;
        ptr                   delta  = Snil, scale = Sfalse, rotation = Sfalse;
        if constexpr (!IS_END) {
            const auto& d = e.swipe ? e.swipe->delta : e.pinch->delta;
            ptr x = Sflonum(d.x); marshRoot(x, roots);
            ptr y = Sflonum(d.y); marshRoot(y, roots);
            delta = Scons(x, y); marshRoot(delta, roots);
            if (e.pinch) {
                scale = Sflonum(e.pinch->scale); marshRoot(scale, roots);
                rotation = Sflonum(e.pinch->rotation); marshRoot(rotation, roots);
            }
        }
        ptr cancelled = Sfalse;
        if constexpr (IS_END)
            cancelled = (e.swipe ? e.swipe->cancelled : e.pinch->cancelled) ? Strue : Sfalse;
        std::optional<int> fingers;
        if constexpr (!IS_END)
            fingers = (int)(e.swipe ? e.swipe->fingers : e.pinch->fingers);
        ptr lst = gestureEventPlist(roots, phase, dir,
                                    e.swipe ? "swipe" : "pinch",
                                    e.swipe ? e.swipe->timeMs : e.pinch->timeMs,
                                    fingers,
                                    delta, scale, rotation, cancelled);
        marshRelease(roots);
        fireSchemeList(thunk, lst);
    }

    class CSchemeGesture : public ITrackpadGesture {
      public:
        // the callbacks arrive as thunks and are carried locked; Snil means
        // "unused" (the counted lock no-ops on immediates). The gesture
        // manager destroys us at config reload, which unlocks them.
        CSchemeGesture(ptr begin, ptr update, ptr end, const char* direction) :
            m_begin(begin), m_update(update), m_end(end), m_direction(direction ? direction : "") {}

        void  begin(const STrackpadGestureBegin& e) override {
            if (!Snullp(m_begin.obj))
                fireSchemeGestureEvent(m_begin.obj, "start", e, m_direction);
        }
        void  update(const STrackpadGestureUpdate& e) override {
            if (!Snullp(m_update.obj))
                fireSchemeGestureEvent(m_update.obj, "update", e, m_direction);
        }
        void  end(const STrackpadGestureEnd& e) override {
            if (!Snullp(m_end.obj))
                fireSchemeGestureEvent(m_end.obj, "end", e, m_direction);
        }

      private:
        SThunkRef   m_begin, m_update, m_end;
        std::string m_direction;
    };

    // ---- gesture action recipes (upstream's hl.gesture action strings, done
    // as typed objects) ------------------------------------------------------
    // A recipe is a Scheme value (maker . args): maker is the address of one
    // of the stateless singleton factories below, args a flat plist with
    // symbol keys (the house plist shape). Built-in and custom actions are
    // indistinguishable to the caller — hl-gesture-add! asks the factory to
    // construct the ITrackpadGesture and moves it straight into the manager
    // (owned from birth, destroyed at reload). The recipe itself is a pure
    // value: registering it twice constructs two independent instances.
    static ptr gestureArgGet(ptr args, const char* key) {
        for (ptr l = args; Spairp(l) && Spairp(Scdr(l)); l = Scdr(Scdr(l)))
            if (Ssymbolp(Scar(l)) && schemeDatumToStr(Scar(l)) == key)
                return Scar(Scdr(l));
        return Sfalse;
    }

    static std::string gestureArgStr(ptr args, const char* key) {
        // symbol (mode tags) or string (special workspace name) values
        const ptr v = gestureArgGet(args, key);
        return (Ssymbolp(v) || Sstringp(v)) ? schemeDatumToStr(v) : std::string();
    }

    static double gestureArgDouble(ptr args, const char* key) {
        const ptr v = gestureArgGet(args, key);
        return Sflonump(v) ? Sflonum_value(v) : 1.0;
    }

    class IGestureMaker {
      public:
        virtual UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection dir) = 0;
        virtual ~IGestureMaker() = default;
    };

    // the five no-argument built-ins
    template <typename G>
    class CTrivialGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr, eTrackpadGestureDirection) override {
            return makeUnique<G>();
        }
    };
    static CTrivialGestureMaker<CWorkspaceSwipeGesture>     s_workspaceSwipeGestureMaker;
    static CTrivialGestureMaker<CMoveTrackpadGesture>       s_moveGestureMaker;
    static CTrivialGestureMaker<CResizeTrackpadGesture>     s_resizeGestureMaker;
    static CTrivialGestureMaker<CCloseTrackpadGesture>      s_closeGestureMaker;
    static CTrivialGestureMaker<CScrollMoveTrackpadGesture> s_scrollMoveGestureMaker;

    class CFloatGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection) override {
            return makeUnique<CFloatTrackpadGesture>(gestureArgStr(args, "mode"));
        }
    };
    static CFloatGestureMaker s_floatGestureMaker;

    class CFullscreenGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection) override {
            return makeUnique<CFullscreenTrackpadGesture>(gestureArgStr(args, "mode"));
        }
    };
    static CFullscreenGestureMaker s_fullscreenGestureMaker;

    class CSpecialWorkspaceGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection) override {
            return makeUnique<CSpecialWorkspaceGesture>(gestureArgStr(args, "name"));
        }
    };
    static CSpecialWorkspaceGestureMaker s_specialWorkspaceGestureMaker;

    class CCursorZoomGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection) override {
            // the underlying ctor parses a zoom string; format our typed number
            return makeUnique<CCursorZoomTrackpadGesture>(std::format("{}", gestureArgDouble(args, "zoom")), gestureArgStr(args, "mode"));
        }
    };
    static CCursorZoomGestureMaker s_cursorZoomGestureMaker;

    // custom: the args are the three thunks ((start . T) (update . T)
    // (finish . T)); CSchemeGesture locks them into SThunkRef members
    class CCustomGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection dir) override {
            const auto toNil = [](ptr p) { return p == Sfalse ? Snil : p; };
            return makeUnique<CSchemeGesture>(toNil(gestureArgGet(args, "start")), toNil(gestureArgGet(args, "update")),
                                              toNil(gestureArgGet(args, "finish")), g_pTrackpadGestures->stringForDir(dir));
        }
    };
    static CCustomGestureMaker s_customGestureMaker;

    // validate an inbound recipe's maker slot: only our ten singletons are
    // legal values (rejects forged pairs and other handle families' integers)
    static IGestureMaker* gestureMakerFromAddress(long long addr) {
        IGestureMaker* makers[] = {&s_workspaceSwipeGestureMaker, &s_moveGestureMaker,       &s_resizeGestureMaker,
                                   &s_closeGestureMaker,          &s_scrollMoveGestureMaker, &s_floatGestureMaker,
                                   &s_fullscreenGestureMaker,     &s_specialWorkspaceGestureMaker,
                                   &s_cursorZoomGestureMaker,     &s_customGestureMaker};
        for (auto* m : makers)
            if (addr == sc<long long>(reinterpret_cast<intptr_t>(m)))
                return m;
        return nullptr;
    }

    // the one mods representation across the API: a LIST of modifier tokens
    // (strings), as built by hl-kbd/hl-key — never a string to split and
    // never a raw mask int. Unknown tokens / non-strings are rejected.
    static std::optional<Input::ModifierMask> modsMaskFromTokens(ptr mods) {
        uint8_t raw = 0;
        for (ptr l = mods; Spairp(l); l = Scdr(l)) {
            if (!Sstringp(Scar(l))) {
                g_configError = "'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\")";
                return std::nullopt;
            }
            std::string tok = schemeDatumToStr(Scar(l));
            std::transform(tok.begin(), tok.end(), tok.begin(), ::toupper);
            uint8_t bit = 0;
            if (tok == "SHIFT")
                bit = 1;
            else if (tok == "CAPS")
                bit = 2;
            else if (tok == "CTRL" || tok == "CONTROL")
                bit = 4;
            else if (tok == "ALT")
                bit = 8;
            else if (tok == "MOD3")
                bit = 32;
            else if (tok == "SUPER" || tok == "META" || tok == "MOD2")
                bit = 64;
            else if (tok == "MOD5")
                bit = 128;
            else {
                g_configError = std::string("unknown modifier token '") + tok + "'";
                return std::nullopt;
            }
            raw |= bit;
        }
        return Input::ModifierMask(sc<Input::eKeyboardModifiers>(raw));
    }

    // (recipe, fingers, direction, mods, scale, disableInhibit) → 0 ok;
    // recipe is (maker . args) — the maker constructs the ITrackpadGesture
    // (built-in or custom alike), which moves into the manager, owned from
    // birth; destroyed at config reload, which also unlocks any thunks it
    // carried. The addGesture result (overshadow rules) is checked.
    static int hlSchemeGesture(ptr recipe, int fingers, const char* direction, ptr mods, double scale, int disableInhibit) {
        if (!g_up || !g_pTrackpadGestures)
            return -1;
        const auto dir = g_pTrackpadGestures->dirForString(direction ? direction : "");
        if (dir == TRACKPAD_GESTURE_DIR_NONE) {
            g_configError = std::string("hl-gesture: invalid direction '") + (direction ? direction : "") + "'";
            return -1;
        }
        const auto mask = modsMaskFromTokens(mods);
        if (!mask)
            return -1;
        if (!Spairp(recipe) || !Sfixnump(Scar(recipe))) {
            g_configError = "hl-gesture: 'action is not a gesture action (see the hl-make-*-gesture constructors)";
            return -1;
        }
        const auto maker = gestureMakerFromAddress(Sfixnum_value(Scar(recipe)));
        if (!maker) {
            g_configError = "hl-gesture: 'action is not a valid gesture action (see the hl-make-*-gesture constructors)";
            return -1;
        }
        auto gesture = maker->make(Scdr(recipe), dir);
        const auto result =
            g_pTrackpadGestures->addGesture(std::move(gesture), sc<size_t>(fingers), dir, *mask, sc<float>(scale), disableInhibit != 0);
        if (!result) {
            g_configError = std::string("hl-gesture: ") + result.error();
            return -1;
        }
        return 0;
    }

    // one accessor per maker — the recipe's maker slot is fetched by the
    // hl-make-* constructor that owns it; no dispatch anywhere (adding a
    // gesture = a subclass + a singleton + one of these one-liners)
    static long long makerAddr(IGestureMaker* m) {
        return sc<long long>(reinterpret_cast<intptr_t>(m));
    }
    static long long hlSchemeGestureMakerWorkspaceSwipe() { return makerAddr(&s_workspaceSwipeGestureMaker); }
    static long long hlSchemeGestureMakerMove()           { return makerAddr(&s_moveGestureMaker); }
    static long long hlSchemeGestureMakerResize()         { return makerAddr(&s_resizeGestureMaker); }
    static long long hlSchemeGestureMakerClose()          { return makerAddr(&s_closeGestureMaker); }
    static long long hlSchemeGestureMakerScrollMove()     { return makerAddr(&s_scrollMoveGestureMaker); }
    static long long hlSchemeGestureMakerFloat()          { return makerAddr(&s_floatGestureMaker); }
    static long long hlSchemeGestureMakerFullscreen()     { return makerAddr(&s_fullscreenGestureMaker); }
    static long long hlSchemeGestureMakerSpecial()        { return makerAddr(&s_specialWorkspaceGestureMaker); }
    static long long hlSchemeGestureMakerCursorZoom()     { return makerAddr(&s_cursorZoomGestureMaker); }
    static long long hlSchemeGestureMakerCustom()         { return makerAddr(&s_customGestureMaker); }

    // (fingers, direction, mods, scale, disableInhibit) → 0 removed / 1 no
    // such gesture / -1 error. removeGesture matches on the registration
    // spec (the manager stores one gesture per spec), never on the action.
    static int hlSchemeGestureRemove(int fingers, const char* direction, ptr mods, double scale, int disableInhibit) {
        if (!g_up || !g_pTrackpadGestures)
            return -1;
        const auto dir = g_pTrackpadGestures->dirForString(direction ? direction : "");
        if (dir == TRACKPAD_GESTURE_DIR_NONE) {
            g_configError = std::string("hl-gesture: invalid direction '") + (direction ? direction : "") + "'";
            return -1;
        }
        const auto mask = modsMaskFromTokens(mods);
        if (!mask)
            return -1;
        const auto result = g_pTrackpadGestures->removeGesture(sc<size_t>(fingers), dir, *mask, sc<float>(scale), disableInhibit != 0);
        if (!result) {
            if (result.error() == "Can't remove a non-existent gesture")
                return 1;
            g_configError = std::string("hl-gesture: ") + result.error();
            return -1;
        }
        return 0;
    }


    static ptr hlSchemeWindowInitialClass(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().initialAppID();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static ptr hlSchemeWindowInitialTitle(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().initialTitle();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static int hlSchemeWindowX11(long long id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->backend().isX11()) ? 1 : 0;
    }

    // ---- layer surfaces as objects (upstream HL.LayerSurface parity) ----------
    // layer surfaces die independently -> weak handles via the guardian
    // (record-cell model); every getter returns #f when the surface is gone
    using PHLLSGROUPREF = Hyprutils::Memory::CWeakPointer<Desktop::View::CLayerSurface>;

    static SP<Desktop::View::CLayerSurface> layerFromHandle(long long id) {
        return reinterpret_cast<SHandle<PHLLSGROUPREF>*>(id)->wp.lock();
    }

    static int hlSchemeLayerAlive(long long id) {
        if (!g_up)
            return -1;
        return layerFromHandle(id) ? 1 : 0;
    }

    static int hlSchemeLayerSame(long long a, long long b) {
        if (!g_up)
            return -1;
        const auto la = layerFromHandle(a);
        const auto lb = layerFromHandle(b);
        return (la && lb && la.get() == lb.get()) ? 1 : 0;
    }

    static ptr hlSchemeLayerAddress(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        const auto addr = std::format("0x{:x}", reinterpret_cast<uintptr_t>(ls.get()));
        return Sstring_utf8(addr.c_str(), addr.size());
    }

    static ptr hlSchemeLayerPid(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        return ls ? Sinteger(sc<int64_t>(ls->getPID())) : Sfalse;
    }

    static ptr hlSchemeLayerMonitorId(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        const auto mon = ls->m_monitor.lock();
        if (!mon)
            return Sfalse;
        return Sinteger((uintptr_t)(new SHandle<PHLMONITORREF>(mon)));
    }

    static ptr hlSchemeLayerNamespace(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return Sstring_utf8(ls->m_namespace.c_str(), ls->m_namespace.size());
    }

    static ptr hlSchemeLayerLevel(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        return ls ? Sinteger(sc<int64_t>(ls->m_layer)) : Sfalse;
    }

    static ptr hlSchemeLayerMapped(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return ls->mapped() ? Strue : Sfalse;
    }

    static ptr hlSchemeLayerKbInteractivity(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        return ls ? Sinteger(sc<int64_t>(ls->m_keyboardInteractivity)) : Sfalse;
    }

    static ptr hlSchemeLayerAboveFullscreen(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return (ls->m_flags & Desktop::View::LAYER_FLAG_ABOVE_FULLSCREEN) ? Strue : Sfalse;
    }

    static ptr hlSchemeLayerPosition(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return Scons(Sinteger((int)ls->m_geometry.x), Sinteger((int)ls->m_geometry.y));
    }

    static ptr hlSchemeLayerSize(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return Scons(Sinteger((int)ls->m_geometry.width), Sinteger((int)ls->m_geometry.height));
    }

    // (monitorFilter, namespaceFilter) — nullptr/empty = no filter; the
    // monitor crosses as a handle id (0 = none) after Scheme-side coercion
    static ptr hlLayers(long long monId, ptr nsFilter) {
        if (!g_up)
            return Sfalse;
        const std::string ns = nsFilter && Sstringp(nsFilter) ? schemeDatumToStr(nsFilter) : "";
        PHLWINDOWREF dummy; // unused; keeps the compiler from warning on the include order
        (void)dummy;
        PHLMONITOR monFilter;
        if (monId > 0)
            monFilter = reinterpret_cast<SHandle<PHLMONITORREF>*>(monId)->wp.lock();

        std::vector<uintptr_t> ids;
        for (const auto& mon : State::monitorState()->monitors()) {
            if (monFilter && mon != monFilter)
                continue;
            for (const auto& level : mon->m_layerSurfaceLayers) {
                for (const auto& lsRef : level) {
                    const auto ls = lsRef.lock();
                    if (!ls)
                        continue;
                    if (!ns.empty() && ls->m_namespace != ns)
                        continue;
                    ids.push_back((uintptr_t)(new SHandle<PHLLSGROUPREF>(ls)));
                }
            }
        }
        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static ptr hlSchemeMonitorNames() {
        if (!g_up)
            return Sfalse;

        std::vector<uintptr_t> ids;
        for (const auto& m : State::monitorState()->monitors()) {
            ids.push_back((uintptr_t)(new SHandle<PHLMONITORREF>(m)));
        }

        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static int hlSchemeWindowEventListen(ptr record, int which) {
        if (!g_up)
            return -1;

        switch (which) {
            case 0: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.openLate.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 1: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.close.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 2: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.title.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 3: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.class_.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 4: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.urgent.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 5: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.pin.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 6: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.fullscreen.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 7: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.moveToWorkspace.listen([ref = SThunkRef(record)](PHLWINDOW w, PHLWORKSPACE ws) { fireSchemeWin(ref.obj, w); })); break;
            case 8: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.active.listen([ref = SThunkRef(record)](PHLWINDOW w, Desktop::eFocusReason) { fireSchemeWin(ref.obj, w); })); break;
            case 9: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.openEarly.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 10: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.kill.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 11: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.bell.listen([ref = SThunkRef(record)](PHLWINDOW w, Event::SCallbackInfo&) { fireSchemeWin(ref.obj, w); })); break;
            case 12: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.updateRules.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            default: break;
        }

        return 0;
    }

    // minimize fires with (window, state): pass the bool as a second arg
    static int hlSchemeWindowMinimizeListen(ptr record) {
        if (!g_up)
            return -1;

        SThunkRef ref(record);
        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.minimize.listen([ref](PHLWINDOW w, bool state) {
            if (!g_up || !w)
                return;
            const auto winId = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
            Scall3(Stop_level_value(Sstring_to_symbol("hl--fire-win-state-rec")), ref.obj, Sinteger(winId), state ? Strue : Sfalse);
        }));
        return 0;
    }

    // lifecycle: 0 = start (session's first render frame), 1 = shutdown
    // (the exit action). Matches upstream: a handler registered after start
    // already fired (only possible when the plugin itself loaded before the
    // first frame) runs on the next loop pass.
    static int hlSchemeLifecycleListen(ptr record, int which) {
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
            g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.exit.listen([ref] { fireScheme(ref.obj); }));

        return 0;
    }

    static int hlSchemeMonitorListen(ptr record, int which) {
        if (!g_up)
            return -1;
        switch (which) {
            case 0: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.monitor.added.listen([ref = SThunkRef(record)](PHLMONITOR m) { fireSchemeMon(ref.obj, m); })); break;
            case 1: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.monitor.removed.listen([ref = SThunkRef(record)](PHLMONITOR m) { fireSchemeMon(ref.obj, m); })); break;
            case 2: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.monitor.focused.listen([ref = SThunkRef(record)](PHLMONITOR m) { fireSchemeMon(ref.obj, m); })); break;
            case 3: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.monitor.layoutChanged.listen([ref = SThunkRef(record)] { fireScheme(ref.obj); })); break;
            default: break;
        }
        return 0;
    }

    static int hlSchemeWorkspaceListen(ptr record, int which) {
        if (!g_up)
            return -1;
        switch (which) {
            case 0: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.created.listen([ref = SThunkRef(record)](PHLWORKSPACEREF ws) { auto w = ws.lock(); if (w) fireSchemeWs(ref.obj, w); })); break;
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
            case 1: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.removed.listen([ref = SThunkRef(record)](PHLWORKSPACEREF ws) { fireSchemeWsRef(ref.obj, ws); })); break;
            // fires (ws mon) handles; ws is #f when no special workspace is
            // open on the monitor (upstream crosses nil the same way)
            case 2: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.specialActive.listen([ref = SThunkRef(record)](PHLWORKSPACE ws, PHLMONITOR mon) { fireSchemeWsMon(ref.obj, ws, mon); })); break;
            case 3: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.moveToMonitor.listen([ref = SThunkRef(record)](PHLWORKSPACE ws, PHLMONITOR mon) { fireSchemeWsMon(ref.obj, ws, mon); })); break;
            default: break;
        }
        return 0;
    }

    static int hlSchemeConfigReloadedListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.config.reloaded.listen([ref = SThunkRef(record)] { fireScheme(ref.obj); }));
        return 0;
    }

    // config.preReload — upstream maps config.unload onto it
    static int hlSchemeConfigUnloadListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.config.preReload.listen([ref = SThunkRef(record)] { fireScheme(ref.obj); }));
        return 0;
    }

    // window.destroy: zero-argument callback (upstream delivers nil — the bus
    // event is Event<PHLWINDOWREF>; identity belongs to the window-close notification)
    static int hlSchemeWindowDestroyListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.destroy.listen([ref = SThunkRef(record)](PHLWINDOWREF) { fireScheme(ref.obj); }));
        return 0;
    }

    // screenshare.state — callbacks receive (active? type name); upstream
    // dispatches 3 positional args (LuaEventHandler.cpp:166)
    static int hlSchemeScreenshareListen(ptr record) {
        if (!g_up)
            return -1;
        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.screenshare.state.listen([ref = SThunkRef(record)](bool state, uint8_t type, const std::string& name) {
            if (!g_up)
                return;
            std::vector<ptr> roots;
            ptr nm = Sstring_utf8(name.c_str(), name.size());
            marshRoot(nm, roots);
            ptr lst = Scons(state ? Strue : Sfalse, Scons(Sinteger((int)type), Scons(nm, Snil)));
            marshRoot(lst, roots);
            marshRelease(roots);
            fireSchemeListRec(ref.obj, lst);   // (active? type name)
        }));
        return 0;
    }

    // input.keyboard.key — high-frequency (every key event); handlers must be
    // trivial. Observe-only: the bus event is Cancellable, Scheme listeners
    // never take the cancellation. keycode is +8 (libinput → xkb), as upstream.
    static int hlSchemeKeyboardKeyListen(ptr record) {
        if (!g_up)
            return -1;
        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.input.keyboard.key.listen([ref = SThunkRef(record)](const IKeyboard::SKeyEvent& keyEvent, Event::SCallbackInfo& _) {
            if (!g_up)
                return;
            fireSchemeListRec(ref.obj, schemeIntList({(int)keyEvent.keycode + 8, (int)keyEvent.timeMs, (int)keyEvent.state}));
        }));
        return 0;
    }

    // layer.opened / layer.closed — callbacks receive the layer's namespace
    static int hlSchemeLayerListen(ptr record, int which) {
        if (!g_up)
            return -1;

        if (which == 0)
            g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.layer.opened.listen([ref = SThunkRef(record)](PHLLS ls) { if (g_up && ls) fireSchemeStr(ref.obj, ls->m_namespace); }));
        else
            g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.layer.closed.listen([ref = SThunkRef(record)](PHLLS ls) { if (g_up && ls) fireSchemeStr(ref.obj, ls->m_namespace); }));
        return 0;
    }

    // handler receives #t when the prop refresh ran as scheduled, #f when it
    // was executed prematurely
    static int hlSchemePropsRefreshedListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.config.props_refreshed.listen([ref = SThunkRef(record)](const bool scheduled) { fireSchemeBool(ref.obj, scheduled); }));
        return 0;
    }

    // unlisten: drop the connection; its destruction unregisters from the
    // bus and releases the handler record's lock. Upstream HL.EventSub-
    // scription:remove parity. The record goes inert: cancel again -> -1.
    static int hlSchemeEventCancel(ptr record) {
        if (!g_up)
            return -1;
        const auto it = g_eventConnections.find(reinterpret_cast<uintptr_t>(record));
        if (it == g_eventConnections.end())
            return -1;
        g_eventConnections.erase(it);
        return 0;
    }

    // upstream HL.EventSubscription:is_active parity
    static int hlSchemeEventActive(ptr record) {
        if (!g_up)
            return -1;
        return g_eventConnections.count(reinterpret_cast<uintptr_t>(record)) ? 1 : 0;
    }

    // identity, mirroring Lua's windowEq: two handles are the same window
    // iff they lock to the same underlying object. Dead handles are never
    // "the same" as anything.
    static int hlSchemeWindowSame(long long idA, long long idB) {
        if (!g_up)
            return 0;

        const auto a = windowFromId(idA);
        const auto b = windowFromId(idB);
        return (a && b && a.get() == b.get()) ? 1 : 0;
    }

    static ptr hlSchemeCurrentSubmap() {
        if (!g_up || !Keybinds::mgr())
            return Sfalse;

        const auto submap = std::string(Keybinds::mgr()->currentSubmap());
        return Sstring_utf8(submap.c_str(), submap.size());
    }

    static ptr hlSchemeCursorPos() {
        if (!g_up || !Pointer::mgr())
            return Sfalse;

        const auto pos = Pointer::mgr()->untransformedPosition();
        return Scons(Sinteger((int)pos.x), Sinteger((int)pos.y));   // (x . y)
    }

    static int hlSchemeWorkspaceActiveListen(ptr record) {
        if (!g_up)
            return -1;

        SThunkRef ref(record);
        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.active.listen([ref](PHLWORKSPACE ws) {
            fireSchemeWs(ref.obj, ws);
        }));
        return 0;
    }

    static void reloadScheme() {
        if (!g_up || g_configPath.empty())
            return;

        // the config reload cleared the rule engine; drop our rule state so
        // the fresh config run re-registers everything
        clearSchemeRules();

        // lua config reloads clear every bind in the registry; that
        // destruction runs our capture destructors, which release the
        // record locks — nothing to clean up here (we hold no references)

        // timers from the previous generation must not fire into the new one;
        // destroying them releases their record locks via the capture dtor
        if (g_pEventLoopManager) {
            for (auto& [addr, e] : g_timerIndex) {
                e.timer->cancel();
                g_pEventLoopManager->removeTimer(e.timer);
            }
            g_timerIndex.clear();
        }

        // drop event subscriptions; the new generation re-registers. This is
        // wholesale BY DESIGN — a reload means fresh handlers, and it is the
        // handler lifetime (each entry's destruction also unlocks its record)
        g_eventConnections.clear();

        // old-generation handles: their Scheme records became unreachable
        // with the generation, so the guardian reaps them at the next GC

        // layouts unregister + re-register with the new generation
        Layouts::clear();

        // re-binding the config value caches: CConfigValue readers (e.g. the
        // layout matcher's general:layout) hold copies made at first use, and
        // this reload may have changed what they should see.
        CConfigValueBase::flushCaches();

        Scall0(Stop_level_value(Sstring_to_symbol("hl--reset")));

        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--load")), Sstring(g_configPath.c_str()));
        if (r == Sfalse)
            LOG(Log::ERR, "[scheme] failed to load {}", g_configPath);
        else
            LOG(Log::INFO, "[scheme] loaded {}", g_configPath);
    }

    static void drainWatch() {
        alignas(struct inotify_event) char buf[4096];
        bool                               dirty = false;

        while (true) {
            const ssize_t n = read(g_watchFd, buf, sizeof(buf));
            if (n <= (ssize_t)sizeof(struct inotify_event))
                break;

            for (ssize_t off = 0; off + (ssize_t)sizeof(struct inotify_event) <= n;) {
                const auto* ev = reinterpret_cast<const struct inotify_event*>(&buf[off]);
                if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)) && ev->len > 0 &&
                    g_configPath.ends_with(ev->name))
                    dirty = true;
                off += (ssize_t)sizeof(struct inotify_event) + ev->len;
            }
        }

        if (dirty)
            reloadScheme();
    }

    // event loop waiters are one-shot: re-arm after every wakeup.
    static void armWatch() {
        if (g_watchFd < 0 || !g_pEventLoopManager)
            return;

        g_pEventLoopManager->doOnReadable(CFileDescriptor(dup(g_watchFd)), [] {
            drainWatch();
            armWatch();
        });
    }

    static void setupWatch() {
        const auto dir = std::filesystem::path(g_configPath).parent_path().string();

        g_watchFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (g_watchFd < 0)
            return;

        if (inotify_add_watch(g_watchFd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0) {
            close(g_watchFd);
            g_watchFd = -1;
            return;
        }

        armWatch();
    }

    // the plugin's own directory: where the .scm machinery and the boot
    // files are installed (same mechanism the boot-file lookup uses)
    static std::string pluginDir() {
        Dl_info info{};
        if (dladdr((void*)&init, &info) && info.dli_fname)
            return std::filesystem::path(info.dli_fname).parent_path().string();
        return {};
    }

    static std::string userConfigPath() {
        // HYPRSCHEME_CONFIG overrides the default location, mirroring how
        // HYPRLAND_CONFIG overrides the compositor's config discovery
        // (Jeremy::getMainConfigPath returns the env path verbatim, no
        // canonicalization)
        if (const char* overridePath = getenv("HYPRSCHEME_CONFIG"); overridePath && *overridePath)
            return overridePath;

        const char* cfg = getenv("XDG_CONFIG_HOME");
        std::string base;
        if (cfg && *cfg)
            base = cfg;
        else if (const char* home = getenv("HOME"); home && *home)
            base = std::string(home) + "/.config";
        else
            return {};

        return base + "/hypr/hyprland.scm";
    }

    // reimplements Compositor.cpp's handleUnrecoverableSignal using the
    // exported CrashReporter::createAndSaveCrash
    static void schemeCrashHandler(int sig) {
        signal(SIGABRT, SIG_DFL);
        signal(SIGSEGV, SIG_DFL);
        signal(SIGALRM, [](int) {
            const char* m = "\nCrashReporter exceeded timeout, forcefully exiting\n";
            [[maybe_unused]] auto w = write(2, m, strlen(m));
            abort();
        });
        alarm(15);
        CrashReporter::createAndSaveCrash(sig);
        abort();
    }

    static SP<IPC::Socket1::SCommand> g_schemeIpcCommand;
    static Hyprutils::Signal::CHyprSignalListener g_ipcReadyListener;

    // hyprctl scheme '<forms>' — evaluate scheme in the compositor. The
    // registration is deferred to the ready event when the plugin loads
    // during the EARLY config load (before STAGE_LATE creates Socket1) —
    // a silent `if (sock())` skip here once cost a whole debugging day.
    static void registerIpc() {
        if (g_schemeIpcCommand)
            return;
        if (!g_pEventLoopManager || !IPC::Socket1::sock()) {
            // a dedicated static, NOT g_lifecycleListeners: the vector
            // reallocates on growth, destroying RAII listeners mid-flight
            if (!g_ipcReadyListener)
                g_ipcReadyListener = Event::bus()->m_events.ready.listen([] { registerIpc(); });
            return;
        }
        g_schemeIpcCommand = IPC::Socket1::sock()->registerCommand(IPC::Socket1::SCommand{
            .name    = "scheme",
            .match   = IPC::Socket1::COMMAND_MATCH_PREFIX,
            .handler = [](const IPC::Socket1::SRequest& req) {
                auto code = req.command.substr(req.command.find_first_of(' ') + 1);
                watchdogEnter("eval");
                const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--eval")), Sstring_utf8(code.c_str(), code.size()));
                watchdogExit();
                std::string out;
                if (Sstringp(r)) {
                    for (iptr i = 0; i < Sstring_length(r); ++i)
                        out += (char)Sstring_ref(r, i);
                }
                return IPC::Socket1::SResponse(out);
            }});
        LOG(Log::INFO, "[scheme] ipc command registered");
    }

    // teardown for plugin unload: everything pointing into this .so must be
    // unregistered before hyprpm dlcloses it
    static Hyprutils::Signal::CHyprSignalListener g_reloadListener;

    void shutdown() {
        if (g_schemeIpcCommand) {
            IPC::Socket1::sock()->unregisterCommand(g_schemeIpcCommand);
            g_schemeIpcCommand.reset();
        }
        g_ipcReadyListener.reset();
        g_reloadListener.reset();
        g_lifecycleListeners.clear();
        g_eventConnections.clear();   // user handlers point into this .so
        Layouts::clear();
        // remove our binds before the .so unmaps (their callbacks point
        // here): ours are the ones tagged "scheme:" in their argument.
        // Collect first, then remove — erasing invalidates the registry span.
        if (Keybinds::mgr()) {
            std::vector<Keybinds::PBind> ours;
            for (const auto& b : Keybinds::mgr()->registry().binds())
                if (b && b->metadata().argument.rfind("scheme:", 0) == 0)
                    ours.push_back(b);
            for (const auto& b : ours)
                Keybinds::mgr()->removeBind(b);
        }
        // the interpreter stays alive: Chez does not survive a teardown +
        // re-init inside the compositor (the second Sbuild_heap hangs), so
        // a re-load of the plugin re-attaches to the live interpreter
        g_up = false;
    }

    // attachInterp: register foreign symbols and load the prelude + API
    // bootstrap. Called on first init and on every soft reload (so a
    // re-loaded plugin picks up its new scheme API against the live
    // interpreter). Returns false when the bootstrap failed.
    static bool attachInterp() {
        Sregister_symbol("hl-scheme-bind", (void*)hlSchemeBind);
        Sregister_symbol("hl-scheme-timer", (void*)hlSchemeTimer);
        Sregister_symbol("hl-scheme-active-title", (void*)hlSchemeActiveTitle);
        Sregister_symbol("hl-scheme-workspace-names", (void*)hlSchemeWorkspaceNames);
        Sregister_symbol("hl-scheme-submap-listen", (void*)hlSchemeSubmapListen);
        Sregister_symbol("hl-scheme-active-window-id", (void*)hlSchemeActiveWindowId);
        Sregister_symbol("hl-handle-free", (void*)hlHandleFree);
        Sregister_symbol("hl-scheme-window-ids", (void*)hlSchemeWindowIds);
        Sregister_symbol("hl-scheme-window-title", (void*)hlSchemeWindowTitle);
        Sregister_symbol("hl-scheme-window-alive", (void*)hlSchemeWindowAlive);
        Sregister_symbol("hl-scheme-window-close", (void*)hlSchemeWindowClose);
        Sregister_symbol("hl-scheme-window-class", (void*)hlSchemeWindowClass);
        Sregister_symbol("hl-scheme-window-workspace-id", (void*)hlSchemeWindowWorkspaceId);
        Sregister_symbol("hl-scheme-window-monitor-id", (void*)hlSchemeWindowMonitorId);
        Sregister_symbol("hl-scheme-window-floating", (void*)hlSchemeWindowFloating);
        Sregister_symbol("hl-scheme-window-size", (void*)hlSchemeWindowSize);
        Sregister_symbol("hl-scheme-window-pid", (void*)hlSchemeWindowPid);
        Sregister_symbol("hl-scheme-window-focus", (void*)hlSchemeWindowFocus);
        Sregister_symbol("hl-scheme-window-float", (void*)hlSchemeWindowFloat);
        Sregister_symbol("hl-scheme-window-move-to-workspace", (void*)hlSchemeWindowMoveToWorkspace);
        Sregister_symbol("hl-scheme-monitor-names", (void*)hlSchemeMonitorNames);
        Sregister_symbol("hl-scheme-window-event-listen", (void*)hlSchemeWindowEventListen);
        Sregister_symbol("hl-scheme-window-minimize-listen", (void*)hlSchemeWindowMinimizeListen);
        Sregister_symbol("hl-scheme-lifecycle-listen", (void*)hlSchemeLifecycleListen);
        Sregister_symbol("hl-scheme-config-reloaded-listen", (void*)hlSchemeConfigReloadedListen);
        Sregister_symbol("hl-scheme-config-unload-listen", (void*)hlSchemeConfigUnloadListen);
        Sregister_symbol("hl-scheme-config-props-refreshed-listen", (void*)hlSchemePropsRefreshedListen);
        Sregister_symbol("hl-scheme-window-destroy-listen", (void*)hlSchemeWindowDestroyListen);
        Sregister_symbol("hl-scheme-layer-listen", (void*)hlSchemeLayerListen);
        Sregister_symbol("hl-scheme-screenshare-listen", (void*)hlSchemeScreenshareListen);
        Sregister_symbol("hl-scheme-keyboard-key-listen", (void*)hlSchemeKeyboardKeyListen);
        Sregister_symbol("hl-scheme-unbind-rec", (void*)hlSchemeUnbindRec);
        Sregister_symbol("hl-scheme-unbind-key", (void*)hlSchemeUnbindKey);
        Sregister_symbol("hl-layers", (void*)hlLayers);
        Sregister_symbol("hl-layer-alive", (void*)hlSchemeLayerAlive);
        Sregister_symbol("hl-layer-same", (void*)hlSchemeLayerSame);
        Sregister_symbol("hl-layer-address", (void*)hlSchemeLayerAddress);
        Sregister_symbol("hl-layer-pid", (void*)hlSchemeLayerPid);
        Sregister_symbol("hl-layer-monitor", (void*)hlSchemeLayerMonitorId);
        Sregister_symbol("hl-layer-namespace", (void*)hlSchemeLayerNamespace);
        Sregister_symbol("hl-layer-level", (void*)hlSchemeLayerLevel);
        Sregister_symbol("hl-layer-mapped", (void*)hlSchemeLayerMapped);
        Sregister_symbol("hl-layer-kb-interactivity", (void*)hlSchemeLayerKbInteractivity);
        Sregister_symbol("hl-layer-above-fs", (void*)hlSchemeLayerAboveFullscreen);
        Sregister_symbol("hl-layer-position", (void*)hlSchemeLayerPosition);
        Sregister_symbol("hl-layer-size", (void*)hlSchemeLayerSize);
        Sregister_symbol("hl-scheme-event-cancel", (void*)hlSchemeEventCancel);
        Sregister_symbol("hl-scheme-event-active", (void*)hlSchemeEventActive);
        Sregister_symbol("hl-scheme-window-same", (void*)hlSchemeWindowSame);
        Sregister_symbol("hl-scheme-current-submap", (void*)hlSchemeCurrentSubmap);
        Sregister_symbol("hl-scheme-cursor-pos", (void*)hlSchemeCursorPos);
        Sregister_symbol("hl-scheme-workspace-active-listen", (void*)hlSchemeWorkspaceActiveListen);
        Sregister_symbol("hl-scheme-workspace-event-listen", (void*)hlSchemeWorkspaceListen);
        Sregister_symbol("hl-scheme-monitor-event-listen", (void*)hlSchemeMonitorListen);
        Sregister_symbol("hl-scheme-workspace-change-id", (void*)hlWorkspaceChangeId);
        Sregister_symbol("hl-scheme-window-fullscreen-toggle", (void*)hlSchemeWindowFullscreenToggle);
        Sregister_symbol("hl-scheme-window-fullscreen-set", (void*)hlSchemeWindowFullscreenSet);
        Sregister_symbol("hl-scheme-window-fullscreen-mode", (void*)hlSchemeWindowFullscreenMode);
        Sregister_symbol("hl-scheme-focus-workspace", (void*)hlSchemeFocusWorkspace);
        Sregister_symbol("hl-scheme-window-float-act", (void*)hlSchemeWindowFloatAct);
        Sregister_symbol("hl-scheme-focus-direction", (void*)hlSchemeFocusDirection);
        Sregister_symbol("hl-scheme-focus-monitor", (void*)hlSchemeFocusMonitor);
        Sregister_symbol("hl-scheme-focus-last", (void*)hlSchemeFocusLast);
        Sregister_symbol("hl-scheme-focus-urgent", (void*)hlSchemeFocusUrgent);
        Sregister_symbol("hl-scheme-window-move-direction", (void*)hlSchemeWindowMoveDirection);
        Sregister_symbol("hl-scheme-window-swap-direction", (void*)hlSchemeWindowSwapDirection);
        Sregister_symbol("hl-scheme-window-swap-next", (void*)hlSchemeWindowSwapNext);
        Sregister_symbol("hl-scheme-window-swap-with", (void*)hlSchemeWindowSwapWith);
        Sregister_symbol("hl-scheme-window-cycle", (void*)hlSchemeWindowCycle);
        Sregister_symbol("hl-scheme-window-center", (void*)hlSchemeWindowCenter);
        Sregister_symbol("hl-scheme-window-resize-px", (void*)hlSchemeWindowResizePx);
        Sregister_symbol("hl-scheme-window-move-px", (void*)hlSchemeWindowMovePx);
        Sregister_symbol("hl-scheme-window-pin-act", (void*)hlSchemeWindowPinAct);
        Sregister_symbol("hl-scheme-window-pseudo", (void*)hlSchemeWindowPseudo);
        Sregister_symbol("hl-scheme-window-kill", (void*)hlSchemeWindowKill);
        Sregister_symbol("hl-scheme-window-signal", (void*)hlSchemeWindowSignal);
        Sregister_symbol("hl-scheme-window-zorder", (void*)hlSchemeWindowZOrder);
        Sregister_symbol("hl-scheme-window-set-prop", (void*)hlSchemeWindowSetProp);
        Sregister_symbol("hl-scheme-window-tag", (void*)hlSchemeWindowTag);
        Sregister_symbol("hl-scheme-window-clear-tags", (void*)hlSchemeWindowClearTags);
        Sregister_symbol("hl-scheme-toggle-swallow", (void*)hlSchemeToggleSwallow);
        Sregister_symbol("hl-scheme-workspace-groups", (void*)hlSchemeWorkspaceGroups);
        Sregister_symbol("hl-scheme-group-alive", (void*)hlSchemeGroupAlive);
        Sregister_symbol("hl-scheme-group-same", (void*)hlSchemeGroupSame);
        Sregister_symbol("hl-scheme-group-members", (void*)hlSchemeGroupMembers);
        Sregister_symbol("hl-scheme-group-current", (void*)hlSchemeGroupCurrent);
        Sregister_symbol("hl-scheme-group-current-idx", (void*)hlSchemeGroupCurrentIdx);
        Sregister_symbol("hl-scheme-group-size", (void*)hlSchemeGroupSize);
        Sregister_symbol("hl-scheme-group-locked", (void*)hlSchemeGroupLocked);
        Sregister_symbol("hl-scheme-group-denied", (void*)hlSchemeGroupDenied);
        Sregister_symbol("hl-scheme-group-add", (void*)hlSchemeGroupAdd);
        Sregister_symbol("hl-scheme-group-remove", (void*)hlSchemeGroupRemove);
        Sregister_symbol("hl-scheme-group-toggle", (void*)hlSchemeGroupToggle);
        Sregister_symbol("hl-scheme-group-set", (void*)hlSchemeGroupSet);
        Sregister_symbol("hl-scheme-monitor-set-special", (void*)hlSchemeMonitorSetSpecial);
        Sregister_symbol("hl-scheme-group-cycle", (void*)hlSchemeGroupCycle);
        Sregister_symbol("hl-scheme-group-index", (void*)hlSchemeGroupIndex);
        Sregister_symbol("hl-scheme-group-move-window", (void*)hlSchemeGroupMoveWindow);
        Sregister_symbol("hl-scheme-group-lock", (void*)hlSchemeGroupLock);
        Sregister_symbol("hl-scheme-group-lock-active", (void*)hlSchemeGroupLockActive);
        Sregister_symbol("hl-scheme-window-into-group", (void*)hlSchemeWindowIntoGroup);
        Sregister_symbol("hl-scheme-window-out-of-group", (void*)hlSchemeWindowOutOfGroup);
        Sregister_symbol("hl-scheme-window-into-or-create-group", (void*)hlSchemeWindowIntoOrCreateGroup);
        Sregister_symbol("hl-scheme-window-deny-from-group", (void*)hlSchemeWindowDenyFromGroup);
        Sregister_symbol("hl-scheme-workspace-rename", (void*)hlSchemeWorkspaceRename);
        Sregister_symbol("hl-scheme-workspace-move-monitor", (void*)hlSchemeWorkspaceMoveMonitor);
        Sregister_symbol("hl-scheme-workspace-toggle-special", (void*)hlSchemeWorkspaceToggleSpecial);
        Sregister_symbol("hl-scheme-workspace-swap-monitors", (void*)hlSchemeWorkspaceSwapMonitors);
        Sregister_symbol("hl-scheme-cursor-move", (void*)hlSchemeCursorMove);
        Sregister_symbol("hl-scheme-cursor-corner", (void*)hlSchemeCursorCorner);
        Sregister_symbol("hl-scheme-exit", (void*)hlSchemeExit);
        Sregister_symbol("hl-scheme-reload-config", (void*)hlSchemeReloadConfig);
        Sregister_symbol("hl-scheme-force-renderer-reload", (void*)hlSchemeForceRendererReload);
        Sregister_symbol("hl-scheme-dpms", (void*)hlSchemeDpms);
        Sregister_symbol("hl-scheme-force-idle", (void*)hlSchemeForceIdle);
        Sregister_symbol("hl-scheme-global", (void*)hlSchemeGlobal);
        Sregister_symbol("hl-scheme-event", (void*)hlSchemeEvent);
        Sregister_symbol("hl-scheme-pass", (void*)hlSchemePass);
        Sregister_symbol("hl-scheme-send-shortcut", (void*)hlSchemeSendShortcut);
        Sregister_symbol("hl-scheme-send-key-state", (void*)hlSchemeSendKeyState);
        Sregister_symbol("hl-scheme-mouse", (void*)hlSchemeMouse);
        Sregister_symbol("hl-exec!", (void*)hlSchemeExec);
        Sregister_symbol("hl-scheme-clear-crashed-lockscreen", (void*)hlSchemeClearCrashedLockscreen);
        Sregister_symbol("hl-scheme-scheduled-prop-refresh-immediately", (void*)hlSchemeScheduledPropRefreshImmediately);
        Sregister_symbol("hl-scheme-release-input-capture", (void*)hlSchemeReleaseInputCapture);
        Sregister_symbol("hl-scheme-window-fullscreen-state", (void*)hlSchemeWindowFullscreenState);
        Sregister_symbol("hl-scheme-layout-message", (void*)hlSchemeLayoutMessage);
        Sregister_symbol("hl-config-begin", (void*)hlConfigBegin);
        Sregister_symbol("hl-config-push-int", (void*)hlConfigPushInt);
        Sregister_symbol("hl-config-push-num", (void*)hlConfigPushNum);
        Sregister_symbol("hl-config-push-bool", (void*)hlConfigPushBool);
        Sregister_symbol("hl-config-push-str", (void*)hlConfigPushStr);
        Sregister_symbol("hl-config-tbl-open", (void*)hlConfigTblOpen);
        Sregister_symbol("hl-config-tbl-key", (void*)hlConfigTblKey);
        Sregister_symbol("hl-config-tbl-set-hash", (void*)hlConfigTblSetHash);
        Sregister_symbol("hl-config-tbl-seti", (void*)hlConfigTblSeti);
        Sregister_symbol("hl-config-set", (void*)hlConfigSet);
        Sregister_symbol("hl-config-last-error", (void*)hlConfigLastError);
        Sregister_symbol("hl-config-get", (void*)hlConfigGet);
        Sregister_symbol("hl-scheme-device-add", (void*)hlSchemeDeviceAdd);
        Sregister_symbol("hl-monitor-begin", (void*)hlMonitorBegin);
        Sregister_symbol("hl-monitor-field-str", (void*)hlMonitorFieldStr);
        Sregister_symbol("hl-monitor-field-num", (void*)hlMonitorFieldNum);
        Sregister_symbol("hl-monitor-field-gap", (void*)hlMonitorFieldGap);
        Sregister_symbol("hl-monitor-field-bool", (void*)hlMonitorFieldBool);
        Sregister_symbol("hl-monitor-commit", (void*)hlMonitorCommit);
        Sregister_symbol("hl-curve-add", (void*)hlCurveAdd);
        Sregister_symbol("hl-animation-set", (void*)hlAnimationSet);
        Sregister_symbol("hl-permission-add", (void*)hlPermissionAdd);
        Sregister_symbol("hl-window-rule-begin", (void*)hlWindowRuleBegin);
        Sregister_symbol("hl-layer-rule-begin", (void*)hlLayerRuleBegin);
        Sregister_symbol("hl-rule-match", (void*)hlRuleMatch);
        Sregister_symbol("hl-window-rule-effect", (void*)hlWindowRuleEffect);
        Sregister_symbol("hl-layer-rule-effect", (void*)hlLayerRuleEffect);
        Sregister_symbol("hl-window-rule-commit", (void*)hlWindowRuleCommit);
        Sregister_symbol("hl-layer-rule-commit", (void*)hlLayerRuleCommit);
        Sregister_symbol("hl-rule-set-enabled", (void*)hlRuleSetEnabled);
        Sregister_symbol("hl-rule-enabled", (void*)hlRuleEnabled);
        Sregister_symbol("hl-workspace-rule-begin", (void*)hlWorkspaceRuleBegin);
        Sregister_symbol("hl-workspace-rule-str", (void*)hlWorkspaceRuleStr);
        Sregister_symbol("hl-workspace-rule-num", (void*)hlWorkspaceRuleNum);
        Sregister_symbol("hl-workspace-rule-bool", (void*)hlWorkspaceRuleBool);
        Sregister_symbol("hl-workspace-rule-gap", (void*)hlWorkspaceRuleGap);
        Sregister_symbol("hl-workspace-rule-layout-opt", (void*)hlWorkspaceRuleLayoutOpt);
        Sregister_symbol("hl-workspace-rule-commit", (void*)hlWorkspaceRuleCommit);
        Sregister_symbol("hl-window-from", (void*)hlWindowFrom);
        Sregister_symbol("hl-urgent-window", (void*)hlUrgentWindow);
        Sregister_symbol("hl-last-window", (void*)hlLastWindow);
        Sregister_symbol("hl-monitor-from", (void*)hlMonitorFrom);
        Sregister_symbol("hl-monitor-at", (void*)hlMonitorAt);
        Sregister_symbol("hl-monitor-at-cursor", (void*)hlMonitorAtCursor);
        Sregister_symbol("hl-active-monitor", (void*)hlActiveMonitor);
        Sregister_symbol("hl-active-workspace", (void*)hlActiveWorkspace);
        Sregister_symbol("hl-active-special-workspace", (void*)hlActiveSpecialWorkspace);
        Sregister_symbol("hl-last-workspace", (void*)hlLastWorkspace);
        Sregister_symbol("hl-workspace-name", (void*)hlWorkspaceName);
        Sregister_symbol("hl-workspace-addressable-name", (void*)hlWorkspaceAddressableName);
        Sregister_symbol("hl-workspace-number", (void*)hlWorkspaceNumber);
        Sregister_symbol("hl-workspace-monitor", (void*)hlWorkspaceMonitor);
        Sregister_symbol("hl-workspace-special", (void*)hlWorkspaceSpecial);
        Sregister_symbol("hl-workspace-active", (void*)hlWorkspaceActive);
        Sregister_symbol("hl-workspace-visible", (void*)hlWorkspaceVisible);
        Sregister_symbol("hl-workspace-empty", (void*)hlWorkspaceEmpty);
        Sregister_symbol("hl-workspace-persistent", (void*)hlWorkspacePersistent);
        Sregister_symbol("hl-workspace-has-urgent", (void*)hlWorkspaceHasUrgent);
        Sregister_symbol("hl-workspace-has-fullscreen", (void*)hlWorkspaceHasFullscreen);
        Sregister_symbol("hl-workspace-fullscreen-mode", (void*)hlWorkspaceFullscreenMode);
        Sregister_symbol("hl-workspace-fullscreen-window", (void*)hlWorkspaceFullscreenWindow);
        Sregister_symbol("hl-workspace-last-window", (void*)hlWorkspaceLastWindow);
        Sregister_symbol("hl-workspace-window-count", (void*)hlWorkspaceWindowCount);
        Sregister_symbol("hl-workspace-group-count", (void*)hlWorkspaceGroupCount);
        Sregister_symbol("hl-workspace-tiled-layout", (void*)hlWorkspaceTiledLayout);
        Sregister_symbol("hl-workspace-alive", (void*)hlWorkspaceAlive);
        Sregister_symbol("hl-workspace-same", (void*)hlWorkspaceSame);
        Sregister_symbol("hl-workspace-selector", (void*)hlWorkspaceSelector);
        Sregister_symbol("hl-workspace-windows", (void*)hlWorkspaceWindows);
        Sregister_symbol("hl-is-key-down", (void*)hlIsKeyDown);
        Sregister_symbol("hl-loaded-plugins", (void*)hlLoadedPlugins);
        Sregister_symbol("hl-version", (void*)hlVersion);
        Sregister_symbol("hl-windows-from", (void*)hlWindowsFrom);
        Sregister_symbol("hl-monitor-name", (void*)hlMonitorName);
        Sregister_symbol("hl-monitor-description", (void*)hlMonitorDescription);
        Sregister_symbol("hl-monitor-number", (void*)hlMonitorNumber);
        Sregister_symbol("hl-monitor-enabled", (void*)hlMonitorEnabled);
        Sregister_symbol("hl-monitor-focused", (void*)hlMonitorFocused);
        Sregister_symbol("hl-monitor-x", (void*)hlMonitorX);
        Sregister_symbol("hl-monitor-y", (void*)hlMonitorY);
        Sregister_symbol("hl-monitor-width", (void*)hlMonitorWidth);
        Sregister_symbol("hl-monitor-height", (void*)hlMonitorHeight);
        Sregister_symbol("hl-monitor-scale", (void*)hlMonitorScale);
        Sregister_symbol("hl-monitor-transform", (void*)hlMonitorTransform);
        Sregister_symbol("hl-monitor-refresh-rate", (void*)hlMonitorRefreshRate);
        Sregister_symbol("hl-monitor-mode", (void*)hlMonitorMode);
        Sregister_symbol("hl-monitor-dpms", (void*)hlMonitorDpms);
        Sregister_symbol("hl-monitor-vrr", (void*)hlMonitorVrr);
        Sregister_symbol("hl-monitor-10bit", (void*)hlMonitor10bit);
        Sregister_symbol("hl-monitor-reserved", (void*)hlMonitorReserved);
        Sregister_symbol("hl-monitor-serial", (void*)hlMonitorSerial);
        Sregister_symbol("hl-monitor-physical-size", (void*)hlMonitorPhysicalSize);
        Sregister_symbol("hl-monitor-mirrors", (void*)hlMonitorMirrors);
        Sregister_symbol("hl-monitor-available-modes", (void*)hlMonitorAvailableModes);
        Sregister_symbol("hl-monitor-hardware-details", (void*)hlMonitorHardwareDetails);
        Sregister_symbol("hl-monitor-mirror-of", (void*)hlMonitorMirrorOf);
        Sregister_symbol("hl-monitor-active-workspace", (void*)hlMonitorActiveWorkspace);
        Sregister_symbol("hl-monitor-active-special-workspace", (void*)hlMonitorActiveSpecialWorkspace);
        Sregister_symbol("hl-monitor-alive", (void*)hlMonitorAlive);
        Sregister_symbol("hl-monitor-same", (void*)hlMonitorSame);
        Sregister_symbol("hl-monitor-selector", (void*)hlMonitorSelector);
        Sregister_symbol("hl-window-fullscreen-handler", (void*)hlWindowFullscreenHandler);
        Sregister_symbol("hl-notify!", (void*)hlNotify);
        Sregister_symbol("hl-notification-add", (void*)hlNotificationAdd);
        Sregister_symbol("hl-notification-list", (void*)hlNotificationList);
        Sregister_symbol("hl-notification-text", (void*)hlNotificationText);
        Sregister_symbol("hl-notification-timeout", (void*)hlNotificationTimeout);
        Sregister_symbol("hl-notification-color", (void*)hlNotificationColor);
        Sregister_symbol("hl-notification-icon", (void*)hlNotificationIcon);
        Sregister_symbol("hl-notification-font-size", (void*)hlNotificationFontSize);
        Sregister_symbol("hl-notification-elapsed", (void*)hlNotificationElapsed);
        Sregister_symbol("hl-notification-age", (void*)hlNotificationAge);
        Sregister_symbol("hl-notification-alive", (void*)hlNotificationAlive);
        Sregister_symbol("hl-notification-same", (void*)hlNotificationSame);
        Sregister_symbol("hl-notification-text-set", (void*)hlNotificationTextSet);
        Sregister_symbol("hl-notification-timeout-set", (void*)hlNotificationTimeoutSet);
        Sregister_symbol("hl-notification-color-set", (void*)hlNotificationColorSet);
        Sregister_symbol("hl-notification-icon-set", (void*)hlNotificationIconSet);
        Sregister_symbol("hl-notification-font-size-set", (void*)hlNotificationFontSizeSet);
        Sregister_symbol("hl-notification-paused-set", (void*)hlNotificationPausedSet);
        Sregister_symbol("hl-notification-paused-q", (void*)hlNotificationPausedQ);
        Sregister_symbol("hl-notification-dismiss", (void*)hlNotificationDismiss);
        Sregister_symbol("hl-timer-set-enabled", (void*)hlTimerSetEnabled);
        Sregister_symbol("hl-timer-enabled", (void*)hlTimerEnabled);
        Sregister_symbol("hl-timer-set-timeout", (void*)hlTimerSetTimeout);
        Sregister_symbol("hl-timer-cancel", (void*)hlTimerCancel);
        Sregister_symbol("hl-scheme-gesture", (void*)hlSchemeGesture);
        Sregister_symbol("hl-scheme-gesture-maker-workspace-swipe", (void*)hlSchemeGestureMakerWorkspaceSwipe);
        Sregister_symbol("hl-scheme-gesture-maker-move", (void*)hlSchemeGestureMakerMove);
        Sregister_symbol("hl-scheme-gesture-maker-resize", (void*)hlSchemeGestureMakerResize);
        Sregister_symbol("hl-scheme-gesture-maker-close", (void*)hlSchemeGestureMakerClose);
        Sregister_symbol("hl-scheme-gesture-maker-scroll-move", (void*)hlSchemeGestureMakerScrollMove);
        Sregister_symbol("hl-scheme-gesture-maker-float", (void*)hlSchemeGestureMakerFloat);
        Sregister_symbol("hl-scheme-gesture-maker-fullscreen", (void*)hlSchemeGestureMakerFullscreen);
        Sregister_symbol("hl-scheme-gesture-maker-special", (void*)hlSchemeGestureMakerSpecial);
        Sregister_symbol("hl-scheme-gesture-maker-cursor-zoom", (void*)hlSchemeGestureMakerCursorZoom);
        Sregister_symbol("hl-scheme-gesture-maker-custom", (void*)hlSchemeGestureMakerCustom);
        Sregister_symbol("hl-scheme-gesture-remove", (void*)hlSchemeGestureRemove);
        Sregister_symbol("hl-scheme-window-hidden", (void*)hlSchemeWindowHidden);

        // window read-side fields (LuaWindow parity)
        Sregister_symbol("hl-scheme-window-address", (void*)hlSchemeWindowAddress);
        Sregister_symbol("hl-scheme-window-mapped", (void*)hlSchemeWindowMapped);
        Sregister_symbol("hl-scheme-window-visible", (void*)hlSchemeWindowVisible);
        Sregister_symbol("hl-scheme-window-accepts-input", (void*)hlSchemeWindowAcceptsInput);
        Sregister_symbol("hl-scheme-window-position", (void*)hlSchemeWindowPosition);
        Sregister_symbol("hl-scheme-window-pin-fullscreened", (void*)hlSchemeWindowPinFullscreened);
        Sregister_symbol("hl-scheme-window-allowed-over-fullscreen", (void*)hlSchemeWindowAllowedOverFullscreen);
        Sregister_symbol("hl-scheme-window-tearing-hint", (void*)hlSchemeWindowTearingHint);
        Sregister_symbol("hl-scheme-window-inhibiting-idle", (void*)hlSchemeWindowInhibitingIdle);
        Sregister_symbol("hl-scheme-window-focus-history-id", (void*)hlSchemeWindowFocusHistoryId);
        Sregister_symbol("hl-scheme-window-content-type", (void*)hlSchemeWindowContentType);
        Sregister_symbol("hl-scheme-window-stable-id", (void*)hlSchemeWindowStableId);
        Sregister_symbol("hl-scheme-window-tags", (void*)hlSchemeWindowTags);
        Sregister_symbol("hl-scheme-window-swallowing-id", (void*)hlSchemeWindowSwallowingId);
        Sregister_symbol("hl-scheme-window-xdg-tag", (void*)hlSchemeWindowXdgTag);
        Sregister_symbol("hl-scheme-window-xdg-description", (void*)hlSchemeWindowXdgDescription);
        Sregister_symbol("hl-scheme-window-layout", (void*)hlSchemeWindowLayout);
        Sregister_symbol("hl-scheme-window-pinned", (void*)hlSchemeWindowPinned);
        Sregister_symbol("hl-scheme-window-pseudo-query", (void*)hlSchemeWindowPseudoQuery);
        Sregister_symbol("hl-scheme-window-maximized-query", (void*)hlSchemeWindowMaximizedQuery);
        Sregister_symbol("hl-scheme-window-in-group", (void*)hlSchemeWindowInGroup);
        Sregister_symbol("hl-scheme-window-group-denied", (void*)hlSchemeWindowGroupDenied);
        Sregister_symbol("hl-scheme-window-group-locked", (void*)hlSchemeWindowGroupLocked);
        Sregister_symbol("hl-scheme-groups-locked", (void*)hlSchemeGroupsLocked);
        Sregister_symbol("hl-scheme-window-group-lock", (void*)hlSchemeWindowGroupLock);
        Sregister_symbol("hl-scheme-window-prop", (void*)hlSchemeWindowPropGet);
        Sregister_symbol("hl-scheme-window-initial-class", (void*)hlSchemeWindowInitialClass);
        Sregister_symbol("hl-scheme-window-initial-title", (void*)hlSchemeWindowInitialTitle);
        Sregister_symbol("hl-scheme-window-x11", (void*)hlSchemeWindowX11);
        Sregister_symbol("hl-scheme-get-submap-ctx", (void*)+[]() -> ptr {
            std::vector<ptr> roots;
            ptr              name  = Sstring_utf8(g_regSubmap.c_str(), g_regSubmap.size());
            marshRoot(name, roots);
            ptr reset = Sstring_utf8(g_regSubmapReset.c_str(), g_regSubmapReset.size());
            marshRoot(reset, roots);
            ptr pair = Scons(name, reset);
            marshRoot(pair, roots);
            marshRelease(roots);
            return pair;   // (name . reset)
        });
        Sregister_symbol("hl-scheme-set-submap-ctx", (void*)+[](const char* name, const char* reset) {
            g_regSubmap      = name ? name : "";
            g_regSubmapReset = reset ? reset : "";
        });
        Sregister_symbol("hl-scheme-enter-submap", (void*)+[](const char* name) -> int {
            if (!g_up || !name)
                return -1;
            return Config::Actions::setSubmap(name) ? 0 : -1;
        });
        Layouts::registerSymbols();

        // the .scm machinery lives next to the plugin; the dev tree is the
        // first candidate so a dev build loads its own sources before any
        // installed copies (mirroring the boot-file fallback order)
#ifndef SOURCE_DIR
#define SOURCE_DIR "" // hand compiles: only the plugin dir is consulted
#endif
        static const std::vector<std::string> scmDirs = [] {
            std::vector<std::string> dirs;
            const char*              src = getenv("HYPRSCHEME_SCM_DIR"); // dev/test override
            if (src && *src)
                dirs.push_back(src);
            if (const auto dev = std::filesystem::path(SOURCE_DIR) / "src" / "config" / "scheme"; std::filesystem::exists(dev))
                dirs.push_back(dev.string());
            dirs.push_back(pluginDir());
            dirs.push_back("/usr/lib/hyprscheme");
            return dirs;
        }();
        const auto tryScm = [](const char* name) {
            for (const auto& dir : scmDirs) {
                if (dir.empty())
                    continue;
                const auto p = dir + "/" + name;
                if (std::filesystem::exists(p))
                    return p;
            }
            LOG(Log::ERR, "[scheme] {} not found (looked in HYPRSCHEME_SCM_DIR, the source tree, the plugin dir)", name);
            return std::string(name);
        };

        // phase 1: the prelude (verified plumbing, unguarded). foreign-procedure
        // resolves symbols at definition time, so symbols must exist before this.
        Scall1(Stop_level_value(Sstring_to_symbol("load")), Sstring(tryScm("hyprscheme-prelude.scm").c_str()));

        // phase 2: the defun machinery (defines `defun`, which phase 3's
        // converted functions use) ...
        Scall1(Stop_level_value(Sstring_to_symbol("hl--load")), Sstring(tryScm("hyprscheme-defun.scm").c_str()));
        // phase 3: ... then the API
        Scall1(Stop_level_value(Sstring_to_symbol("hl--load")), Sstring(tryScm("hyprscheme-bootstrap.scm").c_str()));

        if (Stop_level_value(Sstring_to_symbol("hl--ready")) == Sfalse) {
            LOG(Log::ERR, "[scheme] bootstrap failed, scheme scripting disabled");
            return false;
        }
        g_up = true;
        startWatchdog();
        return true;
    }

    void init() {
        static bool done = false;
        static std::filesystem::file_time_type loadedTime;
        if (done) {
            // the interpreter lives in the pinned first mapping: a re-load
            // re-attaches the SAME code. If the binary changed on disk, say
            // so instead of silently ignoring the upgrade.
            {
                Dl_info self{};
                if (dladdr((void*)&init, &self) && self.dli_fname) {
                    std::error_code ec;
                    const auto t = std::filesystem::last_write_time(self.dli_fname, ec);
                    if (!ec && t != loadedTime)
                        LOG(Log::ERR, "[scheme] the plugin binary changed on disk since this session started; "
                                      "restart the compositor to load the new version (the running interpreter "
                                      "cannot be replaced in-place)");
                }
            }
            // soft reload: the interpreter is still alive; re-attach the
            // foreign symbols, the (possibly new) scheme API and the
            // plumbing that shutdown() removed
            if (!attachInterp())
                return;
            registerIpc();
            g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { reloadScheme(); });
            g_lifecycleListeners.emplace_back(Event::bus()->m_events.start.listen([] {
                g_startSeen = true;
                auto pending = std::move(g_pendingStart);
                g_pendingStart.clear();
                for (const auto& ref : pending)
                    fireScheme(ref.obj);
            }));
            reloadScheme();
            return;
        }
        done = true;

        g_configPath = userConfigPath();
        if (g_configPath.empty() || !std::filesystem::exists(g_configPath)) {
            LOG(Log::INFO, "[scheme] no scheme config found at {} (HYPRSCHEME_CONFIG {}), scheme scripting disabled",
                g_configPath.empty() ? "<unset>" : g_configPath, getenv("HYPRSCHEME_CONFIG") ? "override ignored: file missing" : "not set");
            return;
        }
        LOG(Log::INFO, "[scheme] config: {}", g_configPath);

        // pin ourselves: bump the dlopen refcount so the compositor's
        // dlclose on unload never unmaps the live interpreter
        {
            Dl_info self{};
            if (dladdr((void*)&init, &self) && self.dli_fname)
                dlopen(self.dli_fname, RTLD_NOW | RTLD_NOLOAD);
        }
        {
            Dl_info selft{};
            if (dladdr((void*)&init, &selft) && selft.dli_fname) {
                std::error_code ec;
                loadedTime = std::filesystem::last_write_time(selft.dli_fname, ec);
            }
        }
        Sscheme_init(nullptr);
        // boot files live next to the plugin (.so dir), with the install
        // prefix and the dev kit as fallbacks
        std::string bootDir;
        Dl_info info{};
        if (dladdr((void*)&init, &info) && info.dli_fname)
            bootDir = std::filesystem::path(info.dli_fname).parent_path().string();
        const std::string home = getenv("HOME") ? getenv("HOME") : "";
        const std::vector<std::string> bootDirs = {"/home/chris/GITE/chez-pic", bootDir, home + "/.local/lib/hyprscheme", "/usr/lib/hyprscheme"};
        const auto tryBoot = [&bootDirs](const char* name) {
            for (const auto& dir : bootDirs) {
                if (dir.empty())
                    continue;
                const auto p = dir + "/" + name;
                if (std::filesystem::exists(p))
                    return p;
            }
            return std::string(name);
        };
        Sregister_boot_file(tryBoot("petite.boot").c_str());
        Sregister_boot_file(tryBoot("scheme.boot").c_str());
        Sbuild_heap(nullptr, nullptr);

        if (!attachInterp())
            return;


        // Chez installs its own SIGSEGV/SIGABRT handlers during init,
        // displacing Hyprland's crash reporter. A plugin cannot reach
        // handleUnrecoverableSignal (static), so reimplement its body via
        // the exported CrashReporter::createAndSaveCrash.
        signal(SIGSEGV, schemeCrashHandler);
        signal(SIGABRT, schemeCrashHandler);

        // hyprctl scheme '<forms>' — evaluate scheme in the compositor.
        // direct registration is safe here: verified working (the deferred
        // doLater variant never fired its callback).
        registerIpc();
        // watch the file for edits (skipped during --verify: no event loop yet)
        if (g_pEventLoopManager)
            setupWatch();

        // the lua config load clears all binds; (re)load our file once it settles.
        // as a plugin we load AFTER the initial config load, so also load now.
        g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { reloadScheme(); });

        // lifecycle: dispatch start-notification handlers on the session's first
        // render frame (start fires exactly once, after the first preChecks)
        g_lifecycleListeners.emplace_back(Event::bus()->m_events.start.listen([] {
            g_startSeen = true;
            auto pending = std::move(g_pendingStart);
            g_pendingStart.clear();
            for (const auto& ref : pending)
                fireScheme(ref.obj);
        }));
        reloadScheme();
    }
}

