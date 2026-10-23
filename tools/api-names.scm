#!/usr/bin/env guile
!#
;; Print the PUBLIC API: one name per line, derived by ASKING GUILE.
;;
;;   guile --no-auto-compile -s tools/api-names.scm
;;
;; This is the source of truth for "what is public" — not a list. It loads the
;; machinery the way the host does, then asks each module what it publishes
;; (`module-public-interface`), which cannot disagree with the code the way a
;; hand-kept list can. The list it replaces had already drifted: it named
;; hl-bind-thunk and hl-bind-tokens, two internal record accessors.
;;
;; WHAT COUNTS AS API. Every module init.scm names, EXCEPT the kernel:
;;
;;   - the kernel is the C++ BOUNDARY — gsubrs and the machinery around them.
;;     A config reaches it, and its `load`/`eval` shadow is how a config's own
;;     (load "x.scm") targets the current generation, but its exports are not
;;     the API surface (that is 368 names, 313 of them hl--c-* gsubrs).
;;   - extras and emacs-keymaps ARE API. They are extended API rather than base
;;     API — hl-kbd, define-key!, hl-gesture-add! are what a config actually
;;     calls — and they follow the same testing rule, so they are in.
;;
;; Internals are excluded by NAME, which is the convention the tree already
;; keeps: `hl--*` is internal, `*-rtd` is a record type the C++ resolves by
;; name, `make-hl-*` is a record constructor. A name that is none of those and
;; starts with `hl-` is API — and if it should not be, it is renamed, not
;; filtered.

(use-modules (srfi srfi-1))
(use-modules (ice-9 pretty-print))

(setenv "GUILE_AUTO_COMPILE" "0")

(define (machinery-dir)
  (let* ((self (car (command-line)))
         (dir (dirname (if (string-prefix? "/" self)
                           self
                           (string-append (getcwd) "/" self)))))
    (string-append dir "/..")))

(define dir (machinery-dir))

;; every module init.scm names, read FROM init.scm rather than repeated here —
;; a list in this file would be the same drift the umbrella had.
;;
;; Matched on the form being `use-modules` AND its first module being OURS:
;; init.scm also imports the standard library with plain `use-modules`, and those
;; are not machinery.
(define (named-modules)
  (call-with-input-file (string-append dir "/init.scm")
    (lambda (port)
      (let loop ((acc '()))
        (let ((form (read port)))
          (cond ((eof-object? form) (reverse acc))
                ((and (pair? form)
                      (eq? (car form) 'use-modules)
                      (pair? (cdr form))
                      (pair? (cadr form))
                      (eq? (car (cadr form)) 'hyprscheme))
                 ;; (use-modules (hyprscheme a) (hyprscheme b) ...)
                 (loop (append (reverse (cdr form)) acc)))
                (else (loop acc))))))))

;; the C entry points, stubbed into the kernel: loading the machinery for real
;; needs them, and the stubs answer #f exactly as the lints' do
(define (stub-c-entry-points files)
  (save-module-excursion
    (lambda ()
      (set-current-module (resolve-module '(hyprscheme kernel)))
      (for-each
        (lambda (f)
          (call-with-input-file f
            (lambda (port)
              (let loop ((form (read port)))
                (unless (eof-object? form)
                  (when (pair? form)
                    (let walk ((x form))
                      (cond ((symbol? x)
                             (let ((n (symbol->string x)))
                               (when (and (> (string-length n) 6)
                                          (string=? (substring n 0 6) "hl--c-"))
                                 (module-define! (current-module) x (lambda args #f)))))
                            ((pair? x) (walk (car x)) (walk (cdr x))))))
                  (loop (read port)))))))
        files))))

;; (hyprscheme kernel) -> <dir>/hyprscheme/kernel.scm
(define (module-files)
  (map (lambda (m)
         (string-append dir "/" (symbol->string (car m)) "/"
                        (symbol->string (cadr m)) ".scm"))
       (named-modules)))

;; --- load the machinery, as the host does ------------------------------------

(set! %load-path (cons dir %load-path))
(set! %load-compiled-path (cons dir %load-compiled-path))

(resolve-module '(hyprscheme kernel))
(stub-c-entry-points (cons (string-append dir "/init.scm") (module-files)))

(define gen (make-fresh-user-module))
(save-module-excursion
  (lambda ()
    (set-current-module gen)
    (primitive-load (string-append dir "/init.scm"))))

;; --- ask Guile what each module publishes ------------------------------------

(define (public-names module-name)
  "The names MODULE-NAME publishes — its #:export list, as Guile knows it."
  (let ((m (resolve-module module-name #f #:ensure #f)))
    (if (not m)
        '()
        (let ((pi (module-public-interface m)))
          (if (not pi)
              '()
              (map car (module-map (lambda (sym var) (cons sym var)) pi)))))))

;; API = exported, and not an internal BY THE NAMING CONVENTION.
;;
;; NOTE there is deliberately NO `hl-` prefix requirement here. Most API is
;; hl-*, but the Emacs-ism machinery is un-prefixed ON PURPOSE (CLAUDE.md:
;; "Public API: hl-*; internal: hl--*. Emacs-ism machinery is deliberately
;; un-prefixed"), and `define-key!` / `global-set-key!` are exported from
;; (hyprscheme emacs-keymaps) under those names. Requiring the prefix dropped
;; both — the only non-hl names in the whole tree that are API, and exactly the
;; pair the old umbrella listed. Verified by diffing this tool's output against
;; that list: the only other differences are the two internals that leaked.
(define (api-name? sym)
  (let ((n (symbol->string sym)))
    (and (not (string-prefix? "hl--" n))
         (not (string-suffix? "-rtd" n))
         (not (string-prefix? "make-hl-" n)))))

;; Modules excluded WHOLESALE, by the same argument that excludes the kernel:
;; they are BOUNDARIES, not API. The kernel is the C++ boundary — gsubrs and the
;; machinery around them. (hyprscheme inotify) is the libc boundary in exactly
;; the same shape: a table of foreign bindings, opened and driven entirely by
;; (hyprscheme watch), which nothing else imports and which no config should be
;; calling. Its names are `c-read`, `IN-CLOSE-WRITE` and so on — the inotify(7)
;; spellings, deliberately, because reading like the man page is the point of it.
;; Renaming them all to hl--inotify-* to satisfy the naming convention would make
;; the one module whose job is to mirror C harder to check against the header,
;; which is the wrong trade for a module this narrow.
;;
;; ONE MORE HERE NEEDS A REASON. The test is that the module is a foreign
;; BINDING TABLE, not that it is new or awkward.
(define (boundary-module? m)
  (memq (cadr m) '(kernel inotify)))

(define api
  (apply lset-union eq?
         (map (lambda (m)
                (if (boundary-module? m)
                    '()
                    (filter api-name? (public-names m))))
              (named-modules))))

(for-each (lambda (s) (display s) (newline))
          (sort api (lambda (a b) (string<? (symbol->string a) (symbol->string b)))))
