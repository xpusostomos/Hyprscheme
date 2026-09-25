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

;; the file the innermost form being evaluated came from — read by the
;; defun machinery to record where a function was defined. #f when not
;; inside a file load (hyprctl scheme input, bootstrap setup). A
;; PARAMETER (not a plain variable): parameterize can only bind parameters.
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

;; layout event dispatch. spec is a flat PLIST of callbacks (see
;; hl-layout-add!): 'recalculate 'resize 'window-open 'window-close
;; 'layout-msg. recalculate/resize fn: (count W H windows [dx dy corner])
;; -> ((x y w h) ...); window callbacks: (window) -> ignored; layout-msg
;; fn: (message) -> response string. #f when absent or on error.
;; Five direct entry points (one per callback kind) — no event strings,
;; no dispatching cond.
(define (hl--layout-call spec tag . args)
  (let ((cb (hl--plist-get spec tag #f)))
    (and cb (guard (e (#t (hl--report e)))
              (apply cb args)))))

(define (hl--layout-window-open spec id)
  (hl--layout-call spec 'window-open (hl--mint-window id)))

(define (hl--layout-window-close spec id)
  (hl--layout-call spec 'window-close (hl--mint-window id)))

(define (hl--layout-msg spec msg)
  (let ((r (hl--layout-call spec 'layout-msg msg)))
    (cond ((not r) "rejected")
          ((string? r) r)
          (else ""))))

;; recalculate: payload = (count W H id ...)
(define (hl--layout-recalculate spec payload)
  (hl--layout-call spec 'recalculate (car payload) (cadr payload) (caddr payload)
                   (map hl--mint-window (cdddr payload))))

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
           (cb count W H (map hl--mint-window ids) dx dy corner)))))
