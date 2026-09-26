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
#include <src/desktop/view/window/Window.hpp>
#include <src/desktop/rule/Rule.hpp>

namespace Config::Scheme {

    using namespace Internals;

    static int hlSchemeFocusDirection(const char* dir) {
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

    static int hlSchemePass(SCM id) {
        if (!g_up)
            return -1;
        return actionResult("pass", Config::Actions::pass(actionWindow(id)));
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
        const auto sym = xkb_keysym_from_name(key ? key : "", XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == 0)
            return -1;
        return actionResult("send-shortcut", Config::Actions::pass(*mask, sc<uint32_t>(sym), actionWindow(id)));
    }

    static int hlSchemeSendKeyState(SCM mods, const char* key, int state, SCM id) {
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
            const auto        e      = Desktop::Rule::windowEffects()->get(std::string_view(effect));
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
