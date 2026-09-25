;; hyprscheme-compat-guile.scm — the Guile compatibility layer.
;;
;; Loaded between the prelude and the defun machinery (see
;; SchemeHost::compatFile). Everything the Chez-native prelude and
;; bootstrap take for granted, provided on Guile:
;;   - imports the prelude/bootstrap rely on (rnrs hashtables, exceptions,
;;     srfi-13)
;;   - Chez-only procedures (errorf, andmap, list*, exact, eof-object,
;;     collect, call-with-string-output-port, display-condition, the
;;     Chez time API, top-level-value, copy-environment,
;;     collect-request-handler)
;;   - foreign-procedure, rebuilt on top of the C++ host's registered
;;     function POINTERS (scm_from_pointer) + Guile's libffi — the 299
;;     declarations in the bootstrap stay untouched

(use-modules (rnrs hashtables) (ice-9 exceptions) (srfi srfi-13)
             (srfi srfi-1) (ice-9 ports) (system foreign)
             (ice-9 optargs))

;; ---- Chez-only procedures -----------------------------------------------------

;; (system foreign) exports `void` — the ffi type, whose value is the integer
;; 0. The prelude's (void) wants Chez's unspecified value, and applying the
;; integer 0 is exactly the "Wrong type to apply: (0)" that killed every
;; eval. Capture the ffi type first, then rebind void.
(define hl--ffi-void-type void)
(define (void) (if #f #f))

;; (errorf 'who "fmt" args...) raises the formatted message
(define (errorf who fmt . args)
  (error (apply format #f fmt args)))

(define (andmap f l) (and-map f l))     ; Chez spelling of Guile's and-map
(define (exists pred . lists) (apply any pred lists))   ; R6RS names, Guile spells them
(define (for-all pred . lists) (apply every pred lists)); any/every (srfi-1)

;; Chez's (format FMT args...) has no destination; Guile's wants one first.
;; A string in the first position means Chez style — prepend #f.
(define hl--guile-format format)
(define (format . args)
  (if (string? (car args))
      (apply hl--guile-format #f args)
      (apply hl--guile-format args)))
(define (list* . args) (apply cons* args))

;; on Guile the `load` BINDING is a syntax transformer; the prelude will do
;; (define real-load load) and needs a PROCEDURE. Defining load here — the
;; prelude then captures it into real-load before re-shadowing load itself.
(define (load f) (primitive-load f))
(define (exact v) (inexact->exact v))

;; eof-object: Guile has the predicate but no 0-arg constructor; the eof
;; object is a singleton, so cache one
(define hl--eof-value (read (open-input-string "")))
(define (eof-object) hl--eof-value)

(define (collect) (gc))

;; Guile's default error port does not reach the compositor's stderr in this
;; embedded context (scm_init_guile without a boot sequence), so every write
;; through current-error-port — hl--report's error lines, user stderr —
;; vanishes. (open-file "/dev/stderr" ...) is ALSO wrong: it reopens the log
;; with a fresh write offset, and error lines then overwrite the log's
;; beginning (the "vanished" watchdog report). Wrap the EXISTING fd 2
;; instead — one shared offset with the compositor's own writes.
(define hl--stderr-port (fdopen 2 "w"))
(setvbuf hl--stderr-port 'none)   ; unbuffered: error lines appear immediately

(define (current-error-port) hl--stderr-port)

;; byte-exact string output into a fresh string (call-with-string-output-port
;; is Chez/R6RS; Guile has with-output-to-string)
(define (call-with-string-output-port f)
  (with-output-to-string (lambda () (f (current-output-port)))))

;; Chez conditions -> Guile exceptions: render the message. exception-message
;; only works on some exception types — guard it, or reporting an odd
;; exception would itself raise (and the real error would vanish inside the
;; C++-side catch).
(define (display-condition e p)
  ;; boot-9's (error msg args...) stores msg as a "~A ~S" TEMPLATE with args
  ;; as irritants and formats lazily — exception-message hands back the raw
  ;; template. Render the template against the irritants the way Guile's
  ;; default handler does; fall back to the message or the whole condition.
  (let ((msg (guard (e2 (#t #f)) (exception-message e)))
        (irr (guard (e2 (#t #f)) (exception-irritants e))))
    (cond ((not (string? msg))
           (format p "~s" e))
          ((and (pair? irr) (string-index msg #\~))
           ;; a template: render it (guard: a bad escape must not kill the report)
           (guard (e3 (#t (display msg p)))
             (apply simple-format p msg irr)))
          (else
           (display msg p)))))

;; portable wall clock for the watchdog (Chez: time objects; here: numbers)
(define (time-difference a b) (- a b))
(define (time-nanosecond dt) (inexact->exact (floor (* dt 1000000000))))

;; the interpreter's global-environment lookup with an explicit environment
;; (the prelude reads config-settable watchdog budgets through the generation
;; copy)
(define (top-level-value sym env) (module-ref env sym))

;; the generation model: Chez's copy-environment — fresh variable locations,
;; shared values. (The naive module-use! inheritance shares locations and
;; would leak set!s back into the persistent API — do not use it here.)
(define (copy-environment src)
  (let ((dst (make-fresh-user-module)))
    ;; locals: fresh locations, shared values (set!s stay in the generation)
    (module-for-each (lambda (sym var)
                       (module-define! dst sym (variable-ref var)))
                     src)
    ;; imports (guard from (ice-9 exceptions), the rnrs tables, srfi-13, ...):
    ;; module-for-each only sees LOCALS, so without this the generation is
    ;; missing every imported binding — and every imported macro. Sharing the
    ;; interfaces is safe: imported bindings are immutable in practice.
    (for-each (lambda (iface) (module-use! dst iface))
              (module-uses src))
    dst))

;; GC hooks: Chez's collect-request-handler fires at GC-REQUEST time (the
;; bootstrap's handler drains dead handles, then calls (collect) to let the
;; collection proceed). Guile's equivalent hook point is after-gc-hook, which
;; runs AFTER each collection — same effect for the drain. The bootstrap's
;; handler ends with (collect), which on Guile would re-enter gc from inside
;; the hook, so the hook runs ONLY the drain (hl--drain-handles!, defined in
;; the bootstrap — late binding resolves it at hook time, on the thread that
;; triggered the GC: the event-loop thread, same as Chez).
(define (collect-request-handler f) (if #f #f))   ; Chez protocol, unused here
(define hl--drain-registered #f)
(add-hook! after-gc-hook
  (lambda ()
    ;; the bootstrap defines the drain LATER in its file — early GCs (the
    ;; load itself is allocation-heavy) must no-op until it exists; and a
    ;; failing drain must not kill whatever triggered the GC
    (when (module-variable (current-module) 'hl--drain-handles!)
      (guard (e (#t (hl--report e)))
        (hl--drain-handles!)))))

;; ---- the watchdog (Chez timer-interrupt machinery, Guile-style) ------------------
;; The prelude's watchdog runs callbacks under a wall-clock budget: it arms
;; Chez's timer interrupt and the handler escapes out of the callback when the
;; budget is exceeded. The Guile equivalent delivers SIGALRM as an async, which
;; lands at procedure-entry safe points — the VM hits them in any recursive
;; loop (verified live: a tight (loop (+ i 1)) unwinds via catch). Foreign
;; calls stay non-interruptible on both backends; the C++ log-only watchdog
;; thread remains the backstop for those.
;;
;; Interface parity with Chez:
;;   (timer-interrupt-handler)        -> the current handler
;;   (timer-interrupt-handler FN)     -> install FN
;;   (set-timer TICKS)                -> arm, returns the previous value
;;   (set-timer 0)                    -> disarm
(define hl--timer-handler #f)

(define (hl--run-timer-handler)
  ;; SIGALRM async entry: one handler at a time; deliver only when armed
  (when hl--timer-handler
    (hl--timer-handler)))

(define (timer-interrupt-handler . args)
  (if (null? args)
      hl--timer-handler
      (begin
        (set! hl--timer-handler (car args))
        ;; SIGALRM delivers to the dispatcher, which calls the current
        ;; handler at the next safe point
        (sigaction SIGALRM
          (lambda (sig) (hl--run-timer-handler))))))

(define (set-timer ticks)
  ;; Chez ticks are work units; here alarm() drives the wall-clock cadence.
  ;; The watchdog handler checks WALL-CLOCK time against the budget, so the
  ;; alarm period only sets how OFTEN the budget is checked (10000 rearm
  ;; ticks -> 1s granularity; the default budget is 5000ms). Returns the
  ;; previous value, which the prelude saves/restores across nested runs.
  (let ((prev (alarm (if (and (number? ticks) (> ticks 0))
                         (max 1 (quotient ticks 10000))
                         0))))
    ticks))

;; ---- define-record-type ----------------------------------------------------------
;; Chez's R6RS shorthand: (define-record-type name (fields f ...)) generates
;; make-<name>, <name>? and <name>-<field> accessors. Guile's srfi-9 needs the
;; full form — expand it, keeping Chez's generated names (the bootstrap calls
;; make-hl-window, hl-window-cell etc. by those names).

(define-syntax define-record-type
  (lambda (x)
    (syntax-case x ()
      [(_ name (fields f ...))
       (identifier? #'name)
       (with-syntax
           ((ctor (datum->syntax #'name
                    (string->symbol
                      (string-append "make-"
                        (symbol->string (syntax->datum #'name))))))
            (pred (datum->syntax #'name
                    (string->symbol
                      (string-append
                        (symbol->string (syntax->datum #'name)) "?"))))
            ((acc ...)
             (map (lambda (fd)
                    (datum->syntax #'name
                      (string->symbol
                        (string-append (symbol->string (syntax->datum #'name))
                                       "-"
                                       (symbol->string fd)))))
                  (syntax->datum #'(f ...))))
            ((idx ...)
             (map (lambda (i) (datum->syntax #'name i))
               (let loop ((i 0) (acc (quote ())) (rest (syntax->datum #'(f ...))))
                 (if (null? rest)
                     (reverse acc)
                     (loop (+ i 1) (cons i acc) (cdr rest)))))))
         ;; expand DIRECTLY onto Guile's core record primitives: every
         ;; generated binding is a plain procedure, so forward references
         ;; (code compiled before the record exists — the bootstrap defines
         ;; hl-bind-add! long before the hl-bind record) late-bind exactly
         ;; like Chez's (fields ...) records do. (srfi-9's constructor and
         ;; accessors are syntax transformers — unappliable values, and its
         ;; keyword bindings refuse set!.) NAME itself is bound to the rtd,
         ;; matching the srfi-9 expansion.
         #'(begin
             (define name (make-record-type 'name (list (quote f) ...)))
             (define (ctor f ...) (make-struct/no-tail name f ...))
             (define (pred x) (and (struct? x) (eq? (struct-vtable x) name)))
             (define (acc x) (struct-ref x idx)) ...))])))

;; ---- foreign-procedure ----------------------------------------------------------
;; (foreign-procedure "cname" (type...) rettype) is Chez-native there; here
;; it resolves the function pointer registered by the C++ host (a
;; scm_from_pointer value) and attaches type conversions declaratively —
;; scheme-object crosses as its raw word (hl--scm->word / hl--word->scm),
;; string as a pointer (string->pointer / pointer->string), the numeric
;; types pass through.
(define (hl--ffi-type t)
  (cond ((eq? t 'scheme-object) uintptr_t)
        ((eq? t 'int) int32)
        ((eq? t 'integer-64) int64)
        ((eq? t 'unsigned-64) uint64)
        ((eq? t 'double) double)
        ((eq? t 'string) '*)
        (else (error "foreign-procedure: unknown type" t))))

(define (hl--ffi-arg t a)
  (cond ((eq? t 'scheme-object) (hl--scm->word a))
        ((eq? t 'string) (string->pointer a))
        (else a)))

(define (hl--ffi-ret t r)
  (cond ((eq? t 'scheme-object) (hl--word->scm r))
        ((eq? t 'string) (pointer->string r))
        ((eq? t 'void) (if #f #f))
        (else r)))

(define (hl--foreign-mk cname argtypes rettype)
  (let ((raw (pointer->procedure
               (if (eq? rettype 'void) hl--ffi-void-type (hl--ffi-type rettype))
               (module-ref (current-module) (string->symbol cname))
               (map hl--ffi-type argtypes))))
    (lambda args
      ;; Chez's contract: FOREIGN CALLS ARE NOT INTERRUPTIBLE — a watchdog
      ;; escape happens only when control RETURNS to Scheme. Guile's asyncs
      ;; would otherwise deliver INSIDE the compositor's C++ code (a SIGALRM
      ;; threw mid window-destruction once and crashed the compositor), so
      ;; block system asyncs for the duration of every foreign call.
      (hl--ffi-ret rettype
                   (call-with-blocked-asyncs
                     (lambda ()
                       (apply raw (map hl--ffi-arg argtypes args))))))))

;; NOTE: Guile (unlike Chez) has no curried (define-syntax (name x) ...)
;; form — the transformer must be a plain lambda expression.
(define-syntax foreign-procedure
  (lambda (x)
    (syntax-case x ()
      [(_ cname (argtype ...) rettype)
       (string? (syntax->datum #'cname))
       #'(hl--foreign-mk cname '(argtype ...) 'rettype)])))