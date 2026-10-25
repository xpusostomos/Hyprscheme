#!/usr/bin/env guile
!#
;; Load a CONFIG form by form and report the first form that raises, printing
;; the form itself, the condition, its irritants and the stack.
;;
;;   guile --no-auto-compile -s tools/form-trace.scm ~/.config/hypr/bindings.scm
;;   ... [START]        start at form START (to skip past a known-good prefix)
;;
;; WHY THIS EXISTS. A config error reaches the user as a notification or the
;; banner, and both carry only the condition's MESSAGE. For most conditions that
;; is the whole story — "hl-bind-add!: tokens must be strings, got: 5" names the
;; function and the value. For a failure inside a PROCEDURE it is not:
;;
;;   Wrong number of arguments to #<procedure 7f2fa7966280 at ice-9/eval.scm:330:13 ()>
;;
;; names an address, a file and a line in GUile's own evaluator, and no form, no
;; file and no call site of the config's. The config is 900 lines; the message
;; narrowed it to "somewhere, something was called with the wrong count". This
;; tool narrows it to ONE FORM, and the trace narrows that to the exact call.
;;
;; It is a DIAGNOSTIC, not a lint: not part of `make check`, and it takes the
;; config as an argument rather than globbing, because the file it is for is
;; usually the user's own.

(setenv "GUILE_AUTO_COMPILE" "0")

(use-modules (ice-9 pretty-print)
             (ice-9 rdelim))

(define (machinery-dir)
  (let* ((self (car (command-line)))
         (dir (dirname (if (string-prefix? "/" self)
                           self
                           (string-append (getcwd) "/" self)))))
    (string-append dir "/..")))

;; ---- the machinery, as the host builds it (see tools/load-check.scm) ---------
;; Stubbed into the KERNEL before it is read, then init.scm loaded into a fresh
;; generation — the same steps in the same order, and kept in step with
;; load-check deliberately: a diagnostic that stands the environment up
;; differently from the host diagnoses a different program.

;; A STUB MUST ANSWER A NUMBER FOR SOME GSUBRS, and getting this wrong is what
;; made the first version of this file lie: it reported form 11 of the config
;; raising `wrong-type-arg` with `(= 1 #f)`, which is the DIAGNOSTIC's fault and
;; not the config's. The machinery compares some entry points against a status
;; code ((= 0 (hl--c-bind ...))), and the stub answered #f, so `(= 0 #f)` raised
;; exactly where the config was innocent. This is the same trap tools/
;; doc-exercise.scm records (see its note above gsubr-symbol?); the code below is
;; copied from there rather than re-derived, so the two cannot drift.
(define (gsubr-symbol? s)
  "True for SYMBOL hl--c-* — a C entry point, by the name convention. A STRING
test, not symbol equality: every gsubr shares a prefix, not an identity."
  (and (symbol? s)
       (let ((name (symbol->string s)))
         (and (> (string-length name) 6) (string=? (substring name 0 6) "hl--c-")))))

(define (form-forms f)
  "Every top-level form in F, in order."
  (call-with-input-file f
    (lambda (port)
      (let loop ((acc '()))
        (let ((form (read port)))
          (if (eof-object? form) (reverse acc) (loop (cons form acc))))))))

(define (adjoin x l)
  "L with X in it, adding it only when absent. (srfi srfi-1) is not imported
here — this file deliberately mirrors the small set tools/load-check.scm uses."
  (if (memq x l) l (cons x l)))

(define (proper-prefix l)
  "L up to its first improper tail, which is where a dot or a rest parameter
sits. So (a b . rest) reads as (a b): a rest name can absorb an argument
position, and treating it as one would flag whatever happened to land there."
  (let loop ((l l) (acc '()))
    (if (pair? l) (loop (cdr l) (cons (car l) acc)) (reverse acc))))

(define (define-params form)
  "The FIXED formal parameters of a define, in either spelling —
(define (name a b) ...) or (define name (lambda (a b) ...)). '() for anything
else, including a define*: its #:key list would only mislead position matching,
and the helpers this looks at are plain defines."
  (and (pair? form) (eq? (car form) 'define) (pair? (cdr form))
       (let ((target (cadr form)) (rest (cddr form)))
         (cond ((pair? target) (proper-prefix (cdr target)))
               ((and (pair? rest) (pair? (car rest))
                     (eq? (car (car rest)) 'lambda) (pair? (cdar rest)))
                (proper-prefix (cadr (car rest))))
               (else '())))))

(define (define-name form)
  "The name a define binds, or #f."
  (and (pair? form) (eq? (car form) 'define) (pair? (cdr form))
       (let ((t (cadr form)))
         (and (pair? t) (symbol? (car t)) (car t)))))

(define (numeric-param-map form)
  "PARAMETER -> the value a stub should answer for it, for the parameters of
FORM that reach a NUMERIC COMPARISON in the body.

TWO ANSWERS, because the two comparisons want opposite things and one stub
value cannot serve both:

  (= 0 PARAM ...)      a STATUS CODE — 0 means success, so answer 0.
  (> PARAM 0)          a MAGNITUDE — exec.scm's `(if (> pid 0) pid (error ...))`,
                       pid being the gsubr's result, so #f raises and 0 takes
                       the error branch. Answer 1.

Getting this wrong is how the diagnostic LIES: both shapes read as a real
config failure otherwise, which is exactly what happened twice while this tool
was being written."
  (let ((found (make-hash-table)) (params (define-params form)))
    ;; A PARAMETER IS USUALLY THE HEAD OF A CALL INSIDE THE COMPARISON, not a
    ;; bare element of it: core.scm writes (= 0 (begin-fn ...)), where begin-fn
    ;; is nested one level down. Filtering the comparison's own argument list
    ;; finds nothing — that mistake is why form 32 of the real config was
    ;; reported as a failure of the CONFIG when it was the stub. So the args are
    ;; walked recursively, and a param is recorded wherever it sits, WITH the
    ;; answer its comparison wants.
    (define (mark-params! x ans)
      (when (pair? x)
        (when (and (symbol? (car x)) (memq (car x) params))
          (hash-set! found (car x) (cons 'ans ans)))
        (mark-params! (car x) ans)
        (mark-params! (cdr x) ans)))
    (let walk ((x form))
      (when (pair? x)
        (when (memq (car x) '(= < > <= >=))
          (mark-params! (cdr x) (if (eq? (car x) '=) 0 1)))
        (walk (car x))
        (walk (cdr x))))
    found))

(define (status-gsubrs files)
  "A hash of GSUBR -> the value the stub must answer for it, for the gsubrs the
machinery compares NUMERICALLY. Three ways in, and only the first is visible by
reading a call site:

  1. DIRECT      (= 0 (hl--c-x ...)) — a status code tested inline.
  2. A VARIABLE  (= 0 rc) or (> pid 0), where the variable was bound to a gsubr
                 call. bind.scm does this; the binding and the test are often
                 in different forms.
  3. A PARAMETER a gsubr PASSED AS A VALUE to a helper that compares its own
                 parameter: core.scm's hl--window-rule-mk is handed
                 hl--c-window-rule-begin / -effect / -commit and tests each.
                 No scan of the call can see this, because there is no call.

The value recorded is what to answer — see numeric-param-map for why 0 and 1 are
different answers. The by-name tables are rebuilt PER FILE: `rc` means different
things in different modules."
  (let ((params-of (make-hash-table))      ; helper -> its params, in order
        (numeric-of (make-hash-table))     ; helper -> param -> stub answer
        (answer (make-hash-table)))        ; gsubr -> stub answer
    (define (note! g v)
      (let ((prev (hash-ref answer g #f)))
        (if prev (hash-set! answer g (min prev v)) (hash-set! answer g v))))

    ;; ---- PASS 1: every helper, and the parameters it compares numerically ----
    (for-each
      (lambda (f)
        (for-each
          (lambda (form)
            (let ((name (define-name form)))
              (if name
                  (begin
                    (hash-set! params-of name (define-params form))
                    (hash-set! numeric-of name (numeric-param-map form))))))
          (form-forms f)))
      files)

    ;; the 1-based positions of NAME whose parameters are compared numerically,
    ;; each paired with the value its comparison wants. NOT a membership test —
    ;; a status parameter's answer IS 0, which is falsy, so `(hash-ref m p)`
    ;; would read a recorded 0 as "not recorded". The sentinel is a pair.
    (define (numeric-positions name)
      (let ((ps (hash-ref params-of name)))
        (and ps
             (let ((m (hash-ref numeric-of name)))
               (let loop ((ps ps) (i 1) (acc '()))
                 (if (null? ps)
                     (reverse acc)
                     (let ((v (and m (hash-ref m (car ps) #f))))
                       (loop (cdr ps) (+ i 1)
                             (if (pair? v) (cons (cons i (cdr v)) acc) acc)))))))))

    ;; a gsubr passed at one of those positions
    (define (scan-passed! x)
      (when (pair? x)
        (when (and (symbol? (car x)) (list? (cdr x)))
          (for-each
            (lambda (iv)
              (let ((i (car iv)) (v (cdr iv)))
                (let ((a (if (> i (length (cdr x))) #f (list-ref (cdr x) (- i 1)))))
                  (when (gsubr-symbol? a) (note! a v)))))
            (or (numeric-positions (car x)) '())))
        (scan-passed! (car x))
        ;; list? and pair? both, so a DOTTED tail — (hl-layout-add! name . spec)
        ;; is a real signature in this tree — is not walked as though its cdr
        ;; were a proper list. (length '(name . spec)) raises.
        (when (list? x) (scan-passed! (cdr x)))))

    ;; ---- PASS 2: per file, the direct and through-a-variable shapes ----
    (for-each
      (lambda (f)
        (let ((by-var (make-hash-table))      ; variable -> gsubr it was bound to
              (cmp-var (make-hash-table)))    ; variable -> stub answer it wants
          (define (scan! x)
            (when (pair? x)
              (let ((op (car x)))
                (when (memq op '(= < > <= >=))
                  (let ((answer (if (eq? op '=) 0 1)))
                    (for-each
                      (lambda (a)
                        (cond ((and (pair? a) (gsubr-symbol? (car a)))
                               (note! (car a) answer))
                              ((symbol? a) (hash-set! cmp-var a answer))))
                      (cdr x)))))
              ;; a binding (name (gsubr ...)) — any two-element form headed by a
              ;; symbol whose second element is a gsubr call
              (when (and (symbol? (car x)) (pair? (cdr x)) (pair? (cadr x))
                         (gsubr-symbol? (car (cadr x))))
                (hash-set! by-var (car x) (car (cadr x))))
              (scan! (car x))
              (scan! (cdr x))))
          (for-each (lambda (form) (scan! form) (scan-passed! form)) (form-forms f))
          ;; join WITHIN the file
          (hash-for-each (lambda (v want)
                           (let ((g (hash-ref by-var v)))
                             (when g (note! g want))))
                         cmp-var)))
      files)

    answer))

(define (stub-c-entry-points files)
  (let ((answers (status-gsubrs files)))
    (for-each
      (lambda (f)
        (call-with-input-file f
          (lambda (port)
            (let loop ((form (read port)))
              (unless (eof-object? form)
                (when (pair? form)
                  (let walk ((x form))
                    (cond ((symbol? x)
                           (when (gsubr-symbol? x)
                             (module-define! (current-module) x
                               (cond ((hash-ref answers x)
                                      => (lambda (v) (lambda args v)))
                                     ;; A TOKEN-LIST CONVERTER IS IDENTITY with no
                                     ;; keyboard, which is what the machinery's own
                                     ;; hl-key-logical-to-physical does and
                                     ;; documents. Its INVERSE does not — it
                                     ;; raises with hl--c-config-last-error, which
                                     ;; is #f here, so the message reads
                                     ;; "hl-key-physical-to-logical: #f" — and
                                     ;; that asymmetry is a real defect in the
                                     ;; machinery, reported rather than papered
                                     ;; over here. Identity is the faithful stub
                                     ;; either way: hl--expand (define-key!) calls
                                     ;; it to name a submap, and the name it wants
                                     ;; is the chord as written.
                                     ((memq x '(hl--c-key-physical-to-logical
                                                hl--c-key-logical-to-physical))
                                      (lambda (toks) toks))
                                     (else (lambda args #f))))))
                          ((pair? x) (walk (car x)) (walk (cdr x))))))
                (loop (read port)))))))
      files)))

(define (load-machinery path base)
  (let ((prev (current-module)))
    (set-current-module base)
    (call-with-input-file path
      (lambda (port)
        (let loop ()
          (let ((form (read port)))
            (unless (eof-object? form)
              (catch #t
                (lambda () (eval form (current-module)))
                (lambda (k . args)
                  (format (current-error-port) "machinery load error in ~a: ~s~%"
                          path k)))
              (when (and (pair? form) (eq? (car form) 'define-module))
                (set-current-module (resolve-module (cadr form))))
              (loop))))))
    (set-current-module prev)))

(define (boot!)
  (let* ((dir (machinery-dir))
         (entry (string-append dir "/init.scm"))
         (mods (list "kernel" "enums" "core" "bind" "config" "event" "exec"
                     "gesture" "group" "layer" "monitor" "notification" "query"
                     "rule" "timer" "window" "workspace" "extras"
                     "emacs-keymaps"))
         (all (cons entry
                    (map (lambda (m) (string-append dir "/hyprscheme/" m ".scm"))
                         mods))))
    (set! %load-path (cons dir %load-path))
    (set! %load-compiled-path (cons dir %load-compiled-path))
    (resolve-module '(hyprscheme kernel))
    (set-current-module (resolve-module '(hyprscheme kernel)))
    (module-define! (current-module) 'hl--c-generation (lambda () #f))
    (stub-c-entry-points all)
    (let ((gen (make-fresh-user-module)))
      (load-machinery entry gen)
      (module-set! (resolve-module '(hyprscheme kernel)) 'hl--ready #t)
      gen)))

;; ---- reporting ---------------------------------------------------------------
;;
;; THE PARTS ARE SPREAD OVER TWO SHAPES, and which one you get depends on HOW
;; the error was raised. That is the whole reason this file exists:
;;
;;   (error "hl-bind-add!: ..." value)   -> a CONDITION OBJECT. exception-message
;;                                          is the sentence, irritants the value,
;;                                          and the reader learns both.
;;   (throw #f "Wrong number of ~A" p x) -> RAW. NO condition object at all: the
;;                                          message is a FORMAT TEMPLATE and the
;;                                          parts are positional. Formatting the
;;                                          template against them is the only way
;;                                          to get a sentence out of it.
;;
;; Guile's evaluator raises arity errors the second way, which is why a wrong
;; call count reaches the user as:
;;
;;   Wrong number of arguments to #<procedure 7f2fa7966280 at ice-9/eval.scm:330:13 ()>
;;
;; — an ADDRESS, a file and a line in Guile's own evaluator, and nothing about
;; the config. The last `()` is not noise: it is the procedure's ARGUMENT LIST,
;; so `()` says the procedure takes NONE.

(define (safe thunk fallback)
  "Everything in the reporter goes through this. A reporter that raises while
reporting hides the error it was called for — the first version of this file
did exactly that, dying on `condition?` (unbound here) before printing a word."
  (catch #t (lambda () (thunk)) (lambda _ fallback)))

(define (show-parts key args)
  "Render a raw `(key . args)` from a catch, the way a user needs to read it."
  (format #t "--- raised:     ~s~%" key)
  (cond
    ;; (throw #f TEMPLATE . PARTS) — the arity shape, and the one that matters
    ((and (string? key) (pair? args) (string? (car args)))
     (let ((tmpl (car args)) (parts (cdr args)))
       (format #t "--- message:    ~a~%"
               (safe (lambda ()
                       ;; string-index, not string-contains: the latter is
                       ;; SRFI-13 and is not in the default environment this
                       ;; script runs in, so a reporter that called it would die
                       ;; reporting — which is the failure mode this file's
                       ;; `safe` exists for and already suffered once.
                       (apply format #f
                              (if (string-index tmpl #\~)
                                  tmpl
                                  (string-append tmpl " ~A"))
                              parts))
                     tmpl))
       (format #t "--- parts:      ~s~%" (safe (lambda () parts) '()))))
    ;; a condition OBJECT: message and irritants are separate slots
    ((and (pair? args) (exception? (car args)))
     (let ((c (car args)))
       (format #t "--- message:    ~s~%" (safe (lambda () (exception-message c)) #f))
       (format #t "--- irritants:  ~s~%" (safe (lambda () (exception-irritants c)) '()))))
    (else
     (format #t "--- args:       ~s~%" (safe (lambda () args) '())))))

(define (show-stack!)
  "The stack is the part the banner cannot show at all — and the part that names
the config's own call site."
  (format #t "~%--- backtrace:~%")
  (catch #t
    (lambda () (display-backtrace (make-stack #t 4) (current-error-port)))
    (lambda _ (format #t "--- (no backtrace available)~%"))))

(define (report-failure form n key args)
  (format #t "~%===== FORM ~a RAISED =====~%" n)
  (format #t "~a~%" (call-with-output-string (lambda (p) (pretty-print form p))))
  (show-parts key args)
  (show-stack!))

(define (trace-forms! file start gen tool)
  "Load each form INTO GEN, reporting through TOOL's own bindings.

TWO MODULES ARE LIVE HERE and mixing them up is the bug this signature exists
to prevent: the config's forms must be EVALUATED in the generation (that is
where hl-* lives), while the reporting must be EVALUATED with the tool's module
current — `condition?`, `exception-irritants` and `pretty-print` are the TOOL's
imports and are unbound in the generation. This script is primitive-loaded
rather than compiled, so a free identifier in a procedure body resolves in
whatever module happens to be CURRENT when it runs; the first version of this
file printed the condition by calling those names with the generation current
and died with `Unbound variable: condition?` instead of reporting anything. So
the module is set explicitly on both sides of the boundary, and `eval` is given
its module rather than reading the current one."
  (call-with-input-file file
    (lambda (port)
      (let loop ((n 0))
        (let ((form (read port)))
          (cond
            ((eof-object? form)
             (format #t "~a: all ~a forms loaded~%" file n)
             0)
            (else
             (when (>= (+ n 1) start)
               (catch #t
                 (lambda ()
                   (set-current-module gen)
                   (eval form gen))
                 (lambda (k . args)
                   (set-current-module tool)
                   (report-failure form (+ n 1) k args)
                   (exit 1))))
             (loop (+ n 1)))))))))

(define (ownership-dirs!)
  "Point the state directory somewhere of our own: a config writes mode files
under XDG_STATE_HOME, and a diagnostic may no more touch the real one than a
test may."
  (let ((scratch (string-append "/tmp/hyprscheme-formtrace-"
                                (number->string (getpid)))))
    (mkdir scratch)
    (setenv "XDG_STATE_HOME" scratch)))

(define (main)
  (let* ((args (cdr (command-line)))
         (file (and (pair? args) (car args)))
         (start (if (and (pair? args) (pair? (cdr args)))
                    (string->number (cadr args))
                    1)))
    (unless file
      (format (current-error-port) "usage: form-trace.scm CONFIG.scm [START-FORM]~%")
      (exit 2))
    (ownership-dirs!)
    (let ((gen (boot!))
          (tool (current-module)))
      (exit (trace-forms! file start gen tool)))))

(main)
