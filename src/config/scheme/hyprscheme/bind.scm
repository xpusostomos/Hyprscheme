;; hyprscheme/bind.scm — the bind family — raw wrappers over the compositor's bind operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme bind)
  #:export (
           hl-submap hl-submap-activate! hl-submap-exit! hl-bind-add!
           hl-repeat hl-bind? hl-submap-notification-add! hl-unbind!
           hl-unbind-key!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-submap name fn . reset) "Run FN to define a submap: binds
registered inside FN are scoped to it, and the optional RESET names
the submap returned to on exit. The submap exists only if FN registers
at least one bind."
  (let* ((ctx        (hl--c-get-submap-ctx))
         (prev-name  (car ctx))
         (prev-reset (cdr ctx)))
    (dynamic-wind
      (lambda () (hl--c-set-submap-ctx name (if (null? reset) "" (car reset))))
      (lambda () (fn))
      (lambda () (hl--c-set-submap-ctx prev-name prev-reset)))))

(define (hl-submap-activate! name) "Switch the active submap to NAME
(\"\" or \"reset\" returns to the default map)."
  (= 0 (hl--c-enter-submap name)))

(define (hl-submap-exit! ) "Leave the active submap and return to the
default map."
  (= 0 (hl--c-enter-submap "")))
;; window read-side fields (LuaWindow parity)

;; ---- config: set/get config options ----------------------------------------
;; one maker address per constructor, fetched from its own one-line C
;; accessor — no name strings, no dispatch

;; helpers for the action wrappers: window #f = active; actions 'toggle/'on/'off;
;; directions "l"/"r"/"u"/"d" or the symbols left/right/up/down
;; optional-boolean convention shared by every toggle/set action:
;; 'unset (the #:key default) → 0 (toggle), #t → 1 (on), #f → 2 (off)

(define* (hl-bind-add! tokens thunk
                       #:key (description "")
                       (devices #f)
                       (device-inclusive 'unset)
                       (locked #f) (release #f) (repeat #f) (long-press #f)
                       (non-consuming #f) (auto-consuming #f) (transparent #f)
                       (ignore-mods #f) (dont-inhibit #f) (click #f) (drag #f)
                       (submap-universal #f) (allow-input-capture #f) (mouse #f))
  "Register a keybind: TOKENS is the key token list - modifiers first,
key last - normally built by (hl-kbd \"C-M-a\"), (hl-key \"SUPER+A\")
or written out literally '\"SUPER\" \"Q\" as a quoted list; a modless
key is still a list. THUNK is the action. Keyword options: #:description
(string), #:devices (list - binds are then device-inclusive unless
#:device-inclusive #f opts out), and the boolean flags #:locked,
#:release, #:repeat, #:long-press, #:non-consuming, #:auto-consuming,
#:transparent, #:ignore-mods, #:dont-inhibit, #:click, #:drag,
#:submap-universal, #:allow-input-capture, #:mouse. Returns the bind
handle; remove with (hl-unbind! B). There is no two-string shorthand in
the core API - define your own wrapper on top if you want one."
  (let* ((key (car (reverse tokens)))
         (rec (make-hl-bind tokens thunk))
         (rc (hl--c-bind rec
                        tokens
                        (hl--bind-flags #:click click #:drag drag
                                        #:devices devices
                                        #:device-inclusive device-inclusive
                                        #:locked locked #:release release
                                        #:repeat repeat #:long-press long-press
                                        #:non-consuming non-consuming
                                        #:auto-consuming auto-consuming
                                        #:transparent transparent
                                        #:ignore-mods ignore-mods
                                        #:dont-inhibit dont-inhibit
                                        #:submap-universal submap-universal
                                        #:allow-input-capture allow-input-capture
                                        #:mouse mouse #:key key)
                        description
                        (if (list? devices) devices '()))))
      (if (= 0 rc)
          rec
          (hl--error 'hl-bind-add! "~a" (hl--c-config-last-error)))))
;; ---- key specification helpers -----------------------------------------------
;; (hl-kbd "C-M-a")      — emacs syntax → token list for hl-bind-add!
;; (hl-key "SUPER+SHIFT+Q") — lua/hyprland syntax → token list

(define (hl-repeat ms thunk) "Run THUNK every MS milliseconds until
cancelled with (hl-timer-cancel!) or the config reloads. Returns an
hl-timer handle like hl-after's."
  (let ((rec (make-hl-timer ms thunk)))
    (if (= 0 (hl--c-timer rec (inexact->exact ms) 1))
        rec
        (hl--error 'hl-repeat "timer ~ams rejected, see compositor log" ms))))

(define (hl-bind? x) (and (struct? x) (eq? (struct-vtable x) hl-bind-rtd)))

(define (hl-submap-notification-add! handler)
  "Register a handler that fires when the active submap changes. The
   handler receives the submap name; an empty string means the default
   submap was restored."
  (let ((rec (make-hl-event 'submap handler)))
    (if (= 0 (hl--c-submap-listen rec))
        rec
        (hl--error 'hl-submap-notification-add! "listener rejected, see compositor log"))))

;; ---- workspace events ---------------------------------------------------------

(define (hl-unbind! b)
  "Remove a bind: pass back the hl-bind record hl-bind-add! returned."
  (= 0 (hl--c-unbind-rec b)))

(define (hl-unbind-key! key)
  "Remove EVERY bind whose display key matches KEY (case- and
   whitespace-insensitive, manager-side). For precise removal use
   hl-unbind!."
  (= 0 (hl--c-unbind-key (hl--str key))))

