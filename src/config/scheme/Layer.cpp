/*
    Layer surfaces as Scheme objects (upstream HL.LayerSurface parity).

    Layer surfaces die independently, so a handle is a weak ref: every getter
    reads #f once the surface is gone. No callbacks.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SchemeInternals.hpp"

#include "SThunkRef.hpp"

#include <src/event/EventBus.hpp>
#include <src/state/MonitorState.hpp>
#include <src/debug/log/Logger.hpp>

namespace Config::Scheme {

    using namespace Internals;

    static int hlSchemeLayerAlive(SCM id) {
        if (!g_up)
            return -1;
        return hl::layerOf(id) ? 1 : 0;
    }

    static int hlSchemeLayerSame(SCM a, SCM b) {
        if (!g_up)
            return -1;
        const auto la = hl::layerOf(a);
        const auto lb = hl::layerOf(b);
        return (la && lb && la.get() == lb.get()) ? 1 : 0;
    }

    static SCM hlSchemeLayerAddress(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        if (!ls)
            return SCM_BOOL_F;
        const auto addr = std::format("0x{:x}", reinterpret_cast<uintptr_t>(ls.get()));
        return scm_from_utf8_stringn(addr.c_str(), addr.size());
    }

    static SCM hlSchemeLayerPid(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        return ls ? scm_from_int64(sc<int64_t>(ls->getPID())) : SCM_BOOL_F;
    }

    static SCM hlSchemeLayerMonitorId(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        if (!ls)
            return SCM_BOOL_F;
        const auto mon = ls->m_monitor.lock();
        if (!mon)
            return SCM_BOOL_F;
        return hl::monitorHandle(mon);
    }

    static SCM hlSchemeLayerNamespace(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        if (!ls)
            return SCM_BOOL_F;
        return scm_from_utf8_stringn(ls->m_namespace.c_str(), ls->m_namespace.size());
    }

    static SCM hlSchemeLayerLevel(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        return ls ? scm_from_int64(sc<int64_t>(ls->m_layer)) : SCM_BOOL_F;
    }

    static SCM hlSchemeLayerMapped(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        if (!ls)
            return SCM_BOOL_F;
        return ls->mapped() ? SCM_BOOL_T : SCM_BOOL_F;
    }

    static SCM hlSchemeLayerKbInteractivity(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        return ls ? scm_from_int64(sc<int64_t>(ls->m_keyboardInteractivity)) : SCM_BOOL_F;
    }

    static SCM hlSchemeLayerAboveFullscreen(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        if (!ls)
            return SCM_BOOL_F;
        return (ls->m_flags & Desktop::View::LAYER_FLAG_ABOVE_FULLSCREEN) ? SCM_BOOL_T : SCM_BOOL_F;
    }

    static SCM hlSchemeLayerPosition(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        if (!ls)
            return SCM_BOOL_F;
        return scm_cons(scm_from_int64((int)ls->m_geometry.x), scm_from_int64((int)ls->m_geometry.y));
    }

    static SCM hlSchemeLayerSize(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto ls = hl::layerOf(id);
        if (!ls)
            return SCM_BOOL_F;
        return scm_cons(scm_from_int64((int)ls->m_geometry.width), scm_from_int64((int)ls->m_geometry.height));
    }

    static SCM hlLayers(SCM monId, SCM nsFilter) {
        if (!g_up)
            return SCM_BOOL_F;
        const std::string ns = scm_is_string(nsFilter) ? schemeDatumToStr(nsFilter) : "";
        PHLWINDOWREF dummy; // unused; keeps the compiler from warning on the include order
        (void)dummy;
        PHLMONITOR monFilter;
        if (!scm_is_false(monId))
            monFilter = hl::monitorOf(monId);

        std::vector<SCM> ids;
        for (const auto& mon : State::monitorState()->monitors()) {
            if (monFilter && mon != monFilter)
                continue;
            for (const auto& level : mon->m_layerSurfaceLayers) {
                for (const auto& lsRef : level) {
                    const auto ls = lsRef.lock();
                    if (!ls)
                        continue;
                    if (!ns.empty() && ls->m_namespace != ns)
                        continue;
                    ids.push_back(hl::layerHandle(ls));
                }
            }
        }
        return ids.empty() ? SCM_BOOL_F : hl::listOf(ids);
    }

    static int hlSchemeLayerListen(SCM record, int which) {
        if (!g_up)
            return -1;

        if (which == 0)
            g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.layer.opened.listen([ref = SThunkRef(record)](PHLLS ls) { if (g_up && ls) fireSchemeStr(ref.obj, ls->m_namespace); }));
        else
            g_eventConnections.emplace(SCM_UNPACK(record), Event::bus()->m_events.layer.closed.listen([ref = SThunkRef(record)](PHLLS ls) { if (g_up && ls) fireSchemeStr(ref.obj, ls->m_namespace); }));
        return 0;
    }

    // this family's Scheme-visible surface
    void registerLayer() {
    hl::bind<hlSchemeLayerListen>("hl--c-layer-listen");
    hl::bind<hlLayers>("hl--c-layers");
    hl::bind<hlSchemeLayerAlive>("hl--c-layer-alive");
    hl::bind<hlSchemeLayerSame>("hl--c-layer-same");
    hl::bind<hlSchemeLayerAddress>("hl--c-layer-address");
    hl::bind<hlSchemeLayerPid>("hl--c-layer-pid");
    hl::bind<hlSchemeLayerMonitorId>("hl--c-layer-monitor");
    hl::bind<hlSchemeLayerNamespace>("hl--c-layer-namespace");
    hl::bind<hlSchemeLayerLevel>("hl--c-layer-level");
    hl::bind<hlSchemeLayerMapped>("hl--c-layer-mapped");
    hl::bind<hlSchemeLayerKbInteractivity>("hl--c-layer-kb-interactivity");
    hl::bind<hlSchemeLayerAboveFullscreen>("hl--c-layer-above-fs");
    hl::bind<hlSchemeLayerPosition>("hl--c-layer-position");
    hl::bind<hlSchemeLayerSize>("hl--c-layer-size");
    }

} // namespace Config::Scheme
