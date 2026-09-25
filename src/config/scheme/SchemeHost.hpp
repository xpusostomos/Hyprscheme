#pragma once

/*
    Backend-neutral Scheme host interface — step 1 of GUILE-CONVERSION.txt.

    Everything outside SchemeHostChez.cpp goes through this vocabulary; the
    file that includes Chez's <scheme.h> is exactly one. A second backend
    (Guile) re-implements these ops; the call sites do not change.

    SchemeValue is a word-sized opaque value (Chez ptr / Guile SCM both
    fit). Call sites compare values, pass them around and ask the host
    about them — they never dereference.

    THREAD RULE (unchanged from Chez): every op must run on the
    interpreter's thread — the compositor's event-loop thread that
    initialized the host.
*/

#include <cstdint>
#include <string>

class SchemeValue {
  public:
    SchemeValue() = default;
    bool operator==(const SchemeValue& o) const { return bits_ == o.bits_; }
    bool operator!=(const SchemeValue& o) const { return bits_ != o.bits_; }
    // escape hatch for host-internal code only
    uintptr_t raw() const { return bits_; }
    explicit SchemeValue(uintptr_t bits) : bits_(bits) {}

  private:
    uintptr_t bits_ = 0;
};

namespace SchemeHost {

    // ---- constants ---------------------------------------------------------
    extern const SchemeValue False;
    extern const SchemeValue True;
    extern const SchemeValue Nil;

    // ---- construction ------------------------------------------------------
    SchemeValue integer(long long v);             // exact integer
    SchemeValue flonum(double v);                 // inexact real
    SchemeValue stringVal(const char* cstr);      // NUL-terminated C string
    SchemeValue stringUtf8(const char* data, size_t len);
    SchemeValue symbol(const char* name);
    SchemeValue cons(SchemeValue car, SchemeValue cdr);

    // ---- predicates --------------------------------------------------------
    bool isPair(SchemeValue v);
    bool isString(SchemeValue v);
    bool isSymbol(SchemeValue v);
    bool isNull(SchemeValue v);
    bool isFixnum(SchemeValue v);
    bool isFlonum(SchemeValue v);

    // ---- escape hatches ------------------------------------------------------
    // the raw value word as a C++-side identity key (map keys, ids). A word
    // compare, nothing more. NOT a pointer dereference.
    uintptr_t word(SchemeValue v);
    // C-side truthiness: the raw value word is non-zero. NOT Scheme truthiness
    // (#f is a non-zero word on Chez) — use `!= False` for that.
    bool truthy(SchemeValue v);

    // ---- accessors (non-allocating) ----------------------------------------
    SchemeValue car(SchemeValue pair);
    SchemeValue cdr(SchemeValue pair);
    long long   fixnumValue(SchemeValue v);
    double      flonumValue(SchemeValue v);
    // byte-exact string contents (what the old Sstring_length/Sstring_ref
    // loops did); symbolName likewise for symbols
    std::string stringBytes(SchemeValue v);
    std::string symbolName(SchemeValue v);

    // ---- globals + calls ----------------------------------------------------
    // top-level binding of NAME in the interpreter's global environment
    SchemeValue globalRef(const char* name);
    SchemeValue call0(SchemeValue fn);
    SchemeValue call1(SchemeValue fn, SchemeValue a1);
    SchemeValue call2(SchemeValue fn, SchemeValue a1, SchemeValue a2);
    SchemeValue call3(SchemeValue fn, SchemeValue a1, SchemeValue a2, SchemeValue a3);

    // is a top-level binding present? (the loader's sanity checks)
    bool isBound(const char* name);

    // load a file of forms into the working environment. Chez: the unguarded
    // load (an error here is fatal, as before). Guile: primitive-load wrapped
    // in a catch so errors NEVER unwind through C++ frames (they corrupt the
    // heap otherwise) — returns false and the caller disables scheme.
    bool evalFile(const char* path);

    // ---- backend hook --------------------------------------------------------
    // file to load between the prelude and the defun machinery (Chez: none —
    // its primitives are native; Guile: the compatibility layer)
    const char* compatFile();

    // ---- lifecycle ----------------------------------------------------------
    // C functions become Scheme-callable primitives (Chez: Sregister_symbol;
    // later Guile: gsubr)
    void registerSymbol(const char* name, void* fn);
    void schemeInit();
    void registerBootFile(const char* path);
    void buildHeap();

    // ---- GC pinning (the counted locks SThunkRef/marshRoot ride on) ----------
    // pins stay counted: construction pins, destruction unpins
    void lock(SchemeValue v);
    void unlock(SchemeValue v);

} // namespace SchemeHost