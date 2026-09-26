/*
    The monitor family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"
#include <src/config/shared/monitor/Parser.hpp>
#include <src/config/shared/monitor/MonitorRuleManager.hpp>
#include <src/desktop/state/FocusState.hpp>

#include <src/state/MonitorState.hpp>
#include <src/state/WorkspaceState.hpp>

#include <aquamarine/backend/Backend.hpp>

namespace Config::Scheme {

    using namespace Internals;

    static const char* monitorBackendName(Aquamarine::eBackendType t); // defined below

    PHLMONITOR monitorFromName(const char* name) {
        if (!name || !*name)
            return nullptr;
        for (const auto& m : State::monitorState()->monitors())
            if (m->m_name == name)
                return m;
        return nullptr;
    }

    static int hlSchemeFocusMonitor(const char* name) {
        if (!g_up)
            return -1;
        const auto mon = monitorFromName(name);
        if (!mon) {
            LOG(Log::ERR, "[scheme] focus-monitor: no monitor named {}", name ? name : "");
            return -1;
        }
        return actionResult("focus-monitor", Config::Actions::focusMonitor(mon));
    }

    // explicit set: opens the special workspace on the monitor (creating it
    // when missing); an empty name closes whatever is open there
    static int hlSchemeMonitorSetSpecial(const char* monSel, const char* wsName) {
        if (!g_up)
            return -1;
        const auto mon = State::monitorState()->query().configString(monSel ? monSel : "").run();
        if (!mon)
            return -1;
        PHLWORKSPACE ws = nullptr;
        if (wsName && *wsName) {
            ws = specialWorkspaceFromName(wsName, mon);
            if (!ws)
                return -1;
        }
        mon->setSpecialWorkspace(ws, true);
        return 0;
    }

    // -- monitor getters
    static SCM hlMonitorName(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_utf8_stringn(mon->m_name.c_str(), mon->m_name.size()); });
    }

    static SCM hlMonitorDescription(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_utf8_stringn(mon->m_description.c_str(), mon->m_description.size()); });
    }

    static SCM hlMonitorNumber(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_int64(sc<int>(mon->m_id)); });
    }

    static SCM hlMonitorEnabled(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return boolResult(mon->m_enabled); });
    }

    static SCM hlMonitorFocused(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return boolResult(Desktop::focusState()->monitor() == mon); });
    }

    static SCM hlMonitorX(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_int64(sc<int>(mon->m_position.x)); });
    }

    static SCM hlMonitorY(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_int64(sc<int>(mon->m_position.y)); });
    }

    static SCM hlMonitorWidth(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_int64(sc<int>(mon->m_size.x)); });
    }

    static SCM hlMonitorHeight(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_int64(sc<int>(mon->m_size.y)); });
    }

    static SCM hlMonitorScale(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_double(sc<double>(mon->m_scale)); });
    }

    static SCM hlMonitorTransform(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_int64(sc<int>(mon->m_transform)); });
    }

    static SCM hlMonitorRefreshRate(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_double(sc<double>(mon->m_refreshRate)); });
    }

    static SCM hlMonitorMode(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM {
            const auto s = std::format("{}x{}@{}", sc<int>(mon->m_size.x), sc<int>(mon->m_size.y), mon->m_refreshRate);
            return scm_from_utf8_stringn(s.c_str(), s.size());
        });
    }

    static SCM hlMonitorDpms(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return boolResult(mon->m_dpmsStatus); });
    }

    static SCM hlMonitorVrr(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return boolResult(mon->m_vrrActive != 0); });
    }

    static SCM hlMonitor10bit(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return boolResult(mon->m_enabled10bit); });
    }

    static SCM hlMonitorSerial(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM {
            const auto& s = mon->m_output->serial;
            return scm_from_utf8_stringn(s.c_str(), s.size());
        });
    }

    // (physical-width . physical-height), in mm
    static SCM hlMonitorPhysicalSize(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM {
            return scm_cons(scm_from_int64((int)mon->m_output->physicalSize.x),
                         scm_from_int64((int)mon->m_output->physicalSize.y));
        });
    }

    static SCM hlMonitorMirrors(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM {
            std::vector<SCM> ids;
            for (const auto& mirrorRef : mon->m_mirrors) {
                const auto mirror = mirrorRef.lock();
                if (!mirror)
                    continue;
                const auto mid = hl::monitorHandle(mirror);
                ids.push_back(mid);
            }
            return hl::listOf(ids);
        });
    }

    // list of per-mode plists: ((width w height h refresh-rate r preferred b) ...)
    static SCM hlMonitorAvailableModes(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM {
            std::vector<SCM> roots, modes;
            for (const auto& mode : mon->m_output->modes) {
                if (!mode)
                    continue;
                std::vector<SCM> elems;
                SCM k = scm_from_locale_symbol("width");
                hl::pin(k, roots); elems.push_back(k);
                elems.push_back(scm_from_int64((int)mode->pixelSize.x));
                k = scm_from_locale_symbol("height");
                hl::pin(k, roots); elems.push_back(k);
                elems.push_back(scm_from_int64((int)mode->pixelSize.y));
                k = scm_from_locale_symbol("refresh-rate");
                hl::pin(k, roots); elems.push_back(k);
                SCM r = scm_from_double(mode->refreshRate / 1000.0);
                hl::pin(r, roots); elems.push_back(r);
                k = scm_from_locale_symbol("preferred");
                hl::pin(k, roots); elems.push_back(k);
                elems.push_back(mode->preferred ? SCM_BOOL_T : SCM_BOOL_F);
                SCM m = SCM_EOL;
                for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                    m = scm_cons(*it, m);
                    hl::pin(m, roots);
                }
                modes.push_back(m);
            }
            SCM l = SCM_EOL;
            for (auto it = modes.rbegin(); it != modes.rend(); ++it) {
                l = scm_cons(*it, l);
                hl::pin(l, roots);
            }
            hl::unpin(roots);
            return l;
        });
    }

    // plist: (backend "..." hdr b chroma b bt2020 b vrr-capable b)
    static SCM hlMonitorHardwareDetails(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM {
            std::vector<SCM> roots, elems;
            const std::string backend = monitorBackendName(mon->m_output->getBackend()->type());
            SCM k = scm_from_locale_symbol("backend");
            hl::pin(k, roots); elems.push_back(k);
            SCM b = scm_from_utf8_stringn(backend.c_str(), backend.size());
            hl::pin(b, roots); elems.push_back(b);
            k = scm_from_locale_symbol("hdr");
            hl::pin(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->parsedEDID.hdrMetadata.has_value() ? SCM_BOOL_T : SCM_BOOL_F);
            k = scm_from_locale_symbol("chroma");
            hl::pin(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->parsedEDID.chromaticityCoords.has_value() ? SCM_BOOL_T : SCM_BOOL_F);
            k = scm_from_locale_symbol("bt2020");
            hl::pin(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->parsedEDID.supportsBT2020 ? SCM_BOOL_T : SCM_BOOL_F);
            k = scm_from_locale_symbol("vrr-capable");
            hl::pin(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->vrrCapable ? SCM_BOOL_T : SCM_BOOL_F);
            SCM l = SCM_EOL;
            for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                l = scm_cons(*it, l);
                hl::pin(l, roots);
            }
            hl::unpin(roots);
            return l;
        });
    }

    // the monitor this one mirrors, as a handle; #f when not a mirror
    static SCM hlMonitorMirrorOf(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return monitorHandleResult(mon->m_mirrorOf.lock()); });
    }

    static SCM hlMonitorActiveWorkspace(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return workspaceHandleResult(mon->m_activeWorkspace); });
    }

    static SCM hlMonitorActiveSpecialWorkspace(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return workspaceHandleResult(mon->m_activeSpecialWorkspace); });
    }

    static SCM hlMonitorAlive(SCM id) {
        return boolResult(g_up && hl::monitorOf(id) != nullptr);
    }

    static SCM hlMonitorSame(SCM a, SCM b) {
        const auto ma = g_up ? hl::monitorOf(a) : nullptr;
        const auto mb = g_up ? hl::monitorOf(b) : nullptr;
        return boolResult(ma && mb && ma.get() == mb.get());
    }

    static SCM hlMonitorSelector(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM { return scm_from_utf8_stringn(mon->m_name.c_str(), mon->m_name.size()); });
    }

    static const char* monitorBackendName(Aquamarine::eBackendType t) {
        switch (t) {
            case Aquamarine::AQ_BACKEND_DRM: return "drm";
            case Aquamarine::AQ_BACKEND_WAYLAND: return "wayland";
            case Aquamarine::AQ_BACKEND_HEADLESS: return "headless";
            case Aquamarine::AQ_BACKEND_NULL: return "null";
            default: return "unknown";
        }
    }

    static SCM hlMonitorReserved(SCM id) {
        return monGet(id, [](PHLMONITOR mon) -> SCM {
            // a plist: (top n left n right n bottom n)
            const auto&         r = mon->m_reservedArea;
            std::vector<SCM>    roots;
            std::vector<SCM>    elems;
            const std::string   KEYS[] = {"top", "left", "right", "bottom"};
            const int           VALUES[] = {r.top(), r.left(), r.right(), r.bottom()};
            for (int i = 0; i < 4; ++i) {
                SCM k = scm_from_locale_symbol(KEYS[i].c_str());
                hl::pin(k, roots);
                elems.push_back(k);
                elems.push_back(scm_from_int64(VALUES[i]));
            }
            SCM l = SCM_EOL;
            for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                l = scm_cons(*it, l);
                hl::pin(l, roots);
            }
            hl::unpin(roots);
            return l;
        });
    }

    // (monitorFilter, namespaceFilter) — nullptr/empty = no filter; the
    // monitor crosses as a handle id (0 = none) after Scheme-side coercion
    static SCM hlSchemeMonitorNames() {
        if (!g_up)
            return SCM_BOOL_F;

        std::vector<SCM> ids;
        for (const auto& m : State::monitorState()->monitors()) {
            ids.push_back(hl::monitorHandle(m));
        }

        return ids.empty() ? SCM_BOOL_F : hl::listOf(ids);
    }

    // this family's Scheme-visible surface
    void registerMonitor() {
        hl::bind<hlSchemeFocusMonitor>("hl--c-focus-monitor");
        hl::bind<hlSchemeMonitorSetSpecial>("hl--c-monitor-set-special");
        hl::bind<hlSchemeMonitorNames>("hl--c-monitor-names");
        hl::bind<hlMonitorName>("hl--c-monitor-name");
        hl::bind<hlMonitorDescription>("hl--c-monitor-description");
        hl::bind<hlMonitorNumber>("hl--c-monitor-number");
        hl::bind<hlMonitorEnabled>("hl--c-monitor-enabled");
        hl::bind<hlMonitorFocused>("hl--c-monitor-focused");
        hl::bind<hlMonitorX>("hl--c-monitor-x");
        hl::bind<hlMonitorY>("hl--c-monitor-y");
        hl::bind<hlMonitorWidth>("hl--c-monitor-width");
        hl::bind<hlMonitorHeight>("hl--c-monitor-height");
        hl::bind<hlMonitorScale>("hl--c-monitor-scale");
        hl::bind<hlMonitorTransform>("hl--c-monitor-transform");
        hl::bind<hlMonitorRefreshRate>("hl--c-monitor-refresh-rate");
        hl::bind<hlMonitorMode>("hl--c-monitor-mode");
        hl::bind<hlMonitorDpms>("hl--c-monitor-dpms");
        hl::bind<hlMonitorVrr>("hl--c-monitor-vrr");
        hl::bind<hlMonitor10bit>("hl--c-monitor-10bit");
        hl::bind<hlMonitorReserved>("hl--c-monitor-reserved");
        hl::bind<hlMonitorSerial>("hl--c-monitor-serial");
        hl::bind<hlMonitorPhysicalSize>("hl--c-monitor-physical-size");
        hl::bind<hlMonitorMirrors>("hl--c-monitor-mirrors");
        hl::bind<hlMonitorAvailableModes>("hl--c-monitor-available-modes");
        hl::bind<hlMonitorHardwareDetails>("hl--c-monitor-hardware-details");
        hl::bind<hlMonitorMirrorOf>("hl--c-monitor-mirror-of");
        hl::bind<hlMonitorActiveWorkspace>("hl--c-monitor-active-workspace");
        hl::bind<hlMonitorActiveSpecialWorkspace>("hl--c-monitor-active-special-workspace");
        hl::bind<hlMonitorAlive>("hl--c-monitor-alive");
        hl::bind<hlMonitorSame>("hl--c-monitor-same");
        hl::bind<hlMonitorSelector>("hl--c-monitor-selector");
    }

} // namespace Config::Scheme
