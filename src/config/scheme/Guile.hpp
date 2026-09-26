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

    // top-level binding lookup (in the CURRENT module, which is the generation
    // while one is being built — see Host.cpp's buildGeneration)
    SCM globalRef(const char* name);
    // the same lookup, contained: #f when the name is unbound
    SCM globalRefOrFalse(const char* name);

    // a blank module to hold a generation, or #f if it could not be created
    SCM makeFreshModule();

    // load a file that DEFINES A MODULE, restoring the current module after it
    // (a `define-module` file switches it as a side effect)
    bool loadModule(const char* path);
    // resolve a module by name, or #f
    SCM  resolveModule(const char* name);
    // create a module if it does not exist, so bindings can go into it before
    // its file is read; #f on failure
    SCM  makeModule(const char* name);
    // prepend DIR to %load-path (the modules resolve each other by name)
    bool addLoadPath(const char* dir);
    // set a variable in a named module, contained
    bool setModuleVariable(SCM module, const char* name, SCM value);
    // export every local binding of a module (the kernel, after registration)
    bool exportAll(SCM module);
    // make GEN import another module's public interface
    bool useModule(SCM gen, const char* name);

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
