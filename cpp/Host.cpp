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
// the compositor's own "your config is broken" banner — the surface it uses for
// config errors, as opposed to the notifications it uses for runtime ones
#include <src/errorOverlay/Overlay.hpp>
#include <xkbcommon/xkbcommon.h>
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

      init.scm                —                     THE ENTRY POINT: the one
                                                    file the host loads, and it
                                                    names every module below
      hyprscheme/kernel.scm   (hyprscheme kernel)   the machinery the host
                                                    reaches: the watchdog, the
                                                    fire trampolines, the layout
                                                    entry points, the record
                                                    types those read — and the
                                                    whole C++ boundary
      hyprscheme/<family>.scm (hyprscheme <family>) one module per object
                                                    family: the raw API

    There is no umbrella module: the public API is DERIVED by asking Guile what
    each module publishes (module-public-interface — see tools/api-names.scm),
    not read from a list. A curated `(hyprscheme)` re-export list used to sit
    here and had already drifted from the truth.

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
    // NOTE: the host keeps NO inotify state of its own. Watching for config
    // edits used to live here (an fd, a watched directory, a drain loop), and
    // it was removed once (hyprscheme watch) existed: the interpreter reports
    // every file the config actually loaded (%load-hook), which is a SUPERSET of
    // what a directory watch could guess at, and keeping both made every edit to
    // a split config reload TWICE. The only fd work left in C++ is
    // hl--c-on-readable, which is the event loop and nothing else.
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
        // NO I/O here: this runs on every scheme callback. A debug write to
        // /tmp once lived here and grew to a megabyte in a single session,
        // in the hot path of the whole API.
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

        // THE CONFIG IS LOADED FROM SCHEME, through hl--load-config. NOT
        // through C, and NOT through hl-load. Three reasons, each a decision
        // rather than a preference:
        //
        //   - loading a file is something Guile does natively, so per the
        //     boundary rule (CLAUDE.md, "THE BOUNDARY") C++ has no business
        //     implementing one. There is no compositor API here to bridge.
        //   - the C loader COST a real bug. It caught errors with scm_c_catch
        //     and wrote them to fd 2 from C, so a config with a SYNTAX error —
        //     which dies in the READER — never reached a Scheme handler at all:
        //     hl--config-errors stayed empty and the persistent banner, the
        //     thing the user actually sees, never fired. (Measured.)
        //   - one guard in hl--load-config covers read errors and evaluation
        //     errors alike, which is the shape the scripting API uses too (one
        //     guarded pcall around the whole config).
        //
        // The Scheme side loads into whatever module is CURRENT, which is the
        // generation buildGeneration just made. hl-load, by contrast, names the
        // generation explicitly — that is the one for a file a config pulls in
        // from inside itself.
        const bool loaded = scm_is_true(hl::call1(hl::globalRef("hl--load-config"),
                                                  scm_from_utf8_string(g_configPath.c_str())));

        if (!loaded) {
            LOG(Log::ERR, "[scheme] failed to load {}", g_configPath);
            // the banner was raised by hl--report-config while the load failed
            // — one line per failing file, accumulating (see kernel.scm).
            // NOT only the config file itself: a file the config pulled in
            // through hl-load/hl-load-config failing makes the whole load count
            // as failed, which is what keeps the banner up for it (the nested
            // case reported by Chris — see hl--load-config in kernel.scm).
        } else {
            LOG(Log::INFO, "[scheme] loaded {}", g_configPath);
            // THE CONFIG LOADED CLEANLY, so take down any banner left by the
            // previous one. Called by name rather than from inside the load: the
            // Scheme side cannot know that the WHOLE config finished, since a
            // config is many files and each load is a separate call. The host is
            // the only thing that knows this was the last one. Guarded and
            // best-effort — a banner we failed to clear must not fail a reload
            // that otherwise worked.
            hl::call0(hl::globalRef("hl--report-config-ok"));
        }
    }

    /*
        Arm an event-loop waiter on FD that calls THUNK on each wakeup and
        re-arms: waiters here are one-shot. The fd is dup'd per arming and the
        CFileDescriptor owns that copy.
    */
    static void armReader(int fd, SCM thunk) {
        g_pEventLoopManager->doOnReadable(CFileDescriptor(dup(fd)), [fd, thunk] {
            hl::call0(thunk);
            armReader(fd, thunk);
        });
    }

    /*
        Call a Scheme thunk whenever FD becomes readable.

        THE ONE PRIMITIVE THE MACHINERY CANNOT SUPPLY FOR ITSELF. Waiting on a
        file descriptor means the compositor's event loop — doOnReadable, which
        is C++ — and a Scheme loop around read(2) on this thread would block the
        thread that draws the screen. Everything else about inotify (opening it,
        adding and removing watches, decoding events, deciding what an event
        means) is Scheme: see (hyprscheme inotify) and (hyprscheme watch).

        Deliberately knows nothing about inotify or about config files, so it is
        reusable for any other fd.

        The thunk is PINNED for the life of the registration — a Scheme closure
        held by C++ would otherwise be collected out from under us — and is
        called on the event-loop thread under the usual containment, since an
        exception escaping into C++ corrupts the compositor heap.
    */
    static int hlSchemeOnReadable(int fd, SCM thunk) {
        if (!g_pEventLoopManager || fd < 0 || scm_is_false(scm_procedure_p(thunk)))
            return -1;

        hl::lock(thunk);
        armReader(fd, thunk);
        return 0;
    }

    /*
        Show (or clear, with an empty MESSAGE) the compositor's full-width error
        banner at the top of the screen.

        This is the surface Hyprland itself uses for "your config is broken":
        ConfigManager::addError collects config-time errors into m_errors and
        renders them through this overlay, capped at ~15 lines with "... more",
        while a RUNTIME error pops an ordinary notification instead. The two
        answer different questions — a config error means "go and fix the file"
        and has to persist, a callback error means "that key press failed" and
        should be transient — so we route the same way (kernel.scm hl--report
        versus hl--report-config).

        Empty MESSAGE DESTROYS the banner, which is how a reload that succeeds
        clears one raised by the load before it.
    */
    static int hlSchemeErrorOverlay(const char* message) {
        if (!g_up)
            return -1;
        // guarded: this is called from an error path, and an exception here
        // would replace the error being reported
        try {
            if (message && *message)
                ErrorOverlay::overlay()->queueCreate(std::string(message), ErrorOverlay::Colors::ERROR);
            else
                ErrorOverlay::overlay()->destroy();
        } catch (...) {
            return -1;
        }
        return 0;
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

        // Otherwise the SAME search the compositor makes for its own config,
        // so the scheme config is found wherever the lua config beside it is
        // (Jeremy::getMainConfigPath → Hyprutils::Path::findConfig): the
        // XDG_CONFIG_HOME base, then each XDG_CONFIG_DIRS entry, then
        // /etc/xdg. XDG_CONFIG_HOME unset or empty is not "no config" — the
        // spec says fall back to $HOME/.config, and a relative value is
        // invalid and ignored the same way.
        //
        // The file NAME is still ours (hyprland.scm, beside hyprland.lua),
        // and only the base varies: the search walks bases, not filenames.
        std::vector<std::string> bases;

        const char* xdgHome = getenv("XDG_CONFIG_HOME");
        if (xdgHome && *xdgHome && xdgHome[0] == '/')
            bases.emplace_back(xdgHome);
        else if (const char* home = getenv("HOME"); home && *home)
            bases.emplace_back(std::string(home) + "/.config");

        // XDG_CONFIG_DIRS is a colon-separated list, searched in order; empty
        // or relative entries are invalid per the spec and skipped. Defaults
        // to /etc/xdg when unset.
        bool sawDirs = false;
        if (const char* dirs = getenv("XDG_CONFIG_DIRS"); dirs && *dirs) {
            std::stringstream ss(dirs);
            std::string       entry;
            while (std::getline(ss, entry, ':')) {
                if (!entry.empty() && entry[0] == '/') {
                    bases.emplace_back(entry);
                    sawDirs = true;
                }
            }
        }
        if (!sawDirs)
            bases.emplace_back("/etc/xdg");

        if (bases.empty())
            return {};

        // first base that actually has the file; else the first base, so the
        // "no config file at <path>" log names the place to create one
        for (const auto& base : bases) {
            const auto candidate = base + "/hypr/hyprland.scm";
            if (std::filesystem::exists(candidate))
                return candidate;
        }
        return bases.front() + "/hypr/hyprland.scm";
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
                // `hyprctl scheme` with NO argument used to fall through as
                // substr(npos + 1) = substr(0), which is the whole word
                // "scheme" — evaluated as a symbol, so the reply was
                // "Unbound variable: scheme". Say what is missing instead.
                const auto space = req.command.find_first_of(' ');
                if (space == std::string::npos ||
                    req.command.find_first_not_of(" \t", space) == std::string::npos)
                    return IPC::Socket1::SResponse("usage: hyprctl scheme <expression>");
                auto code = req.command.substr(space + 1);
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
    // GONE. The host used to carry the submap a config was defining into
    // (g_regSubmap/g_regSubmapReset, in Bind.cpp) so that binds created inside
    // an hl-submap thunk inherited the name. The submap is a per-bind argument
    // to hl-bind-add! now (#:submap / #:submap-reset), which is what the
    // compositor always modelled — see the note in Bind.cpp.

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
        // the one thing the machinery cannot do for itself about fds: wait on
        // one. See armReader above — the inotify work is all Scheme.
        hl::bind<hlSchemeOnReadable>("hl--c-on-readable");
        // the compositor's own "your config is broken" banner
        hl::bind<hlSchemeErrorOverlay>("hl--c-error-overlay");
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

    // where the .scm machinery lives. THREE candidates, in a fixed order, and
    // no compiled-in path.
    //
    // Not a search in the sense that matters: the order is fixed and each step
    // is a definite answer, not a guess between plausible places. The one
    // ambiguity worth avoiding — a harness that means to test the TREE
    // silently picking up an installed copy, or the reverse — is handled by
    // the first candidate being explicit and refusing to fall through.
    //
    // 1. HYPRSCHEME_SCM_DIR — a harness naming the tree it means to test.
    //    Named and missing is a broken harness: refuse rather than quietly run
    //    something else.
    // 2. Beside the .so — where `make install` puts it. No build-time path is
    //    needed: DESTDIR is a staging prefix copied out at package time, so a
    //    baked-in $(DESTDIR)$(PREFIX) would name a build-machine directory.
    //    pluginDir() is that same directory AS INSTALLED, and follows the .so.
    // 3. ~/.local/share/hyprscheme/scm — the user-level location, for the one
    //    case the other two cannot serve: a plugin built by hyprpm, which
    //    installs only the .so (one file per plugin, to a root-owned cache
    //    dir) and deletes its build tree afterwards. A hyprpm build step
    //    cannot put the machinery beside the .so, so `make hyprpm-machinery`
    //    puts it here instead. Absent, this candidate is simply skipped.
    // ~/.local/share/hyprscheme/scm, honouring XDG_DATA_HOME the way the spec
    // says (a relative or empty value is invalid and is ignored, falling back
    // to $HOME/.local/share). Where `make hyprpm-machinery` copies the .scm.
    static std::string userScmDir() {
        static const std::string dir = [] {
            const char* dataHome = getenv("XDG_DATA_HOME");
            std::string base;
            if (dataHome && *dataHome && dataHome[0] == '/')
                base = dataHome;
            else if (const char* home = getenv("HOME"); home && *home)
                base = std::string(home) + "/.local/share";
            else
                return std::string();
            return base + "/hyprscheme/scm";
        }();
        return dir;
    }

    static std::string schemeDir() {
        static const std::string dir = [] {
            if (const char* ov = getenv("HYPRSCHEME_SCM_DIR"); ov && *ov) {
                // A named override that is not there is a broken harness, and
                // falling back would run the installed machinery while the
                // harness believed it was running the tree — the exact failure
                // the override exists to prevent. Refuse, loudly.
                if (!std::filesystem::exists(ov)) {
                    LOG(Log::ERR, "[scheme] HYPRSCHEME_SCM_DIR={} does not exist", ov);
                    return std::string();
                }
                return std::string(ov);
            }
            // init.scm is the marker: it is the one file the host loads, so a
            // directory without it is not a machinery directory — its absence
            // is exactly the thing that matters.
            const auto beside = pluginDir();
            if (!beside.empty() && std::filesystem::exists(beside + "/init.scm"))
                return beside;
            const auto userLevel = userScmDir();
            if (!userLevel.empty() && std::filesystem::exists(userLevel + "/init.scm"))
                return userLevel;
            // Nothing found. Say so against the location that WOULD have
            // worked, since the fix differs by how the plugin was installed.
            LOG(Log::ERR, "[scheme] no machinery beside the plugin ({}), nor in {}; "
                          "set HYPRSCHEME_SCM_DIR=<tree> to test a source tree, "
                          "run `make install`, or `make hyprpm-machinery` for a hyprpm build",
                beside, userLevel);
            return beside;
        }();
        return dir;
    }

    // one machinery file, or empty when the directory was refused above
    static std::string schemePath(const char* name) {
        const auto dir = schemeDir();
        if (dir.empty())
            return {};
        return dir + "/" + name;
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
        const auto scmDir = schemeDir();
        if (scmDir.empty()) {
            LOG(Log::ERR, "[scheme] no machinery directory (unset/fix HYPRSCHEME_SCM_DIR, or install the machinery beside the plugin)");
            return false;
        }
        if (!hl::addLoadPath(scmDir.c_str()))
            LOG(Log::ERR, "[scheme] could not put the machinery on %load-path");
        // and the compiled path, where `use-modules` looks for a .go. A
        // separate list from %load-path, and the .go sit BESIDE the .scm, so
        // the same directory goes on both.
        if (!hl::addLoadCompiledPath(scmDir.c_str()))
            LOG(Log::ERR, "[scheme] could not put the machinery on %load-compiled-path");

        // 1. the KERNEL: created first, then given the whole C++ surface.
        //    init.scm reaches the file by name; `use-modules` loads a module
        //    once per PROCESS and reuses it after that, so there is ONE kernel
        //    for the life of the plugin and the gsubrs registered here survive
        //    every generation.
        //
        //    NOTE what `makeModule` actually does: it resolves the name, and
        //    resolve AUTOLOADS, so the kernel's FILE IS READ HERE — before
        //    registerAllBindings() below, not after. It works, and the reason
        //    is `#:export`: a name in the export list exists as an unassigned
        //    placeholder from the moment the module is defined, so the file
        //    reads fine and the gsubrs are assigned into those placeholders
        //    afterwards. The consequence to know is that the kernel's
        //    top-level forms run with the gsubrs NOT yet bound — a gsubr
        //    called at kernel load time is unbound (verified). Keep the
        //    kernel's top level to defines.
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

        // 2. the generation a config runs in: made FIRST, and made CURRENT, so
        //    that init.scm's imports land in it. It imports the families, core
        //    and the kernel DIRECTLY (not the umbrella), so the hl-- helpers stay
        //    reachable — exactly the surface a config had when everything was one
        //    module.
        //
        //    THE ORDER HERE IS THE POINT. Read with the generation current,
        //    init.scm's `use-modules` ARE the generation's environment, and the
        //    host needs no module list of its own. Read with the kernel current —
        //    which is what this did — they land in the kernel instead, and the
        //    generation then has to be assembled by hand, which is how a 23-line
        //    list in C++ came to duplicate the list in init.scm and drift from it.
        const SCM gen = hl::makeFreshModule();
        if (scm_is_false(gen)) {
            LOG(Log::ERR, "[scheme] could not create the generation module");
            scm_set_current_module(prevModule);
            return false;
        }
        scm_set_current_module(gen);

        // 3. ONE FILE loads everything else — the kernel, core, the families,
        //    extras, the umbrella, and the standard library a config has always
        //    had. Each module is loaded exactly once, by name, in dependency
        //    order, and, being loaded by name rather than by path, through
        //    Guile's own loader, which finds a shipped .go or compiles and
        //    caches one.
        //
        //    The host used to enumerate all nineteen files here and load each
        //    through scm_primitive_load: a list in C++ that had to be kept in
        //    step with the module tree, and no compilation at all, since the
        //    primitive bypasses the Scheme-level loader.
        // CLEAR THE READY FLAG BEFORE init.scm IS READ. Load-bearing now that the
        // machinery is loaded ONCE rather than re-read: on every generation after
        // the first the kernel still holds the #t the host set at the end of the
        // previous one, so the check a few lines below would pass without the API
        // having loaded at all, and the guard would be vacuous. Cleared here, it
        // means what it says.
        if (!hl::setModuleVariable(kernel, "hl--ready", SCM_BOOL_F))
            LOG(Log::ERR, "[scheme] could not clear hl--ready");

        // LOADING init.scm IS JUST A CALL TO A GUILE PROCEDURE, so it goes
        // through the same contained-call bridge as everything else and there is
        // no loader in C++ at all. Reached via scm_c_public_ref rather than by
        // name, because init.scm is what DEFINES the machinery — a lookup in the
        // generation would find nothing yet — whereas `primitive-load` is
        // Guile's own and always there.
        //
        // Its failure is a PLUGIN bug, not a config error, so the config banner
        // is the wrong surface for it. Reached before the generation is swapped
        // in, which is exactly why the host can keep the previous one.
        const bool booted = scm_is_true(hl::call1(
            scm_c_public_ref("guile", "primitive-load"),
            scm_from_utf8_string(schemePath("init.scm").c_str())));

        if (!booted) {
            LOG(Log::ERR, "[scheme] init.scm failed to load");
            scm_set_current_module(prevModule);
            return false;
        }

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

        // The interpreter is built FIRST, and unconditionally: "the machinery
        // is up" and "a config file was found" are different questions, and
        // conflating them meant a missing hyprland.scm left the plugin loaded
        // but inert — no machinery, and no `hyprctl scheme` either (the IPC
        // command is registered below, and its handler needs hl--eval to
        // exist). The config file is what the machinery LOADS, not what makes
        // it exist.
        //
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

        // the lua config load clears all binds; (re)load our file once it settles.
        // as a plugin we load AFTER the initial config load, so also load now.
        g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { reloadScheme(); });

        // lifecycle: dispatch start-notification handlers on the session's first
        // render frame (start fires exactly once, after the first preChecks)
        registerStartDispatch();

        // NOW the config file. Absent is a legitimate state, not an error: the
        // API is fully available (the machinery is loaded and `hyprctl scheme`
        // answers), there is simply nothing to evaluate into the generation.
        // That is the state to explore the API from before writing a config.
        g_configPath = userConfigPath();
        if (g_configPath.empty() || !std::filesystem::exists(g_configPath)) {
            LOG(Log::INFO, "[scheme] no config file at {} (HYPRSCHEME_CONFIG {}) — the API is loaded and "
                           "`hyprctl scheme` works; nothing to evaluate",
                g_configPath.empty() ? "<unset>" : g_configPath, getenv("HYPRSCHEME_CONFIG") ? "override ignored: file missing" : "not set");
            return;
        }
        LOG(Log::INFO, "[scheme] config: {}", g_configPath);

        reloadScheme();
    }
}

