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

(define (stub-c-entry-points files)
  (for-each
    (lambda (f)
      (call-with-input-file f
        (lambda (port)
          (let loop ((form (read port)))
            (unless (eof-object? form)
              (when (pair? form)
                (let walk ((x form))
                  (cond ((symbol? x)
                         (let ((name (symbol->string x)))
                           (when (and (> (string-length name) 6)
                                      (string=? (substring name 0 6) "hl--c-"))
                             (module-define! (current-module) x (lambda args #f)))))
                        ((pair? x) (walk (car x)) (walk (cdr x))))))
              (loop (read port)))))))
    files))

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
                       (apply format #f
                              (if (string-contains tmpl "~A")
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
