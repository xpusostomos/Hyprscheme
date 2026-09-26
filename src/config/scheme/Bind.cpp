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



namespace Config::Scheme {

    using namespace Internals;

    static Keybinds::SBindResult fireSchemeBindRec(SCM record); // defined below

    // submap registration context: binds created while set are scoped to it
    static std::string                g_regSubmap;

    static std::string                g_regSubmapReset;

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

    // called from Scheme as a gsubr; flags are raw eBindFlags bits,
    // assembled Scheme-side from the options plist
    static int hlSchemeBind(SCM record, SCM tokens, int flags, const char* desc, SCM devices) {
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
        for (SCM p = devices; scm_is_pair(p) && p != SCM_EOL; p = scm_cdr(p)) {
            if (!scm_is_string(scm_car(p))) {
                g_configError = "hl-bind-add!: 'devices must be a list of device name strings";
                return -1;
            }
            args.devices.emplace(schemeDatumToStr(scm_car(p)));
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

    // called from Scheme as a gsubr: remove one scheme bind
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
    static int hlSchemeUnbindRec(SCM record) {
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

    static SCM submapCtx() {
        std::vector<SCM> roots;
        SCM              name = scm_from_utf8_stringn(g_regSubmap.c_str(), g_regSubmap.size());
        hl::pin(name, roots);
        SCM reset = scm_from_utf8_stringn(g_regSubmapReset.c_str(), g_regSubmapReset.size());
        hl::pin(reset, roots);
        SCM pair = scm_cons(name, reset);
        hl::unpin(roots);   // the cons cell holds both, and is returned
        return pair;        // (name . reset)
    }

    static void setSubmapCtx(const char* name, const char* reset) {
        g_regSubmap      = name ? name : "";
        g_regSubmapReset = reset ? reset : "";
    }

    static int enterSubmap(const char* name) {
        if (!g_up || !name)
            return -1;
        return Config::Actions::setSubmap(name) ? 0 : -1;
    }

    // this family's Scheme-visible surface
    void registerBind() {
        hl::bind<hlSchemeBind>("hl--c-bind");
        hl::bind<hlSchemeUnbindKey>("hl--c-unbind-key");
        hl::bind<hlSchemeUnbindRec>("hl--c-unbind-rec");
        hl::bind<hlSchemeSubmapListen>("hl--c-submap-listen");
        hl::bind<hlSchemeEventCancel>("hl--c-event-cancel");
        hl::bind<hlSchemeEventActive>("hl--c-event-active");
        hl::bind<hlSchemeCurrentSubmap>("hl--c-current-submap");
        hl::bind<submapCtx>("hl--c-get-submap-ctx");
        hl::bind<setSubmapCtx>("hl--c-set-submap-ctx");
        hl::bind<enterSubmap>("hl--c-enter-submap");
    }

} // namespace Config::Scheme
