#pragma once

/*
    The Guile runtime: startup, loading machinery files, looking up globals,
    calling Scheme from C++, and pinning objects against the collector.

    Every entry into Scheme from compositor code goes through the contained
    calls here: a Scheme error must never unwind through a C++ frame (it
    corrupts the compositor's heap), so errors are caught at this boundary,
    reported, and come back as #f.

    THREAD RULE: every op must run on the interpreter's thread — the
    compositor's event-loop thread that ran init().
*/

#include <libguile.h>

#include <string>

namespace hl {

    // start the interpreter (idempotent; the compositor calls it once)
    void init();

    // load a file of Scheme forms, contained: false when it failed
    bool evalFile(const char* path);

    // top-level binding lookup
    SCM  globalRef(const char* name);
    bool isBound(const char* name);

    // contained calls: the result, or #f when the call raised
    SCM call0(SCM fn);
    SCM call1(SCM fn, SCM a1);
    SCM call2(SCM fn, SCM a1, SCM a2);
    SCM call3(SCM fn, SCM a1, SCM a2, SCM a3);

    // GC pinning (SThunkRef rides on this): construction pins, destruction
    // unpins. Immediates need nothing.
    void lock(SCM v);
    void unlock(SCM v);

} // namespace hl
