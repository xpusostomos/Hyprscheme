/*
    The handle registry — see Handles.hpp for the design.

    One foreign object type per family, created after the interpreter starts.
    The finalizer deletes the C++ handle; running it is the machinery's job
    (runFinalizers, pumped from the after-gc-hook), because deleting a handle
    touches compositor state and must happen on the compositor's thread.
*/

#include "Handles.hpp"

namespace hl {

    // the six types, in Family order, with their names for error messages
    static SCM         g_types[6];
    static const char* g_names[6];

    static void handleFinalizer(SCM obj) {
        // the finalizer must not allocate, and must not throw: all it does is
        // release the weak ref behind the handle
        delete static_cast<IHandle*>(scm_foreign_object_ref(obj, 0));
    }

    static SCM typeOf(Family f) {
        return g_types[static_cast<int>(f)];
    }

    void initHandleTypes() {
        struct {
            const char* name;
            Family      family;
        } types[] = {
            {"hl-group", Family::Group},
            {"hl-layer", Family::Layer},
            {"hl-monitor", Family::Monitor},
            {"hl-notification", Family::Notification},
            {"hl-window", Family::Window},
            {"hl-workspace", Family::Workspace},
        };
        for (const auto& t : types) {
            // one slot, named "handle": the C++ object behind the handle
            g_types[static_cast<int>(t.family)] =
                scm_make_foreign_object_type(scm_from_locale_symbol(t.name), scm_list_1(scm_from_locale_symbol("handle")), handleFinalizer);
            g_names[static_cast<int>(t.family)] = t.name;
        }
    }

    int runFinalizers() {
        return scm_run_finalizers();
    }

    SCM wrapHandle(Family f, IHandle* h) {
        return scm_make_foreign_object_1(typeOf(f), h);
    }

    bool isHandle(Family f, SCM v) {
        return SCM_STRUCTP(v) && scm_is_eq(SCM_STRUCT_VTABLE(v), typeOf(f));
    }

    IHandle* unwrapHandle(Family f, SCM v) {
        if (!isHandle(f, v))
            scm_misc_error("hyprscheme", "expected a ~A handle, got ~S", scm_list_2(scm_from_locale_string(g_names[static_cast<int>(f)]), v));
        return static_cast<IHandle*>(scm_foreign_object_ref(v, 0));
    }

} // namespace hl
