#!/usr/bin/env guile
!#
;; Load the Scheme machinery in a plain guile, with every C entry point
;; stubbed, and report anything that fails to load or evaluates to an error.
;; No compositor needed — a fast pre-flight for edits to the machinery.
;;
;;   guile --no-auto-compile -s tools/load-check.scm
;;   guile -s tools/load-check.scm FILE.scm ...   ; ...then extra files
;;
;; It mimics what the host does (Host.cpp's buildGeneration): load the kernel
;; module, stub the gsubrs INTO it, export it whole, then load the API and the
;; public umbrella, and finally a generation that imports them. Stubbing into
;; the kernel is the part that makes this faithful — the machinery is modules
;; now, so a flat "eval everything into one environment" would neither find the
;; gsubrs nor reproduce the real load order.
;;
;; The C entry points are gsubrs named hl--c-*; they are stubbed from the
;; machinery's own call sites, so the list cannot drift from the code. Calls
;; through a stub return #f — this checks that everything LOADS and that
;; top-level forms run, not that the API behaves (that is the test suite's job).

(setenv "GUILE_AUTO_COMPILE" "0")   ; read the machinery, never compile it

(define (machinery-dir)
  (let* ((self (car (command-line)))
         (dir (dirname (if (string-prefix? "/" self)
                           self
                           (string-append (getcwd) "/" self)))))
    (string-append dir "/../src/config/scheme")))

(define (form-line form)
  (or (assq-ref (source-properties form) 'line) 0))

(define failed 0)

(define (report path form)
  (format #t "~a:~a: ERROR: ~s~%" path (form-line form) form))

;; the condition matters as much as the form: without it a failure reads as a
;; form the reader already accepted
(define (report-why who)
  (format #t "        why: ~a~%"
          (call-with-output-string (lambda (p) (display who p)))))

;; every hl--c-* name the machinery mentions, stubbed as a no-op
(define (stub-c-entry-points files)
  (for-each
    (lambda (f)
      (format (current-error-port) "  stubbing from ~a...\n" f)
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

;; load a file form by form so a failure names the offending form, and restore
;; the module afterwards (a define-module file switches it as a side effect)
;;
;; BASE is the module the forms are evaluated in, and it is NOT incidental:
;; `use-modules` imports into whatever module is current, so a file that is all
;; `use-modules` — init.scm — puts its imports wherever this points. Default
;; `(guile-user)`, which is right for a file whose first form is `define-module`
;; (`define-module` is expanded in the current module, and one of the machinery's
;; own modules need not carry its expander). init.scm has no `define-module` and
;; must be read with the GENERATION current, so the host passes it explicitly.
(define* (load-reporting path #:optional (base '(guile-user)))
  (format #t "~a: " path)
  (let ((prev (current-module)))
    (set-current-module (if (pair? base) (resolve-module base) base))
    (call-with-input-file path
      (lambda (port)
        (let loop ((n 0))
          (let ((form (read port)))
            (if (eof-object? form)
                (begin (set-current-module prev) (format #t "~a forms~%" n))
                (begin
                  (catch #t
                    (lambda () (eval form (current-module)))
                    (lambda (k . args)
                      (report path form)
                      (report-why (if (null? args) k (car args)))
                      (set! failed (+ failed 1))))
                  ;; `define-module` evaluated form by form does NOT switch the
                  ;; current module — only the loader's own handling of it does.
                  ;; Without this, every file is defined into (guile-user) and
                  ;; the module keeps nothing but its unassigned #:export
                  ;; placeholders: the forms all evaluate without error, and
                  ;; every name in the module is then unbound. (It made this
                  ;; check weaker than it read: it proved the forms RAN, not
                  ;; that the machinery was there.)
                  (when (and (pair? form) (eq? (car form) 'define-module))
                    (set-current-module (resolve-module (cadr form))))
                  (loop (+ n 1))))))))))

(let* ((dir (machinery-dir))
       (kernel (string-append dir "/hyprscheme/kernel.scm"))
       (entry  (string-append dir "/init.scm"))
       ;; every .scm the machinery is made of, for the stub scan: the entry
       ;; point, the umbrella, and the modules under hyprscheme/
       (all    (cons entry
                     (cons (string-append dir "/hyprscheme.scm")
                           (map (lambda (m) (string-append dir "/hyprscheme/" m ".scm"))
                                (list "kernel" "core" "bind" "config" "event" "exec"
                                      "gesture" "group" "layer" "monitor"
                                      "notification" "query" "rule" "timer"
                                      "window" "workspace" "extras"
                                      "emacs-keymaps"))))))
  ;; THE HOST'S ACTUAL SEQUENCE (Host.cpp buildGeneration), in order:
  ;;   1. the machinery directory on %load-path and %load-compiled-path
  ;;   2. create the kernel module and register the gsubrs into it — the stubs
  ;;      go in the KERNEL, where the host puts the real ones, and BEFORE the
  ;;      kernel is read, so its #:export can name them. Stubbed anywhere else
  ;;      the kernel's own hl--c-* placeholders stay unassigned and every call
  ;;      through one is an unbound variable — a class of error this check then
  ;;      could not see.
  ;;   3. make the GENERATION and make it CURRENT, then load init.scm INTO it.
  ;;      The generation is current so init.scm's `use-modules` land in the
  ;;      generation, which is what makes init.scm the generation's environment
  ;;      and leaves the host no module list of its own to keep in step.
  ;;
  ;; This used to reimplement steps 2-3 by loading all nineteen files itself,
  ;; which meant it could not catch a fault in init.scm — the file the host
  ;; actually loads. Now it loads the same file the host does, in the same
  ;; module, which is the part that matters: importing init.scm's names
  ;; somewhere else would let this pass while the generation had nothing.
  (set! %load-path (cons dir %load-path))
  (set! %load-compiled-path (cons dir %load-compiled-path))

  (resolve-module '(hyprscheme kernel))
  (set-current-module (resolve-module '(hyprscheme kernel)))
  (module-define! (current-module) 'hl--c-generation (lambda () #f))
  (stub-c-entry-points all)

  ;; a generation, as the host builds one, BEFORE init.scm — see step 3 above
  (define gen (make-fresh-user-module))
  (load-reporting entry gen)
  ;; the host sets this once every module is in (see Host.cpp)
  (module-set! (resolve-module '(hyprscheme kernel)) 'hl--ready #t)
  ;; module-variable, not module-ref: the host's lookup (scm_c_lookup) DOES see
  ;; imported bindings, and module-ref does not
  (format #t "hl--ready: ~a~%" (variable-ref (module-variable gen 'hl--ready)))
  (for-each load-reporting (cdr (command-line)))
  (exit (if (zero? failed) 0 1)))
