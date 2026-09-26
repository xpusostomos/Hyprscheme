;; hyprscheme-prelude.scm — the kernel module: the error plumbing, the callback
;; watchdog, the fire trampolines and the custom-layout entry points.
;;
;; `(hyprscheme kernel)` is loaded before the API module, and is the layer the
;; host itself must be able to reach (every name C++ looks up by hand lives
;; here). It uses no export list of its own: the host exports the module whole
;; after registering the gsubrs into it, so the kernel exposes the gsubr surface
;; and its own machinery together, which is the whole of the internal layer.
;;
;; Plain Guile throughout — no dialect shims. Anything the machinery needs
;; that is not in Guile's default environment comes from a standard library
;; below.

(define-module (hyprscheme kernel)
  ;; NOT declarative: this module deliberately SHADOWS Guile's load/eval with
  ;; generation-targeting versions (a config's (load "x.scm") must land in the
  ;; generation, not wherever the interpreter started), and a declarative module
  ;; refuses to shadow an imported binding
  #:declarative? #f
  ;; an explicit export list, not module-export-all!: names added to a module
  ;; after it is defined do NOT reach a module that imports it later, and the
  ;; API module depends on importing this one properly. hl--c-generation is the
  ;; single gsubr the kernel needs — the host defines it here before this file
  ;; is read, so it can be named in the list.
    ;; its own machinery, PLUS every hl--c-* the host registers into it — the
  ;; kernel is the C++ boundary, and every other module imports it to reach
  ;; those. Both lists are generated: the second comes from the C++ side's
  ;; hl::bind<>() registrations.
  #:export (
           eval hl--base-eval hl--base-load hl--bind-fire-rec
           hl--bind-result hl--c-active-monitor
           hl--c-active-special-workspace hl--c-active-title
           hl--c-active-window hl--c-active-workspace hl--c-animation-set
           hl--c-bind hl--c-clear-crashed-lockscreen hl--c-config-begin
           hl--c-config-get hl--c-config-last-error
           hl--c-config-props-refreshed-listen hl--c-config-push-bool
           hl--c-config-push-int hl--c-config-push-num
           hl--c-config-push-str hl--c-config-reloaded-listen
           hl--c-config-set hl--c-config-tbl-key hl--c-config-tbl-open
           hl--c-config-tbl-set-hash hl--c-config-tbl-seti
           hl--c-config-unload-listen hl--c-current-submap
           hl--c-cursor-corner hl--c-cursor-move hl--c-cursor-pos
           hl--c-curve-add hl--c-device-add hl--c-dpms hl--c-enter-submap
           hl--c-event hl--c-event-active hl--c-event-cancel hl--c-exec!
           hl--c-exit hl--c-focus-direction hl--c-focus-last
           hl--c-focus-monitor hl--c-focus-urgent hl--c-focus-workspace
           hl--c-force-idle hl--c-force-renderer-reload hl--c-generation
           hl--c-gesture hl--c-gesture-maker-close
           hl--c-gesture-maker-cursor-zoom hl--c-gesture-maker-custom
           hl--c-gesture-maker-float hl--c-gesture-maker-fullscreen
           hl--c-gesture-maker-move hl--c-gesture-maker-resize
           hl--c-gesture-maker-scroll-move hl--c-gesture-maker-special
           hl--c-gesture-maker-workspace-swipe hl--c-gesture-remove
           hl--c-get-submap-ctx hl--c-global hl--c-group-add
           hl--c-group-alive hl--c-group-current hl--c-group-current-idx
           hl--c-group-cycle hl--c-group-denied hl--c-group-index
           hl--c-group-lock hl--c-group-lock-active hl--c-group-locked
           hl--c-group-members hl--c-group-move-window hl--c-group-remove
           hl--c-group-same hl--c-group-set hl--c-group-size
           hl--c-group-toggle hl--c-group? hl--c-groups-locked
           hl--c-is-key-down hl--c-keyboard-key-listen hl--c-last-window
           hl--c-last-workspace hl--c-layer-above-fs hl--c-layer-address
           hl--c-layer-alive hl--c-layer-kb-interactivity
           hl--c-layer-level hl--c-layer-listen hl--c-layer-mapped
           hl--c-layer-monitor hl--c-layer-namespace hl--c-layer-pid
           hl--c-layer-position hl--c-layer-rule-begin
           hl--c-layer-rule-commit hl--c-layer-rule-effect
           hl--c-layer-same hl--c-layer-size hl--c-layer? hl--c-layers
           hl--c-layout-add hl--c-layout-message hl--c-lifecycle-listen
           hl--c-loaded-plugins hl--c-monitor-10bit
           hl--c-monitor-active-special-workspace
           hl--c-monitor-active-workspace hl--c-monitor-alive
           hl--c-monitor-at hl--c-monitor-at-cursor
           hl--c-monitor-available-modes hl--c-monitor-begin
           hl--c-monitor-commit hl--c-monitor-description
           hl--c-monitor-dpms hl--c-monitor-enabled
           hl--c-monitor-event-listen hl--c-monitor-field-bool
           hl--c-monitor-field-gap hl--c-monitor-field-num
           hl--c-monitor-field-str hl--c-monitor-focused
           hl--c-monitor-from hl--c-monitor-hardware-details
           hl--c-monitor-height hl--c-monitor-mirror-of
           hl--c-monitor-mirrors hl--c-monitor-mode hl--c-monitor-name
           hl--c-monitor-names hl--c-monitor-number
           hl--c-monitor-physical-size hl--c-monitor-refresh-rate
           hl--c-monitor-reserved hl--c-monitor-same hl--c-monitor-scale
           hl--c-monitor-selector hl--c-monitor-serial
           hl--c-monitor-set-special hl--c-monitor-transform
           hl--c-monitor-vrr hl--c-monitor-width hl--c-monitor-x
           hl--c-monitor-y hl--c-monitor? hl--c-mouse
           hl--c-notification-add hl--c-notification-age
           hl--c-notification-alive hl--c-notification-color
           hl--c-notification-color-set hl--c-notification-dismiss
           hl--c-notification-elapsed hl--c-notification-font-size
           hl--c-notification-font-size-set hl--c-notification-icon
           hl--c-notification-icon-set hl--c-notification-list
           hl--c-notification-paused-q hl--c-notification-paused-set
           hl--c-notification-same hl--c-notification-text
           hl--c-notification-text-set hl--c-notification-timeout
           hl--c-notification-timeout-set hl--c-notification?
           hl--c-notify hl--c-pass hl--c-permission-add
           hl--c-release-input-capture hl--c-reload-config
           hl--c-rule-enabled hl--c-rule-match hl--c-rule-set-enabled
           hl--c-scheduled-prop-refresh-immediately
           hl--c-screenshare-listen hl--c-send-key-state
           hl--c-send-shortcut hl--c-set-submap-ctx hl--c-state-get
           hl--c-state-set hl--c-submap-listen hl--c-timer
           hl--c-timer-cancel hl--c-timer-enabled hl--c-timer-set-enabled
           hl--c-timer-set-timeout hl--c-toggle-swallow hl--c-unbind-key
           hl--c-unbind-rec hl--c-urgent-window hl--c-version
           hl--c-window-accepts-input hl--c-window-address
           hl--c-window-alive hl--c-window-allowed-over-fullscreen
           hl--c-window-center hl--c-window-class hl--c-window-clear-tags
           hl--c-window-close hl--c-window-content-type
           hl--c-window-cycle hl--c-window-deny-from-group
           hl--c-window-destroy-listen hl--c-window-event-listen
           hl--c-window-float hl--c-window-float-act
           hl--c-window-floating hl--c-window-focus
           hl--c-window-focus-history-id hl--c-window-from
           hl--c-window-fullscreen-handler hl--c-window-fullscreen-mode
           hl--c-window-fullscreen-set hl--c-window-fullscreen-state
           hl--c-window-fullscreen-toggle hl--c-window-group-denied
           hl--c-window-group-lock hl--c-window-group-locked
           hl--c-window-hidden hl--c-window-ids hl--c-window-in-group
           hl--c-window-inhibiting-idle hl--c-window-initial-class
           hl--c-window-initial-title hl--c-window-into-group
           hl--c-window-into-or-create-group hl--c-window-kill
           hl--c-window-layout hl--c-window-mapped
           hl--c-window-maximized-query hl--c-window-minimize-listen
           hl--c-window-monitor-id hl--c-window-move-direction
           hl--c-window-move-px hl--c-window-move-to-workspace
           hl--c-window-out-of-group hl--c-window-pid
           hl--c-window-pin-act hl--c-window-pin-fullscreened
           hl--c-window-pinned hl--c-window-position hl--c-window-prop
           hl--c-window-pseudo hl--c-window-pseudo-query
           hl--c-window-resize-px hl--c-window-rule-begin
           hl--c-window-rule-commit hl--c-window-rule-effect
           hl--c-window-same hl--c-window-set-prop hl--c-window-signal
           hl--c-window-size hl--c-window-stable-id
           hl--c-window-swallowing-id hl--c-window-swap-direction
           hl--c-window-swap-next hl--c-window-swap-with hl--c-window-tag
           hl--c-window-tags hl--c-window-tearing-hint hl--c-window-title
           hl--c-window-visible hl--c-window-workspace-id
           hl--c-window-x11 hl--c-window-xdg-description
           hl--c-window-xdg-tag hl--c-window-zorder hl--c-window?
           hl--c-windows-from hl--c-workspace-active
           hl--c-workspace-active-listen hl--c-workspace-addressable-name
           hl--c-workspace-alive hl--c-workspace-change-id
           hl--c-workspace-empty hl--c-workspace-event-listen
           hl--c-workspace-fullscreen-mode
           hl--c-workspace-fullscreen-window hl--c-workspace-group-count
           hl--c-workspace-groups hl--c-workspace-has-fullscreen
           hl--c-workspace-has-urgent hl--c-workspace-last-window
           hl--c-workspace-monitor hl--c-workspace-move-monitor
           hl--c-workspace-name hl--c-workspace-names
           hl--c-workspace-number hl--c-workspace-persistent
           hl--c-workspace-rename hl--c-workspace-rule-begin
           hl--c-workspace-rule-bool hl--c-workspace-rule-commit
           hl--c-workspace-rule-gap hl--c-workspace-rule-layout-opt
           hl--c-workspace-rule-num hl--c-workspace-rule-str
           hl--c-workspace-same hl--c-workspace-selector
           hl--c-workspace-special hl--c-workspace-swap-monitors
           hl--c-workspace-tiled-layout hl--c-workspace-toggle-special
           hl--c-workspace-visible hl--c-workspace-window-count
           hl--c-workspace-windows hl--c-workspace? hl--error hl--eval
           hl--event-fire hl--event-thunk hl--event-type
           hl--fire-bool-rec hl--fire-list hl--fire-list-rec
           hl--fire-mon-rec hl--fire-str-rec hl--fire-win-rec
           hl--fire-win-state-rec hl--fire-ws-mon-rec hl--fire-ws-rec
           hl--guarded-run hl--layout-call hl--layout-msg
           hl--layout-recalculate hl--layout-resize
           hl--layout-window-close hl--layout-window-open hl--load
           hl--now-ms hl--plist-cdr hl--plist-get hl--print-exception
           hl--ready hl--record-field hl--report hl--target-env
           hl--timer-fire hl--timer-interval hl--timer-thunk
           hl--watchdog-ms hl--wd-aborted hl--wd-alarm hl--wd-enter
           hl--wd-exit hl--wd-handler hl--wd-handler-set hl--wd-rearm
           hl--wd-stack hl-bind-rtd hl-bind-thunk hl-bind-tokens
           hl-event-rtd hl-rule-rtd hl-timer-rtd load))

(use-modules (ice-9 exceptions)   ; guard, make-exception, exception-* accessors
             (rnrs io ports)      ; call-with-string-output-port
             (rnrs lists)         ; exists, for-all — configs and tests use them
             (srfi srfi-1)        ; any, every, cons*, filter
             (ice-9 optargs))     ; define*

(define hl--ready #f)

;; THIS FILE IS REBUILT ON EVERY CONFIG RELOAD. The host throws the whole
;; Scheme side away and evaluates the prelude, the API and the config into a
;; fresh module — so a config's definitions, and anything it set!, are gone
;; next reload with no bookkeeping. The host owns the generation module and
;; installs it as the *current* module before loading this file, which is why
;; nothing here carries an environment argument or a generation pointer.
;;
;; Exactly three things deliberately live outside that wipe, all in the host:
;; the stderr port (a port owns its fd), the after-gc hook (it must not stack),
;; and the survive list behind hl-state-set! and friends.

;; ---- error reporting ---------------------------------------------------------
;; The compositor's own stderr is where scheme errors must land. The port is
;; installed ONCE by the host (Guile.cpp), not here: a port owns the fd it
;; wraps, so re-creating it per reload would close fd 2 as soon as the previous
;; port was collected — silently losing every scheme error. The host also wraps
;; the existing fd 2 rather than opening /dev/stderr afresh, which would give
;; the log a SECOND write offset (error lines then overwrite the beginning of
;; the file — a real bug once).

;; Raise a formatted error that KEEPS its origin: `who` is the function that
;; rejected the argument, and the renderer below prints it. (Guile's `error`
;; would carry it too, but building the condition explicitly leaves the
;; message formatted and the irritants available for a later backtrace pass.)
(define (hl--error who fmt . args)
  (raise-exception (make-exception (make-error)
                                   (make-exception-with-origin who)
                                   (make-exception-with-message (apply format #f fmt args))
                                   (make-exception-with-irritants args))))

;; Render an exception to PORT. Two shapes arrive here:
;;   - our own hl--error conditions: a formatted message plus an origin
;;   - Guile's own conditions, whose message may still be a "~A"-style
;;     TEMPLATE with the arguments as irritants (the default handler formats
;;     them; exception-message hands back the raw template) — render it the
;;     same way, guarded so a bad escape cannot kill the report
(define (hl--print-exception e port)
  (let ((msg (guard (e2 (#t #f)) (exception-message e)))
        (irr (guard (e2 (#t #f)) (exception-irritants e)))
        (who (guard (e2 (#t #f)) (exception-origin e))))
    (when who
      (display who port)
      (display ": " port))
    (cond ((not (string? msg)) (format port "~s" e))
          ((and (pair? irr) (string-index msg #\~))
           (guard (e3 (#t (display msg port)))
             (apply simple-format port msg irr)))
          (else (display msg port)))))

(define (hl--report e)
  (display "[scheme] error: " (current-error-port))
  (hl--print-exception e (current-error-port))
  (newline (current-error-port))
  #f)

;; ---- wall clock --------------------------------------------------------------
;; Milliseconds since the epoch, used for the watchdog's budget (a wall-clock
;; measurement, not a CPU one: the point is how long the desktop has been
;; frozen).
(define (hl--now-ms)
  (let ((now (gettimeofday)))
    (+ (* (car now) 1000) (quotient (cdr now) 1000))))

;; ---- watchdog ----------------------------------------------------------------
;; Callbacks run on the compositor's main loop: a hung callback freezes the
;; desktop, and nothing outside the process can safely kill a running Guile
;; call. This wrapper bounds them instead: SIGALRM fires on a fixed cadence,
;; the handler checks the WALL-CLOCK budget and escapes out of the callback
;; when it is exceeded — recovery, not just detection. Abandonment semantics
;; match the Lua watchdog's: partial effects stay, the callback is never
;; resumed.
;;
;; Foreign calls are not interruptible (Guile delivers asyncs at procedure
;; entry points; a SIGALRM landing inside compositor C++ crashed it once), so
;; a callback blocked INSIDE a C call escapes only when control returns to
;; Scheme; the host's C-side detector thread is the backstop for those.

(define hl--watchdog-ms 5000)   ; set! from your config; 0 disables
(define hl--wd-rearm 10000)     ; alarm ticks between wall-clock checks
(define hl--wd-stack '())
;; unique marker for "the watchdog abandoned this run"
(define hl--wd-aborted (cons 'watchdog 'aborted))

(define hl--wd-handler #f)

;; (Re)install the SIGALRM delivery path. One handler at a time: the async
;; calls whatever is currently installed.
(define (hl--wd-handler-set f)
  (set! hl--wd-handler f)
  (sigaction SIGALRM (lambda (sig) (when hl--wd-handler (hl--wd-handler)))))

;; Arm the alarm cadence. The argument is in "ticks" (the watchdog's own unit,
;; 10000 to the second); 0 disarms. Returns its argument, so callers can
;; save/restore a previous value.
(define (hl--wd-alarm ticks)
  (alarm (if (and (number? ticks) (> ticks 0))
             (max 1 (quotient ticks 10000))
             0))
  ticks)

(define (hl--wd-enter what escape)
  ;; budget + t0 + escape are captured in the handler's closure — the handler
  ;; fires asynchronously and must not read shared state. The budget is read
  ;; straight out of this closure's environment, which IS the config's
  ;; generation (this file is evaluated into it), so a config's set! is seen.
  (let* ((budget hl--watchdog-ms)
         (t0 (hl--now-ms))
         (old-handler hl--wd-handler)
         (old-ticks (hl--wd-alarm hl--wd-rearm)))
    (set! hl--wd-stack (cons (list old-handler old-ticks) hl--wd-stack))
    (hl--wd-handler-set
      (lambda ()
        (let ((ms (- (hl--now-ms) t0)))
          (if (> ms budget)
              (begin
                (hl--report (format #f "watchdog: ~a abandoned after ~ams" what ms))
                (escape hl--wd-aborted))
              (hl--wd-alarm hl--wd-rearm)))))))

(define (hl--wd-exit)
  (let ((outer (car hl--wd-stack)))
    (set! hl--wd-stack (cdr hl--wd-stack))
    (hl--wd-alarm 0)
    (hl--wd-handler-set (car outer))
    (let ((old-ticks (cadr outer)))
      (if (and (number? old-ticks) (> old-ticks 0))
          (hl--wd-alarm old-ticks)
          (hl--wd-alarm 0)))))

;; runs THUNK under the watchdog; returns its value, or the unique
;; hl--wd-aborted marker if the budget was exceeded. Reentrant: nested
;; dispatches save/restore the outer alarm state.
(define (hl--guarded-run what thunk)
  (call/cc
    (lambda (escape)
      (dynamic-wind
        (lambda () (hl--wd-enter what escape))
        (lambda () (thunk))
        (lambda () (hl--wd-exit))))))

;; ---- the record machinery the trampolines below read ----------------------
;; The fire trampolines pull a user thunk out of a record, so the record
;; TYPES and their accessors live here rather than with the rest of the API:
;; the API imports the kernel, so a kernel reaching back into the API would
;; be a cycle. Keeping the types here also makes them stable identities for
;; the process, so a record made in one generation stays readable by any
;; other. (hl--plist-get lives here for the same reason: the layout entry
;; points below look callbacks up in a spec plist.)

(define (hl--record-field who rtd idx x)
  (if (and (struct? x) (eq? (struct-vtable x) rtd))
      (struct-ref x idx)
      (hl--error who "not a ~a: ~s" (record-type-name rtd) x)))

;; ---- handles ----------------------------------------------------------------
;; A window/workspace/monitor/group/layer/notification handle is a Guile
;; FOREIGN OBJECT built by the C++ side (see Handles.hpp): the object IS the
;; handle, its family is its type, and its lifetime is a finalizer. So nothing
;; here mints, wraps or drains handles — the predicates below are the whole of
;; the handle machinery in Scheme.

;; rules carry a kind and a name; (hyprscheme core) and the rule family
;; both read them, so the type lives here with the others
(define hl-rule-rtd (make-record-type 'hl-rule '(kind name)))

(define hl-event-rtd (make-record-type 'hl-event '(type thunk)))

(define (hl--event-type b) (hl--record-field 'hl--event-type hl-event-rtd 0 b))

(define (hl--event-thunk b) (hl--record-field 'hl--event-thunk hl-event-rtd 1 b))

;; timers: the record IS the handle — the initial interval and the thunk.
;; C++ locks it inside the timer's fire callback; a one-shot timer releases
;; its own lock when it completes, and reload tears the rest down.

(define hl-timer-rtd (make-record-type 'hl-timer '(interval thunk)))

(define (hl--timer-interval t) (hl--record-field 'hl--timer-interval hl-timer-rtd 0 t))

(define (hl--timer-thunk t) (hl--record-field 'hl--timer-thunk hl-timer-rtd 1 t))

;; binds: the record IS the handle — the token LIST (the bind's meaning)
;; and the thunk. C++ locks the record while the bind is registered (see
;; SThunkRef) and stamps its pinned address into the bind's argument tag;
;; the record becomes collectable when the bind is unbound (or dies at
;; reload) and the user drops it. No bind ids, no registries.

(define hl-bind-rtd (make-record-type 'hl-bind '(tokens thunk)))

(define (hl-bind-tokens b) (hl--record-field 'hl-bind-tokens hl-bind-rtd 0 b))

(define (hl-bind-thunk b) (hl--record-field 'hl-bind-thunk hl-bind-rtd 1 b))

(define (hl--plist-cdr l)
  ;; (key value rest ...) → (value rest ...); a trailing bare KEY is an error
  (let ((tail (cdr l)))
    (if (null? tail)
        (hl--error 'hl--plist "odd plist: ~s" l)
        tail)))

(define (hl--plist-get pl key default)
  (let loop ((l pl))
    (if (null? l)
        default
        (let ((tail (hl--plist-cdr l)))
          (if (eq? (car l) key)
              (car tail)
              (loop (cdr tail)))))))

;; ---- event fire helpers -------------------------------------------------------
;; every helper receives an hl-event RECORD (carried locked by its bus
;; connection) plus the payload; the handler thunk is extracted from the
;; record. Return semantics unchanged from the id-era helpers.

;; bind-callback result protocol — the full result-table contract: the Lua
;; callback may return a table {ok, pass_event, error, request_release};
;; Scheme returns it as a plist.
;;   #f                        — DECLINED (auto-consuming binds pass the key
;;                               through; repeating timers stop). Errors and
;;                               the watchdog decline too.
;;   #t / any non-plist value  — handled, 'ok defaults to #t
;;   a plist                   — the full form; 'ok filled in when absent
;; Normalized so the bind fire path always reads #f or one plist shape.
(define (hl--bind-result r)
  (cond ((not r) #f)
        ((and (pair? r) (symbol? (car r)))
         (if (hl--plist-has? r 'ok) r (cons* 'ok #t r)))
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
                  (hl--guarded-run "callback" (hl--event-thunk b)))))
    (not (eq? result hl--wd-aborted))))

;; timer-record fire: extract (hl--timer-thunk b) and run it zero-arg under the
;; bind result protocol (repeating timers stop on ok #f)
(define (hl--timer-fire b)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "callback"
                    (lambda () (hl--bind-result ((hl--timer-thunk b))))))))
    (and (not (eq? result hl--wd-aborted)) result)))

(define (hl--fire-list-rec b lst)
  (guard (e (#t (begin (hl--report e) #f)))
    (hl--guarded-run "handler" (lambda () (apply (hl--event-thunk b) lst)))))

(define (hl--fire-str-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-bool-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-ws-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-mon-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-ws-mon-rec b ws mon)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) ws mon))))))
    (not (eq? result hl--wd-aborted))))

;; window payloads: the id crosses and becomes a real window record here
(define (hl--fire-win-rec b window)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) window))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-win-state-rec b window state)
  (let ((result (guard (e (#t (begin (hl--report e) hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) window state))))))
    (not (eq? result hl--wd-aborted))))

;; bare-thunk list fire for GESTURES (the thunk travels directly, no record)
(define (hl--fire-list fn lst)
  (guard (e (#t (begin (hl--report e) #f)))
    (hl--guarded-run "handler" (lambda () (apply fn lst)))))

;; ---- the current generation ---------------------------------------------------
;; The host owns it (Host.cpp) and rebuilds it on every reload; the pointer is
;; behind this gsubr so the machinery can reach it without living there itself.
;; It is #f only before the first generation exists.
(define (hl--target-env)
  (or (hl--c-generation) (interaction-environment)))

;; (load "file") from a config: the file must land in the CURRENT generation,
;; never in the module the plugin happened to start in. Outside a generation it
;; falls through to Guile's own.
;; (@ (guile) ...) rather than the bare name: in a define-module file Guile
;; hoists every top-level define, so `load`/`eval` already refer to THIS file's
;; shadows by the time these run — capturing them would capture the shadow.
(define hl--base-load (@ (guile) load))
(define hl--base-eval (@ (guile) eval))
(define (hl--load path)
  (guard (e (#t (hl--report e)))
    (let ((gen (hl--c-generation)))
      (if gen
          (save-module-excursion
            (lambda ()
              (set-current-module gen)
              (primitive-load path)))
          (hl--base-load path)))
    #t))
(define (load f) (hl--load f))

;; (eval x) from a config: into the generation, so it sees the config's own
;; definitions — and dies with it, like the next reload's fresh environment.
(define (eval x . o) (hl--base-eval x (if (null? o) (hl--target-env) (car o))))

;; hyprctl scheme entry: evaluate all forms in the string, reply with the
;; last value formatted, or the error text. Evals land in the current
;; generation so they see the config's definitions — and die with it, like
;; the next reload's fresh evaluation environment.
(define (hl--eval code)
  (guard (e (#t (call-with-string-output-port
                  (lambda (p)
                    (display "error: " p)
                    (hl--print-exception e p)))))
    (let ((result (hl--guarded-run "eval"
                    (lambda ()
                      (let ((env (hl--target-env)))
                        (let loop ((port (open-input-string code)) (result *unspecified*))
                          (let ((form (read port)))
                            (if (eof-object? form)
                                result
                                (loop port (eval form env))))))))))
      (if (eq? result hl--wd-aborted)
          "watchdog: eval abandoned (see the compositor log)"
          (format #f "~s" result)))))

;; ---- custom layouts -----------------------------------------------------------
;; layout callbacks: spec = a single recalculate fn, or a plist of callbacks
;; (recalculate . fn) (resize . fn) (window-open . fn) (window-close . fn)
;; (layout-msg . fn). recalculate/resize receive (count W H windows) where
;; windows[i] is the handle for box i (a handle whose id 0 means "not a
;; window" — every query on it returns #f), plus (dx dy corner) for resize.
;; fn returns a list of (x y w h) boxes, or #f on error.

;; layout event dispatch. spec is a flat PLIST of callbacks: 'recalculate
;; 'resize 'window-open 'window-close 'layout-msg. recalculate/resize fn:
;; (count W H windows [dx dy corner]) -> ((x y w h) ...); window callbacks:
;; (window) -> ignored; layout-msg fn: (message) -> response string. #f when
;; absent or on error. Five direct entry points (one per callback kind) — no
;; event strings, no dispatching cond.
(define (hl--layout-call spec tag . args)
  (let ((cb (hl--plist-get spec tag #f)))
    (and cb (guard (e (#t (hl--report e)))
              (apply cb args)))))

(define (hl--layout-window-open spec id)
  (hl--layout-call spec 'window-open id))

(define (hl--layout-window-close spec id)
  (hl--layout-call spec 'window-close id))

(define (hl--layout-msg spec msg)
  (let ((r (hl--layout-call spec 'layout-msg msg)))
    (cond ((not r) "rejected")
          ((string? r) r)
          (else ""))))

;; recalculate: payload = (count W H id ...)
(define (hl--layout-recalculate spec payload)
  (hl--layout-call spec 'recalculate (car payload) (cadr payload) (caddr payload)
                   (cdddr payload)))

;; resize: payload = (count W H dx dy corner id ...); a spec without a
;; 'resize callback falls back to 'recalculate (documented behavior)
(define (hl--layout-resize spec payload)
  (let* ((count (list-ref payload 0))
         (W     (list-ref payload 1))
         (H     (list-ref payload 2))
         (dx    (list-ref payload 3))
         (dy    (list-ref payload 4))
         (corner (list-ref payload 5))
         (ids   (list-tail payload 6))
         (cb    (or (hl--plist-get spec 'resize #f) (hl--plist-get spec 'recalculate #f))))
    (and cb
         (guard (e (#t (hl--report e)))
           (cb count W H ids dx dy corner)))))
