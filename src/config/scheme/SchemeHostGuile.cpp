/*
    The Guile implementation of SchemeHost.hpp — the ONE file in the
    plugin that includes <libguile.h>. A sibling of SchemeHostChez.cpp
    (same ops, same call sites; see GUILE-CONVERSION.txt).

    Value model: SchemeValue is the raw SCM word (SCM_UNPACK/SCM_PACK
    round-trip). Registered C functions keep their word-typed signatures;
    the Scheme side crosses values with the two marshalling primitives
    defined in schemeInit() (hl--scm->word / hl--word->scm) — the FFI
    carries them as uintptr_t. Boehm's GC does not move objects, so a
    stored word stays valid; heap-held values are pinned via
    scm_gc_protect_object (the lock() op).

    Threading: every op must run on the thread that ran schemeInit() —
    the compositor's event-loop thread (same rule as the Chez backend).
*/

#include "SchemeHost.hpp"

#include <libguile.h>
#include <cstring>
#include <cstdlib>

namespace SchemeHost {

    // immediates on Guile are tagged words — nonzero (the truthy() op below
    // keeps the Chez contract: a raw word != 0)
    const SchemeValue False((uintptr_t)SCM_UNPACK(SCM_BOOL_F));
    const SchemeValue True((uintptr_t)SCM_UNPACK(SCM_BOOL_T));
    const SchemeValue Nil((uintptr_t)SCM_UNPACK(SCM_EOL));

    static inline SCM val(SchemeValue v) {
        return SCM_PACK((scm_t_bits)v.raw());
    }
    static inline SchemeValue value(SCM s) {
        return SchemeValue((uintptr_t)SCM_UNPACK(s));
    }

    // the two marshalling primitives the Scheme-side foreign-procedure
    // compatibility layer rides on
    static SCM scmToWord(SCM v) {
        return scm_from_uintptr_t((uintptr_t)SCM_UNPACK(v));
    }
    static SCM wordToScm(SCM w) {
        return SCM_PACK((scm_t_bits)scm_to_uintptr_t(w));
    }

    SchemeValue integer(long long v) {
        return value(scm_from_int64(v));
    }
    SchemeValue flonum(double v) {
        return value(scm_from_double(v));
    }
    SchemeValue stringVal(const char* cstr) {
        return value(scm_from_locale_string(cstr));
    }
    SchemeValue stringUtf8(const char* data, size_t len) {
        return value(scm_from_utf8_stringn(data, len));
    }
    SchemeValue symbol(const char* name) {
        return value(scm_from_locale_symbol(name));
    }
    SchemeValue cons(SchemeValue car_, SchemeValue cdr_) {
        return value(scm_cons(val(car_), val(cdr_)));
    }

    bool isPair(SchemeValue v) {
        return scm_is_true(scm_pair_p(val(v)));
    }
    bool isString(SchemeValue v) {
        return scm_is_string(val(v));
    }
    bool isSymbol(SchemeValue v) {
        return scm_is_true(scm_symbol_p(val(v)));
    }
    bool isNull(SchemeValue v) {
        return scm_is_null(val(v));
    }
    // C++ distinguishes "exact integer" (ids) from inexact reals; Guile's
    // nearest equivalent is exact-integer vs the rest
    bool isFixnum(SchemeValue v) {
        return scm_is_exact_integer(val(v));
    }
    bool isFlonum(SchemeValue v) {
        return scm_is_real(val(v)) && !scm_is_exact_integer(val(v));
    }

    uintptr_t word(SchemeValue v) {
        return v.raw();
    }
    bool truthy(SchemeValue v) {
        return v.raw() != 0;
    }
    SchemeValue car(SchemeValue pair) {
        return value(scm_car(val(pair)));
    }
    SchemeValue cdr(SchemeValue pair) {
        return value(scm_cdr(val(pair)));
    }
    long long fixnumValue(SchemeValue v) {
        return (long long)scm_to_int64(val(v));
    }
    double flonumValue(SchemeValue v) {
        return scm_to_double(val(v));
    }
    std::string stringBytes(SchemeValue v) {
        size_t   len = 0;
        char*    buf = scm_to_utf8_stringn(val(v), &len);
        std::string out(buf, len);
        free(buf);
        return out;
    }
    std::string symbolName(SchemeValue v) {
        size_t   len = 0;
        char*    buf = scm_to_utf8_stringn(scm_symbol_to_string(val(v)), &len);
        std::string out(buf, len);
        free(buf);
        return out;
    }


    // errors must NEVER unwind through C++ frames (the compositor heap
    // corrupts); contain them at the host and print via fd 2 — the plugin's
    // LOG() is currently swallowed (upstream logger refactor). A contained
    // error yields False (C++ callers already treat False as decline).
    static SCM callHandler(void*, SCM tag, SCM args) {
        SCM msg = scm_simple_format(SCM_BOOL_F, scm_from_locale_string("[scheme-guile] call error: ~a ~a\n"),
                                    scm_list_2(tag, args));
        char*    s = scm_to_locale_string(msg);
        (void)!::write(2, s, strlen(s));
        free(s);
        return SCM_BOOL_F;
    }

    SchemeValue globalRef(const char* name) {
        return value(scm_variable_ref(scm_c_lookup(name)));
    }
    struct CallArgs { SCM fn; SCM a1; SCM a2; SCM a3; };
    static SCM callThunk0(void* d) { return scm_call_0(((CallArgs*)d)->fn); }
    static SCM callThunk1(void* d) { auto* a = (CallArgs*)d; return scm_call_1(a->fn, a->a1); }
    static SCM callThunk2(void* d) { auto* a = (CallArgs*)d; return scm_call_2(a->fn, a->a1, a->a2); }
    static SCM callThunk3(void* d) { auto* a = (CallArgs*)d; return scm_call_3(a->fn, a->a1, a->a2, a->a3); }
    SchemeValue call0(SchemeValue fn) {
        CallArgs a{val(fn), SCM_UNDEFINED, SCM_UNDEFINED, SCM_UNDEFINED};
        SCM r = scm_c_catch(SCM_BOOL_T, callThunk0, &a, callHandler, NULL, NULL, NULL);
        return scm_is_true(r) ? value(r) : False;
    }
    SchemeValue call1(SchemeValue fn, SchemeValue a1) {
        CallArgs c{val(fn), val(a1), SCM_UNDEFINED, SCM_UNDEFINED};
        SCM r = scm_c_catch(SCM_BOOL_T, callThunk1, &c, callHandler, NULL, NULL, NULL);
        return scm_is_true(r) ? value(r) : False;
    }
    SchemeValue call2(SchemeValue fn, SchemeValue a1, SchemeValue a2) {
        CallArgs c{val(fn), val(a1), val(a2), SCM_UNDEFINED};
        SCM r = scm_c_catch(SCM_BOOL_T, callThunk2, &c, callHandler, NULL, NULL, NULL);
        return scm_is_true(r) ? value(r) : False;
    }
    SchemeValue call3(SchemeValue fn, SchemeValue a1, SchemeValue a2, SchemeValue a3) {
        CallArgs c{val(fn), val(a1), val(a2), val(a3)};
        SCM r = scm_c_catch(SCM_BOOL_T, callThunk3, &c, callHandler, NULL, NULL, NULL);
        return scm_is_true(r) ? value(r) : False;
    }

    bool isBound(const char* name) {
        // a locally-bound variable (not an imported one): the machinery is
        // defined in the working module, imports don't count
        return scm_module_local_variable(scm_current_module(),
                                         scm_from_locale_symbol(name)) != SCM_BOOL_F;
    }
    // primitive-load under a catch: a Scheme error must never unwind through
    // C++ frames (the compositor heap corrupts). The prelude is loaded with
    // this too — its "unguarded" contract becomes "contained at the host".
    static SCM loadFileThunk(void* path_) {
        scm_primitive_load(scm_from_locale_string((const char*)path_));
        return SCM_BOOL_T;
    }
    static SCM loadFileHandler(void*, SCM tag, SCM args) {
        SCM msg = scm_simple_format(SCM_BOOL_F, scm_from_locale_string("load error: ~a: ~a"),
                                    scm_list_2(tag, args));
        char*    s   = scm_to_locale_string(msg);
        (void)!::write(2, "[gc-crumb] ", 11);
        (void)!::write(2, s, strlen(s));
        (void)!::write(2, "\n", 1);
        free(s);
        return SCM_BOOL_F;
    }
    bool evalFile(const char* path) {
        SCM ok = scm_c_catch(SCM_BOOL_T, loadFileThunk, (void*)path,
                             loadFileHandler, NULL, NULL, NULL);
        return scm_is_true(ok);
    }
    const char* backendName() {
        return "guile";
    }
    const char* compatFile() {
        return "hyprscheme-compat-guile.scm";
    }

    void registerSymbol(const char* name, void* fn) {
        // typed C function pointer, exposed to Scheme as a foreign pointer;
        // the compat layer's foreign-procedure shim resolves it by name and
        // attaches the type conversions (declaratively — no shims per fn)
        scm_c_define(name, scm_from_pointer(fn, NULL));
    }
    void schemeInit() {
        // no auto-compilation inside the compositor: deterministic startup,
        // no ~/.cache/guile writes (must be set before scm_init_guile)
        setenv("GUILE_AUTO_COMPILE", "0", 0);
        scm_init_guile();
        // marshalling primitives for the Scheme-side FFI shim
        scm_c_define_gsubr("hl--scm->word", 1, 0, 0, (scm_t_subr)scmToWord);
        scm_c_define_gsubr("hl--word->scm", 1, 0, 0, (scm_t_subr)wordToScm);
    }
    void registerBootFile(const char*) {
        // Guile has no boot files: the interpreter and its modules come from
        // the system libguile
    }
    void buildHeap() {
        // no heap to build; the interpreter is ready after scm_init_guile
    }

    void lock(SchemeValue v) {
        // immediates (the constants) need no protection — matches Chez,
        // where locking an immediate is a no-op
        if (v == Nil || v == False || v == True)
            return;
        scm_gc_protect_object(val(v));
    }
    void unlock(SchemeValue v) {
        if (v == Nil || v == False || v == True)
            return;
        scm_gc_unprotect_object(val(v));
    }

} // namespace SchemeHost