/*
    The group family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/view/window/WindowGroupMembership.hpp>
#include <src/desktop/state/WindowState.hpp>

#include <src/desktop/view/Group.hpp>
#include <src/desktop/view/window/Window.hpp>

namespace Config::Scheme {

    using namespace Internals;

    static int hlSchemeGroupsLocked() {
        if (!g_up)
            return 0;
        return Desktop::windowState()->groupsLocked() ? 1 : 0;
    }

    static int hlSchemeGroupToggle(SCM id) {
        if (!g_up)
            return -1;
        return actionResult("group-toggle", Config::Actions::toggleGroup(actionWindow(id)));
    }

    // explicit set: a no-op when the window is already in the requested
    // state (group() is null iff the window is not a member of a group)
    static int hlSchemeGroupSet(SCM id, int on) {
        if (!g_up)
            return -1;
        const auto w = actionWindow(id).value_or(nullptr);
        if (!w)
            return -1;
        if ((w->grouping().group() != nullptr) == (on != 0))
            return 0;
        return actionResult("group-set", Config::Actions::toggleGroup(w));
    }

    static int hlSchemeGroupCycle(SCM id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("group-cycle", Config::Actions::changeGroupActive(prev == 0, actionWindow(id)));
    }

    static int hlSchemeGroupIndex(SCM id, int index) {
        if (!g_up)
            return -1;
        return actionResult("group-index", Config::Actions::setGroupActive(index, actionWindow(id)));
    }

    static int hlSchemeGroupMoveWindow(SCM id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("group-move-window", Config::Actions::moveGroupWindow(prev == 0));
    }

    static int hlSchemeGroupLock(int act) {
        if (!g_up)
            return -1;
        return actionResult("group-lock", Config::Actions::lockGroups(sc<Config::Actions::eTogglableAction>(act)));
    }

    static int hlSchemeGroupLockActive(int act) {
        if (!g_up)
            return -1;
        return actionResult("group-lock-active", Config::Actions::lockActiveGroup(sc<Config::Actions::eTogglableAction>(act)));
    }

    static int hlSchemeGroupAlive(SCM id) {
        if (!g_up)
            return -1;
        return hl::groupOf(id) ? 1 : 0;
    }

    static int hlSchemeGroupSame(SCM a, SCM b) {
        if (!g_up)
            return -1;
        const auto ga = hl::groupOf(a);
        const auto gb = hl::groupOf(b);
        return (ga && gb && ga.get() == gb.get()) ? 1 : 0;
    }

    static SCM hlSchemeGroupMembers(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto group = hl::groupOf(id);
        if (!group)
            return SCM_BOOL_F;
        SCM l = SCM_EOL;
        for (const auto& grouped : group->windows()) {
            const auto w = grouped.lock();
            if (!w)
                continue;
            l = scm_cons(hl::windowHandle(w), l);
        }
        SCM out = SCM_EOL;
        for (SCM p = l; scm_is_pair(p); p = scm_cdr(p))
            out = scm_cons(scm_car(p), out);
        return out;
    }

    static SCM hlSchemeGroupCurrent(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto group = hl::groupOf(id);
        if (!group)
            return SCM_BOOL_F;
        const auto current = group->current();
        if (!current)
            return SCM_BOOL_F;
        return hl::windowHandle(current);
    }

    static SCM hlSchemeGroupCurrentIdx(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto group = hl::groupOf(id);
        if (!group)
            return SCM_BOOL_F;
        return scm_from_int64(sc<int64_t>(group->getCurrentIdx()) + 1); // 1-based, upstream parity
    }

    static SCM hlSchemeGroupSize(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto group = hl::groupOf(id);
        return group ? scm_from_int64(sc<int64_t>(group->size())) : SCM_BOOL_F;
    }

    static SCM hlSchemeGroupLocked(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto group = hl::groupOf(id);
        if (!group)
            return SCM_BOOL_F;
        return group->locked() ? SCM_BOOL_T : SCM_BOOL_F;
    }

    static SCM hlSchemeGroupDenied(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto group = hl::groupOf(id);
        if (!group)
            return SCM_BOOL_F;
        return group->denied() ? SCM_BOOL_T : SCM_BOOL_F;
    }

    // index crosses as -1 = append; 1-based otherwise (upstream parity)
    static int hlSchemeGroupAdd(SCM id, SCM winId, long long index) {
        if (!g_up)
            return -1;
        const auto group = hl::groupOf(id);
        if (!group)
            return -1;
        const auto window = hl::windowOf(winId);
        if (!window)
            return -1;
        if (window->grouping().group() == group)
            return 0; // already a member of THIS group: no-op (upstream parity)
        if (group->denied()) {
            g_configError = "hl-group-add!: target group is denied";
            return -2;
        }
        if (!window->grouping().canBeGroupedInto(group)) {
            g_configError = "hl-group-add!: window cannot be added to group";
            return -2;
        }
        group->add(window, index >= 0 ? std::optional<size_t>(sc<size_t>(index - 1)) : std::nullopt);
        return 0;
    }

    static int hlSchemeGroupRemove(SCM id, SCM winId) {
        if (!g_up)
            return -1;
        const auto group = hl::groupOf(id);
        if (!group)
            return -1;
        const auto window = hl::windowOf(winId);
        if (!window || !group->has(window)) {
            g_configError = "hl-group-remove!: window is not a group member";
            return -2;
        }
        group->remove(window);
        return 0;
    }

    // this family's Scheme-visible surface
    void registerGroup() {
        hl::bind<hlSchemeGroupsLocked>("hl--c-groups-locked");
        hl::bind<hlSchemeGroupToggle>("hl--c-group-toggle");
        hl::bind<hlSchemeGroupSet>("hl--c-group-set");
        hl::bind<hlSchemeGroupCycle>("hl--c-group-cycle");
        hl::bind<hlSchemeGroupIndex>("hl--c-group-index");
        hl::bind<hlSchemeGroupMoveWindow>("hl--c-group-move-window");
        hl::bind<hlSchemeGroupLock>("hl--c-group-lock");
        hl::bind<hlSchemeGroupLockActive>("hl--c-group-lock-active");
        hl::bind<hlSchemeGroupAlive>("hl--c-group-alive");
        hl::bind<hlSchemeGroupSame>("hl--c-group-same");
        hl::bind<hlSchemeGroupMembers>("hl--c-group-members");
        hl::bind<hlSchemeGroupCurrent>("hl--c-group-current");
        hl::bind<hlSchemeGroupCurrentIdx>("hl--c-group-current-idx");
        hl::bind<hlSchemeGroupSize>("hl--c-group-size");
        hl::bind<hlSchemeGroupLocked>("hl--c-group-locked");
        hl::bind<hlSchemeGroupDenied>("hl--c-group-denied");
        hl::bind<hlSchemeGroupAdd>("hl--c-group-add");
        hl::bind<hlSchemeGroupRemove>("hl--c-group-remove");
    }

} // namespace Config::Scheme
