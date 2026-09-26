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
(define (load-reporting path)
  (format #t "~a: " path)
  ;; start from a base module: `define-module` is expanded in whatever module is
  ;; current, and one of the machinery's own modules need not carry it
  (let ((prev (current-module)))
    (set-current-module (resolve-module '(guile-user)))
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
                  (loop (+ n 1))))))))))

(let* ((dir (machinery-dir))
       (kernel (string-append dir "/hyprscheme/kernel.scm"))
       (api    (map (lambda (m) (string-append dir "/hyprscheme/" m ".scm"))
                    (list "core" "bind" "config" "event" "exec" "gesture" "group"
                          "layer" "monitor" "notification" "query" "rule" "timer"
                          "window" "workspace" "extras")))
       (pub    (string-append dir "/hyprscheme.scm")))
  ;; the host's order (Host.cpp buildGeneration): create each module, define its
  ;; bindings into it, THEN read its file — the export lists are explicit and
  ;; name bindings that do not come from the file, so they must exist first
  (resolve-module '(hyprscheme kernel))
  (set-current-module (resolve-module '(hyprscheme kernel)))
  (module-define! (current-module) 'hl--c-generation (lambda () #f))
  (load-reporting kernel)

  (resolve-module '(hyprscheme api))
  (set-current-module (resolve-module '(hyprscheme api)))
  (stub-c-entry-points (cons kernel api))
  (for-each load-reporting api)

  (load-reporting pub)
  ;; the host sets this once every module is in (see Host.cpp)
  (module-set! (resolve-module '(hyprscheme kernel)) 'hl--ready #t)

  ;; a generation, as the host builds one: the API and the kernel, directly
  (define gen (make-fresh-user-module))
  (module-use! gen (resolve-interface '(hyprscheme kernel)))
  (for-each (lambda (m) (module-use! gen (resolve-interface (list 'hyprscheme (string->symbol m)))))
            (list "core" "bind" "config" "event" "exec" "gesture" "group" "layer"
                  "monitor" "notification" "query" "rule" "timer" "window"
                  "workspace" "extras"))
  ;; module-variable, not module-ref: the host's lookup (scm_c_lookup) DOES see
  ;; imported bindings, and module-ref does not
  (format #t "hl--ready: ~a~%" (variable-ref (module-variable gen 'hl--ready)))
  (for-each load-reporting (cdr (command-line)))
  (exit (if (zero? failed) 0 1)))
