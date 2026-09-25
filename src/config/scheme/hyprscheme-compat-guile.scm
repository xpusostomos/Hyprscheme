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

(use-modules (rnrs hashtables) (ice-9 exceptions) (srfi srfi-13))

;; ---- Chez-only procedures -----------------------------------------------------

;; (errorf 'who "fmt" args...) raises the formatted message
(define (errorf who fmt . args)
  (error (apply format #f fmt args)))

(define (andmap f l) (and-map f l))     ; Chez spelling of Guile's and-map
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

;; byte-exact string output into a fresh string (call-with-string-output-port
;; is Chez/R6RS; Guile has with-output-to-string)
(define (call-with-string-output-port f)
  (with-output-to-string (lambda () (f (current-output-port)))))

;; Chez conditions -> Guile exceptions: render the message
(define (display-condition e p)
  (cond ((exception? e) (display (or (exception-message e) (format #f "~s" e)) p))
        (else (display e p))))

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
    (module-for-each (lambda (sym var)
                       (module-define! dst sym (variable-ref var)))
                     src)
    dst))

;; GC hooks: Chez's collect-request-handler drives handle reaping; Guile has
;; no such hook yet (step 4 of the migration), so the drain never fires and
;; dead handles leak until then. The C++ watchdog thread backstop is
;; unaffected.
(define (collect-request-handler f) (if #f #f))

;; ---- watchdog stubs ------------------------------------------------------------
;; Chez's timer-interrupt machinery is not ported yet (step 5): the watchdog
;; wrapper degrades to a plain pass-through (callbacks unbudgeted on Guile;
;; the C++ log-only watchdog thread stays as the backstop).
(define (set-timer . _) 0)
(define (timer-interrupt-handler . _) #f)

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
               (if (eq? rettype 'void) void (hl--ffi-type rettype))
               (module-ref (current-module) (string->symbol cname))
               (map hl--ffi-type argtypes))))
    (lambda args
      (hl--ffi-ret rettype
                   (apply raw (map hl--ffi-arg argtypes args))))))

(define-syntax (foreign-procedure x)
  (syntax-case x ()
    [(_ cname (argtype ...) rettype)
     (string? (syntax->datum #'cname))
     #'(hl--foreign-mk cname '(argtype ...) 'rettype)]))