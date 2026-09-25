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
             (srfi srfi-1) (system foreign))
;; srfi-9's define-record-type under an alias, so our Chez-shorthand macro
;; below can expand to it without recursing into itself
(use-modules ((srfi srfi-9) #:select ((define-record-type . hl--srfi-define-record-type))))

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
;; vanishes. Rebind it to a real /dev/stderr file port (append mode —
;; "w" would truncate the compositor's own log!).
(define hl--stderr-port (open-file "/dev/stderr" "a"))
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
  (let ((msg (guard (e2 (#t #f))
               (and (exception? e) (exception-message e)))))
    (display (if (string? msg) msg "?") p)
    (format p " | full: ~s" e)
    (newline p)
    ;; TEMP DIAGNOSTIC: scheme backtrace to the working stderr port
    (parameterize ((current-output-port p))
      (backtrace))))

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
                      (string-append "make-" (symbol->string (syntax->datum #'name))))))
            (pred (datum->syntax #'name
                    (string->symbol
                      (string-append (symbol->string (syntax->datum #'name)) "?"))))
            ((acc ...) (map (lambda (fd)
                              (datum->syntax #'name
                                (string->symbol
                                  (string-append (symbol->string (syntax->datum #'name))
                                                 "-"
                                                 (symbol->string fd)))))
                            (syntax->datum #'(f ...)))))
         #'(hl--srfi-define-record-type name
             (ctor f ...)
             pred
             (f acc) ...))])))

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
      (hl--ffi-ret rettype
                   (apply raw (map hl--ffi-arg argtypes args))))))

;; NOTE: Guile (unlike Chez) has no curried (define-syntax (name x) ...)
;; form — the transformer must be a plain lambda expression.
(define-syntax foreign-procedure
  (lambda (x)
    (syntax-case x ()
      [(_ cname (argtype ...) rettype)
       (string? (syntax->datum #'cname))
       #'(hl--foreign-mk cname '(argtype ...) 'rettype)])))