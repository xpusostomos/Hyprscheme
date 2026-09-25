;; hyprscheme-defun.scm — function metadata machinery, Emacs-style.
;;
;; (defun name (args ...) "docstring" body...) is a drop-in define for
;; NAMED FUNCTIONS that additionally records:
;;   - the arglist, verbatim, captured at macro-expansion time (Chez
;;     exposes no way to ask a closure for its formals, so the macro is
;;     the only place the full arglist exists as data)
;;   - the docstring
;;   - the file the definition was evaluated from (hl--load-source, a
;;     parameter the prelude's file loader binds around each form)
;; in a hashtable keyed by symbol. (describe-function 'name) renders it
;; plus whatever the describe hooks add.
;;
;; Plain (define (f x) ...) keeps working everywhere — defun is opt-in,
;; for the functions worth describing. Macros and lambdas stored in
;; variables cannot be captured this way (describe reports them as
;; not-a-function); that is a documented limitation, same shape as
;; Emacs (defun is for functions).
;;
;; Chez note: pattern variables SUBSTITUTE INSIDE QUOTE in a template —
;; (quote args) with a pattern variable named args would emit the
;; arglist, not the symbol. Hence the pattern variables are named
;; formals/docstring, keeping 'args/'doc as literal property keys.

;; ---- the metadata store ------------------------------------------------------

;; eq hashtable: symbols are eq-comparable, lookups are O(1)
(define hl--function-table (make-eq-hashtable))

(define (hl--function-put! sym prop value)
  (hashtable-update! hl--function-table sym
    (lambda (meta) (cons (cons prop value) meta))
    '()))

(define (hl--function-get sym prop)
  (let loop ((meta (hashtable-ref hl--function-table sym '())))
    (cond ((null? meta) #f)
          ((eq? (caar meta) prop) (cdar meta))
          (else (loop (cdr meta))))))

;; ---- defun -------------------------------------------------------------------
;;
;; The arglist is inserted verbatim into both the generated define and the
;; metadata — whatever form it takes ((a b) / (a b . rest) / (a b . o) /
;; (a b #:optional (c 1))), the function itself and the recorded form are
;; the same datum, so describe can never disagree with the real signature.
;; A leading string with at least one body form after it is the docstring;
;; a lone trailing string stays a plain return value.

(define-syntax defun
  (lambda (x)
    (syntax-case x ()
      [(_ name formals docstring body1 body ...)
       (string? (syntax->datum #'docstring))
       #'(begin
           (define (name . formals) body1 body ...)
           (hl--function-put! 'name 'args (quote formals))
           (hl--function-put! 'name 'doc docstring)
           (hl--function-put! 'name 'file (hl--load-source)))]
      [(_ name formals body1 body ...)
       #'(begin
           (define (name . formals) body1 body ...)
           (hl--function-put! 'name 'args (quote formals))
           (hl--function-put! 'name 'file (hl--load-source)))])))

;; describe hooks: each entry is (sym) -> a string appended to the describe
;; output. The machinery's own contributions (signature, doc, file) are part
;; of describe-function below; hooks are how later layers — a defcustom
;; registry, tests, anything — add theirs without touching this file
;; (Emacs's help-fns-describe-function-functions pattern).
(define hl--describe-hooks '())

(define (hl--add-describe-hook! fn)
  (set! hl--describe-hooks (cons fn hl--describe-hooks)))

;; ---- describe-function --------------------------------------------------------

;; (describe-function 'f) -> a string:
;;
;;   f is a Scheme function: (f args go here)
;;   <blank line>
;;   <docstring, or "(not documented)">
;;   <blank line>
;;   Defined in <file>
;;   <one block per describe hook that returns something>
;;
;; Returns #f if f is not bound to a procedure (unbound names included —
;; a describe of a typo says #f, not an error).
(define (describe-function sym)
  (define (env-value s)
    (guard (e (#t #f)) (eval s (hl--target-env))))
  (let ((fn (and (symbol? sym) (env-value sym))))
    (if (procedure? fn)
        (call-with-string-output-port
          (lambda (p)
            (let ((args (hl--function-get sym 'args))
                  (doc  (hl--function-get sym 'doc))
                  (file (hl--function-get sym 'file)))
              ;; signature line: the captured arglist if defun recorded one,
              ;; otherwise the name alone (a plain define — we know nothing
              ;; about its formals)
              (display (symbol->string sym) p)
              (display " is a Scheme function" p)
              (cond (args (display ": " p)
                          (write (cons sym args) p))
                    (else (display " (a plain define — no recorded arglist)" p)))
              (newline p)
              (newline p)
              (display (or doc "(not documented)") p)
              (newline p)
              (when file
                (newline p)
                (display "Defined in " p)
                (display file p)
                (newline p))
              ;; hook output, in registration order (reverse of the consed list)
              (for-each (lambda (hook)
                          (let ((line (hook sym)))
                            (when line
                              (newline p)
                              (display line p)
                              (newline p))))
                        (reverse hl--describe-hooks)))))
        #f)))
