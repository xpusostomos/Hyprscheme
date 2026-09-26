/*
    The timer family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"

#include <src/helpers/memory/Memory.hpp>
#include <src/keybinds/Manager.hpp>
#include <src/managers/eventLoop/EventLoopManager.hpp>
#include <src/managers/eventLoop/EventLoopTimer.hpp>

#include <chrono>
#include <unordered_map>

namespace Config::Scheme {

    using namespace Internals;

    // the timer index: record address -> live timer state. Entries erase
    // themselves — a one-shot removes its entry in its own completion
    // callback, reload tears the rest down — so the map only ever holds
    // timers that can still fire. No ids, no global timer list: the event
    // loop owns the CEventLoopTimer, the capture owns the record lock.
    struct STimerEntry {
        SP<CEventLoopTimer> timer;
        int                 repeat;
        uint64_t            ms; // current interval (set-timeout re-tunes it)
    };
    static std::unordered_map<uintptr_t, STimerEntry> g_timerIndex;

    // reload teardown: timers from the previous generation must not fire into
    // the new one. Destroying them releases their record locks (the capture's
    // destructor). The index itself is this family's business.
    void cancelAllTimers() {
        if (!g_pEventLoopManager)
            return;
        for (auto& [addr, e] : g_timerIndex) {
            e.timer->cancel();
            g_pEventLoopManager->removeTimer(e.timer);
        }
        g_timerIndex.clear();
    }

    // timer-record fire: extract (hl-timer-thunk b) and run it zero-arg
    // under the bind result protocol (repeating timers stop on ok #f)
    static Keybinds::SBindResult fireSchemeTimerRec(SCM record) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        watchdogEnter("bind callback");
        const SCM r = hl::call1(hl::globalRef("hl--timer-fire"), record);
        watchdogExit();
        if (r == SCM_BOOL_F)
            return {.success = false, .error = "scheme keybind callback declined"};
        if (r == SCM_BOOL_T || !scm_is_pair(r) || !scm_is_symbol(scm_car(r)))
            return {};
        Keybinds::SBindResult res;
        SCM l = r;
        while (scm_is_pair(l) && scm_is_pair(scm_cdr(l))) {
            const std::string k = schemeDatumToStr(scm_car(l));
            const SCM        v  = scm_car(scm_cdr(l));
            if (k == "ok") {
                if (v == SCM_BOOL_F)
                    res.success = false;
            } else if (k == "pass-event") {
                if (v == SCM_BOOL_T)
                    res.passEvent = true;
            } else if (k == "request-release") {
                if (v == SCM_BOOL_T)
                    res.followUp = Keybinds::BIND_FOLLOW_UP_TRIGGER_RELEASE;
            } else if (k == "error" && scm_is_string(v)) {
                res.error = schemeDatumToStr(v);
            }
            l = scm_cdr(scm_cdr(l));
        }
        return res;
    }

    static STimerEntry* timerByRecord(SCM record); // defined below

    // called from Scheme as a gsubr; repeat != 0 re-arms forever
    // (or until the callback errors, which stops zombie loops)
    static int hlSchemeTimer(SCM record, int ms, int repeat) {
        if (!g_up || !g_pEventLoopManager || ms < 0)
            return -1;

        // the capture carries the record LOCKED (SThunkRef): the thunk stays
        // alive while the timer can fire; when the timer object is destroyed
        // (completion below, or reload teardown), the lock releases.
        SThunkRef ref(record);

        auto shared = makeShared<CEventLoopTimer>(std::chrono::milliseconds(ms),
            [ref, ms, repeat](SP<CEventLoopTimer> self, void*) {
                const auto result = fireSchemeTimerRec(ref.obj);

                if (repeat && result.success) {
                    // re-arm at the CURRENT interval — set-timeout re-tunes
                    // the index entry, the captured ms is only the initial one
                    uint64_t cur = ms;
                    if (const auto* e = timerByRecord(ref.obj))
                        cur = e->ms;
                    self->updateTimeout(std::chrono::milliseconds(sc<int64_t>(cur)));
                    return;
                }

                // one-shot done, or the callback failed: tear down.
                // onTimerFire dispatches over a copy of the timer list, so
                // removing ourselves here is safe.
                self->cancel();
                if (g_pEventLoopManager)
                    g_pEventLoopManager->removeTimer(self);
                g_timerIndex.erase(SCM_UNPACK(ref.obj));
            },
            nullptr);

        g_pEventLoopManager->addTimer(shared);
        g_timerIndex.emplace(SCM_UNPACK(record),
                             STimerEntry{shared, repeat, sc<uint64_t>(ms)});
        return 0;
    }

    static STimerEntry* timerByRecord(SCM record) {
        const auto it = g_timerIndex.find(SCM_UNPACK(record));
        return it == g_timerIndex.end() ? nullptr : &it->second;
    }

    static int hlTimerSetEnabled(SCM record, int enabled) {
        if (!g_up)
            return -1;
        auto* e = timerByRecord(record);
        if (!e)
            return -1;
        if (enabled != 0)
            e->timer->updateTimeout(std::chrono::milliseconds(sc<int64_t>(e->ms)));
        else
            e->timer->updateTimeout(std::nullopt);
        return 0;
    }

    static int hlTimerEnabled(SCM record) {
        const auto* e = timerByRecord(record);
        return (e && e->timer && e->timer->armed()) ? 1 : 0;
    }

    static int hlTimerSetTimeout(SCM record, double ms) {
        if (!g_up)
            return -1;
        auto* e = timerByRecord(record);
        if (!e || ms < 1)
            return -1;
        e->ms = sc<uint64_t>(ms);
        e->timer->updateTimeout(std::chrono::milliseconds(sc<int64_t>(ms)));
        return 0;
    }

    static int hlTimerCancel(SCM record) {
        if (!g_up || !g_pEventLoopManager)
            return -1;
        auto* e = timerByRecord(record);
        if (!e)
            return -1;
        e->timer->cancel();
        g_pEventLoopManager->removeTimer(e->timer);
        g_timerIndex.erase(SCM_UNPACK(record));
        return 0;
    }

    // this family's Scheme-visible surface
    void registerTimer() {
        hl::bind<hlSchemeTimer>("hl--c-timer");
        hl::bind<hlTimerSetEnabled>("hl--c-timer-set-enabled");
        hl::bind<hlTimerEnabled>("hl--c-timer-enabled");
        hl::bind<hlTimerSetTimeout>("hl--c-timer-set-timeout");
        hl::bind<hlTimerCancel>("hl--c-timer-cancel");
    }

} // namespace Config::Scheme
