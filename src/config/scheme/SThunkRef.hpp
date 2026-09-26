#pragma once

#include "Guile.hpp"
#include "Bindings.hpp"

// A counted lock on a Scheme value carried inside a C++ callback (Guile's
// scm_gc_protect_object is reference-counted; unprotect removes one
// occurrence). Construction and copying lock; destruction unlocks;
// assignment is deleted. This is how
// handlers/records/thunks live inside event listeners, timers, gestures and
// bind captures: the locked object stays alive exactly as long as any
// carrier does, and the carrier's destruction (unbind, connection teardown,
// timer completion, gesture-manager clear) releases it — no registries, no
// sweeps. (Guile backend: the same shape over scm_gc_protect_object.)
struct SThunkRef {
    SCM obj;
    SThunkRef() : obj(SCM_EOL) { hl::lock(obj); } // nil lock: a no-op
    explicit SThunkRef(SCM o) : obj(o) { hl::lock(obj); }
    SThunkRef(const SThunkRef& o) : obj(o.obj) { hl::lock(obj); }
    ~SThunkRef() { hl::unlock(obj); }
    // lock-new, then unlock-old (safe against self-assignment too)
    void set(SCM o) {
        hl::lock(o);
        hl::unlock(obj);
        obj = o;
    }
    SThunkRef& operator=(const SThunkRef&) = delete;
};