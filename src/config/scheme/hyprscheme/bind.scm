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
           hl-unbind-key! hl-bind-enabled-set! hl-bind-enabled?))

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
  ;; ONE CHORD ONLY. hl-bind-add! is the raw call: Hyprland binds a single
  ;; key+modifier set, and it has no notion of a sequence. hl-kbd returns a LIST
  ;; OF CHORDS for a multi-key spec like "C-x C-f", and feeding that here would
  ;; otherwise die deep in C++ as "A bind requires a trigger" — because
  ;; non-string tokens are skipped — with a stale message attached. Say what is
  ;; actually wrong instead.
  (unless (every string? tokens)
    (hl--error 'hl-bind-add!
               "tokens must be a list of key-name strings, but got ~s. \
If that came from (hl-kbd \"...\") with a space in it, it is a SEQUENCE \
(a list of chords) — bind those with hl-bind-keys!, which registers the \
submaps to chain them."
               tokens))
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

(define (hl-unbind-key! tokens)
  "Remove EVERY bind registered on the same key as TOKENS — a token list,
   as (hl-kbd \"s-l\") returns, never a string. A key can carry several
   binds and this removes them all (the coarse removal, matching the
   compositor's own unbind-by-key); for precise removal use hl-unbind!.

   The comparison is the compositor's, on the bind's DISPLAY KEY, and it is
   case- and whitespace-insensitive. That is why the tokens are joined the
   way they are: a bind registered from the Lua config keeps its argument
   verbatim, so its display key is e.g. \"SUPER + comma\", which normalises
   to \"super+comma\" — the same thing a (hl-kbd \"s-,\") bind's derived
   display key normalises to. Both are reachable with this one call."
  (= 0 (hl--c-unbind-key tokens)))

(define (hl-bind-enabled-set! b on)
  "Arm or disarm bind B, keeping it registered. This is what makes a bind
   CONDITIONAL: register it once and enable it only while some predicate
   holds. A DISABLED bind does not match at all, so its key reaches the
   focused application untouched — with the app's own autorepeat, which
   synthetic key injection cannot reproduce. Returns #t on success; #f if B
   is a stale handle (already unbound, or from a previous generation)."
  (= 0 (hl--c-bind-enabled-set b (if on #t #f))))

(define (hl-bind-enabled? b)
  "#t when bind B is currently armed. #f for a stale handle — one that has
   been unbound, or that a reload has already discarded."
  (= 1 (hl--c-bind-enabled b)))

