#pragma once

#include <src/layout/algorithm/TiledAlgorithm.hpp>
#include "SThunkRef.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Config::Scheme::Layouts {

    /*
        Pure-function layouts. A scheme layout is ONE function:

            (count W H) -> ((x y w h) ...)

        called with the usable work area; it returns one box per target.
        No state, no context object, no per-target identity — see the design
        note in SchemeManager.cpp. Selected via `layout = scheme:NAME`.
        On any error the layout is replaced by a default grid for the rest of
        the generation (sticky didError, mirroring the Lua provider).

        CAVEAT: unlike the Lua provider there is NO watchdog timeout. An
        infinite loop in a layout function freezes the compositor.
    */

    struct SSchemeLayoutProvider {
        std::string name;  // "scheme:NAME"
        bool        active = true;
        bool        didError = false;
        // the callback spec plist, LOCKED (SThunkRef.hpp): the provider is
        // destroyed at Layouts::clear(), which
        // unlocks it; the callbacks travel with the provider, no registry
        SThunkRef spec;   // locked; unlocked when the provider is destroyed
    };

    class CSchemeTiledAlgorithm : public Layout::ITiledAlgorithm {
      public:
        explicit CSchemeTiledAlgorithm(SP<SSchemeLayoutProvider> provider);

        virtual void                       newTarget(SP<Layout::ITarget> target);
        virtual void                       movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt);
        virtual void                       removeTarget(SP<Layout::ITarget> target);
        virtual void                       resizeTarget(const Vector2D& delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE);
        virtual void                       recalculate(Layout::eRecalculateReason reason = Layout::RECALCULATE_REASON_UNKNOWN);
        virtual void                       swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b);
        virtual void                       moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection dir, bool silent);
        virtual SP<Layout::ITarget>        getNextCandidate(SP<Layout::ITarget> old);
        virtual std::optional<std::string> layoutName() const;
        virtual Config::ErrorResult        layoutMsg(const std::string_view& sv);

      private:
        SP<SSchemeLayoutProvider>        m_provider;
        std::vector<WP<Layout::ITarget>> m_targets;

        std::vector<SP<Layout::ITarget>> liveTargets();
        bool                             callLayout(const std::vector<SP<Layout::ITarget>>& targets);
        void                             applyDefaultGrid(const std::vector<SP<Layout::ITarget>>& targets);
        void                             reportError(const std::string& message);
    };

    // called once after the scheme heap is built: registers hl-scheme-layout-add
    void registerSymbols();
    // called on config reload: deactivate + unregister all scheme layouts
    void clear();
}
