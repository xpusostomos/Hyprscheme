/*
    The Chez implementation of SchemeHost.hpp — the ONE file in the
    plugin that includes <scheme.h>. The Guile backend will be a sibling
    (SchemeHostGuile.cpp) implementing the same ops.

    Threading: every op must run on the thread that ran schemeInit() —
    the compositor's event loop thread.
*/

#include "SchemeHost.hpp"

#include <scheme.h>

namespace SchemeHost {

    const SchemeValue False((uintptr_t)Sfalse);
    const SchemeValue True((uintptr_t)Strue);
    const SchemeValue Nil((uintptr_t)Snil);

    static inline ptr val(SchemeValue v) {
        return (ptr)v.raw();
    }
    static inline SchemeValue value(ptr p) {
        return SchemeValue((uintptr_t)p);
    }

    SchemeValue integer(long long v) {
        return value(Sinteger(v));
    }
    SchemeValue flonum(double v) {
        return value(Sflonum(v));
    }
    SchemeValue stringVal(const char* cstr) {
        return value(Sstring(cstr));
    }
    SchemeValue stringUtf8(const char* data, size_t len) {
        return value(Sstring_utf8(data, len));
    }
    SchemeValue symbol(const char* name) {
        return value(Sstring_to_symbol(name));
    }
    SchemeValue cons(SchemeValue car_, SchemeValue cdr_) {
        return value(Scons(val(car_), val(cdr_)));
    }

    bool isPair(SchemeValue v) {
        return Spairp(val(v));
    }
    bool isString(SchemeValue v) {
        return Sstringp(val(v));
    }
    bool isSymbol(SchemeValue v) {
        return Ssymbolp(val(v));
    }
    bool isNull(SchemeValue v) {
        return Snullp(val(v));
    }
    bool isFixnum(SchemeValue v) {
        return Sfixnump(val(v));
    }
    bool isFlonum(SchemeValue v) {
        return Sflonump(val(v));
    }

    uintptr_t word(SchemeValue v) {
        return v.raw();
    }
    bool truthy(SchemeValue v) {
        return v.raw() != 0;
    }
    SchemeValue car(SchemeValue pair) {
        return value(Scar(val(pair)));
    }
    SchemeValue cdr(SchemeValue pair) {
        return value(Scdr(val(pair)));
    }
    long long fixnumValue(SchemeValue v) {
        return (long long)Sfixnum_value(val(v));
    }
    double flonumValue(SchemeValue v) {
        return Sflonum_value(val(v));
    }
    std::string stringBytes(SchemeValue v) {
        std::string out;
        ptr s = val(v);
        iptr n = Sstring_length(s);
        out.reserve((size_t)n);
        for (iptr i = 0; i < n; ++i)
            out += (char)Sstring_ref(s, i);
        return out;
    }
    std::string symbolName(SchemeValue v) {
        ptr s = Ssymbol_to_string(val(v));
        iptr n = Sstring_length(s);
        std::string out;
        out.reserve((size_t)n);
        for (iptr i = 0; i < n; ++i)
            out += (char)Sstring_ref(s, i);
        return out;
    }

    SchemeValue globalRef(const char* name) {
        return value(Stop_level_value(Sstring_to_symbol(name)));
    }
    SchemeValue call0(SchemeValue fn) {
        return value(Scall0(val(fn)));
    }
    SchemeValue call1(SchemeValue fn, SchemeValue a1) {
        return value(Scall1(val(fn), val(a1)));
    }
    SchemeValue call2(SchemeValue fn, SchemeValue a1, SchemeValue a2) {
        return value(Scall2(val(fn), val(a1), val(a2)));
    }
    SchemeValue call3(SchemeValue fn, SchemeValue a1, SchemeValue a2, SchemeValue a3) {
        return value(Scall3(val(fn), val(a1), val(a2), val(a3)));
    }

    bool isBound(const char* name) {
        return call1(globalRef("top-level-bound?"), symbol(name)) == True;
    }
    bool evalFile(const char* path) {
        call1(globalRef("load"), stringVal(path));
        return true; // errors propagate unguarded, as they always have
    }
    const char* compatFile() {
        return nullptr; // Chez primitives are native; no compatibility layer
    }
    void registerSymbol(const char* name, void* fn) {
        Sregister_symbol(name, fn);
    }
    void schemeInit() {
        Sscheme_init(nullptr);
    }
    void registerBootFile(const char* path) {
        Sregister_boot_file(path);
    }
    void buildHeap() {
        Sbuild_heap(nullptr, nullptr);
    }

    void lock(SchemeValue v) {
        Slock_object(val(v));
    }
    void unlock(SchemeValue v) {
        Sunlock_object(val(v));
    }

} // namespace SchemeHost