#pragma once

#include <src/layout/algorithm/TiledAlgorithm.hpp>
#include "SThunkRef.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Config::Scheme::Layouts {

    /*
        Layouts as callbacks. A scheme layout's recalculate callback is:

            (area placements) -> ((window . box) ...)

        It is handed the work AREA (an hl-box in global coordinates) and the
        current PLACEMENTS — one (window . box) pair per window to place, each
        box being where that window is NOW — and returns the same shape: the
        windows to move, and where. A window left out keeps its geometry, so a
        partial return is legal, and the result is matched BY WINDOW rather than
        by position, so a reordered return is either applied correctly or
        rejected — never silently mis-placed. A target with no window is
        skipped. `resize` is the same with (dx dy corner) appended, and may
        return #f to ask for a plain recalculate instead.

        A "box" is the CELL: the compositor insets the window inside it by the
        border and gaps_in, exactly as it does for the built-in layouts. See the
        design note in the layout entry points (SchemeLayout.cpp).

        Selected via `layout = scheme:NAME`. A failed pass — an error, or a
        watchdog abort — falls back to a default grid for THAT pass only: the
        next recalculate tries the layout again, so a transient failure recovers.
        didError is not "gave up", it is "the error has been shown once", which
        keeps a broken layout from posting a notification on every pass.
    */

    struct SSchemeLayoutProvider {
        std::string name;  // "scheme:NAME"
        bool        active = true;
        bool        didError = false; // the error notification has been shown
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
