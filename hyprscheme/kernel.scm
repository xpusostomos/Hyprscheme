;; hyprscheme-prelude.scm — the kernel module: the error plumbing, the callback
;; watchdog, the fire trampolines and the custom-layout entry points.
;;
;; `(hyprscheme kernel)` is loaded before the API modules, and is the layer the
;; host itself must be able to reach: every name C++ looks up by hand lives
;; here. It exports its own machinery together with every hl--c-* gsubr the host
;; registers into it, which is the whole of the internal layer — the families
;; import it, the public umbrella never re-exports it.
;;
;; Plain Guile throughout — no dialect shims. Anything the machinery needs
;; that is not in Guile's default environment comes from a standard library
;; below.

(define-module (hyprscheme kernel)
  ;; NOT declarative: the HOST modifies this module AFTER it is loaded — it
  ;; assigns every gsubr into the exported placeholders below
  ;; (registerAllBindings, which runs once this file has been read) and it sets
  ;; hl--ready. A declarative module says that will not happen, so the compiler
  ;; would be entitled to assume it had. (The flag also had to be off to shadow
  ;; Guile's load/eval; both shadows are gone — see the note where they were.)
  #:declarative? #f
  ;; An explicit export list, not module-export-all!: names added to a module
  ;; after it is defined do NOT reach a module that imports it later, and the
  ;; families depend on importing this one properly. So it is GENERATED —
  ;; this file's own machinery, plus every hl--c-* from the C++ side's
  ;; hl::bind<>() registrations. Regenerate it after adding a gsubr
  ;; (tools/ lists them; see the module-audit lint).
  #:export (
           hl--bind-fire-rec
           hl--bind-result hl--c-active-monitor
           hl--c-active-special-workspace hl--c-active-title
           hl--c-active-window hl--c-active-workspace hl--c-animation-set
           hl--c-bind hl--c-clear-crashed-lockscreen hl--c-config-begin
           hl--c-config-get hl--c-config-last-error
           hl--c-config-props-refreshed-listen hl--c-config-push-bool
           hl--c-config-push-int hl--c-config-push-num
           hl--c-config-push-str hl--c-config-reloaded-listen
           hl--c-config-set hl--c-config-tbl-key hl--c-config-tbl-open
           hl--c-config-tbl-set-hash hl--c-config-tbl-seti
           hl--c-config-unload-listen hl--c-current-submap
           hl--c-schedule-refresh
           hl--c-cursor-corner hl--c-cursor-move hl--c-cursor-pos
           hl--c-bezier-curve-add hl--c-device-add hl--c-dpms hl--c-enter-submap
           hl--c-event hl--c-event-active hl--c-event-cancel hl--c-exec!
           hl--c-exit hl--c-focus-direction hl--c-focus-last
           hl--c-focus-monitor hl--c-focus-urgent hl--c-focus-workspace
           hl--c-force-idle hl--c-force-renderer-reload hl--c-generation
           hl--c-gesture hl--c-gesture-maker-close
           hl--c-gesture-maker-cursor-zoom hl--c-gesture-maker-custom
           hl--c-gesture-maker-float hl--c-gesture-maker-fullscreen
           hl--c-gesture-maker-move hl--c-gesture-maker-resize
           hl--c-gesture-maker-scroll-move hl--c-gesture-maker-special
           hl--c-gesture-maker-workspace-swipe hl--c-gesture-remove
           hl--c-global hl--c-group-add
           hl--c-group-alive hl--c-group-current hl--c-group-current-idx
           hl--c-group-cycle hl--c-group-denied hl--c-group-index
           hl--c-group-lock hl--c-group-lock-all hl--c-group-lock-all-set
           hl--c-group-lock-set hl--c-group-members
           hl--c-group-move-window hl--c-group-remove hl--c-group-same
           hl--c-group-set hl--c-group-size hl--c-group-toggle
           hl--c-group? hl--c-is-key-down
           hl--c-key-logical-to-physical hl--c-key-physical-to-logical
           hl--c-key-regularise hl--c-keyboard-key-listen hl--c-last-window
           hl--c-last-workspace hl--c-layer-above-fs hl--c-layer-address
           hl--c-layer-alive hl--c-layer-kb-interactivity
           hl--c-layer-level hl--c-layer-listen hl--c-layer-mapped
           hl--c-layer-monitor hl--c-layer-namespace hl--c-layer-pid
           hl--c-layer-position hl--c-layer-rule-begin
           hl--c-layer-rule-commit hl--c-layer-rule-effect
           hl--c-layer-same hl--c-layer-size hl--c-layer? hl--c-layers
           hl--c-layout-add hl--c-layout-message hl--c-lifecycle-listen
           hl--c-loaded-plugins hl--c-monitor-10bit
           hl--c-monitor-active-special-workspace
           hl--c-monitor-active-workspace hl--c-monitor-alive
           hl--c-monitor-at hl--c-monitor-at-cursor
           hl--c-monitor-available-modes hl--c-monitor-begin
           hl--c-monitor-commit hl--c-monitor-description
           hl--c-monitor-dpms hl--c-monitor-enabled
           hl--c-monitor-event-listen hl--c-monitor-field-bool
           hl--c-monitor-field-gap hl--c-monitor-field-num
           hl--c-monitor-field-str hl--c-monitor-focused
           hl--c-monitor-from hl--c-monitor-hardware-details
           hl--c-monitor-height hl--c-monitor-mirror-of
           hl--c-monitor-mirrors hl--c-monitor-mode hl--c-monitor-name
           hl--c-monitor-names hl--c-monitor-number
           hl--c-monitor-physical-size hl--c-monitor-refresh-rate
           hl--c-monitor-reserved hl--c-monitor-same hl--c-monitor-scale
           hl--c-monitor-selector hl--c-monitor-serial
           hl--c-monitor-set-special hl--c-monitor-transform
           hl--c-monitor-vrr hl--c-monitor-width hl--c-monitor-x
           hl--c-monitor-y hl--c-monitor? hl--c-mouse
           hl--c-error-overlay hl--c-notification-add hl--c-notification-age
           hl--c-notification-alive hl--c-notification-color
           hl--c-notification-color-set hl--c-notification-dismiss
           hl--c-notification-elapsed hl--c-notification-font-size
           hl--c-notification-font-size-set hl--c-notification-icon
           hl--c-notification-icon-set hl--c-notification-list
           hl--c-notification-paused-q hl--c-notification-paused-set
           hl--c-notification-same hl--c-notification-text
           hl--c-notification-text-set hl--c-notification-timeout
           hl--c-notification-timeout-set hl--c-notification?
           hl--c-notify hl--c-on-readable hl--c-pass hl--c-permission-add
           hl--c-release-input-capture hl--c-reload-config
           hl--c-string-color
           hl--c-rule-enabled hl--c-rule-match hl--c-rule-set-enabled
           hl--c-run-finalizers hl--c-scheduled-prop-refresh-immediately
           hl--c-screenshare-listen hl--c-send-key-state
           hl--c-send-shortcut hl--c-spring-curve-add
           hl--c-state-get
           hl--c-state-set hl--c-submap-listen hl--c-timer
           hl--c-timer-cancel hl--c-timer-enabled hl--c-timer-set-enabled
           hl--c-timer-set-timeout hl--c-toggle-swallow hl--c-unbind-key
           hl--c-bind-enabled hl--c-bind-enabled-set
           hl--c-unbind-rec hl--c-urgent-window hl--c-version
           hl--c-window-accepts-input hl--c-window-address
           hl--c-window-alive hl--c-window-allowed-over-fullscreen
           hl--c-window-center hl--c-window-class hl--c-window-clear-tags
           hl--c-window-close hl--c-window-content-type
           hl--c-window-cycle hl--c-window-deny-from-group
           hl--c-window-destroy-listen hl--c-window-event-listen
           hl--c-window-float-act hl--c-window-floating
           hl--c-window-focus hl--c-window-focus-history-id
           hl--c-window-from hl--c-window-fullscreen-client
           hl--c-window-fullscreen-handler
           hl--c-window-fullscreen-mode hl--c-window-fullscreen-set
           hl--c-window-fullscreen-state
           hl--c-window-group hl--c-window-group-denied
           hl--c-window-group-lock hl--c-window-group-locked
           hl--c-window-hidden hl--c-window-ids hl--c-window-in-group
           hl--c-window-inhibiting-idle hl--c-window-initial-class
           hl--c-window-initial-title hl--c-window-into-group
           hl--c-window-into-or-create-group hl--c-window-kill
           hl--c-window-layout hl--c-window-mapped
           hl--c-window-maximized-query hl--c-window-minimize-listen
           hl--c-window-monitor-id hl--c-window-move-direction
           hl--c-window-move-px hl--c-window-move-to-workspace
           hl--c-window-out-of-group hl--c-window-pid
           hl--c-window-pin-act hl--c-window-pin-fullscreened
           hl--c-window-pinned hl--c-window-position hl--c-window-prop
           hl--c-window-pseudo hl--c-window-pseudo-query
           hl--c-window-resize-px hl--c-window-rule-begin
           hl--c-window-rule-commit hl--c-window-rule-effect
           hl--c-window-same hl--c-window-set-prop hl--c-window-signal
           hl--c-window-size hl--c-window-stable-id
           hl--c-window-swallowing-id hl--c-window-swap-direction
           hl--c-window-swap-next hl--c-window-swap-with hl--c-window-tag
           hl--c-window-tags hl--c-window-tearing-hint hl--c-window-title
           hl--c-window-visible hl--c-window-workspace-id
           hl--c-window-x11 hl--c-window-xdg-description
           hl--c-window-xdg-tag hl--c-window-zorder hl--c-window?
           hl--c-windows-from hl--c-workspace-active
           hl--c-workspace-active-listen hl--c-workspace-addressable-name
           hl--c-workspace-alive hl--c-workspace-change-id
           hl--c-workspace-empty hl--c-workspace-event-listen
           hl--c-workspace-from hl--c-workspace-fullscreen-mode
           hl--c-workspace-fullscreen-window hl--c-workspace-group-count
           hl--c-workspace-groups hl--c-workspace-has-fullscreen
           hl--c-workspace-has-urgent hl--c-workspace-last-window
           hl--c-workspace-monitor hl--c-workspace-move-monitor
           hl--c-workspace-name hl--c-workspace-names
           hl--c-workspace-number hl--c-workspace-persistent
           hl--c-workspace-rename hl--c-workspace-rule-begin
           hl--c-workspace-rule-bool hl--c-workspace-rule-commit
           hl--c-workspace-rule-gap hl--c-workspace-rule-layout-opt
           hl--c-workspace-rule-num hl--c-workspace-rule-str
           hl--c-workspace-same hl--c-workspace-selector
           hl--c-workspace-special hl--c-workspace-swap-monitors
           hl--c-workspace-tiled-layout hl--c-workspace-toggle-special
           hl--c-workspace-visible hl--c-workspace-window-count
           hl--c-workspace-windows hl--c-workspace? hl--eval
           hl--event-fire hl--event-thunk hl--event-type
           hl--fire-bool-rec hl--fire-list hl--fire-list-rec
           hl--fire-mon-rec hl--fire-str-rec hl--fire-win-rec
           hl--fire-win-state-rec hl--fire-ws-mon-rec hl--fire-ws-rec
           hl--fire-win-ws-rec
           hl--guarded-run hl--layout-call hl--layout-msg
           hl--layout-recalculate hl--layout-resize
           hl--layout-window-close hl--layout-window-open
           hl--keyword-error-name hl--now-ms hl--plist-cdr hl--plist-get
           hl--plist-has? hl--print-exception
           hl--arity-error-proc hl--arity-description hl--arity-sentence
           hl--syntax-error-where
           hl--stack-lines hl--stack-text hl--stack-config-frame
           hl--already-reported? hl--config-errors hl--loaded-files
           hl--notify-error hl--reported hl--ready hl--report
           hl--load-config hl--report-config hl--report-config-ok hl--report-watchdog
           hl--target-env
           hl--timer-fire hl--timer-interval hl--timer-thunk
           hl--watchdog-ms hl--wd-aborted hl--wd-alarm hl--wd-enter
           hl--wd-exit hl--wd-handler hl--wd-handler-set hl--wd-rearm
           hl--wd-stack hl-bind-rtd hl--bind-thunk hl--bind-tokens
           hl--bind-flags hl--bind-devices hl--bind-description
           hl-event-rtd hl-rule-rtd hl-timer-rtd))

(use-modules (ice-9 exceptions)   ; guard, make-exception, exception-* accessors
             (ice-9 session)      ; procedure-arguments — the arity in an error message
             (rnrs io ports)      ; call-with-string-output-port
             (rnrs lists)         ; exists, for-all — configs and tests use them
             (srfi srfi-1)        ; any, every, cons*, filter
             (ice-9 optargs))     ; define*

(define hl--ready #f)

;; THE GENERATION IS REBUILT ON EVERY CONFIG RELOAD; THIS MODULE IS NOT. The
;; host throws the Scheme side away and evaluates init.scm and the config into a
;; fresh module, so a config's own definitions are gone next reload with no
;; bookkeeping. The host owns that generation module and installs it as the
;; *current* module before reading init.scm, which is why nothing here carries an
;; environment argument or a generation pointer.
;;
;; What does NOT go with the generation is this file. `use-modules` loads a
;; module once per process, so there is ONE kernel for the life of the plugin
;; and its top-level variables are still here one reload later. That matters
;; because a `set!` from a generation on an IMPORTED binding mutates the
;; EXPORTING module's cell: a config's `(set! hl--watchdog-ms 500)` lands in
;; THIS file's variable and outlives the config that set it. `reset`, below,
;; puts that back, and init.scm calls it once per generation.
;;
;; The three things the host deliberately keeps of its own — the stderr port (a
;; port owns its fd), the after-gc hook (it must not stack) and the survive list
;; behind hl-state-set! and friends — are outside the wipe for the same reason
;; this module is: they must outlive it.

;; ---- error reporting ---------------------------------------------------------
;; The compositor's own stderr is where scheme errors must land. The port is
;; installed ONCE by the host (Guile.cpp), not here: a port owns the fd it
;; wraps, so re-creating it per reload would close fd 2 as soon as the previous
;; port was collected — silently losing every scheme error. The host also wraps
;; the existing fd 2 rather than opening /dev/stderr afresh, which would give
;; the log a SECOND write offset (error lines then overwrite the beginning of
;; the file — a real bug once).

;; ERRORS ARE PLAIN (error MESSAGE ARGUMENTS ...). There is no hl--error: Guile
;; already does what a helper was doing, and better. `error` builds its message
;; by appending one ~S per argument, so
;;
;;     (error "hl-bind-add!: tokens must be strings, got:" tokens)
;;
;; renders as that sentence with the value written at the end. Two things
;; follow, and they are the whole style:
;;
;;   - THE FUNCTION NAMES ITSELF IN THE MESSAGE ("hl-bind-add!: ..."), because
;;     no origin field is set any more. Same rendered line as before, and it
;;     reads the same in the log.
;;   - ARGUMENTS GO LAST. A parameter cannot sit mid-sentence, so a message is
;;     worded to put it at the end. Where a value genuinely has to be
;;     interpolated, format it into the string rather than reaching for ~a:
;;     (error (format #f "..." x)) — rare, and the wordier of the two.
;;
;; Use `write`-style arguments deliberately: a string argument renders QUOTED
;; ("foo"), which is usually what you want when the complaint is about a value.

;; Render an exception to PORT. Guile's conditions carry a "~A"-style TEMPLATE
;; as the message with the arguments as irritants (exception-message hands back
;; the raw template; the default handler is what normally formats it), so it is
;; formatted here the same way, guarded — a bad escape in a message must not
;; kill the report. A condition with no usable message is written whole.
;;
;; A PLAIN STRING is rendered as itself, not through `~s`: the watchdog reports
;; its timeout as a finished message ("watchdog: eval abandoned after 5000ms")
;; rather than as a condition, and quoting it would both read wrong and change a
;; log line the exit test matches on.
;; THE ONE CONDITION THAT DROPS ITS OWN SUBJECT. A `keyword-argument-error`
;; exists to say "you passed a keyword I do not take" — and it is the one error
;; whose message names nothing: Guile raises it with EMPTY irritants, putting the
;; keywords in the `data' slot instead (ice-9/optargs.scm:350 —
;; `(scm-error 'keyword-argument-error #f "Unrecognized keyword" '() args-tail)').
;; So a caller that formats the message against the IRRITANTS, as
;; hl--print-exception does for every other error, prints exactly "Unrecognized
;; keyword" and nothing else.
;;
;; That is not hypothetical: it cost a real diagnosis. A config using a keyword
;; against machinery that predated it raised this, and the banner the user
;; actually looks at said "Unrecognized keyword" with no clue which one — so the
;; answer, that the keyword was one that did not exist in the installed copy, was
;; invisible from the one surface meant to tell them.
;;
;; exception-args for a throw is (subr message irritants data), so the DATA is
;; the LAST element; for this condition it is the leftover argument list, whose
;; car is the offending keyword. Answers #f for anything else, so the caller can
;; use it as a cond test.
(define (hl--keyword-error-name e)
  (let ((args (guard (e2 (#t #f)) (exception-args e))))
    (and (pair? args)
         (let ((data (last args)))
           (and (pair? data) (keyword? (car data)) (car data))))))

;; THE OTHER CONDITION THAT DROPS ITS SUBJECT, and the one that cost a real
;; debugging session. `wrong-number-of-args` says only "Wrong number of
;; arguments" — for THIS condition the irritants hold the procedure that was
;; called with the wrong count, and nothing else:
;;
;;   (#f "Wrong number of arguments to ~A" (#<procedure 7f2f... at ice-9/eval.scm:330:13 ()>) #f)
;;
;; So the rendered message is "Wrong number of arguments to #<procedure
;; 7f2fa7966280 at ice-9/eval.scm:330:13 ()>" — an ADDRESS, and a file and line
;; in GUILE'S OWN EVALUATOR, and nothing at all about the config. Chris reported
;; exactly this shape from a 900-line bindings.scm and it took a whole custom
;; diagnostic to find the line, because the one surface built to tell him
;; (the banner) named nothing.
;;
;; THE PROCEDURE KNOWS MORE THAN ITS ADDRESS. `procedure-arguments` answers the
;; lambda list as data, so the count the caller got wrong can be stated — "takes
;; exactly 0 arguments" — and that is the fact that identifies the bug: a
;; `(lambda ()` used where a window argument was wanted is zero-ary, and the
;; message saying ZERO is the difference between reading the file and guessing.
;;
;; Answers #f for anything else, so the caller uses it as a cond test the same
;; way hl--keyword-error-name is used.
(define (hl--arity-error-proc e)
  (let ((irr (guard (e2 (#t '())) (exception-irritants e))))
    (and (pair? irr) (procedure? (car irr)) (car irr))))

(define (hl--arity-description p)
  "How many arguments P takes, as a phrase — the part the message leaves out.
Reads procedure-arguments, whose shape is an alist.

A FAILURE HERE IS LOGGED, not swallowed. The first version of this answered #f
on any raise, which read as safe for a reporter and was not: procedure-arguments
lives in (ice-9 session) and was not imported, so the branch silently degraded to
the old unhelpful message and said nothing about why. A reporter that hides its
own failure is the one thing an error reporter must not be, and the test for the
count is what caught it."
  (let ((args (guard (e2 (#t
                          (display "[scheme] error: hl--arity-description: " (current-error-port))
                          (hl--print-exception e2 (current-error-port))
                          (newline (current-error-port))
                          #f))
                (procedure-arguments p))))
    (and (pair? args)
         (let ((req (assq-ref args 'required))
               (opt (assq-ref args 'optional))
               (rest (assq-ref args 'rest)))
           (cond ((and (not rest) (null? opt))
                  (format #f "~a argument~a" (length req) (if (= 1 (length req)) "" "s")))
                 (rest "a variable number of arguments")
                 (else (format #f "~a to ~a arguments"
                               (length req) (+ (length req) (length opt)))))))))

;; A BACKTRACE, from a stack captured BEFORE the unwind. This is the other half
;; of the same complaint: the banner showed the message and nothing about WHERE.
;;
;; CAPTURED BEFORE, because the stack is gone by the time a guard runs: measured
;; on Guile 3.0.11, a post-unwind handler sees only its own frames, while a
;; pre-unwind handler sees the config's — so hl--report-config installs one to
;; stash the stack and this renders it. (make-stack's second argument is a DEPTH
;; LIMIT that answers #f when exceeded, not "how many frames": (make-stack #t 4)
;; is #f, which is why an early version of this looked impossible.)
;;
;; ONLY THE FRAMES THAT NAME A FILE, and of those only the ones that are NOT
;; Guile's own machinery. The rest is the reader and the evaluator — eval.scm,
;; boot-9.scm, psyntax.scm — and a wall of those is what makes a raw backtrace
;; unreadable. What is left is the config's own call path, innermost first.
;;
;; A CONFIG FILE'S PATH COMES FROM THE FRAME'S ARGUMENTS, NOT ITS SOURCE, and
;; that is the non-obvious part. The frames of the file being loaded report NO
;; source at all — `frame-source` is #f for them, because the file is loaded form
;; by form and never compiled — so a renderer that only read frame-source (as the
;; first version of this did) found the config's own frames invisible and printed
;; an empty backtrace. What identifies them is a `primitive-load` frame whose
;; argument IS the path. Measured: for a nested load the order is inner, then
;; outer, so the FIRST such frame is the innermost file — which is the one to
;; open.
(define (hl--stack-lines st)
  (if (not st)
      '()
      (let loop ((i 0) (acc '()))
        (if (>= i (stack-length st))
            (reverse acc)
            (let* ((f (stack-ref st i))
                   (src (guard (e (#t #f)) (frame-source f)))
                   (proc (guard (e (#t #f)) (frame-procedure-name f)))
                   (args (guard (e (#t #f)) (frame-arguments f))))
              (loop (+ i 1)
                    (cond
                      ;; a load frame: the path it was asked to load
                      ((and (eq? proc 'primitive-load) (pair? args) (string? (car args)))
                       (let ((base (basename (car args))))
                         ;; our own tooling and the machinery's own entry points
                         ;; are not part of the config's call path
                         (if (member base '("init.scm"))
                             acc
                             (cons (format #f "  ~a (loaded)" base) acc))))
                      ;; a compiled frame: file:line, when it has one
                      ((and (pair? src) (string? (caddr src)))
                       (let* ((path (caddr src))
                              (base (basename path)))
                         (if (member base '("eval.scm" "boot-9.scm" "psyntax.scm"
                                            "exceptions.scm" "session.scm"))
                             acc
                             (cons (if proc
                                       (format #f "  ~a:~a (~a)" base (cadddr src) proc)
                                       (format #f "  ~a:~a" base (cadddr src)))
                                   acc))))
                      (else acc))))))))

;; Render the captured stack into at most MAX lines, innermost first, so a
;; runaway config cannot build a banner taller than the screen. Answers '()
;; when there is nothing to show, which is the ordinary case for an error raised
;; outside a config load (a callback, the watchdog) — those have no captured
;; stack and their report is the message alone, as before.
(define (hl--stack-text st max)
  (let ((lines (hl--stack-lines st)))
    (if (null? lines)
        '()
        (let loop ((l lines) (n max) (acc '()))
          (cond ((null? l) (reverse acc))
                ((= n 0) (reverse (cons "  ... more" acc)))
                (else (loop (cdr l) (- n 1) (cons (car l) acc))))))))

(define (hl--print-exception e port)
  (if (string? e)
      (display e port)
      (let ((msg (guard (e2 (#t #f)) (exception-message e)))
            (irr (guard (e2 (#t #f)) (exception-irritants e))))
        (cond ((not (string? msg)) (format port "~s" e))
              ;; THE ARITY BRANCH COMES FIRST, before the general format one,
              ;; because its message DOES contain a ~A: "Wrong number of
              ;; arguments to ~A" would otherwise be formatted into the
              ;; procedure's ADDRESS, which is the unhelpful rendering this
              ;; branch exists to replace.
              ((hl--arity-error-proc e)
               => (lambda (p)
                    (let ((n (hl--arity-description p)))
                      (format port "~a~a" (hl--arity-sentence msg)
                              (if n (format #f " (the procedure takes ~a)" n) "")))))
              ((and (pair? irr) (string-index msg #\~))
               (guard (e3 (#t (display msg port)))
                 (apply simple-format port msg irr)))
              ;; the keyword the message cannot carry — see above
              ((hl--keyword-error-name e)
               => (lambda (kw) (format port "~a: ~a" msg kw)))
              (else
               ;; THE SAME LESSON AS THE KEYWORD, one condition further on. A
               ;; macro that fails to match raises `syntax-error' with the
               ;; message "source expression failed to match any pattern" and
               ;; the LOCATION in its irritants — (filename . ...) (line . ...)
               ;; (column . ...). Rendering the message alone says the call did
               ;; not match and nothing about where, which is the difference
               ;; between opening the file at line 166 and reading 900 lines to
               ;; find it. A stale define-keymap call written the old way fails
               ;; here, and the LINE is the whole answer.
               (let ((where (guard (e4 (#t #f)) (hl--syntax-error-where irr))))
                 (if where
                     (format port "~a (~a)" msg where)
                     (display msg port))))))))

(define (hl--syntax-error-where irr)
  "The \"file:line:col\" in a syntax-error's irritants as a string, or #f.
The irritants are an alist of (filename . P) (line . N) (column . N), which is
how Guile reports a read or macro-expansion failure — the location is the part
a reader needs and the message never carries it."
  (and (pair? irr) (list? irr)
       (let ((file (assq-ref irr 'filename))
             (line (assq-ref irr 'line))
             (col (assq-ref irr 'column)))
         (and (string? file) line
              (format #f "~a:~a~a" (basename file) line
                      (if col (format #f ":~a" col) ""))))))

(define (hl--arity-sentence msg)
  "Guile's arity message ends in a ~A for the procedure it names, and we have
not printed the procedure — so drop the dangling placeholder. A message that
does not end that way is left alone."
  (if (string-suffix? "to ~A" msg)
      (string-drop-right msg 5)
      msg))

;; ---- reporting an error to the user -------------------------------------------
;; An error used to go ONLY to stderr, and that is nearly invisible: the
;; compositor log does not carry scheme lines in every session (measured: zero
;; "[scheme]" lines in a live one), so a bind thunk that raised looked like a key
;; that simply does nothing. It cost real time — a scratchpad key raised on every
;; press for weeks and the only trace was a log line nobody was reading.
;;
;; SO AN ERROR NOW ALSO POPS A NOTIFICATION. That is what the Lua side does, and
;; this is modelled on it (ConfigManager::addError): a RUNTIME error — one that
;; happens in a callback, with the config already loaded — is a transient
;; notification, while a CONFIG error (one raised while the config is being
;; loaded) is the persistent banner instead. Two questions, two surfaces: "that
;; key press failed" versus "your config is broken, go and fix the file". The
;; banner half is hl--report-config, below.
;;
;; DEDUPLICATED PER (WHO, MESSAGE), which is the load-bearing part. A callback
;; that is broken raises on EVERY event — every window open, every keypress — and
;; an unthrottled notification would be worse than the silent log. The Lua caller
;; keeps a per-function table of already-reported messages (markReported,
;; LuaCaller.cpp); this keeps the same thing keyed by WHO — the name of the thing
;; that failed ("handler", "bind", ...), which callers already pass.
;;
;; The table is WEAK-KEYED on the inner tables only in the Lua version (whose key
;; is a function pointer). Here the key is a symbol or string, so it is a plain
;; table: the set of distinct `who` values is tiny and bounded by the number of
;; call sites.
(define hl--reported (make-hash-table))

(define (hl--already-reported? who message)
  (let ((seen (hash-ref hl--reported who '())))
    (if (member message seen)
        #t
        (begin
          (hash-set! hl--reported who (cons message seen))
          #f))))

;; Pop a warning notification. GUARDED AND SILENT ON FAILURE: this is called from
;; inside error handlers, and a raise here would replace the error being reported
;; with a new one — the one thing an error reporter must never do. It is also the
;; reason it calls the GSUBR rather than hl-notification-add!: (hyprscheme
;; notification) is a FAMILY, and the kernel may not reach into one.
(define (hl--notify-error text)
  (guard (e (#t #f))
    (hl--c-notification-add (list 'text text 'timeout 5000 'icon "warn"
                                 'color #xFFFF6666))))

;; The one error sink. Reports to the log always; ALSO pops a notification,
;; deduplicated, unless WHO is #f (which is how a caller says "this is expected,
;; log it quietly" — a config probe, a test's deliberate error).
;; The one error sink. Reports to the log always; ALSO pops a notification,
;; deduplicated, unless WHO is absent (which is how a caller says "this is
;; expected, log it quietly" — a config probe, a test's deliberate error).
;;
;; A rest arg rather than define*'s #:optional: this is a hot-ish path called
;; from inside guards, and #:optional needs define*, which builds a keyword
;; parser. Two fixed arities are all it has.
(define (hl--report e . who-opt)
  (let ((who (if (pair? who-opt) (car who-opt) #f)))
    (display "[scheme] error: " (current-error-port))
    (hl--print-exception e (current-error-port))
    (newline (current-error-port))
    (when who
      (let ((text (guard (e2 (#t #f))
                    (call-with-string-output-port (lambda (p) (hl--print-exception e p))))))
        (when (string? text)
          (unless (hl--already-reported? who text)
            (hl--notify-error (format #f "scheme error in ~a: ~a" who text))))))
    #f))

;; Report a WATCHDOG ABANDONMENT. Separate from hl--report only because what it
;; has is a finished message rather than a condition — and it DOES notify, which
;; is the point: the desktop was frozen for the whole budget, and the log line
;; alone is what nobody reads.
(define (hl--report-watchdog text)
  (hl--report text "a callback"))

;; ---- config errors: the BANNER, not a notification ----------------------------
;; A file that fails to load is the other half of the Lua split (see hl--report):
;; not "that key press failed" but "your config is broken, go and fix the file".
;; Hyprland answers that with its full-width ErrorOverlay banner, persistent until
;; the config loads cleanly — which is us, via hl--c-error-overlay.
;;
;; The banner ACCUMULATES rather than replaces, so two broken files both show.
;; It is cleared when a config load SUCCEEDS (hl--report-config-ok), which is the
;; only way it should ever go away: a banner left up after you have fixed the
;; file is worse than no banner.
(define hl--config-errors '())

(define (hl--stack-config-frame st)
  "The innermost frame in ST that names a file the config itself is in — a
\"file:line\" string, or #f. Guile's own machinery is skipped for the same reason
hl--stack-lines skips it: eval.scm and boot-9.scm are never what you open. The
CONFIG's files are whatever is left, and the innermost is the one the error is
in — which for a file pulled in by hl-load-config is that file and not the top
one, exactly what a reader needs."
  (let ((lines (hl--stack-lines st)))
    (and (pair? lines) (car lines))))

(define (hl--report-config e . stack-opt)
  (let ((stack (if (pair? stack-opt) (car stack-opt) #f)))
    (display "[scheme] error: " (current-error-port))
    (hl--print-exception e (current-error-port))
    (newline (current-error-port))
    ;; THE BACKTRACE GOES TO THE LOG, not to the banner, and that split is
    ;; deliberate: the banner is CAPTIONED — it is read at a glance to tell you
    ;; the config is broken — while the log line is where you go to find out
    ;; where. So the stack is written here in full (up to a sane cap) and the
    ;; banner keeps its one line per error.
    (for-each (lambda (l) (display l (current-error-port)) (newline (current-error-port)))
              (hl--stack-text stack 20))
    (guard (e2 (#t #f))
      (let* ((where (hl--stack-config-frame stack))
             ;; THE FILE GOES ON THE ERROR ITSELF, not only on the banner's
             ;; caption. hl--config-errors is the record of what went wrong and
             ;; it is read per line — by the test for it, and by anything else
             ;; that wants to say where — so a message with no file in it is a
             ;; message that cannot be acted on. A config is MANY files (this
             ;; tree's own bindings.scm pulls in others), and "your config has
             ;; errors" with nothing to open is the complaint that started this.
             (text (string-append
                     (call-with-string-output-port (lambda (p) (hl--print-exception e p)))
                     (if where (format #f "  [~a]" where) ""))))
        (set! hl--config-errors
          (if (member text hl--config-errors) hl--config-errors
              (cons text hl--config-errors)))
        ;; CAPPED LIKE THE COMPOSITOR'S OWN (which stops at ~15 lines), so a config
        ;; that fails on every form cannot build a banner taller than the screen.
        ;; hl--config-errors is NEWEST FIRST, so the newest 12 are its first 12 —
        ;; taking from the reversed list would show the OLDEST and read backwards.
        ;; "... more" goes LAST, where the truncated tail is.
        ;;
        ;; Counted with a loop rather than `take`, which ERRORS when asked for more
        ;; elements than the list has — so a config with three errors would have
        ;; crashed the banner that was trying to report it.
        (let* ((newest (let loop ((l hl--config-errors) (n 12) (acc '()))
                         (if (or (null? l) (= n 0)) (reverse acc)
                             (loop (cdr l) (- n 1) (cons (car l) acc)))))
               (truncated? (> (length hl--config-errors) 12))
               (lines (reverse newest))
               (body (string-append "Your scheme config has errors:\n"
                                    (string-join
                                      (if truncated? (append lines (list "... more")) lines)
                                      "\n"))))
          (hl--c-error-overlay body))))
    #f))

;; a config load finished with NOTHING recorded: take the banner down.
;; The host calls this only when hl--load-config answered #t, and that now means
;; hl--config-errors was already empty — so the set! below is belt-and-braces
;; rather than the mechanism. The CALL is not redundant, though: taking the
;; overlay down is this function's real job, and only the host knows that the
;; whole config finished (a config is many files, one load each).
(define (hl--report-config-ok)
  (set! hl--config-errors '())
  (guard (e (#t #f)) (hl--c-error-overlay "")))

;; ---- loading THE CONFIG: the host calls THIS, not a C loader -------------------
;; The host used to load the config through hl::evalFile in C++, which caught
;; errors with scm_c_catch and wrote them to fd 2 from C. That is exactly the
;; kind of thing that does not belong there (see the note in Guile.hpp/CLAUDE.md:
;; C++ should only be where the compositor's own API is), and it cost a visible
;; bug: a config with a SYNTAX error dies in the READER, so it never reached any
;; Scheme handler — the C handler wrote a line to a log nobody reads, and the
;; config-error banner never fired. Measured: hl--config-errors stayed empty for
;; a file with an unbalanced paren.
;;
;; ONE GUARD COVERS BOTH KINDS, which was verified before this was written: a
;; read error IS catchable by a Scheme guard around primitive-load, so a syntax
;; error and an evaluation error arrive in the same place. That is also the shape
;; the scripting API uses — one guarded pcall around the whole config
;; (ConfigManager.cpp:717) — so this is parity as well as tidiness.
;;
;; IT MUST NOT BE hl-load. That one aims at the generation (the right thing for a
;; file a config pulls in) and reports through hl--report (a runtime error, i.e.
;; a notification). This aims at whatever module the HOST has made current —
;; which is the generation — and reports through hl--report-config, the banner.
;; Same load, two different things to say about a failure.
;;
;; NOT used for init.scm, which is loaded directly by the host: init.scm is what
;; DEFINES this function, so it cannot be loaded through it. Its failure is a
;; PLUGIN bug rather than a config error, so the banner would be the wrong
;; surface anyway.
;;
;; Answers #t when the config loaded CLEANLY and #f when it did not. The host uses
;; that to decide whether to clear the banner, so what "cleanly" means here is the
;; whole of the contract — and it is NOT "the load did not raise".
;;
;; IT IS "NOTHING WAS RECORDED", and the difference is the nested config. A file
;; pulled in by hl-load-config is loaded by hl-load, which GUARDS the error itself
;; (it has to: the rest of the config must still load). So the raise never reaches
;; this function, this returns #t, and the host called hl--report-config-ok —
;; clearing the banner hl--report-config had just raised, one call earlier. The
;; user saw a notification and no overlay. (Reported by Chris, with garbage at the
;; end of a file loaded via (hl-load-config "bindings").)
;;
;; So the list is cleared HERE, at the START, and the answer is whether anything
;; was put back. That makes the nested case fall out for free: hl-load recorded
;; the error while this load was running, so the list is non-empty and this
;; answers #f, so the host does not clear. Nothing in C++ had to change.
;;
;; Clearing at the start rather than at the end is what makes this work, and it
;; loses nothing: hl--report-config-ok did exactly the same thing, one call later
;; and without the ability to see what happened in between.
;;
;; (Guile's primitive-load returns #<unspecified> on success, which is TRUTHY but
;; not #t. Returning a real boolean is what the host's scm_is_true asks for and
;; what a reader expects.)
;;
;; THE BACKTRACE IS CAPTURED IN A PRE-UNWIND HANDLER, and that is the only place
;; it exists. Measured on Guile 3.0.11: a post-unwind handler — which is what
;; `guard` is — sees only its own frames, because the stack has already been torn
;; down, while a pre-unwind handler sees the config's. So the throw is caught with
;; an explicit `catch` purely to stash the stack, and RE-RAISED so the guard below
;; still does its job on the same condition. Nothing about the error handling
;; changes; the stack is a side-channel.
;;
;; (make-stack's second argument is a DEPTH LIMIT that answers #f when exceeded,
;; NOT "how many frames to keep" — (make-stack #t 4) is #f. The tree's own
;; tools/form-trace.scm was written around that trap.)
(define (hl--load-config path)
  (set! hl--config-errors '())
  (let ((stack #f))
    (guard (e (#t (hl--report-config e stack)))
      (catch #t
        (lambda () (primitive-load path))
        (lambda (key . args) (apply throw key args))
        (lambda (key . args)
          (unless stack
            (set! stack (guard (e2 (#t #f)) (make-stack #t))))
          #f)))
    (null? hl--config-errors)))

;; ---- wall clock --------------------------------------------------------------
;; Milliseconds since the epoch, used for the watchdog's budget (a wall-clock
;; measurement, not a CPU one: the point is how long the desktop has been
;; frozen).
(define (hl--now-ms)
  (let ((now (gettimeofday)))
    (+ (* (car now) 1000) (quotient (cdr now) 1000))))

;; ---- watchdog ----------------------------------------------------------------
;; Callbacks run on the compositor's main loop: a hung callback freezes the
;; desktop, and nothing outside the process can safely kill a running Guile
;; call. This wrapper bounds them instead: SIGALRM fires on a fixed cadence,
;; the handler checks the WALL-CLOCK budget and escapes out of the callback
;; when it is exceeded — recovery, not just detection. Abandonment semantics
;; match the Lua watchdog's: partial effects stay, the callback is never
;; resumed.
;;
;; Foreign calls are not interruptible (Guile delivers asyncs at procedure
;; entry points; a SIGALRM landing inside compositor C++ crashed it once), so
;; a callback blocked INSIDE a C call escapes only when control returns to
;; Scheme; the host's C-side detector thread is the backstop for those.

(define hl--watchdog-ms 5000)   ; set! from your config; 0 disables
(define hl--wd-rearm 10000)     ; alarm ticks between wall-clock checks
(define hl--wd-stack '())
;; unique marker for "the watchdog abandoned this run"
(define hl--wd-aborted (cons 'watchdog 'aborted))

(define hl--wd-handler #f)

;; (Re)install the SIGALRM delivery path. One handler at a time: the async
;; calls whatever is currently installed.
(define (hl--wd-handler-set f)
  (set! hl--wd-handler f)
  (sigaction SIGALRM (lambda (sig) (when hl--wd-handler (hl--wd-handler)))))

;; Arm the alarm cadence. The argument is in "ticks" (the watchdog's own unit,
;; 10000 to the second); 0 disarms. Returns its argument, so callers can
;; save/restore a previous value.
(define (hl--wd-alarm ticks)
  (alarm (if (and (number? ticks) (> ticks 0))
             (max 1 (quotient ticks 10000))
             0))
  ticks)

(define (hl--wd-enter what escape)
  ;; budget + t0 + escape are captured in the handler's closure — the handler
  ;; fires asynchronously and must not read shared state. The budget is read
  ;; straight out of this closure's environment, which IS the config's
  ;; generation (this file is evaluated into it), so a config's set! is seen.
  (let* ((budget hl--watchdog-ms)
         (t0 (hl--now-ms))
         (old-handler hl--wd-handler)
         (old-ticks (hl--wd-alarm hl--wd-rearm)))
    (set! hl--wd-stack (cons (list old-handler old-ticks) hl--wd-stack))
    (hl--wd-handler-set
      (lambda ()
        (let ((ms (- (hl--now-ms) t0)))
          (if (> ms budget)
              (begin
                (hl--report-watchdog (format #f "watchdog: ~a abandoned after ~ams" what ms))
                (escape hl--wd-aborted))
              (hl--wd-alarm hl--wd-rearm)))))))

(define (hl--wd-exit)
  (let ((outer (car hl--wd-stack)))
    (set! hl--wd-stack (cdr hl--wd-stack))
    (hl--wd-alarm 0)
    (hl--wd-handler-set (car outer))
    (let ((old-ticks (cadr outer)))
      (if (and (number? old-ticks) (> old-ticks 0))
          (hl--wd-alarm old-ticks)
          (hl--wd-alarm 0)))))

;; runs THUNK under the watchdog; returns its value, or the unique
;; hl--wd-aborted marker if the budget was exceeded. Reentrant: nested
;; dispatches save/restore the outer alarm state.
(define (hl--guarded-run what thunk)
  (call/cc
    (lambda (escape)
      (dynamic-wind
        (lambda () (hl--wd-enter what escape))
        (lambda () (thunk))
        (lambda () (hl--wd-exit))))))

;; ---- reset: put this module's state back ------------------------------------
;; Called ONCE PER GENERATION by init.scm, after every module is imported and
;; before the config is read — see the note at the top of this file for why a
;; module's top-level variables do not go with the generation.
;;
;; Every variable here that a config could reach is put back to the value this
;; file gives it. The watchdog's own three, and not hl--ready: that flag is the
;; HOST's, and the host clears it before reading init.scm and sets it once it
;; knows every module loaded.
;;
;; UNEXPORTED on purpose. It is not API, and leaving it unexported is also what
;; keeps it out of the API tools' picture of what the kernel publishes. init.scm
;; finds it with `module-local-variable`, which sees own bindings whether they
;; are exported or not — and ONLY own ones, so a module that merely imports
;; something named `reset` does not answer for it.
(define (reset)
  (set! hl--watchdog-ms 5000)
  (set! hl--wd-rearm 10000)
  (set! hl--wd-stack '())
  (set! hl--wd-handler #f))

;; ---- the record machinery the trampolines below read ----------------------
;; The fire trampolines pull a user thunk out of a record, so the record
;; TYPES and their accessors live here rather than with the rest of the API:
;; the API imports the kernel, so a kernel reaching back into the API would
;; be a cycle. Keeping the types here also makes them stable identities for
;; the process, so a record made in one generation stays readable by any
;; other. (hl--plist-get lives here for the same reason: the layout entry
;; points below look callbacks up in a spec plist.)

;; ---- handles ----------------------------------------------------------------
;; A window/workspace/monitor/group/layer/notification handle is a Guile
;; FOREIGN OBJECT built by the C++ side (see Handles.hpp): the object IS the
;; handle, its family is its type, and its lifetime is a finalizer. So nothing
;; here mints, wraps or drains handles — the predicates below are the whole of
;; the handle machinery in Scheme.

;; rules carry a kind and a name; (hyprscheme core) and the rule family
;; both read them, so the type lives here with the others
(define hl-rule-rtd (make-record-type 'hl-rule '(kind name)))

(define hl-event-rtd (make-record-type 'hl-event '(type thunk)))

(define hl--event-type (record-accessor hl-event-rtd 'type))

(define hl--event-thunk (record-accessor hl-event-rtd 'thunk))

;; timers: the record IS the handle — the initial interval and the thunk.
;; C++ locks it inside the timer's fire callback; a one-shot timer releases
;; its own lock when it completes, and reload tears the rest down.

(define hl-timer-rtd (make-record-type 'hl-timer '(interval thunk)))

(define hl--timer-interval (record-accessor hl-timer-rtd 'interval))

(define hl--timer-thunk (record-accessor hl-timer-rtd 'thunk))

;; binds: the record IS the handle — the token LIST (the bind's meaning)
;; and the thunk. C++ locks the record while the bind is registered (see
;; SThunkRef) and stamps its pinned address into the bind's argument tag;
;; the record becomes collectable when the bind is unbound (or dies at
;; reload) and the user drops it. No bind ids, no registries.
;;
;; EVERYTHING INTRINSIC TO A BIND IS A FIELD. Five of them:
;;
;;   tokens       the key token list (what hl-kbd returns)
;;   thunk        the action
;;   flags        the combined eBindFlags bitmask, from the option keywords
;;   devices      the device-scope list (part of the flag DERIVATION, see
;;                hl--bind-flag-mask: a non-empty list sets DEVICE_INCLUSIVE
;;                unless the caller opted out)
;;   description  the display string
;;
;; What is emphatically NOT a field is PLACEMENT — the submap a bind lives in
;; and where it resets to. Those are arguments to hl-bind-add!, because the
;; SAME bind object can be the template for binds in several maps while a
;; placement belongs to one registration. That split is what lets a config
;; build a bind once and register it (see hl-make-bind / hl-bind-add!).
;;
;; The field COUNT is why this is not a struct-ref by index anywhere: the
;; accessors below are the only way in, so adding a field is a change to this
;; file plus whoever reads it, not a silent renumbering.
(define hl-bind-rtd (make-record-type 'hl-bind '(tokens thunk flags devices description)))

;; INTERNAL record-field accessors, hence hl-- : how the machinery takes a bind
;; apart, not something a config reads. hl--bind-tokens/hl--bind-thunk were
;; hl-bind-* once and so leaked into the public API list.
(define hl--bind-tokens (record-accessor hl-bind-rtd 'tokens))

(define hl--bind-thunk (record-accessor hl-bind-rtd 'thunk))

(define hl--bind-flags (record-accessor hl-bind-rtd 'flags))

(define hl--bind-devices (record-accessor hl-bind-rtd 'devices))

(define hl--bind-description (record-accessor hl-bind-rtd 'description))

(define (hl--plist-cdr l)
  ;; (key value rest ...) → (value rest ...); a trailing bare KEY is an error
  (let ((tail (cdr l)))
    (if (null? tail)
        (error "hl--plist: odd plist:" l)
        tail)))

;; hl--plist-has? lives HERE, not in core, because the kernel's own
;; hl--bind-result asks it — and the kernel cannot import core (core imports the
;; kernel). It sat in core while its sibling hl--plist-cdr was already here, so
;; every bind whose callback returned a plist raised `Unbound variable:
;; hl--plist-has?`, was caught by the guard, and was read as DECLINED: the key
;; was passed through instead of consumed.
(define (hl--plist-has? pl key)
  (let loop ((l pl))
    (if (null? l)
        #f
        (let ((tail (hl--plist-cdr l)))
          (if (eq? (car l) key) #t (loop (cdr tail)))))))

(define (hl--plist-get pl key default)
  (let loop ((l pl))
    (if (null? l)
        default
        (let ((tail (hl--plist-cdr l)))
          (if (eq? (car l) key)
              (car tail)
              (loop (cdr tail)))))))

;; ---- event fire helpers -------------------------------------------------------
;; every helper receives an hl-event RECORD (carried locked by its bus
;; connection) plus the payload; the handler thunk is extracted from the
;; record. Return semantics unchanged from the id-era helpers.

;; bind-callback result protocol — the full result-table contract: the Lua
;; callback may return a table {ok, pass_event, error, request_release};
;; Scheme returns it as a plist.
;;   #f                        — DECLINED (auto-consuming binds pass the key
;;                               through; repeating timers stop). Errors and
;;                               the watchdog decline too.
;;   #t / any non-plist value  — handled, 'ok defaults to #t
;;   a plist                   — the full form; 'ok filled in when absent
;; Normalized so the bind fire path always reads #f or one plist shape.
(define (hl--bind-result r)
  (cond ((not r) #f)
        ((and (pair? r) (symbol? (car r)))
         (if (hl--plist-has? r 'ok) r (cons* 'ok #t r)))
        (else '(ok #t))))

;; record-bind fire: the record arrives from the C++ capture (locked);
;; extract the thunk and run it. Contract: #f = declined, otherwise the
;; normalized result plist. Normalization runs INSIDE the guard.
(define (hl--bind-fire-rec b)
  (let ((result (guard (e (#t (begin (hl--report e "bind") hl--wd-aborted)))
                  (hl--guarded-run "callback"
                    (lambda () (hl--bind-result ((hl--bind-thunk b))))))))
    (and (not (eq? result hl--wd-aborted)) result)))

(define (hl--event-fire b)
  (let ((result (guard (e (#t (begin (hl--report e "event handler") hl--wd-aborted)))
                  (hl--guarded-run "callback" (hl--event-thunk b)))))
    (not (eq? result hl--wd-aborted))))

;; timer-record fire: extract (hl--timer-thunk b) and run it zero-arg under the
;; bind result protocol (repeating timers stop on ok #f)
(define (hl--timer-fire b)
  (let ((result (guard (e (#t (begin (hl--report e "timer") hl--wd-aborted)))
                  (hl--guarded-run "callback"
                    (lambda () (hl--bind-result ((hl--timer-thunk b))))))))
    (and (not (eq? result hl--wd-aborted)) result)))

(define (hl--fire-list-rec b lst)
  ;; same shape as its siblings: #t when the handler ran, #f when it errored or
  ;; was aborted. It used to return the handler's value on success and #f on
  ;; error, which reads as "failed" for a handler that legitimately returns #f.
  (let ((result (guard (e (#t (begin (hl--report e "event handler") hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () (apply (hl--event-thunk b) lst))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-str-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e "window event") hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-bool-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e "event") hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-ws-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e "workspace event") hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-mon-rec b arg)
  (let ((result (guard (e (#t (begin (hl--report e "monitor event") hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) arg))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-ws-mon-rec b ws mon)
  (let ((result (guard (e (#t (begin (hl--report e "workspace event") hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) ws mon))))))
    (not (eq? result hl--wd-aborted))))

;; window payloads: the id crosses and becomes a real window record here
(define (hl--fire-win-rec b window)
  (let ((result (guard (e (#t (begin (hl--report e "window event") hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) window))))))
    (not (eq? result hl--wd-aborted))))

(define (hl--fire-win-state-rec b window state)
  (let ((result (guard (e (#t (begin (hl--report e "window event") hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) window state))))))
    (not (eq? result hl--wd-aborted))))

;; window.move_to_workspace carries TWO payloads and delivers both, as the
;; compositor's own Lua does — the handler is told WHERE the window went, not
;; just that it moved. Either can be #f (see THE PAYLOAD RULE in Event.cpp).
(define (hl--fire-win-ws-rec b window ws)
  (let ((result (guard (e (#t (begin (hl--report e "window event") hl--wd-aborted)))
                  (hl--guarded-run "handler" (lambda () ((hl--event-thunk b) window ws))))))
    (not (eq? result hl--wd-aborted))))

;; bare-thunk list fire for GESTURES (the thunk travels directly, no record)
(define (hl--fire-list fn lst)
  (guard (e (#t (begin (hl--report e "gesture") #f)))
    (hl--guarded-run "handler" (lambda () (apply fn lst)))))

;; ---- the current generation ---------------------------------------------------
;; The host owns it (Host.cpp) and rebuilds it on every reload; the pointer is
;; behind this gsubr so the machinery can reach it without living there itself.
;; It is #f only before the first generation exists.
(define (hl--target-env)
  (or (hl--c-generation) (interaction-environment)))

;; LOAD AND EVAL ARE NO LONGER SHADOWED HERE, and this is where they were.
;;
;; `load` was redefined to route a config's (load "x.scm") into the generation,
;; and `eval` with it. Both are gone. Machinery that silently redefines a CORE
;; binding is a trap for everyone who reads a later file, and neither ever
;; needed to be a shadow:
;;
;;   - loading a FILE into the generation is `hl-load` now — a plain public
;;     function in (hyprscheme core), next to the other plumbing. Guile's own
;;     `load` is left exactly as documented; a config may still call it.
;;   - EVAL needed no replacement at all. `(eval x)` with no environment
;;     evaluates in (current-module), and the host leaves the GENERATION current
;;     after building one (Host.cpp buildGeneration does not restore the old
;;     module on the success path) — so a bare `(eval ...)` already lands where
;;     the shadow was putting it. The shadow bought the appearance of control,
;;     not the thing.
;;
;; hl--eval below is unrelated to either: it is the `hyprctl scheme` entry
;; point, called BY NAME from the host and never through `eval`.

;; hyprctl scheme entry: evaluate all forms in the string, reply with the
;; last value formatted, or the error text. Evals land in the current
;; generation so they see the config's definitions — and die with it, like
;; the next reload's fresh evaluation environment.
(define (hl--eval code)
  (guard (e (#t (call-with-string-output-port
                  (lambda (p)
                    (display "error: " p)
                    (hl--print-exception e p)))))
    (let ((result (hl--guarded-run "eval"
                    (lambda ()
                      (let ((env (hl--target-env)))
                        (let loop ((port (open-input-string code)) (result *unspecified*))
                          (let ((form (read port)))
                            (if (eof-object? form)
                                result
                                (loop port (eval form env))))))))))
      (if (eq? result hl--wd-aborted)
          "watchdog: eval abandoned (see the compositor log)"
          (format #f "~s" result)))))

;; ---- custom layouts -----------------------------------------------------------
;; layout event dispatch. spec is a flat PLIST of callbacks: 'recalculate
;; 'resize 'window-open 'window-close 'layout-msg.
;;
;;   recalculate fn: (area placements)              -> ((window . box) ...)
;;   resize fn:      (area placements dx dy corner) -> the same
;;   window callbacks: (window)                     -> ignored
;;   layout-msg fn:  (message)                      -> response string
;;
;; area is an hl-box; placements is ((window . box) ...), the box being where
;; that window is now. #f when the callback is absent or fails. Five direct
;; entry points (one per callback kind) — no event strings, no dispatching cond.
;;
;; LAYOUT CALLBACKS RUN UNDER THE WATCHDOG, like every other callback the
;; compositor can drive: a runaway recalculate is abandoned and reported rather
;; than freezing the compositor with only the C++ detector as a backstop. An
;; abort and an error both read as #f here, and the C++ side rejects #f — so a
;; wedged layout falls back to the default grid instead of leaving the
;; workspace unlaid-out.
(define (hl--layout-call spec tag . args)
  (let ((cb (hl--plist-get spec tag #f)))
    (and cb
         (let ((result (guard (e (#t (begin (hl--report e "layout") hl--wd-aborted)))
                         (hl--guarded-run "layout callback"
                           (lambda () (apply cb args))))))
           (and (not (eq? result hl--wd-aborted)) result)))))

(define (hl--layout-window-open spec id)
  (hl--layout-call spec 'window-open id))

(define (hl--layout-window-close spec id)
  (hl--layout-call spec 'window-close id))

(define (hl--layout-msg spec msg)
  (let ((r (hl--layout-call spec 'layout-msg msg)))
    (cond ((not r) "rejected")
          ((string? r) r)
          (else ""))))

;; recalculate: payload = (area placements) — see hl-layout-add! for the
;; contract. The host builds it; this only hands it to the callback.
(define (hl--layout-recalculate spec payload)
  (hl--layout-call spec 'recalculate (car payload) (cadr payload)))

;; resize: payload = (area placements dx dy corner); a spec without a
;; 'resize callback falls back to 'recalculate (documented behavior)
(define (hl--layout-resize spec payload)
  (let* ((area       (list-ref payload 0))
         (placements (list-ref payload 1))
         (dx         (list-ref payload 2))
         (dy         (list-ref payload 3))
         (corner     (list-ref payload 4))
         (cb         (or (hl--plist-get spec 'resize #f) (hl--plist-get spec 'recalculate #f))))
    (and cb
         (let ((result (guard (e (#t (begin (hl--report e "layout") hl--wd-aborted)))
                         (hl--guarded-run "layout resize callback"
                           (lambda () (cb area placements dx dy corner))))))
           (and (not (eq? result hl--wd-aborted)) result)))))
