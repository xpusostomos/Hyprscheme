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
        // GUILE_AUTO_COMPILE is deliberately NOT set here, and must not be.
        //
        // It used to be `setenv("GUILE_AUTO_COMPILE", "0", 0)` — to keep the
        // compositor's startup deterministic and stop the plugin writing to
        // ~/.cache. But setenv mutates the LIVE process environment, and the
        // compositor is the ancestor of everything the user launches: every
        // process started after the plugin loaded inherited it. That is not a
        // subtle leak — it turned auto-compilation off for every Guile program
        // on the machine, for the whole session, including ones that have
        // nothing to do with Hyprscheme. Found via `GUILE_AUTO_COMPILE=0` in a
        // terminal, in a shell that had never been told any such thing.
        //
        // If auto-compilation ever needs suppressing for OUR interpreter alone,
        // set the Scheme variable `%load-should-auto-compile` to #f — that is
        // the variable the environment variable is read into at init, and it is
        // what `--no-auto-compile` sets. It is scoped to this interpreter and
        // touches no global state.
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
    // corrupts), so every call is contained here: report on fd 2 and return a
    // falsy value. (Plugin LOG() output is swallowed by an upstream logger
    // refactor, hence the direct write.)
    static SCM callHandler(void*, SCM tag, SCM args) {
        SCM  msg = scm_simple_format(SCM_BOOL_F, scm_from_locale_string("[scheme] call error: ~a ~a\n"), scm_list_2(tag, args));
        char* s   = scm_to_locale_string(msg);
        (void)!::write(2, s, strlen(s));
        free(s);
        return SCM_BOOL_F;
    }

    SCM makeModule(const char* name) {
        // Make sure a module exists under NAME so bindings can be defined into
        // it before its file is read (the file's `define-module` then reuses
        // it), and return it.
        //
        // TWO things about scm_c_resolve_module worth knowing, both verified:
        //
        //   - it CREATES on a miss rather than failing, which is what makes it
        //     usable here at all (this is not `#:ensure`: that is a Scheme-level
        //     keyword and this is the C entry point, which has no such argument
        //     — an older comment here claimed otherwise);
        //   - it AUTOLOADS. If NAME is findable on %load-path the file is
        //     READ, right here. So `makeModule("hyprscheme kernel")` in
        //     buildGeneration loads kernel.scm BEFORE registerAllBindings()
        //     runs. That works because `#:export` makes the gsubr names
        //     unassigned placeholders when the module is defined, so the file
        //     reads and registration fills them in — but it means the kernel's
        //     TOP-LEVEL forms run with the gsubrs unbound. See Host.cpp.
        return scm_c_catch(SCM_BOOL_T,
                           [](void* d) -> SCM {
                               return scm_c_resolve_module((const char*)d);
                           }, (void*)name,
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

    // which path variable to prepend to, and what to prepend. A struct rather
    // than a capture because the thunk is a plain C function pointer.
    struct PathSpec {
        const char* var;
        const char* dir;
    };

    static SCM addPathThunk(void* d) {
        // the struct carries which variable to prepend to; see addLoadPath
        const auto* spec = (const PathSpec*)d;
        SCM         var  = scm_c_lookup(spec->var);
        scm_variable_set_x(var, scm_cons(scm_from_utf8_string(spec->dir), scm_variable_ref(var)));
        return SCM_BOOL_T;
    }

    bool addLoadPath(const char* dir) {
        // contained: an exception escaping into C++ corrupts the compositor
        // heap (observed — an uncaught Scheme error here took the process out
        // with "corrupted size vs. prev_size")
        const PathSpec spec{"%load-path", dir};
        return scm_is_true(scm_c_catch(SCM_BOOL_T, addPathThunk, (void*)&spec, callHandler, NULL, NULL, NULL));
    }

    bool addLoadCompiledPath(const char* dir) {
        // %load-compiled-path is where `use-modules` looks for a .go, and it is
        // a SEPARATE list from %load-path — a directory on one is not searched
        // on the other. `guild` has no -C, so setting the variable is the way
        // in. Shipped machinery puts the .go beside each .scm, so this is
        // normally the SAME directory as addLoadPath's.
        const PathSpec spec{"%load-compiled-path", dir};
        return scm_is_true(scm_c_catch(SCM_BOOL_T, addPathThunk, (void*)&spec, callHandler, NULL, NULL, NULL));
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
        SCM fn, a1, a2, a3, a4;
    };

    static SCM callThunk0(void* d) { return scm_call_0(((CallArgs*)d)->fn); }
    static SCM callThunk1(void* d) { return scm_call_1(((CallArgs*)d)->fn, ((CallArgs*)d)->a1); }
    static SCM callThunk2(void* d) { return scm_call_2(((CallArgs*)d)->fn, ((CallArgs*)d)->a1, ((CallArgs*)d)->a2); }
    static SCM callThunk3(void* d) { return scm_call_3(((CallArgs*)d)->fn, ((CallArgs*)d)->a1, ((CallArgs*)d)->a2, ((CallArgs*)d)->a3); }
    static SCM callThunk4(void* d) { return scm_call_4(((CallArgs*)d)->fn, ((CallArgs*)d)->a1, ((CallArgs*)d)->a2, ((CallArgs*)d)->a3, ((CallArgs*)d)->a4); }

    SCM call0(SCM fn) {
        CallArgs a{fn, SCM_UNDEFINED, SCM_UNDEFINED, SCM_UNDEFINED, SCM_UNDEFINED};
        return scm_c_catch(SCM_BOOL_T, callThunk0, &a, callHandler, NULL, NULL, NULL);
    }
    SCM call1(SCM fn, SCM a1) {
        CallArgs a{fn, a1, SCM_UNDEFINED, SCM_UNDEFINED, SCM_UNDEFINED};
        return scm_c_catch(SCM_BOOL_T, callThunk1, &a, callHandler, NULL, NULL, NULL);
    }
    SCM call2(SCM fn, SCM a1, SCM a2) {
        CallArgs a{fn, a1, a2, SCM_UNDEFINED, SCM_UNDEFINED};
        return scm_c_catch(SCM_BOOL_T, callThunk2, &a, callHandler, NULL, NULL, NULL);
    }
    SCM call3(SCM fn, SCM a1, SCM a2, SCM a3) {
        CallArgs a{fn, a1, a2, a3, SCM_UNDEFINED};
        return scm_c_catch(SCM_BOOL_T, callThunk3, &a, callHandler, NULL, NULL, NULL);
    }
    SCM call4(SCM fn, SCM a1, SCM a2, SCM a3, SCM a4) {
        CallArgs a{fn, a1, a2, a3, a4};
        return scm_c_catch(SCM_BOOL_T, callThunk4, &a, callHandler, NULL, NULL, NULL);
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
