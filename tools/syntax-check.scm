#!/usr/bin/env -S guile --no-auto-compile -s
!#
;; syntax-check — run Guile's own reader over Scheme files, surface any
;; read error with the reader's own file:line:col.
;;
;; No hand-rolled parser: Guile's reader (read) is the parser. It reports
;; every paren/string/comment error with a position — this wrapper just
;; makes the failure loud and script-friendly (exit 1) so it can gate
;; edits to the machinery files.
;;
;; Usage: guile tools/syntax-check.scm FILE...
;; Exit 0 = every file reads clean; 1 = at least one read error.

(use-modules (ice-9 exceptions))

;; The reader's message carries the file:line:col itself, but it arrives as a
;; "~A: ~S"-style TEMPLATE with the position and the failure as IRRITANTS —
;; exception-message hands back the raw template. Printing it unformatted says
;; "~A: ~S" and names nothing, which is how this tool once reported a real read
;; error as noise. Format it the way the kernel's renderer does.
(define (render e)
  (let ((msg (guard (e2 (#t #f)) (exception-message e)))
        (irr (guard (e2 (#t #f)) (exception-irritants e))))
    (cond ((not (string? msg)) (format #f "~s" e))
          ((and (pair? irr) (string-index msg #\~))
           (guard (e3 (#t msg)) (apply simple-format #f msg irr)))
          (else msg))))

(define (check-file path)
  (with-exception-handler
      (lambda (e)
        (format #t "~a~%" (render e))
        (throw 'syntax-check-failed))
    (lambda ()
      (call-with-input-file path
        (lambda (p)
          (let loop ()
            (let ((form (read p)))
              (unless (eof-object? form)
                (loop)))))))))

(let* ((files (cdr (command-line)))
       (ok (guard (c (#t #f))
             (for-each check-file files)
             #t)))
  (format #t "~a: (~a files)~%"
          (if ok "ok" "FAILED") (length files))
  (exit (if ok 0 1)))