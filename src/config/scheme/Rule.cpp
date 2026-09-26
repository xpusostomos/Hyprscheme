/*
    The rule family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"

#include <src/desktop/view/window/Window.hpp>
#include <src/desktop/rule/Engine.hpp>
#include <src/desktop/rule/Rule.hpp>
#include <src/desktop/rule/RuleWithEffects.hpp>
#include <src/desktop/rule/layerRule/LayerRule.hpp>
#include <src/desktop/rule/windowRule/WindowRule.hpp>
#include <src/config/lua/types/LuaConfigCssGap.hpp>
#include <src/config/supplementary/propRefresher/PropRefresher.hpp>
#include <src/config/shared/workspace/WorkspaceRule.hpp>
#include <src/config/shared/workspace/WorkspaceRuleManager.hpp>

namespace Config::Scheme {

    using namespace Internals;

    static std::unordered_map<std::string, SP<Desktop::Rule::CWindowRule>> g_windowRules;

    static std::unordered_map<std::string, SP<Desktop::Rule::CLayerRule>>  g_layerRules;
    static std::vector<SP<Desktop::Rule::CWindowRule>>                     g_anonWindowRules;
    static std::vector<SP<Desktop::Rule::CLayerRule>>                      g_anonLayerRules;
    static SP<Desktop::Rule::CWindowRule>                                  g_curWindowRule;
    static SP<Desktop::Rule::CLayerRule>                                   g_curLayerRule;
    static std::optional<Config::CWorkspaceRule>                           g_curWorkspaceRule;
    // rule handles: record address -> the rule, with the record LOCKED in the
    // entry (SThunkRef) — no user capture keeps a rule handle alive, so the
    // index holds the lock. Entries erase at the generation boundary only:
    // rules live for their config generation, so that IS their lifetime.
    static std::unordered_map<uintptr_t, std::pair<SP<Desktop::Rule::IRule>, SThunkRef>> g_ruleIndex;
    // called from reloadScheme: the config reload cleared the engine's rules
    void clearSchemeRules() {
        for (const auto& r : g_anonWindowRules)
            Desktop::Rule::ruleEngine()->unregisterRule(SP<Desktop::Rule::IRule>(r));
        for (const auto& r : g_anonLayerRules)
            Desktop::Rule::ruleEngine()->unregisterRule(SP<Desktop::Rule::IRule>(r));
        g_windowRules.clear();
        g_layerRules.clear();
        g_anonWindowRules.clear();
        g_anonLayerRules.clear();
        g_curWindowRule.reset();
        g_curLayerRule.reset();
        g_curWorkspaceRule.reset();
        g_ruleIndex.clear();
    }

    static int hlWindowRuleBegin(const char* name, int enabled) {
        if (!g_up)
            return -1;
        const std::string              n = name ? name : "";
        SP<Desktop::Rule::CWindowRule> rule;
        const auto                     it = g_windowRules.find(n);
        if (!n.empty() && it != g_windowRules.end())
            rule = it->second;
        else {
            rule = makeShared<Desktop::Rule::CWindowRule>(n);
            if (!n.empty())
                g_windowRules.emplace(n, rule);
            else
                g_anonWindowRules.emplace_back(rule);
            Desktop::Rule::ruleEngine()->registerRule(SP<Desktop::Rule::IRule>(rule));
        }
        rule->setEnabled(enabled != 0);
        g_curWindowRule = rule;
        return 0;
    }

    static int hlLayerRuleBegin(const char* name, int enabled) {
        if (!g_up)
            return -1;
        const std::string              n = name ? name : "";
        SP<Desktop::Rule::CLayerRule>  rule;
        const auto                     it = g_layerRules.find(n);
        if (!n.empty() && it != g_layerRules.end())
            rule = it->second;
        else {
            rule = makeShared<Desktop::Rule::CLayerRule>(n);
            if (!n.empty())
                g_layerRules.emplace(n, rule);
            else
                g_anonLayerRules.emplace_back(rule);
            Desktop::Rule::ruleEngine()->registerRule(SP<Desktop::Rule::IRule>(rule));
        }
        rule->setEnabled(enabled != 0);
        g_curLayerRule = rule;
        return 0;
    }

    static int hlRuleMatch(const char* prop, const char* value) {
        if (!g_up)
            return -1;
        const auto p = Desktop::Rule::matchPropFromString(prop ? prop : "");
        if (!p) {
            g_configError = std::string("unknown match property '") + (prop ? prop : "") + "'";
            return -1;
        }
        if (g_curWindowRule) {
            g_curWindowRule->registerMatch(*p, value ? value : "");
            return 0;
        }
        if (g_curLayerRule) {
            g_curLayerRule->registerMatch(*p, value ? value : "");
            return 0;
        }
        return -1;
    }

    static int hlWindowRuleEffect(const char* effect, const char* value) {
        if (!g_up || !g_curWindowRule)
            return -1;
        const auto e = Desktop::Rule::windowEffects()->get(std::string_view(effect ? effect : ""));
        if (!e) {
            g_configError = std::string("unknown effect '") + (effect ? effect : "") + "'";
            return -1;
        }
        const auto res = g_curWindowRule->addEffect(*e, value ? value : "");
        if (!res) {
            g_configError = res.error();
            return -2;
        }
        return 0;
    }

    static int hlLayerRuleEffect(const char* effect, const char* value) {
        if (!g_up || !g_curLayerRule)
            return -1;
        const auto e = Desktop::Rule::layerEffects()->get(std::string_view(effect ? effect : ""));
        if (!e) {
            g_configError = std::string("unknown layer effect '") + (effect ? effect : "") + "'";
            return -1;
        }
        const auto res = g_curLayerRule->addEffect(*e, value ? value : "");
        if (!res) {
            g_configError = res.error();
            return -2;
        }
        return 0;
    }

    static int hlWindowRuleCommit(SCM record) {
        if (!g_up || !g_curWindowRule)
            return -1;
        g_ruleIndex.emplace(SCM_UNPACK(record),
                            std::make_pair(g_curWindowRule, SThunkRef(record)));
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_WINDOW_STATES);
        g_curWindowRule.reset();
        return 0;
    }

    static int hlLayerRuleCommit(SCM record) {
        if (!g_up || !g_curLayerRule)
            return -1;
        g_ruleIndex.emplace(SCM_UNPACK(record),
                            std::make_pair(g_curLayerRule, SThunkRef(record)));
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_RULES);
        g_curLayerRule.reset();
        return 0;
    }

    static int hlRuleSetEnabled(SCM record, int enabled) {
        if (!g_up)
            return -1;
        const auto it = g_ruleIndex.find(SCM_UNPACK(record));
        if (it == g_ruleIndex.end())
            return -1;
        it->second.first->setEnabled(enabled != 0);
        return 0;
    }

    static int hlRuleEnabled(SCM record) {
        const auto it = g_ruleIndex.find(SCM_UNPACK(record));
        return (it != g_ruleIndex.end() && it->second.first->isEnabled()) ? 1 : 0;
    }

    static int hlWorkspaceRuleBegin(const char* ws, int enabled) {
        if (!g_up)
            return -1;
        if (!ws || !*ws) {
            g_configError = "hl-workspace-rule-add!: workspace selector required";
            return -1;
        }
        g_curWorkspaceRule = Config::CWorkspaceRule{};
        g_curWorkspaceRule->m_workspaceString = ws;
        g_curWorkspaceRule->setEnabled(enabled != 0);
        return 0;
    }

    static int hlWorkspaceRuleStr(const char* field, const char* v) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        const std::string f = field ? field : "";
        auto&             r = *g_curWorkspaceRule;
        if (f == "monitor")
            r.m_monitor = v ? v : "";
        else if (f == "on_created_empty")
            r.m_onCreatedEmptyRunCmd = v ? v : "";
        else if (f == "default_name")
            r.m_defaultName = v ? v : "";
        else if (f == "layout")
            r.m_layout = v ? v : "";
        else if (f == "animation")
            r.m_animationStyle = v ? v : "";
        else {
            g_configError = "unknown workspace-rule field '" + f + "'";
            return -1;
        }
        return 0;
    }

    static int hlWorkspaceRuleNum(const char* field, double v) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        const std::string f = field ? field : "";
        if (f == "border_size")
            g_curWorkspaceRule->m_borderSize = sc<int64_t>(v);
        else {
            g_configError = "unknown workspace-rule numeric field '" + f + "'";
            return -1;
        }
        return 0;
    }

    static int hlWorkspaceRuleBool(const char* field, int v) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        const std::string f = field ? field : "";
        auto&             r = *g_curWorkspaceRule;
        if (f == "default")
            r.m_isDefault = (v != 0);
        else if (f == "persistent")
            r.m_isPersistent = (v != 0);
        else if (f == "no_border")
            r.m_noBorder = (v != 0);
        else if (f == "no_rounding")
            r.m_noRounding = (v != 0);
        else if (f == "decorate")
            r.m_decorate = (v != 0);
        else if (f == "no_shadow")
            r.m_noShadow = (v != 0);
        else {
            g_configError = "unknown workspace-rule bool field '" + f + "'";
            return -1;
        }
        return 0;
    }

    static int hlWorkspaceRuleGap(const char* field) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        const std::string f = field ? field : "";
        Config::Lua::CLuaConfigCssGap gap(0);
        const auto                    err = gap.parse(configScratch());
        lua_settop(configScratch(), 0);
        if (err.errorCode != Config::Lua::PARSE_ERROR_OK) {
            g_configError = err.message.empty() ? "invalid gaps" : err.message;
            return -2;
        }
        const auto& g = *sc<const Config::CCssGapData*>(gap.data());
        auto&       r = *g_curWorkspaceRule;
        if (f == "gaps_in")
            r.m_gapsIn = g;
        else if (f == "gaps_out")
            r.m_gapsOut = g;
        else if (f == "float_gaps")
            r.m_floatGaps = g;
        else {
            g_configError = "unknown workspace-rule gap field '" + f + "'";
            return -1;
        }
        return 0;
    }

    static int hlWorkspaceRuleLayoutOpt(const char* k, const char* v) {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        g_curWorkspaceRule->m_layoutopts[k ? k : ""] = v ? v : "";
        return 0;
    }

    static int hlWorkspaceRuleCommit() {
        if (!g_up || !g_curWorkspaceRule)
            return -1;
        Config::workspaceRuleMgr()->replaceOrAdd(std::move(*g_curWorkspaceRule));
        g_curWorkspaceRule.reset();
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_MONITOR_STATES | Config::Supplementary::REFRESH_WINDOW_STATES);
        return 0;
    }

    // this family's Scheme-visible surface
    void registerRule() {
        hl::bind<hlWindowRuleBegin>("hl--c-window-rule-begin");
        hl::bind<hlLayerRuleBegin>("hl--c-layer-rule-begin");
        hl::bind<hlRuleMatch>("hl--c-rule-match");
        hl::bind<hlWindowRuleEffect>("hl--c-window-rule-effect");
        hl::bind<hlLayerRuleEffect>("hl--c-layer-rule-effect");
        hl::bind<hlWindowRuleCommit>("hl--c-window-rule-commit");
        hl::bind<hlLayerRuleCommit>("hl--c-layer-rule-commit");
        hl::bind<hlRuleSetEnabled>("hl--c-rule-set-enabled");
        hl::bind<hlRuleEnabled>("hl--c-rule-enabled");
        hl::bind<hlWorkspaceRuleBegin>("hl--c-workspace-rule-begin");
        hl::bind<hlWorkspaceRuleStr>("hl--c-workspace-rule-str");
        hl::bind<hlWorkspaceRuleNum>("hl--c-workspace-rule-num");
        hl::bind<hlWorkspaceRuleBool>("hl--c-workspace-rule-bool");
        hl::bind<hlWorkspaceRuleGap>("hl--c-workspace-rule-gap");
        hl::bind<hlWorkspaceRuleLayoutOpt>("hl--c-workspace-rule-layout-opt");
        hl::bind<hlWorkspaceRuleCommit>("hl--c-workspace-rule-commit");
    }

} // namespace Config::Scheme
