#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "Host.hpp"
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
    The Scheme machinery lives in real .scm files installed NEXT TO THE PLUGIN,
    located through the plugin's own load path (dladdr on a known symbol). It is
    a set of MODULES, and the file paths are the module names:

      hyprscheme.scm          (hyprscheme)          the PUBLIC API — a curated
                                                    #:re-export list, and the
                                                    single source of truth for
                                                    what is public
      hyprscheme/kernel.scm   (hyprscheme kernel)   the machinery the host
                                                    reaches: the watchdog, the
                                                    fire trampolines, the layout
                                                    entry points, the record
                                                    types those read
      hyprscheme/api.scm      (hyprscheme api)      the API itself, public and
                                                    internal

    THE WHOLE SCHEME SIDE IS REBUILT ON EVERY CONFIG RELOAD (buildGeneration):
    each module is created, its bindings are defined into it, its file is read,
    and a fresh generation module is given the API and the kernel to import. So
    a definition, a set!, or a handler from a previous generation is unreachable
    afterwards, with no per-variable teardown to keep in step — the same
    clean-slate semantics as upstream's per-generation lua_State, reached by
    rebuilding rather than by copying.

    Note the ORDER, which is load-bearing in two ways: the kernel is read before
    the API (the API imports it), and a module's bindings must exist BEFORE its
    file is read, because the file's #:export names bindings the host defined
    (hl--c-generation, hl--c-run-finalizers).

    Exactly three things deliberately live outside the wipe, each for a
    concrete reason:

      - the stderr port (Guile.cpp): a port owns the fd it wraps, so a
        re-created one closes fd 2 when its predecessor is collected — the
        error log dies silently, and enough of them take the process with it;
      - the after-gc hook (installGcHook): registered once, or it stacks;
      - the survive list (g_schemeState): hl-state-set! and friends are
        documented to outlive reloads.

    Machinery variables a config may set! (hl--watchdog-ms) need no special
    handling: the reload rebuilds them from source, so the override resets on
    its own.
*/



namespace Config::Scheme::Internals {
    bool        g_up = false;         // interpreter + machinery ready
    std::string g_configError;        // last config error, read via c-hl-config-last-error

    // Handles live in Handles.hpp: a compositor object crosses to Scheme as a
    // Guile foreign object holding a heap weak ref, with a finalizer for its
    // lifetime. (Read that header first — the threading rule is load-bearing.)
}

namespace Config::Scheme {

    using namespace Internals;

    static std::string                g_configPath;
    static int                        g_watchFd       = -1;    // inotify fd; dup'd into event loop waiters
    // scheme timers: C++ owns the CEventLoopTimer, Scheme owns the closure
    // (the handler closures travel inside the connections, locked)
    // event subscriptions: C++ owns the listener handles (dropping one
    // unsubscribes); Scheme owns the handler closures by id
    // event connections: record address -> the subscription that keeps the
    // handler plugged into the bus (lost SCM = unregistered, per the listen
    // contract). Entries die three ways: hl-event-cancel!, the reload clear
    // (generation boundary = handler lifetime), plugin teardown.
    std::unordered_map<uintptr_t, Hyprutils::Signal::CHyprSignalListener> g_eventConnections;


    // (the start-dispatch lifecycle state — g_startSeen, g_pendingStart,
    // g_lifecycleListeners — lives with the event family; see Event.cpp)


    // ---- the callback watchdog --------------------------------------------------
    // A detector thread: scheme callbacks run on the main loop, so a hung one
    // freezes everything. Nothing can safely kill a running interpreter call
    // from outside, but we can SAY SO —
    // one loud log line + notification per overrun.

    static std::atomic<bool>        g_watchdogRun{false};
    static std::atomic<int64_t>     g_callbackStartMs{0};
    static std::atomic<const char*> g_callbackWhat{""};

    static int64_t watchdogNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void watchdogEnter(const char* what) {
        {
            std::ofstream pr("/tmp/hs-wd-probe", std::ios::app);
            pr << "enter " << what << "\n";
        }
        g_callbackWhat = what;
        g_callbackStartMs = watchdogNowMs();
    }

    void watchdogExit() {
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

    // called from Scheme as a gsubr: newline-joined workspace
    // display names, or #f. split on the Scheme side.
    // ---- FFI marshalling: proper scheme data, no newline-string shovelling ----
    // Values are plain SCM: the collector does not move objects, so an SCM
    // held in a C++ local stays valid, and a list built head-first is kept
    // alive by its head. Only a value parked in a malloc'd C++ container needs
    // pinning (hl::pin) — Boehm does not scan those.


    // ---- state queries for the toggle/set family -----------------------------
    // Each pair (hl-window-*-set!) has a matching hl-window-*-? reading the
    // effective state from the same source the setter writes.

    // ---- actions: the dispatcher surface --------------------------------------
    // Each handler wraps one Config::Actions call — the same layer the Lua
    // hl.dsp.* dispatchers use. Window args take a scheme handle id, or -1
    // for the active window.

    std::optional<Input::ModifierMask> modsMaskFromTokens(SCM mods); // defined below

    // ---- config: setting config options from scheme ---------------------------
    // The values live in CConfigManager::m_configValues (dotted key →
    // ILuaConfigValue). Each value parses itself off a lua stack; we keep a
    // private scratch lua_State (the same liblua the compositor links) purely
    // as the typed front door — the config manager's interpreter is never
    // involved. Propagation is a plain prop-refresh, exactly like
    // hyprctl eval 'hl.config(...)'.

    // ---- groups as objects (upstream HL.Group parity) --------------------------
    // groups dissolve behind our backs -> weak handles via the guardian (the
    // record-cell model); every getter returns #f when the group is gone
    using PHLGROUPREF = Hyprutils::Memory::CWeakPointer<Desktop::View::CGroup>;

    // read side: marshal the value back as an encoded string
    // "b\n0|1" | "n\n<num>" | "s\n<str>" | "t\n(key\nvalue\n)*" ; #f = unknown

    // ---- per-device config (upstream hl.device parity) -----------------------
    // Lua's hl.device({ name, ... }) writes per-device input overrides;
    // mirrored here as (hl-device-add! NAME . FIELDS). Write-only by parity:
    // Lua exposes no device read/get, and a stored field cannot be unset
    // (insert-or-assign only).

    std::string schemeDatumToStr(SCM p) {
        if (scm_is_symbol(p))
            return hl::symbolName(p);
        return hl::toStdString(p);
    }

    // ---- curves and animations -------------------------------------------------

    // ---- rules: window, layer, workspace ---------------------------------------
    // mirrors the lua hl.window_rule / hl.layer_rule / hl.workspace_rule.
    // Named rules are reused across calls; anonymous rules are unregistered
    // when the config reloads (the reload itself clears the whole engine).

    // ---- workspace rules -------------------------------------------------------

    // ---- queries ----------------------------------------------------------------

    // ---- workspace/monitor handle getters -------------------------------------
    // Mirror upstream Lua's workspace and monitor object fields 1:1 (see
    // LuaWorkspace.cpp / LuaMonitor.cpp). All getters resolve the handle
    // first; a stale or dead handle yields #f from every getter, like an
    // expired Lua object.

    // windows on the workspace as newline-joined window-handle ids

    // reserved area; all-zero means unset
    // when nothing is reserved

    // ---- monitor hardware getters (upstream LuaMonitor parity: serial,
    // physical_width/height, available_modes, mirrors, hardware_details) ----

    // ---- notifications ---------------------------------------------------------

    // ---- timer handles ----------------------------------------------------------


    // ---- gestures ---------------------------------------------------------------
    // A scheme thunk (or three, for live gestures) behind the trackpad gesture
    // system. Registered gestures are cleared by the config reload (the gesture
    // manager clears itself), so no extra bookkeeping is needed.

    // ---- layer surfaces as objects (upstream HL.LayerSurface parity) ----------
    // layer surfaces die independently -> weak handles via the guardian
    // (record-cell model); every getter returns #f when the surface is gone
    using PHLLSGROUPREF = Hyprutils::Memory::CWeakPointer<Desktop::View::CLayerSurface>;

    static bool buildGeneration(); // defined with the interpreter plumbing

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
        // (the index is the timer family's business — see Timer.cpp)
        cancelAllTimers();

        // drop event subscriptions; the new generation re-registers (the
        // event family owns the map — see Event.cpp)
        dropEventHandlers();

        // old-generation handles: their Scheme records became unreachable
        // with the generation, so the guardian reaps them at the next GC

        // layouts unregister + re-register with the new generation
        Layouts::clear();

        // re-binding the config value caches: CConfigValue readers (e.g. the
        // layout matcher's general:layout) hold copies made at first use, and
        // this reload may have changed what they should see.
        CConfigValueBase::flushCaches();

        // handlers subscribed to "start" from the outgoing generation: clearing
        // this is what the comment above promises — a start dispatch belongs to
        // the generation that registered it
        forgetPendingStart();

        // wipe the Scheme side and rebuild it from source, then run the new
        // config in it. A failure anywhere leaves the PREVIOUS generation in
        // place rather than a half-built one.
        if (!buildGeneration())
            return;

        if (!hl::evalFile(g_configPath.c_str()))
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
                const SCM r = hl::call1(hl::globalRef("hl--eval"), scm_from_utf8_stringn(code.c_str(), code.size()));
                watchdogExit();
                std::string out;
                if (scm_is_string(r))
                    out = hl::toStdString(r);
                return IPC::Socket1::SResponse(out);
            }});
        LOG(Log::INFO, "[scheme] ipc command registered");
    }

    // teardown for plugin unload: everything pointing into this .so must be
    // unregistered before hyprpm dlcloses it
    static Hyprutils::Signal::CHyprSignalListener g_reloadListener;

    // ---- submap registration context ------------------------------------------
    // the bind context a submap definition runs under: binds registered while
    // it is set are scoped to that submap (see the hl-submap wrapper).

    void shutdown() {
        if (g_schemeIpcCommand) {
            IPC::Socket1::sock()->unregisterCommand(g_schemeIpcCommand);
            g_schemeIpcCommand.reset();
        }
        g_ipcReadyListener.reset();
        g_reloadListener.reset();
        shutdownEvents();             // user handlers point into this .so
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
        // the interpreter stays alive: an SCM heap cannot be torn down and
        // re-initialized in-process, so a re-load of the plugin re-attaches to
        // the live interpreter (which is also why the .so pins itself)
        g_up = false;
    }

    /*
        The current generation: the module the prelude, the API and the config
        are evaluated into, and the module C++'s Scheme-side entry points are
        resolved from. Rebuilt from source on every reload — see reloadScheme.
    */
    static SCM g_generation = SCM_BOOL_F;

    static SCM hlSchemeGeneration() {
        return g_generation;
    }


    /*
        The survive list: the one deliberate cross-generation bridge
        (hl-state-set! and friends). The VALUE lives here, in the host, because
        it has to outlive every generation — each reload rebuilds the Scheme
        side, so a list kept there would go with it. The list manipulation
        stays in Scheme; this only owns the value.
    */
    static SCM g_schemeState = SCM_EOL;

    static SCM hlSchemeStateGet() {
        return g_schemeState;
    }

    static int hlSchemeStateSet(SCM v) {
        hl::unlock(g_schemeState);
        g_schemeState = v;
        hl::lock(v);
        return 0;
    }

    /*
        The after-gc hook pumps the collector's finalizer queue (Handles.hpp:
        finalizers delete weak refs, which is compositor state, so they must run
        on this thread). Registered with the finalizer gsubr itself, which is
        already 0-arity — no Scheme wrapper, and nothing to allocate inside the
        hook. Once per process: attachInterp runs again on a soft plugin reload,
        and a second registration would stack.
    */
    static void installGcHook() {
        static bool installed = false;
        if (installed)
            return;
        installed = true;
        SCM runner = hl::globalRefOrFalse("hl--c-run-finalizers");
        if (scm_is_false(runner)) {
            LOG(Log::ERR, "[scheme] hl--c-run-finalizers is unbound; the finalizers will not be pumped");
            return;
        }
        hl::lock(runner); // the hook holds it; pin it explicitly too
        hl::call2(hl::globalRefOrFalse("add-hook!"), hl::globalRefOrFalse("after-gc-hook"), runner);
    }

    // attachInterp: register foreign symbols and load the prelude + API
    // bootstrap. Called on first init and on every soft reload (so a
    // re-loaded plugin picks up its new scheme API against the live
    // interpreter). Returns false when the bootstrap failed.
    /*
        Every gsubr, defined into the CURRENT module — which is the generation
        while one is being built, so each reload gets its own bindings along
        with its own API. Registration is cheap (a hash insert each) and it is
        what makes the generation self-contained: nothing is imported from a
        module the plugin happens to have started in.
    */
    static void registerAllBindings() {
        // handle types: the predicates are structural (a foreign object of the
        // family's own type), and the machinery pumps the finalizers
        hl::bind<hl::isWindow>("hl--c-window?");
        hl::bind<hl::isWorkspace>("hl--c-workspace?");
        hl::bind<hl::isMonitor>("hl--c-monitor?");
        hl::bind<hl::isGroup>("hl--c-group?");
        hl::bind<hl::isLayer>("hl--c-layer?");
        hl::bind<hl::isNotification>("hl--c-notification?");

        // the finalizer pump: the after-gc hook is installed with this gsubr as
        // its thunk (installGcHook), so it must be registered or the collector's
        // queue is never drained
        hl::bind<hl::runFinalizers>("hl--c-run-finalizers");

        // the generation pointer and the survive list's storage (the host owns
        // both values; see above)
        hl::bind<hlSchemeGeneration>("hl--c-generation");
        hl::bind<hlSchemeStateGet>("hl--c-state-get");
        hl::bind<hlSchemeStateSet>("hl--c-state-set");

        // each family file registers its own Scheme-visible surface
        registerLayer();
        registerNotification();
        registerTimer();
        registerGesture();
        registerRule();
        registerBind();
        registerConfig();
        registerWorkspace();
        registerMonitor();
        registerQuery();
        registerWindow();
        registerGroup();
        registerExec();
        registerEvent();
        // (tools/split-family.py inserts the next family above this line)
        Layouts::registerSymbols();
    }

    // where the .scm machinery lives. The dev tree is the first candidate so a
    // dev build loads its own sources before any installed copies (mirroring
    // the boot-file fallback order).
#ifndef SOURCE_DIR
#define SOURCE_DIR "" // hand compiles: only the plugin dir is consulted
#endif
    static std::string schemePath(const char* name);

    // the machinery directory the host resolved, for %load-path
    static std::string schemeDir() {
        return std::filesystem::path(schemePath("hyprscheme.scm")).parent_path().string();
    }

    static std::string schemePath(const char* name) {
        static const std::vector<std::string> dirs = [] {
            std::vector<std::string> d;
            const char*              src = getenv("HYPRSCHEME_SCM_DIR"); // dev/test override
            if (src && *src)
                d.push_back(src);
            if (const auto dev = std::filesystem::path(SOURCE_DIR) / "src" / "config" / "scheme"; std::filesystem::exists(dev))
                d.push_back(dev.string());
            d.push_back(pluginDir());
            d.push_back("/usr/lib/hyprscheme");
            return d;
        }();
        for (const auto& dir : dirs) {
            if (dir.empty())
                continue;
            const auto p = dir + "/" + name;
            if (std::filesystem::exists(p))
                return p;
        }
        LOG(Log::ERR, "[scheme] {} not found (looked in HYPRSCHEME_SCM_DIR, the source tree, the plugin dir)", name);
        return std::string(name);
    }

    /*
        Build a fresh generation and evaluate the machinery into it. This is the
        wipe: the prelude, the API and (by reloadScheme) the config all go into
        a brand-new module, so nothing a previous generation defined — or set!
        — can be reached afterwards, with no per-variable teardown to keep in
        step.

        Built into a temporary module and swapped in only once every step has
        succeeded: a broken config or a bad edit to the machinery leaves the
        previous generation live rather than a half-built one.
    */
    static bool buildGeneration() {
        const SCM prevGen    = g_generation;
        const SCM prevModule = scm_current_module();

        // the machinery directory goes on %load-path: the modules resolve each
        // other BY NAME from here ((hyprscheme kernel) is hyprscheme/kernel.scm),
        // and a config's own (use-modules ...) resolves from it too.
        // (`add-to-load-path` is a syntax transformer, not a procedure, so it
        // cannot be called from C++ — the runtime does the variable directly.)
        if (!hl::addLoadPath(schemeDir().c_str()))
            LOG(Log::ERR, "[scheme] could not put the machinery on %load-path");

        // 1. the KERNEL: created first, given its one gsubr, then loaded. Its
        //    export list is explicit (module-export-all! is not enough — see
        //    the note there), so the binding must exist before the file is read.
        const SCM kernel = hl::makeModule("hyprscheme kernel");
        if (scm_is_false(kernel)) {
            LOG(Log::ERR, "[scheme] could not create the kernel module");
            return false;
        }
        // THE WHOLE C++ SURFACE GOES INTO THE KERNEL. Every module wraps gsubrs,
        // and the kernel is the one module they all import, so this is where the
        // boundary belongs — and its export list names them, hence the order.
        scm_set_current_module(kernel);
        registerAllBindings();
        if (!hl::loadModule(schemePath("hyprscheme/kernel.scm").c_str())) {
            LOG(Log::ERR, "[scheme] the kernel failed to load");
            scm_set_current_module(prevModule);
            return false;
        }

        // 2. the modules that hold the API: (hyprscheme core) is the shared
        //    plumbing, one module per family is the raw API, and extras holds
        //    the few conveniences that compose several calls. Each imports the
        //    kernel (and core); none imports another family except extras.
        static const char* kApiModules[] = {
            "hyprscheme/core.scm", "hyprscheme/bind.scm", "hyprscheme/config.scm",
            "hyprscheme/event.scm", "hyprscheme/exec.scm", "hyprscheme/gesture.scm",
            "hyprscheme/group.scm", "hyprscheme/layer.scm", "hyprscheme/monitor.scm",
            "hyprscheme/notification.scm", "hyprscheme/query.scm", "hyprscheme/rule.scm",
            "hyprscheme/timer.scm", "hyprscheme/window.scm", "hyprscheme/workspace.scm",
            "hyprscheme/extras.scm",
        };
        for (const char* rel : kApiModules) {
            if (!hl::loadModule(schemePath(rel).c_str())) {
                LOG(Log::ERR, "[scheme] {} failed to load", rel);
                scm_set_current_module(prevModule);
                return false;
            }
        }

        // 3. the curated public umbrella over them
        if (!hl::loadModule(schemePath("hyprscheme.scm").c_str())) {
            LOG(Log::ERR, "[scheme] the public API umbrella failed to load");
            scm_set_current_module(prevModule);
            return false;
        }

        // 4. the generation a config runs in: imports the API and the kernel
        //    DIRECTLY (not the umbrella), so the hl-- helpers stay reachable —
        //    exactly the surface a config had when everything was one module
        const SCM gen = hl::makeFreshModule();
        // the standard library the machinery itself imports, so a config has
        // the environment it has always had (a config may use call-with-string-
        // output-port, exists/for-all, filter, keyword arguments …)
        if (scm_is_false(gen) ||
            !hl::useModule(gen, "rnrs io ports") || !hl::useModule(gen, "rnrs lists") ||
            !hl::useModule(gen, "srfi srfi-1") || !hl::useModule(gen, "ice-9 optargs") ||
            !hl::useModule(gen, "ice-9 exceptions") || !hl::useModule(gen, "hyprscheme kernel") ||
            !hl::useModule(gen, "hyprscheme core") || !hl::useModule(gen, "hyprscheme bind") ||
            !hl::useModule(gen, "hyprscheme config") || !hl::useModule(gen, "hyprscheme event") ||
            !hl::useModule(gen, "hyprscheme exec") || !hl::useModule(gen, "hyprscheme gesture") ||
            !hl::useModule(gen, "hyprscheme group") || !hl::useModule(gen, "hyprscheme layer") ||
            !hl::useModule(gen, "hyprscheme monitor") || !hl::useModule(gen, "hyprscheme notification") ||
            !hl::useModule(gen, "hyprscheme query") || !hl::useModule(gen, "hyprscheme rule") ||
            !hl::useModule(gen, "hyprscheme timer") || !hl::useModule(gen, "hyprscheme window") ||
            !hl::useModule(gen, "hyprscheme workspace") || !hl::useModule(gen, "hyprscheme extras")) {
            LOG(Log::ERR, "[scheme] could not assemble the generation");
            scm_set_current_module(prevModule);
            return false;
        }
        scm_set_current_module(gen);

        // the machinery is in; say so. The flag lives in the kernel (so the
        // generation can read it through its import) and the host sets it,
        // because the host is what knows every module loaded.
        if (!hl::setModuleVariable(kernel, "hl--ready", SCM_BOOL_T))
            LOG(Log::ERR, "[scheme] could not set hl--ready");
        if (scm_is_false(hl::globalRef("hl--ready"))) {
            LOG(Log::ERR, "[scheme] the API did not finish loading; keeping the previous generation");
            scm_set_current_module(prevModule);
            return false;
        }

        g_generation = gen;
        hl::lock(gen);
        if (!scm_is_false(prevGen))
            hl::unlock(prevGen);
        return true;
    }

    static bool attachInterp() {
        // wipe and rebuild: the prelude and the API go into a fresh generation,
        // with the gsubrs registered into it first
        if (!buildGeneration()) {
            LOG(Log::ERR, "[scheme] scheme scripting disabled");
            return false;
        }
        // the hook's thunk is the finalizer gsubr, so it must exist first (and
        // it is registered into the generation that buildGeneration just made)
        installGcHook();
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
            registerStartDispatch();
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
        hl::init();

        if (!attachInterp())
            return;


        // The interpreter installs its own SIGSEGV/SIGABRT handlers during
        // init, displacing Hyprland's crash reporter. A plugin cannot reach
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
        registerStartDispatch();
        reloadScheme();
    }
}

