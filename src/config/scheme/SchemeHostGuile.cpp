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

    SchemeValue globalRef(const char* name) {
        return value(scm_variable_ref(scm_c_lookup(name)));
    }
    SchemeValue call0(SchemeValue fn) {
        return value(scm_call_0(val(fn)));
    }
    SchemeValue call1(SchemeValue fn, SchemeValue a1) {
        return value(scm_call_1(val(fn), val(a1)));
    }
    SchemeValue call2(SchemeValue fn, SchemeValue a1, SchemeValue a2) {
        return value(scm_call_2(val(fn), val(a1), val(a2)));
    }
    SchemeValue call3(SchemeValue fn, SchemeValue a1, SchemeValue a2, SchemeValue a3) {
        return value(scm_call_3(val(fn), val(a1), val(a2), val(a3)));
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