/*
    The bind family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"

#include <src/config/shared/actions/ConfigActions.hpp>
#include <src/keybinds/InputState.hpp>
#include <src/keybinds/Manager.hpp>
#include <src/keybinds/Resolver.hpp>
#include <src/managers/input/InputManager.hpp>

#include <xkbcommon/xkbcommon.h>
#include <algorithm>



namespace Config::Scheme {

    using namespace Internals;

    static Keybinds::SBindResult fireSchemeBindRec(SCM record); // defined below

    // THE SUBMAP IS A PER-BIND ARGUMENT, and there is no registration context
    // any more. It used to be two globals here (g_regSubmap/g_regSubmapReset)
    // that hl-submap set for the duration of a thunk, so a bind inherited
    // whatever name happened to be ambient while it was created. That matched
    // how the .conf format works (a `submap = resize` directive that changes the
    // parser's state) and how the Lua binding does it, but it made the name
    // unexpressible: neither language could say "this ONE bind goes in resize".
    //
    // Nothing in the compositor required that. CBind::make takes the whole
    // SBindMetadata, and Hyprland's LuaBindingsToplevel.cpp fills .submap from
    // its manager — it is a plain per-bind field all the way down. So the
    // ambient pair is gone, hl-bind-add! takes #:submap / #:submap-reset, and
    // this file writes the arguments straight into the metadata. "" is the
    // default map, as it always was (setSubmap treats "" and "reset" alike).

    // ---- bind handles ---------------------------------------------------------
    // A bind's Scheme handle is an hl-bind RECORD (token list + thunk). The
    // capture inside the CBind carries an SThunkRef to it (SchemeInternals.hpp):
    // locked while any copy lives, released when the bind is destroyed. There
    // is NO bind registry on our side: Hyprland's keybind registry is the only
    // list. The record's pinned address, stamped into the bind's argument
    // metadata at registration, identifies our binds for precise unbind and
    // plugin shutdown.
    static std::string bindTag(SCM record) {
        return "scheme:" + std::to_string(SCM_UNPACK(record));
    }

    // The DISPLAY KEY — Hyprland's own metadata field, which removeBinds matches
    // on and hyprctl prints. It is DERIVED here, never taken from Scheme: a
    // token list is the only thing that crosses the boundary, and this is the
    // single place a string is unavoidable, because Hyprland's field is one.
    //
    // Joined with '+', NOT a space, and that matters for unbinding. A bind
    // registered from Lua stores its argument verbatim, so hl.bind("SUPER +
    // comma") has the display key "SUPER + comma"; normalizeDisplayKey
    // (Registry.cpp) strips whitespace and lowercases but does NOT strip '+'.
    // So "SUPER+comma" and "SUPER + comma" both normalise to "super+comma" and
    // match, while a space-joined "SUPER comma" normalises to "supercomma" and
    // matches nothing — which is how a config could silently fail to stand down
    // a bind the Lua side had registered.
    static std::string displayKeyOf(const std::vector<std::string>& keys) {
        std::string out;
        for (const auto& k : keys) {
            if (!out.empty())
                out += '+';
            out += k;
        }
        return out;
    }

    // The token strings out of a Scheme list, ignoring anything not a string.
    static std::vector<std::string> tokensOf(SCM tokens) {
        std::vector<std::string> keys;
        for (SCM p = tokens; scm_is_pair(p) && p != SCM_EOL; p = scm_cdr(p))
            if (scm_is_string(scm_car(p)))
                keys.emplace_back(hl::toStdString(scm_car(p)));
        return keys;
    }

    // called from Scheme as a gsubr; flags are raw eBindFlags bits,
    // assembled Scheme-side from the options plist. SUBMAP and SUBMAP_RESET are
    // the bind's own (see the note above) — "" for both means the default map
    // with no follow-up.
    static int hlSchemeBind(SCM record, SCM tokens, int flags, const char* desc, SCM devices,
                            const char* submap, const char* submapReset) {
        if (!g_up)
            return -1;

        SThunkRef ref(record); // lock FIRST: the record is a GC root from here on

        std::vector<std::string> keys;
        for (SCM p = tokens; scm_is_pair(p) && p != SCM_EOL; p = scm_cdr(p)) {
            SCM elem = scm_car(p);
            if (scm_is_string(elem))
                keys.emplace_back(hl::toStdString(elem));
        }

        Keybinds::SExtraBindArgs args;
        args.metadata.displayKey = displayKeyOf(keys);
        args.metadata.argument   = bindTag(record);
        if (desc && *desc)
            args.metadata.description = desc;
        args.metadata.submap      = submap ? submap : "";
        args.metadata.submapReset = submapReset ? submapReset : "";
        // devices arrive as a real list of name strings; a non-list element
        // or a non-string device is rejected
        for (SCM p = devices; scm_is_pair(p) && p != SCM_EOL; p = scm_cdr(p)) {
            if (!scm_is_string(scm_car(p))) {
                g_configError = "hl-bind-add!: 'devices must be a list of device name strings";
                return -1;
            }
            args.devices.emplace(schemeDatumToStr(scm_car(p)));
        }

        auto bind = Keybinds::CBind::make(std::move(keys), sc<Keybinds::BindFlags>(flags), [ref] { return fireSchemeBindRec(ref.obj); }, std::move(args));
        if (!bind) {
            // g_configError is what the Scheme side reports when a gsubr answers
            // -1, so returning without setting it surfaced whatever the PREVIOUS
            // failure had left there — a bind rejected for "invalid key" would
            // be blamed on an unrelated call made earlier in the config.
            g_configError = std::format("hl-bind-add!: {}", bind.error());
            LOG(Log::ERR, "[scheme] bind failed: {}", bind.error());
            return -1;
        }

        // no registry of our own: the manager owns the bind from here; its
        // eventual destruction (unbind, reload clearBinds, shutdown) runs the
        // capture destructor, which releases the record lock
        (void)Keybinds::mgr()->addBind(std::move(*bind));
        return 0;
    }

    // called from Scheme as a gsubr: remove one scheme bind
    // TOKENS, never a string — the lookup key is derived from them exactly as
    // the bind's was, so the two can never disagree. Coarse by design: removes
    // EVERY bind on that key (upstream hl.unbind parity); precise removal goes
    // through hlSchemeUnbindRec.
    static int hlSchemeUnbindKey(SCM tokens) {
        if (!g_up || !Keybinds::mgr())
            return -1;
        const auto keys = tokensOf(tokens);
        if (keys.empty())
            return -1;
        const auto removed = Keybinds::mgr()->removeBinds(displayKeyOf(keys));
        return removed > 0 ? 0 : -1;
    }

    // precise: find OUR bind by the argument tag (the record's pinned
    // address) and removeBind it — exactly one match, however many same-key
    // siblings exist
    // our bind, found by the argument tag (the record's pinned address) — the
    // identity that survives however many same-key siblings exist. Shared by
    // unbind and by enable/disable.
    static Keybinds::PBind findSchemeBind(SCM record) {
        if (!g_up || !Keybinds::mgr())
            return nullptr;
        const std::string tag = bindTag(record);
        for (const Keybinds::PBind& b : Keybinds::mgr()->registry().binds())
            if (b && b->metadata().argument == tag)
                return b;
        return nullptr;
    }

    static int hlSchemeUnbindRec(SCM record) {
        const auto b = findSchemeBind(record);
        if (!b)
            return -1; // not registered (already unbound, or a stale handle)
        Keybinds::mgr()->removeBind(b);
        return 0;
    }

    // ARM/DISARM a bind without destroying it. This is what makes a bind
    // CONDITIONAL: registered once, then enabled only while some predicate
    // holds. A disabled bind does not match, so its key falls through to the
    // focused app untouched — with the app's own autorepeat, which no amount of
    // synthetic key injection can reproduce.
    static int hlSchemeBindEnabledSet(SCM record, SCM on) {
        const auto b = findSchemeBind(record);
        if (!b)
            return -1;
        b->setEnabled(scm_is_true(on));
        return 0;
    }

    static int hlSchemeBindEnabled(SCM record) {
        const auto b = findSchemeBind(record);
        if (!b)
            return -1; // stale handle: not registered any more
        return b->enabled() ? 1 : 0;
    }

    // fires a RECORD bind (the capture passes the locked record); the Scheme
    // trampoline extracts the thunk. Result contract identical to the id path.
    static Keybinds::SBindResult fireSchemeBindRec(SCM record) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        watchdogEnter("bind callback");
        const SCM r = hl::call1(hl::globalRef("hl--bind-fire-rec"), record);
        watchdogExit();
        if (r == SCM_BOOL_F)
            return {.success = false, .error = "scheme keybind callback declined"};
        if (r == SCM_BOOL_T || !scm_is_pair(r) || !scm_is_symbol(scm_car(r)))
            return {}; // handled — 'ok defaults to true

        Keybinds::SBindResult res;
        SCM l = r;
        while (scm_is_pair(l) && scm_is_pair(scm_cdr(l))) {
            const std::string k = schemeDatumToStr(scm_car(l));
            const SCM        v  = scm_car(scm_cdr(l));
            if (k == "ok") {
                if (v == SCM_BOOL_F)
                    res.success = false;
            } else if (k == "pass-event") {
                if (v == SCM_BOOL_T)
                    res.passEvent = true;
            } else if (k == "request-release") {
                if (v == SCM_BOOL_T)
                    res.followUp = Keybinds::BIND_FOLLOW_UP_TRIGGER_RELEASE;
            } else if (k == "error" && scm_is_string(v)) {
                res.error = schemeDatumToStr(v);
            }
            l = scm_cdr(scm_cdr(l));
        }
        return res;
    }

    // called from Scheme as a gsubr: subscribe to submap changes
    static int hlSchemeSubmapListen(SCM record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.keybinds.submap.listen([ref = SThunkRef(record)](const std::string& name) {
            fireSchemeStr(ref.obj, name);
        }));
        return 0;
    }

    // unlisten: drop the connection; its destruction unregisters from the
    // bus and releases the handler record's lock. Upstream HL.EventSub-
    // scription:remove parity. The record goes inert: cancel again -> -1.
    static int hlSchemeEventCancel(SCM record) {
        if (!g_up)
            return -1;
        const auto it = g_eventConnections.find(SCM_UNPACK(record));
        if (it == g_eventConnections.end())
            return -1;
        g_eventConnections.erase(it);
        return 0;
    }

    // upstream HL.EventSubscription:is_active parity
    static int hlSchemeEventActive(SCM record) {
        if (!g_up)
            return -1;
        return g_eventConnections.count(SCM_UNPACK(record)) ? 1 : 0;
    }

    static SCM hlSchemeCurrentSubmap() {
        if (!g_up || !Keybinds::mgr())
            return SCM_BOOL_F;

        const auto submap = std::string(Keybinds::mgr()->currentSubmap());
        return scm_from_utf8_stringn(submap.c_str(), submap.size());
    }

    static int enterSubmap(const char* name) {
        if (!g_up || !name)
            return -1;
        return Config::Actions::setSubmap(name) ? 0 : -1;
    }

    // ---- key regularisation ---------------------------------------------------
    //
    // The canonical form of ONE chord: the identity two spellings of the same
    // chord share. Hyprland normalises a bind internally — a CKey plus a
    // modifier bitmask — but never renders that back out (LuaKeybind's keyName()
    // returns the spelling as typed), so this is the first place the identity
    // crosses into Scheme.
    //
    // Two things here cannot be reproduced by sorting the tokens, which is the
    // whole reason this is C++ rather than Scheme:
    //   - modifier ALIASES are Hyprland's to resolve (Resolver.cpp
    //     MODIFIER_NAMES: CONTROL/CTRL, MOD1/ALT and WIN/LOGO/MOD4/SUPER/META
    //     each collapse to one mask), so "MOD1+x" and "M-x" must come out
    //     identical rather than as two different chords;
    //   - the KEY's case and canonical spelling are xkb's
    //     (xkb_keysym_from_name is case-insensitive, xkb_keysym_get_name
    //     renders back), so "x"/"X" and "Tab"/"TAB" collapse.
    // xkb does NOT resolve key aliases — "Esc", "Enter" and "Del" are invalid
    // (verified against libxkbcommon) — so the emacs shorthands stay in Scheme,
    // in hl--emacs-keys, where they already are.

    // mask -> canonical spelling. Hyprland carries only the reverse table
    // (MODIFIER_NAMES); this is its inverse, and deliberately the SHORT names,
    // which is what hl--emacs-mods maps to and from.
    static constexpr std::pair<Input::eKeyboardModifiers, const char*> MODIFIER_CANON[] = {
        {Input::HL_MODIFIER_SHIFT, "SHIFT"}, {Input::HL_MODIFIER_CAPS, "CAPS"}, {Input::HL_MODIFIER_CTRL, "CTRL"},
        {Input::HL_MODIFIER_ALT, "ALT"},     {Input::HL_MODIFIER_MOD2, "MOD2"}, {Input::HL_MODIFIER_MOD3, "MOD3"},
        {Input::HL_MODIFIER_META, "SUPER"},  {Input::HL_MODIFIER_MOD5, "MOD5"},
    };

    // TOKENS is the token list hl-bind-add! takes, for one chord. Returns the
    // same shape, regularised — modifiers first in a fixed order, then the key —
    // or #f on a malformed chord, with g_configError carrying why.
    static SCM hlSchemeKeyRegularise(SCM tokens) {
        if (!g_up)
            return SCM_BOOL_F;

        Input::ModifierMask      mask = Input::HL_MODIFIER_NONE;
        std::vector<std::string> keys;   // the non-modifier tokens, in order

        for (SCM p = tokens; scm_is_pair(p) && p != SCM_EOL; p = scm_cdr(p)) {
            if (!scm_is_string(scm_car(p))) {
                g_configError = "hl-key-regularise: every token must be a string";
                return SCM_BOOL_F;
            }
            const std::string TOK = hl::toStdString(scm_car(p));

            std::string upper = TOK;
            std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return std::toupper(c); });

            if (const auto MOD = Keybinds::modifierFromString(upper)) {
                mask |= *MOD;
                continue;
            }

            // A keysym rendered canonically, or an event/code token left exactly
            // as it came in ("code:28", "mouse:272", "switch:lid", "catchall").
            // ALL of them are kept, in order — CBind::make accepts several, so
            // taking only the last would silently turn "TAB T" into "T".
            std::string canonical = TOK;
            if (const auto SYM = xkb_keysym_from_name(TOK.c_str(), XKB_KEYSYM_CASE_INSENSITIVE); SYM != XKB_KEY_NoSymbol) {
                char buf[128] = {};
                xkb_keysym_get_name(SYM, buf, sizeof buf);
                canonical = buf;
            }
            // Lower case throughout. xkb renders some names capitalised
            // ("Return", "Tab", "F1") and Hyprland resolves names
            // case-insensitively, so folding the case costs nothing and keeps
            // one spelling per key in the canonical form and in submap names.
            std::ranges::transform(canonical, canonical.begin(), [](unsigned char c) { return std::tolower(c); });
            keys.push_back(std::move(canonical));
        }

        if (keys.empty()) {
            g_configError = "hl-key-regularise: a chord needs a key, not only modifiers";
            return SCM_BOOL_F;
        }

        std::vector<SCM> roots;
        std::vector<SCM> elems;
        for (const auto& [BIT, NAME] : MODIFIER_CANON) {
            if (!(mask & BIT))
                continue;
            SCM s = scm_from_utf8_string(NAME);
            hl::pin(s, roots);
            elems.push_back(s);
        }
        for (const auto& K : keys) {
            SCM k = scm_from_utf8_string(K.c_str());
            hl::pin(k, roots);
            elems.push_back(k);
        }

        SCM out = SCM_EOL;
        for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
            out = scm_cons(*it, out);
            hl::pin(out, roots);
        }
        hl::unpin(roots);
        return out;
    }

    // ---- logical <-> physical keys --------------------------------------------
    //
    // Hyprland keys binds on the keysym of a PHYSICAL key at level 0, resolved
    // from a modifier-free state (Keybinds::Manager, m_xkbTranslationState).
    // So "SUPER + percent" parses and then matches nothing: percent is never a
    // level-0 symbol. What actually reaches the seat for the "%" character is
    // keycode 14 with SHIFT, i.e. keysym "5" + the shift bit. Hyprland cannot
    // express that itself — it only ever calls xkb_keysym_from_name, and never
    // introspects keymap levels (no key_get_syms_by_level anywhere in the tree).
    //
    // These two functions are that missing step: they ask the active keymap
    // which (keycode, level) produces a symbol, and which modifier mask reaches
    // that level. A LOGICAL key is the symbol the user means ("%", "A",
    // "Return"); a PHYSICAL key is what has to be pressed ("SHIFT" "5").
    //
    // The level machinery is xkbcommon's and is layout-aware: on us "%" is
    // SHIFT+5, on fr it is a different key entirely, and "#" is AltGr (MOD5)
    // rather than SHIFT. So the mask comes from the keymap, never assumed.

    // The keymap to introspect: the first enabled keyboard's. They all share a
    // layout in practice, and this mirrors how a bind is resolved at the seat.
    static xkb_keymap* activeKeymap() {
        if (!g_pInputManager)
            return nullptr;
        for (const auto& kb : g_pInputManager->m_keyboards)
            if (kb && kb->m_xkbKeymap)
                return kb->m_xkbKeymap;
        return nullptr;
    }

    // Modifier bits we can name as bind tokens, in the canonical order.
    static std::vector<std::string> maskToTokens(xkb_mod_mask_t mask) {
        std::vector<std::string> out;
        for (const auto& [BIT, NAME] : MODIFIER_CANON)
            if (mask & static_cast<xkb_mod_mask_t>(BIT))
                out.emplace_back(NAME);
        return out;
    }

    // A keysym -> the token Hyprland can PARSE. This is xkb's own name, always:
    // "5", "a", "Return", "F1" — and "period", "comma", "percent" for the
    // punctuation keys. It must NOT be the character: Hyprland resolves names
    // through xkb_keysym_from_name, which has no entry for ".", so emitting "."
    // yields "Unknown key: ." (the trap that made s-period fail).
    static std::string keysymToName(xkb_keysym_t sym) {
        char name[128] = {};
        if (xkb_keysym_get_name(sym, name, sizeof name) <= 0)
            return {};
        return name;
    }

    // A keysym -> the token a USER reads and writes: the character where there
    // is one ("%", "A", "."), else the xkb name ("Return", "F1", "Left").
    // This is the logical direction, and the only place a character appears.
    static std::string keysymToChar(xkb_keysym_t sym) {
        const uint32_t CP = xkb_keysym_to_utf32(sym);
        if (CP >= 0x21 && CP <= 0x7E)   // printable ASCII, excluding space
            return std::string(1, static_cast<char>(CP));
        return keysymToName(sym);
    }

    // The keysym a token names, accepting BOTH spellings: the xkb name
    // ("percent", "Return") and a literal character ("%"). xkb knows only the
    // former — xkb_keysym_from_name("%") is NoSymbol — so a one-character token
    // is tried as a codepoint too. CASE-SENSITIVE, deliberately: "A" must stay
    // distinct from "a" or the shift it implies is lost.
    static xkb_keysym_t tokenToKeysym(const std::string& token) {
        // EXACT first, so "A" stays distinct from "a" — that distinction is the
        // whole point of a logical upper-case key, and the case-insensitive
        // flag would erase it.
        if (const auto SYM = xkb_keysym_from_name(token.c_str(), XKB_KEYSYM_NO_FLAGS); SYM != XKB_KEY_NoSymbol)
            return SYM;
        // then case-insensitively, because a named key may be spelled in any
        // case ("F1", "ESCAPE", "CTRL_L" all arrive capitalised from hl-kbd)
        // and Hyprland itself matches names case-insensitively. Only reached
        // for tokens that are not an exact keysym, so "A" is already resolved.
        if (const auto SYM = xkb_keysym_from_name(token.c_str(), XKB_KEYSYM_CASE_INSENSITIVE); SYM != XKB_KEY_NoSymbol)
            return SYM;
        // finally a literal character ("%"), which xkb knows only as "percent"
        if (token.size() == 1)
            return xkb_utf32_to_keysym(static_cast<uint32_t>(static_cast<unsigned char>(token[0])));
        return XKB_KEY_NoSymbol;
    }

    // Split a token list into its modifier mask and its key token.
    static bool splitTokens(SCM tokens, xkb_mod_mask_t& mask, std::string& key, bool& isEvent) {
        // Tokens that carry no keymap level, so must be passed through untouched:
        //   - a SIDED MODIFIER used as the trigger ("CTRL_L"), which is how a
        //     mouse bind is spelled. Hyprland accepts it (SIDED_MODIFIER_NAMES in
        //     keybinds/Bind.cpp) but xkb knows only "Control_L", so sending it to
        //     the keymap lookup would fail on a token Hyprland resolves fine.
        static constexpr const char* SIDED[] = {"SHIFT_L", "SHIFT_R", "CONTROL_L", "CTRL_L",  "CONTROL_R",
                                                "CTRL_R",  "ALT_L",   "ALT_R",     "SUPER_L", "SUPER_R"};

        bool sawKey = false;
        bool sided  = false;
        for (SCM p = tokens; scm_is_pair(p) && p != SCM_EOL; p = scm_cdr(p)) {
            if (!scm_is_string(scm_car(p))) {
                g_configError = "every token must be a string";
                return false;
            }
            const std::string TOK = hl::toStdString(scm_car(p));
            std::string       upper = TOK;
            std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return std::toupper(c); });
            if (const auto MOD = Keybinds::modifierFromString(upper)) {
                mask |= static_cast<xkb_mod_mask_t>(*MOD);
                continue;
            }
            key    = TOK;
            sawKey = true;
            for (const char* S : SIDED) {
                if (upper == S) {
                    sided = true;
                    break;
                }
            }
        }
        if (!sawKey) {
            g_configError = "a chord needs a key, not only modifiers";
            return false;
        }
        isEvent = sided || key.starts_with("code:") || key.starts_with("mouse:") || key.starts_with("switch:") || key == "catchall";
        return true;
    }

    static SCM tokensToList(const std::vector<std::string>& toks) {
        std::vector<SCM> roots;
        SCM              out = SCM_EOL;
        for (auto it = toks.rbegin(); it != toks.rend(); ++it) {
            SCM s = scm_from_utf8_string(it->c_str());
            hl::pin(s, roots);
            out = scm_cons(s, out);
            hl::pin(out, roots);
        }
        hl::unpin(roots);
        return out;
    }

    // LOGICAL -> PHYSICAL: ("A") -> ("SHIFT" "a"); ("%") -> ("SHIFT" "5").
    // Named keys and event tokens pass through untouched.
    static SCM hlSchemeKeyLogicalToPhysical(SCM tokens) {
        if (!g_up)
            return SCM_BOOL_F;

        xkb_mod_mask_t mask = 0;
        std::string    key;
        bool           isEvent = false;
        if (!splitTokens(tokens, mask, key, isEvent))
            return SCM_BOOL_F;

        std::vector<std::string> toks;
        if (isEvent) {
            toks = maskToTokens(mask);
            toks.push_back(key);
            return tokensToList(toks);
        }

        const auto KM = activeKeymap();
        if (!KM) {
            g_configError = "hl-key-logical-to-physical: no keyboard/keymap available";
            return SCM_BOOL_F;
        }

        const xkb_keysym_t WANT = tokenToKeysym(key);
        if (WANT == XKB_KEY_NoSymbol) {
            g_configError = std::format("hl-key-logical-to-physical: cannot resolve key \"{}\"", key);
            return SCM_BOOL_F;
        }

        // find the key+level producing WANT, and the mask that reaches it
        for (xkb_keycode_t kc = 8; kc < 256; ++kc) {
            const xkb_layout_index_t NLAYOUTS = xkb_keymap_num_layouts_for_key(KM, kc);
            for (xkb_layout_index_t lay = 0; lay < NLAYOUTS; ++lay) {
                const xkb_level_index_t NLEVELS = xkb_keymap_num_levels_for_key(KM, kc, lay);
                for (xkb_level_index_t lv = 0; lv < NLEVELS; ++lv) {
                    const xkb_keysym_t* syms = nullptr;
                    if (xkb_keymap_key_get_syms_by_level(KM, kc, lay, lv, &syms) != 1 || syms[0] != WANT)
                        continue;

                    xkb_mod_mask_t      masks[16] = {};
                    const unsigned      GOT       = xkb_keymap_key_get_mods_for_level(KM, kc, lay, lv, masks, 16);
                    const xkb_mod_mask_t LEVELMASK = GOT ? masks[0] : 0;

                    // the level-0 symbol of that key is what Hyprland matches on
                    xkb_state* st = xkb_state_new(KM);
                    if (!st) {
                        g_configError = "hl-key-logical-to-physical: could not build a state";
                        return SCM_BOOL_F;
                    }
                    const xkb_keysym_t BASE = xkb_state_key_get_one_sym(st, kc);
                    xkb_state_unref(st);

                    std::string base = keysymToName(BASE);
                    if (base.empty()) {
                        g_configError = "hl-key-logical-to-physical: no name for the base key";
                        return SCM_BOOL_F;
                    }

                    // our modifiers first, then whatever the level adds
                    toks = maskToTokens(mask);
                    for (auto& m : maskToTokens(LEVELMASK))
                        if (std::ranges::find(toks, m) == toks.end())
                            toks.push_back(m);
                    toks.push_back(base);
                    return tokensToList(toks);
                }
            }
        }

        // Not on this keymap. A NAMED key (F13, Return, Left) needs no level
        // lookup — Hyprland binds it by name, and a keymap that lacks it is the
        // keymap's business, not ours — so pass it through with its modifiers.
        // A CHARACTER, though, has to be producible: that is the whole point of
        // the conversion, so failing to place it is a real error.
        if (xkb_keysym_to_utf32(WANT) < 0x21 || xkb_keysym_to_utf32(WANT) > 0x7E) {
            toks = maskToTokens(mask);
            toks.push_back(key);
            return tokensToList(toks);
        }

        g_configError = std::format("hl-key-logical-to-physical: cannot produce the character \"{}\" on the active layout", key);
        return SCM_BOOL_F;
    }

    // PHYSICAL -> LOGICAL: ("SHIFT" "5") -> ("%"); ("SHIFT" "a") -> ("A").
    static SCM hlSchemeKeyPhysicalToLogical(SCM tokens) {
        if (!g_up)
            return SCM_BOOL_F;

        xkb_mod_mask_t mask = 0;
        std::string    key;
        bool           isEvent = false;
        if (!splitTokens(tokens, mask, key, isEvent))
            return SCM_BOOL_F;

        if (isEvent)
            return tokensToList([&] {
                auto t = maskToTokens(mask);
                t.push_back(key);
                return t;
            }());

        const auto KM = activeKeymap();
        if (!KM) {
            g_configError = "hl-key-physical-to-logical: no keyboard/keymap available";
            return SCM_BOOL_F;
        }

        // The base keysym, exactly as Hyprland resolves it: a state with no
        // modifiers applied, read at that keycode.
        xkb_state* st = xkb_state_new(KM);
        if (!st) {
            g_configError = "hl-key-physical-to-logical: could not build a state";
            return SCM_BOOL_F;
        }
        // apply the mask so the level we read is the one the modifiers select
        xkb_mod_mask_t depressed = 0;
        for (const auto& [BIT, NAME] : MODIFIER_CANON)
            if (mask & static_cast<xkb_mod_mask_t>(BIT))
                depressed |= static_cast<xkb_mod_mask_t>(BIT);
        xkb_state_update_mask(st, depressed, 0, 0, 0, 0, 0);

        const xkb_keysym_t BASE = tokenToKeysym(key);

        // A NAMED key has no character to fold, so it passes through with its
        // modifiers untouched — and applying the mask would be actively wrong:
        // with CTRL|ALT down the F1 key yields XF86Switch_VT_1, not F1, so
        // reading it "at the requested level" would rewrite "C-M-<f1>" into a
        // VT switch. Only a CHARACTER needs the level machinery.
        {
            const uint32_t CP = xkb_keysym_to_utf32(BASE);
            if (CP < 0x21 || CP > 0x7E) {
                auto toks = maskToTokens(mask);
                toks.push_back(key);
                return tokensToList(toks);
            }
        }

        for (xkb_keycode_t kc = 8; kc < 256; ++kc) {
            xkb_state* plain = xkb_state_new(KM);
            const xkb_keysym_t L0 = plain ? xkb_state_key_get_one_sym(plain, kc) : XKB_KEY_NoSymbol;
            if (plain)
                xkb_state_unref(plain);
            if (L0 != BASE)
                continue;

            const xkb_keysym_t SYM = xkb_state_key_get_one_sym(st, kc);
            xkb_state_unref(st);

            std::string tok = keysymToChar(SYM);
            if (tok.empty()) {
                g_configError = "hl-key-physical-to-logical: no name for the resulting key";
                return SCM_BOOL_F;
            }

            // Which level produced it, and so which of our modifiers the
            // character ABSORBED: SHIFT for "%", AltGr for "#" on a fr layout.
            // Those must not be repeated in the output — ("SHIFT" "5") is "%",
            // not "S-%". Hyprland's modifier bits and xkb's real-modifier masks
            // coincide (both put SHIFT at 1<<0, MOD5 at 1<<7), so they subtract
            // directly.
            xkb_mod_mask_t          levelmask = 0;
            const xkb_level_index_t NLEVELS   = xkb_keymap_num_levels_for_key(KM, kc, 0);
            for (xkb_level_index_t lv = 0; lv < NLEVELS; ++lv) {
                const xkb_keysym_t* syms = nullptr;
                if (xkb_keymap_key_get_syms_by_level(KM, kc, 0, lv, &syms) != 1 || syms[0] != SYM)
                    continue;
                xkb_mod_mask_t masks[16] = {};
                if (xkb_keymap_key_get_mods_for_level(KM, kc, 0, lv, masks, 16))
                    levelmask = masks[0];
                break;
            }

            auto toks = maskToTokens(mask & ~levelmask);
            toks.push_back(tok);
            return tokensToList(toks);
        }

        xkb_state_unref(st);
        // same reasoning as the forward direction: a named key that the keymap
        // does not carry is passed through rather than refused
        auto toks = maskToTokens(mask);
        toks.push_back(key);
        return tokensToList(toks);
    }

    // this family's Scheme-visible surface
    void registerBind() {
        hl::bind<hlSchemeBind>("hl--c-bind");
        hl::bind<hlSchemeUnbindKey>("hl--c-unbind-key");
        hl::bind<hlSchemeUnbindRec>("hl--c-unbind-rec");
        hl::bind<hlSchemeBindEnabledSet>("hl--c-bind-enabled-set");
        hl::bind<hlSchemeBindEnabled>("hl--c-bind-enabled");
        hl::bind<hlSchemeSubmapListen>("hl--c-submap-listen");
        hl::bind<hlSchemeEventCancel>("hl--c-event-cancel");
        hl::bind<hlSchemeEventActive>("hl--c-event-active");
        hl::bind<hlSchemeCurrentSubmap>("hl--c-current-submap");
        hl::bind<enterSubmap>("hl--c-enter-submap");
        hl::bind<hlSchemeKeyRegularise>("hl--c-key-regularise");
        hl::bind<hlSchemeKeyLogicalToPhysical>("hl--c-key-logical-to-physical");
        hl::bind<hlSchemeKeyPhysicalToLogical>("hl--c-key-physical-to-logical");
    }

} // namespace Config::Scheme
