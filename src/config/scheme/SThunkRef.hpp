#pragma once

#include "SchemeHost.hpp"

// A counted lock on a Scheme object carried inside a C++ callback (Chez
// locks are reference-counted — gcwrapper.c: Slock_object conses the object
// onto locked_objects, Sunlock_object removes one occurrence). Construction
// and copying lock; destruction unlocks; assignment is deleted. This is how
// handlers/records/thunks live inside event listeners, timers, gestures and
// bind captures: the locked object stays alive exactly as long as any
// carrier does, and the carrier's destruction (unbind, connection teardown,
// timer completion, gesture-manager clear) releases it — no registries, no
// sweeps. (Guile backend: the same shape over scm_gc_protect_object.)
struct SThunkRef {
    SchemeValue obj;
    SThunkRef() : obj(SchemeHost::Nil) { SchemeHost::lock(obj); } // nil lock: a no-op
    explicit SThunkRef(SchemeValue o) : obj(o) { SchemeHost::lock(obj); }
    SThunkRef(const SThunkRef& o) : obj(o.obj) { SchemeHost::lock(obj); }
    ~SThunkRef() { SchemeHost::unlock(obj); }
    // lock-new, then unlock-old (safe against self-assignment too)
    void set(SchemeValue o) {
        SchemeHost::lock(o);
        SchemeHost::unlock(obj);
        obj = o;
    }
    SThunkRef& operator=(const SThunkRef&) = delete;
};