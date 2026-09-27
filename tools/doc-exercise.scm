#!/usr/bin/env guile
!#
;; EXERCISE the wiki's snippets, rather than merely loading them.
;;
;;   guile --no-auto-compile -s tools/doc-exercise.scm
;;
;; tests/t-zzz-docs.sh already evaluates every ```scheme block in the wiki —
;; and that is not enough, for a reason worth stating plainly. Evaluating
;;
;;   (hl-bind-add! (hl-kbd "s-G") (lambda () ... (hl-window-into-group #f "l") ...))
;;
;; cannot fail. The lambda is registered and never called, so nothing inside it
;; is ever looked up. Five names that no longer existed sat in code-snippets.md
;; and passed the whole suite, because a rename that misses the docs is
;; invisible to anything that only loads them.
;;
;; So this tool loads each block and then CALLS what it registered:
;;
;;   1. the machinery is built as the host builds it — see tools/load-check.scm,
;;      which does the same and explains it — with the C entry points stubbed
;;      into the KERNEL (where the host puts the real ones), so no compositor is
;;      needed and nothing has a side effect;
;;   2. every block is evaluated into ONE harness module: the environment a
;;      config is given. The blocks of a page build on each other there, exactly
;;      as they do for a reader who copies the page into their config;
;;   3. in that module the *registering* functions are replaced by wrappers that
;;      record every procedure they are handed. Which ones those are is DERIVED
;;      from the API's own export list (every `-add!`, plus hl-submap, hl-repeat,
;;      hl-after) rather than kept by hand, since a hand-kept list is a list
;;      that drifts;
;;   4. once every block has been evaluated, every recorded procedure is
;;      invoked, and invoking one can record more — the binds inside an
;;      hl-submap thunk are only registered when that thunk runs — so it repeats
;;      until nothing new appears.
;;
;; It FAILS on `unbound-variable`: a name the docs use that does not exist. That
;; is the rot this exists to catch, wherever inside a callback it hides. Any
;; other error is a NOTE, not a failure: the stubs answer #f for everything the
;; compositor would be asked, so `(string-append "x" (hl-window-class w))` is
;; this harness's fault, not the snippet's. Whether a snippet does the right
;; thing is the suite's business.
;;
;; The doc test's skip list (tests/wiki-examples.skip) is honoured, so this and
;; t-zzz-docs.sh agree about which blocks are deliberately schematic.

(setenv "GUILE_AUTO_COMPILE" "0")

;; a plain guile does not carry these; the machinery needs none of them
(use-modules (ice-9 ftw)     ; scandir, to list the wiki
             (ice-9 rdelim)  ; read-line, to walk it
             (srfi srfi-1))  ; sort, so the block order is deterministic

(define (repo-dir)
  (let* ((self (car (command-line)))
         (dir (dirname (if (string-prefix? "/" self)
                           self
                           (string-append (getcwd) "/" self)))))
    (string-append dir "/..")))

(define (machinery-dir) (string-append (repo-dir) "/src/config/scheme"))
(define (wiki-dir) (string-append (repo-dir) "/../Hyprscheme.wiki"))

(define (own-config-dir!)
  "Point the config environment at a scratch directory of our own.
Both variables, for the one rule: NOTHING here may resolve against the real
config. The docs reference files under $XDG_CONFIG_HOME — core.md's
split-config example loads keybinds.scm — so without this a block resolves
against the user's own config, and the lint would read, and had the file
existed LOAD AND RUN, whatever is there. tests/run.sh and tests/soak.sh set
these for the same reason."
  (let ((dir (string-append "/tmp/hyprscheme-doc-exercise-"
                            (number->string (getpid)))))
    (mkdir dir)
    (mkdir (string-append dir "/hypr"))
    ;; the files the docs expect to exist, as the test harness makes them
    (close-port (open-output-file (string-append dir "/hypr/keybinds.scm")))
    (close-port (open-output-file (string-append dir "/hypr/hyprland.scm")))
    (setenv "XDG_CONFIG_HOME" dir)
    (setenv "HYPRSCHEME_CONFIG" (string-append dir "/hypr/hyprland.scm"))))

(define failures 0)
(define notes 0)
(define invoked 0)
(define captured '())   ; ((procedure . label) ...)
(define label "")       ; the block being evaluated, for a failure to name
(define harness #f)     ; the module every block is evaluated in

;; ---- the machinery, as the host builds it ----------------------------------

(define api-modules
  '(core bind config event exec gesture group layer monitor
    notification query rule timer window workspace extras))

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

(define (load-silently path)
  (let ((prev (current-module)))
    (set-current-module (resolve-module '(guile-user)))
    (call-with-input-file path
      (lambda (port)
        (let loop ((form (read port)))
          (unless (eof-object? form)
            (eval form (current-module))
            ;; `define-module` evaluated form by form does NOT switch the
            ;; current module — only the loader's own handling of it does. Left
            ;; to itself this loop defines the whole file into (guile-user), and
            ;; the module keeps nothing but its unassigned #:export
            ;; placeholders, which every name then dereferences to: unbound.
            (when (and (pair? form) (eq? (car form) 'define-module))
              (set-current-module (resolve-module (cadr form))))
            (loop (read port))))))
    (set-current-module prev)))

(define (boot!)
  (let* ((dir (machinery-dir))
         (kernel (string-append dir "/hyprscheme/kernel.scm"))
         (api (map (lambda (m) (string-append dir "/hyprscheme/" (symbol->string m) ".scm"))
                   api-modules)))
    (resolve-module '(hyprscheme kernel))
    (set-current-module (resolve-module '(hyprscheme kernel)))
    (module-define! (current-module) 'hl--c-generation (lambda () #f))
    ;; THE STUBS GO IN THE KERNEL, because that is where the host puts the real
    ;; ones (Host.cpp: registerAllBindings() while the kernel is current). The
    ;; kernel is the one module every family imports to reach C++, and it is
    ;; read AFTER this so its #:export can name what was just defined. Stubbing
    ;; anywhere else leaves the kernel's own hl--c-* placeholders unassigned.
    (stub-c-entry-points (cons kernel api))
    (load-silently kernel)
    (for-each load-silently api)
    (load-silently (string-append dir "/hyprscheme.scm"))
    (module-set! (resolve-module '(hyprscheme kernel)) 'hl--ready #t)))

;; the environment a config is given, in ONE module shared by every block
(define (make-harness)
  (let ((m (make-fresh-user-module)))
    (module-use! m (resolve-interface '(hyprscheme kernel)))
    (for-each (lambda (x) (module-use! m (resolve-interface (list 'hyprscheme x))))
              api-modules)
    ;; the host imports this list too, so a snippet has the environment it has
    ;; always had: guard, exists, filter, define* ...
    (for-each (lambda (i) (module-use! m (resolve-interface i)))
              '((rnrs io ports) (rnrs lists) (srfi srfi-1)
                (ice-9 optargs) (ice-9 exceptions)))
    m))

;; ---- what a block registers -----------------------------------------------

(define (walk-procedures x found)
  (cond ((procedure? x) (found x))
        ((pair? x) (walk-procedures (car x) found) (walk-procedures (cdr x) found))
        ((vector? x) (for-each (lambda (e) (walk-procedures e found)) (vector->list x)))))

(define (registrar? name)
  (let ((s (symbol->string name)))
    (or (string-suffix? "-add!" s)
        (memq name '(hl-submap hl-repeat hl-after)))))

(define (shadow-registrars! mod)
  (module-map
    (lambda (name _var)
      (when (registrar? name)
        (module-define! mod name
          (lambda args
            (for-each (lambda (a)
                        (walk-procedures a (lambda (proc)
                                             (set! captured (cons (cons proc label) captured)))))
                      args)
            #t))))
    (resolve-interface '(hyprscheme))))

;; ---- reporting -------------------------------------------------------------

(define (condition-name e)
  ;; an unbound-variable condition is (unbound-variable #f "Unbound variable:
  ;; ~S" (the-name) #f) — the name is the first irritant
  (let ((args (cdr e)))
    (and (>= (length args) 3) (car (list-ref args 2)))))

(define (show e) (call-with-output-string (lambda (p) (write e p))))

;; A note is counted always and printed only when asked for: they are the
;; harness's own noise (a stub answered #f where the compositor would answer a
;; string), and 130 lines of them on every `make check` would train the reader
;; to ignore the output. DOC_EXERCISE_NOTES=1 shows them.
(define (note e)
  (set! notes (+ notes 1))
  (when (getenv "DOC_EXERCISE_NOTES")
    (format #t "  note: ~a  [in ~a]\n" (show e) label)))

(define (fail e who)
  (set! failures (+ failures 1))
  (format #t "  FAIL unbound: ~a  [in ~a]\n" (condition-name e) who))

;; ---- invoke ----------------------------------------------------------------

(define (attempt thunk n)
  "ok | retry | (key . args)"
  (catch #t
    (lambda ()
      (case n ((0) (thunk)) ((1) (thunk #f)) (else (thunk #f #f)))
      'ok)
    (lambda (key . args)
      (if (eq? key 'wrong-number-of-args) 'retry (cons key args)))))

(define (invoke thunk who)
  ;; a bind thunk takes no arguments, an event callback takes one; the arity is
  ;; not knowable without asking, so try until one is not rejected
  (let loop ((n 0))
    (if (> n 2)
        #t
        (let ((r (attempt thunk n)))
          (cond ((eq? r 'retry) (loop (+ n 1)))
                ((eq? r 'ok) #t)
                (else (if (eq? (car r) 'unbound-variable) (fail r who) (note r))
                      (loop (+ n 1))))))))

(define (run-captured!)
  (let loop ((done '()) (n 0))
    (let ((todo (let r ((l captured) (acc '()))
                  (cond ((null? l) (reverse acc))
                        ((memq (car (car l)) done) (r (cdr l) acc))
                        ((memq (car (car l)) (map car acc)) (r (cdr l) acc))
                        (else (r (cdr l) (cons (car l) acc)))))))
      (cond
        ((null? todo) #t)
        ((> n 400) (format #t "  note: stopped after 400 callbacks (registers on every run?)\n"))
        (else
         ;; the label follows the callback being run, so a thunk registered BY a
         ;; thunk (the binds inside an hl-submap) is blamed on the submap's own
         ;; block rather than on whichever block happened to run last
         (set! label (cdr (car todo)))
         (set! invoked (+ invoked 1))
         (invoke (car (car todo)) (cdr (car todo)))
         (loop (cons (car (car todo)) done) (+ n 1)))))))

;; ---- the blocks ------------------------------------------------------------

(define (read-lines path)
  (call-with-input-file path
    (lambda (p) (let r ((l (read-line p))) (if (eof-object? l) '() (cons l (r (read-line p))))))))

(define (fenced-blocks path)
  "((line . text) ...) for every ```scheme block in PATH, line being 1-based."
  (let loop ((ls (read-lines path)) (n 1) (open #f) (start 0) (buf '()) (acc '()))
    (cond
      ((null? ls) (reverse acc))
      ((and (not open) (string=? (car ls) "```scheme"))
       (loop (cdr ls) (+ n 1) #t n '() acc))
      ((and open (string=? (car ls) "```"))
       (loop (cdr ls) (+ n 1) #f 0 '()
             (cons (cons start (call-with-output-string
                                 (lambda (p)
                                   (for-each (lambda (l) (display l p) (newline p))
                                             (reverse buf)))))
                   acc)))
      (open (loop (cdr ls) (+ n 1) open start (cons (car ls) buf) acc))
      (else (loop (cdr ls) (+ n 1) open start buf acc)))))

(define (skip-patterns)
  (let ((f (string-append (repo-dir) "/tests/wiki-examples.skip")))
    (if (not (file-exists? f))
        '()
        (call-with-input-file f
          (lambda (port)
            (let loop ((l (read-line port)) (acc '()))
              (if (eof-object? l)
                  (reverse acc)
                  (let ((pat (string-trim-both (car (string-split l #\#)))))
                    (loop (read-line port)
                          (if (string=? pat "") acc (cons pat acc)))))))))))

(define (skipped? patterns text)
  (any (lambda (p) (string-contains text p)) patterns))

(define (eval-block mod text)
  (call-with-input-string text
    (lambda (port)
      (let loop ((form (read port)))
        (unless (eof-object? form)
          (catch #t
            (lambda () (eval form mod))
            (lambda (key . args)
              (let ((e (cons key args)))
                (if (memq key '(unbound-variable read-error syntax-error))
                    (fail e label)
                    (note e)))))
          (loop (read port)))))))

;; ---- main ------------------------------------------------------------------

(define (main)
  (if (not (file-exists? (wiki-dir)))
      (begin (format #t "doc-exercise: ~a not found — skipped\n" (wiki-dir)) (exit 0)))
  (own-config-dir!)
  (boot!)
  (set! harness (make-harness))
  (shadow-registrars! harness)
  (let ((blocks 0) (skipped 0) (patterns (skip-patterns)))
    (for-each
      (lambda (f)
        (when (string-suffix? ".md" f)
          (for-each
            (lambda (b)
              (set! blocks (+ blocks 1))
              (if (skipped? patterns (cdr b))
                  (set! skipped (+ skipped 1))
                  (begin (set! label (format #f "~a:~a" f (car b)))
                         (eval-block harness (cdr b)))))
            (fenced-blocks (string-append (wiki-dir) "/" f)))))
      (sort (scandir (wiki-dir)) string<?))
    (run-captured!)
    (format #t "~a: ~a blocks (~a skipped), ~a callbacks invoked, ~a notes\n"
            (if (zero? failures) "ok" "FAIL") blocks skipped invoked notes)
    (exit (if (zero? failures) 0 1))))

(main)
