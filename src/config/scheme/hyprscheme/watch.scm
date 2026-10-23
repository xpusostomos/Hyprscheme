;; hyprscheme/watch.scm — reload when a file the config loaded changes.
;;
;; THE PROBLEM THIS SOLVES: the host loads exactly ONE file — the config — and
;; nothing in that file says what it reached. So the host cannot know that
;; (hl-load "…") pulled in a file in some other directory, and an edit to that
;; file went unnoticed. The INTERPRETER knows, because it did the loading, so the
;; list is kept here and the watching is done here.
;;
;; Only one thing has to be C++: WAITING on the file descriptor
;; (hl--c-on-readable), because that means the compositor's event loop, and a
;; Scheme read loop on this thread would block the thread that draws the screen.
;; Everything else — opening the inotify fd, adding and removing watches,
;; decoding events, and deciding what an event means — is here, in Scheme.
;;
;; A COMPOSER, like extras and emacs-keymaps, and for the same reason: it calls
;; hl-config-reload!, which a FAMILY owns, so it may not be a family itself.

(define-module (hyprscheme watch)
  ;; hl-loaded-files is the only part of this module that is API. Everything
  ;; else is hl--*-prefixed and exported because every top-level define in a
  ;; module here must be (tools/module-audit.py's rule, which is what keeps a
  ;; forgotten export from being invisible) — not because anything outside
  ;; uses them.
  #:export (hl-loaded-files
            hl--watch-loaded hl--watch-dirs hl--watch-fd hl--watch-armed?
            hl--watch-canonical hl--watch-join
            hl--watch-dir-watched? hl--watch-dir-of-wd
            hl--watch-dir! hl--watch-note-loaded!
            hl--watch-interesting? hl--watch-on-readable))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme inotify))
(use-modules (hyprscheme config))

;; guard for the hook — the same import every other module here carries for it.
;; (No srfi-1: the two alist lookups below are written out rather than reaching
;; for rassoc, which is NOT in (guile) and not in srfi-1 either. The compiler
;; caught that, and a lookup this small is clearer spelled out.)
(use-modules (ice-9 exceptions))

;; Every path the loader has reported, most recent first. Grows for the life of
;; the plugin and is never cleared: the question an event asks is "is this one of
;; them?", and a file a LATER config stopped loading is still a file some config
;; asked for — watching it one reload too long is harmless, missing one is the
;; bug this exists to fix.
(define hl--watch-loaded '())

;; (dir . wd) — the directory watches we hold, so a directory is watched once
;; however many of its files get loaded.
(define hl--watch-dirs '())

(define hl--watch-fd #f)

(define (hl-loaded-files)
  "The files the running config has loaded, most recent first. These are the
   paths whose changes reload the config: the list is what the watcher matches an
   inotify event against, so a file that is not here is not reloading anything.

   A file is added when the loader reads it — (hl-load \"…\"), (hl-load-config
   \"…\"), or a bare (load \"…\") — and stays for the life of the session, so an
   entry can outlive the config that asked for it."
  (reverse hl--watch-loaded))

;; ---- recording a load ---------------------------------------------------------
;; The path is CANONICALISED before it is stored, and that is not tidiness: the
;; loader reports the path as it was GIVEN ((hl-load "sub/x.scm") reports
;; "sub/x.scm"), while an inotify event carries a directory and a bare name. Both
;; sides have to be the same string for the match to work at all.
(define (hl--watch-canonical path)
  (or (false-if-exception (canonicalize-path path)) path))

;; An inotify event for a file inside DIR. Both halves are already canonical —
;; DIR came from a canonical path — so joining them gives the canonical path
;; back, with no symlink surprise.
(define (hl--watch-join dir name)
  (if (string=? dir "/")
      (string-append "/" name)
      (string-append dir "/" name)))

(define (hl--watch-dir-watched? dir)
  (let loop ((l hl--watch-dirs))
    (cond ((null? l) #f)
          ((string=? (caar l) dir) #t)
          (else (loop (cdr l))))))

;; the directory a watch descriptor belongs to — an event carries the wd, so
;; this is what turns it back into a path
(define (hl--watch-dir-of-wd wd)
  (let loop ((l hl--watch-dirs))
    (cond ((null? l) #f)
          ((= (cdar l) wd) (caar l))
          (else (loop (cdr l))))))

(define (hl--watch-dir! dir)
  (unless (hl--watch-dir-watched? dir)
    (let ((wd (inotify-add-watch hl--watch-fd dir
                                 (logior IN-CLOSE-WRITE IN-MOVED-TO IN-CREATE))))
      ;; a directory we cannot watch is not an error worth raising over: the
      ;; file simply does not get its reload
      (when (>= wd 0)
        (set! hl--watch-dirs (acons dir wd hl--watch-dirs))))))

(define (hl--watch-note-loaded! path)
  (let ((full (hl--watch-canonical path)))
    (unless (member full hl--watch-loaded)
      (set! hl--watch-loaded (cons full hl--watch-loaded))
      ;; Watching the DIRECTORY, not the file: a file watch follows the inode,
      ;; so an editor that saves by renaming a temp file over the target would
      ;; leave us watching something nobody will ever write to again.
      (hl--watch-dir! (dirname full)))))

;; ---- the event ---------------------------------------------------------------
(define (hl--watch-interesting? mask)
  (not (zero? (logand mask (logior IN-CLOSE-WRITE IN-MOVED-TO IN-CREATE)))))

(define (hl--watch-on-readable)
  (for-each
    (lambda (ev)
      (let ((mask (inotify-event-mask ev))
            (name (inotify-event-name ev))
            (wd   (inotify-event-wd ev)))
        ;; no name means a watch-level event (IN-IGNORED, IN-Q-OVERFLOW) rather
        ;; than a file being touched, so there is nothing to match
        (when (and name (hl--watch-interesting? mask))
          (let ((dir (hl--watch-dir-of-wd wd)))
            (when (and dir (member (hl--watch-join dir name) hl--watch-loaded))
              (hl-config-reload!))))))
    ;; One bufferful per wakeup; the fd stays readable until drained, so the
    ;; event loop calls this again for anything left over.
    (inotify-read-events hl--watch-fd)))

;; ---- wiring ------------------------------------------------------------------
;; THIS FILE IS READ ONCE — the machinery is loaded once per process — so the fd
;; is opened once and the hook is installed once, with no guard needed. If that
;; ever stops being true (a module re-read per generation), this needs one.
;;
;; NO GSBR AT THIS FILE'S TOP LEVEL, and that is not tidiness — it is what keeps
;; the machinery COMPILABLE. `guild compile` runs with the machinery directory on
;; GUILE_LOAD_PATH (it has to: modules resolve each other by name from there),
;; and Guile evaluates `<load-path>/init.scm` at startup, so compiling ANY module
;; evaluates init.scm and therefore this whole file. hl--c-on-readable exists
;; only inside the compositor, so calling it here made `make scm-compile` — and
;; so `make install` and `make hyprpm-machinery` — die with
;;
;;     guile: uncaught exception: Unbound variable: hl--c-on-readable
;;     Cannot exit gracefully when init is in progress; aborting.
;;     Aborted (core dumped)
;;
;; which hyprpm reported as a successful build. The kernel's note about keeping
;; its top level to `define`s is the same rule, one module earlier.
;;
;; So the fd is opened and the arming is ATTEMPTED, guarded. Outside the
;; compositor the attempt fails harmlessly and the fd is closed again, leaving
;; hl--watch-armed? #f (and the hook below inert).
(define hl--watch-fd (inotify-init1 (logior IN-NONBLOCK IN-CLOEXEC)))

(define hl--watch-armed?
  (guard (e (#t #f))
    (and (exact-integer? hl--watch-fd)
         (> hl--watch-fd 0)
         (begin (hl--c-on-readable hl--watch-fd hl--watch-on-readable) #t))))

(unless hl--watch-armed?
  ;; not in the compositor: nothing will ever read the fd, so do not hold it
  (when (and (exact-integer? hl--watch-fd) (> hl--watch-fd 0))
    (inotify-close hl--watch-fd))
  (set! hl--watch-fd #f))

;; %load-hook is Guile's own "a file is being loaded" notification. It fires for
;; load / load-from-path / primitive-load and does NOT fire for use-modules —
;; exactly the right set: the files a CONFIG chose to read, and none of the
;; machinery's own modules, which are loaded by name on the first generation.
;;
;; The hook that was there is CALLED THROUGH rather than replaced: Guile's
;; default is %load-announce, and dropping someone else's behaviour is not this
;; hook's business. Guarded, because it runs inside Guile's loader: a bad path
;; must never take a load down.
(set! %load-hook
  (let ((previous %load-hook))
    (lambda (file)
      (when (procedure? previous)
        (previous file))
      (guard (e (#t #f))
        (when hl--watch-armed?
          (hl--watch-note-loaded! file))))))
