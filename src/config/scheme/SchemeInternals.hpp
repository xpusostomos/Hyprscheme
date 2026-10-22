#pragma once

#include <libguile.h>

#include "Handles.hpp"

#include <src/desktop/DesktopTypes.hpp>
#include <src/config/shared/actions/ConfigActions.hpp>
#include <src/debug/log/Logger.hpp>
#include <src/event/EventBus.hpp>
#include <src/input/Keys.hpp>

struct lua_State; // the config family's scratch state (Config.cpp)

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

/*
    State shared by every translation unit of the plugin.

    The host (Host.cpp) defines it; the object families, the layout entry
    points (SchemeLayout.cpp) and the runtime (Guile.cpp) all reach it here.
*/

namespace Config::Scheme::Internals {
    extern bool g_up;              // interpreter + bootstrap ready
    extern std::string g_configError; // last config error: the rejecting family
                                      // writes it, hl-config-last-error reads it

    // (handles are built and unwrapped through Handles.hpp; nothing here needs
    // to know about them)
}

/*
    Shared machinery, declared here because more than one translation unit uses
    it. Each is DEFINED in the file that owns its family, so no caller has to
    know where.
*/
namespace Config::Scheme {
    // each family file registers its own Scheme-visible entry points
    void registerLayer();
    void registerTimer();
    void registerNotification();
    void registerGesture();
    void registerRule();
    void registerBind();
    void registerConfig();
    void registerWorkspace();
    void registerMonitor();
    void registerQuery();
    void registerWindow();
    void registerGroup();
    void registerExec();
    void registerEvent();
    // (tools/split-family.py inserts the next family above this line)

    // a string out of a Scheme symbol or string (the plist/coercion paths)
    std::string schemeDatumToStr(SCM v);

    // "C-M-s" token list -> a modifier mask, or nullopt if a token is not a
    // modifier (the key-spec, gesture and bind paths all validate with it)
    std::optional<Input::ModifierMask> modsMaskFromTokens(SCM mods);

    // the config family's scratch lua_State, for the shared config parsers
    // (gaps, monitor fields, workspace layout opts) that parse a string value
    lua_State* configScratch();

    // fire a handler (Event.cpp); errors are contained inside the Scheme side
    void fireScheme(SCM record);
    void fireSchemeStr(SCM record, const std::string& arg);

    // the host's side of the event plumbing (Event.cpp)
    void registerStartDispatch(); // the once-only "start" dispatch
    void dropEventHandlers();     // the reload boundary
    void forgetPendingStart();    // the reload boundary, for pre-first-frame handlers
    void shutdownEvents();        // plugin teardown

    // the callback watchdog (defined with the host): every entry point that can
    // run Scheme under a callback brackets it, so a hung callback is reported
    // rather than silently freezing the compositor
    void watchdogEnter(const char* what);
    void watchdogExit();

    // timers belong to the generation that made them (defined in Timer.cpp);
    // the reload tears the old generation's down
    void cancelAllTimers();

    // rules belong to the generation that made them (defined in Rule.cpp); the
    // reload has already cleared the engine, so this drops our own state
    void clearSchemeRules();

    // event subscriptions: record address -> the bus connection. The map IS the
    // set of live listeners (entries die by hl-notification-remove!, at the
    // reload boundary, or at plugin teardown).
    extern std::unordered_map<uintptr_t, Hyprutils::Signal::CHyprSignalListener> g_eventConnections;

    /*
        The handle/selector plumbing every object family shares. These are the
        getter shape the whole API uses — "a stale or dead handle reads #f from
        every getter, like an expired Lua object" — so they live here rather
        than in whichever family happened to define them first.
    */
    /*
        A Scheme identifier as the compositor spells it. Scheme names use '-'
        and the compositor's registries use '_' ("border-color" vs
        "border_color"), so every name that is looked up in one of them goes
        through here first.

        Only '-' becomes '_': a name that already uses underscores is passed
        through unchanged, so both spellings work and no existing config breaks.
    */
    inline std::string hyprlandName(std::string_view name) {
        std::string out(name);
        std::ranges::replace(out, '-', '_');
        return out;
    }

    inline SCM boolResult(bool b) {
        return b ? SCM_BOOL_T : SCM_BOOL_F;
    }

    inline SCM windowHandleResult(PHLWINDOW w) {
        return w ? hl::windowHandle(w) : SCM_BOOL_F;
    }

    inline SCM monitorHandleResult(PHLMONITOR mon) {
        return mon ? hl::monitorHandle(mon) : SCM_BOOL_F;
    }

    inline SCM workspaceHandleResult(PHLWORKSPACE ws) {
        return ws ? hl::workspaceHandle(ws) : SCM_BOOL_F;
    }

    // run a getter against the object behind a handle, or read #f
    template <typename F>
    inline SCM wsGet(SCM id, F&& fn) {
        if (!Internals::g_up)
            return SCM_BOOL_F;
        const auto ws = hl::workspaceOf(id);
        if (!ws)
            return SCM_BOOL_F;
        return fn(ws);
    }

    template <typename F>
    inline SCM monGet(SCM id, F&& fn) {
        if (!Internals::g_up)
            return SCM_BOOL_F;
        const auto mon = hl::monitorOf(id);
        if (!mon)
            return SCM_BOOL_F;
        return fn(mon);
    }

    // a dispatcher's result -> the Scheme-side integer protocol the API uses
    // (0 ok, negative refused), logging the engine's message on failure
    inline int actionResult(const char* name, Config::Actions::ActionResult result) {
        if (result)
            return 0;
        LOG(Log::ERR, "[scheme] {} failed: {}", name, result.error().message);
        return -2;
    }

    // A DIRECTION, converted at the boundary: the Scheme side has already
    // decided the value (it is an enum there, so it cannot be anything else),
    // and this is only the SCM-to-int hop.
    //
    // This used to take a const char* and run it through Math::fromChar, and
    // THAT was how a compositor crash was reachable. fromChar reads the FIRST
    // CHARACTER only and answers DIRECTION_DEFAULT (= -1) for anything it does
    // not recognise, so "" / "north" / "z" all became "no direction", and
    // Actions::moveInDirection then turned that into the empty string via
    // dirToString and handed it to CLayoutManager::moveInDirection, which calls
    // .at(0) on it — std::out_of_range, thrown out through the gsubr trampoline
    // (which catches nothing) into terminate. An abort of the whole session,
    // from a bad direction string.
    //
    // Taking the integer means the value is already one of the four (or, for
    // the one caller that has a directionless form, DIRECTION_DEFAULT's -1 on
    // purpose). No string exists to be empty, so the throwing path is
    // unreachable rather than merely guarded. `sc` is a plain cast — the
    // validation happened in Scheme, where the enum is.
    inline Math::eDirection actionDir(int n) {
        return sc<Math::eDirection>(n);
    }

    // the window an action applies to: the handle, or nullopt. #f means "the
    // focused window" — the one place that convention lives (Window.cpp)
    std::optional<PHLWINDOW> actionWindow(SCM id);
    PHLWINDOW                windowOrFocused(SCM v);

    // A workspace/monitor argument: a handle used directly, or a selector
    // string resolved with the compositor's own grammar. The C++ these feed
    // wants the OBJECT, so a handle is never re-derived from text — that was
    // both wasteful and lossy (it could not express the grammar at all).
    // (defined in Workspace.cpp)
    PHLWORKSPACE workspaceArg(SCM v);
    PHLMONITOR   monitorArg(SCM v);

    // selectors and name lookups, shared between the families that resolve them
    bool                   windowMatchesSelector(const PHLWINDOW& w, const std::string& sel);
    PHLMONITOR             monitorFromName(const char* name);
    PHLWORKSPACE           workspaceFromName(const char* name);
    PHLWORKSPACE           specialWorkspaceFromName(const std::string& wsName, const PHLMONITOR& mon);
    PHLWORKSPACE           workspaceFromSelector(const std::string& sel);
} // namespace Config::Scheme
