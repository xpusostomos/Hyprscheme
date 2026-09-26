;; hyprscheme-prelude.scm — the error plumbing, the callback watchdog, the
;; fire trampolines and the custom-layout entry points.
;;
;; Loaded before the API bootstrap, through the host's contained file loader:
;; an error here means scheme scripting is disabled (the C++ side checks
;; hl--ready at the end of the bootstrap).
;;
;; Plain Guile throughout — no dialect shims. Anything the machinery needs
;; that is not in Guile's default environment comes from a standard library
;; below.

(use-modules (ice-9 exceptions)   ; guard, make-exception, exception-* accessors
             (rnrs io ports)      ; call-with-string-output-port
             (rnrs lists)         ; exists, for-all — configs and tests use them
             (srfi srfi-1)        ; any, every, cons*, filter
             (ice-9 optargs))     ; define*

(define hl--ready #f)

;; the current config generation's environment, or #f before the first
;; hl--reset (the bootstrap load targets the persistent interaction
;; environment). hl--reset installs a fresh copy per generation so user
;; definitions from previous generations cannot leak into new ones.
(define hl--generation #f)

;; ---- error reporting ---------------------------------------------------------
;; The compositor's own stderr is where scheme errors must land: Guile's
;; default error port does not reach it in this embedded context, and opening
;; /dev/stderr afresh would give the log a SECOND write offset (error lines
;; then overwrite the beginning of the file — a real bug once). Wrap the
;; existing fd 2 instead: one shared offset with the compositor's own writes,
;; unbuffered so lines appear immediately.
(define hl--stderr-port (fdopen 2 "w"))
(setvbuf hl--stderr-port 'none)
(set-current-error-port hl--stderr-port)

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
  ;; through the CURRENT generation: config and hyprctl set!s land in the
  ;; generation copy, and reading this closure's own defining environment
  ;; would miss them.
  (let* ((budget (if hl--generation
                     (module-ref hl--generation 'hl--watchdog-ms)
                     hl--watchdog-ms))
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

;; ---- generations -------------------------------------------------------------
;; A generation is a fresh module that imports the persistent API: a copy of
;; the working module's own bindings (fresh locations, shared values — so a
;; config's set! stays in its generation) plus the working module's interfaces
;; (so imports — including macros — are visible downstream). hl--state is the
;; one deliberate cross-generation bridge; it lives in the persistent
;; environment.
(define (hl--generation-copy src)
  (let ((dst (make-fresh-user-module)))
    (module-for-each (lambda (sym var)
                       (module-define! dst sym (variable-ref var)))
                     src)
    (for-each (lambda (iface) (module-use! dst iface))
              (module-uses src))
    dst))

;; the file the innermost form being evaluated came from — read by tooling to
;; record where something was defined. #f when not inside a file load
;; (hyprctl scheme input, bootstrap setup). A PARAMETER (not a plain
;; variable) so it can be bound per load.
(define hl--load-source (make-parameter #f))

;; evaluate every form of a file into an environment (the explicit env
;; argument is the point: plain load always targets the interaction
;; environment, which would leak definitions across generations)
(define (hl--eval-file path env)
  (call-with-input-file path
    (lambda (p)
      (let loop ()
        (let ((form (read p)))
          (unless (eof-object? form)
            (parameterize ((hl--load-source path))
              (eval form env))
            (loop)))))))

;; the environment user-supplied code runs in: the current generation, or
;; the persistent environment before the first reset
(define (hl--target-env)
  (or hl--generation (interaction-environment)))

(define (hl--load path)
  (guard (e (#t (hl--report e)))
    (hl--eval-file path (hl--target-env))
    #t))

;; Generation-aware shadows of load/eval: user code — (load "file") and
;; (eval x) — must target the current generation, never the persistent
;; environment, or the documented config-splitting workflow (see core) would
;; leak definitions across generations. Outside a generation (bootstrap
;; phase) they fall through to Guile's own.
(define hl--base-load load)
(define hl--base-eval eval)
(define (load f) (if hl--generation (hl--load f) (hl--base-load f)))
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
