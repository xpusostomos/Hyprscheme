;; Driver for tests/t-zzz-snippets.sh — run a DOCUMENTED snippet against the
;; live compositor, so the test exercises the wiki's code rather than a copy of
;; it that can drift.
;;
;; Loaded into the generation by the test (the kernel shadows `load` so a load
;; lands in the generation, which is where a config runs). Then:
;;
;;   (run-snippet-thunk "/tmp/block.scm" "s-x")
;;
;; reads the block, evaluates its *definitions* (so a snippet's helpers exist),
;; finds the `hl-bind-add!` whose key is "s-x", evaluates its thunk, calls it,
;; and returns #t. #f when the block has no such bind.
;;
;; Definitions are evaluated, the REGISTERING calls are not: registering again
;; would collide with the bind the doc test already made, and the point here is
;; the callback's behaviour, not a second bind.

(define (snippet-forms file)
  (call-with-input-file file
    (lambda (port)
      (let loop ((acc '()))
        (let ((f (read port)))
          (if (eof-object? f) (reverse acc) (loop (cons f acc))))))))

(define (callback-arg? x)
  (or (and (pair? x) (eq? (car x) 'lambda))          ; an inline lambda
      ;; a procedure passed by name: `(hl-bind-add! (hl-make-bind (hl-kbd "s-z") zoom))`
      (and (symbol? x) (module-variable (current-module) x)
           (procedure? (variable-ref (module-variable (current-module) x))))))

(define (inside-make-bind form)
  ;; A BIND FORM NESTS ITS THUNK. hl-bind-add! now takes one argument — the
  ;; hl-bind record hl-make-bind built — and the thunk is the record's second
  ;; field, so a scan of hl-bind-add!'s OWN arguments finds no lambda at all
  ;; and every bind snippet silently reports "no thunk". Return the inner
  ;; (hl-make-bind ...) form so the search continues inside it.
  (and (pair? form) (pair? (cdr form)) (pair? (cadr form))
       (eq? (car (cadr form)) 'hl-make-bind)
       (cadr form)))

(define (thunk-of form)
  ;; the callback is whichever argument IS one: the third element of a
  ;; hl-make-bind, the second of a notification, a value in a layout's plist —
  ;; one rule rather than one per shape
  (let loop ((args (if (inside-make-bind form) (cdr (inside-make-bind form)) (cdr form))))
    (cond ((null? args) #f)
          ((callback-arg? (car args)) (car args))
          (else (loop (cdr args))))))

(define (registering? form)
  (and (pair? form)
       (let ((s (symbol->string (car form))))
         (or (string-suffix? "-add!" s)
             ;; hl-submap used to be here: it registered binds through its
             ;; thunk. The submap is a per-bind #:submap argument now, so the
             ;; bind itself is the registrar and "-add!" already matches it.
             (memq (car form) '(hl-repeat hl-after))))))

(define (snippet-definitions! forms)
  ;; a snippet's defines are what its thunk closes over, so they go in first
  (for-each (lambda (f)
              (when (and (pair? f) (eq? (car f) 'define))
                (eval f (current-module))))
            forms))

(define (find-form forms pred)
  (let loop ((fs forms))
    (cond ((null? fs) #f)
          ((and (registering? (car fs)) (pred (car fs)) (thunk-of (car fs))) (car fs))
          (else (loop (cdr fs))))))

(define (run-snippet-thunk file key)
  "Evaluate FILE's definitions, then call the thunk bound to KEY."
  (let ((forms (snippet-forms file)))
    (snippet-definitions! forms)
    (let ((form (find-form forms
                           (lambda (f)
                             ;; the KEYS live inside the bind record now, so the
                             ;; search for them goes to hl-make-bind's first
                             ;; argument, not hl-bind-add!'s
                             (let* ((inner (or (inside-make-bind f) f))
                                    (keys (cadr inner)))
                               ;; a bind's keys argument is built by hl-kbd/hl-key;
                               ;; requiring that keeps a `lambda` form (also a
                               ;; pair) from matching on its last element
                               (and (pair? keys)
                                    (memq (car keys) '(hl-kbd hl-key))
                                    (equal? (list-ref keys (- (length keys) 1)) key)))))))
      (if (not form)
          #f
          (begin ((eval (thunk-of form) (current-module))) #t)))))

(define (run-snippet-fn file fn arg)
  "Evaluate FILE's definitions, then call the thunk FN was registered with,
passing ARG — a REAL object, taken live. Use for a callback whose registering
call has no key to match on (an event): the argument is the whole point, since
that is the tier where a callback using its argument as the wrong type shows
up."
  (let ((forms (snippet-forms file)))
    (snippet-definitions! forms)
    (let ((form (find-form forms (lambda (f) (eq? (car f) fn)))))
      (if (not form)
          #f
          (begin ((eval (thunk-of form) (current-module)) arg) #t)))))
