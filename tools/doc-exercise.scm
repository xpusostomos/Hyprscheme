#!/usr/bin/env guile
!#
;; EXERCISE the wiki's snippets, rather than merely loading them.
;;
;;   guile --no-auto-compile -s tools/doc-exercise.scm
;;
;; tests/t-zzz-docs.sh already evaluates every ```scheme block in the wiki —
;; and that is not enough, for a reason worth stating plainly. Evaluating
;;
;;   (hl-bind-add! (hl-make-bind (hl-kbd "s-G") (lambda () ... (hl-window-into-group #f "l") ...)))
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
;;      from the API's own export list (every `-add!`, plus hl-repeat,
;;      hl-after) rather than kept by hand, since a hand-kept list is a list
;;      that drifts. hl-submap used to be in this list — it registered binds
;;      through its thunk — and is gone with it: the submap is a per-bind
;;      #:submap argument now, so the bind registering it is an `-add!`;
;;   4. once every block has been evaluated, every recorded procedure is
;;      invoked, and invoking one can record more (a thunk that registers is
;;      only run when the block that defines it is evaluated), so it repeats
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

(define (machinery-dir) (repo-dir))
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

;; READ FROM init.scm, not repeated here. This was a hand-kept list, and it
;; drifted the moment a module was added: `values` was named in init.scm (so the
;; host loaded it and module-audit was satisfied) while this tool built a tree
;; without it, and every module importing it died on "no code for module". The
;; kernel is excluded — its gsubrs are stubbed below instead, since they do not
;; exist outside the compositor.
(define (named-modules)
  ;; (use-modules (hyprscheme a) (hyprscheme b) ...), in file order. Matched on
  ;; the form being `use-modules` AND its first module being ours: init.scm also
  ;; imports the standard library with plain `use-modules`, and those are not
  ;; machinery.
  (call-with-input-file (string-append (machinery-dir) "/init.scm")
    (lambda (port)
      (let loop ((acc '()))
        (let ((form (read port)))
          (cond ((eof-object? form) (reverse acc))
                ((and (pair? form)
                      (eq? (car form) 'use-modules)
                      (pair? (cdr form))
                      (pair? (cadr form))
                      (eq? (car (cadr form)) 'hyprscheme))
                 (loop (append (reverse (map cadr (cdr form))) acc)))
                (else (loop acc))))))))

(define api-modules
  (remove (lambda (m) (eq? m 'kernel)) (named-modules)))

;; WHY THE STUB MUST ANSWER A NUMBER FOR SOME GSUBRS, and it is not a detail of
;; this harness. The stub answers #f, which is fine for a gsubr whose value the
;; machinery passes along — but the machinery COMPARES some of them against an
;; integer ((= 0 (hl--c-bind ...))), and (= 0 #f) raises. A raise inside a
;; `define`'s initial value leaves that name UNBOUND, so the failure does not
;; show up where it happened: it shows up in whatever LATER block first reads
;; the name, as an unbound-variable FAIL naming an innocent snippet. keymaps.md
;; hit it through (define-keymap NAME ...) -> hl-keymap-add! -> hl-bind-add! ->
;; (= 0 rc), rc being (hl--c-bind ...). Answering 0 for those is also simply
;; more faithful — the compositor answers 0, not #f.
(define (gsubr-symbol? s)
  "True for SYMBOL hl--c-* — a C entry point, by the name convention. A STRING
test, not symbol equality: every gsubr shares a prefix, not an identity."
  (and (symbol? s)
       (let ((name (symbol->string s)))
         (and (> (string-length name) 6) (string=? (substring name 0 6) "hl--c-")))))

(define (status-gsubrs files)
  "The gsubrs that answer a STATUS CODE rather than a value — see the note above
gsubr-symbol? for why that matters. THE COMPARISON IS OFTEN THROUGH A VARIABLE,
so this looks for both shapes: (= 0 (hl--c-x ...)) directly, and (= 0 v) where v
was bound to a gsubr call. Matching only the direct shape misses hl--c-bind."
  ;; PER FILE, deliberately — the three tables are reset for each file rather
  ;; than accumulated across all of them. A variable name is local to the form
  ;; that binds it, and the same name is reused in unrelated files: `rc` is used
  ;; in bind.scm for the bind call AND in several other modules for something
  ;; else. Joined file-wide, the later binding wins and the gsubr we were
  ;; actually after is lost. Both `by-var` and `cmp-var` therefore live inside
  ;; the per-file loop, and only `direct` is accumulated.
  (let ((direct (make-hash-table)))     ; gsubr compared against an integer
    (define (scan! x by-var cmp-var)
      (when (pair? x)
        (when (and (eq? (car x) '=)
                   (pair? (cdr x)) (integer? (cadr x))
                   (pair? (cddr x)))
          (let ((rhs (caddr x)))
            (cond ((and (pair? rhs) (gsubr-symbol? (car rhs)))
                   (hash-set! direct (car rhs) #t))
                  ((symbol? rhs) (hash-set! cmp-var rhs #t)))))
        ;; a binding `(name (gsubr ...))`: any two-element form whose first
        ;; element is a symbol and whose second is a gsubr call. Deliberately
        ;; loose — over-inclusion can only mark one extra gsubr as a status
        ;; gsubr, and only for a variable separately seen compared against an
        ;; integer.
        (when (and (symbol? (car x)) (pair? (cdr x)) (pair? (cadr x))
                   (gsubr-symbol? (car (cadr x))))
          (hash-set! by-var (car x) (car (cadr x))))
        (scan! (car x) by-var cmp-var)
        (scan! (cdr x) by-var cmp-var)))
    (for-each
      (lambda (f)
        (let ((by-var (make-hash-table)) (cmp-var (make-hash-table)))
          (call-with-input-file f
            (lambda (port)
              (let loop ((form (read port)))
                (unless (eof-object? form) (scan! form by-var cmp-var) (loop (read port))))))
          ;; join WITHIN the file: every variable compared against an integer
          ;; that was bound to a gsubr call marks that gsubr a status gsubr
          (hash-for-each (lambda (v _)
                           (let ((g (hash-ref by-var v)))
                             (when g (hash-set! direct g #t))))
                         cmp-var)))
      files)
    direct))

(define (stub-c-entry-points files)
  (let ((status (status-gsubrs files)))
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
                             (if (hash-ref status x)
                                 (lambda args 0)
                                 (lambda args #f)))))
                        ((pair? x) (walk (car x)) (walk (cdr x))))))
              (loop (read port)))))))
    files)))

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

;; this script's OWN module does not import the machinery (it is a plain
;; guile), so the bind record's type has to be resolved by hand to be compared
;; against. Resolved lazily, at first use, because the kernel is not loaded
;; until boot! runs.
(define (bind-rtd)
  (variable-ref (module-variable (resolve-module '(hyprscheme kernel))
                                 'hl-bind-rtd)))

;; THE THUNK IS READ BY FIELD INDEX, not through the kernel's accessor, and
;; that is not laziness. `hl--bind-thunk` is a `record-accessor` the kernel
;; creates; resolving that NAME out of the kernel module from here yields
;; Guile's one-argument STRUCT-FALLBACK procedure instead of the accessor —
;; the same object every time, for every record. A walk that called it would
;; capture one bogus procedure and report every bind in the wiki as the same
;; callback, which is exactly the failure this tool exists to catch, wearing a
;; disguise. struct-ref asks the record directly and cannot be shadowed.
;;
;; The index is 1 because the record is (tokens thunk flags devices ...) and
;; struct-ref counts the vtable as field 0. It is pinned by a check in this
;; file rather than left as folklore: see check-bind-thunk-slot!, called once
;; per run.
(define (bind-thunk b) (struct-ref b 1))

(define (walk-procedures x found)
  (cond ((procedure? x) (found x))
        ((pair? x) (walk-procedures (car x) found) (walk-procedures (cdr x) found))
        ((vector? x) (for-each (lambda (e) (walk-procedures e found)) (vector->list x)))
        ;; A BIND IS A RECORD, and its thunk lives IN it — the registrar for
        ;; binds is hl-bind-add!, which is now handed the whole object rather
        ;; than the thunk as a separate argument. Without this arm the walk
        ;; sees an opaque struct, finds no procedure, and every bind in the
        ;; wiki silently stops being exercised: the callback count fell from
        ;; 120 to 29 the moment the signature changed, with no failure.
        ;;
        ;; Tested by the RECORD TYPE rather than guessed at, so a record that
        ;; merely happens to hold a procedure is not walked into.
        ((and (struct? x) (eq? (struct-vtable x) (bind-rtd)))
         (walk-procedures (bind-thunk x) found))))

(define (registrar? name)
  (let ((s (symbol->string name)))
    (or (string-suffix? "-add!" s)
        ;; no hl-submap: the submap is a per-bind argument now, so a block that
        ;; puts a bind in one does it through hl-bind-add!, which "-add!"
        ;; already matches
        (memq name '(hl-repeat hl-after)))))

(define (shadow-registrars! mod)
  ;; The names to shadow come from the modules the HARNESS imports — the
  ;; families, core, extras and emacs-keymaps — NOT from a single interface.
  ;; There used to be an umbrella module `(hyprscheme)` whose interface listed
  ;; them all; it is gone, so each module is asked directly. That is the same
  ;; derivation tests/t-coverage.sh uses (tools/api-names.scm).
  (for-each
    (lambda (iface)
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
        iface))
    (cons (resolve-interface '(hyprscheme kernel))
          (map (lambda (m) (resolve-interface (list 'hyprscheme m))) api-modules))))

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
  (let ((seen '()))
    (for-each (lambda (e)
                (unless (memq (car e) seen)
                  (set! seen (cons (car e) seen))
                  (format #t "DISTINCT: ~a\n" (cdr e))))
              captured))
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
         ;; thunk is blamed on the block that registered it rather than on
         ;; whichever block happened to run last
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

(define (check-bind-thunk-slot!)
  "Pin the assumption bind-thunk's struct-ref index rests on: that field 1 of an
hl-bind record IS the thunk. Built through the machinery's own constructor, so
this fails if the record's fields are reordered — the failure mode being every
bind in the wiki silently collapsing into one bogus callback, which is a
SILENT one. Cheap, and it runs before any block does."
  (let* ((probe (lambda () 'bind-thunk-slot-probe))
         (b ((variable-ref (module-variable harness 'hl-make-bind))
             '("F24") probe)))
    (unless (eq? (bind-thunk b) probe)
      (format #t "doc-exercise: hl-bind's field 1 is not the thunk — the record's\n\
  fields were reordered and bind-thunk (struct-ref 1) is now wrong. Fix the\n\
  index in tools/doc-exercise.scm before trusting the callback count.\n")
      (exit 1))))

(define (main)
  (if (not (file-exists? (wiki-dir)))
      (begin (format #t "doc-exercise: ~a not found — skipped\n" (wiki-dir)) (exit 0)))
  (own-config-dir!)
  (boot!)
  (set! harness (make-harness))
  (check-bind-thunk-slot!)
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
