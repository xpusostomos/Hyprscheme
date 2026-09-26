/*
    The config family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"

#include <cstring>
#include <src/managers/permissions/DynamicPermissionManager.hpp>
#include <src/config/shared/workspace/WorkspaceRule.hpp>
#include <src/animation/AnimationManager.hpp>
#include <src/config/shared/animation/AnimationTree.hpp>
#include <src/config/shared/monitor/MonitorRuleManager.hpp>

#include <src/keybinds/Manager.hpp>
#include <src/desktop/reserved/ReservedArea.hpp>
#include <src/desktop/rule/Rule.hpp>
#include <src/config/ConfigManager.hpp>
#include <src/config/lua/ConfigManager.hpp>
#include <src/config/lua/types/LuaConfigValue.hpp>
#include <src/config/lua/types/LuaConfigCssGap.hpp>
#include <src/config/lua/types/LuaConfigFloat.hpp>
#include <src/config/lua/types/LuaConfigInt.hpp>
#include <src/config/shared/monitor/Parser.hpp>
#include <src/config/lua/types/LuaConfigValue.hpp>

namespace Config::Scheme {

    using namespace Internals;

    static lua_State*  g_configScratch = nullptr;

    static Config::Lua::ILuaConfigValue* configValueByKey(const char* key) {
        auto* mgr = sc<Lua::CConfigManager*>(Config::mgr().get());
        if (!mgr || !key || !*key)
            return nullptr;
        auto& vals = mgr->m_configValues;
        auto  it   = vals.find(std::string(key));
        if (it == vals.end()) {
            std::string k = key;
            std::ranges::replace(k, ':', '.');
            it = vals.find(k);
        }
        return it == vals.end() ? nullptr : it->second.get();
    }

    lua_State* configScratch() {
        if (!g_configScratch)
            g_configScratch = luaL_newstate();
        return g_configScratch;
    }

    // clears the scratch stack: once at the start of each hl-config-add! value
    static int hlConfigBegin() {
        if (!g_up)
            return -1;
        lua_settop(configScratch(), 0);
        return 0;
    }

    static int hlConfigPushNum(double v) {
        if (!g_up)
            return -1;
        lua_pushnumber(configScratch(), v);
        return 0;
    }

    static int hlConfigPushInt(double v) {
        if (!g_up)
            return -1;
        lua_pushinteger(configScratch(), sc<long long>(v));
        return 0;
    }

    static int hlConfigPushBool(int v) {
        if (!g_up)
            return -1;
        lua_pushboolean(configScratch(), v != 0);
        return 0;
    }

    static int hlConfigPushStr(const char* v) {
        if (!g_up)
            return -1;
        lua_pushlstring(configScratch(), v ? v : "", v ? strlen(v) : 0);
        return 0;
    }

    static int hlConfigTblOpen(int isHash) {
        if (!g_up)
            return -1;
        lua_createtable(configScratch(), isHash ? 0 : 4, isHash ? 4 : 0);
        return 0;
    }

    static int hlConfigTblKey(const char* k) {
        if (!g_up)
            return -1;
        lua_pushlstring(configScratch(), k ? k : "", k ? strlen(k) : 0);
        return 0;
    }

    static int hlConfigTblSetHash() {
        if (!g_up)
            return -1;
        lua_rawset(configScratch(), -3); // pops key + value onto the table below
        return 0;
    }

    static int hlConfigTblSeti(int idx) {
        if (!g_up)
            return -1;
        lua_rawseti(configScratch(), -2, idx); // pops the value onto the table below
        return 0;
    }

    static int hlConfigSet(const char* key) {
        if (!g_up)
            return -1;
        auto* val = configValueByKey(key);
        if (!val) {
            g_configError = std::string("unknown config key '") + (key ? key : "") + "'";
            return -1;
        }
        lua_State*   L   = configScratch();
        const auto   err = val->parse(L);
        lua_settop(L, 0);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK) {
            g_configError = err.message.empty() ? "parse error" : err.message;
            return -2;
        }
        Supplementary::refresher()->scheduleRefresh(val->refreshBits());
        return 0;
    }

    static SCM hlConfigLastError() {
        return scm_from_utf8_stringn(g_configError.c_str(), g_configError.size());
    }

    // mirrored from DEVICE_FIELDS (LuaBindingsConfigRules.cpp)
    enum class eDeviceKind : uint8_t { BOOL, INT, FLOAT, STRING, VEC2 };

    struct SDeviceField {
        const char* name;
        eDeviceKind kind;
        double      lo, hi; // bounds for INT/FLOAT (unused otherwise)
    };

    static const SDeviceField DEVICE_FIELDS[] = {
        {"sensitivity", eDeviceKind::FLOAT, -1, 1},      {"accel_profile", eDeviceKind::STRING, 0, 0},
        {"rotation", eDeviceKind::INT, 0, 359},          {"kb_file", eDeviceKind::STRING, 0, 0},
        {"kb_layout", eDeviceKind::STRING, 0, 0},        {"kb_variant", eDeviceKind::STRING, 0, 0},
        {"kb_options", eDeviceKind::STRING, 0, 0},       {"kb_rules", eDeviceKind::STRING, 0, 0},
        {"kb_model", eDeviceKind::STRING, 0, 0},         {"repeat_rate", eDeviceKind::INT, 0, 200},
        {"repeat_delay", eDeviceKind::INT, 0, 2000},     {"natural_scroll", eDeviceKind::BOOL, 0, 0},
        {"tap_button_map", eDeviceKind::STRING, 0, 0},   {"numlock_by_default", eDeviceKind::BOOL, 0, 0},
        {"resolve_binds_by_sym", eDeviceKind::BOOL, 0, 0}, {"disable_while_typing", eDeviceKind::BOOL, 0, 0},
        {"clickfinger_behavior", eDeviceKind::BOOL, 0, 0}, {"middle_button_emulation", eDeviceKind::BOOL, 0, 0},
        {"tap_to_click", eDeviceKind::BOOL, 0, 0},       {"tap_and_drag", eDeviceKind::BOOL, 0, 0},
        {"drag_lock", eDeviceKind::INT, 0, 2},           {"left_handed", eDeviceKind::BOOL, 0, 0},
        {"scroll_method", eDeviceKind::STRING, 0, 0},    {"scroll_button", eDeviceKind::INT, 0, 300},
        {"scroll_button_lock", eDeviceKind::BOOL, 0, 0}, {"scroll_points", eDeviceKind::STRING, 0, 0},
        {"scroll_factor", eDeviceKind::FLOAT, 0, 100},   {"transform", eDeviceKind::INT, 0, 0},
        {"output", eDeviceKind::STRING, 0, 0},           {"enabled", eDeviceKind::BOOL, 0, 0},
        {"region_position", eDeviceKind::VEC2, 0, 0},    {"absolute_region_position", eDeviceKind::BOOL, 0, 0},
        {"region_size", eDeviceKind::VEC2, 0, 0},        {"relative_input", eDeviceKind::BOOL, 0, 0},
        {"active_area_position", eDeviceKind::VEC2, 0, 0}, {"active_area_size", eDeviceKind::VEC2, 0, 0},
        {"flip_x", eDeviceKind::BOOL, 0, 0},             {"flip_y", eDeviceKind::BOOL, 0, 0},
        {"drag_3fg", eDeviceKind::INT, 0, 2},            {"keybinds", eDeviceKind::BOOL, 0, 0},
        {"share_states", eDeviceKind::INT, 0, 2},        {"release_pressed_on_close", eDeviceKind::BOOL, 0, 0},
        {"tags", eDeviceKind::STRING, 0, 0},
    };

    // minimal ILuaConfigValue holding one device value. parse/push are unused
    // by the plugin (values arrive as scheme objects); the as* readbacks feed
    // the input manager's getDeviceInt/Float/String.
    class CDeviceValue : public Config::Lua::ILuaConfigValue {
      public:
        CDeviceValue(bool b) : m_kind(eDeviceKind::BOOL), m_bool(b) { m_bSetByUser = true; }
        CDeviceValue(Config::INTEGER i) : m_kind(eDeviceKind::INT), m_int(i) { m_bSetByUser = true; }
        CDeviceValue(Config::FLOAT f) : m_kind(eDeviceKind::FLOAT), m_fl(f) { m_bSetByUser = true; }
        CDeviceValue(Config::STRING s) : m_kind(eDeviceKind::STRING), m_str(std::move(s)) { m_bSetByUser = true; }
        CDeviceValue(Config::VEC2 v) : m_kind(eDeviceKind::VEC2), m_vec(v) { m_bSetByUser = true; }

        virtual Config::Lua::SParseError parse(lua_State*) override { return {}; }
        virtual const std::type_info*    underlying() override {
            switch (m_kind) {
                case eDeviceKind::BOOL: return &typeid(bool);
                case eDeviceKind::INT: return &typeid(Config::INTEGER);
                case eDeviceKind::FLOAT: return &typeid(Config::FLOAT);
                case eDeviceKind::STRING: return &typeid(Config::STRING);
                case eDeviceKind::VEC2: return &typeid(Config::VEC2);
            }
            return &typeid(Config::VEC2);
        }
        virtual void const* data() override {
            switch (m_kind) {
                case eDeviceKind::BOOL: return &m_bool;
                case eDeviceKind::INT: return &m_int;
                case eDeviceKind::FLOAT: return &m_fl;
                case eDeviceKind::STRING: return &m_str;
                case eDeviceKind::VEC2: return &m_vec;
            }
            return nullptr;
        }
        virtual std::string toString() override {
            switch (m_kind) {
                case eDeviceKind::BOOL: return m_bool ? "true" : "false";
                case eDeviceKind::INT: return std::to_string(m_int);
                case eDeviceKind::FLOAT: {
                    char buf[64];
                    snprintf(buf, sizeof buf, "%.17g", m_fl);
                    return buf;
                }
                case eDeviceKind::STRING: return m_str;
                case eDeviceKind::VEC2: return std::format("{} x {}", (int)m_vec.x, (int)m_vec.y);
            }
            return "";
        }
        virtual void push(lua_State* L) override {
            switch (m_kind) {
                case eDeviceKind::BOOL: lua_pushboolean(L, m_bool); break;
                case eDeviceKind::INT: lua_pushinteger(L, m_int); break;
                case eDeviceKind::FLOAT: lua_pushnumber(L, m_fl); break;
                case eDeviceKind::STRING: lua_pushstring(L, m_str.c_str()); break;
                case eDeviceKind::VEC2:
                    lua_newtable(L);
                    lua_pushnumber(L, m_vec.x); lua_rawseti(L, -2, 1);
                    lua_pushnumber(L, m_vec.y); lua_rawseti(L, -2, 2);
                    break;
            }
        }
        virtual void reset() override { m_bSetByUser = false; }
        virtual Config::INTEGER asInt() override {
            switch (m_kind) {
                case eDeviceKind::BOOL: return m_bool ? 1 : 0;
                case eDeviceKind::INT: return m_int;
                case eDeviceKind::FLOAT: return (Config::INTEGER)m_fl;
                default: return 0;
            }
        }
        virtual Config::FLOAT asFloat() override {
            switch (m_kind) {
                case eDeviceKind::FLOAT: return m_fl;
                case eDeviceKind::INT: return (Config::FLOAT)m_int;
                default: return 0.F;
            }
        }
        virtual Config::VEC2 asVec2() override { return m_vec; }
        virtual Config::STRING asString() override {
            if (m_kind == eDeviceKind::STRING) return m_str;
            return toString();
        }

      private:
        eDeviceKind     m_kind = eDeviceKind::BOOL;
        bool            m_bool = false;
        Config::INTEGER m_int  = 0;
        Config::FLOAT   m_fl   = 0.F;
        Config::STRING  m_str;
        Config::VEC2    m_vec{0, 0};
    };

    // coerce a scheme value to the field's kind; sets g_configError on failure
    static std::optional<std::pair<std::string, UP<CDeviceValue>>> deviceValue(const SDeviceField& f, SCM v) {
        auto fail = [&](const char* why) -> std::optional<std::pair<std::string, UP<CDeviceValue>>> {
            g_configError = std::format("hl-device-add!: field '{}': {}", f.name, why);
            return std::nullopt;
        };

        if (f.kind == eDeviceKind::BOOL) {
            if (v == SCM_BOOL_T) return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(true))};
            if (v == SCM_BOOL_F) return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(false))};
            return fail("expected #t or #f");
        }
        if (f.kind == eDeviceKind::STRING) {
            if (!scm_is_string(v))
                return fail("expected a string");
            return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(schemeDatumToStr(v)))};
        }
        if (f.kind == eDeviceKind::INT || f.kind == eDeviceKind::FLOAT) {
            double d = 0;
            if (scm_is_integer(v)) d = (double)scm_to_int64(v);
            else if (hl::isInexact(v)) d = scm_to_double(v);
            else return fail("expected a number");
            if ((f.lo != 0 || f.hi != 0) && (d < f.lo || d > f.hi))
                return fail(std::format("out of range [{:.0g}, {:.0g}]", f.lo, f.hi).c_str());
            if (f.kind == eDeviceKind::INT)
                return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue((Config::INTEGER)d))};
            return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue((Config::FLOAT)d))};
        }
        // VEC2: (x . y) or (x y)
        if (!scm_is_pair(v))
            return fail("expected a coordinate pair");
        auto asNum = [](SCM p, double& out) -> bool {
            if (scm_is_integer(p)) { out = (double)scm_to_int64(p); return true; }
            if (hl::isInexact(p)) { out = scm_to_double(p); return true; }
            return false;
        };
        double x, y;
        SCM    tail = scm_cdr(v);
        if (scm_is_pair(tail)) {
            if (!asNum(scm_car(tail), y)) return fail("expected a coordinate pair");
        } else if (!asNum(tail, y))
            return fail("expected a coordinate pair");
        if (!asNum(scm_car(v), x)) return fail("expected a coordinate pair");
        return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(Config::VEC2(x, y)))};
    }

    // (hl-device-add! NAME . FIELDS)
    static int hlSchemeDeviceAdd(const char* name, SCM fields) {
        if (!g_up || !name || !*name) {
            g_configError = "hl-device-add!: a device name is required";
            return -1;
        }
        if (!scm_is_pair(fields)) {
            g_configError = "hl-device-add!: fields must be a plist, e.g. (hl-device-add! NAME 'enabled #t)";
            return -1;
        }

        std::string dev = name;
        std::replace(dev.begin(), dev.end(), ' ', '-');

        // validate + coerce every field first: a bad field writes nothing
        std::vector<std::pair<std::string, UP<CDeviceValue>>> values;
        SCM l = fields;
        while (scm_is_pair(l)) {
            if (!scm_is_pair(scm_cdr(l))) {
                g_configError = "hl-device-add!: odd plist of fields";
                return -1;
            }
            const std::string key = schemeDatumToStr(scm_car(l));
            const SDeviceField* f  = nullptr;
            for (const auto& F : DEVICE_FIELDS) {
                if (key == F.name) { f = &F; break; }
            }
            if (!f) {
                g_configError = std::format("hl-device-add!: unknown field '{}'", key);
                return -1;
            }
            auto v = deviceValue(*f, scm_car(scm_cdr(l)));
            if (!v)
                return -1;
            values.emplace_back(std::move(*v));
            l = scm_cdr(scm_cdr(l));
        }

        // Config::mgr() is the abstract interface; the device store lives on
        // the concrete manager (same cast used elsewhere in this file)
        auto* cmgr = sc<Config::Lua::CConfigManager*>(Config::mgr().get());
        auto& cfg  = cmgr->m_deviceConfigs[dev];
        for (auto& [k, val] : values)
            cfg.values.insert_or_assign(std::move(k), std::move(val));

        Config::Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_INPUT_DEVICES);
        return 0;
    }

    // ---- monitor rules ---------------------------------------------------------
    // mirrors the lua hl.monitor: fields parse into a CMonitorRuleParser seeded
    // from the existing rule, then commit to the rule manager and refresh.
    static UP<Config::CMonitorRuleParser> g_monitorParser;

    static int hlMonitorBegin(const char* output) {
        if (!g_up)
            return -1;
        if (!output || !*output) {
            g_configError = "hl-monitor-rule-add!: output name required";
            return -1;
        }
        g_monitorParser      = makeUnique<Config::CMonitorRuleParser>(std::string(output));
        const auto& all      = Config::monitorRuleMgr()->all();
        const auto  existing = std::ranges::find_if(all, [&output](const auto& rule) { return rule.m_name == output; });
        if (existing != all.end())
            g_monitorParser->rule() = *existing;
        return 0;
    }

    static int hlMonitorFieldStr(const char* field, const char* value) {
        if (!g_up || !g_monitorParser)
            return -1;
        const std::string f = field ? field : "";
        const std::string v = value ? value : "";
        auto&             p = *g_monitorParser;
        bool              ok;
        if (f == "mode")
            ok = p.parseMode(v);
        else if (f == "position")
            ok = p.parsePosition(v);
        else if (f == "scale")
            ok = p.parseScale(v);
        else if (f == "mirror") {
            p.setMirror(v);
            ok = true;
        } else if (f == "cm")
            ok = p.parseCM(v);
        else if (f == "icc")
            ok = p.parseICC(v);
        else if (f == "sdr_eotf") {
            p.rule().m_sdrEotf = NTransferFunction::fromString(v);
            ok                 = true;
        } else {
            g_configError = "hl-monitor-rule-add!: unknown string field '" + f + "'";
            return -1;
        }
        if (!ok)
            g_configError = p.getError() ? *p.getError() : "invalid value for '" + f + "'";
        return ok ? 0 : -1;
    }

    static int hlMonitorFieldNum(const char* field, double v) {
        if (!g_up || !g_monitorParser)
            return -1;
        const std::string f = field ? field : "";
        auto&             p = *g_monitorParser;

        // typed validation with the same ranges the lua config uses
        std::unique_ptr<Config::Lua::ILuaConfigValue> val;
        if (f == "transform")
            val.reset(new Config::Lua::CLuaConfigInt(0, std::optional<Config::INTEGER>(0), std::optional<Config::INTEGER>(7)));
        else if (f == "bitdepth")
            val.reset(new Config::Lua::CLuaConfigInt(8));
        else if (f == "vrr")
            val.reset(new Config::Lua::CLuaConfigInt(-1, std::optional<Config::INTEGER>(-1), std::optional<Config::INTEGER>(3)));
        else if (f == "supports_wide_color" || f == "supports_hdr")
            val.reset(new Config::Lua::CLuaConfigInt(0, std::optional<Config::INTEGER>(-1), std::optional<Config::INTEGER>(1)));
        else if (f == "sdr_max_luminance")
            val.reset(new Config::Lua::CLuaConfigInt(80));
        else if (f == "max_luminance" || f == "max_avg_luminance")
            val.reset(new Config::Lua::CLuaConfigInt(-1));
        else if (f == "sdrbrightness")
            val.reset(new Config::Lua::CLuaConfigFloat(1.F));
        else if (f == "sdrsaturation")
            val.reset(new Config::Lua::CLuaConfigFloat(1.F));
        else if (f == "sdr_min_luminance")
            val.reset(new Config::Lua::CLuaConfigFloat(0.2F));
        else if (f == "min_luminance")
            val.reset(new Config::Lua::CLuaConfigFloat(-1.F));
        else {
            g_configError = "hl-monitor-rule-add!: unknown numeric field '" + f + "'";
            return -1;
        }

        lua_State*      L = configScratch();
        lua_settop(L, 0);
        const bool isFloat = (f == "sdrbrightness" || f == "sdrsaturation" || f == "sdr_min_luminance" || f == "min_luminance");
        if (isFloat)
            lua_pushnumber(L, v);
        else
            lua_pushinteger(L, sc<long long>(v));
        const auto err = val->parse(L);
        lua_settop(L, 0);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK) {
            g_configError = err.message.empty() ? "invalid value" : err.message;
            return -2;
        }

        auto& rule = p.rule();
        if (f == "transform")
            rule.m_transform = sc<wl_output_transform>(sc<int>(v));
        else if (f == "bitdepth")
            rule.m_enable10bit = sc<int>(v) == 10;
        else if (f == "vrr")
            rule.m_vrr = sc<int>(v) < 0 ? std::nullopt : std::optional(sc<int>(v));
        else if (f == "supports_wide_color")
            rule.m_supportsWideColor = sc<int>(v);
        else if (f == "supports_hdr")
            rule.m_supportsHDR = sc<int>(v);
        else if (f == "sdr_max_luminance")
            rule.m_sdrMaxLuminance = sc<int>(v);
        else if (f == "max_luminance")
            rule.m_maxLuminance = sc<int>(v);
        else if (f == "max_avg_luminance")
            rule.m_maxAvgLuminance = sc<int>(v);
        else if (f == "sdrbrightness")
            rule.m_sdrBrightness = sc<float>(v);
        else if (f == "sdrsaturation")
            rule.m_sdrSaturation = sc<float>(v);
        else if (f == "sdr_min_luminance")
            rule.m_sdrMinLuminance = sc<float>(v);
        else if (f == "min_luminance")
            rule.m_minLuminance = sc<float>(v);
        return 0;
    }

    // gap fields (reserved / reserved_area): the value is pushed onto the
    // scratch stack by the config push helpers
    static int hlMonitorFieldGap(const char* field) {
        if (!g_up || !g_monitorParser)
            return -1;
        const std::string f = field ? field : "";
        if (f != "reserved" && f != "reserved_area") {
            g_configError = "hl-monitor-rule-add!: unknown gap field '" + f + "'";
            return -1;
        }
        Config::Lua::CLuaConfigCssGap gap(0);
        const auto                    err = gap.parse(configScratch());
        lua_settop(configScratch(), 0);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK) {
            g_configError = err.message.empty() ? "invalid reserved area" : err.message;
            return -2;
        }
        const auto& g = *sc<const Config::CCssGapData*>(gap.data());
        if (!g_monitorParser->setReserved(Desktop::CReservedArea(g.m_top, g.m_right, g.m_bottom, g.m_left))) {
            g_configError = "invalid reserved area";
            return -2;
        }
        return 0;
    }

    static int hlMonitorFieldBool(const char* field, int v) {
        if (!g_up || !g_monitorParser)
            return -1;
        const std::string f = field ? field : "";
        if (f != "disabled") {
            g_configError = "hl-monitor-rule-add!: unknown bool field '" + f + "'";
            return -1;
        }
        g_monitorParser->rule().m_disabled = (v != 0);
        return 0;
    }

    static int hlMonitorCommit() {
        if (!g_up || !g_monitorParser)
            return -1;
        Config::monitorRuleMgr()->add(std::move(g_monitorParser->rule()));
        g_monitorParser.reset();
        Supplementary::refresher()->scheduleRefresh(Supplementary::REFRESH_MONITOR_STATES);
        return 0;
    }

    static int hlCurveAdd(const char* name, int type, double a, double b, double c, double d) {
        if (!g_up)
            return -1;
        if (!name || !*name) {
            g_configError = "hl-curve-add!: name required";
            return -1;
        }
        if (type == 0)
            Animation::mgr()->addBezierWithName(name, Vector2D{a, b}, Vector2D{c, d});
        else if (type == 1) {
            if (a <= 0.5F || b <= 0.5F || c <= 0.5F) {
                g_configError = "hl-curve-add!: spring params must be >= 0.5";
                return -1;
            }
            Hyprutils::Animation::SSpringCurve curve;
            curve.stiffness = sc<float>(a);
            curve.damping   = sc<float>(b);
            curve.mass      = sc<float>(c);
            Animation::mgr()->addSpringWithName(name, curve);
        } else {
            g_configError = "hl-curve-add!: unknown type";
            return -1;
        }
        return 0;
    }

    static int hlAnimationSet(const char* leaf, int enabled, double speed, const char* curve, const char* style) {
        if (!g_up)
            return -1;
        if (!leaf || !*leaf) {
            g_configError = "hl-animation-add!: leaf required";
            return -1;
        }
        // an unknown leaf would be accepted here and crash the config
        // re-apply on the next reload — validate against the tree
        if (!Config::animationTree()->nodeExists(leaf)) {
            g_configError = "hl-animation-add!: unknown animation leaf '" + std::string(leaf) + "'";
            return -1;
        }
        const std::string cv = curve ? curve : "";
        const std::string sv = style ? style : "";
        if (!cv.empty() && !Animation::mgr()->bezierExists(cv) && !Animation::mgr()->springExists(cv)) {
            g_configError = "hl-animation-add!: curve '" + cv + "' is not defined (declare it with hl-curve-add!)";
            return -1;
        }
        if (!sv.empty()) {
            const auto err = Animation::mgr()->styleValidInConfigVar(leaf, sv);
            if (!err.empty()) {
                g_configError = err;
                return -1;
            }
        }
        Config::animationTree()->setConfigForNode(leaf, enabled != 0, sc<float>(speed), cv, sv);
        return 0;
    }

    // ---- permissions -----------------------------------------------------------
    // mirrors the lua hl.permission; only takes effect at first launch, like
    // upstream — permission rules require a compositor restart.
    static int hlPermissionAdd(const char* binary, const char* typeStr, const char* modeStr) {
        if (!g_up)
            return -1;
        auto* mgr = sc<Lua::CConfigManager*>(Config::mgr().get());
        if (!mgr || !mgr->isFirstLaunch()) {
            g_configError = "hl-permission-add!: permission rules only take effect at startup; set them in your config and restart";
            return -1;
        }
        if (!g_pDynamicPermissionManager) {
            g_configError = "hl-permission-add!: permission manager unavailable";
            return -1;
        }
        const std::string           t = typeStr ? typeStr : "";
        const std::string           m = modeStr ? modeStr : "";
        eDynamicPermissionType      type = PERMISSION_TYPE_UNKNOWN;
        eDynamicPermissionAllowMode mode = PERMISSION_RULE_ALLOW_MODE_UNKNOWN;
        if (t == "screencopy")
            type = PERMISSION_TYPE_SCREENCOPY;
        else if (t == "cursorpos")
            type = PERMISSION_TYPE_CURSOR_POS;
        else if (t == "plugin")
            type = PERMISSION_TYPE_PLUGIN;
        else if (t == "keyboard" || t == "keeb")
            type = PERMISSION_TYPE_KEYBOARD;
        else if (t == "input-capture")
            type = PERMISSION_TYPE_INPUT_CAPTURE;
        if (m == "ask")
            mode = PERMISSION_RULE_ALLOW_MODE_ASK;
        else if (m == "allow")
            mode = PERMISSION_RULE_ALLOW_MODE_ALLOW;
        else if (m == "deny")
            mode = PERMISSION_RULE_ALLOW_MODE_DENY;
        if (type == PERMISSION_TYPE_UNKNOWN || mode == PERMISSION_RULE_ALLOW_MODE_UNKNOWN) {
            g_configError = "hl-permission-add!: unknown type '" + t + "' or mode '" + m + "'";
            return -1;
        }
        g_pDynamicPermissionManager->addConfigPermissionRule(binary ? binary : "", type, mode);
        return 0;
    }

    static SCM hlConfigGet(const char* key) {
        if (!g_up)
            return SCM_BOOL_F;
        auto* val = configValueByKey(key);
        if (!val)
            return SCM_BOOL_F;
        lua_State* L = configScratch();
        val->push(L);

        std::vector<SCM> roots;
        SCM              result = SCM_BOOL_F;

        switch (lua_type(L, -1)) {
            case LUA_TNIL: break;
            case LUA_TBOOLEAN: result = lua_toboolean(L, -1) ? SCM_BOOL_T : SCM_BOOL_F; break;
            case LUA_TNUMBER: {
                const auto D = lua_tonumber(L, -1);
                result       = (long long)D == D ? scm_from_int64((long long)D) : scm_from_double(D);
                break;
            }
            case LUA_TSTRING: {
                const char* s = lua_tostring(L, -1);
                result        = scm_from_utf8_stringn(s, strlen(s));
                break;
            }
            case LUA_TTABLE: {
                // tables come back as a PLIST (key value key value ...) — the
                // same shape hl-config-add! accepts going in
                std::vector<SCM> elems;
                lua_pushnil(L);
                while (lua_next(L, -2) != 0) {
                    if (lua_type(L, -2) == LUA_TSTRING) {
                        const char* k = lua_tostring(L, -2);
                        SCM         keySym = scm_from_locale_symbol(k);
                        hl::pin(keySym, roots);
                        elems.push_back(keySym);
                    } else {
                        elems.push_back(scm_from_int64(lua_tointeger(L, -2)));
                    }

                    switch (lua_type(L, -1)) {
                        case LUA_TNUMBER: {
                            const auto D = lua_tonumber(L, -1);
                            SCM         v = (long long)D == D ? scm_from_int64((long long)D) : scm_from_double(D);
                            hl::pin(v, roots);
                            elems.push_back(v);
                            break;
                        }
                        case LUA_TSTRING: {
                            const char* s = lua_tostring(L, -1);
                            SCM         v = scm_from_utf8_stringn(s, strlen(s));
                            hl::pin(v, roots);
                            elems.push_back(v);
                            break;
                        }
                        case LUA_TBOOLEAN: elems.push_back(lua_toboolean(L, -1) ? SCM_BOOL_T : SCM_BOOL_F); break;
                        default: break;
                    }
                    lua_pop(L, 1);
                }
                result = SCM_EOL;
                for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                    result = scm_cons(*it, result);
                    hl::pin(result, roots);
                }
                break;
            }
            default: break;
        }

        lua_settop(L, 0);
        hl::unpin(roots);   // release just before returning; nothing allocates after
        return result;
    }

    // this family's Scheme-visible surface
    void registerConfig() {
        hl::bind<hlConfigBegin>("hl--c-config-begin");
        hl::bind<hlConfigPushNum>("hl--c-config-push-num");
        hl::bind<hlConfigPushInt>("hl--c-config-push-int");
        hl::bind<hlConfigPushBool>("hl--c-config-push-bool");
        hl::bind<hlConfigPushStr>("hl--c-config-push-str");
        hl::bind<hlConfigTblOpen>("hl--c-config-tbl-open");
        hl::bind<hlConfigTblKey>("hl--c-config-tbl-key");
        hl::bind<hlConfigTblSetHash>("hl--c-config-tbl-set-hash");
        hl::bind<hlConfigTblSeti>("hl--c-config-tbl-seti");
        hl::bind<hlConfigSet>("hl--c-config-set");
        hl::bind<hlConfigLastError>("hl--c-config-last-error");
        hl::bind<hlConfigGet>("hl--c-config-get");
        hl::bind<hlSchemeDeviceAdd>("hl--c-device-add");
        hl::bind<hlMonitorBegin>("hl--c-monitor-begin");
        hl::bind<hlMonitorFieldStr>("hl--c-monitor-field-str");
        hl::bind<hlMonitorFieldNum>("hl--c-monitor-field-num");
        hl::bind<hlMonitorFieldGap>("hl--c-monitor-field-gap");
        hl::bind<hlMonitorFieldBool>("hl--c-monitor-field-bool");
        hl::bind<hlMonitorCommit>("hl--c-monitor-commit");
        hl::bind<hlCurveAdd>("hl--c-curve-add");
        hl::bind<hlAnimationSet>("hl--c-animation-set");
        hl::bind<hlPermissionAdd>("hl--c-permission-add");
    }

} // namespace Config::Scheme
