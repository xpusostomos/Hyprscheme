/*
    The exec family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"
#include <src/config/supplementary/executor/Executor.hpp>
#include <src/input/Keys.hpp>
#include <src/pointer/PointerManager.hpp>
#include <src/desktop/rule/Engine.hpp>
#include <src/desktop/rule/windowRule/WindowRule.hpp>
#include <src/config/supplementary/propRefresher/PropRefresher.hpp>
#include <src/managers/SessionLockManager.hpp>

#include <src/helpers/math/Direction.hpp>
#include <src/keybinds/Manager.hpp>
#include <src/keybinds/Resolver.hpp>
#include <src/desktop/view/window/Window.hpp>
#include <src/desktop/rule/Rule.hpp>

namespace Config::Scheme {

    using namespace Internals;

    // DIR is the compositor's own integer (Math::eDirection) — the Scheme side
    // holds a direction-enum and converts it, so nothing here can be a string
    // the compositor would have to parse. See actionDir in SchemeInternals.hpp.
    static int hlSchemeFocusDirection(int dir) {
        if (!g_up)
            return -1;
        return actionResult("focus-direction", Config::Actions::moveFocus(actionDir(dir)));
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

    static int hlSchemeCursorMove(double x, double y) {
        if (!g_up)
            return -1;
        return actionResult("cursor-move", Config::Actions::moveCursor(Vector2D{x, y}));
    }

    static int hlSchemeCursorCorner(SCM id, int corner) {
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

    static int hlSchemeDpms(int act, SCM monArg) {
        if (!g_up)
            return -1;
        std::optional<PHLMONITOR> mon; // absent = every monitor
        if (!scm_is_false(monArg) && !(scm_is_string(monArg) && scm_to_utf8_string(monArg) && !*scm_to_utf8_string(monArg))) {
            mon = monitorArg(monArg);
            if (!mon) {
                LOG(Log::ERR, "[scheme] dpms: no monitor for that argument");
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

    static int hlSchemePass(SCM id) {
        if (!g_up)
            return -1;
        return actionResult("pass", Config::Actions::pass(actionWindow(id)));
    }

    // A key NAME ("f", "Right", "code:28", "mouse:272") -> the XKB KEYCODE that
    // Actions::pass/sendKeyState actually take. NOT a keysym: pass() forwards
    // its key straight to sendKeyboardKey as `key - 8`, i.e. an evdev/XKB
    // keycode (the same units as state()->m_lastCode, set from KEYCODE in
    // KeybindManager). The two numberings are unrelated, so a keysym handed to
    // pass() silently sends the WRONG key — and for a named key like Right it
    // sends a number far outside the keycode range, which is dropped without a
    // word. The scripting API resolves at exactly this point
    // (LuaBindingsDispatchers dsp_sendShortcut/dsp_sendKeyState call
    // Keybinds::resolver()->resolveKeycode), and so do we.
    //
    // This is a conversion, not a policy: the caller says "f", the compositor
    // wants a keycode, and the boundary is the one place that is allowed to
    // know both. Returns std::nullopt for a name the keymap does not have.
    static std::optional<uint32_t> keycodeFromName(const char* key) {
        const auto code = Keybinds::resolver()->resolveKeycode(key ? key : "");
        if (!code) {
            LOG(Log::ERR, "[scheme] no such key: {}", code.error());
            return std::nullopt;
        }
        return *code;
    }

    // mods arrive as a LIST of modifier tokens (as built by hl-kbd/hl-key),
    // the same shape every mods-taking API takes; only the key is resolved
    // from its string name here
    static int hlSchemeSendShortcut(SCM mods, const char* key, SCM id) {
        if (!g_up)
            return -1;
        const auto mask = modsMaskFromTokens(mods);
        if (!mask)
            return -1;
        const auto code = keycodeFromName(key);
        if (!code)
            return -1;
        return actionResult("send-shortcut", Config::Actions::pass(*mask, *code, actionWindow(id)));
    }

    // STATE is the compositor's own key-state integer (0 up, 1 down, 2 repeat),
    // arrived at from a key-state-enum on the Scheme side — the same split the
    // scripting API makes, where its dispatcher maps "down"/"up"/"repeat" to
    // these numbers before calling the action. The gsubr mirrors
    // Actions::sendKeyState's parameter and makes no decision of its own.
    static int hlSchemeSendKeyState(SCM mods, const char* key, int state, SCM id) {
        if (!g_up)
            return -1;
        const auto mask = modsMaskFromTokens(mods);
        if (!mask)
            return -1;
        const auto code = keycodeFromName(key);
        if (!code)
            return -1;
        return actionResult("send-key-state", Config::Actions::sendKeyState(*mask, *code, sc<uint32_t>(state), actionWindow(id)));
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

    static int hlSchemeLayoutMessage(const char* msg) {
        if (!g_up)
            return -1;
        return actionResult("layout-msg", Config::Actions::layoutMessage(std::string(msg ? msg : "")));
    }

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
    static int hlSchemeExec(const char* cmd, SCM effects) {
        if (!g_up || !cmd || !*cmd)
            return -1;

        // walk the effects plist; empty → plain spawn, else build the rule
        auto rule = makeShared<Desktop::Rule::CWindowRule>();
        bool any  = false;
        for (SCM l = effects; scm_is_pair(l); l = scm_cdr(scm_cdr(l))) {
            if (!scm_is_pair(scm_cdr(l))) {
                g_configError = "hl-exec!: odd plist of rule effects";
                return -1;
            }
            const std::string effect = schemeDatumToStr(scm_car(l));
            const auto        e      = Desktop::Rule::windowEffects()->get(hyprlandName(effect));
            if (!e) {
                g_configError = std::format("hl-exec!: unknown rule effect '{}'", effect);
                return -1;
            }
            const auto res = rule->addEffect(*e, schemeDatumToStr(scm_car(scm_cdr(l))));
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

    static SCM hlSchemeCursorPos() {
        if (!g_up || !Pointer::mgr())
            return SCM_BOOL_F;

        const auto pos = Pointer::mgr()->untransformedPosition();
        return scm_cons(scm_from_int64((int)pos.x), scm_from_int64((int)pos.y));   // (x . y)
    }

    // the one mods representation across the API: a LIST of modifier tokens
    // (strings), as built by hl-kbd/hl-key — never a string to split and
    // never a raw mask int. Unknown tokens / non-strings are rejected.
    std::optional<Input::ModifierMask> modsMaskFromTokens(SCM mods) {
        uint8_t raw = 0;
        for (SCM l = mods; scm_is_pair(l); l = scm_cdr(l)) {
            if (!scm_is_string(scm_car(l))) {
                g_configError = "'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\")";
                return std::nullopt;
            }
            std::string tok = schemeDatumToStr(scm_car(l));
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

    // this family's Scheme-visible surface
    void registerExec() {
        hl::bind<hlSchemeFocusDirection>("hl--c-focus-direction");
        hl::bind<hlSchemeFocusLast>("hl--c-focus-last");
        hl::bind<hlSchemeFocusUrgent>("hl--c-focus-urgent");
        hl::bind<hlSchemeCursorMove>("hl--c-cursor-move");
        hl::bind<hlSchemeCursorCorner>("hl--c-cursor-corner");
        hl::bind<hlSchemeCursorPos>("hl--c-cursor-pos");
        hl::bind<hlSchemeExit>("hl--c-exit");
        hl::bind<hlSchemeReloadConfig>("hl--c-reload-config");
        hl::bind<hlSchemeForceRendererReload>("hl--c-force-renderer-reload");
        hl::bind<hlSchemeDpms>("hl--c-dpms");
        hl::bind<hlSchemeForceIdle>("hl--c-force-idle");
        hl::bind<hlSchemeGlobal>("hl--c-global");
        hl::bind<hlSchemeEvent>("hl--c-event");
        hl::bind<hlSchemePass>("hl--c-pass");
        hl::bind<hlSchemeSendShortcut>("hl--c-send-shortcut");
        hl::bind<hlSchemeSendKeyState>("hl--c-send-key-state");
        hl::bind<hlSchemeMouse>("hl--c-mouse");
        hl::bind<hlSchemeReleaseInputCapture>("hl--c-release-input-capture");
        hl::bind<hlSchemeLayoutMessage>("hl--c-layout-message");
        hl::bind<hlSchemeClearCrashedLockscreen>("hl--c-clear-crashed-lockscreen");
        hl::bind<hlSchemeScheduledPropRefreshImmediately>("hl--c-scheduled-prop-refresh-immediately");
        hl::bind<hlSchemeExec>("hl--c-exec!");
    }

} // namespace Config::Scheme
