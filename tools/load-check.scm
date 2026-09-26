#!/usr/bin/env guile
!#
;; Load the Scheme machinery in a plain guile, with every C entry point
;; stubbed, and report anything that fails to load or evaluates to an error.
;; No compositor needed — a fast pre-flight for edits to the machinery.
;;
;;   guile --no-auto-compile -s tools/load-check.scm   ; prelude + bootstrap
;;   guile -s tools/load-check.scm FILE.scm ...    ; ...then extra files
;;
;; The C entry points are gsubrs named hl--c-*; they are stubbed from the
;; bootstrap's own call sites, so the list cannot drift from the code. Calls
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

;; every hl--c-* name the bootstrap mentions, stubbed as a no-op
(define (stub-c-entry-points bootstrap)
  (format (current-error-port) "  stubbing from ~a...\n" bootstrap)
  (let loop ((form (call-with-input-file bootstrap read)))
    (unless (eof-object? form)
      (when (pair? form)
        (let walk ((f form))
          (cond ((symbol? f)
                 (let ((name (symbol->string f)))
                   (when (and (> (string-length name) 6)
                              (string=? (substring name 0 6) "hl--c-"))
                     (module-define! (current-module) f (lambda args #f)))))
                ((pair? f) (walk (car f)) (walk (cdr f))))))
      (loop (read)))))

;; The after-gc-hook form is skipped: this tool stubs the C entry points with
;; VARIADIC closures, which cons, and a GC hook that allocates re-triggers the
;; collector (the machinery's own hook is a bare gsubr call for that reason).
(define (gc-hook-form? form)
  (and (pair? form) (eq? (car form) 'add-hook!)))

(define (load-reporting path)
  (format #t "~a: " path)
  (call-with-input-file path
    (lambda (port)
      (let loop ((n 0))
        (let ((form (read port)))
          (if (eof-object? form)
              (format #t "~a forms~%" n)
              (begin
                (catch #t
                  (lambda () (unless (gc-hook-form? form) (eval form (interaction-environment))))
                  (lambda args
                    (report path form)
                    (set! failed (+ failed 1))))
                (loop (+ n 1)))))))))

(let* ((dir (machinery-dir))
       (bootstrap (string-append dir "/hyprscheme-bootstrap.scm")))
  (stub-c-entry-points bootstrap)
  (for-each load-reporting
            (append (list (string-append dir "/hyprscheme-prelude.scm") bootstrap)
                    (cdr (command-line))))
  (format #t "hl--ready: ~a~%" (module-ref (current-module) 'hl--ready))
  (exit (if (zero? failed) 0 1)))
