#include "SchemeManager.hpp"
#include "SchemeLayout.hpp"
#include "SchemeInternals.hpp"

#include <scheme.h>

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
#include <src/desktop/view/window/Window.hpp>
#include <src/desktop/view/window/WindowGroupMembership.hpp>
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

(define (hl--fire id)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                        (hl--guarded-run "callback" (cdr entry)))))
          (not (eq? result hl--wd-aborted))))))

;; bind-callback fire: #f from the thunk DECLINES the key (upstream's
;; ok=false auto-consuming protocol); errors and the watchdog also decline
;; (upstream maps Lua callback errors to success=false, ConfigManager.cpp:
;; 1418, so the two behave identically downstream). Anything else consumes.
;; fireSchemeBind maps the result onto SBindResult{.success}.
(define (hl--bind-fire id)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                        (hl--guarded-run "callback" (cdr entry)))))
          (and (not (eq? result hl--wd-aborted)) (not (eq? result #f)))))))

(define (hl--fire-str id arg)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                        (hl--guarded-run "handler" (lambda () ((cdr entry) arg))))))
          (not (eq? result hl--wd-aborted))))))

;; events carrying a boolean payload: the handler receives #t or #f
(define (hl--fire-bool id arg)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                        (hl--guarded-run "handler" (lambda () ((cdr entry) arg))))))
          (not (eq? result hl--wd-aborted))))))

;; events carrying a workspace handle: the payload crosses as a handle id
;; (or #f); the record constructor lives in the bootstrap
(define (hl--fire-ws id arg)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                        (hl--guarded-run "handler" (lambda () ((cdr entry) (and (not (eq? arg #f)) (make-hl-workspace arg))))))))
          (not (eq? result hl--wd-aborted))))))

;; events carrying a monitor handle
(define (hl--fire-mon id arg)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                        (hl--guarded-run "handler" (lambda () ((cdr entry) (and (not (eq? arg #f)) (make-hl-monitor arg))))))))
          (not (eq? result hl--wd-aborted))))))

;; events carrying two handles (workspace, monitor); #f crosses for an
;; absent one (e.g. no special workspace open)
(define (hl--fire-ws-mon id ws mon)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                        (hl--guarded-run "handler" (lambda () ((cdr entry)
                                                                 (and (not (eq? ws #f)) (make-hl-workspace ws))
                                                                 (and (not (eq? mon #f)) (make-hl-monitor mon))))))))
          (not (eq? result hl--wd-aborted))))))

;; defined here so event handlers receive real window records; the record
;; constructor lives in the bootstrap, loaded after this prelude
(define (hl--fire-win id win-id)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                        (hl--guarded-run "handler" (lambda () ((cdr entry) (make-hl-window win-id)))))))
          (not (eq? result hl--wd-aborted))))))

;; events carrying (window, bool) payloads: minimize state
(define (hl--fire-win-state id win-id state)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                        (hl--guarded-run "handler" (lambda () ((cdr entry) (make-hl-window win-id) state))))))
          (not (eq? result hl--wd-aborted))))))

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

;; layout callbacks: spec = "count\nW\nH\n<handle-id per target>"
;; (kept to 2 foreign args; Scall passes at most 3). the fn receives
;; (count W H windows) where windows[i] is the handle for box i (a handle
;; whose id 0 means "not a window" — every query on it returns #f).
;; fn returns list of (x y w h), or #f on error.
(define (hl--split-lines s)
  (let loop ((i 0) (start 0) (acc '()))
    (cond
      ((>= i (string-length s))
       (reverse (cons (substring s start i) acc)))
      ((char=? (string-ref s i) #\newline)
       (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
      (else (loop (+ i 1) start acc)))))

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

;; layout event dispatch. a layout provider is an alist of callbacks:
;;   ((recalculate . fn) (resize . fn) (window-open . fn) (window-close . fn))
;; recalculate/resize fn: (count W H windows [dx dy corner]) -> ((x y w h) ...)
;; window callbacks: (window) -> ignored. #f when absent or on error.
(define (hl--layout-event id event payload)
  (let* ((prov (assv id hl--binds))
         (cbs  (if prov (cdr prov) #f)))
    (if (not cbs)
        #f
        (guard (e (#t (hl--report e)))
          (cond
            ((equal? event "window-open")
             (let ((cb (assq 'window-open cbs)))
               (if cb ((cdr cb) (make-hl-window (string->number payload))) #f)))
            ((equal? event "window-close")
             (let ((cb (assq 'window-close cbs)))
               (if cb ((cdr cb) (make-hl-window (string->number payload))) #f)))
            ((equal? event "layout-msg")
             (let ((cb (assq 'layout-msg cbs)))
               (if (not cb)
                   ""
                   (let ((r ((cdr cb) payload)))
                     (cond ((not r) "rejected")
                           ((string? r) r)
                           (else ""))))))
            (else
             (let* ((parts (hl--split-lines payload))
                    (count (string->number (list-ref parts 0)))
                    (W     (string->number (list-ref parts 1)))
                    (H     (string->number (list-ref parts 2))))
               (if (equal? event "resize")
                   (let* ((dx     (string->number (list-ref parts 3)))
                          (dy     (string->number (list-ref parts 4)))
                          (corner (string->number (list-ref parts 5)))
                          (wins   (map make-hl-window (map string->number (list-tail parts 6))))
                          (cb     (or (assq 'resize cbs) (assq 'recalculate cbs))))
                     (if cb (apply (cdr cb) (list count W H wins dx dy corner)) #f))
                   (let ((wins (map make-hl-window (map string->number (list-tail parts 3))))
                         (cb   (assq 'recalculate cbs)))
                     (if cb ((cdr cb) count W H wins) #f))))))))))
)scm";

static constexpr const char* SCHEME_BOOTSTRAP = R"scm(
(define hl--binds '())

(define c-hl-bind (foreign-procedure "hl-scheme-bind" (scheme-object int string string) int))
(define c-hl-exec (foreign-procedure "hl-scheme-exec" (string) int))
(define c-hl-timer (foreign-procedure "hl-scheme-timer" (int int) int))
(define c-hl-active-title (foreign-procedure "hl-scheme-active-title" () scheme-object))
(define c-hl-workspace-names (foreign-procedure "hl-scheme-workspace-names" () scheme-object))
(define c-hl-submap-listen (foreign-procedure "hl-scheme-submap-listen" () int))
(define c-hl-active-window-id (foreign-procedure "hl-scheme-active-window-id" () int))
(define c-hl-window-ids (foreign-procedure "hl-scheme-window-ids" () scheme-object))
(define c-hl-window-title (foreign-procedure "hl-scheme-window-title" (int) scheme-object))
(define c-hl-window-alive (foreign-procedure "hl-scheme-window-alive" (int) int))
(define c-hl-window-close (foreign-procedure "hl-scheme-window-close" (int) int))
(define c-hl-window-class (foreign-procedure "hl-scheme-window-class" (int) scheme-object))
(define c-hl-window-workspace-id (foreign-procedure "hl-scheme-window-workspace-id" (int) scheme-object))
(define c-hl-window-monitor-id (foreign-procedure "hl-scheme-window-monitor-id" (int) scheme-object))
(define c-hl-window-floating (foreign-procedure "hl-scheme-window-floating" (int) int))
(define c-hl-window-size (foreign-procedure "hl-scheme-window-size" (int) scheme-object))
(define c-hl-window-pid (foreign-procedure "hl-scheme-window-pid" (int) int))
(define c-hl-window-focus (foreign-procedure "hl-scheme-window-focus" (int) int))
(define c-hl-window-float (foreign-procedure "hl-scheme-window-float" (int) int))
(define c-hl-window-move-to-workspace (foreign-procedure "hl-scheme-window-move-to-workspace" (int string) int))
(define c-hl-monitor-names (foreign-procedure "hl-scheme-monitor-names" () scheme-object))
(define c-hl-window-event-listen (foreign-procedure "hl-scheme-window-event-listen" (int) int))
(define c-hl-window-minimize-listen (foreign-procedure "hl-scheme-window-minimize-listen" () int))
(define c-hl-lifecycle-listen (foreign-procedure "hl-scheme-lifecycle-listen" (int) int))
(define c-hl-config-reloaded-listen (foreign-procedure "hl-scheme-config-reloaded-listen" () int))
(define c-hl-config-unload-listen (foreign-procedure "hl-scheme-config-unload-listen" () int))
(define c-hl-config-props-refreshed-listen (foreign-procedure "hl-scheme-config-props-refreshed-listen" () int))
(define c-hl-window-destroy-listen (foreign-procedure "hl-scheme-window-destroy-listen" () int))
(define c-hl-layer-listen (foreign-procedure "hl-scheme-layer-listen" (int) int))
(define c-hl-unbind (foreign-procedure "hl-scheme-unbind" (int) int))
(define c-hl-unbind-key (foreign-procedure "hl-scheme-unbind-key" (string) int))
(define c-hl-window-same (foreign-procedure "hl-scheme-window-same" (int int) int))
(define c-hl-current-submap (foreign-procedure "hl-scheme-current-submap" () scheme-object))
(define c-hl-cursor-pos (foreign-procedure "hl-scheme-cursor-pos" () scheme-object))
(define c-hl-workspace-active-listen (foreign-procedure "hl-scheme-workspace-active-listen" () int))
(define c-hl-monitor-event-listen (foreign-procedure "hl-scheme-monitor-event-listen" (int) int))
(define c-hl-workspace-event-listen (foreign-procedure "hl-scheme-workspace-event-listen" (int) int))
(define c-hl-workspace-change-id (foreign-procedure "hl-scheme-workspace-change-id" (string double) int))
(define c-hl-define-layout (foreign-procedure "hl-scheme-define-layout" (string) int))

;; pure-function layout; see SchemeLayout.hpp for the contract
;; spec is a single recalculate fn, or an alist of callbacks:
;;   ((recalculate . fn) (resize . fn) (window-open . fn) (window-close . fn))
;; resize fn: (count W H windows dx dy corner) -> boxes; when absent a resize
;; falls back to the recalculate fn. state: keep it in a closure around fn.
(define (hl-define-layout name spec)
  (let* ((prov (if (procedure? spec) (list (cons 'recalculate spec)) spec))
         (id   (c-hl-define-layout name)))
    (if (< id 0)
        (errorf 'hl-define-layout "layout ~a rejected, see compositor log" name)
        (hl--register id prov))))

(define c-hl-get-submap-ctx (foreign-procedure "hl-scheme-get-submap-ctx" () scheme-object))
(define c-hl-set-submap-ctx (foreign-procedure "hl-scheme-set-submap-ctx" (string string) void))
(define c-hl-enter-submap (foreign-procedure "hl-scheme-enter-submap" (string) int))

;; binds registered inside fn are scoped to the submap; the optional reset
;; names the submap returned to on exit. the submap exists only if fn
;; registers at least one bind.
(define (hl-submap name fn . reset)
  (let* ((ctx        (hl--split-lines (c-hl-get-submap-ctx)))
         (prev-name  (list-ref ctx 0))
         (prev-reset (list-ref ctx 1)))
    (dynamic-wind
      (lambda () (c-hl-set-submap-ctx name (if (null? reset) "" (car reset))))
      (lambda () (fn))
      (lambda () (c-hl-set-submap-ctx prev-name prev-reset)))))

;; switch the active submap ("" or "reset" returns to the default)
(define (hl-submap-activate! name)
  (= 0 (c-hl-enter-submap name)))

(define (hl-submap-exit!)
  (= 0 (c-hl-enter-submap "")))
(define c-hl-window-fullscreen-toggle (foreign-procedure "hl-scheme-window-fullscreen-toggle" (int int) int))
(define c-hl-window-fullscreen-set (foreign-procedure "hl-scheme-window-fullscreen-set" (int int) int))
(define c-hl-window-fullscreen-mode (foreign-procedure "hl-scheme-window-fullscreen-mode" (int) int))
(define c-hl-window-hidden (foreign-procedure "hl-scheme-window-hidden" (int) int))
(define c-hl-window-pinned (foreign-procedure "hl-scheme-window-pinned" (int) int))
(define c-hl-window-pseudo-query (foreign-procedure "hl-scheme-window-pseudo-query" (int) int))
(define c-hl-window-maximized-query (foreign-procedure "hl-scheme-window-maximized-query" (int) int))
(define c-hl-window-in-group (foreign-procedure "hl-scheme-window-in-group" (int) int))
(define c-hl-window-group-denied (foreign-procedure "hl-scheme-window-group-denied" (int) int))
(define c-hl-window-group-locked (foreign-procedure "hl-scheme-window-group-locked" (int) int))
(define c-hl-groups-locked (foreign-procedure "hl-scheme-groups-locked" () int))
(define c-hl-window-group-lock (foreign-procedure "hl-scheme-window-group-lock" (int int) int))
(define c-hl-window-prop (foreign-procedure "hl-scheme-window-prop" (int string) scheme-object))
(define c-hl-window-initial-class (foreign-procedure "hl-scheme-window-initial-class" (int) scheme-object))
(define c-hl-window-initial-title (foreign-procedure "hl-scheme-window-initial-title" (int) scheme-object))

;; ---- actions: the dispatcher surface (window id -1 = active window) --------
(define c-hl-focus-workspace (foreign-procedure "hl-scheme-focus-workspace" (string) int))
(define c-hl-focus-direction (foreign-procedure "hl-scheme-focus-direction" (string) int))
(define c-hl-focus-monitor (foreign-procedure "hl-scheme-focus-monitor" (string) int))
(define c-hl-focus-last (foreign-procedure "hl-scheme-focus-last" () int))
(define c-hl-focus-urgent (foreign-procedure "hl-scheme-focus-urgent" () int))
(define c-hl-window-move-direction (foreign-procedure "hl-scheme-window-move-direction" (int string) int))
(define c-hl-window-swap-direction (foreign-procedure "hl-scheme-window-swap-direction" (int string) int))
(define c-hl-window-swap-next (foreign-procedure "hl-scheme-window-swap-next" (int int) int))
(define c-hl-window-swap-with (foreign-procedure "hl-scheme-window-swap-with" (int int) int))
(define c-hl-window-float-act (foreign-procedure "hl-scheme-window-float-act" (int int) int))
(define c-hl-window-cycle (foreign-procedure "hl-scheme-window-cycle" (int int int) int))
(define c-hl-window-center (foreign-procedure "hl-scheme-window-center" (int) int))
(define c-hl-window-resize-px (foreign-procedure "hl-scheme-window-resize-px" (int double double int) int))
(define c-hl-window-move-px (foreign-procedure "hl-scheme-window-move-px" (int double double int) int))
(define c-hl-window-pin-act (foreign-procedure "hl-scheme-window-pin-act" (int int) int))
(define c-hl-window-pseudo (foreign-procedure "hl-scheme-window-pseudo" (int int) int))
(define c-hl-window-kill (foreign-procedure "hl-scheme-window-kill" (int) int))
(define c-hl-window-signal (foreign-procedure "hl-scheme-window-signal" (int int) int))
(define c-hl-window-zorder (foreign-procedure "hl-scheme-window-zorder" (int string) int))
(define c-hl-window-set-prop (foreign-procedure "hl-scheme-window-set-prop" (int string string) int))
(define c-hl-window-tag (foreign-procedure "hl-scheme-window-tag" (int string) int))
(define c-hl-window-clear-tags (foreign-procedure "hl-scheme-window-clear-tags" (int) int))
(define c-hl-toggle-swallow (foreign-procedure "hl-scheme-toggle-swallow" () int))
(define c-hl-group-toggle (foreign-procedure "hl-scheme-group-toggle" (int) int))
(define c-hl-group-set (foreign-procedure "hl-scheme-group-set" (int int) int))
(define c-hl-monitor-set-special (foreign-procedure "hl-scheme-monitor-set-special" (string string) int))
(define c-hl-group-cycle (foreign-procedure "hl-scheme-group-cycle" (int int) int))
(define c-hl-group-index (foreign-procedure "hl-scheme-group-index" (int int) int))
(define c-hl-group-move-window (foreign-procedure "hl-scheme-group-move-window" (int int) int))
(define c-hl-group-lock (foreign-procedure "hl-scheme-group-lock" (int) int))
(define c-hl-group-lock-active (foreign-procedure "hl-scheme-group-lock-active" (int) int))
(define c-hl-window-into-group (foreign-procedure "hl-scheme-window-into-group" (int string) int))
(define c-hl-window-out-of-group (foreign-procedure "hl-scheme-window-out-of-group" (int string) int))
(define c-hl-window-into-or-create-group (foreign-procedure "hl-scheme-window-into-or-create-group" (int string) int))
(define c-hl-window-deny-from-group (foreign-procedure "hl-scheme-window-deny-from-group" (int int) int))
(define c-hl-workspace-rename (foreign-procedure "hl-scheme-workspace-rename" (string string) int))
(define c-hl-workspace-move-monitor (foreign-procedure "hl-scheme-workspace-move-monitor" (string string) int))
(define c-hl-workspace-toggle-special (foreign-procedure "hl-scheme-workspace-toggle-special" (string) int))
(define c-hl-workspace-swap-monitors (foreign-procedure "hl-scheme-workspace-swap-monitors" (string string) int))
(define c-hl-cursor-move (foreign-procedure "hl-scheme-cursor-move" (double double) int))
(define c-hl-cursor-corner (foreign-procedure "hl-scheme-cursor-corner" (int int) int))
(define c-hl-exit (foreign-procedure "hl-scheme-exit" () int))
(define c-hl-reload-config (foreign-procedure "hl-scheme-reload-config" () int))
(define c-hl-force-renderer-reload (foreign-procedure "hl-scheme-force-renderer-reload" () int))
(define c-hl-dpms (foreign-procedure "hl-scheme-dpms" (int string) int))
(define c-hl-force-idle (foreign-procedure "hl-scheme-force-idle" (double) int))
(define c-hl-global (foreign-procedure "hl-scheme-global" (string) int))
(define c-hl-event (foreign-procedure "hl-scheme-event" (string) int))
(define c-hl-pass (foreign-procedure "hl-scheme-pass" (int) int))
(define c-hl-send-shortcut (foreign-procedure "hl-scheme-send-shortcut" (string string int) int))
(define c-hl-send-key-state (foreign-procedure "hl-scheme-send-key-state" (string string int int) int))
(define c-hl-mouse (foreign-procedure "hl-scheme-mouse" (string) int))
(define c-hl-release-input-capture (foreign-procedure "hl-scheme-release-input-capture" () int))
(define c-hl-window-fullscreen-state (foreign-procedure "hl-scheme-window-fullscreen-state" (int int int int) int))
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
(define c-hl-window-rule-commit (foreign-procedure "hl-window-rule-commit" () double))
(define c-hl-layer-rule-commit (foreign-procedure "hl-layer-rule-commit" () double))
(define c-hl-rule-set-enabled (foreign-procedure "hl-rule-set-enabled" (double int) int))
(define c-hl-rule-enabled (foreign-procedure "hl-rule-enabled" (double) int))
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
(define c-hl-layers (foreign-procedure "hl-layers" () scheme-object))
(define c-hl-is-key-down (foreign-procedure "hl-is-key-down" (string) int))
(define c-hl-loaded-plugins (foreign-procedure "hl-loaded-plugins" () scheme-object))
(define c-hl-version (foreign-procedure "hl-version" () scheme-object))
(define c-hl-windows-from (foreign-procedure "hl-windows-from" (string) scheme-object))
(define c-hl-window-fullscreen-handler (foreign-procedure "hl-window-fullscreen-handler" (int) scheme-object))
(define c-hl-notify (foreign-procedure "hl-notify!" (string double string string double) scheme-object))
(define c-hl-timer-set-enabled (foreign-procedure "hl-timer-set-enabled" (double int) int))
(define c-hl-timer-enabled (foreign-procedure "hl-timer-enabled" (double) int))
(define c-hl-timer-set-timeout (foreign-procedure "hl-timer-set-timeout" (double double) int))
(define c-hl-exec-raw (foreign-procedure "hl-exec!" (string) int))
(define c-hl-exec-with-rules (foreign-procedure "hl-exec-shell-with-rules!" (string) int))
(define c-hl-gesture (foreign-procedure "hl-scheme-gesture" (int string int string double int) scheme-object))

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
(define c-hl-window-x11 (foreign-procedure "hl-scheme-window-x11" (int) int))

(define (hl--register id thunk)
  (set! hl--binds (cons (cons id thunk) hl--binds))
  id)

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
      (errorf 'hl-bind "click and drag are exclusive"))
    (when (and (hl--plist-get pl 'mouse #f)
               (or (hl--plist-get pl 'repeat #f) (hl--plist-get pl 'locked #f) (hl--plist-get pl 'release #f)))
      (errorf 'hl-bind "mouse is exclusive with repeat/locked/release"))
    (+ (if (or click drag) 2 0)                    ; click/drag imply release: upstream also sets BIND_FLAG_RELEASE for them
       (if (or devices (hl--plist-get pl 'device-inclusive #f)) 8192 0)  ; device binds are inclusive by default, as upstream (device option present ⇒ inclusive)
       (if (equal? key "catchall") 16384 0)      ; upstream: key "catchall" ⇒ BIND_FLAG_CATCH_ALL
       (hl--plist-fold (lambda (opt bit acc) (+ acc (if (hl--plist-get pl opt #f) bit 0))) 0 hl--flag-bits))))

(define (hl--bind-impl tokens thunk . opts)
  (let* ((key (car (reverse tokens)))
         (id (c-hl-bind tokens
                         (hl--bind-flags opts key)
                         (hl--plist-get opts 'description "")
                         (let ((ds (hl--plist-get opts 'devices #f)))
                           (if (list? ds)
                               (let loop ((rest ds) (acc ""))
                                 (cond ((null? rest) acc)
                                       ((null? (cdr rest)) (string-append acc (car rest)))
                                       (else (loop (cdr rest) (string-append acc (car rest) ",")))))
                               "")))))
    (if (< id 0)
        (errorf 'hl-bind "bind ~a rejected, see compositor log" tokens)
        (hl--register id thunk))))

;; hl-bind is the one way to register a bind: TOKENS is a list of key
;; tokens — the modifiers first, then the key. Build it with a helper:
;;   (hl-bind (kbd "C-M-a") THUNK . OPTS)        — emacs syntax
;;   (hl-bind (hl-kbd "SUPER+A") THUNK . OPTS)   — hyprland syntax
;; or write the list out literally:
;;   (hl-bind '("SUPER" "Q") THUNK . OPTS)
;; A modless key is still a list: (kbd "g") → ("g"). There is no
;; two-string shorthand in the core API — define your own wrapper on top
;; if you want one (see the wiki, binds).
(define (hl-bind tokens thunk . opts)
  (apply hl--bind-impl tokens thunk opts))

;; ---- key specification helpers -----------------------------------------------
;; (kbd "C-M-a")      — emacs syntax → (mods . key) pair for hl-bind
;; (hl-kbd "SUPER+SHIFT+Q") — lua/hyprland syntax → (mods . key) pair

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

(define (kbd spec)
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

(define (hl-kbd spec)
  ;; parse a lua/hyprland key specification string → a LIST of key tokens
  ;; e.g. "SUPER+SHIFT+Q" → ("SUPER" "SHIFT" "Q")
  (map hl--trim (hl--split-string spec #\+)))

(define (hl-exec-shell! cmd)
  (c-hl-exec cmd))

(define (hl-after ms thunk)
  (let ((id (c-hl-timer ms 0)))
    (if (< id 0)
        (errorf 'hl-after "timer ~ams rejected, see compositor log" ms)
        (hl--register id thunk))))

(define (hl-repeat ms thunk)
  (let ((id (c-hl-timer ms 1)))
    (if (< id 0)
        (errorf 'hl-repeat "timer ~ams rejected, see compositor log" ms)
        (hl--register id thunk))))

;; returns #f when no window has focus
(define (hl-active-title)
  (c-hl-active-title))

;; collections are marshalled as one newline-joined string and split here
;; (hl--split-lines lives in the prelude), so all list building happens
;; under Scheme's GC (no raw ptrs across calls)
(define (hl-workspaces)
  (let ((joined (c-hl-workspace-names)))
    (if (eq? joined #f)
        '()
        (map make-hl-workspace (map string->number (hl--split-lines joined))))))

(define (hl-on-submap thunk)
  (let ((id (c-hl-submap-listen)))
    (if (< id 0)
        (errorf 'hl-on-submap "listener rejected, see compositor log")
        (hl--register id thunk))))

;; windows are opaque records wrapping an int id. C++ keeps a weak reference
;; keyed by id; the compositor can destroy the window at any moment, and
;; stale handles simply report #f — the id is the only thing crossing FFI.
(define-record-type hl-window (fields id))

;; workspaces and monitors use the same handle model: an opaque record
;; wrapping an int id that indexes a weak-ref registry C++-side. Getters
;; mirror upstream Lua's object fields 1:1; a stale or dead handle yields #f
;; from every getter.
(define-record-type hl-workspace (fields id))
(define-record-type hl-monitor (fields id))

(define c-hl-workspace-name (foreign-procedure "hl-workspace-name" (int) scheme-object))
(define c-hl-workspace-addressable-name (foreign-procedure "hl-workspace-addressable-name" (int) scheme-object))
(define c-hl-workspace-number (foreign-procedure "hl-workspace-number" (int) scheme-object))
(define c-hl-workspace-monitor (foreign-procedure "hl-workspace-monitor" (int) scheme-object))
(define c-hl-workspace-special (foreign-procedure "hl-workspace-special" (int) scheme-object))
(define c-hl-workspace-active (foreign-procedure "hl-workspace-active" (int) scheme-object))
(define c-hl-workspace-visible (foreign-procedure "hl-workspace-visible" (int) scheme-object))
(define c-hl-workspace-empty (foreign-procedure "hl-workspace-empty" (int) scheme-object))
(define c-hl-workspace-persistent (foreign-procedure "hl-workspace-persistent" (int) scheme-object))
(define c-hl-workspace-has-urgent (foreign-procedure "hl-workspace-has-urgent" (int) scheme-object))
(define c-hl-workspace-has-fullscreen (foreign-procedure "hl-workspace-has-fullscreen" (int) scheme-object))
(define c-hl-workspace-fullscreen-mode (foreign-procedure "hl-workspace-fullscreen-mode" (int) scheme-object))
(define c-hl-workspace-fullscreen-window (foreign-procedure "hl-workspace-fullscreen-window" (int) scheme-object))
(define c-hl-workspace-last-window (foreign-procedure "hl-workspace-last-window" (int) scheme-object))
(define c-hl-workspace-window-count (foreign-procedure "hl-workspace-window-count" (int) scheme-object))
(define c-hl-workspace-group-count (foreign-procedure "hl-workspace-group-count" (int) scheme-object))
(define c-hl-workspace-tiled-layout (foreign-procedure "hl-workspace-tiled-layout" (int) scheme-object))
(define c-hl-workspace-alive (foreign-procedure "hl-workspace-alive" (int) scheme-object))
(define c-hl-workspace-same (foreign-procedure "hl-workspace-same" (int int) scheme-object))
(define c-hl-workspace-selector (foreign-procedure "hl-workspace-selector" (int) scheme-object))

(define c-hl-monitor-name (foreign-procedure "hl-monitor-name" (int) scheme-object))
(define c-hl-monitor-description (foreign-procedure "hl-monitor-description" (int) scheme-object))
(define c-hl-monitor-number (foreign-procedure "hl-monitor-number" (int) scheme-object))
(define c-hl-monitor-enabled (foreign-procedure "hl-monitor-enabled" (int) scheme-object))
(define c-hl-monitor-focused (foreign-procedure "hl-monitor-focused" (int) scheme-object))
(define c-hl-monitor-x (foreign-procedure "hl-monitor-x" (int) scheme-object))
(define c-hl-monitor-y (foreign-procedure "hl-monitor-y" (int) scheme-object))
(define c-hl-monitor-width (foreign-procedure "hl-monitor-width" (int) scheme-object))
(define c-hl-monitor-height (foreign-procedure "hl-monitor-height" (int) scheme-object))
(define c-hl-monitor-scale (foreign-procedure "hl-monitor-scale" (int) scheme-object))
(define c-hl-monitor-transform (foreign-procedure "hl-monitor-transform" (int) scheme-object))
(define c-hl-monitor-refresh-rate (foreign-procedure "hl-monitor-refresh-rate" (int) scheme-object))
(define c-hl-monitor-mode (foreign-procedure "hl-monitor-mode" (int) scheme-object))
(define c-hl-monitor-dpms (foreign-procedure "hl-monitor-dpms" (int) scheme-object))
(define c-hl-monitor-vrr (foreign-procedure "hl-monitor-vrr" (int) scheme-object))
(define c-hl-monitor-10bit (foreign-procedure "hl-monitor-10bit" (int) scheme-object))
(define c-hl-monitor-reserved (foreign-procedure "hl-monitor-reserved" (int) scheme-object))
(define c-hl-monitor-mirror-of (foreign-procedure "hl-monitor-mirror-of" (int) scheme-object))
(define c-hl-monitor-active-workspace (foreign-procedure "hl-monitor-active-workspace" (int) scheme-object))
(define c-hl-monitor-active-special-workspace (foreign-procedure "hl-monitor-active-special-workspace" (int) scheme-object))
(define c-hl-monitor-alive (foreign-procedure "hl-monitor-alive" (int) scheme-object))
(define c-hl-monitor-same (foreign-procedure "hl-monitor-same" (int int) scheme-object))
(define c-hl-monitor-selector (foreign-procedure "hl-monitor-selector" (int) scheme-object))

(define (hl-active-window)
  (let ((id (c-hl-active-window-id)))
    (if (< id 0) #f (make-hl-window id))))

(define (hl-windows)
  (let ((joined (c-hl-window-ids)))
    (if (eq? joined #f)
        '()
        (map make-hl-window (map string->number (hl--split-lines joined))))))

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
    (and id (make-hl-workspace id))))

;; => monitor handle, or #f
(define (hl-window-monitor w)
  (let ((id (c-hl-window-monitor-id (hl-window-id w))))
    (and id (make-hl-monitor id))))

(define (hl-window-floating? w)
  (= 1 (c-hl-window-floating (hl-window-id w))))

;; => (width . height), or #f when stale
(define (hl-window-size w)
  (let ((s (c-hl-window-size (hl-window-id w))))
    (if (eq? s #f)
        #f
        (let ((wh (map string->number (hl--split-lines s))))
          (cons (car wh) (cadr wh))))))

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
;; array), or alist (hash table, e.g. '((top . 10) (bottom . 10))).
;; writes propagate like a runtime hl.config — the affected subsystems
;; refresh immediately.
(define (hl-config-add! key val)
  (c-hl-config-begin)
  (hl--push-val val)
  (if (= 0 (c-hl-config-set (hl--str key)))
      #t
      (errorf 'hl-config-add! "~a" (c-hl-config-last-error))))

(define (hl--push-val v)
  (cond ((number? v) (c-hl-config-push-num (exact->inexact v)))
        ((boolean? v) (c-hl-config-push-bool (if v 1 0)))
        ((string? v) (c-hl-config-push-str v))
        ((and (pair? v) (pair? (car v)))
         ;; alist → hash table
         (c-hl-config-tbl-open 1)
         (for-each (lambda (kv)
                     (c-hl-config-tbl-key (hl--str (car kv)))
                     (hl--push-val (cdr kv))
                     (c-hl-config-tbl-set-hash))
                   v))
        ((and (pair? v) (symbol? (car v)))
         ;; plist → hash table (the API's nested named-fields form)
         (c-hl-config-tbl-open 1)
         (hl--plist-fold (lambda (k val _)
                           (c-hl-config-tbl-key (hl--str k))
                           (hl--push-val val)
                           (c-hl-config-tbl-set-hash))
                         0 v))
        ((list? v)
         ;; array table (e.g. a vec2 '(20 20))
         (c-hl-config-tbl-open 0)
         (let loop ((rest v) (i 1))
           (unless (null? rest)
             (hl--push-val (car rest))
             (c-hl-config-tbl-seti i)
             (loop (cdr rest) (+ i 1)))))
        (else (errorf 'hl-config-add! "unsupported value ~s" v))))

(define (hl-config-get key)
  (let ((s (c-hl-config-get (hl--str key))))
    (if (not s)
        #f
        (hl--unmarshal (hl--split-lines s)))))

;; ---- rules -------------------------------------------------------------------
;; window/layer rules: an alist with 'match (an alist of property → value),
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
(define-record-type hl-rule (fields id))

(define (hl--rule-spec-value v)
  (cond ((string? v) v)
        ((boolean? v) (if v "true" "false"))
        ((number? v) (number->string v))
        (else #f)))

(define (hl--window-rule-mk name spec begin-fn effect-fn commit-fn what)
  (let ((enabled (hl--plist-get spec 'enabled #t)))
    (if (not (= 0 (begin-fn (if name (hl--str name) "") (if enabled 1 0))))
        (errorf what "~a" (c-hl-config-last-error))
        (let loop ((rest spec))
          (cond ((null? rest)
                 (let ((id (commit-fn)))
                   (if (< id 0)
                       (errorf what "~a" (c-hl-config-last-error))
                       (make-hl-rule id))))
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
  (hl--window-rule-mk name spec c-hl-window-rule-begin c-hl-window-rule-effect c-hl-window-rule-commit 'hl-window-rule-add!))

(define (hl-layer-rule-add! name . spec)
  (hl--window-rule-mk name spec c-hl-layer-rule-begin c-hl-layer-rule-effect c-hl-layer-rule-commit 'hl-layer-rule-add!))

(define (hl-rule-set-enabled rule enabled)
  (if (= 0 (c-hl-rule-set-enabled (exact->inexact (hl-rule-id rule)) (if enabled 1 0)))
      #t
      (errorf 'hl-rule-set-enabled "unknown rule")))

(define (hl-rule-enabled? rule)
  (= 1 (c-hl-rule-enabled (exact->inexact (hl-rule-id rule)))))

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
                         ((pair? v)
                          (c-hl-config-begin)
                          (hl--push-val v)
                          (if (= 0 (c-hl-workspace-rule-gap k))
                              (loop (cdr tail))
                              (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                         (else (errorf 'hl-workspace-rule-add! "unsupported value for ~a" k))))))))))

;; ---- queries ------------------------------------------------------------------
;; selectors use the config selector syntax: "class:^foot$", "title:foo",
;; "pid:123", "address:0x...", "workspace:3", "floating", "tiled", ...
;; (hl-window-from SELECTOR) -> window handle | #f
(define (hl-window-from sel)
  (let ((id (c-hl-window-from (hl--str sel))))
    (if (< id 0) #f (make-hl-window (inexact->exact id)))))

(define (hl-urgent-window)
  (let ((id (c-hl-urgent-window)))
    (if (< id 0) #f (make-hl-window (inexact->exact id)))))

(define (hl-last-window)
  (let ((id (c-hl-last-window)))
    (if (< id 0) #f (make-hl-window (inexact->exact id)))))

;; monitors resolve by name or "desc:DESCRIPTION" (config monitor syntax)
;; monitor queries => monitor handle, or #f
(define (hl-monitor-from sel)
  (let ((id (c-hl-monitor-from (hl--str sel))))
    (and id (make-hl-monitor id))))

(define (hl-monitor-at x y)
  (let ((id (c-hl-monitor-at (exact->inexact x) (exact->inexact y))))
    (and id (make-hl-monitor id))))

(define (hl-monitor-at-cursor)
  (let ((id (c-hl-monitor-at-cursor)))
    (and id (make-hl-monitor id))))

(define (hl-active-monitor)
  (let ((id (c-hl-active-monitor)))
    (and id (make-hl-monitor id))))

;; workspace queries => workspace handle, or #f
(define (hl-active-workspace)
  (let ((id (c-hl-active-workspace)))
    (and id (make-hl-workspace id))))

(define (hl-active-special-workspace)
  (let ((id (c-hl-active-special-workspace)))
    (and id (make-hl-workspace id))))

(define (hl-last-workspace)
  (let ((id (c-hl-last-workspace)))
    (and id (make-hl-workspace id))))

;; windows on a workspace (a workspace handle or selector: "3", "name:foo",
;; "special:bar") => list of window handles
(define (hl-workspace-windows ws)
  (let ((s (c-hl-workspace-windows (hl--ws-arg ws))))
    (if (not s)
        '()
        (map make-hl-window (map string->number (hl--split-lines s))))))

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
    (and id (make-hl-monitor id))))

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
    (and id (make-hl-window id))))

;; => window handle, or #f
(define (hl-workspace-last-window w)
  (let ((id (c-hl-workspace-last-window (hl-workspace-id w))))
    (and id (make-hl-window id))))

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

;; reserved area as an alist: ((top . n) (left . n) (right . n) (bottom . n))
(define (hl-monitor-reserved m)
  (let ((s (c-hl-monitor-reserved (hl-monitor-id m))))
    (if (not s)
        #f
        (let loop ((rest (hl--split-lines s)) (acc '()))
          (if (or (null? rest) (null? (cdr rest)))
              (reverse acc)
              (loop (cddr rest)
                    (cons (cons (string->symbol (car rest))
                                (string->number (cadr rest)))
                          acc)))))))

;; the monitor this one mirrors, as a handle; #f when not a mirror
(define (hl-monitor-mirror-of m)
  (let ((id (c-hl-monitor-mirror-of (hl-monitor-id m))))
    (and id (make-hl-monitor id))))

;; => workspace handle, or #f
(define (hl-monitor-active-workspace m)
  (let ((id (c-hl-monitor-active-workspace (hl-monitor-id m))))
    (and id (make-hl-workspace id))))

;; => workspace handle, or #f when no special workspace is open
(define (hl-monitor-active-special-workspace m)
  (let ((id (c-hl-monitor-active-special-workspace (hl-monitor-id m))))
    (and id (make-hl-workspace id))))

(define (hl-monitor-alive? m)
  (eq? (c-hl-monitor-alive (hl-monitor-id m)) #t))

(define (hl-monitor-rule-add!=? a b)
  (eq? (c-hl-monitor-same (hl-monitor-id a) (hl-monitor-id b)) #t))

;; => list of (monitor . namespace) pairs
(define (hl-layers)
  (let ((s (c-hl-layers)))
    (if (not s)
        '()
        (let loop ((rest (hl--split-lines s)) (acc '()))
          (if (or (null? rest) (null? (cdr rest)))
              (reverse acc)
              (loop (cddr rest) (cons (cons (car rest) (cadr rest)) acc)))))))

;; key by keysym name: (hl-is-key-down "Return")
(define (hl-is-key-down key)
  (= 1 (c-hl-is-key-down key)))

(define (hl-loaded-plugins)
  (let ((s (c-hl-loaded-plugins)))
    (if s (hl--split-lines s) '())))

(define (hl-version)
  (c-hl-version))

;; ALL windows matching a selector: (hl-windows-from "class:^foot$")
(define (hl-windows-from sel)
  (let ((s (c-hl-windows-from (hl--str sel))))
    (if (not s)
        '()
        (map make-hl-window (map string->number (hl--split-lines s))))))

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

;; ---- timer handles --------------------------------------------------------------
;; hl-after/hl-repeat return timer ids; these control them afterwards
;; absent → toggle (via the enabled? query); #t/#f → explicit set
(define (hl-timer-enabled-set! id . on?)
  (if (= 0 (c-hl-timer-set-enabled (exact->inexact id)
                                   (if (if (null? on?) (not (hl-timer-enabled? id)) (car on?)) 1 0)))
      #t
      (errorf 'hl-timer-enabled-set! "unknown timer")))
(define (hl-timer-enabled? id)
  (= 1 (c-hl-timer-enabled (exact->inexact id))))
(define (hl-timer-set-timeout id ms)
  (if (= 0 (c-hl-timer-set-timeout (exact->inexact id) (exact->inexact ms)))
      #t
      (errorf 'hl-timer-set-timeout "timeout must be >= 1ms")))

;; ---- exec variants ----------------------------------------------------------------
;; (hl-exec! "cmd") — no shell; the string is execvp'd (space-split)
;; (hl-exec-shell-with-rules! "[float size 800 500] mygame") — classic exec rules
(define (hl-exec! cmd)
  (> (c-hl-exec-raw cmd) 0))
(define (hl-exec-shell-with-rules! cmd)
  (> (c-hl-exec-with-rules cmd) 0))

;; ---- gestures ----------------------------------------------------------------------
;; (hl-gesture 3 "swipe" (lambda () ...) ['mods "SUPER"] ['scale 1.0] ['disable-inhibit #t])
;; the thunk fires when the gesture ends (3+ finger swipes/pinches).
;; live variant: (hl-gesture-live 3 "swipe" on-begin on-update on-end ...) —
;; on-update receives (dx dy scale) as a 3-list.
(define (hl-gesture fingers direction thunk . opts)
  (let ((s (c-hl-gesture fingers (hl--str direction) 0
                         (hl--str (hl--plist-get opts 'mods ""))
                         (exact->inexact (hl--plist-get opts 'scale 1.0))
                         (if (hl--plist-get opts 'disable-inhibit #f) 1 0))))
    (if (not s)
        (errorf 'hl-gesture "~a" (c-hl-config-last-error))
        (hl--register (string->number s) thunk))))

(define (hl-gesture-live fingers direction on-begin on-update on-end . opts)
  (let ((s (c-hl-gesture fingers (hl--str direction) 1
                         (hl--str (hl--plist-get opts 'mods ""))
                         (exact->inexact (hl--plist-get opts 'scale 1.0))
                         (if (hl--plist-get opts 'disable-inhibit #f) 1 0))))
    (if (not s)
        (errorf 'hl-gesture "~a" (c-hl-config-last-error))
        (let ((ids (map string->number (hl--split-lines s))))
          (hl--register (car ids) on-begin)
          (hl--register (cadr ids) (lambda (payload) (apply on-update (map string->number (hl--split-lines payload)))))
          (hl--register (caddr ids) on-end)
          #t))))

;; ---- monitors, curves, animations, permissions ------------------------------

;; (hl-monitor-rule-add! "DP-1" '((mode . "preferred") (scale . "1.6") (position . "0x0")
;;                      (transform . 0) (bitdepth . 10) (vrr . 1)
;;                      (reserved . '((top . 60)))))
;; string fields: mode position scale mirror cm icc sdr_eotf
;; numeric fields: transform bitdepth vrr supports_wide_color supports_hdr
;;   sdrbrightness sdrsaturation sdr_min_luminance sdr_max_luminance
;;   min_luminance max_luminance max_avg_luminance
;; gap fields (alist): reserved / reserved_area ; bool: disabled
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
         (vs (append (map exact->inexact vals) (list 0 0 0 0))))
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


;; decode "b|n|s|t" + payload lines into a scheme value
(define (hl--unmarshal lines)
  (case (string->symbol (car lines))
    ((b) (string=? (cadr lines) "1"))
    ((n) (string->number (cadr lines)))
    ((s) (cadr lines))
    ((t)
     (let loop ((rest (cdr lines)) (acc '()))
       (cond ((null? rest) (reverse acc))
             ((null? (cdr rest)) (reverse acc)) ; malformed tail
             (else
              (let ((k (car rest)) (v (cadr rest)))
                (let ((k2 (if (let ((n (string->number k))) n) (string->number k) (string->symbol k)))
                      (v2 (or (string->number v) v)))
                  (loop (cddr rest) (cons (cons k2 v2) acc))))))))
    (else #f)))

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
  (let ((joined (c-hl-monitor-names)))
    (if (eq? joined #f)
        '()
        (map make-hl-monitor (map string->number (hl--split-lines joined))))))

(define (hl--window-listen which handler)
  (let ((id (c-hl-window-event-listen which)))
    (if (< id 0)
        (errorf 'hl-on-window "listener rejected, see compositor log")
        (hl--register id handler))))

(define (hl-on-window-open handler)
  (hl--window-listen 0 handler))

(define (hl-on-window-close handler)
  (hl--window-listen 1 handler))

;; handlers receive the window handle
(define (hl-on-window-title handler)
  (hl--window-listen 2 handler))

(define (hl-on-window-class handler)
  (hl--window-listen 3 handler))

(define (hl-on-window-urgent handler)
  (hl--window-listen 4 handler))

(define (hl-on-window-pin handler)
  (hl--window-listen 5 handler))

(define (hl-on-window-fullscreen handler)
  (hl--window-listen 6 handler))

;; fires when a window moves to a different workspace
(define (hl-on-window-move-to-workspace handler)
  (hl--window-listen 7 handler))

;; fires when the focused window changes
(define (hl-on-window-active handler)
  (hl--window-listen 8 handler))

;; handler signature: (lambda (w state) ...) — state is #t when minimized
(define (hl-on-window-minimize handler)
  (let ((id (c-hl-window-minimize-listen)))
    (if (< id 0)
        (errorf 'hl-on-window-minimize "listener rejected, see compositor log")
        (hl--register id handler))))

;; fires when a window is created and mapped, but before window rules are
;; applied (hl-on-window-open waits for full initialization)
(define (hl-on-window-open-early handler)
  (hl--window-listen 9 handler))

;; fires when the window is forcefully killed, e.g. via hyprctl kill
(define (hl-on-window-kill handler)
  (hl--window-listen 10 handler))

;; fires when a window rings the system bell, even if it's muted
(define (hl-on-window-bell handler)
  (hl--window-listen 11 handler))

;; fires when a window's rules are re-evaluated, e.g. on a title change
(define (hl-on-window-update-rules handler)
  (hl--window-listen 12 handler))

;; lifecycle: fires once when the session starts (its first render frame) and
;; once before exit. handlers registered after startup fire immediately.
(define (hl-on-start handler)
  (let ((id (c-hl-lifecycle-listen 0)))
    (if (< id 0)
        (errorf 'hl-on-start "listener rejected, see compositor log")
        (hl--register id handler))))

(define (hl-on-shutdown handler)
  (let ((id (c-hl-lifecycle-listen 1)))
    (if (< id 0)
        (errorf 'hl-on-shutdown "listener rejected, see compositor log")
        (hl--register id handler))))

(define (hl-on-config-reloaded handler)
  (let ((id (c-hl-config-reloaded-listen)))
    (if (< id 0)
        (errorf 'hl-on-config-reloaded "listener rejected, see compositor log")
        (hl--register id handler))))

;; fires BEFORE a config reload (upstream config.unload → config.preReload)
(define (hl-on-config-unload handler)
  (let ((id (c-hl-config-unload-listen)))
    (if (< id 0)
        (errorf 'hl-on-config-unload "listener rejected, see compositor log")
        (hl--register id handler))))

;; zero-argument callback — upstream delivers nil for window.destroy (the bus
;; event is a weak ref; identity belongs to hl-on-window-close)
(define (hl-on-window-destroy handler)
  (let ((id (c-hl-window-destroy-listen)))
    (if (< id 0)
        (errorf 'hl-on-window-destroy "listener rejected, see compositor log")
        (hl--register id handler))))

;; layer callbacks receive the layer's namespace string
(define (hl-on-layer-open handler)
  (let ((id (c-hl-layer-listen 0)))
    (if (< id 0)
        (errorf 'hl-on-layer-open "listener rejected, see compositor log")
        (hl--register id handler))))

(define (hl-on-layer-close handler)
  (let ((id (c-hl-layer-listen 1)))
    (if (< id 0)
        (errorf 'hl-on-layer-close "listener rejected, see compositor log")
        (hl--register id handler))))

;; handler signature: (lambda (scheduled?) ...) — #t when the prop refresh ran
;; as scheduled, #f when it was executed prematurely
(define (hl-on-config-props-refreshed handler)
  (let ((id (c-hl-config-props-refreshed-listen)))
    (if (< id 0)
        (errorf 'hl-on-config-props-refreshed "listener rejected, see compositor log")
        (hl--register id handler))))

;; identity, as in Lua's windowEq: true iff both handles refer to the same
;; live window (two handles for one window each get their own id)
(define (hl-window=? a b)
  (= 1 (c-hl-window-same (hl-window-id a) (hl-window-id b))))

(define (hl-unbind id)
  (= 0 (c-hl-unbind id)))

;; removes ALL binds matching a key string (the display key as passed to
;; hl-bind — e.g. "SUPER T", "C-M-x", "mouse:272"). case-insensitive,
;; whitespace-insensitive. returns #t if any binds were removed.
(define (hl-unbind-key key)
  (= 0 (c-hl-unbind-key (hl--str key))))

(define (hl-current-submap)
  (or (c-hl-current-submap) ""))

;; => (x . y), or #f
(define (hl-cursor-pos)
  (let ((s (c-hl-cursor-pos)))
    (if (eq? s #f)
        #f
        (let ((xy (map string->number (hl--split-lines s))))
          (cons (car xy) (cadr xy))))))

(define (hl-on-workspace-active handler)
  (let ((id (c-hl-workspace-active-listen)))
    (if (< id 0)
        (errorf 'hl-on-workspace-active "listener rejected, see compositor log")
        (hl--register id handler))))

;; handler signature: (lambda (ws) ...) — ws is a workspace handle
(define (hl--workspace-event-listen which handler)
  (let ((id (c-hl-workspace-event-listen which)))
    (if (< id 0)
        (errorf 'hl-on-workspace "listener rejected, see compositor log")
        (hl--register id handler))))

;; fires when a workspace is created
(define (hl-on-workspace-created handler)
  (hl--workspace-event-listen 0 handler))

;; fires when a workspace is removed. The handle it passes is BORN DEAD —
;; every getter on it returns #f, exactly like an expired object in upstream
;; Lua. See the comment at the C++ listener for why the name is not provided.
(define (hl-on-workspace-removed handler)
  (hl--workspace-event-listen 1 handler))

;; fires when the opened special workspace on a monitor changes; handler
;; signature: (lambda (ws mon) ...) — ws is #f when no special workspace is
;; open on that monitor
(define (hl-on-workspace-special-active handler)
  (hl--workspace-event-listen 2 handler))

;; fires when a workspace moves to a different monitor: (lambda (ws mon) ...)
(define (hl-on-workspace-move-to-monitor handler)
  (hl--workspace-event-listen 3 handler))

;; handler signature: (lambda (mon) ...) — mon is a monitor handle
(define (hl--monitor-event-listen which handler)
  (let ((id (c-hl-monitor-event-listen which)))
    (if (< id 0)
        (errorf 'hl-on-monitor "listener rejected, see compositor log")
        (hl--register id handler))))

(define (hl-on-monitor-added handler)
  (hl--monitor-event-listen 0 handler))

(define (hl-on-monitor-removed handler)
  (hl--monitor-event-listen 1 handler))

(define (hl-on-monitor-focused handler)
  (hl--monitor-event-listen 2 handler))

;; fires when the monitor arrangement changes (no payload)
(define (hl-on-monitor-layout-changed handler)
  (hl--monitor-event-listen 3 handler))

;; reload boundary: drop the previous generation's handler table and install
;; a FRESH environment for the new one. The copy inherits the API (defined
;; once in the persistent environment) but user definitions from previous
;; generations become unreachable when the copy is replaced — the same
;; clean-slate semantics as upstream's per-generation lua_State. hl--state
;; is the one deliberate cross-generation bridge (it lives in the persistent
;; environment).
(define (hl--reset)
  (set! hl--binds '())
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

(define (hl-state-set! k v)
  (let ((entry (assq k hl--state)))
    (if entry
        (set-cdr! entry v)
        (set! hl--state (cons (cons k v) hl--state))))
  v)

(define (hl-state-ref k . default)
  (let ((entry (assq k hl--state)))
    (if entry
        (cdr entry)
        (if (null? default) #f (car default)))))

(define (hl-state-keys)
  (map car hl--state))

(set! hl--ready #t)
)scm";

namespace Config::Scheme::Internals {
    bool g_up          = false; // interpreter + bootstrap ready
    int  g_nextBindId  = 0;
    int  g_nextWindowId = 0;

    // window handles: id -> WEAK reference. The compositor destroys windows
    // whenever it wants; a stale handle is detected via lock() == nullptr.
    // Deliberately non-owning: a strong ref would keep a zombie window alive.
    std::unordered_map<int, PHLWINDOWREF> g_windows;

    // workspace and monitor handles: same model as window handles — an int id
    // in the user-facing record, mapped to a weak ref here so handles can be
    // validated (and go stale) at use time. Deliberately non-owning too.
    int g_nextWorkspaceId = 0;
    int g_nextMonitorId   = 0;
    std::unordered_map<int, PHLWORKSPACEREF> g_workspaces;
    std::unordered_map<int, PHLMONITORREF>   g_monitors;
}

namespace Config::Scheme {

    using namespace Internals;

    static std::string                g_configPath;
    // submap registration context: binds created while set are scoped to it
    static std::string                g_regSubmap;
    static std::string                g_regSubmapReset;
    static int                        g_watchFd       = -1;    // inotify fd; dup'd into event loop waiters
    // binds keyed by their scheme id, so hl-unbind can target one
    static std::vector<std::pair<int, Keybinds::PBind>> g_binds;
    // scheme timers: C++ owns the CEventLoopTimer, Scheme owns the closure
    // (kept alive via the hl--binds assoc list, keyed by the same ids)
    static std::vector<std::pair<int, SP<CEventLoopTimer>>> g_timers;
    // event subscriptions: C++ owns the listener handles (dropping one
    // unsubscribes); Scheme owns the handler closures by id
    static std::vector<Hyprutils::Signal::CHyprSignalListener> g_submapListeners;
    static std::vector<Hyprutils::Signal::CHyprSignalListener> g_windowEventListeners;

    // lifecycle: the start event is dispatched by an init-time listener
    // (covers the plugin-auto-loaded-at-startup path, where the config load
    // precedes the first render frame).
    static bool                                        g_startSeen      = false;
    static std::vector<int>                            g_pendingStart;
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

    static Keybinds::SBindResult fireSchemeBind(int id) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        watchdogEnter("bind callback");
        // hl--bind-fire: #f = declined (thunk returned #f, or error/watchdog)
        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--bind-fire")), Sinteger(id));
        watchdogExit();
        if (r == Sfalse)
            return {.success = false, .error = "scheme keybind callback declined"};
        return {};
    }

    // fires a handler registered for id with no payload; errors contained
    static void fireScheme(int id) {
        if (!g_up)
            return;
        watchdogEnter("handler");
        Scall1(Stop_level_value(Sstring_to_symbol("hl--fire")), Sinteger(id));
        watchdogExit();
    }

    // called from Scheme via foreign-procedure; flags are raw eBindFlags bits,
    // assembled Scheme-side from the options alist
    static int hlSchemeBind(ptr tokens, int flags, const char* desc, const char* devices) {
        if (!g_up)
            return -1;

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

        const int id = g_nextBindId++;

        Keybinds::SExtraBindArgs args;
        std::string display_key;
        for (const auto& k : keys) {
            if (!display_key.empty()) display_key += ' ';
            display_key += k;
        }
        args.metadata.displayKey = display_key;
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

        auto bind = Keybinds::CBind::make(std::move(keys), sc<Keybinds::BindFlags>(flags), [id] { return fireSchemeBind(id); }, std::move(args));
        if (!bind) {
            LOG(Log::ERR, "[scheme] bind failed: {}", bind.error());
            return -1;
        }

        g_binds.emplace_back(id, Keybinds::mgr()->addBind(std::move(*bind)));
        return id;
    }

    // called from Scheme via foreign-procedure: remove one scheme bind
    static int hlSchemeUnbindKey(const char* key) {
        if (!g_up || !Keybinds::mgr() || !key || !*key)
            return -1;
        const auto removed = Keybinds::mgr()->removeBinds(key);
        // drop our g_binds entries whose displayKey matches
        std::erase_if(g_binds, [&key](const auto& b) {
            return b.second && b.second->metadata().displayKey == key;
        });
        return removed > 0 ? 0 : -1;
    }

    static int hlSchemeUnbind(int id) {
        if (!g_up)
            return -1;

        const auto it = std::ranges::find_if(g_binds, [id](const auto& b) { return b.first == id; });
        if (it == g_binds.end())
            return -1;

        if (Keybinds::mgr())
            Keybinds::mgr()->removeBind(it->second);
        g_binds.erase(it);
        return 0;
    }

    // called from Scheme via foreign-procedure
    static int hlSchemeExec(const char* cmd) {
        if (!g_up || !cmd)
            return -1;

        return (int)Config::Supplementary::executor()->spawn(cmd).value_or(-1);
    }

    // fires a handler registered for id with a string payload; all errors are
    // contained inside hl--fire-str's guard
    static void fireSchemeStr(int id, const std::string& arg) {
        if (!g_up)
            return;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-str")), Sinteger(id), Sstring_utf8(arg.c_str(), arg.size()));
        watchdogExit();
    }

    // fires a handler registered for id with a boolean payload (#t/#f); all
    // errors are contained inside hl--fire-bool's guard
    static void fireSchemeBool(int id, bool arg) {
        if (!g_up)
            return;

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-bool")), Sinteger(id), arg ? Strue : Sfalse);
        watchdogExit();
    }

    // events carrying window payloads: the window crosses as a fresh handle id
    static void fireSchemeWin(int id, PHLWINDOW window) {
        if (!g_up || !window)
            return;

        const int winId = g_nextWindowId++;
        g_windows.emplace(winId, PHLWINDOWREF(window));

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-win")), Sinteger(id), Sinteger(winId));
        watchdogExit();
    }

    // handle registries resolve like windowFromId: lock, or drop the dead
    // entry while we're here
    static PHLWORKSPACE workspaceFromId(int id) {
        const auto it = g_workspaces.find(id);
        if (it == g_workspaces.end())
            return nullptr;
        auto ws = it->second.lock();
        if (!ws)
            g_workspaces.erase(it);
        return ws;
    }

    static PHLMONITOR monitorFromId(int id) {
        const auto it = g_monitors.find(id);
        if (it == g_monitors.end())
            return nullptr;
        auto mon = it->second.lock();
        if (!mon)
            g_monitors.erase(it);
        return mon;
    }

    // events carrying workspace/monitor payloads: the object crosses as a
    // fresh handle id; a null object crosses as #f. The Ref variant stores
    // the weak ref as-is without locking — used by workspace.removed, which
    // fires mid-destruction (that handle is born dead; see the comment at
    // the listener).
    static void fireSchemeWs(int id, PHLWORKSPACE ws) {
        if (!g_up)
            return;

        int wsId = -1;
        if (ws) {
            wsId = g_nextWorkspaceId++;
            g_workspaces.emplace(wsId, PHLWORKSPACEREF(ws));
        }

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-ws")), Sinteger(id), wsId >= 0 ? Sinteger(wsId) : Sfalse);
        watchdogExit();
    }

    static void fireSchemeWsRef(int id, PHLWORKSPACEREF ws) {
        if (!g_up)
            return;

        const int wsId = g_nextWorkspaceId++;
        g_workspaces.emplace(wsId, ws);

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-ws")), Sinteger(id), Sinteger(wsId));
        watchdogExit();
    }

    static void fireSchemeMon(int id, PHLMONITOR mon) {
        if (!g_up)
            return;

        int monId = -1;
        if (mon) {
            monId = g_nextMonitorId++;
            g_monitors.emplace(monId, PHLMONITORREF(mon));
        }

        watchdogEnter("handler");
        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-mon")), Sinteger(id), monId >= 0 ? Sinteger(monId) : Sfalse);
        watchdogExit();
    }

    // two-handle payloads (workspace, monitor); a null object crosses as #f
    static void fireSchemeWsMon(int id, PHLWORKSPACE ws, PHLMONITOR mon) {
        if (!g_up)
            return;

        int wsId = -1, monId = -1;
        if (ws) {
            wsId = g_nextWorkspaceId++;
            g_workspaces.emplace(wsId, PHLWORKSPACEREF(ws));
        }
        if (mon) {
            monId = g_nextMonitorId++;
            g_monitors.emplace(monId, PHLMONITORREF(mon));
        }

        watchdogEnter("handler");
        Scall3(Stop_level_value(Sstring_to_symbol("hl--fire-ws-mon")), Sinteger(id), wsId >= 0 ? Sinteger(wsId) : Sfalse, monId >= 0 ? Sinteger(monId) : Sfalse);
        watchdogExit();
    }

    // called from Scheme via foreign-procedure; repeat != 0 re-arms forever
    // (or until the callback errors, which stops zombie loops)
    static int hlSchemeTimer(int ms, int repeat) {
        if (!g_up || !g_pEventLoopManager || ms < 0)
            return -1;

        const int id = g_nextBindId++;

        auto shared = makeShared<CEventLoopTimer>(std::chrono::milliseconds(ms),
            [id, ms, repeat](SP<CEventLoopTimer> self, void*) {
                const auto result = fireSchemeBind(id);

                if (repeat && result.success) {
                    self->updateTimeout(std::chrono::milliseconds(ms));
                    return;
                }

                // one-shot done, or the callback failed: tear down.
                // onTimerFire dispatches over a copy of the timer list, so
                // removing ourselves here is safe.
                self->cancel();
                if (g_pEventLoopManager)
                    g_pEventLoopManager->removeTimer(self);
                std::erase_if(g_timers, [self](const auto& t) { return t.second == self; });
            },
            nullptr);

        g_pEventLoopManager->addTimer(shared);
        g_timers.emplace_back(id, shared);
        return id;
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
    static ptr hlSchemeWorkspaceNames() {
        if (!g_up)
            return Sfalse;

        std::string joined;
        for (const auto& wsRef : State::Workspace::state()->workspaces()) {
            const auto ws = wsRef.lock();
            if (!ws)
                continue;
            const int id = g_nextWorkspaceId++;
            g_workspaces.emplace(id, wsRef);
            if (!joined.empty())
                joined += '\n';
            joined += std::to_string(id);
        }

        if (joined.empty())
            return Sfalse;

        return Sstring_utf8(joined.c_str(), joined.size());
    }

    // called from Scheme via foreign-procedure: subscribe to submap changes
    static int hlSchemeSubmapListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_submapListeners.emplace_back(Event::bus()->m_events.keybinds.submap.listen([id](const std::string& name) {
            fireSchemeStr(id, name);
        }));
        return id;
    }

    // resolves a handle id to the live window, or null if stale (dead or
    // unknown). lazily drops dead entries while we're here.
    static std::optional<PHLWINDOW> actionWindow(int id);

    static PHLWINDOW windowFromId(int id) {
        const auto it = g_windows.find(id);
        if (it == g_windows.end())
            return nullptr;

        auto window = it->second.lock();
        if (!window)
            g_windows.erase(it);

        return window;
    }

    static int hlSchemeActiveWindowId() {
        if (!g_up)
            return -1;

        const auto window = Desktop::focusState()->window();
        if (!window)
            return -1;

        const int id = g_nextWindowId++;
        g_windows.emplace(id, PHLWINDOWREF(window));
        return id;
    }

    static ptr hlSchemeWindowIds() {
        if (!g_up)
            return Sfalse;

        std::string joined;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w->mapped())
                continue;

            if (!joined.empty())
                joined += '\n';

            const int id = g_nextWindowId++;
            g_windows.emplace(id, PHLWINDOWREF(w));
            joined += std::to_string(id);
        }

        if (joined.empty())
            return Sfalse;

        return Sstring_utf8(joined.c_str(), joined.size());
    }

    static ptr hlSchemeWindowTitle(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto title = window->metadata().title();
        return Sstring_utf8(title.c_str(), title.size());
    }

    static int hlSchemeWindowAlive(int id) {
        if (!g_up)
            return 0;

        return windowFromId(id) ? 1 : 0;
    }

    static int hlSchemeWindowClose(int id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (id >= 0 && !window)
            return -1;
        if (!window)
            return -1;

        return Config::Actions::closeWindow(window) ? 0 : -2;
    }

    static ptr hlSchemeWindowClass(int id) {
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

    static ptr hlSchemeWindowWorkspaceId(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window || !window->m_workspace)
            return Sfalse;

        const int wsId = g_nextWorkspaceId++;
        g_workspaces.emplace(wsId, PHLWORKSPACEREF(window->m_workspace));
        return Sinteger(wsId);
    }

    static ptr hlSchemeWindowMonitorId(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto monitor = State::monitorState()->query().id(window->monitorID()).run();
        if (!monitor)
            return Sfalse;

        const int monId = g_nextMonitorId++;
        g_monitors.emplace(monId, PHLMONITORREF(monitor));
        return Sinteger(monId);
    }

    static int hlSchemeWindowFloating(int id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->isFloating()) ? 1 : 0;
    }

    static ptr hlSchemeWindowSize(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto sz = window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        const auto s  = std::to_string((int)sz.x) + "\n" + std::to_string((int)sz.y);
        return Sstring_utf8(s.c_str(), s.size());
    }

    static int hlSchemeWindowPid(int id) {
        if (!g_up)
            return -1;

        const auto window = windowFromId(id);
        if (!window)
            return -1;

        return (int)window->backend().pid();
    }

    static int hlSchemeWindowFocus(int id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::focus(*window) ? 0 : -2;
    }

    static int hlSchemeWindowFloat(int id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_TOGGLE, *window) ? 0 : -2;
    }

    static int hlSchemeWindowMoveToWorkspace(int id, const char* name) {
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
    static int hlSchemeWindowFullscreenSet(int id, int modeRaw) {
        if (!g_up)
            return -1;
        const auto window = actionWindow(id).value_or(nullptr);
        if (!window)
            return -1;
        const auto mode = sc<Fullscreen::eFullscreenMode>(modeRaw);
        return Config::Actions::fullscreenWindow(mode, false, window) ? 0 : -2;
    }

    static int hlSchemeWindowFullscreenToggle(int id, int modeRaw) {
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

    static int hlSchemeWindowFullscreenMode(int id) {
        if (!g_up)
            return -1;

        const auto window = windowFromId(id);
        if (!window)
            return -1;

        return sc<int>(Fullscreen::controller()->getFullscreenModes(window).internal);
    }

    static int hlSchemeWindowHidden(int id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->isHidden()) ? 1 : 0;
    }

    static int hlSchemeWindowPinned(int id) {
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
    static PHLWINDOW windowFromSchemeId(int id) {
        return id >= 0 ? windowFromId(id) : Desktop::focusState()->window();
    }

    static int hlSchemeWindowPseudoQuery(int id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        return (window && window->layoutTarget()->isPseudo()) ? 1 : 0;
    }

    static int hlSchemeWindowMaximizedQuery(int id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        return (window && Fullscreen::controller()->getFullscreenModes(window).internal == Fullscreen::FSMODE_MAXIMIZED) ? 1 : 0;
    }

    static int hlSchemeWindowInGroup(int id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        return (window && window->grouping().group()) ? 1 : 0;
    }

    static int hlSchemeWindowGroupDenied(int id) {
        if (!g_up)
            return 0;
        const auto window = windowFromSchemeId(id);
        if (!window)
            return 0;
        const auto group = window->grouping().group();
        return (group && group->denied()) ? 1 : 0;
    }

    static int hlSchemeWindowGroupLocked(int id) {
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
    static int hlSchemeWindowGroupLock(int id, int act) {
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
    static ptr hlSchemeWindowPropGet(int id, const char* prop) {
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

    static std::optional<PHLWINDOW> actionWindow(int id) {
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

    static int hlSchemeWindowMoveDirection(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-move-direction", Config::Actions::moveInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapDirection(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-direction", Config::Actions::swapInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapNext(int id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-next", Config::Actions::swapNext(prev == 0, actionWindow(id)));
    }

    static int hlSchemeWindowSwapWith(int id, int otherId) {
        if (!g_up)
            return -1;
        const auto other = windowFromId(otherId);
        if (!other)
            return -1;
        return actionResult("window-swap-with", Config::Actions::swapWith(other, actionWindow(id)));
    }

    // filter: 0 = all, 1 = tiled only, 2 = floating only
    static int hlSchemeWindowCycle(int id, int next, int filter) {
        if (!g_up)
            return -1;
        std::optional<bool> tiled, floating;
        if (filter == 1)
            tiled = true;
        else if (filter == 2)
            floating = true;
        return actionResult("window-cycle", Config::Actions::cycleNext(next != 0, tiled, floating, actionWindow(id)));
    }

    static int hlSchemeWindowCenter(int id) {
        if (!g_up)
            return -1;
        return actionResult("window-center", Config::Actions::center(actionWindow(id)));
    }

    static int hlSchemeWindowResizePx(int id, double w, double h, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-resize", Config::Actions::resize(Vector2D{w, h}, relative != 0, actionWindow(id)));
    }

    static int hlSchemeWindowMovePx(int id, double x, double y, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-move", Config::Actions::move(Vector2D{x, y}, relative != 0, actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowFloatAct(int id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-float", Config::Actions::floatWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowPinAct(int id, int act) {        if (!g_up)
            return -1;
        return actionResult("window-pin", Config::Actions::pinWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowPseudo(int id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-pseudo", Config::Actions::pseudoWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowKill(int id) {
        if (!g_up)
            return -1;
        return actionResult("window-kill", Config::Actions::killWindow(actionWindow(id)));
    }

    static int hlSchemeWindowSignal(int id, int sig) {
        if (!g_up)
            return -1;
        return actionResult("window-signal", Config::Actions::signalWindow(sig, actionWindow(id)));
    }

    static int hlSchemeWindowZOrder(int id, const char* mode) {
        if (!g_up)
            return -1;
        return actionResult("window-zorder", Config::Actions::alterZOrder(std::string(mode ? mode : ""), actionWindow(id)));
    }

    static int hlSchemeWindowSetProp(int id, const char* prop, const char* val) {
        if (!g_up)
            return -1;
        return actionResult("window-set-prop", Config::Actions::setProp(std::string(prop ? prop : ""), std::string(val ? val : ""), actionWindow(id)));
    }

    static int hlSchemeWindowTag(int id, const char* tag) {
        if (!g_up)
            return -1;
        return actionResult("window-tag", Config::Actions::tag(std::string(tag ? tag : ""), actionWindow(id)));
    }

    static int hlSchemeWindowClearTags(int id) {
        if (!g_up)
            return -1;
        return actionResult("window-clear-tags", Config::Actions::clearTags(actionWindow(id)));
    }

    static int hlSchemeToggleSwallow() {
        if (!g_up)
            return -1;
        return actionResult("toggle-swallow", Config::Actions::toggleSwallow());
    }

    static int hlSchemeGroupToggle(int id) {
        if (!g_up)
            return -1;
        return actionResult("group-toggle", Config::Actions::toggleGroup(actionWindow(id)));
    }

    // explicit set: a no-op when the window is already in the requested
    // state (group() is null iff the window is not a member of a group)
    static int hlSchemeGroupSet(int id, int on) {
        if (!g_up)
            return -1;
        const auto w = actionWindow(id).value_or(nullptr);
        if (!w)
            return -1;
        if ((w->grouping().group() != nullptr) == (on != 0))
            return 0;
        return actionResult("group-set", Config::Actions::toggleGroup(w));
    }

    static int hlSchemeGroupCycle(int id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("group-cycle", Config::Actions::changeGroupActive(prev == 0, actionWindow(id)));
    }

    static int hlSchemeGroupIndex(int id, int index) {
        if (!g_up)
            return -1;
        return actionResult("group-index", Config::Actions::setGroupActive(index, actionWindow(id)));
    }

    static int hlSchemeGroupMoveWindow(int id, int prev) {
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

    static int hlSchemeWindowIntoGroup(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-group", Config::Actions::moveIntoGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowOutOfGroup(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-out-of-group", Config::Actions::moveOutOfGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowIntoOrCreateGroup(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-or-create-group", Config::Actions::moveIntoOrCreateGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowDenyFromGroup(int id, int act) {
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

    static int hlSchemeCursorCorner(int id, int corner) {
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

    static int hlSchemePass(int id) {
        if (!g_up)
            return -1;
        return actionResult("pass", Config::Actions::pass(actionWindow(id)));
    }

    static Input::ModifierMask gestureMods(const char* mods); // defined below

    // mods/key arrive as the same strings binds take (e.g. "SUPER" "F10");
    // resolve to mask + keysym here so the Scheme surface stays string-based
    static int hlSchemeSendShortcut(const char* mods, const char* key, int id) {
        if (!g_up)
            return -1;
        const auto sym = xkb_keysym_from_name(key ? key : "", XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == 0)
            return -1;
        return actionResult("send-shortcut", Config::Actions::pass(gestureMods(mods), sc<uint32_t>(sym), actionWindow(id)));
    }

    static int hlSchemeSendKeyState(const char* mods, const char* key, int state, int id) {
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
    static int hlSchemeWindowFullscreenState(int id, int internalMode, int clientMode, int layoutAware) {
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
    static ptr hlConfigGet(const char* key) {
        if (!g_up)
            return Sfalse;
        auto* val = configValueByKey(key);
        if (!val)
            return Sfalse;
        lua_State* L = configScratch();
        val->push(L);
        std::string out;
        switch (lua_type(L, -1)) {
            case LUA_TNIL: lua_settop(L, 0); return Sfalse;
            case LUA_TBOOLEAN: out = std::string("b\n") + (lua_toboolean(L, -1) ? "1" : "0"); break;
            case LUA_TNUMBER: {
                char buf[64];
                snprintf(buf, sizeof buf, "n\n%.17g", lua_tonumber(L, -1));
                out = buf;
                break;
            }
            case LUA_TSTRING: out = std::string("s\n") + lua_tostring(L, -1); break;
            case LUA_TTABLE: {
                out = "t\n";
                lua_pushnil(L);
                while (lua_next(L, -2) != 0) {
                    std::string k, v;
                    if (lua_type(L, -2) == LUA_TSTRING)
                        k = lua_tostring(L, -2);
                    else
                        k = std::to_string((long long)lua_tointeger(L, -2));
                    switch (lua_type(L, -1)) {
                        case LUA_TNUMBER: {
                            char buf[64];
                            snprintf(buf, sizeof buf, "%.17g", lua_tonumber(L, -1));
                            v = buf;
                            break;
                        }
                        case LUA_TSTRING: v = lua_tostring(L, -1); break;
                        case LUA_TBOOLEAN: v = lua_toboolean(L, -1) ? "1" : "0"; break;
                        default: v = "?"; break;
                    }
                    out += k + "\n" + v + "\n";
                    lua_pop(L, 1);
                }
                break;
            }
            default: lua_settop(L, 0); return Sfalse;
        }
        lua_settop(L, 0);
        return Sstring_utf8(out.c_str(), out.size());
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
    static std::unordered_map<int, SP<Desktop::Rule::IRule>>               g_ruleHandles;
    static int                                                             g_nextRuleId = 1;

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
        g_ruleHandles.clear();
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

    static double hlWindowRuleCommit() {
        if (!g_up || !g_curWindowRule)
            return -1;
        const int id = g_nextRuleId++;
        g_ruleHandles.emplace(id, g_curWindowRule);
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_WINDOW_STATES);
        g_curWindowRule.reset();
        return id;
    }

    static double hlLayerRuleCommit() {
        if (!g_up || !g_curLayerRule)
            return -1;
        const int id = g_nextRuleId++;
        g_ruleHandles.emplace(id, g_curLayerRule);
        Supplementary::refresher()->scheduleRefresh(Config::Supplementary::REFRESH_RULES);
        g_curLayerRule.reset();
        return id;
    }

    static int hlRuleSetEnabled(double id, int enabled) {
        if (!g_up)
            return -1;
        const auto it = g_ruleHandles.find(sc<int>(id));
        if (it == g_ruleHandles.end())
            return -1;
        it->second->setEnabled(enabled != 0);
        return 0;
    }

    static int hlRuleEnabled(double id) {
        const auto it = g_ruleHandles.find(sc<int>(id));
        return (it != g_ruleHandles.end() && it->second->isEnabled()) ? 1 : 0;
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
            const int id = g_nextWindowId++;
            g_windows.emplace(id, PHLWINDOWREF(w));
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
        const int id = g_nextWindowId++;
        g_windows.emplace(id, PHLWINDOWREF(w));
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
            const int id = g_nextWindowId++;
            g_windows.emplace(id, PHLWINDOWREF(candidate));
            return (double)id;
        }
        return -1;
    }

    static ptr monitorIdResult(PHLMONITOR m) {
        if (!m)
            return Sfalse;
        const int id = g_nextMonitorId++;
        g_monitors.emplace(id, PHLMONITORREF(m));
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
        const int id = g_nextWorkspaceId++;
        g_workspaces.emplace(id, PHLWORKSPACEREF(mon->m_activeWorkspace));
        return Sinteger(id);
    }

    static ptr hlActiveSpecialWorkspace() {
        if (!g_up)
            return Sfalse;
        const auto mon = Desktop::focusState()->monitor();
        if (!mon || !mon->m_activeSpecialWorkspace)
            return Sfalse;
        const int id = g_nextWorkspaceId++;
        g_workspaces.emplace(id, PHLWORKSPACEREF(mon->m_activeSpecialWorkspace));
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
        const int id = g_nextWorkspaceId++;
        g_workspaces.emplace(id, PHLWORKSPACEREF(ws));
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
        const int id = g_nextWindowId++;
        g_windows.emplace(id, PHLWINDOWREF(w));
        return Sinteger(id);
    }

    static ptr workspaceHandleResult(PHLWORKSPACE ws) {
        if (!ws)
            return Sfalse;
        const int id = g_nextWorkspaceId++;
        g_workspaces.emplace(id, PHLWORKSPACEREF(ws));
        return Sinteger(id);
    }

    static ptr monitorHandleResult(PHLMONITOR mon) {
        if (!mon)
            return Sfalse;
        const int id = g_nextMonitorId++;
        g_monitors.emplace(id, PHLMONITORREF(mon));
        return Sinteger(id);
    }

    template <typename F>
    static ptr wsGet(int id, F&& fn) {
        if (!g_up)
            return Sfalse;
        const auto ws = workspaceFromId(id);
        if (!ws)
            return Sfalse;
        return fn(ws);
    }

    template <typename F>
    static ptr monGet(int id, F&& fn) {
        if (!g_up)
            return Sfalse;
        const auto mon = monitorFromId(id);
        if (!mon)
            return Sfalse;
        return fn(mon);
    }

    // -- workspace getters
    static ptr hlWorkspaceName(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { const auto& s = ws->displayName(); return Sstring_utf8(s.c_str(), s.size()); });
    }

    static ptr hlWorkspaceAddressableName(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { const auto& s = ws->addressableName(); return Sstring_utf8(s.c_str(), s.size()); });
    }

    static ptr hlWorkspaceNumber(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto n = ws->numberedID();
            return n ? Sinteger(sc<int>(*n)) : Sfalse;
        });
    }

    static ptr hlWorkspaceMonitor(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return monitorHandleResult(ws->m_monitor.lock()); });
    }

    static ptr hlWorkspaceSpecial(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->type() == Workspace::eWorkspaceType::SPECIAL); });
    }

    static ptr hlWorkspaceActive(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto mon = ws->m_monitor.lock();
            return boolResult(mon && (mon->m_activeWorkspace == ws || mon->m_activeSpecialWorkspace == ws));
        });
    }

    static ptr hlWorkspaceVisible(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->visible()); });
    }

    static ptr hlWorkspaceEmpty(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->getWindowCount() == 0); });
    }

    static ptr hlWorkspacePersistent(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto REGULAR = dynamicPointerCast<Workspace::CRegularWorkspace>(ws);
            return boolResult(REGULAR && REGULAR->isPersistent());
        });
    }

    static ptr hlWorkspaceHasUrgent(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(ws->hasUrgentWindow()); });
    }

    static ptr hlWorkspaceHasFullscreen(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return boolResult(Fullscreen::controller()->hasFullscreen(ws)); });
    }

    static ptr hlWorkspaceFullscreenMode(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return Sinteger(sc<int>(Fullscreen::controller()->getFullscreenModes(ws).internal)); });
    }

    static ptr hlWorkspaceFullscreenWindow(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return windowHandleResult(Fullscreen::controller()->getFullscreenWindow(ws)); });
    }

    static ptr hlWorkspaceLastWindow(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return windowHandleResult(ws->getLastFocusedWindow()); });
    }

    static ptr hlWorkspaceWindowCount(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return Sinteger(ws->getWindowCount()); });
    }

    static ptr hlWorkspaceGroupCount(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr { return Sinteger(ws->getGroups()); });
    }

    // windows on the workspace as newline-joined window-handle ids

    static ptr hlWorkspaceTiledLayout(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            std::string layoutName = "unknown";
            const auto  SPACE      = ws->space();
            if (SPACE && SPACE->algorithm() && SPACE->algorithm()->tiledAlgo())
                layoutName = Layout::Supplementary::algoMatcher()->getNameForTiledAlgo(SPACE->algorithm()->tiledAlgo().get());
            return Sstring_utf8(layoutName.c_str(), layoutName.size());
        });
    }

    static ptr hlWorkspaceAlive(int id) {
        return boolResult(g_up && workspaceFromId(id) != nullptr);
    }

    // identity, mirroring hl-window=?: true iff both handles lock to the same
    // live workspace; dead handles are never "the same" as anything
    static ptr hlWorkspaceSame(int a, int b) {
        const auto wa = g_up ? workspaceFromId(a) : nullptr;
        const auto wb = g_up ? workspaceFromId(b) : nullptr;
        return boolResult(wa && wb && wa.get() == wb.get());
    }

    // -- monitor getters
    static ptr hlMonitorName(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sstring_utf8(mon->m_name.c_str(), mon->m_name.size()); });
    }

    static ptr hlMonitorDescription(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sstring_utf8(mon->m_description.c_str(), mon->m_description.size()); });
    }

    static ptr hlMonitorNumber(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_id)); });
    }

    static ptr hlMonitorEnabled(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_enabled); });
    }

    static ptr hlMonitorFocused(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(Desktop::focusState()->monitor() == mon); });
    }

    static ptr hlMonitorX(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_position.x)); });
    }

    static ptr hlMonitorY(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_position.y)); });
    }

    static ptr hlMonitorWidth(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_size.x)); });
    }

    static ptr hlMonitorHeight(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_size.y)); });
    }

    static ptr hlMonitorScale(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sflonum(sc<double>(mon->m_scale)); });
    }

    static ptr hlMonitorTransform(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sinteger(sc<int>(mon->m_transform)); });
    }

    static ptr hlMonitorRefreshRate(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return Sflonum(sc<double>(mon->m_refreshRate)); });
    }

    static ptr hlMonitorMode(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            const auto s = std::format("{}x{}@{}", sc<int>(mon->m_size.x), sc<int>(mon->m_size.y), mon->m_refreshRate);
            return Sstring_utf8(s.c_str(), s.size());
        });
    }

    static ptr hlMonitorDpms(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_dpmsStatus); });
    }

    static ptr hlMonitorVrr(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_vrrActive != 0); });
    }

    static ptr hlMonitor10bit(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return boolResult(mon->m_enabled10bit); });
    }

    // reserved area as a k\nv\n-encoded map (schemed into an alist); all-zero
    // when nothing is reserved
    static ptr hlMonitorReserved(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr {
            const auto& r = mon->m_reservedArea;
            const auto  s = std::format("top\n{}\nleft\n{}\nright\n{}\nbottom\n{}\n", r.top(), r.left(), r.right(), r.bottom());
            return Sstring_utf8(s.c_str(), s.size());
        });
    }

    // the monitor this one mirrors, as a handle; #f when not a mirror
    static ptr hlMonitorMirrorOf(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return monitorHandleResult(mon->m_mirrorOf.lock()); });
    }

    static ptr hlMonitorActiveWorkspace(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return workspaceHandleResult(mon->m_activeWorkspace); });
    }

    static ptr hlMonitorActiveSpecialWorkspace(int id) {
        return monGet(id, [](PHLMONITOR mon) -> ptr { return workspaceHandleResult(mon->m_activeSpecialWorkspace); });
    }

    static ptr hlMonitorAlive(int id) {
        return boolResult(g_up && monitorFromId(id) != nullptr);
    }

    static ptr hlMonitorSame(int a, int b) {
        const auto ma = g_up ? monitorFromId(a) : nullptr;
        const auto mb = g_up ? monitorFromId(b) : nullptr;
        return boolResult(ma && mb && ma.get() == mb.get());
    }

    // -- selector bridges: handles are accepted anywhere a selector string is,
    // resolved through the canonical selector exactly like upstream's
    // *SelectorOrObject helpers (LuaBindingsInternal.cpp)
    static ptr hlWorkspaceSelector(int id) {
        return wsGet(id, [](PHLWORKSPACE ws) -> ptr {
            const auto s = Workspace::selector(*ws);
            return Sstring_utf8(s.c_str(), s.size());
        });
    }

    static ptr hlMonitorSelector(int id) {
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
        std::string joined;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w->mapped() || w->m_workspace != ws)
                continue;
            const int id = g_nextWindowId++;
            g_windows.emplace(id, PHLWINDOWREF(w));
            if (!joined.empty())
                joined += '\n';
            joined += std::to_string(id);
        }
        if (joined.empty())
            return Sfalse;
        return Sstring_utf8(joined.c_str(), joined.size());
    }

    // layer surfaces as "monitor\nnamespace\n" pairs
    static ptr hlLayers() {
        if (!g_up)
            return Sfalse;
        std::string out;
        for (const auto& mon : State::monitorState()->monitors()) {
            for (const auto& level : mon->m_layerSurfaceLayers) {
                for (const auto& lsRef : level) {
                    const auto ls = lsRef.lock();
                    if (!ls)
                        continue;
                    out += mon->m_name + "\n" + ls->m_namespace + "\n";
                }
            }
        }
        if (out.empty())
            return Sfalse;
        return Sstring_utf8(out.c_str(), out.size());
    }

    static int hlIsKeyDown(const char* key) {
        if (!g_up || !Keybinds::mgr() || !key || !*key)
            return 0;
        const auto sym = xkb_keysym_from_name(key, XKB_KEYSYM_NO_FLAGS);
        if (sym == XKB_KEY_NoSymbol)
            return 0;
        return Keybinds::mgr()->inputState().isKeysymDown(sym) ? 1 : 0;
    }

    static ptr hlLoadedPlugins() {
        if (!g_up || !g_pPluginSystem)
            return Sfalse;
        std::string out;
        for (const auto* plugin : g_pPluginSystem->getAllPlugins()) {
            if (!out.empty())
                out += '\n';
            out += plugin->m_name;
        }
        if (out.empty())
            return Sfalse;
        return Sstring_utf8(out.c_str(), out.size());
    }

    static ptr hlVersion() {
        return Sstring_utf8(HYPRLAND_VERSION, strlen(HYPRLAND_VERSION));
    }

    // windows matching a selector: newline-joined handle ids (like hl-windows)
    static ptr hlWindowsFrom(const char* sel) {
        if (!g_up)
            return Sfalse;
        const std::string selector = sel ? sel : "";
        std::string       joined;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!windowMatchesSelector(w, selector))
                continue;
            const int id = g_nextWindowId++;
            g_windows.emplace(id, PHLWINDOWREF(w));
            if (!joined.empty())
                joined += '\n';
            joined += std::to_string(id);
        }
        if (joined.empty())
            return Sfalse;
        return Sstring_utf8(joined.c_str(), joined.size());
    }

    static ptr hlWindowFullscreenHandler(int id) {
        if (!g_up)
            return Sfalse;
        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;
        const auto name = Fullscreen::controller()->getFullscreenHandlerNameAsString(window);
        return Sstring_utf8(name.c_str(), name.size());
    }

    // ---- notifications ---------------------------------------------------------

    static ptr hlNotify(const char* text, double durationMs, const char* icon, const char* color, double fontSize) {
        if (!g_up)
            return Sfalse;

        // icon names, mirroring the lua config's table
        eIcons  theIcon = ICON_NONE;
        const std::string ic = icon ? icon : "";
        const std::pair<const char*, eIcons> ICON_NAMES[] = {
            {"warning", ICON_WARNING}, {"warn", ICON_WARNING},     {"info", ICON_INFO},       {"hint", ICON_HINT},
            {"error", ICON_ERROR},     {"err", ICON_ERROR},        {"confused", ICON_CONFUSED},
            {"question", ICON_CONFUSED}, {"ok", ICON_OK},           {"none", ICON_NONE},
        };
        for (const auto& [n, i] : ICON_NAMES)
            if (ic == n) {
                theIcon = i;
                break;
            }

        // color: config hex form "0xAARRGGBB" (decimal digits also accepted)
        CHyprColor col(0);
        const std::string cs = color ? color : "";
        if (!cs.empty()) {
            try {
                col = CHyprColor(std::stoull(cs.starts_with("0x") || cs.starts_with("0X") ? cs.substr(2) : cs, nullptr, 16));
            } catch (...) {
                g_configError = "hl-notify!: bad color (expected 0xAARRGGBB)";
                return Sfalse;
            }
        }

        Notification::overlay()->addNotification(text ? text : "", col, sc<float>(durationMs), theIcon, sc<float>(fontSize));
        return Strue;
    }

    // ---- timer handles ----------------------------------------------------------

    static std::unordered_map<int, uint64_t> g_timerMs;

    static SP<CEventLoopTimer> timerById(int id) {
        for (const auto& [tid, t] : g_timers)
            if (tid == id)
                return t;
        return nullptr;
    }

    static int hlTimerSetEnabled(double id, int enabled) {
        if (!g_up)
            return -1;
        const auto timer = timerById(sc<int>(id));
        if (!timer)
            return -1;
        if (enabled != 0) {
            const auto it = g_timerMs.find(sc<int>(id));
            timer->updateTimeout(std::chrono::milliseconds(it != g_timerMs.end() ? sc<int64_t>(it->second) : 1));
        } else
            timer->updateTimeout(std::nullopt);
        return 0;
    }

    static int hlTimerEnabled(double id) {
        const auto timer = timerById(sc<int>(id));
        return (timer && timer->armed()) ? 1 : 0;
    }

    static int hlTimerSetTimeout(double id, double ms) {
        if (!g_up)
            return -1;
        const auto timer = timerById(sc<int>(id));
        if (!timer || ms < 1)
            return -1;
        g_timerMs[sc<int>(id)] = sc<uint64_t>(ms);
        timer->updateTimeout(std::chrono::milliseconds(sc<int64_t>(ms)));
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

    class CSchemeGesture : public ITrackpadGesture {
      public:
        CSchemeGesture(int actionId, int beginId, int updateId, int endId) :
            m_actionId(actionId), m_beginId(beginId), m_updateId(updateId), m_endId(endId) {}

        void  begin(const STrackpadGestureBegin& e) override {
            if (m_beginId >= 0)
                fireSchemeBind(m_beginId);
        }
        void  update(const STrackpadGestureUpdate& e) override {
            if (m_updateId < 0)
                return;
            float dx = 0, dy = 0;
            if (e.swipe) {
                dx = e.swipe->delta.x;
                dy = e.swipe->delta.y;
            }
            fireSchemeStr(m_updateId, std::format("{} {} {}", dx, dy, e.scale));
        }
        void  end(const STrackpadGestureEnd& e) override {
            if (m_endId >= 0)
                fireSchemeBind(m_endId);
            else if (m_actionId >= 0)
                fireSchemeBind(m_actionId);
        }

      private:
        int m_actionId, m_beginId, m_updateId, m_endId;
    };

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

    // (fingers, direction, live?, mods, scale, disableInhibit) → the allocated
    // handler ids as "a" (simple) or "b u e" (live), space-separated; #f on error
    static ptr hlSchemeGesture(int fingers, const char* direction, int live, const char* mods, double scale, int disableInhibit) {
        if (!g_up || !g_pTrackpadGestures)
            return Sfalse;
        const auto dir = g_pTrackpadGestures->dirForString(direction ? direction : "");
        if (dir == TRACKPAD_GESTURE_DIR_NONE) {
            g_configError = std::string("hl-gesture: invalid direction '") + (direction ? direction : "") + "'";
            return Sfalse;
        }
        const int a = g_nextBindId++;
        int       b = -1, u = -1, e = -1;
        if (live) {
            b = g_nextBindId++;
            u = g_nextBindId++;
            e = g_nextBindId++;
            g_pTrackpadGestures->addGesture(
                makeUnique<CSchemeGesture>(-1, b, u, e), sc<size_t>(fingers), dir, gestureMods(mods), sc<float>(scale), disableInhibit != 0);
            const auto ids = std::to_string(b) + "\n" + std::to_string(u) + "\n" + std::to_string(e);
            return Sstring_utf8(ids.c_str(), ids.size());
        }
        g_pTrackpadGestures->addGesture(makeUnique<CSchemeGesture>(a, -1, -1, -1), sc<size_t>(fingers), dir, gestureMods(mods), sc<float>(scale),
                                        disableInhibit != 0);
        return Sstring_utf8(std::to_string(a).c_str(), std::to_string(a).size());
    }


    static ptr hlSchemeWindowInitialClass(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().initialAppID();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static ptr hlSchemeWindowInitialTitle(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().initialTitle();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static int hlSchemeWindowX11(int id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->backend().isX11()) ? 1 : 0;
    }

    static ptr hlSchemeMonitorNames() {
        if (!g_up)
            return Sfalse;

        std::string joined;
        for (const auto& m : State::monitorState()->monitors()) {
            const int id = g_nextMonitorId++;
            g_monitors.emplace(id, PHLMONITORREF(m));
            if (!joined.empty())
                joined += '\n';
            joined += std::to_string(id);
        }

        if (joined.empty())
            return Sfalse;

        return Sstring_utf8(joined.c_str(), joined.size());
    }

    static int hlSchemeWindowEventListen(int which) {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;

        switch (which) {
            case 0: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.openLate.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 1: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.close.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 2: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.title.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 3: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.class_.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 4: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.urgent.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 5: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.pin.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 6: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.fullscreen.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 7: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.moveToWorkspace.listen([id](PHLWINDOW w, PHLWORKSPACE ws) { fireSchemeWin(id, w); })); break;
            case 8: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.active.listen([id](PHLWINDOW w, Desktop::eFocusReason) { fireSchemeWin(id, w); })); break;
            case 9: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.openEarly.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 10: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.kill.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 11: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.bell.listen([id](PHLWINDOW w, Event::SCallbackInfo&) { fireSchemeWin(id, w); })); break;
            case 12: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.updateRules.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            default: break;
        }

        return id;
    }

    // minimize fires with (window, state): pass the bool as a second arg
    static int hlSchemeWindowMinimizeListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_windowEventListeners.emplace_back(Event::bus()->m_events.window.minimize.listen([id](PHLWINDOW w, bool state) {
            if (!g_up || !w)
                return;
            const int winId = g_nextWindowId++;
            g_windows.emplace(winId, PHLWINDOWREF(w));
            Scall3(Stop_level_value(Sstring_to_symbol("hl--fire-win-state")), Sinteger(id), Sinteger(winId), state ? Strue : Sfalse);
        }));
        return id;
    }

    // lifecycle: 0 = start (session's first render frame), 1 = shutdown
    // (the exit action). Matches upstream: a handler registered after start
    // already fired (only possible when the plugin itself loaded before the
    // first frame) runs on the next loop pass.
    static int hlSchemeLifecycleListen(int which) {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;

        if (which == 0) {
            if (g_startSeen) {
                // the handler is registered by Scheme only AFTER this call
                // returns, so the immediate fire must wait for the next pass
                if (g_pEventLoopManager)
                    g_pEventLoopManager->doLater([id] { fireScheme(id); });
            } else
                g_pendingStart.emplace_back(id);
        } else
            g_windowEventListeners.emplace_back(Event::bus()->m_events.exit.listen([id] { fireScheme(id); }));

        return id;
    }

    static int hlSchemeMonitorListen(int which) {
        if (!g_up)
            return -1;
        const int id = g_nextBindId++;
        switch (which) {
            case 0: g_windowEventListeners.emplace_back(Event::bus()->m_events.monitor.added.listen([id](PHLMONITOR m) { fireSchemeMon(id, m); })); break;
            case 1: g_windowEventListeners.emplace_back(Event::bus()->m_events.monitor.removed.listen([id](PHLMONITOR m) { fireSchemeMon(id, m); })); break;
            case 2: g_windowEventListeners.emplace_back(Event::bus()->m_events.monitor.focused.listen([id](PHLMONITOR m) { fireSchemeMon(id, m); })); break;
            case 3: g_windowEventListeners.emplace_back(Event::bus()->m_events.monitor.layoutChanged.listen([id] { fireScheme(id); })); break;
            default: break;
        }
        return id;
    }

    static int hlSchemeWorkspaceListen(int which) {
        if (!g_up)
            return -1;
        const int id = g_nextBindId++;
        switch (which) {
            case 0: g_windowEventListeners.emplace_back(Event::bus()->m_events.workspace.created.listen([id](PHLWORKSPACEREF ws) { auto w = ws.lock(); if (w) fireSchemeWs(id, w); })); break;
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
            case 1: g_windowEventListeners.emplace_back(Event::bus()->m_events.workspace.removed.listen([id](PHLWORKSPACEREF ws) { fireSchemeWsRef(id, ws); })); break;
            // fires (ws mon) handles; ws is #f when no special workspace is
            // open on the monitor (upstream crosses nil the same way)
            case 2: g_windowEventListeners.emplace_back(Event::bus()->m_events.workspace.specialActive.listen([id](PHLWORKSPACE ws, PHLMONITOR mon) { fireSchemeWsMon(id, ws, mon); })); break;
            case 3: g_windowEventListeners.emplace_back(Event::bus()->m_events.workspace.moveToMonitor.listen([id](PHLWORKSPACE ws, PHLMONITOR mon) { fireSchemeWsMon(id, ws, mon); })); break;
            default: break;
        }
        return id;
    }

    static int hlSchemeConfigReloadedListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_windowEventListeners.emplace_back(Event::bus()->m_events.config.reloaded.listen([id] { fireScheme(id); }));
        return id;
    }

    // config.preReload — upstream maps config.unload onto it
    static int hlSchemeConfigUnloadListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_windowEventListeners.emplace_back(Event::bus()->m_events.config.preReload.listen([id] { fireScheme(id); }));
        return id;
    }

    // window.destroy: zero-argument callback (upstream delivers nil — the bus
    // event is Event<PHLWINDOWREF>; identity belongs to hl-on-window-close)
    static int hlSchemeWindowDestroyListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_windowEventListeners.emplace_back(Event::bus()->m_events.window.destroy.listen([id](PHLWINDOWREF) { fireScheme(id); }));
        return id;
    }

    // layer.opened / layer.closed — callbacks receive the layer's namespace
    static int hlSchemeLayerListen(int which) {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        if (which == 0)
            g_windowEventListeners.emplace_back(Event::bus()->m_events.layer.opened.listen([id](PHLLS ls) { if (g_up && ls) fireSchemeStr(id, ls->m_namespace); }));
        else
            g_windowEventListeners.emplace_back(Event::bus()->m_events.layer.closed.listen([id](PHLLS ls) { if (g_up && ls) fireSchemeStr(id, ls->m_namespace); }));
        return id;
    }

    // handler receives #t when the prop refresh ran as scheduled, #f when it
    // was executed prematurely
    static int hlSchemePropsRefreshedListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_windowEventListeners.emplace_back(Event::bus()->m_events.config.props_refreshed.listen([id](const bool scheduled) { fireSchemeBool(id, scheduled); }));
        return id;
    }

    // identity, mirroring Lua's windowEq: two handles are the same window
    // iff they lock to the same underlying object. Dead handles are never
    // "the same" as anything.
    static int hlSchemeWindowSame(int idA, int idB) {
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
        const auto s   = std::to_string((int)pos.x) + "\n" + std::to_string((int)pos.y);
        return Sstring_utf8(s.c_str(), s.size());
    }

    static int hlSchemeWorkspaceActiveListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_windowEventListeners.emplace_back(Event::bus()->m_events.workspace.active.listen([id](PHLWORKSPACE ws) {
            fireSchemeWs(id, ws);
        }));
        return id;
    }

    static void reloadScheme() {
        if (!g_up || g_configPath.empty())
            return;

        // the config reload cleared the rule engine; drop our rule state so
        // the fresh config run re-registers everything
        clearSchemeRules();

        // lua config reloads clear every bind in the registry; drop our stale handles.
        for (const auto& [id, b] : g_binds) {
            if (Keybinds::mgr())
                Keybinds::mgr()->removeBind(b);
        }
        g_binds.clear();

        // timers from the previous generation must not fire into the new one
        if (g_pEventLoopManager) {
            for (const auto& [id, t] : g_timers) {
                t->cancel();
                g_pEventLoopManager->removeTimer(t);
            }
        }
        g_timers.clear();

        // drop event subscriptions; the new generation re-registers
        g_submapListeners.clear();
        g_windowEventListeners.clear();

        // old-generation window handles die with their generation
        g_windows.clear();

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
        Layouts::clear();
        // remove our binds: the compositor keeps running without us
        for (const auto& [id, b] : g_binds) {
            if (Keybinds::mgr())
                Keybinds::mgr()->removeBind(b);
        }
        g_binds.clear();
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
        Sregister_symbol("hl-scheme-unbind", (void*)hlSchemeUnbind);
        Sregister_symbol("hl-scheme-unbind-key", (void*)hlSchemeUnbindKey);
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
        Sregister_symbol("hl-workspace-windows", (void*)hlWorkspaceWindows);
        Sregister_symbol("hl-layers", (void*)hlLayers);
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
        Sregister_symbol("hl-monitor-mirror-of", (void*)hlMonitorMirrorOf);
        Sregister_symbol("hl-monitor-active-workspace", (void*)hlMonitorActiveWorkspace);
        Sregister_symbol("hl-monitor-active-special-workspace", (void*)hlMonitorActiveSpecialWorkspace);
        Sregister_symbol("hl-monitor-alive", (void*)hlMonitorAlive);
        Sregister_symbol("hl-monitor-same", (void*)hlMonitorSame);
        Sregister_symbol("hl-monitor-selector", (void*)hlMonitorSelector);
        Sregister_symbol("hl-window-fullscreen-handler", (void*)hlWindowFullscreenHandler);
        Sregister_symbol("hl-notify!", (void*)hlNotify);
        Sregister_symbol("hl-timer-set-enabled", (void*)hlTimerSetEnabled);
        Sregister_symbol("hl-timer-enabled", (void*)hlTimerEnabled);
        Sregister_symbol("hl-timer-set-timeout", (void*)hlTimerSetTimeout);
        Sregister_symbol("hl-exec!", (void*)hlSchemeExecRaw);
        Sregister_symbol("hl-exec-shell-with-rules!", (void*)hlSchemeExecWithRules);
        Sregister_symbol("hl-scheme-gesture", (void*)hlSchemeGesture);
        Sregister_symbol("hl-scheme-window-hidden", (void*)hlSchemeWindowHidden);
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
            return Sstring_utf8((g_regSubmap + "\n" + g_regSubmapReset).c_str(), g_regSubmap.size() + 1 + g_regSubmapReset.size());
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
                for (const auto id : pending)
                    fireScheme(id);
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

        // lifecycle: dispatch hl-on-start handlers on the session's first
        // render frame (start fires exactly once, after the first preChecks)
        g_lifecycleListeners.emplace_back(Event::bus()->m_events.start.listen([] {
            g_startSeen = true;
            auto pending = std::move(g_pendingStart);
            g_pendingStart.clear();
            for (const auto id : pending)
                fireScheme(id);
        }));
        reloadScheme();
    }
}

