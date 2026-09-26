/*
    The gesture family.

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

#include <src/helpers/math/Direction.hpp>
#include <src/managers/input/trackpad/TrackpadGestures.hpp>
#include <src/managers/input/trackpad/gestures/ITrackpadGesture.hpp>
#include <src/managers/input/trackpad/gestures/WorkspaceSwipeGesture.hpp>
#include <src/managers/input/trackpad/gestures/MoveGesture.hpp>
#include <src/managers/input/trackpad/gestures/ResizeGesture.hpp>
#include <src/managers/input/trackpad/gestures/CloseGesture.hpp>
#include <src/managers/input/trackpad/gestures/ScrollMoveGesture.hpp>
#include <src/managers/input/trackpad/gestures/FloatGesture.hpp>
#include <src/managers/input/trackpad/gestures/FullscreenGesture.hpp>
#include <src/managers/input/trackpad/gestures/SpecialWorkspaceGesture.hpp>
#include <src/managers/input/trackpad/gestures/CursorZoomGesture.hpp>

namespace Config::Scheme {

    using namespace Internals;

    // gesture event plist — upstream pushGestureEvent parity (LuaFunctionGesture
    // .cpp:42-122). The handler is APPLIED the plist fields, so callbacks
    // destructure them as normal lambda args:
    //   (lambda (phase direction type time-ms fingers delta . rest))   ; begin/update
    //   (lambda (phase direction type time-ms cancelled) ...)          ; end
    static SCM gestureEventPlist(std::vector<SCM>& roots, const char* phase, const std::string& dir,
                                 const char* type, uint32_t timeMs, std::optional<int> fingers, SCM deltaPair,
                                 SCM scale, SCM rotation, SCM cancelled) {
        std::vector<SCM> elems;
        auto push = [&](SCM p) { hl::pin(p, roots); elems.push_back(p); };
        push(scm_from_locale_symbol("phase"));      push(scm_from_utf8_stringn(phase, strlen(phase)));
        push(scm_from_locale_symbol("direction"));  push(scm_from_utf8_stringn(dir.c_str(), dir.size()));
        push(scm_from_locale_symbol("type"));       push(scm_from_utf8_stringn(type, strlen(type)));
        push(scm_from_locale_symbol("time-ms"));    elems.push_back(scm_from_int64((int)timeMs));
        if (fingers) { push(scm_from_locale_symbol("fingers")); elems.push_back(scm_from_int64(*fingers)); }
        if (!scm_is_null(deltaPair)) { push(scm_from_locale_symbol("delta")); push(deltaPair); }
        if (!scm_is_false(scale)) { push(scm_from_locale_symbol("scale")); push(scale); }
        if (!scm_is_false(rotation)) { push(scm_from_locale_symbol("rotation")); push(rotation); }
        if (!scm_is_false(cancelled)) { push(scm_from_locale_symbol("cancelled")); push(cancelled); }
        SCM l = SCM_EOL;
        for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
            l = scm_cons(*it, l);
            hl::pin(l, roots);
        }
        return l;
    }

    // bare-thunk variant: gestures carry their callbacks directly (SThunkRef
    // members), so the fire passes the callable itself; the plist is APPLIED
    // to it (spread args)
    static void fireSchemeList(SCM thunk, SCM lst) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        hl::call2(hl::globalRef("hl--fire-list"), thunk, lst);
        watchdogExit();
    }

    template <typename E>
    static void fireSchemeGestureEvent(SCM thunk, const char* phase, const E& e, const std::string& dir) {
        if (!g_up)
            return;
        std::vector<SCM>      roots;
        constexpr bool        IS_END = std::is_same_v<E, ITrackpadGesture::STrackpadGestureEnd>;
        SCM                   delta  = SCM_EOL, scale = SCM_BOOL_F, rotation = SCM_BOOL_F;
        if constexpr (!IS_END) {
            const auto& d = e.swipe ? e.swipe->delta : e.pinch->delta;
            SCM x = scm_from_double(d.x); hl::pin(x, roots);
            SCM y = scm_from_double(d.y); hl::pin(y, roots);
            delta = scm_cons(x, y); hl::pin(delta, roots);
            if (e.pinch) {
                scale = scm_from_double(e.pinch->scale); hl::pin(scale, roots);
                rotation = scm_from_double(e.pinch->rotation); hl::pin(rotation, roots);
            }
        }
        SCM cancelled = SCM_BOOL_F;
        if constexpr (IS_END)
            cancelled = (e.swipe ? e.swipe->cancelled : e.pinch->cancelled) ? SCM_BOOL_T : SCM_BOOL_F;
        std::optional<int> fingers;
        if constexpr (!IS_END)
            fingers = (int)(e.swipe ? e.swipe->fingers : e.pinch->fingers);
        SCM lst = gestureEventPlist(roots, phase, dir,
                                    e.swipe ? "swipe" : "pinch",
                                    e.swipe ? e.swipe->timeMs : e.pinch->timeMs,
                                    fingers,
                                    delta, scale, rotation, cancelled);
        hl::unpin(roots);
        fireSchemeList(thunk, lst);
    }

    class CSchemeGesture : public ITrackpadGesture {
      public:
        // the callbacks arrive as thunks and are carried locked; SCM_EOL means
        // "unused" (the counted lock no-ops on immediates). The gesture
        // manager destroys us at config reload, which unlocks them.
        CSchemeGesture(SCM begin, SCM update, SCM end, const char* direction) :
            m_begin(begin), m_update(update), m_end(end), m_direction(direction ? direction : "") {}

        void  begin(const STrackpadGestureBegin& e) override {
            if (!scm_is_null(m_begin.obj))
                fireSchemeGestureEvent(m_begin.obj, "start", e, m_direction);
        }
        void  update(const STrackpadGestureUpdate& e) override {
            if (!scm_is_null(m_update.obj))
                fireSchemeGestureEvent(m_update.obj, "update", e, m_direction);
        }
        void  end(const STrackpadGestureEnd& e) override {
            if (!scm_is_null(m_end.obj))
                fireSchemeGestureEvent(m_end.obj, "end", e, m_direction);
        }

      private:
        SThunkRef   m_begin, m_update, m_end;
        std::string m_direction;
    };

    // ---- gesture action recipes (upstream's hl.gesture action strings, done
    // as typed objects) ------------------------------------------------------
    // A recipe is a Scheme value (maker . args): maker is the address of one
    // of the stateless singleton factories below, args a flat plist with
    // symbol keys (the house plist shape). Built-in and custom actions are
    // indistinguishable to the caller — hl-gesture-add! asks the factory to
    // construct the ITrackpadGesture and moves it straight into the manager
    // (owned from birth, destroyed at reload). The recipe itself is a pure
    // value: registering it twice constructs two independent instances.
    static SCM gestureArgGet(SCM args, const char* key) {
        for (SCM l = args; scm_is_pair(l) && scm_is_pair(scm_cdr(l)); l = scm_cdr(scm_cdr(l)))
            if (scm_is_symbol(scm_car(l)) && schemeDatumToStr(scm_car(l)) == key)
                return scm_car(scm_cdr(l));
        return SCM_BOOL_F;
    }

    static std::string gestureArgStr(SCM args, const char* key) {
        // symbol (mode tags) or string (special workspace name) values
        const SCM v = gestureArgGet(args, key);
        return (scm_is_symbol(v) || scm_is_string(v)) ? schemeDatumToStr(v) : std::string();
    }

    static double gestureArgDouble(SCM args, const char* key) {
        const SCM v = gestureArgGet(args, key);
        return hl::isInexact(v) ? scm_to_double(v) : 1.0;
    }

    class IGestureMaker {
      public:
        virtual UP<ITrackpadGesture> make(SCM args, eTrackpadGestureDirection dir) = 0;
        virtual ~IGestureMaker() = default;
    };

    // the five no-argument built-ins
    template <typename G>
    class CTrivialGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(SCM, eTrackpadGestureDirection) override {
            return makeUnique<G>();
        }
    };

    static CTrivialGestureMaker<CWorkspaceSwipeGesture>     s_workspaceSwipeGestureMaker;

    static CTrivialGestureMaker<CMoveTrackpadGesture>       s_moveGestureMaker;

    static CTrivialGestureMaker<CResizeTrackpadGesture>     s_resizeGestureMaker;

    static CTrivialGestureMaker<CCloseTrackpadGesture>      s_closeGestureMaker;

    static CTrivialGestureMaker<CScrollMoveTrackpadGesture> s_scrollMoveGestureMaker;

    class CFloatGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(SCM args, eTrackpadGestureDirection) override {
            return makeUnique<CFloatTrackpadGesture>(gestureArgStr(args, "mode"));
        }
    };

    static CFloatGestureMaker s_floatGestureMaker;

    class CFullscreenGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(SCM args, eTrackpadGestureDirection) override {
            return makeUnique<CFullscreenTrackpadGesture>(gestureArgStr(args, "mode"));
        }
    };

    static CFullscreenGestureMaker s_fullscreenGestureMaker;

    class CSpecialWorkspaceGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(SCM args, eTrackpadGestureDirection) override {
            return makeUnique<CSpecialWorkspaceGesture>(gestureArgStr(args, "name"));
        }
    };

    static CSpecialWorkspaceGestureMaker s_specialWorkspaceGestureMaker;

    class CCursorZoomGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(SCM args, eTrackpadGestureDirection) override {
            // the underlying ctor parses a zoom string; format our typed number
            return makeUnique<CCursorZoomTrackpadGesture>(std::format("{}", gestureArgDouble(args, "zoom")), gestureArgStr(args, "mode"));
        }
    };

    static CCursorZoomGestureMaker s_cursorZoomGestureMaker;

    // custom: the args are the three thunks ((start . T) (update . T)

    // (finish . T)); CSchemeGesture locks them into SThunkRef members
    class CCustomGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(SCM args, eTrackpadGestureDirection dir) override {
            const auto toNil = [](SCM p) { return p == SCM_BOOL_F ? SCM_EOL : p; };
            return makeUnique<CSchemeGesture>(toNil(gestureArgGet(args, "start")), toNil(gestureArgGet(args, "update")),
                                              toNil(gestureArgGet(args, "finish")), g_pTrackpadGestures->stringForDir(dir));
        }
    };

    static CCustomGestureMaker s_customGestureMaker;

    // validate an inbound recipe's maker slot: only our ten singletons are

    // legal values (rejects forged pairs and other handle families' integers)
    static IGestureMaker* gestureMakerFromAddress(long long addr) {
        IGestureMaker* makers[] = {&s_workspaceSwipeGestureMaker, &s_moveGestureMaker,       &s_resizeGestureMaker,
                                   &s_closeGestureMaker,          &s_scrollMoveGestureMaker, &s_floatGestureMaker,
                                   &s_fullscreenGestureMaker,     &s_specialWorkspaceGestureMaker,
                                   &s_cursorZoomGestureMaker,     &s_customGestureMaker};
        for (auto* m : makers)
            if (addr == sc<long long>(reinterpret_cast<intptr_t>(m)))
                return m;
        return nullptr;
    }

    // (recipe, fingers, direction, mods, scale, disableInhibit) → 0 ok;
    // recipe is (maker . args) — the maker constructs the ITrackpadGesture
    // (built-in or custom alike), which moves into the manager, owned from
    // birth; destroyed at config reload, which also unlocks any thunks it
    // carried. The addGesture result (overshadow rules) is checked.
    static int hlSchemeGesture(SCM recipe, int fingers, const char* direction, SCM mods, double scale, int disableInhibit) {
        if (!g_up || !g_pTrackpadGestures)
            return -1;
        const auto dir = g_pTrackpadGestures->dirForString(direction ? direction : "");
        if (dir == TRACKPAD_GESTURE_DIR_NONE) {
            g_configError = std::string("hl-gesture: invalid direction '") + (direction ? direction : "") + "'";
            return -1;
        }
        const auto mask = modsMaskFromTokens(mods);
        if (!mask)
            return -1;
        if (!scm_is_pair(recipe) || !scm_is_integer(scm_car(recipe))) {
            g_configError = "hl-gesture: 'action is not a gesture action (see the hl-make-*-gesture constructors)";
            return -1;
        }
        const auto maker = gestureMakerFromAddress(scm_to_int64(scm_car(recipe)));
        if (!maker) {
            g_configError = "hl-gesture: 'action is not a valid gesture action (see the hl-make-*-gesture constructors)";
            return -1;
        }
        auto gesture = maker->make(scm_cdr(recipe), dir);
        const auto result =
            g_pTrackpadGestures->addGesture(std::move(gesture), sc<size_t>(fingers), dir, *mask, sc<float>(scale), disableInhibit != 0);
        if (!result) {
            g_configError = std::string("hl-gesture: ") + result.error();
            return -1;
        }
        return 0;
    }

    // one accessor per maker — the recipe's maker slot is fetched by the
    // hl-make-* constructor that owns it; no dispatch anywhere (adding a
    // gesture = a subclass + a singleton + one of these one-liners)
    static long long makerAddr(IGestureMaker* m) {
        return sc<long long>(reinterpret_cast<intptr_t>(m));
    }

    static long long hlSchemeGestureMakerWorkspaceSwipe() { return makerAddr(&s_workspaceSwipeGestureMaker); }

    static long long hlSchemeGestureMakerMove()           { return makerAddr(&s_moveGestureMaker); }

    static long long hlSchemeGestureMakerResize()         { return makerAddr(&s_resizeGestureMaker); }

    static long long hlSchemeGestureMakerClose()          { return makerAddr(&s_closeGestureMaker); }

    static long long hlSchemeGestureMakerScrollMove()     { return makerAddr(&s_scrollMoveGestureMaker); }

    static long long hlSchemeGestureMakerFloat()          { return makerAddr(&s_floatGestureMaker); }

    static long long hlSchemeGestureMakerFullscreen()     { return makerAddr(&s_fullscreenGestureMaker); }

    static long long hlSchemeGestureMakerSpecial()        { return makerAddr(&s_specialWorkspaceGestureMaker); }

    static long long hlSchemeGestureMakerCursorZoom()     { return makerAddr(&s_cursorZoomGestureMaker); }

    static long long hlSchemeGestureMakerCustom()         { return makerAddr(&s_customGestureMaker); }

    // (fingers, direction, mods, scale, disableInhibit) → 0 removed / 1 no
    // such gesture / -1 error. removeGesture matches on the registration
    // spec (the manager stores one gesture per spec), never on the action.
    static int hlSchemeGestureRemove(int fingers, const char* direction, SCM mods, double scale, int disableInhibit) {
        if (!g_up || !g_pTrackpadGestures)
            return -1;
        const auto dir = g_pTrackpadGestures->dirForString(direction ? direction : "");
        if (dir == TRACKPAD_GESTURE_DIR_NONE) {
            g_configError = std::string("hl-gesture: invalid direction '") + (direction ? direction : "") + "'";
            return -1;
        }
        const auto mask = modsMaskFromTokens(mods);
        if (!mask)
            return -1;
        const auto result = g_pTrackpadGestures->removeGesture(sc<size_t>(fingers), dir, *mask, sc<float>(scale), disableInhibit != 0);
        if (!result) {
            if (result.error() == "Can't remove a non-existent gesture")
                return 1;
            g_configError = std::string("hl-gesture: ") + result.error();
            return -1;
        }
        return 0;
    }

    // this family's Scheme-visible surface
    void registerGesture() {
        hl::bind<hlSchemeGesture>("hl--c-gesture");
        hl::bind<hlSchemeGestureMakerWorkspaceSwipe>("hl--c-gesture-maker-workspace-swipe");
        hl::bind<hlSchemeGestureMakerMove>("hl--c-gesture-maker-move");
        hl::bind<hlSchemeGestureMakerResize>("hl--c-gesture-maker-resize");
        hl::bind<hlSchemeGestureMakerClose>("hl--c-gesture-maker-close");
        hl::bind<hlSchemeGestureMakerScrollMove>("hl--c-gesture-maker-scroll-move");
        hl::bind<hlSchemeGestureMakerFloat>("hl--c-gesture-maker-float");
        hl::bind<hlSchemeGestureMakerFullscreen>("hl--c-gesture-maker-fullscreen");
        hl::bind<hlSchemeGestureMakerSpecial>("hl--c-gesture-maker-special");
        hl::bind<hlSchemeGestureMakerCursorZoom>("hl--c-gesture-maker-cursor-zoom");
        hl::bind<hlSchemeGestureMakerCustom>("hl--c-gesture-maker-custom");
        hl::bind<hlSchemeGestureRemove>("hl--c-gesture-remove");
    }

} // namespace Config::Scheme
