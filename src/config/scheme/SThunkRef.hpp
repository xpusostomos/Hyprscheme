#pragma once


// A counted lock on a Scheme object carried inside a C++ callback (Chez
// locks are reference-counted — gcwrapper.c: Slock_object conses the object
// onto locked_objects, Sunlock_object removes one occurrence). Construction
// and copying lock; destruction unlocks; assignment is deleted. This is how
// handlers/records/thunks live inside event listeners, timers, gestures and
// bind captures: the locked object stays alive exactly as long as any
// carrier does, and the carrier's destruction (unbind, connection teardown,
// timer completion, gesture-manager clear) releases it — no registries, no
// sweeps.
struct SThunkRef {
    ptr obj;
    SThunkRef() : obj(Snil) { Slock_object(obj); }        // nil lock: a no-op
    explicit SThunkRef(ptr o) : obj(o) { Slock_object(obj); }
    SThunkRef(const SThunkRef& o) : obj(o.obj) { Slock_object(obj); }
    ~SThunkRef() { Sunlock_object(obj); }
    // lock-new, then unlock-old (safe against self-assignment too)
    void set(ptr o) {
        Slock_object(o);
        Sunlock_object(obj);
        obj = o;
    }
    SThunkRef& operator=(const SThunkRef&) = delete;
};
