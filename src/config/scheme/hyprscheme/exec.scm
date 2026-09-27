;; hyprscheme/exec.scm — the exec family — raw wrappers over the compositor's exec operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme exec)
  #:export (
           hl-event? hl-focus-direction-set! hl-focus-last!
           hl-focus-urgent! hl-cursor-move! hl-cursor-move-to-corner!
           hl-exit! hl-force-renderer-reload! hl-force-idle! hl-global!
           hl-event! hl-mouse-action! hl-clear-crashed-lockscreen!
           hl-exec-scheduled-prop-refresh-immediately
           hl-release-input-capture! hl-layout-msg hl-exec! hl-cursor-pos))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-event? x) (and (struct? x) (eq? (struct-vtable x) hl-event-rtd)))

(define (hl-focus-direction-set! dir)
  "Move the focus in direction DIR (left/right/up/down)."
  (= 0 (hl--c-focus-direction (hl--dir dir))))

(define (hl-focus-last! )
  "Move the focus to the previously-focused window."
  (= 0 (hl--c-focus-last)))

(define (hl-focus-urgent! )
  "Move the focus to an urgent window, or the last one."
  (= 0 (hl--c-focus-urgent)))

(define (hl-cursor-move! x y)
  "Move the cursor to (X, Y)."
  (= 0 (hl--c-cursor-move (exact->inexact x) (exact->inexact y))))

(define (hl-cursor-move-to-corner! w corner)
  "Move the cursor to CORNER (0-3) of WINDOW."
  (= 0 (hl--c-cursor-corner w corner)))

(define (hl-exit! )
  "Quit the compositor."
  (= 0 (hl--c-exit)))

(define (hl-force-renderer-reload! )
  "Force the renderer to reload on all monitors."
  (= 0 (hl--c-force-renderer-reload)))

(define (hl-force-idle! seconds)
  "Pretend SECONDS of idle time have elapsed for all idle timers."
  (= 0 (hl--c-force-idle (exact->inexact seconds))))

(define (hl-global! action)
  "Activate the global shortcut named ACTION (D-Bus GlobalShortcuts; see
   bind-globals)."
  (= 0 (hl--c-global (hl--str action))))

(define (hl-event! data)
  "Send DATA as an event on socket2, visible to other IPC clients."
  (= 0 (hl--c-event (hl--str data))))

(define (hl-mouse-action! action)
  "Begin an interactive mouse action for mouse binds: (hl-mouse-action!
   \"drag\") or (hl-mouse-action! \"resize\")."
  (= 0 (hl--c-mouse (hl--str action))))

(define (hl-clear-crashed-lockscreen! )
  "Manual escape hatch when a lock screen has crashed: clears the
   session lock so the session is usable again. Refused while a lock
   client is attached or the session isn't locked - it can never unlock
   a live lock screen."
  (let ((rc (hl--c-clear-crashed-lockscreen)))
    (if (= 0 rc) #t (hl--error 'hl-clear-crashed-lockscreen! "~a" (hl--c-config-last-error)))))

(define (hl-exec-scheduled-prop-refresh-immediately )
  "Run the config-time deferred refresh pass NOW (rule/prop/layout
   changes schedule it). #t when it executed as scheduled, #f otherwise."
  (= 0 (hl--c-scheduled-prop-refresh-immediately)))

(define (hl-release-input-capture! )
  "Release any active input capture session."
  (= 0 (hl--c-release-input-capture)))

(define (hl-layout-msg msg)
  "Send MSG to the active workspace's layout (see custom layouts in the
   wiki)."
  (= 0 (hl--c-layout-message (hl--str msg))))

;; ---- config -----------------------------------------------------------------

(define* (hl-exec! cmd #:key #:allow-other-keys #:rest effects)
  "The one exec. Spawns CMD asynchronously through the compositor's
executor (shell, env injection; never blocks the config). Keyword
EFFECTS build a one-shot window rule pinned to the spawned window by
pid - e.g. (hl-exec! \"foot\" #:float #t #:workspace \"games\"); the
keys and value forms are the window-rule effects (see window-rules),
with the same coercion as hl-window-rule-add!. With no effects the
command may still carry the legacy inline rule prefix
(\"[float size 800 500] mygame\") - the C++ layer parses it on the
plain path. Returns the new pid."
  (define (flat l)
    (cond ((null? l) '())
          ((null? (cdr l)) (hl--error 'hl-exec! "odd plist of rule effects"))
          (else (cons* (hl--str (car l))
                       (hl--rule-spec-value (cadr l))
                       (flat (cddr l))))))
  (let ((pid (hl--c-exec! (hl--str cmd)
                         (flat (map (lambda (x)
                                      (if (keyword? x) (keyword->symbol x) x))
                                    effects)))))
    (if (> pid 0)
        pid
        (hl--error 'hl-exec! "~a" (hl--c-config-last-error)))))

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

(define (hl-cursor-pos )
  "The cursor position, as (x . y), or #f."
  (hl--c-cursor-pos))   ; (x . y), or #f

;; The reload boundary is the HOST's (Host.cpp): it tears the C++ side down,
;; then rebuilds this whole file — prelude, API and config — into a fresh
;; module. There is no reset to perform here, because nothing here survives to
;; need resetting: same clean-slate semantics as upstream's per-generation
;; lua_State, with the wipe done by rebuilding rather than by copying.

;; cross-reload state: the VALUE lives in the host (hl--c-state-get/-set), not
;; here — a reload rebuilds this whole file, so a list kept in it would be lost.
;; The list manipulation stays here; only the storage is the host's. Note that
;; generation-bound values stored through it (e.g. window handles) still die
;; with their generation — the state survives, the resources do not.

