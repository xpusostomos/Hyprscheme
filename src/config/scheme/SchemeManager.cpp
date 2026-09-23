#ifndef HYPRTHEME_SCHEME_H_SEEN
#define HYPRTHEME_SCHEME_H_SEEN
#include <scheme.h>
#endif
#include "SThunkRef.hpp"
#include "SchemeManager.hpp"
#include "SchemeLayout.hpp"
#include "SchemeInternals.hpp"



#include <src/debug/log/Logger.hpp>
#include <src/debug/crash/CrashReporter.hpp>
#include <src/event/EventBus.hpp>
#include <src/helpers/memory/Memory.hpp>
#include <src/helpers/math/Direction.hpp>
#include <src/input/Keys.hpp>
#include <src/ipc/s1/S1.hpp>
#include <src/ipc/s2/S2.hpp>
#include <src/keybinds/Manager.hpp>
#include <src/keybinds/InputState.hpp>
#include <src/managers/eventLoop/EventLoopManager.hpp>
#include <src/managers/eventLoop/EventLoopTimer.hpp>
#include <src/managers/fullscreen/FullscreenController.hpp>
#include <src/managers/fullscreen/FullscreenTypes.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/pointer/PointerManager.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/state/WindowState.hpp>
#include <src/desktop/view/Group.hpp>
#include <src/desktop/view/LayerSurface.hpp>
#include <src/desktop/view/window/WindowPresentation.hpp>
#include <src/desktop/state/ViewState.hpp>
#include <src/desktop/history/WindowHistoryTracker.hpp>
#include <src/desktop/history/WorkspaceHistoryTracker.hpp>
#include <src/layout/algorithm/tiled/master/MasterAlgorithm.hpp>
#include <src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp>
#include <src/protocols/types/ContentType.hpp>
#include <src/desktop/view/window/Window.hpp>
#include <src/desktop/view/window/WindowFullscreenPolicy.hpp>
#include <src/desktop/view/window/WindowGroupMembership.hpp>
#include <src/desktop/view/window/WindowSwallowController.hpp>
#include <src/desktop/reserved/ReservedArea.hpp>
#include <src/desktop/rule/Engine.hpp>
#include <src/desktop/rule/Rule.hpp>
#include <src/desktop/rule/RuleWithEffects.hpp>
#include <src/desktop/rule/layerRule/LayerRule.hpp>
#include <src/desktop/rule/layerRule/LayerRuleApplicator.hpp>
#include <src/desktop/view/LayerSurface.hpp>
#include <src/notification/Notification.hpp>
#include <src/notification/NotificationOverlay.hpp>
#include <src/managers/input/trackpad/TrackpadGestures.hpp>
#include <src/managers/input/trackpad/gestures/ITrackpadGesture.hpp>
#include <src/managers/input/trackpad/gestures/WorkspaceSwipeGesture.hpp>
#include <src/managers/input/trackpad/gestures/MoveGesture.hpp>
#include <src/managers/input/trackpad/gestures/ResizeGesture.hpp>
#include <src/managers/input/trackpad/gestures/CloseGesture.hpp>
#include <src/managers/input/trackpad/gestures/FloatGesture.hpp>
#include <src/managers/input/trackpad/gestures/FullscreenGesture.hpp>
#include <src/managers/input/trackpad/gestures/SpecialWorkspaceGesture.hpp>
#include <src/managers/input/trackpad/gestures/CursorZoomGesture.hpp>
#include <src/managers/input/trackpad/gestures/ScrollMoveGesture.hpp>
#include <src/state/MonitorState.hpp>
#include <src/state/WorkspaceState.hpp>
#include <src/state/workspace/Resolver.hpp>
#include <src/workspace/HLWorkspace.hpp>
#include <src/workspace/RegularWorkspace.hpp>
#include <src/workspace/query/Query.hpp>
#include <src/layout/supplementary/WorkspaceAlgoMatcher.hpp>
#include <src/layout/space/Space.hpp>
#include <src/layout/algorithm/Algorithm.hpp>
#include <src/config/ConfigManager.hpp>
#include <src/config/lua/ConfigManager.hpp>
#include <src/config/lua/types/LuaConfigValue.hpp>
#include <aquamarine/backend/Backend.hpp>
#include <src/config/lua/types/LuaConfigBool.hpp>
#include <src/config/lua/types/LuaConfigCssGap.hpp>
#include <src/config/lua/types/LuaConfigFloat.hpp>
#include <src/config/lua/types/LuaConfigInt.hpp>
#include <src/config/lua/types/LuaConfigString.hpp>
#include <src/config/shared/actions/ConfigActions.hpp>
#include <src/config/shared/animation/AnimationTree.hpp>
#include <src/config/shared/monitor/Parser.hpp>
#include <src/config/shared/monitor/MonitorRuleManager.hpp>
#include <src/config/shared/workspace/WorkspaceRule.hpp>
#include <src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <src/config/supplementary/executor/Executor.hpp>
#include <src/config/supplementary/propRefresher/PropRefresher.hpp>
#include <src/config/lua/types/LuaConfigValue.hpp>
#include <src/animation/AnimationManager.hpp>
#include <src/pointer/PointerManager.hpp>
#include <src/plugins/PluginSystem.hpp>
#include <xkbcommon/xkbcommon.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstring>
#include <regex>
#include <format>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <vector>

using namespace Config::Scheme::Internals;
using namespace Hyprutils::OS;
using Hyprutils::OS::CFileDescriptor;

/*
    Bootstrap, evaluated by the interpreter before the user config is loaded.

    Loaded in two phases:
      1. SCHEME_PRELUDE is loaded unguarded. It only contains the error
         plumbing (verified, static); an error here means the embedded
         error handler exits — the same failure mode as before.
      2. SCHEME_BOOTSTRAP is loaded *through* the prelude's guarded hl--load.
         Any error prints and leaves hl--ready #f; the C side then disables
         scheme instead of continuing half-initialized.

    Both phases evaluate into the PERSISTENT environment (the interpreter's
    interaction environment), which holds the API and the cross-reload
    state forever. Each config generation is then evaluated in its own
    FRESH environment — a copy made by hl--reset — so definitions from
    previous generations cannot leak into new ones (mirroring upstream's
    per-generation lua_State). Machinery variables the config may set!
    (hl--watchdog-ms) are read through the generation copy so overrides
    reach them; hl--state is the one deliberate cross-generation bridge.
*/

static constexpr const char* SCHEME_PRELUDE = R"scm(
;; the default embedded error handler calls exit on uncaught exceptions,
;; so everything user- or config-supplied must run under these guards.
(define hl--ready #f)

;; the current config generation's environment, or #f before the first
;; hl--reset (the bootstrap load targets the persistent interaction
;; environment). hl--reset installs a fresh copy per generation so user
;; definitions from previous generations cannot leak into new ones.
(define hl--generation #f)

(define (hl--report e)
  (display "[scheme] error: " (current-error-port))
  (display-condition e (current-error-port))
  (newline (current-error-port))
  #f)

;; ---- watchdog ---------------------------------------------------------------
;; callbacks run on the compositor's main loop: a hung callback freezes the
;; desktop (we cannot kill a Chez call from outside). This wrapper bounds
;; them instead: a Chez timer interrupt fires periodically; the handler
;; checks WALL-CLOCK time (ticks are work units, not seconds) and escapes
;; out of the callback when the budget is exceeded — recovery, not just
;; detection. Abandonment semantics match the Lua watchdog: partial effects
;; stay, the callback is never resumed. Foreign calls are not interruptible,
;; so a callback blocked INSIDE a C call escapes only when it returns to
;; Scheme; the C-side detector thread is the backstop for those.

(define hl--watchdog-ms 5000)   ; set! from your config; 0 disables
(define hl--wd-rearm 10000)     ; timer budget between wall-clock checks
(define hl--wd-stack '())
;; unique marker for "the watchdog abandoned this run"
(define hl--wd-aborted (cons 'watchdog 'aborted))

(define (hl--ms-since t0)
  (quotient (time-nanosecond (time-difference (current-time) t0)) 1000000))

(define (hl--wd-enter what escape)
  ;; budget + t0 + escape are captured in the handler's closure — the
  ;; handler fires asynchronously and must not read shared state. The
  ;; budget is read through the CURRENT generation: config and hyprctl
  ;; set!s land in the generation copy, and reading this closure's own
  ;; defining environment would miss them.
  (let* ((budget (if hl--generation
                     (top-level-value 'hl--watchdog-ms hl--generation)
                     hl--watchdog-ms))
         (t0 (current-time))
         (old-handler (timer-interrupt-handler))
         (old-ticks (set-timer hl--wd-rearm)))
    (set! hl--wd-stack (cons (list old-handler old-ticks) hl--wd-stack))
    (timer-interrupt-handler
      (lambda ()
        (let ((ms (hl--ms-since t0)))
          (if (> ms budget)
              (begin
                (hl--report (format "watchdog: ~a abandoned after ~ams" what ms))
                (escape hl--wd-aborted))
              (set-timer hl--wd-rearm)))))))

(define (hl--wd-exit)
  (let ((outer (car hl--wd-stack)))
    (set! hl--wd-stack (cdr hl--wd-stack))
    (set-timer 0)
    (timer-interrupt-handler (car outer))
    (let ((old-ticks (cadr outer)))
      (if (and (number? old-ticks) (> old-ticks 0))
          (set-timer old-ticks)
          (set-timer 0)))))

;; runs THUNK under the watchdog; returns its value, or the unique
;; hl--wd-aborted marker if the budget was exceeded. Reentrant: nested
;; dispatches save/restore the outer timer state.
(define (hl--guarded-run what thunk)
  (call/cc
    (lambda (escape)
      (dynamic-wind
        (lambda () (hl--wd-enter what escape))
        (lambda () (thunk))
        (lambda () (hl--wd-exit))))))

;; ---- event fire helpers -------------------------------------------------------
;; every helper receives the hl-event RECORD (carried locked by its bus
;; connection) plus the payload; the handler thunk is extracted from the
;; record. Return semantics unchanged from the id-era helpers.

;; bind-callback result protocol — full upstream dispatchResultFromLua
;; parity (ConfigManager.cpp:1380): the Lua callback may return a table
;; {ok, pass_event, error, request_release}; Scheme returns it as a plist.
;;   #f                        — DECLINED (auto-consuming binds pass the key
;;                               through; repeating timers stop). Errors and
;;                               the watchdog decline too (upstream maps Lua
;;                               callback errors to success=false,
;;                               ConfigManager.cpp:1418).
;;   #t / any non-plist value  — handled, 'ok defaults to #t
;;   a plist                   — the full form; 'ok filled in when absent
;; Normalized so fireSchemeBind always reads #f or one plist shape.
(define (hl--bind-result r)
  (cond ((not r) #f)
        ((and (pair? r) (symbol? (car r)))
         (if (hl--plist-has? r 'ok) r (list* 'ok #t r)))
        (else '(ok #t))))

;; record-bind fire: the record arrives from the C++ capture (locked);
;; extract the thunk and run it. Contract: #f = declined, otherwise the
;; normalized result plist. Normalization runs INSIDE the guard.
(define (hl--bind-fire-rec b)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "callback"
                    (lambda () (hl--bind-result ((hl-bind-thunk b))))))))
    (and (not (eq? result hl--wd-aborted)) result)))

(define (hl--event-fire b)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "callback" (hl-event-thunk b)))))
    (not (eq? result hl--wd-aborted))))

;; numeric-list payloads (keyboard-key triples, screenshare triples): the
;; handler is applied to the elements of the list
;; timer-record fire: extract (hl-timer-thunk b) and run it zero-arg under
;; the bind result protocol (repeating timers stop on ok #f)
(define (hl--timer-fire b)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "callback"
                    (lambda () (hl--bind-result ((hl-timer-thunk b))))))))
    (and (not (eq? result hl--wd-aborted)) result)))

(define (hl--fire-list-rec b lst)
  (guard (e (#t (begin (hl--report e) #f)))
    (hl--guarded-run "handler" (lambda () (apply (hl-event-thunk b) lst)))))

(define (hl--fire-str-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl-event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-bool-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl-event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-ws-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl-event-thunk b) (and (not (eq? arg #f)) (hl--mint-workspace arg))))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-mon-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl-event-thunk b) (and (not (eq? arg #f)) (hl--mint-monitor arg))))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-ws-mon-rec b ws mon)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl-event-thunk b)
                                                           (and (not (eq? ws #f)) (hl--mint-workspace ws))
                                                           (and (not (eq? mon #f)) (hl--mint-monitor mon))))))))
    (not (eq? result hl--wd-aborted))))

;; window payloads: the id crosses and becomes a real window record here
(define (hl--fire-win-rec b win-id)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl-event-thunk b) (hl--mint-window win-id)))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-win-state-rec b win-id state)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl-event-thunk b) (hl--mint-window win-id) state))))))
    (not (eq? result hl--wd-aborted))))

;; bare-thunk list fire for GESTURES (the thunk travels directly, no record)
(define (hl--fire-list fn lst)
  (guard (e (#t (begin (hl--report e) #f)))
    (hl--guarded-run "handler" (lambda () (apply fn lst)))))

;; evaluate every form of a file into an environment (the explicit env
;; argument is the point: plain load always targets the interaction
;; environment, which would leak definitions across generations)
(define (hl--eval-file path env)
  (call-with-input-file path
    (lambda (p)
      (let loop ()
        (let ((form (read p)))
          (unless (eof-object? form)
            (eval form env)
            (loop)))))))

;; the environment user-supplied code runs in: the current generation, or
;; the persistent environment before the first reset
(define (hl--target-env)
  (or hl--generation (interaction-environment)))

(define (hl--load path)
  (guard (e (#t (hl--report e)))
    (hl--eval-file path (hl--target-env))
    #t))

;; generation-aware shadows of load/eval: user-code (load "file") and
;; (eval x) must target the current generation, never the persistent
;; environment — otherwise the documented config-splitting workflow (see
;; core) would leak definitions across generations. Without hl--generation
;; (bootstrap phase) they pass through.
(define real-load load)
(define real-eval eval)
(define load (lambda (f) (if hl--generation (hl--load f) (real-load f))))
(define eval (lambda (x . o) (real-eval x (if (null? o) (hl--target-env) (car o)))))

;; hyprctl scheme entry: evaluate all forms in the string, reply with the
;; last value formatted, or the error text. Evals land in the current
;; generation so they see the config's definitions — and die with it, like
;; upstream's evals (they run in a lua_State the next reload discards).
(define (hl--eval code)
  (guard (e (#t (call-with-string-output-port
                  (lambda (p)
                    (display "error: " p)
                    (display-condition e p)))))
    (let ((result (hl--guarded-run "eval"
                    (lambda ()
                      (let ((env (hl--target-env)))
                        (let loop ((port (open-input-string code)) (result (void)))
                          (let ((form (read port)))
                            (if (eof-object? form)
                                result
                                (loop port (eval form env))))))))))
      (if (eq? result hl--wd-aborted)
          "watchdog: eval abandoned (see the compositor log)"
          (format "~s" result)))))

;; layout callbacks: spec = a single recalculate fn, or a plist of callbacks
;; (recalculate . fn) (resize . fn) (window-open . fn) (window-close . fn)
;; (layout-msg . fn). recalculate/resize receive (count W H windows) where
;; windows[i] is the handle for box i (a handle whose id 0 means "not a
;; window" — every query on it returns #f), plus (dx dy corner) for resize.
;; fn returns list of (x y w h), or #f on error.

(define (hl--split-string str char)
  (let loop ((i 0) (start 0) (acc '()))
    (cond
      ((>= i (string-length str))
       (reverse (cons (substring str start i) acc)))
      ((char=? (string-ref str i) char)
       (loop (+ i 1) (+ i 1) (cons (substring str start i) acc)))
      (else (loop (+ i 1) start acc)))))

(define (hl--string-index str char)
  (let loop ((i 0))
    (cond ((>= i (string-length str)) #f)
          ((char=? (string-ref str i) char) i)
          (else (loop (+ i 1))))))

(define (hl--string-join lst sep)
  (if (null? lst) ""
      (let loop ((acc (car lst)) (rest (cdr lst)))
        (if (null? rest) acc
            (loop (string-append acc sep (car rest)) (cdr rest))))))

(define (hl--trim str)
  (define n (string-length str))
  (let loop ((s 0) (e n))
    (cond ((and (< s e) (char-whitespace? (string-ref str s))) (loop (+ s 1) e))
          ((and (< s e) (char-whitespace? (string-ref str (- e 1)))) (loop s (- e 1)))
          (else (substring str s e)))))

;; layout event dispatch. a layout provider is a PLIST of callbacks:
;;   ((recalculate . fn) (resize . fn) (window-open . fn) (window-close . fn))
;; recalculate/resize fn: (count W H windows [dx dy corner]) -> ((x y w h) ...)
;; window callbacks: (window) -> ignored. #f when absent or on error.
(define (hl--layout-event-spec spec event payload)
  (let* ((cbs spec))
    (if (not cbs)
        #f
        (guard (e (#t (hl--report e)))
          (cond
            ((equal? event "window-open")
             (let ((cb (hl--plist-get cbs 'window-open #f)))
               (if cb (cb (hl--mint-window payload)) #f)))
            ((equal? event "window-close")
             (let ((cb (hl--plist-get cbs 'window-close #f)))
               (if cb (cb (hl--mint-window payload)) #f)))
            ((equal? event "layout-msg")
             (let ((cb (hl--plist-get cbs 'layout-msg #f)))
               (if (not cb)
                   ""
                   (let ((r (cb payload)))
                     (cond ((not r) "rejected")
                           ((string? r) r)
                           (else ""))))))
            (else
             ;; payload is a real list: (count W H id... [dx dy corner] for resize)
             (let* ((count (car payload))
                    (W     (cadr payload))
                    (H     (caddr payload))
                    (tail  (cdddr payload)))
               (if (equal? event "resize")
                   (let* ((n    (length tail))
                          (cb   (or (hl--plist-get cbs 'resize #f) (hl--plist-get cbs 'recalculate #f))))
                     (if cb
                         (apply cb
                                (list count W H
                                      (map hl--mint-window (list-head tail (- n 3)))
                                      (list-ref tail (- n 3))
                                      (list-ref tail (- n 2))
                                      (list-ref tail (- n 1))))
                         #f))
                   (let ((cb (hl--plist-get cbs 'recalculate #f)))
                     (if cb (cb count W H (map hl--mint-window tail)) #f))))))))))
)scm";

static constexpr const char* SCHEME_BOOTSTRAP = R"scm(
;; hl-bind-add!: record first (locked + tagged C++-side), then the tokens
;; the bind is built from, flags, description, devices
(define c-hl-bind (foreign-procedure "hl-scheme-bind" (scheme-object scheme-object int string string) int))
(define c-hl-exec (foreign-procedure "hl-scheme-exec" (string) int))
(define c-hl-timer (foreign-procedure "hl-scheme-timer" (scheme-object int int) int))
(define c-hl-active-title (foreign-procedure "hl-scheme-active-title" () scheme-object))
(define c-hl-workspace-names (foreign-procedure "hl-scheme-workspace-names" () scheme-object))
(define c-hl-submap-listen (foreign-procedure "hl-scheme-submap-listen" (scheme-object) int))
(define c-hl-active-window-id (foreign-procedure "hl-scheme-active-window-id" () double))
(define c-hl-window-ids (foreign-procedure "hl-scheme-window-ids" () scheme-object))
(define c-hl-window-title (foreign-procedure "hl-scheme-window-title" (integer-64) scheme-object))
(define c-hl-window-alive (foreign-procedure "hl-scheme-window-alive" (integer-64) int))
(define c-hl-window-close (foreign-procedure "hl-scheme-window-close" (integer-64) int))
(define c-hl-window-class (foreign-procedure "hl-scheme-window-class" (integer-64) scheme-object))
(define c-hl-window-workspace-id (foreign-procedure "hl-scheme-window-workspace-id" (integer-64) scheme-object))
(define c-hl-window-monitor-id (foreign-procedure "hl-scheme-window-monitor-id" (integer-64) scheme-object))
(define c-hl-window-floating (foreign-procedure "hl-scheme-window-floating" (integer-64) int))
(define c-hl-window-size (foreign-procedure "hl-scheme-window-size" (integer-64) scheme-object))
(define c-hl-window-pid (foreign-procedure "hl-scheme-window-pid" (integer-64) int))
(define c-hl-window-focus (foreign-procedure "hl-scheme-window-focus" (integer-64) int))
(define c-hl-window-float (foreign-procedure "hl-scheme-window-float" (integer-64) int))
(define c-hl-window-move-to-workspace (foreign-procedure "hl-scheme-window-move-to-workspace" (integer-64 string) int))
(define c-hl-monitor-names (foreign-procedure "hl-scheme-monitor-names" () scheme-object))
(define c-hl-window-event-listen (foreign-procedure "hl-scheme-window-event-listen" (scheme-object int) int))
(define c-hl-window-minimize-listen (foreign-procedure "hl-scheme-window-minimize-listen" (scheme-object) int))
(define c-hl-lifecycle-listen (foreign-procedure "hl-scheme-lifecycle-listen" (scheme-object int) int))
(define c-hl-config-reloaded-listen (foreign-procedure "hl-scheme-config-reloaded-listen" (scheme-object) int))
(define c-hl-config-unload-listen (foreign-procedure "hl-scheme-config-unload-listen" (scheme-object) int))
(define c-hl-config-props-refreshed-listen (foreign-procedure "hl-scheme-config-props-refreshed-listen" (scheme-object) int))
(define c-hl-window-destroy-listen (foreign-procedure "hl-scheme-window-destroy-listen" (scheme-object) int))
(define c-hl-layer-listen (foreign-procedure "hl-scheme-layer-listen" (scheme-object int) int))
(define c-hl-screenshare-listen (foreign-procedure "hl-scheme-screenshare-listen" (scheme-object) int))
(define c-hl-keyboard-key-listen (foreign-procedure "hl-scheme-keyboard-key-listen" (scheme-object) int))
(define c-hl-unbind-rec (foreign-procedure "hl-scheme-unbind-rec" (scheme-object) int))
(define c-hl-unbind-key (foreign-procedure "hl-scheme-unbind-key" (string) int))
(define c-hl-event-cancel (foreign-procedure "hl-scheme-event-cancel" (scheme-object) int))
(define c-hl-event-active (foreign-procedure "hl-scheme-event-active" (scheme-object) int))
(define c-hl-window-same (foreign-procedure "hl-scheme-window-same" (integer-64 integer-64) int))
(define c-hl-current-submap (foreign-procedure "hl-scheme-current-submap" () scheme-object))
(define c-hl-cursor-pos (foreign-procedure "hl-scheme-cursor-pos" () scheme-object))
(define c-hl-workspace-active-listen (foreign-procedure "hl-scheme-workspace-active-listen" (scheme-object) int))
(define c-hl-monitor-event-listen (foreign-procedure "hl-scheme-monitor-event-listen" (scheme-object int) int))
(define c-hl-workspace-event-listen (foreign-procedure "hl-scheme-workspace-event-listen" (scheme-object int) int))
(define c-hl-workspace-change-id (foreign-procedure "hl-scheme-workspace-change-id" (string double) int))
(define c-hl-workspace-groups (foreign-procedure "hl-scheme-workspace-groups" (integer-64) scheme-object))
(define c-hl-group-members (foreign-procedure "hl-scheme-group-members" (integer-64) scheme-object))
(define c-hl-group-current (foreign-procedure "hl-scheme-group-current" (integer-64) scheme-object))
(define c-hl-group-current-idx (foreign-procedure "hl-scheme-group-current-idx" (integer-64) scheme-object))
(define c-hl-group-size (foreign-procedure "hl-scheme-group-size" (integer-64) scheme-object))
(define c-hl-group-locked (foreign-procedure "hl-scheme-group-locked" (integer-64) scheme-object))
(define c-hl-group-denied (foreign-procedure "hl-scheme-group-denied" (integer-64) scheme-object))
(define c-hl-group-alive (foreign-procedure "hl-scheme-group-alive" (integer-64) int))
(define c-hl-group-same (foreign-procedure "hl-scheme-group-same" (integer-64 integer-64) int))
(define c-hl-group-add (foreign-procedure "hl-scheme-group-add" (integer-64 integer-64 integer-64) int))
(define c-hl-group-remove (foreign-procedure "hl-scheme-group-remove" (integer-64 integer-64) int))
(define c-hl-layout-add (foreign-procedure "hl-scheme-layout-add" (string scheme-object) int))

;; pure-function layout; see SchemeLayout.hpp for the contract
;; spec is a single recalculate fn, or a PLIST of callbacks:
;;   'recalculate fn 'resize fn 'window-open fn 'window-close fn
;; resize fn: (count W H windows dx dy corner) -> boxes; when absent a resize
;; falls back to the recalculate fn. state: keep it in a closure around fn.
;; spec is a single recalculate procedure, or a PLIST of callbacks:
;; (hl-layout-add! "name" 'recalculate fn 'resize fn 'layout-msg fn)
;; layouts are registered in the compositor's global registry (mutation,
;; hence ! and -add!). SPEC is a PLIST of callbacks — every callback is
;; named explicitly: 'recalculate LAMBDA 'resize LAMBDA ... There is no
;; shorthand that guesses a bare lambda's role.
(define (hl-layout-add! name . spec)
  (when (or (null? spec) (and (pair? spec) (procedure? (car spec))))
    (errorf 'hl-layout-add!
            "the callbacks must be named, e.g. (hl-layout-add! ~s 'recalculate LAMBDA ...)"
            name))
  (if (= 0 (c-hl-layout-add name spec))
      name
      (errorf 'hl-layout-add! "layout ~a rejected, see compositor log" name)))

(define c-hl-get-submap-ctx (foreign-procedure "hl-scheme-get-submap-ctx" () scheme-object))
(define c-hl-set-submap-ctx (foreign-procedure "hl-scheme-set-submap-ctx" (string string) void))
(define c-hl-enter-submap (foreign-procedure "hl-scheme-enter-submap" (string) int))

;; binds registered inside fn are scoped to the submap; the optional reset
;; names the submap returned to on exit. the submap exists only if fn
;; registers at least one bind.
(define (hl-submap name fn . reset)
  (let* ((ctx        (c-hl-get-submap-ctx))
         (prev-name  (car ctx))
         (prev-reset (cdr ctx)))
    (dynamic-wind
      (lambda () (c-hl-set-submap-ctx name (if (null? reset) "" (car reset))))
      (lambda () (fn))
      (lambda () (c-hl-set-submap-ctx prev-name prev-reset)))))

;; switch the active submap ("" or "reset" returns to the default)
(define (hl-submap-activate! name)
  (= 0 (c-hl-enter-submap name)))

(define (hl-submap-exit!)
  (= 0 (c-hl-enter-submap "")))
(define c-hl-window-fullscreen-toggle (foreign-procedure "hl-scheme-window-fullscreen-toggle" (integer-64 int) int))
(define c-hl-window-fullscreen-set (foreign-procedure "hl-scheme-window-fullscreen-set" (integer-64 int) int))
(define c-hl-window-fullscreen-mode (foreign-procedure "hl-scheme-window-fullscreen-mode" (integer-64) int))
(define c-hl-window-hidden (foreign-procedure "hl-scheme-window-hidden" (integer-64) int))
;; window read-side fields (LuaWindow parity)
(define c-hl-window-address (foreign-procedure "hl-scheme-window-address" (integer-64) scheme-object))
(define c-hl-window-mapped (foreign-procedure "hl-scheme-window-mapped" (integer-64) int))
(define c-hl-window-visible (foreign-procedure "hl-scheme-window-visible" (integer-64) int))
(define c-hl-window-accepts-input (foreign-procedure "hl-scheme-window-accepts-input" (integer-64) int))
(define c-hl-window-position (foreign-procedure "hl-scheme-window-position" (integer-64) scheme-object))
(define c-hl-window-pin-fullscreened (foreign-procedure "hl-scheme-window-pin-fullscreened" (integer-64) int))
(define c-hl-window-allowed-over-fullscreen (foreign-procedure "hl-scheme-window-allowed-over-fullscreen" (integer-64) int))
(define c-hl-window-tearing-hint (foreign-procedure "hl-scheme-window-tearing-hint" (integer-64) int))
(define c-hl-window-inhibiting-idle (foreign-procedure "hl-scheme-window-inhibiting-idle" (integer-64) int))
(define c-hl-window-focus-history-id (foreign-procedure "hl-scheme-window-focus-history-id" (integer-64) scheme-object))
(define c-hl-window-content-type (foreign-procedure "hl-scheme-window-content-type" (integer-64) scheme-object))
(define c-hl-window-stable-id (foreign-procedure "hl-scheme-window-stable-id" (integer-64) scheme-object))
(define c-hl-window-tags (foreign-procedure "hl-scheme-window-tags" (integer-64) scheme-object))
(define c-hl-window-swallowing-id (foreign-procedure "hl-scheme-window-swallowing-id" (integer-64) scheme-object))
(define c-hl-window-xdg-tag (foreign-procedure "hl-scheme-window-xdg-tag" (integer-64) scheme-object))
(define c-hl-window-xdg-description (foreign-procedure "hl-scheme-window-xdg-description" (integer-64) scheme-object))
(define c-hl-window-layout (foreign-procedure "hl-scheme-window-layout" (integer-64) scheme-object))
(define c-hl-window-pinned (foreign-procedure "hl-scheme-window-pinned" (integer-64) int))
(define c-hl-window-pseudo-query (foreign-procedure "hl-scheme-window-pseudo-query" (integer-64) int))
(define c-hl-window-maximized-query (foreign-procedure "hl-scheme-window-maximized-query" (integer-64) int))
(define c-hl-window-in-group (foreign-procedure "hl-scheme-window-in-group" (integer-64) int))
(define c-hl-window-group-denied (foreign-procedure "hl-scheme-window-group-denied" (integer-64) int))
(define c-hl-window-group-locked (foreign-procedure "hl-scheme-window-group-locked" (integer-64) int))
(define c-hl-groups-locked (foreign-procedure "hl-scheme-groups-locked" () int))
(define c-hl-window-group-lock (foreign-procedure "hl-scheme-window-group-lock" (integer-64 int) int))
(define c-hl-window-prop (foreign-procedure "hl-scheme-window-prop" (integer-64 string) scheme-object))
(define c-hl-window-initial-class (foreign-procedure "hl-scheme-window-initial-class" (integer-64) scheme-object))
(define c-hl-window-initial-title (foreign-procedure "hl-scheme-window-initial-title" (integer-64) scheme-object))

;; ---- actions: the dispatcher surface (window id -1 = active window) --------
(define c-hl-focus-workspace (foreign-procedure "hl-scheme-focus-workspace" (string) int))
(define c-hl-focus-direction (foreign-procedure "hl-scheme-focus-direction" (string) int))
(define c-hl-focus-monitor (foreign-procedure "hl-scheme-focus-monitor" (string) int))
(define c-hl-focus-last (foreign-procedure "hl-scheme-focus-last" () int))
(define c-hl-focus-urgent (foreign-procedure "hl-scheme-focus-urgent" () int))
(define c-hl-window-move-direction (foreign-procedure "hl-scheme-window-move-direction" (integer-64 string) int))
(define c-hl-window-swap-direction (foreign-procedure "hl-scheme-window-swap-direction" (integer-64 string) int))
(define c-hl-window-swap-next (foreign-procedure "hl-scheme-window-swap-next" (integer-64 int) int))
(define c-hl-window-swap-with (foreign-procedure "hl-scheme-window-swap-with" (integer-64 integer-64) int))
(define c-hl-window-float-act (foreign-procedure "hl-scheme-window-float-act" (integer-64 int) int))
(define c-hl-window-cycle (foreign-procedure "hl-scheme-window-cycle" (integer-64 int int) int))
(define c-hl-window-center (foreign-procedure "hl-scheme-window-center" (integer-64) int))
(define c-hl-window-resize-px (foreign-procedure "hl-scheme-window-resize-px" (integer-64 double double int) int))
(define c-hl-window-move-px (foreign-procedure "hl-scheme-window-move-px" (integer-64 double double int) int))
(define c-hl-window-pin-act (foreign-procedure "hl-scheme-window-pin-act" (integer-64 int) int))
(define c-hl-window-pseudo (foreign-procedure "hl-scheme-window-pseudo" (integer-64 int) int))
(define c-hl-window-kill (foreign-procedure "hl-scheme-window-kill" (integer-64) int))
(define c-hl-window-signal (foreign-procedure "hl-scheme-window-signal" (integer-64 int) int))
(define c-hl-window-zorder (foreign-procedure "hl-scheme-window-zorder" (integer-64 string) int))
(define c-hl-window-set-prop (foreign-procedure "hl-scheme-window-set-prop" (integer-64 string string) int))
(define c-hl-window-tag (foreign-procedure "hl-scheme-window-tag" (integer-64 string) int))
(define c-hl-window-clear-tags (foreign-procedure "hl-scheme-window-clear-tags" (integer-64) int))
(define c-hl-toggle-swallow (foreign-procedure "hl-scheme-toggle-swallow" () int))
(define c-hl-group-toggle (foreign-procedure "hl-scheme-group-toggle" (integer-64) int))
(define c-hl-group-set (foreign-procedure "hl-scheme-group-set" (integer-64 int) int))
(define c-hl-monitor-set-special (foreign-procedure "hl-scheme-monitor-set-special" (string string) int))
(define c-hl-group-cycle (foreign-procedure "hl-scheme-group-cycle" (integer-64 int) int))
(define c-hl-group-index (foreign-procedure "hl-scheme-group-index" (integer-64 int) int))
(define c-hl-group-move-window (foreign-procedure "hl-scheme-group-move-window" (integer-64 int) int))
(define c-hl-group-lock (foreign-procedure "hl-scheme-group-lock" (int) int))
(define c-hl-group-lock-active (foreign-procedure "hl-scheme-group-lock-active" (int) int))
(define c-hl-window-into-group (foreign-procedure "hl-scheme-window-into-group" (integer-64 string) int))
(define c-hl-window-out-of-group (foreign-procedure "hl-scheme-window-out-of-group" (integer-64 string) int))
(define c-hl-window-into-or-create-group (foreign-procedure "hl-scheme-window-into-or-create-group" (integer-64 string) int))
(define c-hl-window-deny-from-group (foreign-procedure "hl-scheme-window-deny-from-group" (integer-64 int) int))
(define c-hl-workspace-rename (foreign-procedure "hl-scheme-workspace-rename" (string string) int))
(define c-hl-workspace-move-monitor (foreign-procedure "hl-scheme-workspace-move-monitor" (string string) int))
(define c-hl-workspace-toggle-special (foreign-procedure "hl-scheme-workspace-toggle-special" (string) int))
(define c-hl-workspace-swap-monitors (foreign-procedure "hl-scheme-workspace-swap-monitors" (string string) int))
(define c-hl-cursor-move (foreign-procedure "hl-scheme-cursor-move" (double double) int))
(define c-hl-cursor-corner (foreign-procedure "hl-scheme-cursor-corner" (integer-64 int) int))
(define c-hl-exit (foreign-procedure "hl-scheme-exit" () int))
(define c-hl-reload-config (foreign-procedure "hl-scheme-reload-config" () int))
(define c-hl-force-renderer-reload (foreign-procedure "hl-scheme-force-renderer-reload" () int))
(define c-hl-dpms (foreign-procedure "hl-scheme-dpms" (int string) int))
(define c-hl-force-idle (foreign-procedure "hl-scheme-force-idle" (double) int))
(define c-hl-global (foreign-procedure "hl-scheme-global" (string) int))
(define c-hl-event (foreign-procedure "hl-scheme-event" (string) int))
(define c-hl-pass (foreign-procedure "hl-scheme-pass" (integer-64) int))
(define c-hl-send-shortcut (foreign-procedure "hl-scheme-send-shortcut" (string string integer-64) int))
(define c-hl-send-key-state (foreign-procedure "hl-scheme-send-key-state" (string string int integer-64) int))
(define c-hl-mouse (foreign-procedure "hl-scheme-mouse" (string) int))
(define c-hl-release-input-capture (foreign-procedure "hl-scheme-release-input-capture" () int))
(define c-hl-window-fullscreen-state (foreign-procedure "hl-scheme-window-fullscreen-state" (integer-64 int int int) int))
(define c-hl-layout-message (foreign-procedure "hl-scheme-layout-message" (string) int))
;; ---- config: set/get config options ----------------------------------------
(define c-hl-config-begin (foreign-procedure "hl-config-begin" () int))
(define c-hl-config-push-int (foreign-procedure "hl-config-push-int" (double) int))
(define c-hl-config-push-num (foreign-procedure "hl-config-push-num" (double) int))
(define c-hl-config-push-bool (foreign-procedure "hl-config-push-bool" (int) int))
(define c-hl-config-push-str (foreign-procedure "hl-config-push-str" (string) int))
(define c-hl-config-tbl-open (foreign-procedure "hl-config-tbl-open" (int) int))
(define c-hl-config-tbl-key (foreign-procedure "hl-config-tbl-key" (string) int))
(define c-hl-config-tbl-set-hash (foreign-procedure "hl-config-tbl-set-hash" () int))
(define c-hl-config-tbl-seti (foreign-procedure "hl-config-tbl-seti" (int) int))
(define c-hl-config-set (foreign-procedure "hl-config-set" (string) int))
(define c-hl-config-last-error (foreign-procedure "hl-config-last-error" () scheme-object))
(define c-hl-config-get (foreign-procedure "hl-config-get" (string) scheme-object))
(define c-hl-monitor-begin (foreign-procedure "hl-monitor-begin" (string) int))
(define c-hl-monitor-field-str (foreign-procedure "hl-monitor-field-str" (string string) int))
(define c-hl-monitor-field-num (foreign-procedure "hl-monitor-field-num" (string double) int))
(define c-hl-monitor-field-gap (foreign-procedure "hl-monitor-field-gap" (string) int))
(define c-hl-monitor-field-bool (foreign-procedure "hl-monitor-field-bool" (string int) int))
(define c-hl-monitor-commit (foreign-procedure "hl-monitor-commit" () int))
(define c-hl-curve-add (foreign-procedure "hl-curve-add" (string int double double double double) int))
(define c-hl-animation-set (foreign-procedure "hl-animation-set" (string int double string string) int))
(define c-hl-permission-add (foreign-procedure "hl-permission-add" (string string string) int))
(define c-hl-window-rule-begin (foreign-procedure "hl-window-rule-begin" (string int) int))
(define c-hl-layer-rule-begin (foreign-procedure "hl-layer-rule-begin" (string int) int))
(define c-hl-rule-match (foreign-procedure "hl-rule-match" (string string) int))
(define c-hl-window-rule-effect (foreign-procedure "hl-window-rule-effect" (string string) int))
(define c-hl-layer-rule-effect (foreign-procedure "hl-layer-rule-effect" (string string) int))
(define c-hl-window-rule-commit (foreign-procedure "hl-window-rule-commit" (scheme-object) int))
(define c-hl-layer-rule-commit (foreign-procedure "hl-layer-rule-commit" (scheme-object) int))
(define c-hl-rule-set-enabled (foreign-procedure "hl-rule-set-enabled" (scheme-object int) int))
(define c-hl-rule-enabled (foreign-procedure "hl-rule-enabled" (scheme-object) int))
(define c-hl-workspace-rule-begin (foreign-procedure "hl-workspace-rule-begin" (string int) int))
(define c-hl-workspace-rule-str (foreign-procedure "hl-workspace-rule-str" (string string) int))
(define c-hl-workspace-rule-num (foreign-procedure "hl-workspace-rule-num" (string double) int))
(define c-hl-workspace-rule-bool (foreign-procedure "hl-workspace-rule-bool" (string int) int))
(define c-hl-workspace-rule-gap (foreign-procedure "hl-workspace-rule-gap" (string) int))
(define c-hl-workspace-rule-layout-opt (foreign-procedure "hl-workspace-rule-layout-opt" (string string) int))
(define c-hl-workspace-rule-commit (foreign-procedure "hl-workspace-rule-commit" () int))
(define c-hl-window-from (foreign-procedure "hl-window-from" (string) double))
(define c-hl-urgent-window (foreign-procedure "hl-urgent-window" () double))
(define c-hl-last-window (foreign-procedure "hl-last-window" () double))
(define c-hl-monitor-from (foreign-procedure "hl-monitor-from" (string) scheme-object))
(define c-hl-monitor-at (foreign-procedure "hl-monitor-at" (double double) scheme-object))
(define c-hl-monitor-at-cursor (foreign-procedure "hl-monitor-at-cursor" () scheme-object))
(define c-hl-active-monitor (foreign-procedure "hl-active-monitor" () scheme-object))
(define c-hl-active-workspace (foreign-procedure "hl-active-workspace" () scheme-object))
(define c-hl-active-special-workspace (foreign-procedure "hl-active-special-workspace" () scheme-object))
(define c-hl-last-workspace (foreign-procedure "hl-last-workspace" () scheme-object))
(define c-hl-workspace-windows (foreign-procedure "hl-workspace-windows" (string) scheme-object))
(define c-hl-layers (foreign-procedure "hl-layers" (integer-64 scheme-object) scheme-object))
(define c-hl-layer-alive (foreign-procedure "hl-layer-alive" (integer-64) int))
(define c-hl-layer-same (foreign-procedure "hl-layer-same" (integer-64 integer-64) int))
(define c-hl-layer-address (foreign-procedure "hl-layer-address" (integer-64) scheme-object))
(define c-hl-layer-pid (foreign-procedure "hl-layer-pid" (integer-64) scheme-object))
(define c-hl-layer-monitor (foreign-procedure "hl-layer-monitor" (integer-64) scheme-object))
(define c-hl-layer-namespace (foreign-procedure "hl-layer-namespace" (integer-64) scheme-object))
(define c-hl-layer-level (foreign-procedure "hl-layer-level" (integer-64) scheme-object))
(define c-hl-layer-mapped (foreign-procedure "hl-layer-mapped" (integer-64) scheme-object))
(define c-hl-layer-kb-interactivity (foreign-procedure "hl-layer-kb-interactivity" (integer-64) scheme-object))
(define c-hl-layer-above-fs (foreign-procedure "hl-layer-above-fs" (integer-64) scheme-object))
(define c-hl-layer-position (foreign-procedure "hl-layer-position" (integer-64) scheme-object))
(define c-hl-layer-size (foreign-procedure "hl-layer-size" (integer-64) scheme-object))
(define c-hl-is-key-down (foreign-procedure "hl-is-key-down" (string) int))
(define c-hl-loaded-plugins (foreign-procedure "hl-loaded-plugins" () scheme-object))
(define c-hl-version (foreign-procedure "hl-version" () scheme-object))
(define c-hl-windows-from (foreign-procedure "hl-windows-from" (string) scheme-object))
(define c-hl-window-fullscreen-handler (foreign-procedure "hl-window-fullscreen-handler" (integer-64) scheme-object))
(define c-hl-notify (foreign-procedure "hl-notify!" (string double string string double) scheme-object))
(define c-hl-timer-set-enabled (foreign-procedure "hl-timer-set-enabled" (scheme-object int) int))
(define c-hl-timer-enabled (foreign-procedure "hl-timer-enabled" (scheme-object) int))
(define c-hl-timer-set-timeout (foreign-procedure "hl-timer-set-timeout" (scheme-object double) int))
(define c-hl-timer-cancel (foreign-procedure "hl-timer-cancel" (scheme-object) int))
(define c-hl-exec-raw (foreign-procedure "hl-exec!" (string) int))
(define c-hl-exec-with-rules (foreign-procedure "hl-exec-shell-with-rules!" (string) int))
;; one maker address per constructor, fetched from its own one-line C
;; accessor — no name strings, no dispatch
(define c-hl-gesture-maker-workspace-swipe (foreign-procedure "hl-scheme-gesture-maker-workspace-swipe" () integer-64))
(define c-hl-gesture-maker-move (foreign-procedure "hl-scheme-gesture-maker-move" () integer-64))
(define c-hl-gesture-maker-resize (foreign-procedure "hl-scheme-gesture-maker-resize" () integer-64))
(define c-hl-gesture-maker-close (foreign-procedure "hl-scheme-gesture-maker-close" () integer-64))
(define c-hl-gesture-maker-scroll-move (foreign-procedure "hl-scheme-gesture-maker-scroll-move" () integer-64))
(define c-hl-gesture-maker-float (foreign-procedure "hl-scheme-gesture-maker-float" () integer-64))
(define c-hl-gesture-maker-fullscreen (foreign-procedure "hl-scheme-gesture-maker-fullscreen" () integer-64))
(define c-hl-gesture-maker-special (foreign-procedure "hl-scheme-gesture-maker-special" () integer-64))
(define c-hl-gesture-maker-cursor-zoom (foreign-procedure "hl-scheme-gesture-maker-cursor-zoom" () integer-64))
(define c-hl-gesture-maker-custom (foreign-procedure "hl-scheme-gesture-maker-custom" () integer-64))
(define c-hl-gesture (foreign-procedure "hl-scheme-gesture" (scheme-object int string string double int) int))
(define c-hl-gesture-remove (foreign-procedure "hl-scheme-gesture-remove" (int string string double int) int))

;; helpers for the action wrappers: window #f = active; actions 'toggle/'on/'off;
;; directions "l"/"r"/"u"/"d" or the symbols left/right/up/down
(define (hl--wid w)
  (if w (hl-window-id w) -1))

;; optional-boolean convention shared by every toggle/set action:
;; no argument → 0 (toggle), #t → 1 (on), #f → 2 (off)
(define (hl--bool-act args)
  (if (null? args) 0 (if (car args) 1 2)))

(define (hl--dir d)
  (if (symbol? d) (symbol->string d) d))

;; names/tags/props accept symbols or strings (numbers become strings)
(define (hl--str s)
  (cond ((symbol? s) (symbol->string s))
        ((number? s) (number->string s))
        (else s)))
(define c-hl-window-x11 (foreign-procedure "hl-scheme-window-x11" (integer-64) int))

;; flat option list -> assoc list: ('release #t 'description "x")
;; ---- plist helpers ----------------------------------------------------------
;; The API's one convention for named fields: a flat "keyword" list
;;   'release #t 'description "x"
;; — the same shape Guile uses for #:keyword arguments. A MISSING value
;; reports the DEFAULT; pass #!eof when absence must be told apart from an
;; explicit #f (an option that was given the value #f).

(define (hl--plist-cdr l)
  ;; (key value rest ...) → (value rest ...); a trailing bare KEY is an error
  (let ((tail (cdr l)))
    (if (null? tail)
        (errorf 'hl--plist "odd plist: ~s" l)
        tail)))

;; public: read a field out of any plist (gesture events, config tables);
;; missing key reports DEFAULT — pass #!eof to tell absent apart from #f
(define (hl-plist-get plist key . default)
  (apply hl--plist-get plist key default))

(define (hl--plist-get pl key default)
  (let loop ((l pl))
    (if (null? l)
        default
        (let ((tail (hl--plist-cdr l)))
          (if (eq? (car l) key)
              (car tail)
              (loop (cdr tail)))))))

(define (hl--plist-has? pl key)
  (let loop ((l pl))
    (if (null? l)
        #f
        (let ((tail (hl--plist-cdr l)))
          (if (eq? (car l) key) #t (loop (cdr tail)))))))

(define (hl--plist-fold f seed pl)
  (let loop ((l pl) (acc seed))
    (if (null? l)
        acc
        (let ((tail (hl--plist-cdr l)))
          (loop (cdr tail) (f (car l) (car tail) acc))))))

;; map (k v ...) → (k (f k v) ...), consing a fresh plist
(define (hl--plist-map f pl)
  (let loop ((l pl))
    (if (null? l)
        '()
        (let ((tail (hl--plist-cdr l)))
          (cons (car l) (cons (f (car l) (car tail)) (loop (cdr tail))))))))

;; eBindFlags bits from src/keybinds/Bind.hpp
;; option → eBindFlags bit, as a plist, mirrored from upstream Keybinds/Bind.hpp
;; (BIND_FLAG_* = 1 << n). Derived bits are added separately in hl--bind-flags:
;; click/drag imply RELEASE, a device option implies DEVICE_INCLUSIVE, and the
;; key name "catchall" implies CATCH_ALL (upstream LuaBindingsToplevel.cpp).
(define hl--flag-bits
  '(locked 1 release 2 repeat 4 long-press 8
    non-consuming 16 auto-consuming 32 transparent 64
    ignore-mods 128 dont-inhibit 256 click 512 drag 1024
    submap-universal 2048 allow-input-capture 4096 mouse 32768))

(define (hl--bind-flags pl key)
  (let ((click (hl--plist-get pl 'click #f))
        (drag  (hl--plist-get pl 'drag #f))
        (devices (hl--plist-get pl 'devices #f)))
    (when (and click drag)
      (errorf 'hl-bind-add! "click and drag are exclusive"))
    (when (and (hl--plist-get pl 'mouse #f)
               (or (hl--plist-get pl 'repeat #f) (hl--plist-get pl 'locked #f) (hl--plist-get pl 'release #f)))
      (errorf 'hl-bind-add! "mouse is exclusive with repeat/locked/release"))
    (+ (if (or click drag) 2 0)                    ; click/drag imply release: upstream also sets BIND_FLAG_RELEASE for them
       (if (if (hl--plist-has? pl 'device-inclusive)
               (hl--plist-get pl 'device-inclusive #f)   ; explicit 'device-inclusive #f opts OUT of inclusivity
               (pair? devices))                          ; absent: inclusive when devices are listed (upstream default true)
           8192
           0)
       (if (equal? key "catchall") 16384 0)      ; upstream: key "catchall" ⇒ BIND_FLAG_CATCH_ALL
       (hl--plist-fold (lambda (opt bit acc) (+ acc (if (hl--plist-get pl opt #f) bit 0))) 0 hl--flag-bits))))

(define (hl--bind-impl tokens thunk . opts)
  (let* ((key (car (reverse tokens)))
         (rec (make-hl-bind tokens thunk))
         (rc (c-hl-bind rec
                        tokens
                        (hl--bind-flags opts key)
                        (hl--plist-get opts 'description "")
                        (let ((ds (hl--plist-get opts 'devices #f)))
                          (if (list? ds)
                              (let loop ((rest ds) (acc ""))
                                (cond ((null? rest) acc)
                                      ((null? (cdr rest)) (string-append acc (car rest)))
                                      (else (loop (cdr rest) (string-append acc (car rest) ",")))))
                              "")))))
    (if (= 0 rc)
        rec
        (errorf 'hl-bind-add! "bind ~a rejected, see compositor log" tokens))))

;; hl-bind-add! is the one way to register a bind: TOKENS is a list of key
;; tokens — the modifiers first, then the key. Build it with a helper:
;;   (hl-bind-add! (hl-kbd "C-M-a") THUNK . OPTS)        — emacs syntax
;;   (hl-bind-add! (hl-key "SUPER+A") THUNK . OPTS)   — hyprland syntax
;; or write the list out literally:
;;   (hl-bind-add! '("SUPER" "Q") THUNK . OPTS)
;; A modless key is still a list: (hl-kbd "g") → ("g"). There is no
;; two-string shorthand in the core API — define your own wrapper on top
;; if you want one (see the wiki, binds).
(define (hl-bind-add! tokens thunk . opts)
  (apply hl--bind-impl tokens thunk opts))

;; ---- key specification helpers -----------------------------------------------
;; (hl-kbd "C-M-a")      — emacs syntax → token list for hl-bind-add!
;; (hl-key "SUPER+SHIFT+Q") — lua/hyprland syntax → token list

(define hl--emacs-mods
  '(("C" . "CTRL") ("M" . "ALT") ("S" . "SHIFT")
    ("s" . "SUPER") ("A" . "ALT") ("H" . "MOD3")))

(define hl--emacs-keys
  '(("RET" . "Return") ("SPC" . "space") ("ESC" . "Escape")
    ("TAB" . "Tab") ("DEL" . "Delete") ("LFD" . "Return")))

;; emacs mouse events → hyprland mouse:CODE
;; emacs mouse-1=left, mouse-2=middle, mouse-3=right
;; hyprland: 272=left, 273=right, 274=middle
(define hl--emacs-mouse
  '(("<down-mouse-1>" . "mouse:272") ("<mouse-1>" . "mouse:272")
    ("<down-mouse-2>" . "mouse:274") ("<mouse-2>" . "mouse:274")
    ("<down-mouse-3>" . "mouse:273") ("<mouse-3>" . "mouse:273")
    ("<mouse-4>" . "mouse:275") ("<mouse-5>" . "mouse:276")))

(define (hl--emacs-key name)
  ;; translate an emacs key name to the xkb/hyprland name
  (let ((mapped (or (assoc name hl--emacs-keys) (assoc name hl--emacs-mouse))))
    (if mapped
        (cdr mapped)
        ;; bracketed keyboard keysym: <f1> → F1, <left> → Left, <kp-1> → KP_1
        (if (and (> (string-length name) 2)
                 (char=? (string-ref name 0) #\<)
                 (char=? (string-ref name (- (string-length name) 1)) #\>))
            (let* ((inner (substring name 1 (- (string-length name) 1)))
                   (n (string-length inner)))
              (let build ((i 0) (acc '()))
                (if (= i n)
                    (list->string (reverse acc))
                    (let ((c (string-ref inner i)))
                      (build (+ i 1)
                             (cons (if (char=? c #\-) #\_ c) acc))))))
            name))))

(define (hl-kbd spec)
  ;; parse an emacs key specification string → a LIST of key tokens
  ;; e.g. "C-M-a" → ("CTRL" "ALT" "a"), "<f1>" → ("F1")
  (let loop ((str spec) (acc '()))
    (let ((dash (hl--string-index str #\-)))
      (if (and dash (> dash 0))
          (let ((prefix (substring str 0 dash)))
            (if (assoc prefix hl--emacs-mods)
                (loop (substring str (+ dash 1) (string-length str))
                      (cons (cdr (assoc prefix hl--emacs-mods)) acc))
                (reverse (cons (hl--emacs-key str) acc))))
          (reverse (cons (hl--emacs-key str) acc))))))

(define (hl-key spec)
  ;; parse a lua/hyprland key specification string → a LIST of key tokens
  ;; e.g. "SUPER+SHIFT+Q" → ("SUPER" "SHIFT" "Q")
  (map hl--trim (hl--split-string spec #\+)))

(define (hl-exec-shell! cmd)
  (c-hl-exec cmd))

;; hl-after/hl-repeat return hl-timer RECORDS; these control them afterwards.
;; A one-shot releases its own record lock when it completes; a repeating
;; timer lives until reload (or hl-timer-cancel!).
(define (hl-after ms thunk)
  (let ((rec (make-hl-timer ms thunk)))
    (if (= 0 (c-hl-timer rec (exact ms) 0))
        rec
        (errorf 'hl-after "timer ~ams rejected, see compositor log" ms))))

(define (hl-repeat ms thunk)
  (let ((rec (make-hl-timer ms thunk)))
    (if (= 0 (c-hl-timer rec (exact ms) 1))
        rec
        (errorf 'hl-repeat "timer ~ams rejected, see compositor log" ms))))

;; returns #f when no window has focus
(define (hl-active-title)
  (c-hl-active-title))

;; collections are marshalled as one newline-joined string and split here
(define (hl-workspaces)
  (let ((ids (c-hl-workspace-names)))
    (if (eq? ids #f)
        '()
        (map hl--mint-workspace ids))))



;; handles are opaque records whose single field is a guardian CELL
;; ((address . 0) — see the mint helpers and the C++ IHandle comment): the
;; address is a heap weak ref C++-side, and the record's death deletes it
;; through the guardian. Stale handles simply report #f from every getter;
;; the address is the only thing crossing FFI.
(define-record-type hl-window (fields cell))
(define-record-type hl-workspace (fields cell))
(define-record-type hl-monitor (fields cell))

(define (hl-window-id w) (car (hl-window-cell w)))
(define (hl-workspace-id w) (car (hl-workspace-cell w)))
(define (hl-monitor-id w) (car (hl-monitor-cell w)))

;; ---- handle lifetime ---------------------------------------------------------
;; mint = record + guardian cell; the cell dies with the record, the guardian
;; yields it, and the collect-request-handler below frees the C++ weak ref.
;; (Upstream: userdata embed the weak ref and the Lua GC __gc destructs it.)
(define hl--handle-guardian (make-guardian))

(define (hl--mint-window v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-window "implausible handle value ~s" v)))
      (r (make-hl-window cell)))
    (hl--handle-guardian cell)
    r))

(define (hl--mint-workspace v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-workspace "implausible handle value ~s" v)))
      (r (make-hl-workspace cell)))
    (hl--handle-guardian cell)
    r))

(define (hl--mint-monitor v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-monitor "implausible handle value ~s" v)))
      (r (make-hl-monitor cell)))
    (hl--handle-guardian cell)
    r))

;; notifications use the same handle model; the per-handle paused state
;; lives in the C++ handle object (see SNotificationHandle)
(define-record-type hl-notification (fields cell))
(define (hl-notification-id n) (car (hl-notification-cell n)))

(define (hl--mint-notification v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-notification "implausible handle value ~s" v)))
      (r (make-hl-notification cell)))
    (hl--handle-guardian cell)
    r))

;; events: the record IS the handle — the event type (a full symbol like
;; 'window-open) and the handler thunk. C++ locks the record inside the bus
;; connection (SThunkRef); when the connection is torn down at reload or
;; unload, the record unlocks and the handler becomes collectable.
(define-record-type hl-event (fields type thunk))

;; timers: the record IS the handle — the initial interval and the thunk.
;; C++ locks it inside the timer's fire callback; a one-shot timer releases
;; its own lock when it completes, and reload tears the rest down.
(define-record-type hl-timer (fields interval thunk))

;; binds: the record IS the handle — the token LIST (the bind's meaning)
;; and the thunk. C++ locks the record while the bind is registered (see
;; SThunkRef) and stamps its pinned address into the bind's argument tag;
;; the record becomes collectable when the bind is unbound (or dies at
;; reload) and the user drops it. No bind ids, no registries.
(define-record-type hl-bind (fields tokens thunk))

(define c-hl-workspace-name (foreign-procedure "hl-workspace-name" (integer-64) scheme-object))
(define c-hl-workspace-addressable-name (foreign-procedure "hl-workspace-addressable-name" (integer-64) scheme-object))
(define c-hl-workspace-number (foreign-procedure "hl-workspace-number" (integer-64) scheme-object))
(define c-hl-workspace-monitor (foreign-procedure "hl-workspace-monitor" (integer-64) scheme-object))
(define c-hl-workspace-special (foreign-procedure "hl-workspace-special" (integer-64) scheme-object))
(define c-hl-workspace-active (foreign-procedure "hl-workspace-active" (integer-64) scheme-object))
(define c-hl-workspace-visible (foreign-procedure "hl-workspace-visible" (integer-64) scheme-object))
(define c-hl-workspace-empty (foreign-procedure "hl-workspace-empty" (integer-64) scheme-object))
(define c-hl-workspace-persistent (foreign-procedure "hl-workspace-persistent" (integer-64) scheme-object))
(define c-hl-workspace-has-urgent (foreign-procedure "hl-workspace-has-urgent" (integer-64) scheme-object))
(define c-hl-workspace-has-fullscreen (foreign-procedure "hl-workspace-has-fullscreen" (integer-64) scheme-object))
(define c-hl-workspace-fullscreen-mode (foreign-procedure "hl-workspace-fullscreen-mode" (integer-64) scheme-object))
(define c-hl-workspace-fullscreen-window (foreign-procedure "hl-workspace-fullscreen-window" (integer-64) scheme-object))
(define c-hl-workspace-last-window (foreign-procedure "hl-workspace-last-window" (integer-64) scheme-object))
(define c-hl-workspace-window-count (foreign-procedure "hl-workspace-window-count" (integer-64) scheme-object))
(define c-hl-workspace-group-count (foreign-procedure "hl-workspace-group-count" (integer-64) scheme-object))
(define c-hl-workspace-tiled-layout (foreign-procedure "hl-workspace-tiled-layout" (integer-64) scheme-object))
(define c-hl-workspace-alive (foreign-procedure "hl-workspace-alive" (integer-64) scheme-object))
(define c-hl-workspace-same (foreign-procedure "hl-workspace-same" (integer-64 integer-64) scheme-object))
(define c-hl-workspace-selector (foreign-procedure "hl-workspace-selector" (integer-64) scheme-object))

(define c-hl-monitor-name (foreign-procedure "hl-monitor-name" (integer-64) scheme-object))
(define c-hl-monitor-description (foreign-procedure "hl-monitor-description" (integer-64) scheme-object))
(define c-hl-monitor-number (foreign-procedure "hl-monitor-number" (integer-64) scheme-object))
(define c-hl-monitor-enabled (foreign-procedure "hl-monitor-enabled" (integer-64) scheme-object))
(define c-hl-monitor-focused (foreign-procedure "hl-monitor-focused" (integer-64) scheme-object))
(define c-hl-monitor-x (foreign-procedure "hl-monitor-x" (integer-64) scheme-object))
(define c-hl-monitor-y (foreign-procedure "hl-monitor-y" (integer-64) scheme-object))
(define c-hl-monitor-width (foreign-procedure "hl-monitor-width" (integer-64) scheme-object))
(define c-hl-monitor-height (foreign-procedure "hl-monitor-height" (integer-64) scheme-object))
(define c-hl-monitor-scale (foreign-procedure "hl-monitor-scale" (integer-64) scheme-object))
(define c-hl-monitor-transform (foreign-procedure "hl-monitor-transform" (integer-64) scheme-object))
(define c-hl-monitor-refresh-rate (foreign-procedure "hl-monitor-refresh-rate" (integer-64) scheme-object))
(define c-hl-monitor-mode (foreign-procedure "hl-monitor-mode" (integer-64) scheme-object))
(define c-hl-monitor-dpms (foreign-procedure "hl-monitor-dpms" (integer-64) scheme-object))
(define c-hl-monitor-vrr (foreign-procedure "hl-monitor-vrr" (integer-64) scheme-object))
(define c-hl-monitor-10bit (foreign-procedure "hl-monitor-10bit" (integer-64) scheme-object))
(define c-hl-monitor-reserved (foreign-procedure "hl-monitor-reserved" (integer-64) scheme-object))
(define c-hl-monitor-serial (foreign-procedure "hl-monitor-serial" (integer-64) scheme-object))
(define c-hl-monitor-physical-size (foreign-procedure "hl-monitor-physical-size" (integer-64) scheme-object))
(define c-hl-monitor-mirrors (foreign-procedure "hl-monitor-mirrors" (integer-64) scheme-object))
(define c-hl-monitor-available-modes (foreign-procedure "hl-monitor-available-modes" (integer-64) scheme-object))
(define c-hl-monitor-hardware-details (foreign-procedure "hl-monitor-hardware-details" (integer-64) scheme-object))
(define c-hl-monitor-mirror-of (foreign-procedure "hl-monitor-mirror-of" (integer-64) scheme-object))
(define c-hl-monitor-active-workspace (foreign-procedure "hl-monitor-active-workspace" (integer-64) scheme-object))
(define c-hl-monitor-active-special-workspace (foreign-procedure "hl-monitor-active-special-workspace" (integer-64) scheme-object))
(define c-hl-monitor-alive (foreign-procedure "hl-monitor-alive" (integer-64) scheme-object))
(define c-hl-monitor-same (foreign-procedure "hl-monitor-same" (integer-64 integer-64) scheme-object))
(define c-hl-monitor-selector (foreign-procedure "hl-monitor-selector" (integer-64) scheme-object))

(define (hl-active-window)
  (let ((id (c-hl-active-window-id)))
    (if (< id 0) #f (hl--mint-window id))))

(define (hl-windows)
  (let ((ids (c-hl-window-ids)))
    (if (eq? ids #f)
        '()
        (map hl--mint-window ids))))

(define (hl-window-title w)
  (c-hl-window-title (hl-window-id w)))

(define (hl-window-alive? w)
  (= 1 (c-hl-window-alive (hl-window-id w))))

(define (hl-window-close! w)
  (= 0 (c-hl-window-close (hl-window-id w))))

(define (hl-window-class w)
  (c-hl-window-class (hl-window-id w)))

;; => workspace handle, or #f
(define (hl-window-workspace w)
  (let ((id (c-hl-window-workspace-id (hl-window-id w))))
    (and id (hl--mint-workspace id))))

;; => monitor handle, or #f
(define (hl-window-monitor w)
  (let ((id (c-hl-window-monitor-id (hl-window-id w))))
    (and id (hl--mint-monitor id))))

(define (hl-window-floating? w)
  (= 1 (c-hl-window-floating (hl-window-id w))))

;; => (width . height), or #f when stale
(define (hl-window-size w)
  (c-hl-window-size (hl-window-id w)))   ; (width . height), or #f

(define (hl-window-pid w)
  (c-hl-window-pid (hl-window-id w)))

(define (hl-window-focus! w)
  (= 0 (c-hl-window-focus (hl-window-id w))))

;; act: 'toggle (default), 'on, 'off
(define (hl-window-float-set! w . on?)
  (= 0 (c-hl-window-float-act (hl--wid w) (hl--bool-act on?))))

(define (hl-window-workspace-set! w ws)
  (= 0 (c-hl-window-move-to-workspace (hl--wid w) (hl--ws-arg ws))))

;; convenience: move a window to another monitor. upstream has no direct
;; window→monitor dispatch — it moves to the target's ACTIVE workspace
;; (LuaBindingsDispatchers.cpp:451-458), which this mirrors.
(define (hl-window-monitor-set! w mon)
  (hl-window-workspace-set! w (hl-monitor-active-workspace mon)))

;; ---- actions: navigation and geometry --------------------------------------
;; directions: "l"/"r"/"u"/"d" or 'left/'right/'up/'down. Window args accept
;; a handle or #f (= active window). Action results: #t on success, #f on
;; rejection (message in the compositor log).

(define (hl-workspace-focus! ws)
  (= 0 (c-hl-focus-workspace (hl--ws-arg ws))))

(define (hl-focus-direction-set! dir)
  (= 0 (c-hl-focus-direction (hl--dir dir))))

(define (hl-monitor-focus! mon)
  (= 0 (c-hl-focus-monitor (hl--mon-arg mon))))

(define (hl-focus-last!)
  (= 0 (c-hl-focus-last)))

(define (hl-focus-urgent!)
  (= 0 (c-hl-focus-urgent)))

(define (hl-window-move-direction! w dir)
  (= 0 (c-hl-window-move-direction (hl--wid w) (hl--dir dir))))

(define (hl-window-swap-direction! w dir)
  (= 0 (c-hl-window-swap-direction (hl--wid w) (hl--dir dir))))

;; prev: 'prev or #t swaps backwards
(define (hl-window-swap-next! w . opt)
  (= 0 (c-hl-window-swap-next (hl--wid w) (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 (if (car opt) 1 0))))))

(define (hl-window-swap-with! w other)
  (= 0 (c-hl-window-swap-with (hl--wid w) (hl-window-id other))))

;; opts: 'prev, 'tiled, 'floating (combinable symbols)
(define (hl-window-cycle! . opt)
  (let loop ((rest opt) (next 1) (filter 0))
    (cond ((null? rest)
           (= 0 (c-hl-window-cycle -1 next filter)))
          ((eq? (car rest) 'prev) (loop (cdr rest) 0 filter))
          ((eq? (car rest) 'tiled) (loop (cdr rest) next 1))
          ((eq? (car rest) 'floating) (loop (cdr rest) next 2))
          (else (loop (cdr rest) next filter)))))

(define (hl-window-center! w)
  (= 0 (c-hl-window-center (hl--wid w))))

;; absolute by default; 'relative (or 'rel) makes the deltas relative
(define (hl-window-size-set! w width height . opt)
  (= 0 (c-hl-window-resize-px (hl--wid w) (exact->inexact width) (exact->inexact height)
         (if (null? opt) 0 (if (memq (car opt) '(relative rel)) 1 0)))))

(define (hl-window-position-set! w x y . opt)
  (= 0 (c-hl-window-move-px (hl--wid w) (exact->inexact x) (exact->inexact y)
         (if (null? opt) 0 (if (memq (car opt) '(relative rel)) 1 0)))))

(define (hl-window-pinned-set! w . on?)
  (= 0 (c-hl-window-pin-act (hl--wid w) (hl--bool-act on?))))

(define (hl-window-pseudo-set! w . on?)
  (= 0 (c-hl-window-pseudo (hl--wid w) (hl--bool-act on?))))

(define (hl-window-kill! w)
  (= 0 (c-hl-window-kill (hl--wid w))))

(define (hl-window-signal! w sig)
  (= 0 (c-hl-window-signal (hl--wid w) sig)))

;; mode: "up" | "down" | "top" | "bottom" (alterZOrder mode string)
(define (hl-window-zorder-set! w mode)
  (= 0 (c-hl-window-zorder (hl--wid w) (hl--str mode))))

(define (hl-window-prop-set! w prop val)
  (= 0 (c-hl-window-set-prop (hl--wid w) (hl--str prop) (hl--str val))))

(define (hl-window-tag-add! w tag)
  (= 0 (c-hl-window-tag (hl--wid w) (hl--str tag))))

(define (hl-window-tags-clear! w)
  (= 0 (c-hl-window-clear-tags (hl--wid w))))

(define (hl-window-swallow-toggle!)
  (= 0 (c-hl-toggle-swallow)))

;; ---- actions: groups --------------------------------------------------------

;; W is a window (or #f for the active window); absent ON? → the dedicated
;; C++ membership toggle, #t/#f → explicit set. Getter: (hl-window-group? w).
(define (hl-window-group-set! w . on?)
  (if (null? on?)
      (= 0 (c-hl-group-toggle (hl--wid w)))
      (= 0 (c-hl-group-set (hl--wid w) (if (car on?) 1 0)))))

(define (hl-window-group? w)
  (= 1 (c-hl-window-in-group (hl-window-id w))))

(define (hl-group-cycle! w . opt)
  (= 0 (c-hl-group-cycle (hl--wid w) (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

(define (hl-group-window-active! w index)
  (= 0 (c-hl-group-index (hl--wid w) index)))

(define (hl-group-window-move-next! w . opt)
  (= 0 (c-hl-group-move-window (hl--wid w) (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

;; GLOBAL group lock: locks all groups compositor-wide (upstream lockGroups —
;; there is no window involved). Getter: (hl-groups-locked?).
(define (hl-groups-lock-set! . on?)
  (= 0 (c-hl-group-lock (hl--bool-act on?))))

(define (hl-groups-locked?)
  (= 1 (c-hl-groups-locked)))

;; per-group lock: the group of window W (#f = active window); absent ON? →
;; toggle, #t/#f → set. Getter: (hl-window-group-lock? w).
(define (hl-window-group-lock-set! w . on?)
  (= 0 (c-hl-window-group-lock (hl--wid w) (hl--bool-act on?))))

(define (hl-window-group-lock? w)
  (= 1 (c-hl-window-group-locked (hl-window-id w))))

(define (hl-window-group-move-in! w dir)
  (= 0 (c-hl-window-into-group (hl--wid w) (hl--dir dir))))

(define (hl-window-group-move-out! w dir)
  (= 0 (c-hl-window-out-of-group (hl--wid w) (hl--dir dir))))

(define (hl-window-group-move-in-or-create! w dir)
  (= 0 (c-hl-window-into-or-create-group (hl--wid w) (hl--dir dir))))

(define (hl-window-deny-from-group-set! w . on?)
  (= 0 (c-hl-window-deny-from-group (hl--wid w) (hl--bool-act on?))))

;; ---- actions: workspaces and monitors ---------------------------------------

(define (hl-workspace-name-set! old new)
  (= 0 (c-hl-workspace-rename (hl--ws-arg old) (hl--str new))))

(define (hl-workspace-monitor-set! ws mon)
  (= 0 (c-hl-workspace-move-monitor (hl--ws-arg ws) (hl--mon-arg mon))))

(define (hl--special-name s)
  ;; normalize a special-workspace name/selector to its bare name, so an
  ;; open/close comparison survives a leading "special:" on either side
  (if (and (> (string-length s) 8) (string=? (substring s 0 8) "special:"))
      (substring s 8 (string-length s))
      s))

;; WS . ON? — absent toggles the named special workspace on the FOCUSED monitor
;; (the C++ toggle creates it on demand); #t/#f force open/close through the
;; same toggle when the focused monitor's active special workspace already
;; equals/differs from the target.
(define (hl-workspace-special-set! ws . on?)
  (if (null? on?)
      (= 0 (c-hl-workspace-toggle-special (hl--ws-arg ws)))
      (let* ((mon (hl-active-monitor))
             (active (and mon (hl-monitor-active-special-workspace mon)))
             (cur (and active (hl--special-name (hl-workspace-name active))))
             (target (hl--special-name (hl--ws-arg ws))))
        (if (eqv? (car on?) (and cur (string=? cur target)))
            0
            (= 0 (c-hl-workspace-toggle-special (hl--ws-arg ws)))))))

;; explicit monitor-scoped set: opens WS (a special workspace name or handle,
;; created when missing) on MON; #f closes whatever special workspace is open
;; there. The workspace must be named — a closed monitor keeps no record of its
;; last special workspace, so there is no toggling needing no argument.
(define (hl-monitor-workspace-special-set! mon ws)
  (= 0 (c-hl-monitor-set-special (hl--mon-arg mon) (if ws (hl--ws-arg ws) ""))))

(define (hl-monitor-swap! mon1 mon2)
  (= 0 (c-hl-workspace-swap-monitors (hl--mon-arg mon1) (hl--mon-arg mon2))))

;; change a numbered workspace's ID (upstream validates: must be > 0, not in
;; use, and only NUMBERED workspaces can be re-IDed — named/special cannot)
(define (hl-workspace-id-set! ws new-id)
  (= 0 (c-hl-workspace-change-id (hl--ws-arg ws) (exact->inexact new-id))))

;; ---- actions: cursor and misc -----------------------------------------------

(define (hl-cursor-move! x y)
  (= 0 (c-hl-cursor-move (exact->inexact x) (exact->inexact y))))

(define (hl-cursor-move-to-corner! w corner)
  (= 0 (c-hl-cursor-corner (hl--wid w) corner)))

;; DANGER: quits Hyprland
(define (hl-exit!)
  (= 0 (c-hl-exit)))

(define (hl-config-reload!)
  (= 0 (c-hl-reload-config)))

(define (hl-force-renderer-reload!)
  (= 0 (c-hl-force-renderer-reload)))

;; act: 'toggle/'on/'off; mon: #f (all) or a monitor handle/name
;; MON . ON? — MON is a monitor (or #f for all); absent meaning of ON? is
;; toggle, #t on, #f off
(define (hl-monitor-power-set! mon . on?)
  (= 0 (c-hl-dpms (hl--bool-act on?) (if mon (hl--mon-arg mon) ""))))

(define (hl-force-idle! seconds)
  (= 0 (c-hl-force-idle (exact->inexact seconds))))

(define (hl-global! action)
  (= 0 (c-hl-global (hl--str action))))

(define (hl-event! data)
  (= 0 (c-hl-event (hl--str data))))

(define (hl-window-pass-shortcut! w)
  (= 0 (c-hl-pass (hl--wid w))))

;; mods: mask int (SHIFT 1 CAPS 2 CTRL 4 ALT 8 MOD2 16 MOD3 32 META 64 MOD5 128)
;; key: xkb keycode
(define (hl-window-send-shortcut! mods key . w)
  (= 0 (c-hl-send-shortcut mods key (if (null? w) -1 (hl--wid (car w))))))

(define (hl-window-send-key-state! mods key state . w)
  (= 0 (c-hl-send-key-state mods key state (if (null? w) -1 (hl--wid (car w))))))

;; interactive drag/resize for mouse binds: (hl-mouse-action! "drag") / (hl-mouse-action! "resize")
(define (hl-mouse-action! action)
  (= 0 (c-hl-mouse (hl--str action))))

(define (hl-release-input-capture!)
  (= 0 (c-hl-release-input-capture)))

;; send a message to the active workspace's layout (see custom-layouts)
(define (hl-layout-msg msg)
  (= 0 (c-hl-layout-message (hl--str msg))))

;; ---- config -----------------------------------------------------------------
;; set/get config options from scheme. keys accept "general:gaps_in" or
;; "general.gaps_in". values: number, string, boolean, list (vec2-style
;; array), or a PLIST for tables (e.g. '(top 10 bottom 10)).
;; writes propagate like a runtime hl.config — the affected subsystems
;; refresh immediately.
(define (hl-config-add! key val)
  (c-hl-config-begin)
  (hl--push-val val)
  (if (= 0 (c-hl-config-set (hl--str key)))
      #t
      (errorf 'hl-config-add! "~a" (c-hl-config-last-error))))

(define (hl--push-val v)
  (cond ((number? v)
         ;; exact integers must reach INT options as integers (upstream
         ;; rejects doubles for them); FLOAT options accept integers too
         (if (exact? v)
             (c-hl-config-push-int (exact->inexact v))
             (c-hl-config-push-num v)))
        ((boolean? v) (c-hl-config-push-bool (if v 1 0)))
        ((string? v) (c-hl-config-push-str v))
        ((and (pair? v) (symbol? (car v)))
         ;; plist → hash table (the API's nested named-fields form)
         (c-hl-config-tbl-open 1)
         (hl--plist-fold (lambda (k val _)
                           (c-hl-config-tbl-key (hl--str k))
                           (hl--push-val val)
                           (c-hl-config-tbl-set-hash))
                         0 v))
        ((and (list? v) (or (null? v) (not (pair? (car v)))))
         ;; array table (e.g. a vec2 '(20 20))
         (c-hl-config-tbl-open 0)
         (let loop ((rest v) (i 1))
           (unless (null? rest)
             (hl--push-val (car rest))
             (c-hl-config-tbl-seti i)
             (loop (cdr rest) (+ i 1)))))
        (else (errorf 'hl-config-add! "unsupported value ~s" v))))

(define (hl-config-get key)
  (c-hl-config-get (hl--str key)))   ; #t/#f, number, string, or a plist for tables

(define c-hl-device-add (foreign-procedure "hl-scheme-device-add" (string scheme-object) int))

;; per-device input config: NAME + a spread plist of fields. Write-only
;; (upstream hl.device parity — no device read side, no per-field unset);
;; validated against the field table and applied to the live device.
(define (hl-device-add! name . fields)
  (if (= 0 (c-hl-device-add (hl--str name) fields))
      #t
      (errorf 'hl-device-add! "~a" (c-hl-config-last-error))))

(define c-hl-exec-with-rule (foreign-procedure "hl-exec-with-rule" (string scheme-object) int))

;; spawn CMD under a one-shot rule built from an effects PLIST ('float #t
;; 'workspace "games" ...). effects only — no match: the executor tags the
;; spawned window by pid itself. values go through the same rule-spec
;; coercion as hl-window-rule-add! (numbers/bools → config strings).
(define (hl-exec-with-rule! cmd . fields)
  (define (flat l)
    (cond ((null? l) '())
          ((null? (cdr l)) (errorf 'hl-exec-with-rule! "odd plist of effects"))
          (else (list* (hl--str (car l))
                       (hl--rule-spec-value (cadr l))
                       (flat (cddr l))))))
  (let ((pid (c-hl-exec-with-rule (hl--str cmd) (flat fields))))
    (if (> pid 0)
        pid
        (errorf 'hl-exec-with-rule! "~a" (c-hl-config-last-error)))))

;; ---- rules -------------------------------------------------------------------
;; window/layer rules: a plist with 'match (a plist of property → value),
;; optional 'name and 'enabled, and every other key being an EFFECT (its
;; config string form). Returns a rule handle for hl-rule-set-enabled /
;; hl-rule-enabled?. Anonymous rules (name #f) are re-created on each
;; config reload; named rules are reused across calls.
;;   (hl-window-rule-add! "term" '((match . ((class . "foot"))) (float . #t)
;;                            (opacity . "0.8") (workspace . "3")))
;;   (hl-window-rule-add! #f '((match . ((class . "(?i)games"))) (monitor . "DP-1")))
;; match properties: class title initial_class initial_title floating tag
;;   xwayland fullscreen pinned focus group modal on_workspace content
;;   namespace exec_token exec_pid ...
;; effects: float tile fullscreen maximize fullscreen_state move size center
;;   pseudo monitor workspace no_initial_focus pin group suppress_event
;;   content no_close_for scrolling_width rounding opacity border_color
;;   idle_inhibit animation tag min_size max_size ... (the window-rule page
;;   has the full list with value forms)
;; rules: the record IS the handle - the rule KIND ('window or 'layer) and
;; the NAME (or "" for anonymous). The index C++-side locks the record while
;; the rule is registered (rules live for their config generation); a stale
;; record from a dead generation just fails to resolve.
(define-record-type hl-rule (fields kind name))

(define (hl--rule-spec-value v)
  (cond ((string? v) v)
        ((boolean? v) (if v "true" "false"))
        ((number? v) (number->string v))
        (else #f)))

(define (hl--window-rule-mk name spec begin-fn effect-fn commit-fn what kind)
  (let ((enabled (hl--plist-get spec 'enabled #t)))
    (if (not (= 0 (begin-fn (if name (hl--str name) "") (if enabled 1 0))))
        (errorf what "~a" (c-hl-config-last-error))
        (let loop ((rest spec))
          (cond ((null? rest)
                 ;; done: make the handle record, commit, index it C++-side
                 (let ((rec (make-hl-rule kind (or name ""))))
                   (if (not (= 0 (commit-fn rec)))
                       (errorf what "~a" (c-hl-config-last-error))
                       rec)))
                (else
                 (let* ((tail (hl--plist-cdr rest))
                        (k  (hl--str (car rest)))
                        (v  (car tail)))
                   (cond ((equal? k "match")
                          (let mloop ((m v))
                            (cond ((null? m) (loop (cdr tail)))
                                  (else
                                   (let* ((mtail (hl--plist-cdr m))
                                          (mk (hl--str (car m)))
                                          (sv (hl--rule-spec-value (car mtail))))
                                     (if (not sv)
                                         (errorf what "bad match value for ~a" mk)
                                         (if (= 0 (c-hl-rule-match mk sv))
                                             (mloop (cdr mtail))
                                             (errorf what "~a" (c-hl-config-last-error)))))))))
                         ((equal? k "enabled")
                          (loop (cdr tail)))
                         (else
                          (let ((sv (hl--rule-spec-value v)))
                            (if (not sv)
                                (errorf what "bad effect value for ~a" k)
                                (if (= 0 (effect-fn k sv))
                                    (loop (cdr tail))
                                    (errorf what "~a" (c-hl-config-last-error))))))))))))))

(define (hl-window-rule-add! name . spec)
  (hl--window-rule-mk name spec c-hl-window-rule-begin c-hl-window-rule-effect c-hl-window-rule-commit 'hl-window-rule-add! 'window))

(define (hl-layer-rule-add! name . spec)
  (hl--window-rule-mk name spec c-hl-layer-rule-begin c-hl-layer-rule-effect c-hl-layer-rule-commit 'hl-layer-rule-add! 'layer))

;; absent -> toggle (via the enabled? query); #t/#f -> explicit set
(define (hl-rule-enabled-set! rule . on?)
  (if (= 0 (c-hl-rule-set-enabled rule
                            (if (if (null? on?) (not (hl-rule-enabled? rule)) (car on?)) 1 0)))
      #t
      (errorf 'hl-rule-enabled-set! "unknown rule")))

(define (hl-rule-enabled? rule)
  (= 1 (c-hl-rule-enabled rule)))

;; ---- groups as objects (upstream HL.Group parity) ----------------------------
;; a group is the tabbed-window arrangement on a workspace; groups dissolve
;; behind our backs, so handles follow the weak-handle model (stale -> #f).
;; passives only: state reads + two mutators; NO callbacks (upstream parity).
(define-record-type hl-group (fields cell))
(define (hl-group-id g) (car (hl-group-cell g)))

(define (hl--mint-group v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-group "implausible handle value ~s" v)))
      (r (make-hl-group cell)))
    (hl--handle-guardian cell)
    r))

;; => list of hl-group records on the workspace
(define (hl-workspace-groups WS)
  (let ((l (c-hl-workspace-groups (hl-workspace-id WS))))
    (if l (map hl--mint-group l) '())))

(define (hl-group-members g)
  (let ((l (c-hl-group-members (hl-group-id g))))
    (if l (map hl--mint-window l) '())))

(define (hl-group-size g)
  (c-hl-group-size (hl-group-id g)))

;; => the currently-focused member as a window handle, or #f
(define (hl-group-current g)
  (let ((id (c-hl-group-current (hl-group-id g))))
    (and id (hl--mint-window id))))

;; => 1-based position of the focused member
(define (hl-group-current-index g)
  (c-hl-group-current-idx (hl-group-id g)))

(define (hl-group-locked? g)
  (eq? (c-hl-group-locked (hl-group-id g)) #t))

(define (hl-group-denied? g)
  (eq? (c-hl-group-denied (hl-group-id g)) #t))

(define (hl-group-alive? g)
  (eq? (c-hl-group-alive (hl-group-id g)) #t))

(define (hl-group=? a b)
  (= 1 (c-hl-group-same (hl-group-id a) (hl-group-id b))))

;; add WINDOW to the group, optionally at a 1-based INDEX; errors when the
;; group is denied or the window cannot be grouped into it
(define (hl-group-add! g window . index)
  (if (= 0 (c-hl-group-add (hl-group-id g) (hl-window-id window)
                           (if (null? index) -1 (exact (car index)))))
      #t
      (errorf 'hl-group-add! "~a" (c-hl-config-last-error))))

(define (hl-group-remove! g window)
  (if (= 0 (c-hl-group-remove (hl-group-id g) (hl-window-id window)))
      #t
      (errorf 'hl-group-remove! "~a" (c-hl-config-last-error))))

;; workspace rules: (hl-workspace-rule-add! "3" 'monitor "DP-1" 'layout "master")
;; fields: monitor default persistent gaps_in gaps_out float_gaps border_size
;;   no_border no_rounding decorate no_shadow on_created_empty default_name
;;   layout animation layout_opts
(define (hl-workspace-rule-add! ws . spec)
  (let ((enabled (hl--plist-get spec 'enabled #t)))
    (if (not (= 0 (c-hl-workspace-rule-begin (hl--ws-arg ws) (if enabled 1 0))))
        (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))
        (let loop ((rest spec))
          (cond ((null? rest)
                 (if (= 0 (c-hl-workspace-rule-commit))
                     #t
                     (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                (else
                 (let* ((tail (hl--plist-cdr rest))
                        (k  (hl--str (car rest)))
                        (v  (car tail)))
                   (cond ((member k (list "workspace" "enabled"))
                          (loop (cdr tail)))
                         ((equal? k "layout_opts")
                          (let oloop ((o v))
                            (cond ((null? o) (loop (cdr tail)))
                                  (else
                                   (let* ((otail (hl--plist-cdr o))
                                          (sv (hl--rule-spec-value (car otail))))
                                     (if (not sv)
                                         (errorf 'hl-workspace-rule-add! "bad layout_opts value")
                                         (if (= 0 (c-hl-workspace-rule-layout-opt (hl--str (car o)) sv))
                                             (oloop (cdr otail))
                                             (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error)))))))))
                         ((member k (list "gaps_in" "gaps_out" "float_gaps"))
                          ;; css-gap fields: scalars and per-side plists both
                          ;; go through the gap parser, whatever their type
                          (c-hl-config-begin)
                          (hl--push-val v)
                          (if (= 0 (c-hl-workspace-rule-gap k))
                              (loop (cdr tail))
                              (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                         ((string? v)
                          (if (= 0 (c-hl-workspace-rule-str k v))
                              (loop (cdr tail))
                              (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                         ((number? v)
                          (if (= 0 (c-hl-workspace-rule-num k (exact->inexact v)))
                              (loop (cdr tail))
                              (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                         ((boolean? v)
                          (if (= 0 (c-hl-workspace-rule-bool k (if v 1 0)))
                              (loop (cdr tail))
                              (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                         (else (errorf 'hl-workspace-rule-add! "unsupported value for ~a" k))))))))))

;; ---- queries ------------------------------------------------------------------
;; selectors use the config selector syntax: "class:^foot$", "title:foo",
;; "pid:123", "address:0x...", "workspace:3", "floating", "tiled", ...
;; (hl-window-from SELECTOR) -> window handle | #f
(define (hl-window-from sel)
  (let ((id (c-hl-window-from (hl--str sel))))
    (if (< id 0) #f (hl--mint-window (inexact->exact id)))))

(define (hl-urgent-window)
  (let ((id (c-hl-urgent-window)))
    (if (< id 0) #f (hl--mint-window (inexact->exact id)))))

(define (hl-last-window)
  (let ((id (c-hl-last-window)))
    (if (< id 0) #f (hl--mint-window (inexact->exact id)))))

;; monitors resolve by name or "desc:DESCRIPTION" (config monitor syntax)
;; monitor queries => monitor handle, or #f
(define (hl-monitor-from sel)
  (let ((id (c-hl-monitor-from (hl--str sel))))
    (and id (hl--mint-monitor id))))

(define (hl-monitor-at x y)
  (let ((id (c-hl-monitor-at (exact->inexact x) (exact->inexact y))))
    (and id (hl--mint-monitor id))))

(define (hl-monitor-at-cursor)
  (let ((id (c-hl-monitor-at-cursor)))
    (and id (hl--mint-monitor id))))

(define (hl-active-monitor)
  (let ((id (c-hl-active-monitor)))
    (and id (hl--mint-monitor id))))

;; workspace queries => workspace handle, or #f
(define (hl-active-workspace)
  (let ((id (c-hl-active-workspace)))
    (and id (hl--mint-workspace id))))

(define (hl-active-special-workspace)
  (let ((id (c-hl-active-special-workspace)))
    (and id (hl--mint-workspace id))))

(define (hl-last-workspace)
  (let ((id (c-hl-last-workspace)))
    (and id (hl--mint-workspace id))))

;; windows on a workspace (a workspace handle or selector: "3", "name:foo",
;; "special:bar") => list of window handles
(define (hl-workspace-windows ws)
  (let ((s (c-hl-workspace-windows (hl--ws-arg ws))))
    (if (not s)
        '()
        (map hl--mint-window s))))

;; ---- workspace/monitor handle getters ---------------------------------------
;; every getter takes a handle; a stale or dead handle yields #f from every
;; getter, like an expired object in upstream Lua. Getters mirror Lua's
;; workspace/monitor object fields 1:1.

;; handles are accepted anywhere a selector string is: the handle resolves
;; through its canonical selector, like upstream's selector-or-object
;; helpers. A dead handle resolves to "" and the action fails cleanly.
(define (hl--ws-arg ws)
  (if (hl-workspace? ws)
      (or (c-hl-workspace-selector (hl-workspace-id ws)) "")
      (hl--str ws)))

(define (hl--mon-arg m)
  (if (hl-monitor? m)
      (or (c-hl-monitor-selector (hl-monitor-id m)) "")
      (hl--str m)))

;; -- workspace getters
(define (hl-workspace-name w)
  (c-hl-workspace-name (hl-workspace-id w)))

(define (hl-workspace-addressable-name w)
  (c-hl-workspace-addressable-name (hl-workspace-id w)))

;; the workspace's numbered ID, or #f for named/special workspaces
(define (hl-workspace-number w)
  (c-hl-workspace-number (hl-workspace-id w)))

;; => monitor handle, or #f
(define (hl-workspace-monitor w)
  (let ((id (c-hl-workspace-monitor (hl-workspace-id w))))
    (and id (hl--mint-monitor id))))

(define (hl-workspace-special? w)
  (eq? (c-hl-workspace-special (hl-workspace-id w)) #t))

;; #t when this workspace is the active (or active special) one on its monitor
(define (hl-workspace-active? w)
  (eq? (c-hl-workspace-active (hl-workspace-id w)) #t))

(define (hl-workspace-visible? w)
  (eq? (c-hl-workspace-visible (hl-workspace-id w)) #t))

(define (hl-workspace-empty? w)
  (eq? (c-hl-workspace-empty (hl-workspace-id w)) #t))

(define (hl-workspace-persistent? w)
  (eq? (c-hl-workspace-persistent (hl-workspace-id w)) #t))

(define (hl-workspace-has-urgent? w)
  (eq? (c-hl-workspace-has-urgent (hl-workspace-id w)) #t))

(define (hl-workspace-has-fullscreen? w)
  (eq? (c-hl-workspace-has-fullscreen (hl-workspace-id w)) #t))

;; internal fullscreen mode (0 none / 1 max / 2 full), -1 when none
(define (hl-workspace-fullscreen-mode w)
  (c-hl-workspace-fullscreen-mode (hl-workspace-id w)))

;; => window handle, or #f
(define (hl-workspace-fullscreen-window w)
  (let ((id (c-hl-workspace-fullscreen-window (hl-workspace-id w))))
    (and id (hl--mint-window id))))

;; => window handle, or #f
(define (hl-workspace-last-window w)
  (let ((id (c-hl-workspace-last-window (hl-workspace-id w))))
    (and id (hl--mint-window id))))

(define (hl-workspace-window-count w)
  (c-hl-workspace-window-count (hl-workspace-id w)))

(define (hl-workspace-group-count w)
  (c-hl-workspace-group-count (hl-workspace-id w)))

;; the tiled layout currently serving the workspace (string)
(define (hl-workspace-tiled-layout w)
  (c-hl-workspace-tiled-layout (hl-workspace-id w)))

(define (hl-workspace-alive? w)
  (eq? (c-hl-workspace-alive (hl-workspace-id w)) #t))

(define (hl-workspace=? a b)
  (eq? (c-hl-workspace-same (hl-workspace-id a) (hl-workspace-id b)) #t))

;; -- monitor getters
(define (hl-monitor-name m)
  (c-hl-monitor-name (hl-monitor-id m)))

(define (hl-monitor-description m)
  (c-hl-monitor-description (hl-monitor-id m)))

(define (hl-monitor-number m)
  (c-hl-monitor-number (hl-monitor-id m)))

(define (hl-monitor-enabled? m)
  (eq? (c-hl-monitor-enabled (hl-monitor-id m)) #t))

(define (hl-monitor-focused? m)
  (eq? (c-hl-monitor-focused (hl-monitor-id m)) #t))

(define (hl-monitor-x m)
  (c-hl-monitor-x (hl-monitor-id m)))

(define (hl-monitor-y m)
  (c-hl-monitor-y (hl-monitor-id m)))

(define (hl-monitor-width m)
  (c-hl-monitor-width (hl-monitor-id m)))

(define (hl-monitor-height m)
  (c-hl-monitor-height (hl-monitor-id m)))

(define (hl-monitor-scale m)
  (c-hl-monitor-scale (hl-monitor-id m)))

;; rotation/flip transform (0-7), see the monitor docs
(define (hl-monitor-transform m)
  (c-hl-monitor-transform (hl-monitor-id m)))

(define (hl-monitor-refresh-rate m)
  (c-hl-monitor-refresh-rate (hl-monitor-id m)))

;; current mode as "WIDTHxHEIGHT@RATE"
(define (hl-monitor-mode m)
  (c-hl-monitor-mode (hl-monitor-id m)))

(define (hl-monitor-power? m)
  (eq? (c-hl-monitor-dpms (hl-monitor-id m)) #t))

(define (hl-monitor-vrr? m)
  (eq? (c-hl-monitor-vrr (hl-monitor-id m)) #t))

(define (hl-monitor-10bit? m)
  (eq? (c-hl-monitor-10bit (hl-monitor-id m)) #t))

;; reserved area as a plist: (top n left n right n bottom n)
(define (hl-monitor-reserved m)
  (c-hl-monitor-reserved (hl-monitor-id m)))

;; the monitor's serial (string), or #f when stale
(define (hl-monitor-serial m)
  (c-hl-monitor-serial (hl-monitor-id m)))

;; physical size in mm: (w . h), or #f
(define (hl-monitor-physical-size m)
  (c-hl-monitor-physical-size (hl-monitor-id m)))

;; monitor handles mirroring this one (or #f when none)
(define (hl-monitor-mirrors m)
  (or (c-hl-monitor-mirrors (hl-monitor-id m)) '()))

;; available modes: ((width w height h refresh-rate r preferred b) ...)
(define (hl-monitor-available-modes m)
  (or (c-hl-monitor-available-modes (hl-monitor-id m)) '()))

;; hardware details: a plist (backend "..." hdr b chroma b bt2020 b vrr-capable b)
(define (hl-monitor-hardware-details m)
  (c-hl-monitor-hardware-details (hl-monitor-id m)))

;; the monitor this one mirrors, as a handle; #f when not a mirror
(define (hl-monitor-mirror-of m)
  (let ((id (c-hl-monitor-mirror-of (hl-monitor-id m))))
    (and id (hl--mint-monitor id))))

;; => workspace handle, or #f
(define (hl-monitor-active-workspace m)
  (let ((id (c-hl-monitor-active-workspace (hl-monitor-id m))))
    (and id (hl--mint-workspace id))))

;; => workspace handle, or #f when no special workspace is open
(define (hl-monitor-active-special-workspace m)
  (let ((id (c-hl-monitor-active-special-workspace (hl-monitor-id m))))
    (and id (hl--mint-workspace id))))

(define (hl-monitor-alive? m)
  (eq? (c-hl-monitor-alive (hl-monitor-id m)) #t))

(define (hl-monitor-rule-add!=? a b)
  (eq? (c-hl-monitor-same (hl-monitor-id a) (hl-monitor-id b)) #t))

;; => list of (monitor . namespace) pairs
;; ---- layer surfaces as objects (upstream HL.LayerSurface parity) -------------
;; layer surfaces die independently -> weak handles via the guardian;
;; every getter returns #f when the surface is gone. NO callbacks.
;; filters are plist-style: (hl-layers), (hl-layers 'monitor MON),
;; (hl-layers 'namespace "ns"), or combined.
(define-record-type hl-layer (fields cell))
(define (hl-layer-id s) (car (hl-layer-cell s)))

(define (hl--mint-layer v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-layer "implausible handle value ~s" v)))
      (r (make-hl-layer cell)))
    (hl--handle-guardian cell)
    r))

(define (hl-layers . filters)
  (let* ((mon  (hl--plist-get filters 'monitor #f))
         (ns   (hl--plist-get filters 'namespace #f))
         (monId (if (and mon (hl-monitor? mon)) (hl-monitor-id mon) 0))
         (l (c-hl-layers monId (and ns (hl--str ns)))))
    (if l (map hl--mint-layer l) '())))

(define (hl-layer-alive? s)
  (= 1 (c-hl-layer-alive (hl-layer-id s))))

(define (hl-layer=? a b)
  (= 1 (c-hl-layer-same (hl-layer-id a) (hl-layer-id b))))

;; => stable 0x… identity (same idea as hl-window-address)
(define (hl-layer-address s)
  (c-hl-layer-address (hl-layer-id s)))

(define (hl-layer-pid s)
  (c-hl-layer-pid (hl-layer-id s)))

;; => monitor handle
(define (hl-layer-monitor s)
  (let ((id (c-hl-layer-monitor (hl-layer-id s))))
    (and id (hl--mint-monitor id))))

(define (hl-layer-namespace s)
  (c-hl-layer-namespace (hl-layer-id s)))

;; => int — shell layer: 0 background / 1 bottom / 2 top / 3 overlay
(define (hl-layer-level s)
  (c-hl-layer-level (hl-layer-id s)))

(define (hl-layer-mapped? s)
  (eq? (c-hl-layer-mapped (hl-layer-id s)) #t))

;; => int — 0 none / 1 exclusive / 2 on-demand (lock screens: exclusive)
(define (hl-layer-kb-interactivity s)
  (c-hl-layer-kb-interactivity (hl-layer-id s)))

(define (hl-layer-above-fullscreen? s)
  (eq? (c-hl-layer-above-fs (hl-layer-id s)) #t))

;; => (x . y), monitor-local
(define (hl-layer-position s)
  (c-hl-layer-position (hl-layer-id s)))

;; => (width . height)
(define (hl-layer-size s)
  (c-hl-layer-size (hl-layer-id s)))

;; key by keysym name: (hl-is-key-down "Return")
(define (hl-is-key-down key)
  (= 1 (c-hl-is-key-down key)))

(define (hl-loaded-plugins)
  (or (c-hl-loaded-plugins) '()))

(define (hl-version)
  (c-hl-version))

;; ALL windows matching a selector: (hl-windows-from "class:^foot$")
(define (hl-windows-from sel)
  (let ((s (c-hl-windows-from (hl--str sel))))
    (if (not s)
        '()
        (map hl--mint-window s))))

;; which fullscreen handler a window uses (string)
(define (hl-window-fullscreen-handler w)
  (c-hl-window-fullscreen-handler (hl--wid w)))

;; ---- notifications ------------------------------------------------------------
;; (hl-notify! "text" 5000) or with options:
;;   (hl-notify! "text" 5000 '((icon . "info") (color . "0x80FF80FF") (font-size . 13)))
;; icons: none warn info hint error/err confused/question ok
;; color: 0xAARRGGBB hex string; 0 color = default for the icon
(define (hl-notify! text duration . opts)
  (let ((s (c-hl-notify (hl--str text) (exact->inexact duration)
                        (hl--str (hl--plist-get opts 'icon "none"))
                        (hl--str (hl--plist-get opts 'color "0"))
                        (exact->inexact (hl--plist-get opts 'font-size 13)))))
    (if s #t #f)))

;; ---- live notification objects (upstream hl.notification parity) ------------
;; (hl-notification-add! 'text "…" 'timeout ms ['icon "info"] ['color "…"]
;;   ['font-size n]) => a notification HANDLE. 'timeout is required; pause
;; freezes the timeout timer (the bubble stays until dismissed); expired
;; handles read #f and mutate as no-ops.
(define c-hl-notification-add (foreign-procedure "hl-notification-add" (scheme-object) scheme-object))
(define c-hl-notification-list (foreign-procedure "hl-notification-list" () scheme-object))
(define c-hl-notification-text (foreign-procedure "hl-notification-text" (integer-64) scheme-object))
(define c-hl-notification-timeout (foreign-procedure "hl-notification-timeout" (integer-64) scheme-object))
(define c-hl-notification-color (foreign-procedure "hl-notification-color" (integer-64) scheme-object))
(define c-hl-notification-icon (foreign-procedure "hl-notification-icon" (integer-64) scheme-object))
(define c-hl-notification-font-size (foreign-procedure "hl-notification-font-size" (integer-64) scheme-object))
(define c-hl-notification-elapsed (foreign-procedure "hl-notification-elapsed" (integer-64) scheme-object))
(define c-hl-notification-age (foreign-procedure "hl-notification-age" (integer-64) scheme-object))
(define c-hl-notification-alive (foreign-procedure "hl-notification-alive" (integer-64) scheme-object))
(define c-hl-notification-same (foreign-procedure "hl-notification-same" (integer-64 integer-64) scheme-object))
(define c-hl-notification-text-set (foreign-procedure "hl-notification-text-set" (integer-64 string) int))
(define c-hl-notification-timeout-set (foreign-procedure "hl-notification-timeout-set" (integer-64 double) int))
(define c-hl-notification-color-set (foreign-procedure "hl-notification-color-set" (integer-64 string) int))
(define c-hl-notification-icon-set (foreign-procedure "hl-notification-icon-set" (integer-64 scheme-object) int))
(define c-hl-notification-font-size-set (foreign-procedure "hl-notification-font-size-set" (integer-64 double) int))
(define c-hl-notification-paused-set (foreign-procedure "hl-notification-paused-set" (integer-64 int) int))
(define c-hl-notification-paused-q (foreign-procedure "hl-notification-paused-q" (integer-64) scheme-object))
(define c-hl-notification-dismiss (foreign-procedure "hl-notification-dismiss" (integer-64) int))

(define (hl-notification-add! . fields)
  (let ((id (c-hl-notification-add fields)))
    (if id
        (hl--mint-notification id)
        (errorf 'hl-notification-add! "~a" (c-hl-config-last-error)))))

;; => list of live notification handles
(define (hl-notifications)
  (let ((l (c-hl-notification-list)))
    (if l (map hl--mint-notification l) '())))

(define (hl-notification-text n)
  (c-hl-notification-text (hl-notification-id n)))

(define (hl-notification-timeout n)
  (c-hl-notification-timeout (hl-notification-id n)))

;; => the raw 0xAARRGGBB integer (readback parity with upstream)
(define (hl-notification-color n)
  (c-hl-notification-color (hl-notification-id n)))

;; => the icon's hyprctl id (names exist on the write side only, upstream parity)
(define (hl-notification-icon n)
  (c-hl-notification-icon (hl-notification-id n)))

(define (hl-notification-font-size n)
  (c-hl-notification-font-size (hl-notification-id n)))

;; ms excluding paused spans / ms since creation
(define (hl-notification-elapsed n)
  (c-hl-notification-elapsed (hl-notification-id n)))

(define (hl-notification-age n)
  (c-hl-notification-age (hl-notification-id n)))

(define (hl-notification-alive? n)
  (eq? (c-hl-notification-alive (hl-notification-id n)) #t))

(define (hl-notification=? a b)
  (eq? (c-hl-notification-same (hl-notification-id a) (hl-notification-id b)) #t))

;; failed setters raise with the config system's own message
(define (hl--notif-act who act)
  (if (= 0 act) #t (errorf who "~a" (c-hl-config-last-error))))

(define (hl-notification-text-set! n text)
  (hl--notif-act 'hl-notification-text-set!
    (c-hl-notification-text-set (hl-notification-id n) (hl--str text))))

(define (hl-notification-timeout-set! n ms)
  (hl--notif-act 'hl-notification-timeout-set!
    (c-hl-notification-timeout-set (hl-notification-id n) (exact->inexact ms))))

(define (hl-notification-color-set! n color)
  (hl--notif-act 'hl-notification-color-set!
    (c-hl-notification-color-set (hl-notification-id n) (hl--str color))))

(define (hl-notification-icon-set! n icon)
  (hl--notif-act 'hl-notification-icon-set!
    (c-hl-notification-icon-set (hl-notification-id n) icon)))

(define (hl-notification-font-size-set! n size)
  (hl--notif-act 'hl-notification-font-size-set!
    (c-hl-notification-font-size-set (hl-notification-id n) (exact->inexact size))))

;; absent => toggle (via the paused? query); #t/#f => explicit
(define (hl-notification-paused-set! n . on?)
  (= 0 (c-hl-notification-paused-set (hl-notification-id n)
         (if (if (null? on?) (not (eq? (hl-notification-paused? n) #t)) (eq? (car on?) #t)) 1 0))))

(define (hl-notification-paused? n)
  (eq? (c-hl-notification-paused-q (hl-notification-id n)) #t))

(define (hl-notification-dismiss! n)
  (= 0 (c-hl-notification-dismiss (hl-notification-id n))))

;; ---- timer handles --------------------------------------------------------------
;; control functions take the hl-timer record
;; absent → toggle (via the enabled? query); #t/#f → explicit set
(define (hl-timer-enabled-set! t . on?)
  (if (= 0 (c-hl-timer-set-enabled t
                                   (if (if (null? on?) (not (hl-timer-enabled? t)) (car on?)) 1 0)))
      #t
      (errorf 'hl-timer-enabled-set! "unknown timer")))
(define (hl-timer-enabled? t)
  (= 1 (c-hl-timer-enabled t)))
(define (hl-timer-set-timeout t ms)
  (if (= 0 (c-hl-timer-set-timeout t (exact->inexact ms)))
      #t
      (errorf 'hl-timer-set-timeout "timeout must be >= 1ms")))

;; beyond-upstream extension: kill a repeating timer before reload. The
;; index entry erases itself; the record lock releases with the timer.
(define (hl-timer-cancel! t)
  (= 0 (c-hl-timer-cancel t)))

;; ---- exec variants ----------------------------------------------------------------
;; (hl-exec! "cmd") — no shell; the string is execvp'd (space-split)
;; (hl-exec-shell-with-rules! "[float size 800 500] mygame") — classic exec rules
(define (hl-exec! cmd)
  (> (c-hl-exec-raw cmd) 0))
(define (hl-exec-shell-with-rules! cmd)
  (> (c-hl-exec-with-rules cmd) 0))

;; ---- gestures ----------------------------------------------------------------------
;; Gesture actions are typed values, not strings: an hl-gesture-action is an
;; opaque (maker . args) pair built by the hl-make-*-gesture constructors
;; below — one per upstream built-in action class, plus hl-make-custom-gesture
;; for callback-backed ones. Built-in and custom actions are indistinguishable
;; from the caller's side. A recipe is a pure value: registering it under two
;; different specs (e.g. 2-finger and 3-finger swipes both opening a special
;; workspace) constructs two independent C++ gestures from the same recipe.
;;
;; (hl-gesture-add! 'fingers N 'direction "dir" 'action ACTION
;;                  ['mods "SUPER"] ['scale 1.0] ['disable-inhibit #t])
;; fingers + direction required (upstream hl.gesture parity); the optional
;; fields describe the gesture INPUT, independent of the action. Returns an
;; hl-gesture handle for (hl-gesture-remove! G) — the manager matches removal
;; on the registration spec, never on the action (supersedes upstream's
;; action = "unset" string).
(define (hl-gesture-action? x)
  (and (pair? x) (integer? (car x)) (exact? (car x)) (positive? (car x)) (list? (cdr x))))

;; one optional mode argument, restricted to the allowed symbols
(define (hl--gesture-mode name args allowed default)
  (cond ((null? args) default)
        ((and (= 1 (length args)) (memq (car args) allowed)) (car args))
        (else (errorf name "mode must be one of ~a" allowed))))

(define (hl-make-workspace-swipe-gesture)
  (cons (c-hl-gesture-maker-workspace-swipe) '()))

(define (hl-make-move-gesture)
  (cons (c-hl-gesture-maker-move) '()))

(define (hl-make-resize-gesture)
  (cons (c-hl-gesture-maker-resize) '()))

(define (hl-make-close-gesture)
  (cons (c-hl-gesture-maker-close) '()))

(define (hl-make-scroll-move-gesture)
  (cons (c-hl-gesture-maker-scroll-move) '()))

;; 'toggle (default) / 'float / 'tile — force a direction of floating
(define (hl-make-float-gesture . mode)
  (let ((m (hl--gesture-mode 'hl-make-float-gesture mode '(toggle float tile) 'toggle)))
    (cons (c-hl-gesture-maker-float) (list 'mode m))))

;; 'fullscreen (default) / 'maximize
(define (hl-make-fullscreen-gesture . mode)
  (let ((m (hl--gesture-mode 'hl-make-fullscreen-gesture mode '(fullscreen maximize) 'fullscreen)))
    (cons (c-hl-gesture-maker-fullscreen) (list 'mode m))))

;; toggles the named special workspace (empty string = the default special)
(define (hl-make-special-workspace-gesture name)
  (unless (string? name)
    (errorf 'hl-make-special-workspace-gesture "workspace name must be a string, got ~a" name))
  (cons (c-hl-gesture-maker-special) (list 'name name)))

;; ZOOM (number) / 'toggle (default) / 'mult / 'live — the numeric argument
;; is unused in live mode, so 1 is a good placeholder there
(define (hl-make-cursor-zoom-gesture zoom . mode)
  (unless (real? zoom)
    (errorf 'hl-make-cursor-zoom-gesture "zoom must be a number, got ~a" zoom))
  (let ((m (hl--gesture-mode 'hl-make-cursor-zoom-gesture mode '(toggle mult live) 'toggle)))
    (cons (c-hl-gesture-maker-cursor-zoom) (list 'zoom (exact->inexact zoom) 'mode m))))

;; (hl-make-custom-gesture ['start FN] ['update FN] ['finish FN]) — at least
;; one procedure required; each is applied the gesture event plist as spread
;; args (see bind-gestures.md for the fields). The three closures share state
;; naturally by closing over a let — wrap the constructor in a function to get
;; fresh state per registration.
(define (hl-make-custom-gesture . fields)
  (define known '(start update finish))
  (when (null? fields)
    (errorf 'hl-make-custom-gesture "at least one of 'start, 'update, 'finish is required"))
  (let loop ((l fields))
    (unless (null? l)
      (unless (memq (car l) known)
        (errorf 'hl-make-custom-gesture "unknown field ~a" (car l)))
      (unless (and (pair? (cdr l)) (procedure? (cadr l)))
        (errorf 'hl-make-custom-gesture "field ~a needs a procedure" (car l)))
      (loop (cddr l))))
  (let build ((l fields) (acc '()))
    (if (null? l)
        (cons (c-hl-gesture-maker-custom) acc)
        (build (cddr l) (cons (cadr l) (cons (car l) acc))))))

(define (hl-gesture-add! . fields)
  (define known '(fingers direction mods scale disable-inhibit action))
  (for-each (lambda (k)
              (unless (memq k known)
                (errorf 'hl-gesture-add! "unknown field ~a" k)))
            (let loop ((l fields) (acc '()))
              (if (null? l) (reverse acc) (loop (cddr l) (cons (car l) acc)))))
  (let* ((fingers   (hl--plist-get fields 'fingers #!eof))
         (direction (hl--plist-get fields 'direction #!eof))
         (action    (hl--plist-get fields 'action #!eof))
         (mods      (hl--plist-get fields 'mods ""))
         (scale     (hl--plist-get fields 'scale 1.0))
         (inhibit   (hl--plist-get fields 'disable-inhibit #f)))
    (cond ((eq? fingers #!eof)
           (errorf 'hl-gesture-add! "field 'fingers' is required"))
          ((not (and (integer? fingers) (exact? fingers) (>= fingers 2)))
           (errorf 'hl-gesture-add! "field 'fingers' must be an integer >= 2"))
          ((eq? direction #!eof)
           (errorf 'hl-gesture-add! "field 'direction' is required"))
          ((not (string? direction))
           (errorf 'hl-gesture-add! "field 'direction' must be a string"))
          ((eq? action #!eof)
           (errorf 'hl-gesture-add! "an action is required — 'action (hl-make-...-gesture ...)"))
          ((not (hl-gesture-action? action))
           (errorf 'hl-gesture-add! "field 'action' must be an hl-gesture-action (see the hl-make-*-gesture constructors)"))
          ((not (or (<= -10.0 scale -0.1) (<= 0.1 scale 10.0)))
           (errorf 'hl-gesture-add! "field 'scale' must be between -10 and -0.1 or between 0.1 and 10 - it is currently: ~a" scale))
          (else
           (let ((rc (c-hl-gesture action fingers (hl--str direction) (hl--str mods) (exact->inexact scale) (if inhibit 1 0))))
             (cond ((not (= 0 rc))
                    (errorf 'hl-gesture-add! "~a" (c-hl-config-last-error)))
                   (else
                    ;; the handle is the exact registration spec — the manager
                    ;; matches removal on it, so remove! always hits our gesture
                    (list fingers direction (hl--str mods) scale inhibit))))))))

;; the hl-gesture handle: (fingers direction mods scale disable-inhibit)
(define (hl-gesture? x)
  (and (pair? x) (list? x) (= 5 (length x)) (integer? (car x)) (string? (cadr x))))

;; → #t removed / #f nothing registered under that spec / error
(define (hl-gesture-remove! g)
  (unless (hl-gesture? g)
    (errorf 'hl-gesture-remove! "not an hl-gesture handle"))
  (let ((rc (c-hl-gesture-remove (car g) (cadr g) (caddr g) (exact->inexact (cadddr g))
                                 (if (list-ref g 4) 1 0))))
    (cond ((= rc 0) #t)
          ((= rc 1) #f)
          (else (errorf 'hl-gesture-remove! "~a" (c-hl-config-last-error))))))

;; ---- monitors, curves, animations, permissions ------------------------------

;; (hl-monitor-rule-add! "DP-1" '((mode . "preferred") (scale . "1.6") (position . "0x0")
;;                      (transform . 0) (bitdepth . 10) (vrr . 1)
;;                      (reserved . '((top . 60)))))
;; string fields: mode position scale mirror cm icc sdr_eotf
;; numeric fields: transform bitdepth vrr supports_wide_color supports_hdr
;;   sdrbrightness sdrsaturation sdr_min_luminance sdr_max_luminance
;;   min_luminance max_luminance max_avg_luminance
;; gap fields (plist): reserved / reserved_area ; bool: disabled
(define (hl-monitor-rule-add! output . fields)
  (if (not (= 0 (c-hl-monitor-begin (hl--mon-arg output))))
      (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))
      (let loop ((rest fields))
        (cond ((null? rest)
               (if (= 0 (c-hl-monitor-commit))
                   #t
                   (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
              (else
               (let* ((tail (hl--plist-cdr rest))
                      (f  (hl--str (car rest)))
                      (v  (car tail)))
                 (cond ((string? v)
                        (if (= 0 (c-hl-monitor-field-str f v))
                            (loop (cdr tail))
                            (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
                       ((number? v)
                        (if (= 0 (c-hl-monitor-field-num f (exact->inexact v)))
                            (loop (cdr tail))
                            (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
                       ((boolean? v)
                        (if (= 0 (c-hl-monitor-field-bool f (if v 1 0)))
                            (loop (cdr tail))
                            (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
                       ((pair? v)
                        (c-hl-config-begin)
                        (hl--push-val v)
                        (if (= 0 (c-hl-monitor-field-gap f))
                            (loop (cdr tail))
                            (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
                       (else (errorf 'hl-monitor-rule-add! "unsupported value for ~a" f)))))))))

;; (hl-curve-add! "mycurve" 'bezier 0.25 0.1 0.25 1.0)
;; (hl-curve-add! "myspring" 'spring 250 25 1)
(define (hl-curve-add! name type . vals)
  (let* ((t  (if (eq? type 'spring) 1 0))
         ;; pad THEN coerce: exact zeros are invalid in foreign double slots
         (vs (map exact->inexact (append vals (list 0 0 0 0)))))
    (if (= 0 (c-hl-curve-add (hl--str name) t
                             (list-ref vs 0) (list-ref vs 1) (list-ref vs 2) (list-ref vs 3)))
        #t
        (errorf 'hl-curve-add! "~a" (c-hl-config-last-error)))))

;; (hl-animation-add! "windowsIn" 'enabled #t 'speed 8 'curve "mycurve" 'style "popin 80%")
;; speed defaults to 8; declare curves with hl-curve-add! first (or use builtins
;; like "default"); styles are the animation style strings (popin, slide, ...)
(define (hl-animation-add! leaf . opts)
  (let* ((enabled (hl--plist-get opts 'enabled #t))
         (speed   (hl--plist-get opts 'speed 8))
         (curve   (hl--plist-get opts 'curve ""))
         (style   (hl--plist-get opts 'style "")))
    (if (= 0 (c-hl-animation-set (hl--str leaf) (if enabled 1 0)
                                 (exact->inexact speed) (hl--str curve) (hl--str style)))
        #t
        (errorf 'hl-animation-add! "~a" (c-hl-config-last-error)))))

;; (hl-permission-add! "/usr/bin/grim" 'screencopy 'allow)
;; only takes effect at first launch — permission rules require a
;; compositor restart (same as the lua config)
(define (hl-permission-add! binary type mode)
  (if (= 0 (c-hl-permission-add binary (hl--str type) (hl--str mode)))
      #t
      (errorf 'hl-permission-add! "~a" (c-hl-config-last-error))))


;; toggles; modes mirror Fullscreen::eFullscreenMode (1 maximized, 2 fullscreen)
;; optional second arg = mode (default 2); (hl-window-fullscreen w 1) maximizes
;; mode-aware toggle: mode 2 = fullscreen, 1 = maximized
;; absent → the dedicated C++ fullscreen toggle (fullscreen flavour);
;; #t/#f → explicit set
(define (hl-window-fullscreen-set! w . on?)
  (if (null? on?)
      (= 0 (c-hl-window-fullscreen-toggle (hl--wid w) 2))
      (= 0 (c-hl-window-fullscreen-set (hl--wid w) (if (car on?) 2 0)))))

(define (hl-window-maximized-set! w . on?)
  (if (null? on?)
      (= 0 (c-hl-window-fullscreen-toggle (hl--wid w) 1))
      (= 0 (c-hl-window-fullscreen-set (hl--wid w) (if (car on?) 1 0)))))

;; explicit state: (internal-mode client-mode layout-aware?) — modes 0/1/2
(define (hl-window-fullscreen-state w internal client . layout-aware)
  (= 0 (c-hl-window-fullscreen-state (hl--wid w) internal client
         (if (null? layout-aware) 0 (if (car layout-aware) 1 0)))))

;; 0 = none, 1 = maximized, 2 = fullscreen, -1 = stale
(define (hl-window-fullscreen-mode w)
  (c-hl-window-fullscreen-mode (hl-window-id w)))

(define (hl-window-hidden? w)
  (= 1 (c-hl-window-hidden (hl-window-id w))))

;; ---- window read-side fields (upstream LuaWindow field parity) --------------
;; => "0x..." — the window's stable address (identity across queries;
;;    handles differ per call, the address does not)
(define (hl-window-address w)
  (c-hl-window-address (hl-window-id w)))

(define (hl-window-mapped? w)
  (= 1 (c-hl-window-mapped (hl-window-id w))))

;; mapped AND accepting input AND non-zero alpha
(define (hl-window-visible? w)
  (= 1 (c-hl-window-visible (hl-window-id w))))

(define (hl-window-accepts-input? w)
  (= 1 (c-hl-window-accepts-input (hl-window-id w))))

;; => (x . y), or #f when stale
(define (hl-window-position w)
  (c-hl-window-position (hl-window-id w)))

(define (hl-window-pin-fullscreened? w)
  (= 1 (c-hl-window-pin-fullscreened (hl-window-id w))))

(define (hl-window-allowed-over-fullscreen? w)
  (= 1 (c-hl-window-allowed-over-fullscreen (hl-window-id w))))

(define (hl-window-tearing-hint? w)
  (= 1 (c-hl-window-tearing-hint (hl-window-id w))))

(define (hl-window-inhibiting-idle? w)
  (= 1 (c-hl-window-inhibiting-idle (hl-window-id w))))

;; => int — 0 is the most recently focused window; -1 when not in history
(define (hl-window-focus-history-id w)
  (c-hl-window-focus-history-id (hl-window-id w)))

;; => "none" / "photo" / "video" / "game"
(define (hl-window-content-type w)
  (c-hl-window-content-type (hl-window-id w)))

;; => hex string of the metadata stable id
(define (hl-window-stable-id w)
  (c-hl-window-stable-id (hl-window-id w)))

;; => list of tag strings (static tags, cf. hl-window-tag-add!)
(define (hl-window-tags w)
  (c-hl-window-tags (hl-window-id w)))

;; => the window this one is swallowing, or #f
(define (hl-window-swallowing w)
  (let ((id (c-hl-window-swallowing-id (hl-window-id w))))
    (and id (hl--mint-window id))))

;; xdg shell metadata, or #f when unset
(define (hl-window-xdg-tag w)
  (c-hl-window-xdg-tag (hl-window-id w)))

(define (hl-window-xdg-description w)
  (c-hl-window-xdg-description (hl-window-id w)))

;; => plist: ('name "master" 'is-master #f 'perc-master 0.5 'perc-size 1.0)
;;          | ('name "scrolling" 'column (index n width f windows (…))
;;               'index-in-column n)
;; #f when the window is floating or has no tiled layout target
(define (hl-window-layout w)
  (c-hl-window-layout (hl-window-id w)))

(define (hl-window-pinned? w)
  (= 1 (c-hl-window-pinned (hl-window-id w))))

(define (hl-window-pseudo? w)
  (= 1 (c-hl-window-pseudo-query (hl--wid w))))

(define (hl-window-maximized? w)
  (= 1 (c-hl-window-maximized-query (hl--wid w))))

(define (hl-window-deny-from-group? w)
  (= 1 (c-hl-window-group-denied (hl--wid w))))

;; read back a dynamic window prop that hl-window-prop-set! writes: #t/#f for
;; booleans, numbers for opacities and border/rounding, #f for unknown props
(define (hl-window-prop w prop)
  (c-hl-window-prop (hl--wid w) (hl--str prop)))

(define (hl-window-initial-class w)
  (c-hl-window-initial-class (hl-window-id w)))

(define (hl-window-initial-title w)
  (c-hl-window-initial-title (hl-window-id w)))

(define (hl-window-x11? w)
  (= 1 (c-hl-window-x11 (hl-window-id w))))

(define (hl-monitors)
  (let ((ids (c-hl-monitor-names)))
    (if (eq? ids #f)
        '()
        (map hl--mint-monitor ids))))

;; ---- events ------------------------------------------------------------------
;; each notification-add! creates an hl-event record (type symbol + handler),
;; hands it to C++ (locked inside the bus connection), and returns the record.
;; handler shapes are per event — see the events wiki page.

;; ---- window events (one C++ entry, a which-number per kind) -------------------
(define (hl--window-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (c-hl-window-event-listen rec which))
        rec
        (errorf who "listener rejected, see compositor log"))))

;; fires when a window opens (fully initialized, window rules applied)
(define (hl-window-open-notification-add! handler)
  (hl--window-listen 0 'window-open handler 'hl-window-open-notification-add!))

(define (hl-window-close-notification-add! handler)
  (hl--window-listen 1 'window-close handler 'hl-window-close-notification-add!))

;; handlers receive the window handle
(define (hl-window-title-notification-add! handler)
  (hl--window-listen 2 'window-title handler 'hl-window-title-notification-add!))

(define (hl-window-class-notification-add! handler)
  (hl--window-listen 3 'window-class handler 'hl-window-class-notification-add!))

(define (hl-window-urgent-notification-add! handler)
  (hl--window-listen 4 'window-urgent handler 'hl-window-urgent-notification-add!))

(define (hl-window-pin-notification-add! handler)
  (hl--window-listen 5 'window-pin handler 'hl-window-pin-notification-add!))

(define (hl-window-fullscreen-notification-add! handler)
  (hl--window-listen 6 'window-fullscreen handler 'hl-window-fullscreen-notification-add!))

;; fires when a window moves to a different workspace
(define (hl-window-move-to-workspace-notification-add! handler)
  (hl--window-listen 7 'window-move-to-workspace handler 'hl-window-move-to-workspace-notification-add!))

;; fires when the focused window changes
(define (hl-window-active-notification-add! handler)
  (hl--window-listen 8 'window-active handler 'hl-window-active-notification-add!))

;; handler signature: (lambda (w state) ...) — state is #t when minimized
(define (hl-window-minimize-notification-add! handler)
  (let ((rec (make-hl-event 'window-minimize handler)))
    (if (= 0 (c-hl-window-minimize-listen rec))
        rec
        (errorf 'hl-window-minimize-notification-add! "listener rejected, see compositor log"))))

;; fires when a window is created and mapped, but before window rules are
;; applied (window-open waits for full initialization)
(define (hl-window-open-early-notification-add! handler)
  (hl--window-listen 9 'window-open-early handler 'hl-window-open-early-notification-add!))

;; fires when the window is forcefully killed, e.g. via hyprctl kill
(define (hl-window-kill-notification-add! handler)
  (hl--window-listen 10 'window-kill handler 'hl-window-kill-notification-add!))

;; fires when a window rings the system bell, even if it's muted
(define (hl-window-bell-notification-add! handler)
  (hl--window-listen 11 'window-bell handler 'hl-window-bell-notification-add!))

;; fires when a window's rules are re-evaluated, e.g. on a title change
(define (hl-window-update-rules-notification-add! handler)
  (hl--window-listen 12 'window-update-rules handler 'hl-window-update-rules-notification-add!))

;; lifecycle: fires once when the session starts (its first render frame) and
;; once before exit. handlers registered after startup fire immediately.
(define (hl-start-notification-add! handler)
  (let ((rec (make-hl-event 'start handler)))
    (if (= 0 (c-hl-lifecycle-listen rec 0))
        rec
        (errorf 'hl-start-notification-add! "listener rejected, see compositor log"))))

(define (hl-shutdown-notification-add! handler)
  (let ((rec (make-hl-event 'shutdown handler)))
    (if (= 0 (c-hl-lifecycle-listen rec 1))
        rec
        (errorf 'hl-shutdown-notification-add! "listener rejected, see compositor log"))))

(define (hl-config-reloaded-notification-add! handler)
  (let ((rec (make-hl-event 'config-reloaded handler)))
    (if (= 0 (c-hl-config-reloaded-listen rec))
        rec
        (errorf 'hl-config-reloaded-notification-add! "listener rejected, see compositor log"))))

;; fires BEFORE a config reload (upstream config.unload → config.preReload)
(define (hl-config-unload-notification-add! handler)
  (let ((rec (make-hl-event 'config-unload handler)))
    (if (= 0 (c-hl-config-unload-listen rec))
        rec
        (errorf 'hl-config-unload-notification-add! "listener rejected, see compositor log"))))

;; zero-argument callback — upstream delivers nil for window.destroy (the bus
;; event is a weak ref; identity belongs to the window-close notification)
(define (hl-window-destroy-notification-add! handler)
  (let ((rec (make-hl-event 'window-destroy handler)))
    (if (= 0 (c-hl-window-destroy-listen rec))
        rec
        (errorf 'hl-window-destroy-notification-add! "listener rejected, see compositor log"))))

;; layer callbacks receive the layer's namespace string
(define (hl-layer-open-notification-add! handler)
  (let ((rec (make-hl-event 'layer-open handler)))
    (if (= 0 (c-hl-layer-listen rec 0))
        rec
        (errorf 'hl-layer-open-notification-add! "listener rejected, see compositor log"))))

(define (hl-layer-close-notification-add! handler)
  (let ((rec (make-hl-event 'layer-close handler)))
    (if (= 0 (c-hl-layer-listen rec 1))
        rec
        (errorf 'hl-layer-close-notification-add! "listener rejected, see compositor log"))))

;; high-frequency: fires on every key event — keep handlers trivial
(define (hl-keyboard-key-notification-add! handler)
  (let ((rec (make-hl-event 'keyboard-key handler)))
    (if (= 0 (c-hl-keyboard-key-listen rec))
        rec
        (errorf 'hl-keyboard-key-notification-add! "listener rejected, see compositor log"))))

(define (hl-screenshare-state-notification-add! handler)
  (let ((rec (make-hl-event 'screenshare-state handler)))
    (if (= 0 (c-hl-screenshare-listen rec))
        rec
        (errorf 'hl-screenshare-state-notification-add! "listener rejected, see compositor log"))))

;; handler signature: (lambda (scheduled?) ...) — #t when the prop refresh ran
;; as scheduled, #f when it was executed prematurely
(define (hl-config-props-refreshed-notification-add! handler)
  (let ((rec (make-hl-event 'config-props-refreshed handler)))
    (if (= 0 (c-hl-config-props-refreshed-listen rec))
        rec
        (errorf 'hl-config-props-refreshed-notification-add! "listener rejected, see compositor log"))))

(define (hl-submap-notification-add! handler)
  (let ((rec (make-hl-event 'submap handler)))
    (if (= 0 (c-hl-submap-listen rec))
        rec
        (errorf 'hl-submap-notification-add! "listener rejected, see compositor log"))))

;; ---- workspace events ---------------------------------------------------------
(define (hl-workspace-active-notification-add! handler)
  (let ((rec (make-hl-event 'workspace-active handler)))
    (if (= 0 (c-hl-workspace-active-listen rec))
        rec
        (errorf 'hl-workspace-active-notification-add! "listener rejected, see compositor log"))))

;; handler signature: (lambda (ws) ...) — ws is a workspace handle
(define (hl--workspace-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (c-hl-workspace-event-listen rec which))
        rec
        (errorf who "listener rejected, see compositor log"))))

;; fires when a workspace is created
(define (hl-workspace-created-notification-add! handler)
  (hl--workspace-listen 0 'workspace-created handler 'hl-workspace-created-notification-add!))

;; fires when a workspace is removed. The handle it passes is BORN DEAD —
;; every getter on it returns #f, exactly like an expired object in upstream
;; Lua. See the comment at the C++ listener for why the name is not provided.
(define (hl-workspace-removed-notification-add! handler)
  (hl--workspace-listen 1 'workspace-removed handler 'hl-workspace-removed-notification-add!))

;; fires when the opened special workspace on a monitor changes; handler
;; signature: (lambda (ws mon) ...) — ws is #f when no special workspace is
;; open on that monitor
(define (hl-workspace-special-active-notification-add! handler)
  (hl--workspace-listen 2 'workspace-special-active handler 'hl-workspace-special-active-notification-add!))

;; fires when a workspace moves to a different monitor: (lambda (ws mon) ...)
(define (hl-workspace-move-to-monitor-notification-add! handler)
  (hl--workspace-listen 3 'workspace-move-to-monitor handler 'hl-workspace-move-to-monitor-notification-add!))

;; ---- monitor events -----------------------------------------------------------
;; handler signature: (lambda (mon) ...) — mon is a monitor handle
(define (hl--monitor-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (c-hl-monitor-event-listen rec which))
        rec
        (errorf who "listener rejected, see compositor log"))))

(define (hl-monitor-added-notification-add! handler)
  (hl--monitor-listen 0 'monitor-added handler 'hl-monitor-added-notification-add!))

(define (hl-monitor-removed-notification-add! handler)
  (hl--monitor-listen 1 'monitor-removed handler 'hl-monitor-removed-notification-add!))

(define (hl-monitor-focused-notification-add! handler)
  (hl--monitor-listen 2 'monitor-focused handler 'hl-monitor-focused-notification-add!))

;; fires when the monitor arrangement changes (no payload)
(define (hl-monitor-layout-changed-notification-add! handler)
  (hl--monitor-listen 3 'monitor-layout-changed handler 'hl-monitor-layout-changed-notification-add!))

;; identity, as in Lua's windowEq: true iff both handles refer to the same
;; live window
(define (hl-window=? a b)
  (= 1 (c-hl-window-same (hl-window-id a) (hl-window-id b))))

(define (hl-unbind! b)
  (= 0 (c-hl-unbind-rec b)))

;; coarse: removes EVERY bind whose display key matches (case- and
;; whitespace-insensitive, manager-side); precise removal is hl-unbind!
(define (hl-unbind-key! key)
  (= 0 (c-hl-unbind-key (hl--str key))))

;; unlisten: drop the connection; the record goes inert. upstream
;; HL.EventSubscription:remove parity. #f when already gone.
(define (hl-notification-remove! rec)
  (= 0 (c-hl-event-cancel rec)))
(define (hl-notification-active? rec)
  (= 1 (c-hl-event-active rec)))

(define (hl-current-submap)
  (or (c-hl-current-submap) ""))

;; => (x . y), or #f
(define (hl-cursor-pos)
  (c-hl-cursor-pos))   ; (x . y), or #f

;; reload boundary: drop the previous generation's handler table and install
;; a FRESH environment for the new one. The copy inherits the API (defined
;; once in the persistent environment) but user definitions from previous
;; generations become unreachable when the copy is replaced — the same
;; clean-slate semantics as upstream's per-generation lua_State. hl--state
;; is the one deliberate cross-generation bridge (it lives in the persistent
;; environment).
(define (hl--reset)
  ;; break the chain: clear the slot BEFORE copying so the new copy does
  ;; not retain the previous generation's environment through it (that
  ;; would keep every old generation alive), and user code never sees a
  ;; stale env reference
  (set! hl--generation #f)
  (set! hl--generation (copy-environment (interaction-environment))))

;; cross-reload state: lives in the bootstrap (evaluated ONCE), deliberately
;; OUTSIDE hl--reset's reach. Reloads wipe binds/timers/listeners/handles;
;; this assoc survives for as long as the interpreter does. Note that
;; generation-bound values stored here (e.g. window handles) still die with
;; their generation — the state survives, the resources do not.
(define hl--state '())

;; cross-reload state: an internal PLIST. a re-set keeps the original position.
(define (hl-state-set! k v)
  (define (put l)
    (cond ((null? l) (list k v))
          ((eq? (car l) k) (append (list k v) (cddr l)))
          (else (cons (car l) (cons (cadr l) (put (cddr l)))))))
  (set! hl--state (put hl--state))
  v)

(define (hl-state-ref k . default)
  (let ((v (hl--plist-get hl--state k #!eof)))
    (if (eq? v #!eof) (if (null? default) #f (car default)) v)))

(define (hl-state-keys)
  (let loop ((l hl--state) (acc '()))
    (if (null? l) (reverse acc) (loop (cddr l) (cons (car l) acc)))))

;; remove KEY from the cross-reload state; #t when it was there, #f if not.
;; (hyprscheme extension — upstream Lua has no cross-reload state at all)
(define (hl-state-remove! k)
  (let loop ((l hl--state) (acc '()))
    (cond ((null? l)
           (set! hl--state (reverse acc))
           #f)
          ((eq? (car l) k)
           (set! hl--state (append (reverse acc) (cddr l)))
           #t)
          (else (loop (cddr l)
                      (cons (cadr l) (cons (car l) acc)))))))

(define c-hl-handle-free (foreign-procedure "hl-handle-free" (unsigned-64) int))

;; deletes the C++ weak ref behind a dead handle's cell. Allocation-free by
;; design: this runs inside the GC rendezvous, where consing would re-enter
;; the collector.
(define (hl--drain-handles!)
  (let loop ()
    (let ((dead (hl--handle-guardian)))
      (when dead
        (c-hl-handle-free (car dead))
        (loop)))))

;; the Scheme translation of upstream's Lua GC hook: drain dead handles at
;; every GC request, then let the collector proceed.
(collect-request-handler
  (lambda ()
    (hl--drain-handles!)
    (collect)))

(set! hl--ready #t)
)scm";

namespace Config::Scheme::Internals {
    bool g_up          = false; // interpreter + bootstrap ready

    // ---- object handles -------------------------------------------------------
    // A handle is a heap-allocated weak ref; the Scheme record carries its
    // address (an integer), and a guardian deletes the object when the
    // record dies — see the collect-request-handler install at the end of
    // the bootstrap. Stale == lock() == nullptr. There is NO registry: the
    // handle itself is the only state. Upstream parity: Lua userdata embed
    // the same weak ref and the Lua GC hook destructs it; our guardian is
    // that hook.
    struct IHandle {
        virtual ~IHandle() = default;
    };
    template <typename W>
    struct SHandle : IHandle {
        W wp;
        SHandle() = default;
        template <typename T>
        explicit SHandle(T o) : wp(o) {}
    };

    // shared with SchemeLayout.cpp (declared in SchemeInternals.hpp)
    uintptr_t mintWindowHandle(PHLWINDOW window) {
        return (uintptr_t)(new SHandle<PHLWINDOWREF>(window));
    }
}

namespace Config::Scheme {

    using namespace Internals;

    static std::string                g_configPath;
    // submap registration context: binds created while set are scoped to it
    static std::string                g_regSubmap;
    static std::string                g_regSubmapReset;
    static int                        g_watchFd       = -1;    // inotify fd; dup'd into event loop waiters
    // scheme timers: C++ owns the CEventLoopTimer, Scheme owns the closure
    // (the handler closures travel inside the connections, locked)
    // event subscriptions: C++ owns the listener handles (dropping one
    // unsubscribes); Scheme owns the handler closures by id
    // event connections: record address -> the subscription that keeps the
    // handler plugged into the bus (lost ptr = unregistered, per the listen
    // contract). Entries die three ways: hl-event-cancel!, the reload clear
    // (generation boundary = handler lifetime), plugin teardown.
    static std::unordered_map<uintptr_t, Hyprutils::Signal::CHyprSignalListener> g_eventConnections;

    // lifecycle: the start event is dispatched by an init-time listener
    // (covers the plugin-auto-loaded-at-startup path, where the config load
    // precedes the first render frame).
    static bool                                        g_startSeen      = false;
    static std::vector<SThunkRef>                      g_pendingStart;
    static std::vector<Hyprutils::Signal::CHyprSignalListener> g_lifecycleListeners;


    // ---- the callback watchdog --------------------------------------------------
    // A detector thread: scheme callbacks run on the main loop, so a hung one
    // freezes everything. We can't safely kill a Chez call, but we can SAY SO —
    // one loud log line + notification per overrun.

    static std::atomic<bool>        g_watchdogRun{false};
    static std::atomic<int64_t>     g_callbackStartMs{0};
    static std::atomic<const char*> g_callbackWhat{""};

    static int64_t watchdogNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    static void watchdogEnter(const char* what) {
        {
            std::ofstream pr("/tmp/hs-wd-probe", std::ios::app);
            pr << "enter " << what << "\n";
        }
        g_callbackWhat = what;
        g_callbackStartMs = watchdogNowMs();
    }

    static void watchdogExit() {
        g_callbackStartMs = 0;
    }

    static void startWatchdog() {
        if (g_watchdogRun.exchange(true))
            return;
        std::thread([] {
            bool reported = false;
            while (g_watchdogRun) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                const auto start = g_callbackStartMs.load();
                if (start == 0) {
                    reported = false;
                    continue;
                }
                const auto elapsed = watchdogNowMs() - start;
                if (elapsed > 5000 && !reported) {
                    reported = true;
                    // LOG ONLY — the notification overlay is not thread-safe and
                    // aborts when poked from a side thread (verified: signal 6)
                    LOG(Log::ERR, "[scheme] watchdog: callback '{}' has been running for {}ms — the compositor is likely frozen by it", g_callbackWhat.load(),
                        elapsed);
                }
            }
        }).detach();
    }

    // defined below (device/config section); used by the bind result reader
    static std::string schemeDatumToStr(ptr p);

    static Keybinds::SBindResult fireSchemeBind(int id) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        watchdogEnter("bind callback");
        // hl--bind-fire: #f = declined (thunk returned #f, or error/watchdog);
        // otherwise the normalized result plist (or a non-plist truthy value
        // for plain callbacks)
        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--bind-fire")), Sinteger(id));
        watchdogExit();
        if (r == Sfalse)
            return {.success = false, .error = "scheme keybind callback declined"};
        if (r == Strue || !Spairp(r) || !Ssymbolp(Scar(r)))
            return {}; // handled — 'ok defaults to true

        // result-table return (upstream dispatchResultFromLua parity):
        // {ok, pass-event, request-release, error}
        Keybinds::SBindResult res;
        ptr l = r;
        while (Spairp(l) && Spairp(Scdr(l))) {
            const std::string k = schemeDatumToStr(Scar(l));
            const ptr        v  = Scar(Scdr(l));
            if (k == "ok") {
                if (v == Sfalse)
                    res.success = false;
            } else if (k == "pass-event") {
                if (v == Strue)
                    res.passEvent = true;
            } else if (k == "request-release") {
                if (v == Strue)
                    res.followUp = Keybinds::BIND_FOLLOW_UP_TRIGGER_RELEASE;
            } else if (k == "error" && Sstringp(v)) {
                res.error = schemeDatumToStr(v);
            }
            l = Scdr(Scdr(l));
        }
        return res;
    }

    // fires a handler registered for id with no payload; errors contained
    // numeric payloads (e.g. live gesture update): a real list of numbers
    template <typename T>
    static ptr schemeIntList(const std::vector<T>& vals) {
        if (vals.empty())
            return Snil;
        ptr l = Snil;
        for (auto it = vals.rbegin(); it != vals.rend(); ++it) {
            l = Scons(Sinteger(*it), l);
            Slock_object(l);
        }
        for (ptr p = l; Spairp(p); p = Scdr(p))
            Sunlock_object(p);
        return l;
    }

    static ptr schemeIntList(std::initializer_list<int> vals) {
        return schemeIntList(std::vector<int>(vals));
    }

    // record+pin every heap object a call creates; release all when the value
    // is built (a moving GC can then never invalidate a pointer mid-build).
    static void marshRoot(ptr p, std::vector<ptr>& roots) {
        Slock_object(p);
        roots.push_back(p);
    }
    static void marshRelease(std::vector<ptr>& roots) {
        for (auto it = roots.rbegin(); it != roots.rend(); ++it)
            Sunlock_object(*it);
    }

    static void fireScheme(ptr record) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        Scall1(Stop_level_value(Sstring_to_symbol("hl--event-fire")), record);
        watchdogExit();
    }

    // called from Scheme via foreign-procedure; flags are raw eBindFlags bits,
    // assembled Scheme-side from the options plist
    // ---- bind handles ---------------------------------------------------------
    // A bind's Scheme handle is an hl-bind RECORD (token list + thunk). The
    // capture inside the CBind carries an SThunkRef to it (SchemeInternals.hpp):
    // locked while any copy lives, released when the bind is destroyed. There
    // is NO bind registry on our side: Hyprland's keybind registry is the only
    // list. The record's pinned address, stamped into the bind's argument
    // metadata at registration, identifies our binds for precise unbind and
    // plugin shutdown.
    static std::string bindTag(ptr record) {
        return "scheme:" + std::to_string(reinterpret_cast<uintptr_t>(record));
    }

    static Keybinds::SBindResult fireSchemeBindRec(ptr record); // defined below

    static int hlSchemeBind(ptr record, ptr tokens, int flags, const char* desc, const char* devices) {
        if (!g_up)
            return -1;

        SThunkRef ref(record); // lock FIRST: the record is a GC root from here on

        std::vector<std::string> keys;
        for (ptr p = tokens; Spairp(p) && p != Snil; p = Scdr(p)) {
            ptr elem = Scar(p);
            if (Sstringp(elem)) {
                std::string tok;
                for (iptr i = 0; i < Sstring_length(elem); ++i)
                    tok += Sstring_ref(elem, i);
                keys.emplace_back(std::move(tok));
            }
        }

        Keybinds::SExtraBindArgs args;
        std::string display_key;
        for (const auto& k : keys) {
            if (!display_key.empty()) display_key += ' ';
            display_key += k;
        }
        args.metadata.displayKey = display_key;
        args.metadata.argument   = bindTag(record);
        if (desc && *desc)
            args.metadata.description = desc;
        args.metadata.submap      = g_regSubmap;
        args.metadata.submapReset = g_regSubmapReset;
        if (devices && *devices) {
            std::istringstream ds(devices);
            for (std::string dev; std::getline(ds, dev, ',');)
                if (!dev.empty())
                    args.devices.emplace(dev);
        }

        auto bind = Keybinds::CBind::make(std::move(keys), sc<Keybinds::BindFlags>(flags), [ref] { return fireSchemeBindRec(ref.obj); }, std::move(args));
        if (!bind) {
            LOG(Log::ERR, "[scheme] bind failed: {}", bind.error());
            return -1;
        }

        // no registry of our own: the manager owns the bind from here; its
        // eventual destruction (unbind, reload clearBinds, shutdown) runs the
        // capture destructor, which releases the record lock
        (void)Keybinds::mgr()->addBind(std::move(*bind));
        return 0;
    }

    // fires a RECORD bind (the capture passes the locked record); the Scheme
    // trampoline extracts the thunk. Result contract identical to the id path.
    static Keybinds::SBindResult fireSchemeBindRec(ptr record) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        watchdogEnter("bind callback");
        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--bind-fire-rec")), record);
        watchdogExit();
        if (r == Sfalse)
            return {.success = false, .error = "scheme keybind callback declined"};
        if (r == Strue || !Spairp(r) || !Ssymbolp(Scar(r)))
            return {}; // handled — 'ok defaults to true

        Keybinds::SBindResult res;
        ptr l = r;
        while (Spairp(l) && Spairp(Scdr(l))) {
            const std::string k = schemeDatumToStr(Scar(l));
            const ptr        v  = Scar(Scdr(l));
            if (k == "ok") {
                if (v == Sfalse)
                    res.success = false;
            } else if (k == "pass-event") {
                if (v == Strue)
                    res.passEvent = true;
            } else if (k == "request-release") {
                if (v == Strue)
                    res.followUp = Keybinds::BIND_FOLLOW_UP_TRIGGER_RELEASE;
            } else if (k == "error" && Sstringp(v)) {
                res.error = schemeDatumToStr(v);
            }
            l = Scdr(Scdr(l));
        }
        return res;
    }

    // called from Scheme via foreign-procedure: remove one scheme bind
    static int hlSchemeUnbindKey(const char* key) {
        if (!g_up || !Keybinds::mgr() || !key || !*key)
            return -1;
        // coarse: removes EVERY bind whose display key matches (upstream
        // hl.unbind parity); precise removal goes through hlSchemeUnbindRec
        const auto removed = Keybinds::mgr()->removeBinds(key);
        return removed > 0 ? 0 : -1;
    }

    // precise: find OUR bind by the argument tag (the record's pinned
    // address) and removeBind it — exactly one match, however many same-key
    // siblings exist
    static int hlSchemeUnbindRec(ptr record) {
        if (!g_up || !Keybinds::mgr())
            return -1;
        const std::string tag = bindTag(record);
        for (const Keybinds::PBind& b : Keybinds::mgr()->registry().binds())
            if (b && b->metadata().argument == tag) {
                Keybinds::mgr()->removeBind(b);
                return 0;
            }
        return -1; // not registered (already unbound, or a stale handle)
    }

    // called from Scheme via foreign-procedure
    static int hlSchemeExec(const char* cmd) {
        if (!g_up || !cmd)
            return -1;

        return (int)Config::Supplementary::executor()->spawn(cmd).value_or(-1);
    }

    // fires a handler registered for id with a string payload; all errors are
    // contained inside hl--fire-str's guard
    static void fireSchemeStr(ptr record, const std::string& arg) {
        if (!g_up)
            return;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-str-rec")), record, Sstring_utf8(arg.c_str(), arg.size()));
        watchdogExit();
    }

    // fires a handler registered for id with a boolean payload (#t/#f); all
    // errors are contained inside hl--fire-bool's guard
    static void fireSchemeBool(ptr record, bool arg) {
        if (!g_up)
            return;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-bool-rec")), record, arg ? Strue : Sfalse);
        watchdogExit();
    }

    // events carrying window payloads: the window crosses as a fresh handle id
    static void fireSchemeWin(ptr record, PHLWINDOW window) {
        if (!g_up || !window)
            return;

        const auto winId = (uintptr_t)(new SHandle<PHLWINDOWREF>(window));

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-win-rec")), record, Sinteger(winId));
        watchdogExit();
    }

    static PHLWORKSPACE workspaceFromId(long long id) {
        return reinterpret_cast<SHandle<PHLWORKSPACEREF>*>(id)->wp.lock();
    }

    static PHLMONITOR monitorFromId(long long id) {
        return reinterpret_cast<SHandle<PHLMONITORREF>*>(id)->wp.lock();
    }

    // events carrying workspace/monitor payloads: the object crosses as a
    // fresh handle id; a null object crosses as #f. The Ref variant stores
    // the weak ref as-is without locking — used by workspace.removed, which
    // fires mid-destruction (that handle is born dead; see the comment at
    // the listener).
    static void fireSchemeWs(ptr record, PHLWORKSPACE ws) {
        if (!g_up)
            return;

        const auto wsId = ws ? (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws)) : 0;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-ws-rec")), record, wsId ? Sinteger(wsId) : Sfalse);
        watchdogExit();
    }

    static void fireSchemeWsRef(ptr record, PHLWORKSPACEREF ws) {
        if (!g_up)
            return;

        const auto wsId = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws));

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-ws-rec")), record, Sinteger(wsId));
        watchdogExit();
    }

    static void fireSchemeMon(ptr record, PHLMONITOR mon) {
        if (!g_up)
            return;

        const auto monId = mon ? (uintptr_t)(new SHandle<PHLMONITORREF>(mon)) : 0;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-mon-rec")), record, monId ? Sinteger(monId) : Sfalse);
        watchdogExit();
    }

    // two-handle payloads (workspace, monitor); a null object crosses as #f
    static void fireSchemeWsMon(ptr record, PHLWORKSPACE ws, PHLMONITOR mon) {
        if (!g_up)
            return;

        const auto wsId  = ws ? (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws)) : 0;
        const auto monId = mon ? (uintptr_t)(new SHandle<PHLMONITORREF>(mon)) : 0;

        watchdogEnter("handler");
        Scall3(Stop_level_value(Sstring_to_symbol("hl--fire-ws-mon-rec")), record, wsId ? Sinteger(wsId) : Sfalse, monId ? Sinteger(monId) : Sfalse);
        watchdogExit();
    }

    // called from Scheme via foreign-procedure; repeat != 0 re-arms forever
    // (or until the callback errors, which stops zombie loops)
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

    // timer-record fire: extract (hl-timer-thunk b) and run it zero-arg
    // under the bind result protocol (repeating timers stop on ok #f)
    static Keybinds::SBindResult fireSchemeTimerRec(ptr record) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        watchdogEnter("bind callback");
        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--timer-fire")), record);
        watchdogExit();
        if (r == Sfalse)
            return {.success = false, .error = "scheme keybind callback declined"};
        if (r == Strue || !Spairp(r) || !Ssymbolp(Scar(r)))
            return {};
        Keybinds::SBindResult res;
        ptr l = r;
        while (Spairp(l) && Spairp(Scdr(l))) {
            const std::string k = schemeDatumToStr(Scar(l));
            const ptr        v  = Scar(Scdr(l));
            if (k == "ok") {
                if (v == Sfalse)
                    res.success = false;
            } else if (k == "pass-event") {
                if (v == Strue)
                    res.passEvent = true;
            } else if (k == "request-release") {
                if (v == Strue)
                    res.followUp = Keybinds::BIND_FOLLOW_UP_TRIGGER_RELEASE;
            } else if (k == "error" && Sstringp(v)) {
                res.error = schemeDatumToStr(v);
            }
            l = Scdr(Scdr(l));
        }
        return res;
    }

    static STimerEntry* timerByRecord(ptr record); // defined below (timer control)

    static int hlSchemeTimer(ptr record, int ms, int repeat) {
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
                g_timerIndex.erase(reinterpret_cast<uintptr_t>(ref.obj));
            },
            nullptr);

        g_pEventLoopManager->addTimer(shared);
        g_timerIndex.emplace(reinterpret_cast<uintptr_t>(record),
                             STimerEntry{shared, repeat, sc<uint64_t>(ms)});
        return 0;
    }

    // called from Scheme via foreign-procedure: focused window title, or #f
    static ptr hlSchemeActiveTitle() {
        if (!g_up)
            return Sfalse;

        const auto window = Desktop::focusState()->window();
        if (!window)
            return Sfalse;

        const auto title = window->metadata().title();
        return Sstring_utf8(title.c_str(), title.size());
    }

    // called from Scheme via foreign-procedure: newline-joined workspace
    // display names, or #f. split on the Scheme side.
    // ---- FFI marshalling: proper scheme data, no newline-string shovelling ----
    // Chez's GC runs at allocation points (Scons/Sstring allocate), and its
    // moving collector relocates heap objects. Building a list from C must
    // therefore root the objects held across allocations. Sinteger is an
    // immediate (immune); cons cells and strings/flonums are heap objects and
    // get Slock_object-pinned until the value is complete. Releasing just
    // before the return is safe: nothing allocates in between, and the FFI
    // return re-roots the result.

    static ptr hlSchemeWorkspaceNames() {
        if (!g_up)
            return Sfalse;

        // proper scheme data: a real list of ids, not a newline-joined string.
        // (Sinteger is an immediate — only the cons cells need rooting.)
        std::vector<uintptr_t> ids;
        for (const auto& wsRef : State::Workspace::state()->workspaces()) {
            const auto ws = wsRef.lock();
            if (!ws)
                continue;
            ids.push_back((uintptr_t)(new SHandle<PHLWORKSPACEREF>(wsRef)));
        }

        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    // called from Scheme via foreign-procedure: subscribe to submap changes
    static int hlSchemeSubmapListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.keybinds.submap.listen([ref = SThunkRef(record)](const std::string& name) {
            fireSchemeStr(ref.obj, name);
        }));
        return 0;
    }

    // resolves a handle id to the live window, or null if stale (dead or
    // unknown). lazily drops dead entries while we're here.
    static std::optional<PHLWINDOW> actionWindow(long long id);

    // called from Scheme via foreign-procedure when the guardian yields a
    // dead handle record: runs the weak ref's destructor (unregisters the
    // observer from the object's control block)
    static int hlHandleFree(unsigned long long addr) {
        // implausible addresses (corruption, truncation bugs) are skipped and
        // logged rather than crashed on; valid user-space pointers are < 2^47
        if ((addr >> 47) != 0) {
            LOG(Log::ERR, "[scheme] handle free: implausible address {:#x} — skipping", addr);
            return -1;
        }
        delete reinterpret_cast<IHandle*>(addr);
        return 0;
    }

    static PHLWINDOW windowFromId(long long id) {
        return reinterpret_cast<SHandle<PHLWINDOWREF>*>(id)->wp.lock();
    }

    static double hlSchemeActiveWindowId() {
        if (!g_up)
            return -1;

        const auto window = Desktop::focusState()->window();
        if (!window)
            return -1;

        return (double)(uintptr_t)(new SHandle<PHLWINDOWREF>(window));
    }

    static ptr hlSchemeWindowIds() {
        if (!g_up)
            return Sfalse;

        std::vector<uintptr_t> ids;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w->mapped())
                continue;
            const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
            ids.push_back(id);
        }

        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static ptr hlSchemeWindowTitle(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto title = window->metadata().title();
        return Sstring_utf8(title.c_str(), title.size());
    }

    static int hlSchemeWindowAlive(long long id) {
        if (!g_up)
            return 0;

        return windowFromId(id) ? 1 : 0;
    }

    static int hlSchemeWindowClose(long long id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (id >= 0 && !window)
            return -1;
        if (!window)
            return -1;

        return Config::Actions::closeWindow(window) ? 0 : -2;
    }

    static ptr hlSchemeWindowClass(long long id) {
        if (!g_up)
            return Sfalse;
        {
            std::ofstream pr("/tmp/hs-sel-debug", std::ios::app);
            auto w = windowFromId(id);
            pr << "class(" << id << ") -> " << (w ? w->metadata().appID() : "NULL") << "\n";
        }

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().appID();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static ptr hlSchemeWindowWorkspaceId(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window || !window->m_workspace)
            return Sfalse;

        const auto wsId = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(window->m_workspace));
        return Sinteger(wsId);
    }

    static ptr hlSchemeWindowMonitorId(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto monitor = State::monitorState()->query().id(window->monitorID()).run();
        if (!monitor)
            return Sfalse;

        const auto monId = (uintptr_t)(new SHandle<PHLMONITORREF>(monitor));
        return Sinteger(monId);
    }

    static int hlSchemeWindowFloating(long long id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->isFloating()) ? 1 : 0;
    }

    static ptr hlSchemeWindowSize(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto sz = window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        return Scons(Sinteger((int)sz.x), Sinteger((int)sz.y));   // (w . h)
    }

    static int hlSchemeWindowPid(long long id) {
        if (!g_up)
            return -1;

        const auto window = windowFromId(id);
        if (!window)
            return -1;

        return (int)window->backend().pid();
    }

    // ---- window read-side fields (LuaWindow.cpp field parity, 2026-09-21) ----
    // upstream's is_master / perc_master / perc_size / index / index_in_column
    // / column all live inside ONE `layout` table — mirrored as the single
    // hl-window-layout plist getter, not five functions.
    static int hlWindowFocusHistoryId(PHLWINDOW wnd) {
        const auto& history = Desktop::History::windowTracker()->fullHistory();
        for (size_t i = 0; i < history.size(); ++i) {
            if (history[i].lock() == wnd)
                return sc<int>(history.size() - i - 1); // reverse order, upstream parity
        }
        return -1;
    }

    static ptr hlSchemeWindowAddress(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto addr = std::format("0x{:x}", reinterpret_cast<uintptr_t>(window.get()));
        return Sstring_utf8(addr.c_str(), addr.size());
    }

    static int hlSchemeWindowMapped(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->mapped()) ? 1 : 0;
    }

    static int hlSchemeWindowVisible(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->mapped() && window->acceptsInput() && window->alphaNonZero()) ? 1 : 0;
    }

    static int hlSchemeWindowAcceptsInput(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->acceptsInput()) ? 1 : 0;
    }

    static ptr hlSchemeWindowPosition(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto pos = window->position(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        return Scons(Sinteger((int)pos.x), Sinteger((int)pos.y)); // (x . y)
    }

    static int hlSchemeWindowPinFullscreened(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->fullscreenPolicy().pinFullscreened()) ? 1 : 0;
    }

    static int hlSchemeWindowAllowedOverFullscreen(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && window->fullscreenPolicy().allowedOverFullscreen()) ? 1 : 0;
    }

    static int hlSchemeWindowTearingHint(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && (window->m_hints & Desktop::View::WINDOW_HINT_TEAR)) ? 1 : 0;
    }

    static int hlSchemeWindowInhibitingIdle(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromId(id);
        return (window && g_pInputManager && g_pInputManager->isWindowInhibiting(window, false)) ? 1 : 0;
    }

    static ptr hlSchemeWindowFocusHistoryId(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        return Sinteger(hlWindowFocusHistoryId(window));
    }

    static ptr hlSchemeWindowContentType(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto ct = NContentType::toString(window->getContentType());
        return Sstring_utf8(ct.c_str(), ct.size());
    }

    static ptr hlSchemeWindowStableId(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto sid = std::format("{:x}", window->metadata().stableID());
        return Sstring_utf8(sid.c_str(), sid.size());
    }

    static ptr hlSchemeWindowTags(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        ptr l = Snil;
        for (const auto& tag : window->m_ruleApplicator->m_tagKeeper.getTags())
            l = Scons(Sstring_utf8(tag.c_str(), tag.size()), l);
        return l;
    }

    static ptr hlSchemeWindowSwallowingId(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto swallowee = window->swallowing().swallowee();
        if (!swallowee)
            return Sfalse;
        const auto winId = (uintptr_t)(new SHandle<PHLWINDOWREF>(swallowee));
        return Sinteger(winId);
    }

    static ptr hlSchemeWindowXdgTag(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto tag = window->backend().metadata().tag;
        if (!tag)
            return Sfalse;
        return Sstring_utf8(tag->c_str(), tag->size());
    }

    static ptr hlSchemeWindowXdgDescription(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto desc = window->backend().metadata().description;
        if (!desc)
            return Sfalse;
        return Sstring_utf8(desc->c_str(), desc->size());
    }

    // upstream's `layout` window field: {name} for plain algos, plus
    // is_master/perc_master/perc_size under master, and a nested column
    // table {index width windows} + index_in_column under scrolling.
    // Mirrored as a plist: (name "master" 'is-master #f 'perc-master 0.5
    // 'perc-size 1.0) | (name "scrolling" 'column (index n width f
    // windows (…)) 'index-in-column n). Stale handle / no algo -> #f.
    static ptr hlSchemeWindowLayout(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto target = window->layoutTarget();
        if (!target || target->floating() || !window->m_workspace || !window->m_workspace->space())
            return Sfalse;
        const auto& algo = window->m_workspace->space()->algorithm();
        if (!algo || !algo->tiledAlgo())
            return Sfalse;
        const auto& tiledAlgo = algo->tiledAlgo();

        const std::string name = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(tiledAlgo.get());
        ptr l = Scons(Sstring_to_symbol("name"), Scons(Sstring_utf8(name.c_str(), name.size()), Snil));

        if (const auto* master = dynamic_cast<Layout::Tiled::CMasterAlgorithm*>(tiledAlgo.get())) {
            const auto node = master->getNodeFromTarget(target);
            if (node) {
                l = Scons(Sstring_to_symbol("is-master"),
                     Scons(node->isMaster ? Strue : Sfalse, l));
                l = Scons(Sstring_to_symbol("perc-master"),
                     Scons(Sflonum(node->percMaster), l));
                l = Scons(Sstring_to_symbol("perc-size"),
                     Scons(Sflonum(node->percSize), l));
            }
        } else if (auto* scrolling = dynamic_cast<Layout::Tiled::CScrollingAlgorithm*>(tiledAlgo.get())) {
            const auto data = scrolling->dataFor(target);
            if (data) {
                const auto col = data->column.lock();
                if (col) {
                    const auto scrollingData = col->scrollingData.lock();
                    ptr column = Snil;
                    if (scrollingData)
                        column = Scons(Sstring_to_symbol("index"),
                                   Scons(Sinteger((int)scrollingData->idx(col)), column));
                    column = Scons(Sstring_to_symbol("width"),
                               Scons(Sflonum(col->getColumnWidth()), column));
                    ptr windows = Snil;
                    for (const auto& td : col->targetDatas) {
                        const auto t = td->target.lock();
                        if (!t)
                            continue;
                        const auto win = t->window();
                        if (!win)
                            continue;
                        const auto winId = (uintptr_t)(new SHandle<PHLWINDOWREF>(win));
                        windows = Scons(Sinteger(winId), windows);
                    }
                    column = Scons(Sstring_to_symbol("windows"),
                               Scons(windows, column));
                    l = Scons(Sstring_to_symbol("index-in-column"),
                          Scons(Sinteger((int)col->idx(target)), l));
                    l = Scons(Sstring_to_symbol("column"), Scons(column, l));
                }
            }
        }
        return l;
    }

    static int hlSchemeWindowFocus(long long id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::focus(*window) ? 0 : -2;
    }

    static int hlSchemeWindowFloat(long long id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_TOGGLE, *window) ? 0 : -2;
    }

    static int hlSchemeWindowMoveToWorkspace(long long id, const char* name) {
        if (!g_up || !name)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;
        const PHLWINDOW w = *window;

        const auto target = State::Workspace::resolver()->getWorkspaceTargetFromString(name);
        if (!target.valid())
            return -2;

        auto ws = State::Workspace::state()->find(target);
        if (!ws) {
            // create missing workspaces on the window's own monitor
            // (isEmpty=false, like Lua's resolveWorkspaceStr — an empty-flagged
            // workspace gets swept before the window lands in it)
            const auto mon = w->m_workspace ? w->m_workspace->m_monitor.lock() : Desktop::focusState()->monitor();
            ws             = State::Workspace::state()->create(target, mon, false);
        }
        if (!ws)
            return -2;

        return Config::Actions::moveToWorkspace(ws, false, w) ? 0 : -2;
    }

    // toggles the given fullscreen mode (FSMODE_FULLSCREEN=2, FSMODE_MAXIMIZED=1),
    // mirroring the toggle logic in Lua's dsp_fullscreenWindowWithAction
    // true set: 0 = none, 1 = maximized, 2 = fullscreen — regardless of the
    // current state (the toggle above exits when already in the mode)
    static int hlSchemeWindowFullscreenSet(long long id, int modeRaw) {
        if (!g_up)
            return -1;
        const auto window = actionWindow(id).value_or(nullptr);
        if (!window)
            return -1;
        const auto mode = sc<Fullscreen::eFullscreenMode>(modeRaw);
        return Config::Actions::fullscreenWindow(mode, false, window) ? 0 : -2;
    }

    static int hlSchemeWindowFullscreenToggle(long long id, int modeRaw) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;
        const PHLWINDOW w = *window;

        const auto mode = sc<Fullscreen::eFullscreenMode>(modeRaw);
        if (Fullscreen::controller()->isFullscreen(w, mode))
            return Config::Actions::fullscreenWindow(Fullscreen::FSMODE_NONE, false, w) ? 0 : -2;

        return Config::Actions::fullscreenWindow(mode, false, w) ? 0 : -2;
    }

    static int hlSchemeWindowFullscreenMode(long long id) {
        if (!g_up)
            return -1;

        const auto window = windowFromId(id);
        if (!window)
            return -1;

        return sc<int>(Fullscreen::controller()->getFullscreenModes(window).internal);
    }

    static int hlSchemeWindowHidden(long long id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->isHidden()) ? 1 : 0;
    }

    static int hlSchemeWindowPinned(long long id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && (window->m_state & Desktop::View::WINDOW_STATE_PINNED)) ? 1 : 0;
    }

    // ---- state queries for the toggle/set family -----------------------------
    // Each pair (hl-window-*-set!) has a matching hl-window-*-? reading the
    // effective state from the same source the setter writes.

    // windowHandle: scheme ids 0+ map to registry windows, -1 to the focused
    // window (same convention as actionWindow below)
    static PHLWINDOW windowFromSchemeId(long long id) {
        return id >= 0 ? windowFromId(id) : Desktop::focusState()->window();
    }

    static int hlSchemeWindowPseudoQuery(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        return (window && window->layoutTarget()->isPseudo()) ? 1 : 0;
    }

    static int hlSchemeWindowMaximizedQuery(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        return (window && Fullscreen::controller()->getFullscreenModes(window).internal == Fullscreen::FSMODE_MAXIMIZED) ? 1 : 0;
    }

    static int hlSchemeWindowInGroup(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        return (window && window->grouping().group()) ? 1 : 0;
    }

    static int hlSchemeWindowGroupDenied(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        if (!window)
            return 0;
        const auto group = window->grouping().group();
        return (group && group->denied()) ? 1 : 0;
    }

    static int hlSchemeWindowGroupLocked(long long id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        if (!window)
            return 0;
        const auto group = window->grouping().group();
        return (group && group->locked()) ? 1 : 0;
    }

    static int hlSchemeGroupsLocked() {
        if (!g_up)
            return 0;
        return Desktop::windowState()->groupsLocked() ? 1 : 0;
    }

    // window-scoped group lock: toggle/set the lock on the group of the given
    // window (id 0 → focused). Mirrors upstream lockActiveGroup with the
    // target window explicit instead of always-focused.
    static int hlSchemeWindowGroupLock(long long id, int act) {
        if (!g_up)
            return -1;
        const auto window = windowFromSchemeId(id);
        if (!window)
            return -1;
        const auto group = window->grouping().group();
        if (!group)
            return -1;
        switch (act) {
            case 0: group->setLocked(!group->locked()); break;
            case 1: group->setLocked(true); break;
            default: group->setLocked(false); break;
        }
        window->presentation().refreshValues();
        return 0;
    }

    // read back the dynamic window props setProp writes: effective values
    // (defaults included), as #t/#f for booleans, numbers for opacities and
    // border/rounding, and #f for unknown/unsupported prop names.
    static ptr hlSchemeWindowPropGet(long long id, const char* prop) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromSchemeId(id);
        if (!window || !prop || !*prop)
            return Sfalse;
        const std::string p = prop;
        auto&             A = *window->m_ruleApplicator;
        if (p == "opacity")
            return Sflonum(A.alpha().value().alpha);
        if (p == "opacity_inactive")
            return Sflonum(A.alphaInactive().value().alpha);
        if (p == "opacity_fullscreen")
            return Sflonum(A.alphaFullscreen().value().alpha);
        if (p == "border_size")
            return Sinteger(A.borderSize().value());
        if (p == "rounding")
            return Sinteger(A.rounding().value());
#define HL_READ_BOOL(NAME, CNAME)  \
    if (p == NAME)                 \
        return A.CNAME().value() ? Strue : Sfalse;
        HL_READ_BOOL("allows_input", allowsInput)
        HL_READ_BOOL("decorate", decorate)
        HL_READ_BOOL("focus_on_activate", focusOnActivate)
        HL_READ_BOOL("keep_aspect_ratio", keepAspectRatio)
        HL_READ_BOOL("nearest_neighbor", nearestNeighbor)
        HL_READ_BOOL("no_anim", noAnim)
        HL_READ_BOOL("no_blur", noBlur)
        HL_READ_BOOL("no_dim", noDim)
        HL_READ_BOOL("no_focus", noFocus)
        HL_READ_BOOL("no_max_size", noMaxSize)
        HL_READ_BOOL("no_shadow", noShadow)
        HL_READ_BOOL("no_glow", noGlow)
        HL_READ_BOOL("no_wobble", noWobble)
        HL_READ_BOOL("no_shortcuts_inhibit", noShortcutsInhibit)
        HL_READ_BOOL("opaque", opaque)
        HL_READ_BOOL("dim_around", dimAround)
        HL_READ_BOOL("force_rgbx", RGBX)
        HL_READ_BOOL("sync_fullscreen", syncFullscreen)
        HL_READ_BOOL("immediate", tearing)
        HL_READ_BOOL("xray", xray)
        HL_READ_BOOL("render_unfocused", renderUnfocused)
        HL_READ_BOOL("no_follow_mouse", noFollowMouse)
        HL_READ_BOOL("no_screen_share", noScreenShare)
        HL_READ_BOOL("no_vrr", noVRR)
        HL_READ_BOOL("no_auto_hdr", noAutoHDR)
        HL_READ_BOOL("persistent_size", persistentSize)
        HL_READ_BOOL("stay_focused", stayFocused)
        HL_READ_BOOL("no_xdg_drags", noXdgDrags)
#undef HL_READ_BOOL
        return Sfalse;
    }

    // ---- actions: the dispatcher surface --------------------------------------
    // Each handler wraps one Config::Actions call — the same layer the Lua
    // hl.dsp.* dispatchers use. Window args take a scheme handle id, or -1
    // for the active window.

    static std::optional<PHLWINDOW> actionWindow(long long id) {
        if (id >= 0)
            return windowFromId(id);
        return Desktop::focusState()->window();
    }

    static int actionResult(const char* name, Config::Actions::ActionResult result) {
        if (result)
            return 0;
        LOG(Log::ERR, "[scheme] {} failed: {}", name, result.error().message);
        return -2;
    }

    static Math::eDirection actionDir(const char* s) {
        return Math::fromChar(s && *s ? s[0] : 'x');
    }

    static PHLMONITOR monitorFromName(const char* name) {
        if (!name || !*name)
            return nullptr;
        for (const auto& m : State::monitorState()->monitors())
            if (m->m_name == name)
                return m;
        return nullptr;
    }

    static PHLWORKSPACE workspaceFromName(const char* name) {
        if (!name || !*name)
            return nullptr;
        return State::Workspace::state()->query().input(std::string(name)).run();
    }

    static int hlSchemeFocusWorkspace(const char* ws) {
        if (!g_up)
            return -1;
        return actionResult("focus-workspace", Config::Actions::changeWorkspace(std::string(ws ? ws : "")));
    }

    static int hlSchemeFocusDirection(const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("focus-direction", Config::Actions::moveFocus(actionDir(dir)));
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

    static int hlSchemeFocusLast() {
        if (!g_up)
            return -1;
        return actionResult("focus-last", Config::Actions::focusCurrentOrLast());
    }

    static int hlSchemeFocusUrgent() {
        if (!g_up)
            return -1;
        return actionResult("focus-urgent", Config::Actions::focusUrgentOrLast());
    }

    static int hlSchemeWindowMoveDirection(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-move-direction", Config::Actions::moveInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapDirection(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-direction", Config::Actions::swapInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapNext(long long id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-next", Config::Actions::swapNext(prev == 0, actionWindow(id)));
    }

    static int hlSchemeWindowSwapWith(long long id, long long otherId) {
        if (!g_up)
            return -1;
        const auto other = windowFromId(otherId);
        if (!other)
            return -1;
        return actionResult("window-swap-with", Config::Actions::swapWith(other, actionWindow(id)));
    }

    // filter: 0 = all, 1 = tiled only, 2 = floating only
    static int hlSchemeWindowCycle(long long id, int next, int filter) {
        if (!g_up)
            return -1;
        std::optional<bool> tiled, floating;
        if (filter == 1)
            tiled = true;
        else if (filter == 2)
            floating = true;
        return actionResult("window-cycle", Config::Actions::cycleNext(next != 0, tiled, floating, actionWindow(id)));
    }

    static int hlSchemeWindowCenter(long long id) {
        if (!g_up)
            return -1;
        return actionResult("window-center", Config::Actions::center(actionWindow(id)));
    }

    static int hlSchemeWindowResizePx(long long id, double w, double h, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-resize", Config::Actions::resize(Vector2D{w, h}, relative != 0, actionWindow(id)));
    }

    static int hlSchemeWindowMovePx(long long id, double x, double y, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-move", Config::Actions::move(Vector2D{x, y}, relative != 0, actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowFloatAct(long long id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-float", Config::Actions::floatWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowPinAct(long long id, int act) {        if (!g_up)
            return -1;
        return actionResult("window-pin", Config::Actions::pinWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowPseudo(long long id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-pseudo", Config::Actions::pseudoWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowKill(long long id) {
        if (!g_up)
            return -1;
        return actionResult("window-kill", Config::Actions::killWindow(actionWindow(id)));
    }

    static int hlSchemeWindowSignal(long long id, int sig) {
        if (!g_up)
            return -1;
        return actionResult("window-signal", Config::Actions::signalWindow(sig, actionWindow(id)));
    }

    static int hlSchemeWindowZOrder(long long id, const char* mode) {
        if (!g_up)
            return -1;
        return actionResult("window-zorder", Config::Actions::alterZOrder(std::string(mode ? mode : ""), actionWindow(id)));
    }

    static int hlSchemeWindowSetProp(long long id, const char* prop, const char* val) {
        if (!g_up)
            return -1;
        return actionResult("window-set-prop", Config::Actions::setProp(std::string(prop ? prop : ""), std::string(val ? val : ""), actionWindow(id)));
    }

    static int hlSchemeWindowTag(long long id, const char* tag) {
        if (!g_up)
            return -1;
        return actionResult("window-tag", Config::Actions::tag(std::string(tag ? tag : ""), actionWindow(id)));
    }

    static int hlSchemeWindowClearTags(long long id) {
        if (!g_up)
            return -1;
        return actionResult("window-clear-tags", Config::Actions::clearTags(actionWindow(id)));
    }

    static int hlSchemeToggleSwallow() {
        if (!g_up)
            return -1;
        return actionResult("toggle-swallow", Config::Actions::toggleSwallow());
    }

    static int hlSchemeGroupToggle(long long id) {
        if (!g_up)
            return -1;
        return actionResult("group-toggle", Config::Actions::toggleGroup(actionWindow(id)));
    }

    // explicit set: a no-op when the window is already in the requested
    // state (group() is null iff the window is not a member of a group)
    static int hlSchemeGroupSet(long long id, int on) {
        if (!g_up)
            return -1;
        const auto w = actionWindow(id).value_or(nullptr);
        if (!w)
            return -1;
        if ((w->grouping().group() != nullptr) == (on != 0))
            return 0;
        return actionResult("group-set", Config::Actions::toggleGroup(w));
    }

    static int hlSchemeGroupCycle(long long id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("group-cycle", Config::Actions::changeGroupActive(prev == 0, actionWindow(id)));
    }

    static int hlSchemeGroupIndex(long long id, int index) {
        if (!g_up)
            return -1;
        return actionResult("group-index", Config::Actions::setGroupActive(index, actionWindow(id)));
    }

    static int hlSchemeGroupMoveWindow(long long id, int prev) {
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

    static int hlSchemeWindowIntoGroup(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-group", Config::Actions::moveIntoGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowOutOfGroup(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-out-of-group", Config::Actions::moveOutOfGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowIntoOrCreateGroup(long long id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-or-create-group", Config::Actions::moveIntoOrCreateGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowDenyFromGroup(long long id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-deny-from-group", Config::Actions::denyWindowFromGroup(sc<Config::Actions::eTogglableAction>(act)));
    }

    static int hlSchemeWorkspaceRename(const char* oldName, const char* newName) {
        if (!g_up)
            return -1;
        const auto ws = workspaceFromName(oldName);
        if (!ws) {
            LOG(Log::ERR, "[scheme] workspace-rename: no workspace named {}", oldName ? oldName : "");
            return -1;
        }
        return actionResult("workspace-rename", Config::Actions::renameWorkspace(ws, std::string(newName ? newName : "")));
    }

    static int hlSchemeWorkspaceMoveMonitor(const char* wsName, const char* monName) {
        if (!g_up)
            return -1;
        const auto ws  = workspaceFromName(wsName);
        const auto mon = monitorFromName(monName);
        if (!ws || !mon) {
            LOG(Log::ERR, "[scheme] workspace-move-to-monitor: no workspace named {} or monitor named {}", wsName ? wsName : "", monName ? monName : "");
            return -1;
        }
        return actionResult("workspace-move-to-monitor", Config::Actions::moveToMonitor(ws, mon));
    }

    // create-or-find a special workspace by (prefixed) selector
    static PHLWORKSPACE specialWorkspaceFromName(const std::string& wsName, const PHLMONITOR& mon) {
        std::string sel = wsName;
        if (!sel.starts_with("special:"))
            sel = "special:" + sel;
        auto ws = workspaceFromName(sel.c_str());
        if (!ws) {
            const auto target = State::Workspace::resolver()->getWorkspaceTargetFromString(sel);
            if (!target.valid())
                return nullptr;
            ws = State::Workspace::state()->create(target, mon);
        }
        return ws;
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

    static int hlSchemeWorkspaceToggleSpecial(const char* wsName) {
        if (!g_up)
            return -1;
        // a toggle must CREATE the special workspace when it does not exist
        // yet (upstream's dispatcher takes a bare name and creates on
        // demand) — resolving only would make first use impossible
        if (!wsName || !*wsName)
            return -1;
        const auto ws = specialWorkspaceFromName(wsName, Desktop::focusState()->monitor());
        if (!ws)
            return -1;
        return actionResult("workspace-toggle-special", Config::Actions::toggleSpecial(ws));
    }

    static int hlSchemeWorkspaceSwapMonitors(const char* mon1, const char* mon2) {
        if (!g_up)
            return -1;
        const auto a = monitorFromName(mon1);
        const auto b = monitorFromName(mon2);
        if (!a || !b) {
            LOG(Log::ERR, "[scheme] workspace-swap-monitors: no monitor named {} or {}", mon1 ? mon1 : "", mon2 ? mon2 : "");
            return -1;
        }
        return actionResult("workspace-swap-monitors", Config::Actions::swapActiveWorkspaces(a, b));
    }

    static int hlSchemeCursorMove(double x, double y) {
        if (!g_up)
            return -1;
        return actionResult("cursor-move", Config::Actions::moveCursor(Vector2D{x, y}));
    }

    static int hlSchemeCursorCorner(long long id, int corner) {
        if (!g_up)
            return -1;
        return actionResult("cursor-move-to-corner", Config::Actions::moveCursorToCorner(corner, actionWindow(id)));
    }

    static int hlSchemeExit() {
        if (!g_up)
            return -1;
        return actionResult("exit", Config::Actions::exit());
    }

    static int hlSchemeReloadConfig() {
        if (!g_up)
            return -1;
        return actionResult("reload-config", Config::Actions::reloadConfig());
    }

    static int hlSchemeForceRendererReload() {
        if (!g_up)
            return -1;
        return actionResult("force-renderer-reload", Config::Actions::forceRendererReload());
    }

    static int hlSchemeDpms(int act, const char* monName) {
        if (!g_up)
            return -1;
        std::optional<PHLMONITOR> mon;
        if (monName && *monName) {
            mon = monitorFromName(monName);
            if (!mon) {
                LOG(Log::ERR, "[scheme] dpms: no monitor named {}", monName);
                return -1;
            }
        }
        return actionResult("dpms", Config::Actions::dpms(sc<Config::Actions::eTogglableAction>(act), mon));
    }

    static int hlSchemeForceIdle(double seconds) {
        if (!g_up)
            return -1;
        return actionResult("force-idle", Config::Actions::forceIdle(sc<float>(seconds)));
    }

    static int hlSchemeGlobal(const char* action) {
        if (!g_up)
            return -1;
        return actionResult("global", Config::Actions::global(std::string(action ? action : "")));
    }

    static int hlSchemeEvent(const char* data) {
        if (!g_up)
            return -1;
        return actionResult("event", Config::Actions::event(std::string(data ? data : "")));
    }

    static int hlSchemePass(long long id) {
        if (!g_up)
            return -1;
        return actionResult("pass", Config::Actions::pass(actionWindow(id)));
    }

    static Input::ModifierMask gestureMods(const char* mods); // defined below

    // mods/key arrive as the same strings binds take (e.g. "SUPER" "F10");
    // resolve to mask + keysym here so the Scheme surface stays string-based
    static int hlSchemeSendShortcut(const char* mods, const char* key, long long id) {
        if (!g_up)
            return -1;
        const auto sym = xkb_keysym_from_name(key ? key : "", XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == 0)
            return -1;
        return actionResult("send-shortcut", Config::Actions::pass(gestureMods(mods), sc<uint32_t>(sym), actionWindow(id)));
    }

    static int hlSchemeSendKeyState(const char* mods, const char* key, int state, long long id) {
        if (!g_up)
            return -1;
        const auto sym = xkb_keysym_from_name(key ? key : "", XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == 0)
            return -1;
        return actionResult("send-key-state", Config::Actions::sendKeyState(gestureMods(mods), sc<uint32_t>(sym), sc<uint32_t>(state), actionWindow(id)));
    }

    static int hlSchemeMouse(const char* action) {
        if (!g_up)
            return -1;
        return actionResult("mouse", Config::Actions::mouse(std::string(action ? action : "")));
    }

    static int hlSchemeReleaseInputCapture() {
        if (!g_up)
            return -1;
        return actionResult("release-input-capture", Config::Actions::releaseInputCapture());
    }

    // explicit fullscreen: internal and client modes, layout-aware flag
    static int hlSchemeWindowFullscreenState(long long id, int internalMode, int clientMode, int layoutAware) {
        if (!g_up)
            return -1;
        const auto w = actionWindow(id);
        if (id >= 0 && !w)
            return -1;
        return actionResult("window-fullscreen-state",
            Config::Actions::fullscreenWindow(sc<Fullscreen::eFullscreenMode>(internalMode), sc<Fullscreen::eFullscreenMode>(clientMode), layoutAware != 0, w));
    }

    static int hlSchemeLayoutMessage(const char* msg) {
        if (!g_up)
            return -1;
        return actionResult("layout-msg", Config::Actions::layoutMessage(std::string(msg ? msg : "")));
    }

    // ---- config: setting config options from scheme ---------------------------
    // The values live in CConfigManager::m_configValues (dotted key →
    // ILuaConfigValue). Each value parses itself off a lua stack; we keep a
    // private scratch lua_State (the same liblua the compositor links) purely
    // as the typed front door — the config manager's interpreter is never
    // involved. Propagation is a plain prop-refresh, exactly like
    // hyprctl eval 'hl.config(...)'.

    static lua_State*  g_configScratch = nullptr;
    static std::string g_configError;

    // ---- groups as objects (upstream HL.Group parity) --------------------------
    // groups dissolve behind our backs -> weak handles via the guardian (the
    // record-cell model); every getter returns #f when the group is gone
    using PHLGROUPREF = Hyprutils::Memory::CWeakPointer<Desktop::View::CGroup>;

    static ptr hlSchemeWorkspaceGroups(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ws = workspaceFromId(id);
        if (!ws)
            return Sfalse;
        ptr                                 l = Snil;
        std::vector<const Desktop::View::CGroup*> pushed;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (w->m_workspace != ws || !w->grouping().group())
                continue;
            const auto* g = w->grouping().group().get();
            if (std::find(pushed.begin(), pushed.end(), g) != pushed.end())
                continue;
            pushed.push_back(g);
            l = Scons(Sinteger((uintptr_t)(new SHandle<PHLGROUPREF>(w->grouping().group()))), l);
        }
        // members were collected head-first: reverse for document order
        ptr out = Snil;
        for (ptr p = l; Spairp(p); p = Scdr(p))
            out = Scons(Scar(p), out);
        return out;
    }

    static SP<Desktop::View::CGroup> groupFromHandle(long long id) {
        return reinterpret_cast<SHandle<PHLGROUPREF>*>(id)->wp.lock();
    }

    static int hlSchemeGroupAlive(long long id) {
        if (!g_up)
            return -1;
        return groupFromHandle(id) ? 1 : 0;
    }

    static int hlSchemeGroupSame(long long a, long long b) {
        if (!g_up)
            return -1;
        const auto ga = groupFromHandle(a);
        const auto gb = groupFromHandle(b);
        return (ga && gb && ga.get() == gb.get()) ? 1 : 0;
    }

    static ptr hlSchemeGroupMembers(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        ptr l = Snil;
        for (const auto& grouped : group->windows()) {
            const auto w = grouped.lock();
            if (!w)
                continue;
            l = Scons(Sinteger(Internals::mintWindowHandle(w)), l);
        }
        ptr out = Snil;
        for (ptr p = l; Spairp(p); p = Scdr(p))
            out = Scons(Scar(p), out);
        return out;
    }

    static ptr hlSchemeGroupCurrent(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        const auto current = group->current();
        if (!current)
            return Sfalse;
        return Sinteger(Internals::mintWindowHandle(current));
    }

    static ptr hlSchemeGroupCurrentIdx(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        return Sinteger(sc<int64_t>(group->getCurrentIdx()) + 1); // 1-based, upstream parity
    }

    static ptr hlSchemeGroupSize(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        return group ? Sinteger(sc<int64_t>(group->size())) : Sfalse;
    }

    static ptr hlSchemeGroupLocked(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        return group->locked() ? Strue : Sfalse;
    }

    static ptr hlSchemeGroupDenied(long long id) {
        if (!g_up)
            return Sfalse;
        const auto group = groupFromHandle(id);
        if (!group)
            return Sfalse;
        return group->denied() ? Strue : Sfalse;
    }

    // index crosses as -1 = append; 1-based otherwise (upstream parity)
    static int hlSchemeGroupAdd(long long id, long long winId, long long index) {
        if (!g_up)
            return -1;
        const auto group = groupFromHandle(id);
        if (!group)
            return -1;
        const auto window = windowFromId(winId);
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

    static int hlSchemeGroupRemove(long long id, long long winId) {
        if (!g_up)
            return -1;
        const auto group = groupFromHandle(id);
        if (!group)
            return -1;
        const auto window = windowFromId(winId);
        if (!window || !group->has(window)) {
            g_configError = "hl-group-remove!: window is not a group member";
            return -2;
        }
        group->remove(window);
        return 0;
    }


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

    static lua_State* configScratch() {
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

    static ptr hlConfigLastError() {
        return Sstring_utf8(g_configError.c_str(), g_configError.size());
    }

    // read side: marshal the value back as an encoded string
    // "b\n0|1" | "n\n<num>" | "s\n<str>" | "t\n(key\nvalue\n)*" ; #f = unknown

    // ---- per-device config (upstream hl.device parity) -----------------------
    // Lua's hl.device({ name, ... }) writes per-device input overrides;
    // mirrored here as (hl-device-add! NAME . FIELDS). Write-only by parity:
    // Lua exposes no device read/get, and a stored field cannot be unset
    // (insert-or-assign only).

    enum class eDeviceKind : uint8_t { BOOL, INT, FLOAT, STRING, VEC2 };

    struct SDeviceField {
        const char* name;
        eDeviceKind kind;
        double      lo, hi; // bounds for INT/FLOAT (unused otherwise)
    };

    // mirrored from DEVICE_FIELDS (LuaBindingsConfigRules.cpp)
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

    static std::string schemeDatumToStr(ptr p) {
        if (Ssymbolp(p))
            p = Ssymbol_to_string(p);
        std::string s;
        for (iptr i = 0; i < Sstring_length(p); ++i)
            s += (char)Sstring_ref(p, i);
        return s;
    }

    // coerce a scheme value to the field's kind; sets g_configError on failure
    static std::optional<std::pair<std::string, UP<CDeviceValue>>> deviceValue(const SDeviceField& f, ptr v) {
        auto fail = [&](const char* why) -> std::optional<std::pair<std::string, UP<CDeviceValue>>> {
            g_configError = std::format("hl-device-add!: field '{}': {}", f.name, why);
            return std::nullopt;
        };

        if (f.kind == eDeviceKind::BOOL) {
            if (v == Strue) return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(true))};
            if (v == Sfalse) return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(false))};
            return fail("expected #t or #f");
        }
        if (f.kind == eDeviceKind::STRING) {
            if (!Sstringp(v))
                return fail("expected a string");
            return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(schemeDatumToStr(v)))};
        }
        if (f.kind == eDeviceKind::INT || f.kind == eDeviceKind::FLOAT) {
            double d = 0;
            if (Sfixnump(v)) d = (double)Sfixnum_value(v);
            else if (Sflonump(v)) d = Sflonum_value(v);
            else return fail("expected a number");
            if ((f.lo != 0 || f.hi != 0) && (d < f.lo || d > f.hi))
                return fail(std::format("out of range [{:.0g}, {:.0g}]", f.lo, f.hi).c_str());
            if (f.kind == eDeviceKind::INT)
                return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue((Config::INTEGER)d))};
            return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue((Config::FLOAT)d))};
        }
        // VEC2: (x . y) or (x y)
        if (!Spairp(v))
            return fail("expected a coordinate pair");
        auto asNum = [](ptr p, double& out) -> bool {
            if (Sfixnump(p)) { out = (double)Sfixnum_value(p); return true; }
            if (Sflonump(p)) { out = Sflonum_value(p); return true; }
            return false;
        };
        double x, y;
        ptr    tail = Scdr(v);
        if (Spairp(tail)) {
            if (!asNum(Scar(tail), y)) return fail("expected a coordinate pair");
        } else if (!asNum(tail, y))
            return fail("expected a coordinate pair");
        if (!asNum(Scar(v), x)) return fail("expected a coordinate pair");
        return std::pair{std::string(f.name), UP<CDeviceValue>(new CDeviceValue(Config::VEC2(x, y)))};
    }

    // (hl-device-add! NAME . FIELDS)
    static int hlSchemeDeviceAdd(const char* name, ptr fields) {
        if (!g_up || !name || !*name) {
            g_configError = "hl-device-add!: a device name is required";
            return -1;
        }
        if (!Spairp(fields)) {
            g_configError = "hl-device-add!: fields must be a plist, e.g. (hl-device-add! NAME 'enabled #t)";
            return -1;
        }

        std::string dev = name;
        std::replace(dev.begin(), dev.end(), ' ', '-');

        // validate + coerce every field first: a bad field writes nothing
        std::vector<std::pair<std::string, UP<CDeviceValue>>> values;
        ptr l = fields;
        while (Spairp(l)) {
            if (!Spairp(Scdr(l))) {
                g_configError = "hl-device-add!: odd plist of fields";
                return -1;
            }
            const std::string key = schemeDatumToStr(Scar(l));
            const SDeviceField* f  = nullptr;
            for (const auto& F : DEVICE_FIELDS) {
                if (key == F.name) { f = &F; break; }
            }
            if (!f) {
                g_configError = std::format("hl-device-add!: unknown field '{}'", key);
                return -1;
            }
            auto v = deviceValue(*f, Scar(Scdr(l)));
            if (!v)
                return -1;
            values.emplace_back(std::move(*v));
            l = Scdr(Scdr(l));
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

    // (hl-exec-with-rule! CMD . FIELDS) — spawn CMD under a one-shot window
    // rule built from an effects plist ('float #t 'workspace "games" ...).
    // Effects only, no match: the executor tags the spawned window by pid
    // itself (upstream hl.exec rule-object parity, SExecRequest.rule).
    static int hlSchemeExecRule(const char* cmd, ptr fields) {
        if (!g_up || !cmd || !*cmd)
            return -1;

        auto rule = makeShared<Desktop::Rule::CWindowRule>();
        ptr  l    = fields;
        while (Spairp(l)) {
            if (!Spairp(Scdr(l))) {
                g_configError = "hl-exec-with-rule!: odd plist of effects";
                return -1;
            }
            const std::string effect = schemeDatumToStr(Scar(l));
            const auto        e      = Desktop::Rule::windowEffects()->get(std::string_view(effect));
            if (!e) {
                g_configError = std::format("hl-exec-with-rule!: unknown effect '{}'", effect);
                return -1;
            }
            const auto res = rule->addEffect(*e, schemeDatumToStr(Scar(Scdr(l))));
            if (!res) {
                g_configError = std::format("hl-exec-with-rule!: effect '{}': {}", effect, res.error());
                return -1;
            }
            l = Scdr(Scdr(l));
        }

        // an empty field list spawns plain (upstream: empty rule → spawn(proc))
        if (l != Snil) {
            g_configError = "hl-exec-with-rule!: odd plist of effects";
            return -1;
        }

        return (int)Config::Supplementary::executor()
                   ->spawn(Config::Supplementary::SExecRequest{.exec = cmd, .rule = std::move(rule)})
                   .value_or(-1);
    }
    static ptr hlConfigGet(const char* key) {
        if (!g_up)
            return Sfalse;
        auto* val = configValueByKey(key);
        if (!val)
            return Sfalse;
        lua_State* L = configScratch();
        val->push(L);

        std::vector<ptr> roots;
        ptr              result = Sfalse;

        switch (lua_type(L, -1)) {
            case LUA_TNIL: break;
            case LUA_TBOOLEAN: result = lua_toboolean(L, -1) ? Strue : Sfalse; break;
            case LUA_TNUMBER: {
                const auto D = lua_tonumber(L, -1);
                result       = (long long)D == D ? Sinteger((long long)D) : Sflonum(D);
                break;
            }
            case LUA_TSTRING: {
                const char* s = lua_tostring(L, -1);
                result        = Sstring_utf8(s, strlen(s));
                break;
            }
            case LUA_TTABLE: {
                // tables come back as a PLIST (key value key value ...) — the
                // same shape hl-config-add! accepts going in
                std::vector<ptr> elems;
                lua_pushnil(L);
                while (lua_next(L, -2) != 0) {
                    if (lua_type(L, -2) == LUA_TSTRING) {
                        const char* k = lua_tostring(L, -2);
                        ptr         keySym = Sstring_to_symbol(k);
                        marshRoot(keySym, roots);
                        elems.push_back(keySym);
                    } else {
                        elems.push_back(Sinteger(lua_tointeger(L, -2)));
                    }

                    switch (lua_type(L, -1)) {
                        case LUA_TNUMBER: {
                            const auto D = lua_tonumber(L, -1);
                            ptr         v = (long long)D == D ? Sinteger((long long)D) : Sflonum(D);
                            marshRoot(v, roots);
                            elems.push_back(v);
                            break;
                        }
                        case LUA_TSTRING: {
                            const char* s = lua_tostring(L, -1);
                            ptr         v = Sstring_utf8(s, strlen(s));
                            marshRoot(v, roots);
                            elems.push_back(v);
                            break;
                        }
                        case LUA_TBOOLEAN: elems.push_back(lua_toboolean(L, -1) ? Strue : Sfalse); break;
                        default: break;
                    }
                    lua_pop(L, 1);
                }
                result = Snil;
                for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                    result = Scons(*it, result);
                    marshRoot(result, roots);
                }
                break;
            }
            default: break;
        }

        lua_settop(L, 0);
        marshRelease(roots);   // release just before returning; nothing allocates after
        return result;
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

    static int hlWorkspaceChangeId(const char* wsName, double newId) {
        if (!g_up)
            return -1;
        const auto ws = workspaceFromName(wsName);
        if (!ws) {
            g_configError = "no workspace named " + std::string(wsName ? wsName : "");
            return -1;
        }
        return Config::Actions::changeWorkspaceID(ws, sc<int64_t>(newId)) ? 0 : -2;
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

    // ---- curves and animations -------------------------------------------------

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

    // ---- rules: window, layer, workspace ---------------------------------------
    // mirrors the lua hl.window_rule / hl.layer_rule / hl.workspace_rule.
    // Named rules are reused across calls; anonymous rules are unregistered
    // when the config reloads (the reload itself clears the whole engine).

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
    static void clearSchemeRules() {
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

    static int hlWindowRuleCommit(ptr record) {
        if (!g_up || !g_curWindowRule)
            return -1;
        g_ruleIndex.emplace(reinterpret_cast<uintptr_t>(record),
                            std::make_pair(g_curWindowRule, SThunkRef(record)));
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_WINDOW_STATES);
        g_curWindowRule.reset();
        return 0;
    }

    static int hlLayerRuleCommit(ptr record) {
        if (!g_up || !g_curLayerRule)
            return -1;
        g_ruleIndex.emplace(reinterpret_cast<uintptr_t>(record),
                            std::make_pair(g_curLayerRule, SThunkRef(record)));
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_RULES);
        g_curLayerRule.reset();
        return 0;
    }

    static int hlRuleSetEnabled(ptr record, int enabled) {
        if (!g_up)
            return -1;
        const auto it = g_ruleIndex.find(reinterpret_cast<uintptr_t>(record));
        if (it == g_ruleIndex.end())
            return -1;
        it->second.first->setEnabled(enabled != 0);
        return 0;
    }

    static int hlRuleEnabled(ptr record) {
        const auto it = g_ruleIndex.find(reinterpret_cast<uintptr_t>(record));
        return (it != g_ruleIndex.end() && it->second.first->isEnabled()) ? 1 : 0;
    }

    // ---- workspace rules -------------------------------------------------------

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

    // ---- queries ----------------------------------------------------------------

    // selector matching, implemented here: prefixes dispatch to a full-match
    // regex over class/initialclass/title/initialtitle, plus pid:/address:/
    // tag:/stableid: equality and the bare "floating"/"tiled"/"active" forms.
    static bool windowMatchesSelector(const PHLWINDOW& w, const std::string& sel) {
        if (!w || !w->mapped())
            return false;
        if (sel.empty() || sel == "active")
            return Desktop::focusState()->window() == w;

        auto       body = sel;
        auto       isFloat = std::optional<bool>{};
        if (body.starts_with("floating")) {
            isFloat = true;
            body    = "active";
        } else if (body.starts_with("tiled")) {
            isFloat = false;
            body    = "active";
        }

        auto matchRegex = [](const std::string& text, const std::string& pattern) {
            try {
                std::regex re(pattern);
                return std::regex_match(text, re);
            } catch (...) { return false; }
        };

        std::string mode, arg;
        const auto& m = body;
        auto strip  = [&m](const std::string& pfx, std::string& out) { if (m.starts_with(pfx)) { out = m.substr(pfx.size()); return true; } return false; };
        if (strip("class:", arg)) { if (!matchRegex(w->metadata().appID(), arg)) return false; }
        else if (strip("initialclass:", arg)) { if (!matchRegex(w->metadata().initialAppID(), arg)) return false; }
        else if (strip("title:", arg)) { if (!matchRegex(w->metadata().title(), arg)) return false; }
        else if (strip("initialtitle:", arg)) { if (!matchRegex(w->metadata().initialTitle(), arg)) return false; }
        else if (strip("pid:", arg)) { if (std::to_string(w->backend().pid()) != arg) return false; }
        else if (strip("address:", arg)) { if (std::format("0x{:x}", rc<uintptr_t>(w.get())) != arg) return false; }
        else if (strip("tag:", arg)) {
            bool tagged = false;
            if (w->m_ruleApplicator)
                for (const auto& t : w->m_ruleApplicator->m_tagKeeper.getTags())
                    if (matchRegex(t, arg)) { tagged = true; break; }
            if (!tagged) return false;
        }

        if (isFloat && w->isFloating() != *isFloat)
            return false;
        return true;
    }

    static double hlWindowFrom(const char* sel) {
        if (!g_up)
            return -1;
        const std::string selector = sel ? sel : "";
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!windowMatchesSelector(w, selector))
                continue;
            const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
            return (double)id;
        }
        return -1;
    }

    static double hlUrgentWindow() {
        if (!g_up)
            return -1;
        const auto w = Desktop::viewState()->query().urgent().runWindow();
        if (!w)
            return -1;
        const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
        return (double)id;
    }

    static double hlLastWindow() {
        if (!g_up)
            return -1;
        const auto current     = Desktop::focusState()->window();
        const auto& fullHistory = Desktop::History::windowTracker()->fullHistory();
        for (auto it = fullHistory.rbegin(); it != fullHistory.rend(); ++it) {
            const auto candidate = it->lock();
            if (!candidate || !candidate->mapped())
                continue;
            if (current && candidate == current)
                continue;
            const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(candidate));
            return (double)id;
        }
        return -1;
    }

    static ptr monitorIdResult(PHLMONITOR m) {
        if (!m)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLMONITORREF>(m));
        return Sinteger(id);
    }

    static ptr hlMonitorFrom(const char* sel) {
        if (!g_up)
            return Sfalse;
        return monitorIdResult(State::monitorState()->query().configString(sel ? sel : "").run());
    }

    static ptr hlMonitorAt(double x, double y) {
        if (!g_up)
            return Sfalse;
        return monitorIdResult(State::monitorState()->query().vec(Vector2D{x, y}).run());
    }

    static ptr hlMonitorAtCursor() {
        if (!g_up || !Pointer::mgr())
            return Sfalse;
        const auto pos = Pointer::mgr()->untransformedPosition();
        return monitorIdResult(State::monitorState()->query().vec(pos).run());
    }

    static ptr hlActiveMonitor() {
        if (!g_up)
            return Sfalse;
        return monitorIdResult(Desktop::focusState()->monitor());
    }

    static ptr hlActiveWorkspace() {
        if (!g_up)
            return Sfalse;
        const auto mon = Desktop::focusState()->monitor();
        if (!mon || !mon->m_activeWorkspace)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(mon->m_activeWorkspace));
        return Sinteger(id);
    }

    static ptr hlActiveSpecialWorkspace() {
        if (!g_up)
            return Sfalse;
        const auto mon = Desktop::focusState()->monitor();
        if (!mon || !mon->m_activeSpecialWorkspace)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(mon->m_activeSpecialWorkspace));
        return Sinteger(id);
    }

    static ptr hlLastWorkspace() {
        if (!g_up)
            return Sfalse;
        const auto mon     = Desktop::focusState()->monitor();
        const auto current = mon ? mon->m_activeWorkspace : nullptr;
        if (!current)
            return Sfalse;
        const auto previous = Desktop::History::workspaceTracker()->previousWorkspace(current);
        auto       ws       = previous.workspace.lock();
        if (!ws && previous.target.valid())
            ws = State::Workspace::state()->find(previous.target);
        if (!ws)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws));
        return Sinteger(id);
    }

    // ---- workspace/monitor handle getters -------------------------------------
    // Mirror upstream Lua's workspace and monitor object fields 1:1 (see
    // LuaWorkspace.cpp / LuaMonitor.cpp). All getters resolve the handle
    // first; a stale or dead handle yields #f from every getter, like an
    // expired Lua object.

    static ptr boolResult(bool b) {
        return b ? Strue : Sfalse;
    }

    static ptr windowHandleResult(PHLWINDOW w) {
        if (!w)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
        return Sinteger(id);
    }

    static ptr workspaceHandleResult(PHLWORKSPACE ws) {
        if (!ws)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLWORKSPACEREF>(ws));
        return Sinteger(id);
    }

    static ptr monitorHandleResult(PHLMONITOR mon) {
        if (!mon)
            return Sfalse;
        const auto id = (uintptr_t)(new SHandle<PHLMONITORREF>(mon));
        return Sinteger(id);
    }

    template <typename F>
    static ptr wsGet(long long id, F&& fn) {
        if (!g_up)
            return Sfalse;
        const auto ws = workspaceFromId(id);
        if (!ws)
            return Sfalse;
        return fn(ws);
    }

    template <typename F>
    static ptr monGet(long long id, F&& fn) {
        if (!g_up)
            return Sfalse;
        const auto mon = monitorFromId(id);
        if (!mon)
            return Sfalse;
        return fn(mon);
    }

    // -- workspace getters
    static ptr hlWorkspaceName(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { const auto& s = ws->displayName(); return Sstring_utf8(s.c_str(), s.size()); });
    }

    static ptr hlWorkspaceAddressableName(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { const auto& s = ws->addressableName(); return Sstring_utf8(s.c_str(), s.size()); });
    }

    static ptr hlWorkspaceNumber(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto n = ws->numberedID();
            return n ? Sinteger(sc<int>(*n)) : Sfalse;
        });
    }

    static ptr hlWorkspaceMonitor(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return monitorHandleResult(ws->m_monitor.lock()); });
    }

    static ptr hlWorkspaceSpecial(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->type() == Workspace::eWorkspaceType::SPECIAL); });
    }

    static ptr hlWorkspaceActive(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto mon = ws->m_monitor.lock();
            return boolResult(mon && (mon->m_activeWorkspace == ws || mon->m_activeSpecialWorkspace == ws));
        });
    }

    static ptr hlWorkspaceVisible(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->visible()); });
    }

    static ptr hlWorkspaceEmpty(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->getWindowCount() == 0); });
    }

    static ptr hlWorkspacePersistent(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto REGULAR = dynamicPointerCast<Workspace::CRegularWorkspace>(ws);
            return boolResult(REGULAR && REGULAR->isPersistent());
        });
    }

    static ptr hlWorkspaceHasUrgent(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->hasUrgentWindow()); });
    }

    static ptr hlWorkspaceHasFullscreen(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(Fullscreen::controller()->hasFullscreen(ws)); });
    }

    static ptr hlWorkspaceFullscreenMode(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return Sinteger(sc<int>(Fullscreen::controller()->getFullscreenModes(ws).internal)); });
    }

    static ptr hlWorkspaceFullscreenWindow(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return windowHandleResult(Fullscreen::controller()->getFullscreenWindow(ws)); });
    }

    static ptr hlWorkspaceLastWindow(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return windowHandleResult(ws->getLastFocusedWindow()); });
    }

    static ptr hlWorkspaceWindowCount(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return Sinteger(ws->getWindowCount()); });
    }

    static ptr hlWorkspaceGroupCount(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return Sinteger(ws->getGroups()); });
    }

    // windows on the workspace as newline-joined window-handle ids

    static ptr hlWorkspaceTiledLayout(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            std::string layoutName = "unknown";
            const auto  SPACE      = ws->space();
            if (SPACE && SPACE->algorithm() && SPACE->algorithm()->tiledAlgo())
                layoutName = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(SPACE->algorithm()->tiledAlgo().get());
            return Sstring_utf8(layoutName.c_str(), layoutName.size());
        });
    }

    static ptr hlWorkspaceAlive(long long id) {
        return boolResult(g_up && workspaceFromId(id) != nullptr);
    }

    // identity, mirroring hl-window=?: true iff both handles lock to the same
    // live workspace; dead handles are never "the same" as anything
    static ptr hlWorkspaceSame(long long a, long long b) {
        const auto wa = g_up ? workspaceFromId(a) : nullptr;
        const auto wb = g_up ? workspaceFromId(b) : nullptr;
        return boolResult(wa && wb && wa.get() == wb.get());
    }

    // -- monitor getters
    static ptr hlMonitorName(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sstring_utf8(mon->m_name.c_str(), mon->m_name.size()); });
    }

    static ptr hlMonitorDescription(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sstring_utf8(mon->m_description.c_str(), mon->m_description.size()); });
    }

    static ptr hlMonitorNumber(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_id)); });
    }

    static ptr hlMonitorEnabled(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_enabled); });
    }

    static ptr hlMonitorFocused(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(Desktop::focusState()->monitor() == mon); });
    }

    static ptr hlMonitorX(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_position.x)); });
    }

    static ptr hlMonitorY(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_position.y)); });
    }

    static ptr hlMonitorWidth(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_size.x)); });
    }

    static ptr hlMonitorHeight(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_size.y)); });
    }

    static ptr hlMonitorScale(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sflonum(sc<double>(mon->m_scale)); });
    }

    static ptr hlMonitorTransform(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_transform)); });
    }

    static ptr hlMonitorRefreshRate(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sflonum(sc<double>(mon->m_refreshRate)); });
    }

    static ptr hlMonitorMode(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            const auto s = std::format("{}x{}@{}", sc<int>(mon->m_size.x), sc<int>(mon->m_size.y), mon->m_refreshRate);
            return Sstring_utf8(s.c_str(), s.size());
        });
    }

    static ptr hlMonitorDpms(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_dpmsStatus); });
    }

    static ptr hlMonitorVrr(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_vrrActive != 0); });
    }

    static ptr hlMonitor10bit(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_enabled10bit); });
    }

    // reserved area; all-zero means unset
    // when nothing is reserved

    // ---- monitor hardware getters (upstream LuaMonitor parity: serial,
    // physical_width/height, available_modes, mirrors, hardware_details) ----

    static const char* monitorBackendName(Aquamarine::eBackendType t) {
        switch (t) {
            case Aquamarine::AQ_BACKEND_DRM: return "drm";
            case Aquamarine::AQ_BACKEND_WAYLAND: return "wayland";
            case Aquamarine::AQ_BACKEND_HEADLESS: return "headless";
            case Aquamarine::AQ_BACKEND_NULL: return "null";
            default: return "unknown";
        }
    }

    static ptr hlMonitorSerial(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            const auto& s = mon->m_output->serial;
            return Sstring_utf8(s.c_str(), s.size());
        });
    }

    // (physical-width . physical-height), in mm
    static ptr hlMonitorPhysicalSize(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            return Scons(Sinteger((int)mon->m_output->physicalSize.x),
                         Sinteger((int)mon->m_output->physicalSize.y));
        });
    }

    static ptr hlMonitorMirrors(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            std::vector<uintptr_t> ids;
            for (const auto& mirrorRef : mon->m_mirrors) {
                const auto mirror = mirrorRef.lock();
                if (!mirror)
                    continue;
                const auto mid = (uintptr_t)(new SHandle<PHLMONITORREF>(mirror));
                ids.push_back(mid);
            }
            return schemeIntList(ids);
        });
    }

    // list of per-mode plists: ((width w height h refresh-rate r preferred b) ...)
    static ptr hlMonitorAvailableModes(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            std::vector<ptr> roots, modes;
            for (const auto& mode : mon->m_output->modes) {
                if (!mode)
                    continue;
                std::vector<ptr> elems;
                ptr k = Sstring_to_symbol("width");
                marshRoot(k, roots); elems.push_back(k);
                elems.push_back(Sinteger((int)mode->pixelSize.x));
                k = Sstring_to_symbol("height");
                marshRoot(k, roots); elems.push_back(k);
                elems.push_back(Sinteger((int)mode->pixelSize.y));
                k = Sstring_to_symbol("refresh-rate");
                marshRoot(k, roots); elems.push_back(k);
                ptr r = Sflonum(mode->refreshRate / 1000.0);
                marshRoot(r, roots); elems.push_back(r);
                k = Sstring_to_symbol("preferred");
                marshRoot(k, roots); elems.push_back(k);
                elems.push_back(mode->preferred ? Strue : Sfalse);
                ptr m = Snil;
                for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                    m = Scons(*it, m);
                    marshRoot(m, roots);
                }
                modes.push_back(m);
            }
            ptr l = Snil;
            for (auto it = modes.rbegin(); it != modes.rend(); ++it) {
                l = Scons(*it, l);
                marshRoot(l, roots);
            }
            marshRelease(roots);
            return l;
        });
    }

    // plist: (backend "..." hdr b chroma b bt2020 b vrr-capable b)
    static ptr hlMonitorHardwareDetails(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            std::vector<ptr> roots, elems;
            const std::string backend = monitorBackendName(mon->m_output->getBackend()->type());
            ptr k = Sstring_to_symbol("backend");
            marshRoot(k, roots); elems.push_back(k);
            ptr b = Sstring_utf8(backend.c_str(), backend.size());
            marshRoot(b, roots); elems.push_back(b);
            k = Sstring_to_symbol("hdr");
            marshRoot(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->parsedEDID.hdrMetadata.has_value() ? Strue : Sfalse);
            k = Sstring_to_symbol("chroma");
            marshRoot(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->parsedEDID.chromaticityCoords.has_value() ? Strue : Sfalse);
            k = Sstring_to_symbol("bt2020");
            marshRoot(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->parsedEDID.supportsBT2020 ? Strue : Sfalse);
            k = Sstring_to_symbol("vrr-capable");
            marshRoot(k, roots);
            elems.push_back(k); elems.push_back(mon->m_output->vrrCapable ? Strue : Sfalse);
            ptr l = Snil;
            for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                l = Scons(*it, l);
                marshRoot(l, roots);
            }
            marshRelease(roots);
            return l;
        });
    }
    static ptr hlMonitorReserved(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            // a plist: (top n left n right n bottom n)
            const auto&         r = mon->m_reservedArea;
            std::vector<ptr>    roots;
            std::vector<ptr>    elems;
            const std::string   KEYS[] = {"top", "left", "right", "bottom"};
            const int           VALUES[] = {r.top(), r.left(), r.right(), r.bottom()};
            for (int i = 0; i < 4; ++i) {
                ptr k = Sstring_to_symbol(KEYS[i].c_str());
                marshRoot(k, roots);
                elems.push_back(k);
                elems.push_back(Sinteger(VALUES[i]));
            }
            ptr l = Snil;
            for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
                l = Scons(*it, l);
                marshRoot(l, roots);
            }
            marshRelease(roots);
            return l;
        });
    }

    // the monitor this one mirrors, as a handle; #f when not a mirror
    static ptr hlMonitorMirrorOf(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return monitorHandleResult(mon->m_mirrorOf.lock()); });
    }

    static ptr hlMonitorActiveWorkspace(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return workspaceHandleResult(mon->m_activeWorkspace); });
    }

    static ptr hlMonitorActiveSpecialWorkspace(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return workspaceHandleResult(mon->m_activeSpecialWorkspace); });
    }

    static ptr hlMonitorAlive(long long id) {
        return boolResult(g_up && monitorFromId(id) != nullptr);
    }

    static ptr hlMonitorSame(long long a, long long b) {
        const auto ma = g_up ? monitorFromId(a) : nullptr;
        const auto mb = g_up ? monitorFromId(b) : nullptr;
        return boolResult(ma && mb && ma.get() == mb.get());
    }

    // -- selector bridges: handles are accepted anywhere a selector string is,
    // resolved through the canonical selector exactly like upstream's
    // *SelectorOrObject helpers (LuaBindingsInternal.cpp)
    static ptr hlWorkspaceSelector(long long id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto s = Workspace::selector(*ws);
            return Sstring_utf8(s.c_str(), s.size());
        });
    }

    static ptr hlMonitorSelector(long long id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sstring_utf8(mon->m_name.c_str(), mon->m_name.size()); });
    }

    // workspace resolution via the resolver (the query() chain has proven
    // unreliable from the plugin; the resolver+find path is verified)
    static PHLWORKSPACE workspaceFromSelector(const std::string& sel) {
        const auto target = State::Workspace::resolver()->getWorkspaceTargetFromString(sel);
        if (!target.valid())
            return nullptr;
        return State::Workspace::state()->find(target);
    }

    // windows on a workspace: newline-joined handle ids (like hl-windows)
    static ptr hlWorkspaceWindows(const char* sel) {
        if (!g_up)
            return Sfalse;
        const auto ws = workspaceFromSelector(sel ? sel : "");
        if (!ws)
            return Sfalse;
        std::vector<uintptr_t> ids;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w->mapped() || w->m_workspace != ws)
                continue;
            ids.push_back((uintptr_t)(new SHandle<PHLWINDOWREF>(PHLWINDOWREF(w))));
        }
        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    // bare-thunk/record list fire (gestures, screenshare, keyboard-key)
    static void fireSchemeListRec(ptr record, ptr lst) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-list-rec")), record, lst);
        watchdogExit();
    }

    static int hlIsKeyDown(const char* key) {
        if (!g_up || !Keybinds::mgr())
            return -1;
        const auto sym = xkb_keysym_from_name(key ? key : "", XKB_KEYSYM_CASE_INSENSITIVE);
        if (!sym)
            return -1;
        return Keybinds::mgr()->inputState().isKeysymDown(sym) ? 1 : 0;
    }

    static ptr hlLoadedPlugins() {
        if (!g_up)
            return Sfalse;
        ptr l = Snil;
        for (const auto& p : g_pPluginSystem->getAllPlugins()) {
            if (!p)
                continue;
            const std::string name = p->m_name;
            l = Scons(Sstring_utf8(name.c_str(), name.size()), l);
        }
        return l;
    }

    static ptr hlVersion() {
        return Sstring_utf8(HYPRLAND_VERSION, strlen(HYPRLAND_VERSION));
    }

    // windows matching a selector: handle ids as a scheme list
    static ptr hlWindowsFrom(const char* sel) {
        if (!g_up)
            return Sfalse;
        const std::string selector = sel ? sel : "";
        std::vector<uintptr_t> ids;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!windowMatchesSelector(w, selector))
                continue;
            ids.push_back((uintptr_t)(new SHandle<PHLWINDOWREF>(w)));
        }
        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static ptr hlWindowFullscreenHandler(long long id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto name = Fullscreen::controller()->getFullscreenHandlerNameAsString(window);
        return Sstring_utf8(name.c_str(), name.size());
    }

    // ---- notifications ---------------------------------------------------------

    // ---- notifications: shared field parsing (used by hl-notify! AND the
    // live notification objects) ------------------------------------------------
    // icon names, mirroring the lua config's table
    static eIcons schemeIconFromStr(const std::string& ic, bool* ok = nullptr) {
        static const std::pair<const char*, eIcons> ICON_NAMES[] = {
            {"warning", ICON_WARNING}, {"warn", ICON_WARNING},     {"info", ICON_INFO},       {"hint", ICON_HINT},
            {"error", ICON_ERROR},     {"err", ICON_ERROR},       {"confused", ICON_CONFUSED},
            {"question", ICON_CONFUSED}, {"ok", ICON_OK},         {"none", ICON_NONE},
        };
        for (const auto& [n, i] : ICON_NAMES)
            if (ic == n)
                return i;
        if (ok)
            *ok = false;
        return ICON_NONE;
    }

    static eIcons schemeIconFromScheme(ptr v, bool* ok = nullptr) {
        if (Sstringp(v))
            return schemeIconFromStr(schemeDatumToStr(v), ok);
        if (Sfixnump(v)) {
            const auto raw = Sfixnum_value(v);
            if (raw >= ICON_WARNING && raw <= ICON_NONE)
                return sc<eIcons>(raw);
        }
        if (ok)
            *ok = false;
        return ICON_NONE;
    }

    // color: config hex form "0xAARRGGBB" (decimal digits also accepted)
    static std::optional<CHyprColor> schemeColorFromStr(const std::string& cs) {
        if (cs.empty())
            return CHyprColor(0);
        try {
            return CHyprColor(std::stoull(cs.starts_with("0x") || cs.starts_with("0X") ? cs.substr(2) : cs, nullptr, 16));
        } catch (...) {
            return std::nullopt;
        }
    }

    static ptr hlNotify(const char* text, double durationMs, const char* icon, const char* color, double fontSize) {
        if (!g_up)
            return Sfalse;

        eIcons     theIcon = ICON_NONE;
        const auto ic      = schemeIconFromStr(icon ? icon : "");
        if (icon && *icon) {
            bool ok = true;
            theIcon = schemeIconFromStr(icon, &ok);
            if (!ok) {
                g_configError = "hl-notify!: bad icon (expected none/warn/info/hint/error/confused/ok)";
                return Sfalse;
            }
        }
        const auto col = schemeColorFromStr(color ? color : "");
        if (!col) {
            g_configError = "hl-notify!: bad color (expected 0xAARRGGBB)";
            return Sfalse;
        }
        Notification::overlay()->addNotification(text ? text : "", *col, sc<float>(durationMs), theIcon, sc<float>(fontSize));
        return Strue;
    }

    // ---- live notification objects (upstream hl.notification parity) -----------
    // A notification handle is a guardian-managed weak ref; the per-handle
    // 'paused' bit rides in the handle (upstream's SNotificationRef.paused —
    // a paused handle releases its lock when the handle dies). Pause freezes
    // the timeout timer: the bubble stays on screen until dismissed.
    struct SNotificationHandle : IHandle {
        WP<Notification::CNotification> wp;
        bool                            paused = false;
        explicit SNotificationHandle(SP<Notification::CNotification> n) : wp(n) {}
        ~SNotificationHandle() override {
            if (paused)
                if (auto n = wp.lock())
                    n->unlock();
        }
    };

    static SP<Notification::CNotification> notificationFromHandle(long long id) {
        return reinterpret_cast<SNotificationHandle*>(id)->wp.lock();
    }

    static ptr hlNotificationAdd(ptr fields) {
        if (!g_up)
            return Sfalse;

        // walk the plist first: a bad field writes nothing (device-add parity)
        std::string text;
        double      timeout = -1, fontSize = 13.0;
        eIcons      icon = ICON_NONE;
        CHyprColor  color(0);
        ptr         l = fields;
        while (Spairp(l) && Spairp(Scdr(l))) {
            const std::string k = schemeDatumToStr(Scar(l));
            const ptr         v = Scar(Scdr(l));
            if (k == "text") {
                if (!Sstringp(v)) {
                    g_configError = "hl-notification-add!: 'text must be a string";
                    return Sfalse;
                }
                text = schemeDatumToStr(v);
            } else if (k == "timeout" || k == "duration" || k == "time") {
                if (!Sfixnump(v) && !Sflonump(v)) {
                    g_configError = "hl-notification-add!: 'timeout must be a number (ms)";
                    return Sfalse;
                }
                timeout = Sfixnump(v) ? sc<double>(Sfixnum_value(v)) : Sflonum_value(v);
                if (timeout < 0) {
                    g_configError = "hl-notification-add!: 'timeout must be >= 0";
                    return Sfalse;
                }
            } else if (k == "icon") {
                bool ok = true;
                icon = schemeIconFromScheme(v, &ok);
                if (!ok) {
                    g_configError = "hl-notification-add!: bad 'icon (expected none/warn/info/hint/error/confused/ok or an id 0-6)";
                    return Sfalse;
                }
            } else if (k == "color") {
                const auto c = schemeColorFromStr(Sstringp(v) ? schemeDatumToStr(v) : (Sfixnump(v) ? std::to_string(Sfixnum_value(v)) : ""));
                if (!c) {
                    g_configError = "hl-notification-add!: bad 'color (expected 0xAARRGGBB)";
                    return Sfalse;
                }
                color = *c;
            } else if (k == "font-size") {
                if (!Sfixnump(v) && !Sflonump(v)) {
                    g_configError = "hl-notification-add!: 'font-size must be a number";
                    return Sfalse;
                }
                fontSize = Sfixnump(v) ? sc<double>(Sfixnum_value(v)) : Sflonum_value(v);
                if (fontSize <= 0) {
                    g_configError = "hl-notification-add!: 'font-size must be > 0";
                    return Sfalse;
                }
            } else {
                g_configError = std::format("hl-notification-add!: unknown field '{}'", k);
                return Sfalse;
            }
            l = Scdr(Scdr(l));
        }
        if (text.empty()) {
            g_configError = "hl-notification-add!: 'text is required";
            return Sfalse;
        }
        if (timeout < 0) {
            g_configError = "hl-notification-add!: 'timeout is required";
            return Sfalse;
        }

        const auto n = Notification::overlay()->addNotification(text, color, sc<float>(timeout), icon, sc<float>(fontSize));
        if (!n)
            return Sfalse;
        return Sinteger((uintptr_t)(new SNotificationHandle(n)));
    }

    static ptr hlNotificationList() {
        if (!g_up)
            return Sfalse;
        std::vector<uintptr_t> ids;
        for (const auto& n : Notification::overlay()->getNotifications())
            ids.push_back((uintptr_t)(new SNotificationHandle(n)));
        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static ptr hlNotificationText(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        if (!n)
            return Sfalse;
        return Sstring_utf8(n->text().c_str(), n->text().size());
    }

    static ptr hlNotificationTimeout(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sflonum(n->timeMs()) : Sfalse;
    }

    static ptr hlNotificationColor(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sinteger(n->color().getAsHex()) : Sfalse;
    }

    static ptr hlNotificationIcon(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sinteger(sc<int>(n->icon())) : Sfalse;
    }

    static ptr hlNotificationFontSize(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sflonum(n->fontSize()) : Sfalse;
    }

    static ptr hlNotificationElapsed(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sflonum(n->timeElapsedMs()) : Sfalse;
    }

    static ptr hlNotificationAge(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        return n ? Sflonum(n->timeElapsedSinceCreationMs()) : Sfalse;
    }

    static ptr hlNotificationAlive(long long id) {
        if (!g_up)
            return Sfalse;
        return notificationFromHandle(id) ? Strue : Sfalse;
    }

    static ptr hlNotificationSame(long long a, long long b) {
        if (!g_up)
            return Sfalse;
        const auto na = notificationFromHandle(a);
        const auto nb = notificationFromHandle(b);
        return (na && nb && na.get() == nb.get()) ? Strue : Sfalse;
    }

    // expired handles mutate as silent no-ops (upstream parity)
    static int hlNotificationTextSet(long long id, const char* text) {
        if (!g_up)
            return -1;
        if (const auto n = notificationFromHandle(id))
            n->setText(std::string(text ? text : ""));
        return 0;
    }

    static int hlNotificationTimeoutSet(long long id, double ms) {
        if (!g_up)
            return -1;
        if (ms < 0) {
            g_configError = "hl-notification-timeout-set!: timeout must be >= 0";
            return -2;
        }
        if (const auto n = notificationFromHandle(id))
            n->resetTimeout(sc<float>(ms));
        return 0;
    }

    static int hlNotificationColorSet(long long id, const char* color) {
        if (!g_up)
            return -1;
        const auto c = schemeColorFromStr(color ? color : "");
        if (!c) {
            g_configError = "hl-notification-color-set!: bad color (expected 0xAARRGGBB)";
            return -2;
        }
        if (const auto n = notificationFromHandle(id))
            n->setColor(*c);
        return 0;
    }

    static int hlNotificationIconSet(long long id, ptr v) {
        if (!g_up)
            return -1;
        bool ok = true;
        const auto icon = schemeIconFromScheme(v, &ok);
        if (!ok) {
            g_configError = "hl-notification-icon-set!: bad icon (expected none/warn/info/hint/error/confused/ok or an id 0-6)";
            return -2;
        }
        if (const auto n = notificationFromHandle(id))
            n->setIcon(icon);
        return 0;
    }

    static int hlNotificationFontSizeSet(long long id, double size) {
        if (!g_up)
            return -1;
        if (size <= 0) {
            g_configError = "hl-notification-font-size-set!: font size must be > 0";
            return -2;
        }
        if (const auto n = notificationFromHandle(id))
            n->setFontSize(sc<float>(size));
        return 0;
    }

    static int hlNotificationPausedSet(long long id, int on) {
        if (!g_up)
            return -1;
        auto* h = reinterpret_cast<SNotificationHandle*>(id);
        if (const auto n = h->wp.lock()) {
            if (on && !h->paused) {
                n->lock();
                h->paused = true;
            } else if (!on && h->paused) {
                n->unlock();
                h->paused = false;
            }
        }
        return 0;
    }

    static ptr hlNotificationPausedQ(long long id) {
        if (!g_up)
            return Sfalse;
        const auto n = notificationFromHandle(id);
        if (!n)
            return Sfalse;
        return n->isLocked() ? Strue : Sfalse;
    }

    static int hlNotificationDismiss(long long id) {
        if (!g_up)
            return -1;
        if (const auto n = notificationFromHandle(id))
            Notification::overlay()->dismissNotification(n);
        return 0;
    }

    // ---- timer handles ----------------------------------------------------------


    static STimerEntry* timerByRecord(ptr record) {
        const auto it = g_timerIndex.find(reinterpret_cast<uintptr_t>(record));
        return it == g_timerIndex.end() ? nullptr : &it->second;
    }

    static int hlTimerSetEnabled(ptr record, int enabled) {
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

    static int hlTimerEnabled(ptr record) {
        const auto* e = timerByRecord(record);
        return (e && e->timer && e->timer->armed()) ? 1 : 0;
    }

    static int hlTimerSetTimeout(ptr record, double ms) {
        if (!g_up)
            return -1;
        auto* e = timerByRecord(record);
        if (!e || ms < 1)
            return -1;
        e->ms = sc<uint64_t>(ms);
        e->timer->updateTimeout(std::chrono::milliseconds(sc<int64_t>(ms)));
        return 0;
    }

    static int hlTimerCancel(ptr record) {
        if (!g_up || !g_pEventLoopManager)
            return -1;
        auto* e = timerByRecord(record);
        if (!e)
            return -1;
        e->timer->cancel();
        g_pEventLoopManager->removeTimer(e->timer);
        g_timerIndex.erase(reinterpret_cast<uintptr_t>(record));
        return 0;
    }

    // ---- exec variants ----------------------------------------------------------

    static int hlSchemeExecRaw(const char* cmd) {
        if (!g_up || !cmd)
            return -1;
        return (int)Config::Supplementary::executor()->spawnRaw(cmd).value_or(-1);
    }

    static int hlSchemeExecWithRules(const char* cmd) {
        if (!g_up || !cmd)
            return -1;
        return (int)Config::Supplementary::executor()->spawnWithRules(cmd).value_or(-1);
    }

    // ---- gestures ---------------------------------------------------------------
    // A scheme thunk (or three, for live gestures) behind the trackpad gesture
    // system. Registered gestures are cleared by the config reload (the gesture
    // manager clears itself), so no extra bookkeeping is needed.

    // gesture event plist — upstream pushGestureEvent parity (LuaFunctionGesture
    // .cpp:42-122). The handler is APPLIED the plist fields, so callbacks
    // destructure them as normal lambda args:
    //   (lambda (phase direction type time-ms fingers delta . rest))   ; begin/update
    //   (lambda (phase direction type time-ms cancelled) ...)          ; end
    static ptr gestureEventPlist(std::vector<ptr>& roots, const char* phase, const std::string& dir,
                                 const char* type, uint32_t timeMs, std::optional<int> fingers, ptr deltaPair,
                                 ptr scale, ptr rotation, ptr cancelled) {
        std::vector<ptr> elems;
        auto push = [&](ptr p) { marshRoot(p, roots); elems.push_back(p); };
        push(Sstring_to_symbol("phase"));      push(Sstring_utf8(phase, strlen(phase)));
        push(Sstring_to_symbol("direction"));  push(Sstring_utf8(dir.c_str(), dir.size()));
        push(Sstring_to_symbol("type"));       push(Sstring_utf8(type, strlen(type)));
        push(Sstring_to_symbol("time-ms"));    elems.push_back(Sinteger((int)timeMs));
        if (fingers) { push(Sstring_to_symbol("fingers")); elems.push_back(Sinteger(*fingers)); }
        if (deltaPair) { push(Sstring_to_symbol("delta")); push(deltaPair); }
        if (scale && scale != Sfalse) { push(Sstring_to_symbol("scale")); push(scale); }
        if (rotation && rotation != Sfalse) { push(Sstring_to_symbol("rotation")); push(rotation); }
        if (cancelled && cancelled != Sfalse) { push(Sstring_to_symbol("cancelled")); push(cancelled); }
        ptr l = Snil;
        for (auto it = elems.rbegin(); it != elems.rend(); ++it) {
            l = Scons(*it, l);
            marshRoot(l, roots);
        }
        return l;
    }

    // bare-thunk variant: gestures carry their callbacks directly (SThunkRef
    // members), so the fire passes the callable itself; the plist is APPLIED
    // to it (spread args)
    static void fireSchemeList(ptr thunk, ptr lst) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-list")), thunk, lst);
        watchdogExit();
    }

    template <typename E>
    static void fireSchemeGestureEvent(ptr thunk, const char* phase, const E& e, const std::string& dir) {
        if (!g_up)
            return;
        std::vector<ptr>      roots;
        constexpr bool        IS_END = std::is_same_v<E, ITrackpadGesture::STrackpadGestureEnd>;
        ptr                   delta  = Snil, scale = Sfalse, rotation = Sfalse;
        if constexpr (!IS_END) {
            const auto& d = e.swipe ? e.swipe->delta : e.pinch->delta;
            ptr x = Sflonum(d.x); marshRoot(x, roots);
            ptr y = Sflonum(d.y); marshRoot(y, roots);
            delta = Scons(x, y); marshRoot(delta, roots);
            if (e.pinch) {
                scale = Sflonum(e.pinch->scale); marshRoot(scale, roots);
                rotation = Sflonum(e.pinch->rotation); marshRoot(rotation, roots);
            }
        }
        ptr cancelled = Sfalse;
        if constexpr (IS_END)
            cancelled = (e.swipe ? e.swipe->cancelled : e.pinch->cancelled) ? Strue : Sfalse;
        std::optional<int> fingers;
        if constexpr (!IS_END)
            fingers = (int)(e.swipe ? e.swipe->fingers : e.pinch->fingers);
        ptr lst = gestureEventPlist(roots, phase, dir,
                                    e.swipe ? "swipe" : "pinch",
                                    e.swipe ? e.swipe->timeMs : e.pinch->timeMs,
                                    fingers,
                                    delta, scale, rotation, cancelled);
        marshRelease(roots);
        fireSchemeList(thunk, lst);
    }

    class CSchemeGesture : public ITrackpadGesture {
      public:
        // the callbacks arrive as thunks and are carried locked; Snil means
        // "unused" (the counted lock no-ops on immediates). The gesture
        // manager destroys us at config reload, which unlocks them.
        CSchemeGesture(ptr begin, ptr update, ptr end, const char* direction) :
            m_begin(begin), m_update(update), m_end(end), m_direction(direction ? direction : "") {}

        void  begin(const STrackpadGestureBegin& e) override {
            if (!Snullp(m_begin.obj))
                fireSchemeGestureEvent(m_begin.obj, "start", e, m_direction);
        }
        void  update(const STrackpadGestureUpdate& e) override {
            if (!Snullp(m_update.obj))
                fireSchemeGestureEvent(m_update.obj, "update", e, m_direction);
        }
        void  end(const STrackpadGestureEnd& e) override {
            if (!Snullp(m_end.obj))
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
    static ptr gestureArgGet(ptr args, const char* key) {
        for (ptr l = args; Spairp(l) && Spairp(Scdr(l)); l = Scdr(Scdr(l)))
            if (Ssymbolp(Scar(l)) && schemeDatumToStr(Scar(l)) == key)
                return Scar(Scdr(l));
        return Sfalse;
    }

    static std::string gestureArgStr(ptr args, const char* key) {
        // symbol (mode tags) or string (special workspace name) values
        const ptr v = gestureArgGet(args, key);
        return (Ssymbolp(v) || Sstringp(v)) ? schemeDatumToStr(v) : std::string();
    }

    static double gestureArgDouble(ptr args, const char* key) {
        const ptr v = gestureArgGet(args, key);
        return Sflonump(v) ? Sflonum_value(v) : 1.0;
    }

    class IGestureMaker {
      public:
        virtual UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection dir) = 0;
        virtual ~IGestureMaker() = default;
    };

    // the five no-argument built-ins
    template <typename G>
    class CTrivialGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr, eTrackpadGestureDirection) override {
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
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection) override {
            return makeUnique<CFloatTrackpadGesture>(gestureArgStr(args, "mode"));
        }
    };
    static CFloatGestureMaker s_floatGestureMaker;

    class CFullscreenGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection) override {
            return makeUnique<CFullscreenTrackpadGesture>(gestureArgStr(args, "mode"));
        }
    };
    static CFullscreenGestureMaker s_fullscreenGestureMaker;

    class CSpecialWorkspaceGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection) override {
            return makeUnique<CSpecialWorkspaceGesture>(gestureArgStr(args, "name"));
        }
    };
    static CSpecialWorkspaceGestureMaker s_specialWorkspaceGestureMaker;

    class CCursorZoomGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection) override {
            // the underlying ctor parses a zoom string; format our typed number
            return makeUnique<CCursorZoomTrackpadGesture>(std::format("{}", gestureArgDouble(args, "zoom")), gestureArgStr(args, "mode"));
        }
    };
    static CCursorZoomGestureMaker s_cursorZoomGestureMaker;

    // custom: the args are the three thunks ((start . T) (update . T)
    // (finish . T)); CSchemeGesture locks them into SThunkRef members
    class CCustomGestureMaker final : public IGestureMaker {
      public:
        UP<ITrackpadGesture> make(ptr args, eTrackpadGestureDirection dir) override {
            const auto toNil = [](ptr p) { return p == Sfalse ? Snil : p; };
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

    static Input::ModifierMask gestureMods(const char* mods) {
        // space-separated modifier names → mask (SUPER = META)
        uint8_t raw = 0;
        if (!mods || !*mods)
            return Input::ModifierMask(sc<Input::eKeyboardModifiers>(raw));
        std::istringstream ss(mods);
        for (std::string tok; ss >> tok;) {
            std::transform(tok.begin(), tok.end(), tok.begin(), ::toupper);
            if (tok == "SHIFT")
                raw |= 1;
            else if (tok == "CAPS")
                raw |= 2;
            else if (tok == "CTRL" || tok == "CONTROL")
                raw |= 4;
            else if (tok == "ALT")
                raw |= 8;
            else if (tok == "MOD3")
                raw |= 32;
            else if (tok == "SUPER" || tok == "META" || tok == "MOD2")
                raw |= 64;
            else if (tok == "MOD5")
                raw |= 128;
        }
        return Input::ModifierMask(sc<Input::eKeyboardModifiers>(raw));
    }

    // (recipe, fingers, direction, mods, scale, disableInhibit) → 0 ok;
    // recipe is (maker . args) — the maker constructs the ITrackpadGesture
    // (built-in or custom alike), which moves into the manager, owned from
    // birth; destroyed at config reload, which also unlocks any thunks it
    // carried. The addGesture result (overshadow rules) is checked.
    static int hlSchemeGesture(ptr recipe, int fingers, const char* direction, const char* mods, double scale, int disableInhibit) {
        if (!g_up || !g_pTrackpadGestures)
            return -1;
        const auto dir = g_pTrackpadGestures->dirForString(direction ? direction : "");
        if (dir == TRACKPAD_GESTURE_DIR_NONE) {
            g_configError = std::string("hl-gesture: invalid direction '") + (direction ? direction : "") + "'";
            return -1;
        }
        if (!Spairp(recipe) || !Sfixnump(Scar(recipe))) {
            g_configError = "hl-gesture: 'action is not a gesture action (see the hl-make-*-gesture constructors)";
            return -1;
        }
        const auto maker = gestureMakerFromAddress(Sfixnum_value(Scar(recipe)));
        if (!maker) {
            g_configError = "hl-gesture: 'action is not a valid gesture action (see the hl-make-*-gesture constructors)";
            return -1;
        }
        auto gesture = maker->make(Scdr(recipe), dir);
        const auto result =
            g_pTrackpadGestures->addGesture(std::move(gesture), sc<size_t>(fingers), dir, gestureMods(mods), sc<float>(scale), disableInhibit != 0);
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
    static int hlSchemeGestureRemove(int fingers, const char* direction, const char* mods, double scale, int disableInhibit) {
        if (!g_up || !g_pTrackpadGestures)
            return -1;
        const auto dir = g_pTrackpadGestures->dirForString(direction ? direction : "");
        if (dir == TRACKPAD_GESTURE_DIR_NONE) {
            g_configError = std::string("hl-gesture: invalid direction '") + (direction ? direction : "") + "'";
            return -1;
        }
        const auto result = g_pTrackpadGestures->removeGesture(sc<size_t>(fingers), dir, gestureMods(mods), sc<float>(scale), disableInhibit != 0);
        if (!result) {
            if (result.error() == "Can't remove a non-existent gesture")
                return 1;
            g_configError = std::string("hl-gesture: ") + result.error();
            return -1;
        }
        return 0;
    }


    static ptr hlSchemeWindowInitialClass(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().initialAppID();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static ptr hlSchemeWindowInitialTitle(long long id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().initialTitle();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static int hlSchemeWindowX11(long long id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->backend().isX11()) ? 1 : 0;
    }

    // ---- layer surfaces as objects (upstream HL.LayerSurface parity) ----------
    // layer surfaces die independently -> weak handles via the guardian
    // (record-cell model); every getter returns #f when the surface is gone
    using PHLLSGROUPREF = Hyprutils::Memory::CWeakPointer<Desktop::View::CLayerSurface>;

    static SP<Desktop::View::CLayerSurface> layerFromHandle(long long id) {
        return reinterpret_cast<SHandle<PHLLSGROUPREF>*>(id)->wp.lock();
    }

    static int hlSchemeLayerAlive(long long id) {
        if (!g_up)
            return -1;
        return layerFromHandle(id) ? 1 : 0;
    }

    static int hlSchemeLayerSame(long long a, long long b) {
        if (!g_up)
            return -1;
        const auto la = layerFromHandle(a);
        const auto lb = layerFromHandle(b);
        return (la && lb && la.get() == lb.get()) ? 1 : 0;
    }

    static ptr hlSchemeLayerAddress(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        const auto addr = std::format("0x{:x}", reinterpret_cast<uintptr_t>(ls.get()));
        return Sstring_utf8(addr.c_str(), addr.size());
    }

    static ptr hlSchemeLayerPid(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        return ls ? Sinteger(sc<int64_t>(ls->getPID())) : Sfalse;
    }

    static ptr hlSchemeLayerMonitorId(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        const auto mon = ls->m_monitor.lock();
        if (!mon)
            return Sfalse;
        return Sinteger((uintptr_t)(new SHandle<PHLMONITORREF>(mon)));
    }

    static ptr hlSchemeLayerNamespace(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return Sstring_utf8(ls->m_namespace.c_str(), ls->m_namespace.size());
    }

    static ptr hlSchemeLayerLevel(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        return ls ? Sinteger(sc<int64_t>(ls->m_layer)) : Sfalse;
    }

    static ptr hlSchemeLayerMapped(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return ls->mapped() ? Strue : Sfalse;
    }

    static ptr hlSchemeLayerKbInteractivity(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        return ls ? Sinteger(sc<int64_t>(ls->m_keyboardInteractivity)) : Sfalse;
    }

    static ptr hlSchemeLayerAboveFullscreen(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return (ls->m_flags & Desktop::View::LAYER_FLAG_ABOVE_FULLSCREEN) ? Strue : Sfalse;
    }

    static ptr hlSchemeLayerPosition(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return Scons(Sinteger((int)ls->m_geometry.x), Sinteger((int)ls->m_geometry.y));
    }

    static ptr hlSchemeLayerSize(long long id) {
        if (!g_up)
            return Sfalse;
        const auto ls = layerFromHandle(id);
        if (!ls)
            return Sfalse;
        return Scons(Sinteger((int)ls->m_geometry.width), Sinteger((int)ls->m_geometry.height));
    }

    // (monitorFilter, namespaceFilter) — nullptr/empty = no filter; the
    // monitor crosses as a handle id (0 = none) after Scheme-side coercion
    static ptr hlLayers(long long monId, ptr nsFilter) {
        if (!g_up)
            return Sfalse;
        const std::string ns = nsFilter && Sstringp(nsFilter) ? schemeDatumToStr(nsFilter) : "";
        PHLWINDOWREF dummy; // unused; keeps the compiler from warning on the include order
        (void)dummy;
        PHLMONITOR monFilter;
        if (monId > 0)
            monFilter = reinterpret_cast<SHandle<PHLMONITORREF>*>(monId)->wp.lock();

        std::vector<uintptr_t> ids;
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
                    ids.push_back((uintptr_t)(new SHandle<PHLLSGROUPREF>(ls)));
                }
            }
        }
        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static ptr hlSchemeMonitorNames() {
        if (!g_up)
            return Sfalse;

        std::vector<uintptr_t> ids;
        for (const auto& m : State::monitorState()->monitors()) {
            ids.push_back((uintptr_t)(new SHandle<PHLMONITORREF>(m)));
        }

        return ids.empty() ? Sfalse : schemeIntList(ids);
    }

    static int hlSchemeWindowEventListen(ptr record, int which) {
        if (!g_up)
            return -1;

        switch (which) {
            case 0: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.openLate.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 1: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.close.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 2: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.title.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 3: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.class_.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 4: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.urgent.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 5: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.pin.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 6: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.fullscreen.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 7: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.moveToWorkspace.listen([ref = SThunkRef(record)](PHLWINDOW w, PHLWORKSPACE ws) { fireSchemeWin(ref.obj, w); })); break;
            case 8: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.active.listen([ref = SThunkRef(record)](PHLWINDOW w, Desktop::eFocusReason) { fireSchemeWin(ref.obj, w); })); break;
            case 9: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.openEarly.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 10: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.kill.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            case 11: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.bell.listen([ref = SThunkRef(record)](PHLWINDOW w, Event::SCallbackInfo&) { fireSchemeWin(ref.obj, w); })); break;
            case 12: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.updateRules.listen([ref = SThunkRef(record)](PHLWINDOW w) { fireSchemeWin(ref.obj, w); })); break;
            default: break;
        }

        return 0;
    }

    // minimize fires with (window, state): pass the bool as a second arg
    static int hlSchemeWindowMinimizeListen(ptr record) {
        if (!g_up)
            return -1;

        SThunkRef ref(record);
        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.minimize.listen([ref](PHLWINDOW w, bool state) {
            if (!g_up || !w)
                return;
            const auto winId = (uintptr_t)(new SHandle<PHLWINDOWREF>(w));
            Scall3(Stop_level_value(Sstring_to_symbol("hl--fire-win-state-rec")), ref.obj, Sinteger(winId), state ? Strue : Sfalse);
        }));
        return 0;
    }

    // lifecycle: 0 = start (session's first render frame), 1 = shutdown
    // (the exit action). Matches upstream: a handler registered after start
    // already fired (only possible when the plugin itself loaded before the
    // first frame) runs on the next loop pass.
    static int hlSchemeLifecycleListen(ptr record, int which) {
        if (!g_up)
            return -1;

        SThunkRef ref(record);

        if (which == 0) {
            if (g_startSeen) {
                // the handler is registered by Scheme only AFTER this call
                // returns, so the immediate fire must wait for the next pass
                if (g_pEventLoopManager)
                    g_pEventLoopManager->doLater([ref] { fireScheme(ref.obj); });
            } else
                g_pendingStart.emplace_back(record);
        } else
            g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.exit.listen([ref] { fireScheme(ref.obj); }));

        return 0;
    }

    static int hlSchemeMonitorListen(ptr record, int which) {
        if (!g_up)
            return -1;
        switch (which) {
            case 0: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.monitor.added.listen([ref = SThunkRef(record)](PHLMONITOR m) { fireSchemeMon(ref.obj, m); })); break;
            case 1: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.monitor.removed.listen([ref = SThunkRef(record)](PHLMONITOR m) { fireSchemeMon(ref.obj, m); })); break;
            case 2: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.monitor.focused.listen([ref = SThunkRef(record)](PHLMONITOR m) { fireSchemeMon(ref.obj, m); })); break;
            case 3: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.monitor.layoutChanged.listen([ref = SThunkRef(record)] { fireScheme(ref.obj); })); break;
            default: break;
        }
        return 0;
    }

    static int hlSchemeWorkspaceListen(ptr record, int which) {
        if (!g_up)
            return -1;
        switch (which) {
            case 0: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.created.listen([ref = SThunkRef(record)](PHLWORKSPACEREF ws) { auto w = ws.lock(); if (w) fireSchemeWs(ref.obj, w); })); break;
            // removed fires from ~CHLWorkspace: the payload cannot be locked
            // (hyprutils marks the impl "destroying"), but the data pointer
            // stays valid until the destructor returns, so the name COULD be
            // read through it — the same members the destructor itself just
            // formatted into its IPC events.
            //
            // Decision (2026-09-18): this event delivers a born-dead handle
            // whose getters all return #f, exactly matching upstream Lua (an
            // expired object reads all-nil). The name is retrievable at this
            // instant — a later enhancement could snapshot it into the handle
            // at fire time and expose it through (hl-workspace-name), but that
            // would go beyond what Lua offers, so it is deliberately not done.
            case 1: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.removed.listen([ref = SThunkRef(record)](PHLWORKSPACEREF ws) { fireSchemeWsRef(ref.obj, ws); })); break;
            // fires (ws mon) handles; ws is #f when no special workspace is
            // open on the monitor (upstream crosses nil the same way)
            case 2: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.specialActive.listen([ref = SThunkRef(record)](PHLWORKSPACE ws, PHLMONITOR mon) { fireSchemeWsMon(ref.obj, ws, mon); })); break;
            case 3: g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.moveToMonitor.listen([ref = SThunkRef(record)](PHLWORKSPACE ws, PHLMONITOR mon) { fireSchemeWsMon(ref.obj, ws, mon); })); break;
            default: break;
        }
        return 0;
    }

    static int hlSchemeConfigReloadedListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.config.reloaded.listen([ref = SThunkRef(record)] { fireScheme(ref.obj); }));
        return 0;
    }

    // config.preReload — upstream maps config.unload onto it
    static int hlSchemeConfigUnloadListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.config.preReload.listen([ref = SThunkRef(record)] { fireScheme(ref.obj); }));
        return 0;
    }

    // window.destroy: zero-argument callback (upstream delivers nil — the bus
    // event is Event<PHLWINDOWREF>; identity belongs to the window-close notification)
    static int hlSchemeWindowDestroyListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.window.destroy.listen([ref = SThunkRef(record)](PHLWINDOWREF) { fireScheme(ref.obj); }));
        return 0;
    }

    // screenshare.state — callbacks receive (active? type name); upstream
    // dispatches 3 positional args (LuaEventHandler.cpp:166)
    static int hlSchemeScreenshareListen(ptr record) {
        if (!g_up)
            return -1;
        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.screenshare.state.listen([ref = SThunkRef(record)](bool state, uint8_t type, const std::string& name) {
            if (!g_up)
                return;
            std::vector<ptr> roots;
            ptr nm = Sstring_utf8(name.c_str(), name.size());
            marshRoot(nm, roots);
            ptr lst = Scons(state ? Strue : Sfalse, Scons(Sinteger((int)type), Scons(nm, Snil)));
            marshRoot(lst, roots);
            marshRelease(roots);
            fireSchemeListRec(ref.obj, lst);   // (active? type name)
        }));
        return 0;
    }

    // input.keyboard.key — high-frequency (every key event); handlers must be
    // trivial. Observe-only: the bus event is Cancellable, Scheme listeners
    // never take the cancellation. keycode is +8 (libinput → xkb), as upstream.
    static int hlSchemeKeyboardKeyListen(ptr record) {
        if (!g_up)
            return -1;
        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.input.keyboard.key.listen([ref = SThunkRef(record)](const IKeyboard::SKeyEvent& keyEvent, Event::SCallbackInfo& _) {
            if (!g_up)
                return;
            fireSchemeListRec(ref.obj, schemeIntList({(int)keyEvent.keycode + 8, (int)keyEvent.timeMs, (int)keyEvent.state}));
        }));
        return 0;
    }

    // layer.opened / layer.closed — callbacks receive the layer's namespace
    static int hlSchemeLayerListen(ptr record, int which) {
        if (!g_up)
            return -1;

        if (which == 0)
            g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.layer.opened.listen([ref = SThunkRef(record)](PHLLS ls) { if (g_up && ls) fireSchemeStr(ref.obj, ls->m_namespace); }));
        else
            g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.layer.closed.listen([ref = SThunkRef(record)](PHLLS ls) { if (g_up && ls) fireSchemeStr(ref.obj, ls->m_namespace); }));
        return 0;
    }

    // handler receives #t when the prop refresh ran as scheduled, #f when it
    // was executed prematurely
    static int hlSchemePropsRefreshedListen(ptr record) {
        if (!g_up)
            return -1;

        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.config.props_refreshed.listen([ref = SThunkRef(record)](const bool scheduled) { fireSchemeBool(ref.obj, scheduled); }));
        return 0;
    }

    // unlisten: drop the connection; its destruction unregisters from the
    // bus and releases the handler record's lock. Upstream HL.EventSub-
    // scription:remove parity. The record goes inert: cancel again -> -1.
    static int hlSchemeEventCancel(ptr record) {
        if (!g_up)
            return -1;
        const auto it = g_eventConnections.find(reinterpret_cast<uintptr_t>(record));
        if (it == g_eventConnections.end())
            return -1;
        g_eventConnections.erase(it);
        return 0;
    }

    // upstream HL.EventSubscription:is_active parity
    static int hlSchemeEventActive(ptr record) {
        if (!g_up)
            return -1;
        return g_eventConnections.count(reinterpret_cast<uintptr_t>(record)) ? 1 : 0;
    }

    // identity, mirroring Lua's windowEq: two handles are the same window
    // iff they lock to the same underlying object. Dead handles are never
    // "the same" as anything.
    static int hlSchemeWindowSame(long long idA, long long idB) {
        if (!g_up)
            return 0;

        const auto a = windowFromId(idA);
        const auto b = windowFromId(idB);
        return (a && b && a.get() == b.get()) ? 1 : 0;
    }

    static ptr hlSchemeCurrentSubmap() {
        if (!g_up || !Keybinds::mgr())
            return Sfalse;

        const auto submap = std::string(Keybinds::mgr()->currentSubmap());
        return Sstring_utf8(submap.c_str(), submap.size());
    }

    static ptr hlSchemeCursorPos() {
        if (!g_up || !Pointer::mgr())
            return Sfalse;

        const auto pos = Pointer::mgr()->untransformedPosition();
        return Scons(Sinteger((int)pos.x), Sinteger((int)pos.y));   // (x . y)
    }

    static int hlSchemeWorkspaceActiveListen(ptr record) {
        if (!g_up)
            return -1;

        SThunkRef ref(record);
        g_eventConnections.emplace(reinterpret_cast<uintptr_t>(record), Event::bus()->m_events.workspace.active.listen([ref](PHLWORKSPACE ws) {
            fireSchemeWs(ref.obj, ws);
        }));
        return 0;
    }

    static void reloadScheme() {
        if (!g_up || g_configPath.empty())
            return;

        // the config reload cleared the rule engine; drop our rule state so
        // the fresh config run re-registers everything
        clearSchemeRules();

        // lua config reloads clear every bind in the registry; that
        // destruction runs our capture destructors, which release the
        // record locks — nothing to clean up here (we hold no references)

        // timers from the previous generation must not fire into the new one;
        // destroying them releases their record locks via the capture dtor
        if (g_pEventLoopManager) {
            for (auto& [addr, e] : g_timerIndex) {
                e.timer->cancel();
                g_pEventLoopManager->removeTimer(e.timer);
            }
            g_timerIndex.clear();
        }

        // drop event subscriptions; the new generation re-registers. This is
        // wholesale BY DESIGN — a reload means fresh handlers, and it is the
        // handler lifetime (each entry's destruction also unlocks its record)
        g_eventConnections.clear();

        // old-generation handles: their Scheme records became unreachable
        // with the generation, so the guardian reaps them at the next GC

        // layouts unregister + re-register with the new generation
        Layouts::clear();

        // re-binding the config value caches: CConfigValue readers (e.g. the
        // layout matcher's general:layout) hold copies made at first use, and
        // this reload may have changed what they should see.
        CConfigValueBase::flushCaches();

        Scall0(Stop_level_value(Sstring_to_symbol("hl--reset")));

        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--load")), Sstring(g_configPath.c_str()));
        if (r == Sfalse)
            LOG(Log::ERR, "[scheme] failed to load {}", g_configPath);
        else
            LOG(Log::INFO, "[scheme] loaded {}", g_configPath);
    }

    static void drainWatch() {
        alignas(struct inotify_event) char buf[4096];
        bool                               dirty = false;

        while (true) {
            const ssize_t n = read(g_watchFd, buf, sizeof(buf));
            if (n <= (ssize_t)sizeof(struct inotify_event))
                break;

            for (ssize_t off = 0; off + (ssize_t)sizeof(struct inotify_event) <= n;) {
                const auto* ev = reinterpret_cast<const struct inotify_event*>(&buf[off]);
                if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)) && ev->len > 0 &&
                    g_configPath.ends_with(ev->name))
                    dirty = true;
                off += (ssize_t)sizeof(struct inotify_event) + ev->len;
            }
        }

        if (dirty)
            reloadScheme();
    }

    // event loop waiters are one-shot: re-arm after every wakeup.
    static void armWatch() {
        if (g_watchFd < 0 || !g_pEventLoopManager)
            return;

        g_pEventLoopManager->doOnReadable(CFileDescriptor(dup(g_watchFd)), [] {
            drainWatch();
            armWatch();
        });
    }

    static void setupWatch() {
        const auto dir = std::filesystem::path(g_configPath).parent_path().string();

        g_watchFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (g_watchFd < 0)
            return;

        if (inotify_add_watch(g_watchFd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0) {
            close(g_watchFd);
            g_watchFd = -1;
            return;
        }

        armWatch();
    }

    static std::string writeSchemeFile(const char* name, const char* content) {
        const char* runtime = getenv("XDG_RUNTIME_DIR");
        // unique per process: two compositor instances sharing XDG_RUNTIME_DIR
        // would otherwise race on the same file mid-read
        const auto  path = std::filesystem::path(runtime ? runtime : "/tmp") /
            (std::string(name) + "." + std::to_string(getpid()));

        std::ofstream out(path, std::ios::binary);
        // explicit length: content is a char* and would stop at an embedded NUL
        out.write(content, std::strlen(content));
        out.close();

        return path.string();
    }

    static std::string userConfigPath() {
        // HYPRSCHEME_CONFIG overrides the default location, mirroring how
        // HYPRLAND_CONFIG overrides the compositor's config discovery
        // (Jeremy::getMainConfigPath returns the env path verbatim, no
        // canonicalization)
        if (const char* overridePath = getenv("HYPRSCHEME_CONFIG"); overridePath && *overridePath)
            return overridePath;

        const char* cfg = getenv("XDG_CONFIG_HOME");
        std::string base;
        if (cfg && *cfg)
            base = cfg;
        else if (const char* home = getenv("HOME"); home && *home)
            base = std::string(home) + "/.config";
        else
            return {};

        return base + "/hypr/hyprland.scm";
    }

    // reimplements Compositor.cpp's handleUnrecoverableSignal using the
    // exported CrashReporter::createAndSaveCrash
    static void schemeCrashHandler(int sig) {
        signal(SIGABRT, SIG_DFL);
        signal(SIGSEGV, SIG_DFL);
        signal(SIGALRM, [](int) {
            const char* m = "\nCrashReporter exceeded timeout, forcefully exiting\n";
            [[maybe_unused]] auto w = write(2, m, strlen(m));
            abort();
        });
        alarm(15);
        CrashReporter::createAndSaveCrash(sig);
        abort();
    }

    static SP<IPC::Socket1::SCommand> g_schemeIpcCommand;
    static Hyprutils::Signal::CHyprSignalListener g_ipcReadyListener;

    // hyprctl scheme '<forms>' — evaluate scheme in the compositor. The
    // registration is deferred to the ready event when the plugin loads
    // during the EARLY config load (before STAGE_LATE creates Socket1) —
    // a silent `if (sock())` skip here once cost a whole debugging day.
    static void registerIpc() {
        if (g_schemeIpcCommand)
            return;
        if (!g_pEventLoopManager || !IPC::Socket1::sock()) {
            // a dedicated static, NOT g_lifecycleListeners: the vector
            // reallocates on growth, destroying RAII listeners mid-flight
            if (!g_ipcReadyListener)
                g_ipcReadyListener = Event::bus()->m_events.ready.listen([] { registerIpc(); });
            return;
        }
        g_schemeIpcCommand = IPC::Socket1::sock()->registerCommand(IPC::Socket1::SCommand{
            .name    = "scheme",
            .match   = IPC::Socket1::COMMAND_MATCH_PREFIX,
            .handler = [](const IPC::Socket1::SRequest& req) {
                auto code = req.command.substr(req.command.find_first_of(' ') + 1);
                watchdogEnter("eval");
                const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--eval")), Sstring_utf8(code.c_str(), code.size()));
                watchdogExit();
                std::string out;
                if (Sstringp(r)) {
                    for (iptr i = 0; i < Sstring_length(r); ++i)
                        out += (char)Sstring_ref(r, i);
                }
                return IPC::Socket1::SResponse(out);
            }});
        LOG(Log::INFO, "[scheme] ipc command registered");
    }

    // teardown for plugin unload: everything pointing into this .so must be
    // unregistered before hyprpm dlcloses it
    static Hyprutils::Signal::CHyprSignalListener g_reloadListener;

    void shutdown() {
        if (g_schemeIpcCommand) {
            IPC::Socket1::sock()->unregisterCommand(g_schemeIpcCommand);
            g_schemeIpcCommand.reset();
        }
        g_ipcReadyListener.reset();
        g_reloadListener.reset();
        g_lifecycleListeners.clear();
        g_eventConnections.clear();   // user handlers point into this .so
        Layouts::clear();
        // remove our binds before the .so unmaps (their callbacks point
        // here): ours are the ones tagged "scheme:" in their argument.
        // Collect first, then remove — erasing invalidates the registry span.
        if (Keybinds::mgr()) {
            std::vector<Keybinds::PBind> ours;
            for (const auto& b : Keybinds::mgr()->registry().binds())
                if (b && b->metadata().argument.rfind("scheme:", 0) == 0)
                    ours.push_back(b);
            for (const auto& b : ours)
                Keybinds::mgr()->removeBind(b);
        }
        // the interpreter stays alive: Chez does not survive a teardown +
        // re-init inside the compositor (the second Sbuild_heap hangs), so
        // a re-load of the plugin re-attaches to the live interpreter
        g_up = false;
    }

    // attachInterp: register foreign symbols and load the prelude + API
    // bootstrap. Called on first init and on every soft reload (so a
    // re-loaded plugin picks up its new scheme API against the live
    // interpreter). Returns false when the bootstrap failed.
    static bool attachInterp() {
        Sregister_symbol("hl-scheme-bind", (void*)hlSchemeBind);
        Sregister_symbol("hl-scheme-exec", (void*)hlSchemeExec);
        Sregister_symbol("hl-scheme-timer", (void*)hlSchemeTimer);
        Sregister_symbol("hl-scheme-active-title", (void*)hlSchemeActiveTitle);
        Sregister_symbol("hl-scheme-workspace-names", (void*)hlSchemeWorkspaceNames);
        Sregister_symbol("hl-scheme-submap-listen", (void*)hlSchemeSubmapListen);
        Sregister_symbol("hl-scheme-active-window-id", (void*)hlSchemeActiveWindowId);
        Sregister_symbol("hl-handle-free", (void*)hlHandleFree);
        Sregister_symbol("hl-scheme-window-ids", (void*)hlSchemeWindowIds);
        Sregister_symbol("hl-scheme-window-title", (void*)hlSchemeWindowTitle);
        Sregister_symbol("hl-scheme-window-alive", (void*)hlSchemeWindowAlive);
        Sregister_symbol("hl-scheme-window-close", (void*)hlSchemeWindowClose);
        Sregister_symbol("hl-scheme-window-class", (void*)hlSchemeWindowClass);
        Sregister_symbol("hl-scheme-window-workspace-id", (void*)hlSchemeWindowWorkspaceId);
        Sregister_symbol("hl-scheme-window-monitor-id", (void*)hlSchemeWindowMonitorId);
        Sregister_symbol("hl-scheme-window-floating", (void*)hlSchemeWindowFloating);
        Sregister_symbol("hl-scheme-window-size", (void*)hlSchemeWindowSize);
        Sregister_symbol("hl-scheme-window-pid", (void*)hlSchemeWindowPid);
        Sregister_symbol("hl-scheme-window-focus", (void*)hlSchemeWindowFocus);
        Sregister_symbol("hl-scheme-window-float", (void*)hlSchemeWindowFloat);
        Sregister_symbol("hl-scheme-window-move-to-workspace", (void*)hlSchemeWindowMoveToWorkspace);
        Sregister_symbol("hl-scheme-monitor-names", (void*)hlSchemeMonitorNames);
        Sregister_symbol("hl-scheme-window-event-listen", (void*)hlSchemeWindowEventListen);
        Sregister_symbol("hl-scheme-window-minimize-listen", (void*)hlSchemeWindowMinimizeListen);
        Sregister_symbol("hl-scheme-lifecycle-listen", (void*)hlSchemeLifecycleListen);
        Sregister_symbol("hl-scheme-config-reloaded-listen", (void*)hlSchemeConfigReloadedListen);
        Sregister_symbol("hl-scheme-config-unload-listen", (void*)hlSchemeConfigUnloadListen);
        Sregister_symbol("hl-scheme-config-props-refreshed-listen", (void*)hlSchemePropsRefreshedListen);
        Sregister_symbol("hl-scheme-window-destroy-listen", (void*)hlSchemeWindowDestroyListen);
        Sregister_symbol("hl-scheme-layer-listen", (void*)hlSchemeLayerListen);
        Sregister_symbol("hl-scheme-screenshare-listen", (void*)hlSchemeScreenshareListen);
        Sregister_symbol("hl-scheme-keyboard-key-listen", (void*)hlSchemeKeyboardKeyListen);
        Sregister_symbol("hl-scheme-unbind-rec", (void*)hlSchemeUnbindRec);
        Sregister_symbol("hl-scheme-unbind-key", (void*)hlSchemeUnbindKey);
        Sregister_symbol("hl-layers", (void*)hlLayers);
        Sregister_symbol("hl-layer-alive", (void*)hlSchemeLayerAlive);
        Sregister_symbol("hl-layer-same", (void*)hlSchemeLayerSame);
        Sregister_symbol("hl-layer-address", (void*)hlSchemeLayerAddress);
        Sregister_symbol("hl-layer-pid", (void*)hlSchemeLayerPid);
        Sregister_symbol("hl-layer-monitor", (void*)hlSchemeLayerMonitorId);
        Sregister_symbol("hl-layer-namespace", (void*)hlSchemeLayerNamespace);
        Sregister_symbol("hl-layer-level", (void*)hlSchemeLayerLevel);
        Sregister_symbol("hl-layer-mapped", (void*)hlSchemeLayerMapped);
        Sregister_symbol("hl-layer-kb-interactivity", (void*)hlSchemeLayerKbInteractivity);
        Sregister_symbol("hl-layer-above-fs", (void*)hlSchemeLayerAboveFullscreen);
        Sregister_symbol("hl-layer-position", (void*)hlSchemeLayerPosition);
        Sregister_symbol("hl-layer-size", (void*)hlSchemeLayerSize);
        Sregister_symbol("hl-scheme-event-cancel", (void*)hlSchemeEventCancel);
        Sregister_symbol("hl-scheme-event-active", (void*)hlSchemeEventActive);
        Sregister_symbol("hl-scheme-window-same", (void*)hlSchemeWindowSame);
        Sregister_symbol("hl-scheme-current-submap", (void*)hlSchemeCurrentSubmap);
        Sregister_symbol("hl-scheme-cursor-pos", (void*)hlSchemeCursorPos);
        Sregister_symbol("hl-scheme-workspace-active-listen", (void*)hlSchemeWorkspaceActiveListen);
        Sregister_symbol("hl-scheme-workspace-event-listen", (void*)hlSchemeWorkspaceListen);
        Sregister_symbol("hl-scheme-monitor-event-listen", (void*)hlSchemeMonitorListen);
        Sregister_symbol("hl-scheme-workspace-change-id", (void*)hlWorkspaceChangeId);
        Sregister_symbol("hl-scheme-window-fullscreen-toggle", (void*)hlSchemeWindowFullscreenToggle);
        Sregister_symbol("hl-scheme-window-fullscreen-set", (void*)hlSchemeWindowFullscreenSet);
        Sregister_symbol("hl-scheme-window-fullscreen-mode", (void*)hlSchemeWindowFullscreenMode);
        Sregister_symbol("hl-scheme-focus-workspace", (void*)hlSchemeFocusWorkspace);
        Sregister_symbol("hl-scheme-window-float-act", (void*)hlSchemeWindowFloatAct);
        Sregister_symbol("hl-scheme-focus-direction", (void*)hlSchemeFocusDirection);
        Sregister_symbol("hl-scheme-focus-monitor", (void*)hlSchemeFocusMonitor);
        Sregister_symbol("hl-scheme-focus-last", (void*)hlSchemeFocusLast);
        Sregister_symbol("hl-scheme-focus-urgent", (void*)hlSchemeFocusUrgent);
        Sregister_symbol("hl-scheme-window-move-direction", (void*)hlSchemeWindowMoveDirection);
        Sregister_symbol("hl-scheme-window-swap-direction", (void*)hlSchemeWindowSwapDirection);
        Sregister_symbol("hl-scheme-window-swap-next", (void*)hlSchemeWindowSwapNext);
        Sregister_symbol("hl-scheme-window-swap-with", (void*)hlSchemeWindowSwapWith);
        Sregister_symbol("hl-scheme-window-cycle", (void*)hlSchemeWindowCycle);
        Sregister_symbol("hl-scheme-window-center", (void*)hlSchemeWindowCenter);
        Sregister_symbol("hl-scheme-window-resize-px", (void*)hlSchemeWindowResizePx);
        Sregister_symbol("hl-scheme-window-move-px", (void*)hlSchemeWindowMovePx);
        Sregister_symbol("hl-scheme-window-pin-act", (void*)hlSchemeWindowPinAct);
        Sregister_symbol("hl-scheme-window-pseudo", (void*)hlSchemeWindowPseudo);
        Sregister_symbol("hl-scheme-window-kill", (void*)hlSchemeWindowKill);
        Sregister_symbol("hl-scheme-window-signal", (void*)hlSchemeWindowSignal);
        Sregister_symbol("hl-scheme-window-zorder", (void*)hlSchemeWindowZOrder);
        Sregister_symbol("hl-scheme-window-set-prop", (void*)hlSchemeWindowSetProp);
        Sregister_symbol("hl-scheme-window-tag", (void*)hlSchemeWindowTag);
        Sregister_symbol("hl-scheme-window-clear-tags", (void*)hlSchemeWindowClearTags);
        Sregister_symbol("hl-scheme-toggle-swallow", (void*)hlSchemeToggleSwallow);
        Sregister_symbol("hl-scheme-workspace-groups", (void*)hlSchemeWorkspaceGroups);
        Sregister_symbol("hl-scheme-group-alive", (void*)hlSchemeGroupAlive);
        Sregister_symbol("hl-scheme-group-same", (void*)hlSchemeGroupSame);
        Sregister_symbol("hl-scheme-group-members", (void*)hlSchemeGroupMembers);
        Sregister_symbol("hl-scheme-group-current", (void*)hlSchemeGroupCurrent);
        Sregister_symbol("hl-scheme-group-current-idx", (void*)hlSchemeGroupCurrentIdx);
        Sregister_symbol("hl-scheme-group-size", (void*)hlSchemeGroupSize);
        Sregister_symbol("hl-scheme-group-locked", (void*)hlSchemeGroupLocked);
        Sregister_symbol("hl-scheme-group-denied", (void*)hlSchemeGroupDenied);
        Sregister_symbol("hl-scheme-group-add", (void*)hlSchemeGroupAdd);
        Sregister_symbol("hl-scheme-group-remove", (void*)hlSchemeGroupRemove);
        Sregister_symbol("hl-scheme-group-toggle", (void*)hlSchemeGroupToggle);
        Sregister_symbol("hl-scheme-group-set", (void*)hlSchemeGroupSet);
        Sregister_symbol("hl-scheme-monitor-set-special", (void*)hlSchemeMonitorSetSpecial);
        Sregister_symbol("hl-scheme-group-cycle", (void*)hlSchemeGroupCycle);
        Sregister_symbol("hl-scheme-group-index", (void*)hlSchemeGroupIndex);
        Sregister_symbol("hl-scheme-group-move-window", (void*)hlSchemeGroupMoveWindow);
        Sregister_symbol("hl-scheme-group-lock", (void*)hlSchemeGroupLock);
        Sregister_symbol("hl-scheme-group-lock-active", (void*)hlSchemeGroupLockActive);
        Sregister_symbol("hl-scheme-window-into-group", (void*)hlSchemeWindowIntoGroup);
        Sregister_symbol("hl-scheme-window-out-of-group", (void*)hlSchemeWindowOutOfGroup);
        Sregister_symbol("hl-scheme-window-into-or-create-group", (void*)hlSchemeWindowIntoOrCreateGroup);
        Sregister_symbol("hl-scheme-window-deny-from-group", (void*)hlSchemeWindowDenyFromGroup);
        Sregister_symbol("hl-scheme-workspace-rename", (void*)hlSchemeWorkspaceRename);
        Sregister_symbol("hl-scheme-workspace-move-monitor", (void*)hlSchemeWorkspaceMoveMonitor);
        Sregister_symbol("hl-scheme-workspace-toggle-special", (void*)hlSchemeWorkspaceToggleSpecial);
        Sregister_symbol("hl-scheme-workspace-swap-monitors", (void*)hlSchemeWorkspaceSwapMonitors);
        Sregister_symbol("hl-scheme-cursor-move", (void*)hlSchemeCursorMove);
        Sregister_symbol("hl-scheme-cursor-corner", (void*)hlSchemeCursorCorner);
        Sregister_symbol("hl-scheme-exit", (void*)hlSchemeExit);
        Sregister_symbol("hl-scheme-reload-config", (void*)hlSchemeReloadConfig);
        Sregister_symbol("hl-scheme-force-renderer-reload", (void*)hlSchemeForceRendererReload);
        Sregister_symbol("hl-scheme-dpms", (void*)hlSchemeDpms);
        Sregister_symbol("hl-scheme-force-idle", (void*)hlSchemeForceIdle);
        Sregister_symbol("hl-scheme-global", (void*)hlSchemeGlobal);
        Sregister_symbol("hl-scheme-event", (void*)hlSchemeEvent);
        Sregister_symbol("hl-scheme-pass", (void*)hlSchemePass);
        Sregister_symbol("hl-scheme-send-shortcut", (void*)hlSchemeSendShortcut);
        Sregister_symbol("hl-scheme-send-key-state", (void*)hlSchemeSendKeyState);
        Sregister_symbol("hl-scheme-mouse", (void*)hlSchemeMouse);
        Sregister_symbol("hl-scheme-release-input-capture", (void*)hlSchemeReleaseInputCapture);
        Sregister_symbol("hl-scheme-window-fullscreen-state", (void*)hlSchemeWindowFullscreenState);
        Sregister_symbol("hl-scheme-layout-message", (void*)hlSchemeLayoutMessage);
        Sregister_symbol("hl-config-begin", (void*)hlConfigBegin);
        Sregister_symbol("hl-config-push-int", (void*)hlConfigPushInt);
        Sregister_symbol("hl-config-push-num", (void*)hlConfigPushNum);
        Sregister_symbol("hl-config-push-bool", (void*)hlConfigPushBool);
        Sregister_symbol("hl-config-push-str", (void*)hlConfigPushStr);
        Sregister_symbol("hl-config-tbl-open", (void*)hlConfigTblOpen);
        Sregister_symbol("hl-config-tbl-key", (void*)hlConfigTblKey);
        Sregister_symbol("hl-config-tbl-set-hash", (void*)hlConfigTblSetHash);
        Sregister_symbol("hl-config-tbl-seti", (void*)hlConfigTblSeti);
        Sregister_symbol("hl-config-set", (void*)hlConfigSet);
        Sregister_symbol("hl-config-last-error", (void*)hlConfigLastError);
        Sregister_symbol("hl-config-get", (void*)hlConfigGet);
        Sregister_symbol("hl-scheme-device-add", (void*)hlSchemeDeviceAdd);
        Sregister_symbol("hl-exec-with-rule", (void*)hlSchemeExecRule);
        Sregister_symbol("hl-monitor-begin", (void*)hlMonitorBegin);
        Sregister_symbol("hl-monitor-field-str", (void*)hlMonitorFieldStr);
        Sregister_symbol("hl-monitor-field-num", (void*)hlMonitorFieldNum);
        Sregister_symbol("hl-monitor-field-gap", (void*)hlMonitorFieldGap);
        Sregister_symbol("hl-monitor-field-bool", (void*)hlMonitorFieldBool);
        Sregister_symbol("hl-monitor-commit", (void*)hlMonitorCommit);
        Sregister_symbol("hl-curve-add", (void*)hlCurveAdd);
        Sregister_symbol("hl-animation-set", (void*)hlAnimationSet);
        Sregister_symbol("hl-permission-add", (void*)hlPermissionAdd);
        Sregister_symbol("hl-window-rule-begin", (void*)hlWindowRuleBegin);
        Sregister_symbol("hl-layer-rule-begin", (void*)hlLayerRuleBegin);
        Sregister_symbol("hl-rule-match", (void*)hlRuleMatch);
        Sregister_symbol("hl-window-rule-effect", (void*)hlWindowRuleEffect);
        Sregister_symbol("hl-layer-rule-effect", (void*)hlLayerRuleEffect);
        Sregister_symbol("hl-window-rule-commit", (void*)hlWindowRuleCommit);
        Sregister_symbol("hl-layer-rule-commit", (void*)hlLayerRuleCommit);
        Sregister_symbol("hl-rule-set-enabled", (void*)hlRuleSetEnabled);
        Sregister_symbol("hl-rule-enabled", (void*)hlRuleEnabled);
        Sregister_symbol("hl-workspace-rule-begin", (void*)hlWorkspaceRuleBegin);
        Sregister_symbol("hl-workspace-rule-str", (void*)hlWorkspaceRuleStr);
        Sregister_symbol("hl-workspace-rule-num", (void*)hlWorkspaceRuleNum);
        Sregister_symbol("hl-workspace-rule-bool", (void*)hlWorkspaceRuleBool);
        Sregister_symbol("hl-workspace-rule-gap", (void*)hlWorkspaceRuleGap);
        Sregister_symbol("hl-workspace-rule-layout-opt", (void*)hlWorkspaceRuleLayoutOpt);
        Sregister_symbol("hl-workspace-rule-commit", (void*)hlWorkspaceRuleCommit);
        Sregister_symbol("hl-window-from", (void*)hlWindowFrom);
        Sregister_symbol("hl-urgent-window", (void*)hlUrgentWindow);
        Sregister_symbol("hl-last-window", (void*)hlLastWindow);
        Sregister_symbol("hl-monitor-from", (void*)hlMonitorFrom);
        Sregister_symbol("hl-monitor-at", (void*)hlMonitorAt);
        Sregister_symbol("hl-monitor-at-cursor", (void*)hlMonitorAtCursor);
        Sregister_symbol("hl-active-monitor", (void*)hlActiveMonitor);
        Sregister_symbol("hl-active-workspace", (void*)hlActiveWorkspace);
        Sregister_symbol("hl-active-special-workspace", (void*)hlActiveSpecialWorkspace);
        Sregister_symbol("hl-last-workspace", (void*)hlLastWorkspace);
        Sregister_symbol("hl-workspace-name", (void*)hlWorkspaceName);
        Sregister_symbol("hl-workspace-addressable-name", (void*)hlWorkspaceAddressableName);
        Sregister_symbol("hl-workspace-number", (void*)hlWorkspaceNumber);
        Sregister_symbol("hl-workspace-monitor", (void*)hlWorkspaceMonitor);
        Sregister_symbol("hl-workspace-special", (void*)hlWorkspaceSpecial);
        Sregister_symbol("hl-workspace-active", (void*)hlWorkspaceActive);
        Sregister_symbol("hl-workspace-visible", (void*)hlWorkspaceVisible);
        Sregister_symbol("hl-workspace-empty", (void*)hlWorkspaceEmpty);
        Sregister_symbol("hl-workspace-persistent", (void*)hlWorkspacePersistent);
        Sregister_symbol("hl-workspace-has-urgent", (void*)hlWorkspaceHasUrgent);
        Sregister_symbol("hl-workspace-has-fullscreen", (void*)hlWorkspaceHasFullscreen);
        Sregister_symbol("hl-workspace-fullscreen-mode", (void*)hlWorkspaceFullscreenMode);
        Sregister_symbol("hl-workspace-fullscreen-window", (void*)hlWorkspaceFullscreenWindow);
        Sregister_symbol("hl-workspace-last-window", (void*)hlWorkspaceLastWindow);
        Sregister_symbol("hl-workspace-window-count", (void*)hlWorkspaceWindowCount);
        Sregister_symbol("hl-workspace-group-count", (void*)hlWorkspaceGroupCount);
        Sregister_symbol("hl-workspace-tiled-layout", (void*)hlWorkspaceTiledLayout);
        Sregister_symbol("hl-workspace-alive", (void*)hlWorkspaceAlive);
        Sregister_symbol("hl-workspace-same", (void*)hlWorkspaceSame);
        Sregister_symbol("hl-workspace-selector", (void*)hlWorkspaceSelector);
        Sregister_symbol("hl-workspace-windows", (void*)hlWorkspaceWindows);
        Sregister_symbol("hl-is-key-down", (void*)hlIsKeyDown);
        Sregister_symbol("hl-loaded-plugins", (void*)hlLoadedPlugins);
        Sregister_symbol("hl-version", (void*)hlVersion);
        Sregister_symbol("hl-windows-from", (void*)hlWindowsFrom);
        Sregister_symbol("hl-monitor-name", (void*)hlMonitorName);
        Sregister_symbol("hl-monitor-description", (void*)hlMonitorDescription);
        Sregister_symbol("hl-monitor-number", (void*)hlMonitorNumber);
        Sregister_symbol("hl-monitor-enabled", (void*)hlMonitorEnabled);
        Sregister_symbol("hl-monitor-focused", (void*)hlMonitorFocused);
        Sregister_symbol("hl-monitor-x", (void*)hlMonitorX);
        Sregister_symbol("hl-monitor-y", (void*)hlMonitorY);
        Sregister_symbol("hl-monitor-width", (void*)hlMonitorWidth);
        Sregister_symbol("hl-monitor-height", (void*)hlMonitorHeight);
        Sregister_symbol("hl-monitor-scale", (void*)hlMonitorScale);
        Sregister_symbol("hl-monitor-transform", (void*)hlMonitorTransform);
        Sregister_symbol("hl-monitor-refresh-rate", (void*)hlMonitorRefreshRate);
        Sregister_symbol("hl-monitor-mode", (void*)hlMonitorMode);
        Sregister_symbol("hl-monitor-dpms", (void*)hlMonitorDpms);
        Sregister_symbol("hl-monitor-vrr", (void*)hlMonitorVrr);
        Sregister_symbol("hl-monitor-10bit", (void*)hlMonitor10bit);
        Sregister_symbol("hl-monitor-reserved", (void*)hlMonitorReserved);
        Sregister_symbol("hl-monitor-serial", (void*)hlMonitorSerial);
        Sregister_symbol("hl-monitor-physical-size", (void*)hlMonitorPhysicalSize);
        Sregister_symbol("hl-monitor-mirrors", (void*)hlMonitorMirrors);
        Sregister_symbol("hl-monitor-available-modes", (void*)hlMonitorAvailableModes);
        Sregister_symbol("hl-monitor-hardware-details", (void*)hlMonitorHardwareDetails);
        Sregister_symbol("hl-monitor-mirror-of", (void*)hlMonitorMirrorOf);
        Sregister_symbol("hl-monitor-active-workspace", (void*)hlMonitorActiveWorkspace);
        Sregister_symbol("hl-monitor-active-special-workspace", (void*)hlMonitorActiveSpecialWorkspace);
        Sregister_symbol("hl-monitor-alive", (void*)hlMonitorAlive);
        Sregister_symbol("hl-monitor-same", (void*)hlMonitorSame);
        Sregister_symbol("hl-monitor-selector", (void*)hlMonitorSelector);
        Sregister_symbol("hl-window-fullscreen-handler", (void*)hlWindowFullscreenHandler);
        Sregister_symbol("hl-notify!", (void*)hlNotify);
        Sregister_symbol("hl-notification-add", (void*)hlNotificationAdd);
        Sregister_symbol("hl-notification-list", (void*)hlNotificationList);
        Sregister_symbol("hl-notification-text", (void*)hlNotificationText);
        Sregister_symbol("hl-notification-timeout", (void*)hlNotificationTimeout);
        Sregister_symbol("hl-notification-color", (void*)hlNotificationColor);
        Sregister_symbol("hl-notification-icon", (void*)hlNotificationIcon);
        Sregister_symbol("hl-notification-font-size", (void*)hlNotificationFontSize);
        Sregister_symbol("hl-notification-elapsed", (void*)hlNotificationElapsed);
        Sregister_symbol("hl-notification-age", (void*)hlNotificationAge);
        Sregister_symbol("hl-notification-alive", (void*)hlNotificationAlive);
        Sregister_symbol("hl-notification-same", (void*)hlNotificationSame);
        Sregister_symbol("hl-notification-text-set", (void*)hlNotificationTextSet);
        Sregister_symbol("hl-notification-timeout-set", (void*)hlNotificationTimeoutSet);
        Sregister_symbol("hl-notification-color-set", (void*)hlNotificationColorSet);
        Sregister_symbol("hl-notification-icon-set", (void*)hlNotificationIconSet);
        Sregister_symbol("hl-notification-font-size-set", (void*)hlNotificationFontSizeSet);
        Sregister_symbol("hl-notification-paused-set", (void*)hlNotificationPausedSet);
        Sregister_symbol("hl-notification-paused-q", (void*)hlNotificationPausedQ);
        Sregister_symbol("hl-notification-dismiss", (void*)hlNotificationDismiss);
        Sregister_symbol("hl-timer-set-enabled", (void*)hlTimerSetEnabled);
        Sregister_symbol("hl-timer-enabled", (void*)hlTimerEnabled);
        Sregister_symbol("hl-timer-set-timeout", (void*)hlTimerSetTimeout);
        Sregister_symbol("hl-timer-cancel", (void*)hlTimerCancel);
        Sregister_symbol("hl-exec!", (void*)hlSchemeExecRaw);
        Sregister_symbol("hl-exec-shell-with-rules!", (void*)hlSchemeExecWithRules);
        Sregister_symbol("hl-scheme-gesture", (void*)hlSchemeGesture);
        Sregister_symbol("hl-scheme-gesture-maker-workspace-swipe", (void*)hlSchemeGestureMakerWorkspaceSwipe);
        Sregister_symbol("hl-scheme-gesture-maker-move", (void*)hlSchemeGestureMakerMove);
        Sregister_symbol("hl-scheme-gesture-maker-resize", (void*)hlSchemeGestureMakerResize);
        Sregister_symbol("hl-scheme-gesture-maker-close", (void*)hlSchemeGestureMakerClose);
        Sregister_symbol("hl-scheme-gesture-maker-scroll-move", (void*)hlSchemeGestureMakerScrollMove);
        Sregister_symbol("hl-scheme-gesture-maker-float", (void*)hlSchemeGestureMakerFloat);
        Sregister_symbol("hl-scheme-gesture-maker-fullscreen", (void*)hlSchemeGestureMakerFullscreen);
        Sregister_symbol("hl-scheme-gesture-maker-special", (void*)hlSchemeGestureMakerSpecial);
        Sregister_symbol("hl-scheme-gesture-maker-cursor-zoom", (void*)hlSchemeGestureMakerCursorZoom);
        Sregister_symbol("hl-scheme-gesture-maker-custom", (void*)hlSchemeGestureMakerCustom);
        Sregister_symbol("hl-scheme-gesture-remove", (void*)hlSchemeGestureRemove);
        Sregister_symbol("hl-scheme-window-hidden", (void*)hlSchemeWindowHidden);

        // window read-side fields (LuaWindow parity)
        Sregister_symbol("hl-scheme-window-address", (void*)hlSchemeWindowAddress);
        Sregister_symbol("hl-scheme-window-mapped", (void*)hlSchemeWindowMapped);
        Sregister_symbol("hl-scheme-window-visible", (void*)hlSchemeWindowVisible);
        Sregister_symbol("hl-scheme-window-accepts-input", (void*)hlSchemeWindowAcceptsInput);
        Sregister_symbol("hl-scheme-window-position", (void*)hlSchemeWindowPosition);
        Sregister_symbol("hl-scheme-window-pin-fullscreened", (void*)hlSchemeWindowPinFullscreened);
        Sregister_symbol("hl-scheme-window-allowed-over-fullscreen", (void*)hlSchemeWindowAllowedOverFullscreen);
        Sregister_symbol("hl-scheme-window-tearing-hint", (void*)hlSchemeWindowTearingHint);
        Sregister_symbol("hl-scheme-window-inhibiting-idle", (void*)hlSchemeWindowInhibitingIdle);
        Sregister_symbol("hl-scheme-window-focus-history-id", (void*)hlSchemeWindowFocusHistoryId);
        Sregister_symbol("hl-scheme-window-content-type", (void*)hlSchemeWindowContentType);
        Sregister_symbol("hl-scheme-window-stable-id", (void*)hlSchemeWindowStableId);
        Sregister_symbol("hl-scheme-window-tags", (void*)hlSchemeWindowTags);
        Sregister_symbol("hl-scheme-window-swallowing-id", (void*)hlSchemeWindowSwallowingId);
        Sregister_symbol("hl-scheme-window-xdg-tag", (void*)hlSchemeWindowXdgTag);
        Sregister_symbol("hl-scheme-window-xdg-description", (void*)hlSchemeWindowXdgDescription);
        Sregister_symbol("hl-scheme-window-layout", (void*)hlSchemeWindowLayout);
        Sregister_symbol("hl-scheme-window-pinned", (void*)hlSchemeWindowPinned);
        Sregister_symbol("hl-scheme-window-pseudo-query", (void*)hlSchemeWindowPseudoQuery);
        Sregister_symbol("hl-scheme-window-maximized-query", (void*)hlSchemeWindowMaximizedQuery);
        Sregister_symbol("hl-scheme-window-in-group", (void*)hlSchemeWindowInGroup);
        Sregister_symbol("hl-scheme-window-group-denied", (void*)hlSchemeWindowGroupDenied);
        Sregister_symbol("hl-scheme-window-group-locked", (void*)hlSchemeWindowGroupLocked);
        Sregister_symbol("hl-scheme-groups-locked", (void*)hlSchemeGroupsLocked);
        Sregister_symbol("hl-scheme-window-group-lock", (void*)hlSchemeWindowGroupLock);
        Sregister_symbol("hl-scheme-window-prop", (void*)hlSchemeWindowPropGet);
        Sregister_symbol("hl-scheme-window-initial-class", (void*)hlSchemeWindowInitialClass);
        Sregister_symbol("hl-scheme-window-initial-title", (void*)hlSchemeWindowInitialTitle);
        Sregister_symbol("hl-scheme-window-x11", (void*)hlSchemeWindowX11);
        Sregister_symbol("hl-scheme-get-submap-ctx", (void*)+[]() -> ptr {
            std::vector<ptr> roots;
            ptr              name  = Sstring_utf8(g_regSubmap.c_str(), g_regSubmap.size());
            marshRoot(name, roots);
            ptr reset = Sstring_utf8(g_regSubmapReset.c_str(), g_regSubmapReset.size());
            marshRoot(reset, roots);
            ptr pair = Scons(name, reset);
            marshRoot(pair, roots);
            marshRelease(roots);
            return pair;   // (name . reset)
        });
        Sregister_symbol("hl-scheme-set-submap-ctx", (void*)+[](const char* name, const char* reset) {
            g_regSubmap      = name ? name : "";
            g_regSubmapReset = reset ? reset : "";
        });
        Sregister_symbol("hl-scheme-enter-submap", (void*)+[](const char* name) -> int {
            if (!g_up || !name)
                return -1;
            return Config::Actions::setSubmap(name) ? 0 : -1;
        });
        Layouts::registerSymbols();

        // phase 1: the prelude (verified plumbing, unguarded). foreign-procedure
        // resolves symbols at definition time, so symbols must exist before this.
        Scall1(Stop_level_value(Sstring_to_symbol("load")),
            Sstring(writeSchemeFile("hypr-scheme-prelude.scm", SCHEME_PRELUDE).c_str()));

        // phase 2: the API, loaded through the prelude's guarded hl--load
        const auto BOOT = writeSchemeFile("hypr-scheme-bootstrap.scm", SCHEME_BOOTSTRAP);
        Scall1(Stop_level_value(Sstring_to_symbol("hl--load")), Sstring(BOOT.c_str()));

        if (Stop_level_value(Sstring_to_symbol("hl--ready")) == Sfalse) {
            LOG(Log::ERR, "[scheme] bootstrap failed, scheme scripting disabled");
            return false;
        }
        g_up = true;
        startWatchdog();
        return true;
    }

    void init() {
        static bool done = false;
        static std::filesystem::file_time_type loadedTime;
        if (done) {
            // the interpreter lives in the pinned first mapping: a re-load
            // re-attaches the SAME code. If the binary changed on disk, say
            // so instead of silently ignoring the upgrade.
            {
                Dl_info self{};
                if (dladdr((void*)&init, &self) && self.dli_fname) {
                    std::error_code ec;
                    const auto t = std::filesystem::last_write_time(self.dli_fname, ec);
                    if (!ec && t != loadedTime)
                        LOG(Log::ERR, "[scheme] the plugin binary changed on disk since this session started; "
                                      "restart the compositor to load the new version (the running interpreter "
                                      "cannot be replaced in-place)");
                }
            }
            // soft reload: the interpreter is still alive; re-attach the
            // foreign symbols, the (possibly new) scheme API and the
            // plumbing that shutdown() removed
            if (!attachInterp())
                return;
            registerIpc();
            g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { reloadScheme(); });
            g_lifecycleListeners.emplace_back(Event::bus()->m_events.start.listen([] {
                g_startSeen = true;
                auto pending = std::move(g_pendingStart);
                g_pendingStart.clear();
                for (const auto& ref : pending)
                    fireScheme(ref.obj);
            }));
            reloadScheme();
            return;
        }
        done = true;

        g_configPath = userConfigPath();
        if (g_configPath.empty() || !std::filesystem::exists(g_configPath)) {
            LOG(Log::INFO, "[scheme] no scheme config found at {} (HYPRSCHEME_CONFIG {}), scheme scripting disabled",
                g_configPath.empty() ? "<unset>" : g_configPath, getenv("HYPRSCHEME_CONFIG") ? "override ignored: file missing" : "not set");
            return;
        }
        LOG(Log::INFO, "[scheme] config: {}", g_configPath);

        // pin ourselves: bump the dlopen refcount so the compositor's
        // dlclose on unload never unmaps the live interpreter
        {
            Dl_info self{};
            if (dladdr((void*)&init, &self) && self.dli_fname)
                dlopen(self.dli_fname, RTLD_NOW | RTLD_NOLOAD);
        }
        {
            Dl_info selft{};
            if (dladdr((void*)&init, &selft) && selft.dli_fname) {
                std::error_code ec;
                loadedTime = std::filesystem::last_write_time(selft.dli_fname, ec);
            }
        }
        Sscheme_init(nullptr);
        // boot files live next to the plugin (.so dir), with the install
        // prefix and the dev kit as fallbacks
        std::string bootDir;
        Dl_info info{};
        if (dladdr((void*)&init, &info) && info.dli_fname)
            bootDir = std::filesystem::path(info.dli_fname).parent_path().string();
        const std::string home = getenv("HOME") ? getenv("HOME") : "";
        const std::vector<std::string> bootDirs = {"/home/chris/GITE/chez-pic", bootDir, home + "/.local/lib/hyprscheme", "/usr/lib/hyprscheme"};
        const auto tryBoot = [&bootDirs](const char* name) {
            for (const auto& dir : bootDirs) {
                if (dir.empty())
                    continue;
                const auto p = dir + "/" + name;
                if (std::filesystem::exists(p))
                    return p;
            }
            return std::string(name);
        };
        Sregister_boot_file(tryBoot("petite.boot").c_str());
        Sregister_boot_file(tryBoot("scheme.boot").c_str());
        Sbuild_heap(nullptr, nullptr);

        if (!attachInterp())
            return;


        // Chez installs its own SIGSEGV/SIGABRT handlers during init,
        // displacing Hyprland's crash reporter. A plugin cannot reach
        // handleUnrecoverableSignal (static), so reimplement its body via
        // the exported CrashReporter::createAndSaveCrash.
        signal(SIGSEGV, schemeCrashHandler);
        signal(SIGABRT, schemeCrashHandler);

        // hyprctl scheme '<forms>' — evaluate scheme in the compositor.
        // direct registration is safe here: verified working (the deferred
        // doLater variant never fired its callback).
        registerIpc();
        // watch the file for edits (skipped during --verify: no event loop yet)
        if (g_pEventLoopManager)
            setupWatch();

        // the lua config load clears all binds; (re)load our file once it settles.
        // as a plugin we load AFTER the initial config load, so also load now.
        g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { reloadScheme(); });

        // lifecycle: dispatch start-notification handlers on the session's first
        // render frame (start fires exactly once, after the first preChecks)
        g_lifecycleListeners.emplace_back(Event::bus()->m_events.start.listen([] {
            g_startSeen = true;
            auto pending = std::move(g_pendingStart);
            g_pendingStart.clear();
            for (const auto& ref : pending)
                fireScheme(ref.obj);
        }));
        reloadScheme();
    }
}

