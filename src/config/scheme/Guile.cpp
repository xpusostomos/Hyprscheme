/*
    The Guile runtime — see Guile.hpp. This is the ONE file in the plugin that
    includes <libguile.h> for the runtime's sake; the boundary's conversion
    helpers live in Bindings.hpp.

    Values are plain SCM everywhere. Boehm's collector does not move objects,
    so an SCM held in a C++ local or a C++ container stays valid; anything
    that must survive a collection is pinned with hl::lock (SThunkRef).
*/

#include "Guile.hpp"

#include "Handles.hpp"

#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <vector>

namespace hl {

    /*
        Scheme errors must land on the compositor's fd 2.

        Wrapped ONCE for the process lifetime, and pinned. This is deliberately
        NOT part of the machinery that each config reload rebuilds: a port owns
        the fd it wraps, so a re-created stderr port closes fd 2 the moment the
        previous one is collected — silently losing every scheme error, and
        eventually taking the process with it (measured: twenty collected fd-2
        ports and it exits). One of the three things that outlive a generation.

        Doing it through the same Scheme calls the machinery used to make keeps
        the port's buffering behaviour identical.
    */
    static SCM installStderrPortThunk(void*) {
        SCM port = scm_call_2(scm_c_public_ref("guile", "fdopen"), scm_from_int(2), scm_from_locale_string("w"));
        // unbuffered: error lines must appear as they happen
        scm_call_3(scm_c_public_ref("guile", "setvbuf"), port, scm_from_locale_symbol("none"), SCM_UNDEFINED);
        scm_call_1(scm_c_public_ref("guile", "set-current-error-port"), port);
        lock(port); // the port must never be collected; see above
        return port;
    }

    static SCM installStderrPortHandler(void*, SCM tag, SCM args) {
        (void)!::write(2, "[scheme] could not install the stderr port\n", 42);
        return SCM_BOOL_F;
    }

    static void installStderrPort() {
        scm_c_catch(SCM_BOOL_T, installStderrPortThunk, NULL, installStderrPortHandler, NULL, NULL, NULL);
    }

    void init() {
        // no auto-compilation inside the compositor: deterministic startup, no
        // ~/.cache/guile writes (must be set before scm_init_guile)
        setenv("GUILE_AUTO_COMPILE", "0", 0);
        scm_init_guile();

        // handles are finalized by the machinery on the compositor's thread,
        // never by Guile's finalizer thread — see Handles.hpp
        scm_set_automatic_finalization_enabled(0);
        initHandleTypes();

        // init() runs ONCE per process (Host.cpp's `done` guard): a soft plugin
        // reload re-attaches without coming through here, which is what makes
        // this the right home for the one-per-process pieces
        installStderrPort();
    }

    // Errors must NEVER unwind through C++ frames (the compositor heap
    // corrupts), so every call and load is contained here: report on fd 2 and
    // return a falsy value. (Plugin LOG() output is swallowed by an upstream
    // logger refactor, hence the direct write.)
    static SCM callHandler(void*, SCM tag, SCM args) {
        SCM  msg = scm_simple_format(SCM_BOOL_F, scm_from_locale_string("[scheme] call error: ~a ~a\n"), scm_list_2(tag, args));
        char* s   = scm_to_locale_string(msg);
        (void)!::write(2, s, strlen(s));
        free(s);
        return SCM_BOOL_F;
    }

    static SCM loadFileThunk(void* path) {
        scm_primitive_load(scm_from_locale_string((const char*)path));
        return SCM_BOOL_T;
    }

    static SCM loadFileHandler(void*, SCM tag, SCM args) {
        SCM  msg = scm_simple_format(SCM_BOOL_F, scm_from_locale_string("load error: ~a: ~a"), scm_list_2(tag, args));
        char* s   = scm_to_locale_string(msg);
        (void)!::write(2, "[scheme] ", 9);
        (void)!::write(2, s, strlen(s));
        (void)!::write(2, "\n", 1);
        free(s);
        return SCM_BOOL_F;
    }

    bool evalFile(const char* path) {
        SCM ok = scm_c_catch(SCM_BOOL_T, loadFileThunk, (void*)path, loadFileHandler, NULL, NULL, NULL);
        return scm_is_true(ok);
    }

    bool loadModule(const char* path) {
        // A file whose first form is `define-module` needs that MACRO available
        // where the form is expanded — and it is expanded in whatever module is
        // current. So the load happens from a base module that certainly has it,
        // never from one of the machinery's own modules (which import only what
        // they were told to, and may not carry `define-module`'s expander).
        //
        // `define-module` then creates or reuses the module the file names and
        // makes it current, so a module the host pre-populated — the kernel,
        // which takes the gsubrs before its file is read — keeps those bindings.
        const SCM prev = scm_current_module();
        SCM       base = scm_c_catch(SCM_BOOL_T,
                                     [](void*) -> SCM { return scm_c_resolve_module("guile-user"); }, NULL,
                                     [](void*, SCM, SCM) -> SCM { return SCM_BOOL_F; }, NULL, NULL, NULL);
        if (!scm_is_false(base))
            scm_set_current_module(base);
        const bool ok = evalFile(path);
        scm_set_current_module(prev);
        return ok;
    }

    SCM makeModule(const char* name) {
        // create the module if it does not exist yet, so bindings can be
        // defined into it BEFORE its file is read (the file's define-module
        // then reuses it). #:ensure is what makes it create rather than fail.
        return scm_c_catch(SCM_BOOL_T,
                           [](void* d) -> SCM {
                               return scm_c_resolve_module((const char*)d);
                           }, (void*)name,
                           [](void*, SCM, SCM) -> SCM { return SCM_BOOL_F; }, NULL, NULL, NULL);
    }

    SCM resolveModule(const char* name) {
        return scm_c_catch(SCM_BOOL_T,
                           [](void* d) -> SCM { return scm_c_resolve_module((const char*)d); }, (void*)name,
                           [](void*, SCM, SCM) -> SCM { return SCM_BOOL_F; }, NULL, NULL, NULL);
    }

    /*
        A blank module for a generation: the machinery a config runs against is
        rebuilt into one on every reload, so nothing it defines can be reached
        once it is replaced. Contained like every other call — a failure must
        not unwind through C++.
    */
    static SCM freshModuleThunk(void*) {
        return scm_call_0(scm_c_public_ref("guile", "make-fresh-user-module"));
    }

    static SCM freshModuleHandler(void*, SCM, SCM) {
        return SCM_BOOL_F;
    }

    SCM makeFreshModule() {
        return scm_c_catch(SCM_BOOL_T, freshModuleThunk, NULL, freshModuleHandler, NULL, NULL, NULL);
    }

    static SCM addLoadPathThunk(void* d) {
        SCM var = scm_c_lookup("%load-path");
        scm_variable_set_x(var, scm_cons(scm_from_utf8_string((const char*)d), scm_variable_ref(var)));
        return SCM_BOOL_T;
    }

    bool addLoadPath(const char* dir) {
        // contained: an exception escaping into C++ corrupts the compositor
        // heap (observed — an uncaught Scheme error here took the process out
        // with "corrupted size vs. prev_size")
        return scm_is_true(scm_c_catch(SCM_BOOL_T, addLoadPathThunk, (void*)dir, callHandler, NULL, NULL, NULL));
    }

    static SCM lookupThunk(void* d) {
        return scm_variable_ref(scm_c_lookup((const char*)d));
    }

    SCM globalRefOrFalse(const char* name) {
        // contained: an unbound lookup raises, and an exception escaping into
        // C++ corrupts the compositor heap (observed)
        return scm_c_catch(SCM_BOOL_T, lookupThunk, (void*)name,
                           [](void*, SCM, SCM) -> SCM { return SCM_BOOL_F; }, NULL, NULL, NULL);
    }

    struct SetVar {
        SCM         module;
        const char* name;
        SCM         value;
    };

    static SCM setVarThunk(void* d) {
        auto* a = (SetVar*)d;
        scm_variable_set_x(scm_module_variable(a->module, scm_from_utf8_symbol(a->name)), a->value);
        return SCM_BOOL_T;
    }

    bool setModuleVariable(SCM module, const char* name, SCM value) {
        SetVar a{module, name, value};
        return scm_is_true(scm_c_catch(SCM_BOOL_T, setVarThunk, &a, callHandler, NULL, NULL, NULL));
    }

    SCM globalRef(const char* name) {
        // scm_c_lookup resolves in the CURRENT module, and a module's lookup
        // DOES consult its imports — so this finds the API's and the kernel's
        // names from the generation, which imports both
        return scm_variable_ref(scm_c_lookup(name));
    }

    // one small applier for the module plumbing below: (proc arg) / (proc a b),
    // where proc is resolved from (guile) at call time
    struct CallArgs {
        SCM fn, a1, a2, a3;
    };

    static SCM callThunk0(void* d) { return scm_call_0(((CallArgs*)d)->fn); }
    static SCM callThunk1(void* d) { return scm_call_1(((CallArgs*)d)->fn, ((CallArgs*)d)->a1); }
    static SCM callThunk2(void* d) { return scm_call_2(((CallArgs*)d)->fn, ((CallArgs*)d)->a1, ((CallArgs*)d)->a2); }
    static SCM callThunk3(void* d) { return scm_call_3(((CallArgs*)d)->fn, ((CallArgs*)d)->a1, ((CallArgs*)d)->a2, ((CallArgs*)d)->a3); }

    SCM call0(SCM fn) {
        CallArgs a{fn, SCM_UNDEFINED, SCM_UNDEFINED, SCM_UNDEFINED};
        return scm_c_catch(SCM_BOOL_T, callThunk0, &a, callHandler, NULL, NULL, NULL);
    }
    SCM call1(SCM fn, SCM a1) {
        CallArgs a{fn, a1, SCM_UNDEFINED, SCM_UNDEFINED};
        return scm_c_catch(SCM_BOOL_T, callThunk1, &a, callHandler, NULL, NULL, NULL);
    }
    SCM call2(SCM fn, SCM a1, SCM a2) {
        CallArgs a{fn, a1, a2, SCM_UNDEFINED};
        return scm_c_catch(SCM_BOOL_T, callThunk2, &a, callHandler, NULL, NULL, NULL);
    }
    SCM call3(SCM fn, SCM a1, SCM a2, SCM a3) {
        CallArgs a{fn, a1, a2, a3};
        return scm_c_catch(SCM_BOOL_T, callThunk3, &a, callHandler, NULL, NULL, NULL);
    }

    struct ModuleCall {
        const char* proc;
        SCM         a, b;
    };

    static SCM moduleCall1Thunk(void* d) {
        auto* m = (ModuleCall*)d;
        return scm_call_1(scm_c_public_ref("guile", m->proc), m->a);
    }

    static SCM moduleCall2Thunk(void* d) {
        auto* m = (ModuleCall*)d;
        return scm_call_2(scm_c_public_ref("guile", m->proc), m->a, m->b);
    }

    bool exportAll(SCM module) {
        ModuleCall m{"module-export-all!", module, SCM_UNDEFINED};
        return scm_is_true(scm_c_catch(SCM_BOOL_T, moduleCall1Thunk, &m, callHandler, NULL, NULL, NULL));
    }

    bool useModule(SCM gen, const char* name) {
        // (module-use! gen (resolve-interface '(hyprscheme api)))
        SCM parts = SCM_EOL, cur = SCM_EOL;
        std::string tok;
        std::istringstream in(name);
        std::vector<std::string> toks;
        while (in >> tok)
            toks.push_back(tok);
        for (auto it = toks.rbegin(); it != toks.rend(); ++it)
            parts = scm_cons(scm_from_utf8_symbol(it->c_str()), parts);

        ModuleCall r{"resolve-interface", parts, SCM_UNDEFINED};
        SCM        iface = scm_c_catch(SCM_BOOL_T, moduleCall1Thunk, &r, callHandler, NULL, NULL, NULL);
        if (scm_is_false(iface))
            return false;

        ModuleCall u{"module-use!", gen, iface};
        return scm_is_true(scm_c_catch(SCM_BOOL_T, moduleCall2Thunk, &u, callHandler, NULL, NULL, NULL));
    }

    void lock(SCM v) {
        // immediates are not heap objects: nothing to protect
        if (scm_is_eq(v, SCM_EOL) || scm_is_eq(v, SCM_BOOL_F) || scm_is_eq(v, SCM_BOOL_T))
            return;
        scm_gc_protect_object(v);
    }

    void unlock(SCM v) {
        if (scm_is_eq(v, SCM_EOL) || scm_is_eq(v, SCM_BOOL_F) || scm_is_eq(v, SCM_BOOL_T))
            return;
        scm_gc_unprotect_object(v);
    }

} // namespace hl
