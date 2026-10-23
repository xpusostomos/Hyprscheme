#pragma once

/*
    Handles: how compositor objects are represented in Scheme.

    A handle is a Guile FOREIGN OBJECT whose single slot holds a heap weak ref
    to the compositor object, plus a finalizer that deletes it when the Scheme
    object becomes unreachable. So:

      - the object IS the handle — no address ever reaches Scheme, and there is
        no id to wrap, pass or forge;
      - the six families have six distinct types, so a handle of the wrong
        family is a type error at the boundary rather than a reinterpreted
        pointer;
      - lifetime is Guile's own: the finalizer replaces the guardian cell, the
        GC-hook drain and the handle-free entry point the machinery used to run
        by hand.

    Finalizers must NOT run on Guile's finalizer thread: deleting a handle
    releases a weak reference, which unregisters an observer from the object's
    control block — compositor state, only safe on the compositor's thread. So
    automatic finalization is switched off (Guile.cpp init) and the machinery
    pumps runFinalizers() from the event loop (the after-gc-hook).

    Creating the weak ref must happen on the compositor thread too, which is
    where every entry point runs (the thread rule in Bindings.hpp).
*/

#include <libguile.h>

#include <src/desktop/DesktopTypes.hpp>
#include <src/desktop/view/Group.hpp>
#include <src/desktop/view/LayerSurface.hpp>
#include <src/notification/Notification.hpp>

namespace hl {

    // the six handle families
    enum class Family {
        Group,
        Layer,
        Monitor,
        Notification,
        Window,
        Workspace,
    };

    // the C++ object behind a handle: one heap weak ref
    struct IHandle {
        virtual ~IHandle() = default;
    };
    template <typename W>
    struct SHandle : IHandle {
        W wp;
        SHandle() = default;
        template <typename T>
        explicit SHandle(T o) : wp(o) {}
    };

    // a notification handle also carries its paused bit (the compositor's
    // notification object has no paused state of its own)
    struct SNotificationHandle : IHandle {
        WP<Notification::CNotification> wp;
        bool                            paused = false;
        explicit SNotificationHandle(SP<Notification::CNotification> n) : wp(n) {}
        ~SNotificationHandle() override {
            if (paused)
                if (auto n = wp.lock())
                    n->unlock();
        }
    };

    // ---- the registry (Handles.cpp) --------------------------------------------
    void initHandleTypes();     // once, after the interpreter has started
    int  runFinalizers();       // run what the collector has queued

    SCM      wrapHandle(Family f, IHandle* h);
    bool     isHandle(Family f, SCM v);
    IHandle* unwrapHandle(Family f, SCM v);   // type-checked; raises on mismatch

    // ---- per family, explicitly -------------------------------------------------
    // wrapping takes the compositor object; unwrapping locks it again, or gives
    // null when it has since died — every getter then reads as it does for a
    // stale handle: #f

    inline SCM windowHandle(PHLWINDOWREF w) {
        return wrapHandle(Family::Window, new SHandle<PHLWINDOWREF>(w));
    }
    inline PHLWINDOW windowOf(SCM v) {
        return static_cast<SHandle<PHLWINDOWREF>*>(unwrapHandle(Family::Window, v))->wp.lock();
    }
    inline bool isWindow(SCM v) { return isHandle(Family::Window, v); }

    inline SCM workspaceHandle(PHLWORKSPACEREF ws) {
        return wrapHandle(Family::Workspace, new SHandle<PHLWORKSPACEREF>(ws));
    }
    inline PHLWORKSPACE workspaceOf(SCM v) {
        return static_cast<SHandle<PHLWORKSPACEREF>*>(unwrapHandle(Family::Workspace, v))->wp.lock();
    }
    inline bool isWorkspace(SCM v) { return isHandle(Family::Workspace, v); }

    inline SCM monitorHandle(PHLMONITORREF m) {
        return wrapHandle(Family::Monitor, new SHandle<PHLMONITORREF>(m));
    }
    inline PHLMONITOR monitorOf(SCM v) {
        return static_cast<SHandle<PHLMONITORREF>*>(unwrapHandle(Family::Monitor, v))->wp.lock();
    }
    inline bool isMonitor(SCM v) { return isHandle(Family::Monitor, v); }

    inline SCM groupHandle(const SP<Desktop::View::CGroup>& g) {
        return wrapHandle(Family::Group, new SHandle<WP<Desktop::View::CGroup>>(g));
    }
    inline SP<Desktop::View::CGroup> groupOf(SCM v) {
        return static_cast<SHandle<WP<Desktop::View::CGroup>>*>(unwrapHandle(Family::Group, v))->wp.lock();
    }
    inline bool isGroup(SCM v) { return isHandle(Family::Group, v); }

    inline SCM layerHandle(const SP<Desktop::View::CLayerSurface>& l) {
        return wrapHandle(Family::Layer, new SHandle<WP<Desktop::View::CLayerSurface>>(l));
    }
    inline SP<Desktop::View::CLayerSurface> layerOf(SCM v) {
        return static_cast<SHandle<WP<Desktop::View::CLayerSurface>>*>(unwrapHandle(Family::Layer, v))->wp.lock();
    }
    inline bool isLayer(SCM v) { return isHandle(Family::Layer, v); }

    inline SCM notificationHandle(SP<Notification::CNotification> n) {
        return wrapHandle(Family::Notification, new SNotificationHandle(n));
    }
    inline SNotificationHandle* notificationHandleOf(SCM v) {
        return static_cast<SNotificationHandle*>(unwrapHandle(Family::Notification, v));
    }
    inline SP<Notification::CNotification> notificationOf(SCM v) {
        return notificationHandleOf(v)->wp.lock();
    }
    inline bool isNotification(SCM v) { return isHandle(Family::Notification, v); }

} // namespace hl
