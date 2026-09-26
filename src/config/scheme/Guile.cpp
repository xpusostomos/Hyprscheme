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

namespace hl {

    void init() {
        // no auto-compilation inside the compositor: deterministic startup, no
        // ~/.cache/guile writes (must be set before scm_init_guile)
        setenv("GUILE_AUTO_COMPILE", "0", 0);
        scm_init_guile();

        // handles are finalized by the machinery on the compositor's thread,
        // never by Guile's finalizer thread — see Handles.hpp
        scm_set_automatic_finalization_enabled(0);
        initHandleTypes();
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

    SCM globalRef(const char* name) {
        return scm_variable_ref(scm_c_lookup(name));
    }

    bool isBound(const char* name) {
        // a locally-bound variable (not an imported one): the machinery is
        // defined in the working module, imports don't count
        return !scm_is_eq(scm_module_local_variable(scm_current_module(), scm_from_locale_symbol(name)), SCM_BOOL_F);
    }

    struct CallArgs {
        SCM fn, a1, a2, a3;
    };

    static SCM callThunk0(void* d) { return scm_call_0(((CallArgs*)d)->fn); }
    static SCM callThunk1(void* d) { return scm_call_1(((CallArgs*)d)->fn, ((CallArgs*)d)->a1); }
    static SCM callThunk2(void* d) { return scm_call_2(((CallArgs*)d)->fn, ((CallArgs*)d)->a1, ((CallArgs*)d)->a2); }
    static SCM callThunk3(void* d) { return scm_call_3(((CallArgs*)d)->fn, ((CallArgs*)d)->a1, ((CallArgs*)d)->a2, ((CallArgs*)d)->a3); }

    SCM call0(SCM fn) {
        CallArgs a{fn, SCM_UNDEFINED, SCM_UNDEFINED, SCM_UNDEFINED};
        SCM      r = scm_c_catch(SCM_BOOL_T, callThunk0, &a, callHandler, NULL, NULL, NULL);
        return r;
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
