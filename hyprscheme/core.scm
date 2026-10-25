;; hyprscheme/core.scm — shared Scheme plumbing: argument coercion, the plist convention, key-spec parsing,
;; the cross-reload state accessors, and the listen helpers
;;
;; imports (hyprscheme kernel) [the C++ boundary] and nothing else in the tree.
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme core)
  #:export (
           bind-flag-enum
           hl--bind-flag-mask hl--bool-act hl--chord-key hl--chord-mods
           hl--modifier-token? hl--modifier-tokens
           hl--gesture-mode
           hl--mon-arg hl--monitor-listen hl--notif-act hl--plist-fold
           hl--plist-map hl--push-val hl--rule-kind
           hl--hyprland-name hl--rule-name hl--rule-spec-value
           hl--special-name hl--str
           hl--window-listen hl--window-rule-mk hl--workspace-listen
           hl--load-into-generation
           hl--ws-arg hl-after hl-box hl-box-h hl-box-rtd hl-box-w
           hl-box-x hl-box-y hl-current-submap hl-group?
           hl-layer? hl-layout-add! hl-load hl-make-bind hl-monitor? hl-notification?
           hl-plist-get hl-state-keys hl-state-ref hl-state-remove!
           hl-state-set! hl-window? hl-workspace? make-hl-bind
           make-hl-event make-hl-rule make-hl-timer
           string->color))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme enums))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

;; ---- boxes -------------------------------------------------------------------
;; A BOX is a rectangle: where its top-left corner is, and how big it is. Four
;; numbers, nothing more. Coordinates are GLOBAL — the whole desktop is one
;; coordinate space with the monitors laid out side by side in it, so a second
;; monitor's x is not 0. Layout callbacks are handed boxes and return boxes.
;;
;; A box is a WHOLE-PIXEL rectangle. The compositor hands back exact integers,
;; and rounds anything a layout returns, so dividing the work area and putting
;; the result straight into a box is fine: (quotient (hl-box-w area) 2) works.

(define hl-box-rtd (make-record-type 'hl-box '(x y w h)))

(define (hl-box x y w h) "Make a box: a rectangle at (X, Y) of size W by H.
Coordinates are global, as everywhere in the API."
  (make-struct/no-tail hl-box-rtd x y w h))

;; record-accessor is Guile's own: it type-checks and reports "Wrong type
;; argument (want `hl-box')" naming the record type, so there is no helper here.
;; One is anonymous, though, so the docstring is attached as a property — that
;; is what procedure-documentation (and the docs) read.
(define hl-box-x (record-accessor hl-box-rtd 'x))
(set-procedure-property! hl-box-x 'documentation "The box's left edge.")
(define hl-box-y (record-accessor hl-box-rtd 'y))
(set-procedure-property! hl-box-y 'documentation "The box's top edge.")
(define hl-box-w (record-accessor hl-box-rtd 'w))
(set-procedure-property! hl-box-w 'documentation "The box's width.")
(define hl-box-h (record-accessor hl-box-rtd 'h))
(set-procedure-property! hl-box-h 'documentation "The box's height.")

(define (hl-layout-add! name . spec) "Register the custom layout NAME.
SPEC is either a single recalculate procedure or a plist of named
callbacks: 'recalculate fn, 'resize fn, 'window-open fn,
'window-close fn, 'layout-msg fn. The recalculate fn receives
(area placements): AREA is an hl-box (the work area, in global
coordinates) and PLACEMENTS is a list of (window . box) pairs, one per
window to place, each box being where that window is NOW. It returns
the same shape — the windows it wants moved, and where — and any window
it leaves out keeps its current geometry. resize additionally gets
(dx dy corner) and falls back to the recalculate fn when absent.
Keep layout state in closures around the callbacks.
Layouts register in the compositor's global registry (hence -add!),
and every callback must be NAMED — there is no shorthand that guesses
a bare lambda's role."
  (when (or (null? spec) (and (pair? spec) (procedure? (car spec))))
    (error "hl-layout-add!: the callbacks must be named, e.g. (hl-layout-add! NAME
             'recalculate LAMBDA ...) — the name given was:"
           name))
  (if (= 0 (hl--c-layout-add name spec))
      name
      (error "hl-layout-add!: layout rejected, see compositor log — the name was:" name)))

(define (hl--bool-act on?)
  (cond ((eq? on? 'unset) 0)
        (on? 1)
        (else 2)))

;; A NAME as a string, for the few places that genuinely need one rather than a
;; gsubr argument: the workspace/monitor argument resolvers (a workspace may be
;; named by a NUMBER — "2") and the two name helpers below, which do string
;; work on it. The gsubr boundary takes strings AND symbols itself, so no CALLER
;; of an API function converts any more — this is the consumer normalising.
(define (hl--str s)
  (cond ((symbol? s) (symbol->string s))
        ((number? s) (number->string s))
        (else s)))

;; flat option list -> assoc list: ('release #t 'description "x")
;; ---- plist helpers ----------------------------------------------------------
;; The API's one convention for named fields: a flat "keyword" list
;;   'release #t 'description "x"
;; — the same shape Guile uses for #:keyword arguments. A MISSING value
;; reports the DEFAULT; pass the-eof-object when absence must be told apart
;; from an explicit #f (an option that was given the value #f).

(define (hl-plist-get plist key . default) "Read KEY out of PLIST (the
plist convention used across the API: alternating key symbols and
values — gesture events, config tables). A missing key reports
DEFAULT; pass the-eof-object to tell an absent key apart from a stored
#f."
  ;; DEFAULT is optional (the docstring above says so). hl--plist-get takes it
  ;; positionally, so it is passed explicitly: the old (apply ... default) form
  ;; raised "Wrong number of arguments" when the default was omitted (apply of
  ;; an empty list), and apply cannot spread a scalar either.
  (hl--plist-get plist key (if (null? default) #f (car default))))

(define (hl--plist-fold f seed pl)
  (let loop ((l pl) (acc seed))
    (if (null? l)
        acc
        (let ((tail (hl--plist-cdr l)))
          (loop (cdr tail) (f (car l) (car tail) acc))))))

;; ---- key chords: the one mods/key split -------------------------------------
;; A key spec is ONE FLAT TOKEN LIST, modifiers first and the key last —
;; ("CTRL" "ALT" "a"), or ("F10") for a modless key (still a list). hl-kbd and
;; hl-key build them, and every function that takes a chord takes that list.
;;
;; Splitting it is therefore the same job every time, and it lives here so that
;; it is done in ONE place: (hyprscheme bind) splits it for hl-bind-add!, the
;; senders split it for the compositor, and a config that wants the two halves
;; calls these rather than writing its own (car (reverse ...)) pair — which is
;; what bind.scm and the example configs used to do.
;;
;; THE SPLIT IS POSITIONAL, NOT A MODIFIER TABLE. The last token is the key even
;; when it spells like a modifier ("s-M" is Super+M the key, per hl-kbd's own
;; docstring), so a table lookup would be wrong here — and a token list that is
;; all modifiers (hl-kbd's trailing-dash form) has no key at all, which is why
;; hl--chord-key is the one that can raise.

;; THE MODIFIER TOKEN NAMES — what hl-kbd emits for a modifier, and the list
;; that tells a modifier from a key. It lives here rather than in
;; (hyprscheme emacs-keymaps) because the chord split below needs it and a
;; family may not import emacs-keymaps; core is a leaf every family may import.
;; emacs-keymaps imports it from here for the same reason.
(define hl--modifier-tokens
  '("CTRL" "ALT" "SHIFT" "SUPER" "MOD2" "MOD3" "MOD5" "CAPS"))

(define (hl--modifier-token? tok)
  "Is TOK a modifier token rather than a key?"
  (and (string? tok) (member tok hl--modifier-tokens) #t))

(define (hl--chord-mods tokens)
  "The modifier tokens of TOKENS — the chord minus its key. #f (or '()) has
   none, so this answers '() rather than raising."
  (if (null? tokens) '() (reverse (cdr (reverse tokens)))))

(define (hl--chord-key tokens)
  "The KEY token of TOKENS: the last one. Raises when there is no key to
   return — an empty list, or a PURE MODIFIER list.

   That second case is the one worth naming: hl-kbd's trailing-dash form is a
   modifier list with no key at all, so (hl-kbd \"C-M-\") is
   (\"CTRL\" \"ALT\") — and taking `car (reverse ...)` there would hand back
   \"ALT\" and send a modifier as if it were a keypress. The last token being a
   modifier is therefore an error, not the key."
  (cond ((null? tokens)
         (error "hl--chord-key: a chord needs a key, but the token list is empty"))
        ((hl--modifier-token? (car (reverse tokens)))
         (error "hl--chord-key: a chord needs a key, not only modifiers — got:" tokens))
        (else (car (reverse tokens)))))

;; map (k v ...) → (k (f k v) ...), consing a fresh plist

(define (hl--plist-map f pl)
  (let loop ((l pl))
    (if (null? l)
        '()
        (let ((tail (hl--plist-cdr l)))
          (cons (car l) (cons (f (car l) (car tail)) (loop (cdr tail))))))))

;; ---- eBindFlags — the bind option bits ---------------------------------------
;;
;; THESE ARE MIRRORED FROM C++ BY HAND, and they are the one place in the
;; Scheme tree that has to be. `hl--bind-flag-mask` combines them into the single
;; integer the boundary takes, and a Scheme function cannot read a C++ enum — so
;; the values are copied, and the job is to make a MISMATCH VISIBLE.
;;
;; So the spellings below deliberately match src/keybinds/Bind.hpp line for
;; line: same names (modulo the case a symbol forces), SAME ORDER, and the same
;; (ash 1 N) rather than a decimal literal. A number like 32768 tells you
;; nothing; (ash 1 15) sits next to Bind.hpp's (1 << 15) and can be diffed by
;; eye. That is the whole reason this is written out rather than computed.
;;
;; The `,` before each (ash ...) is what makes this a table rather than an
;; expression: the shifts are evaluated, the pairs are written the way Bind.hpp
;; writes them, and the two can be compared line by line.
(define-enum-values bind-flag-enum
  `((locked              . ,(ash 1 0))
    (release             . ,(ash 1 1))
    (repeat              . ,(ash 1 2))
    (long-press          . ,(ash 1 3))
    (non-consuming       . ,(ash 1 4))
    (auto-consuming      . ,(ash 1 5))
    (transparent         . ,(ash 1 6))
    (ignore-mods         . ,(ash 1 7))
    (dont-inhibit        . ,(ash 1 8))
    (click               . ,(ash 1 9))
    (drag                . ,(ash 1 10))
    (submap-universal    . ,(ash 1 11))
    (allow-input-capture . ,(ash 1 12))
    (device-inclusive    . ,(ash 1 13))
    (catch-all           . ,(ash 1 14))
    (mouse               . ,(ash 1 15))))

;; Combine the option keywords into the single integer the boundary takes.
;;
;; COMBINED WITH logior, NOT +, and that is load-bearing rather than tidy. Two
;; options that mean the same bit used to ACCUMULATE: click and drag each imply
;; RELEASE, so `(+ ... )` with both set added 2+2 = 4 — which is BIND_FLAG_REPEAT,
;; so `(hl-bind-add! ... #:click #t #:release #t)` silently armed autorepeat.
;; (Measured: that call answered 516, and `(logand 516 4)` is 4.) logior is what
;; a set of bits wants and cannot collide however the options overlap.
(define* (hl--bind-flag-mask #:key (click #f) (drag #f) (devices #f)
                         (device-inclusive 'unset) (locked #f) (release #f)
                         (repeat #f) (long-press #f) (non-consuming #f)
                         (auto-consuming #f) (transparent #f)
                         (ignore-mods #f) (dont-inhibit #f)
                         (submap-universal #f) (allow-input-capture #f)
                         (mouse #f) (key ""))
  (when (and click drag)
    (error "hl-make-bind: click and drag are exclusive"))
  (when (and (or long-press release) repeat)
    (error "hl-make-bind: long-press / release is incompatible with repeat"))
  (when (and mouse (or repeat locked release))
    (error "hl-make-bind: mouse is exclusive with repeat/locked/release"))
  ;; the option keywords, one bit each
  (logior (if locked               (enum-value bind-flag-enum 'locked)               0)
          (if release              (enum-value bind-flag-enum 'release)              0)
          (if repeat               (enum-value bind-flag-enum 'repeat)               0)
          (if long-press           (enum-value bind-flag-enum 'long-press)           0)
          (if non-consuming        (enum-value bind-flag-enum 'non-consuming)        0)
          (if auto-consuming       (enum-value bind-flag-enum 'auto-consuming)       0)
          (if transparent          (enum-value bind-flag-enum 'transparent)          0)
          (if ignore-mods          (enum-value bind-flag-enum 'ignore-mods)          0)
          (if dont-inhibit         (enum-value bind-flag-enum 'dont-inhibit)         0)
          (if click                (enum-value bind-flag-enum 'click)                0)
          (if drag                 (enum-value bind-flag-enum 'drag)                 0)
          (if submap-universal     (enum-value bind-flag-enum 'submap-universal)     0)
          (if allow-input-capture  (enum-value bind-flag-enum 'allow-input-capture)  0)
          (if mouse                (enum-value bind-flag-enum 'mouse)                0)
          ;; DERIVED BITS. These are not options a caller passes; they follow
          ;; from one, and each is named here rather than inlined so the set
          ;; stays complete.
          ;;
          ;; click and drag imply RELEASE (upstream sets BIND_FLAG_RELEASE for
          ;; both) — logior, so it cannot double-count when the caller also asked
          ;; for #:release explicitly.
          (if (or click drag)      (enum-value bind-flag-enum 'release)             0)
          ;; device-inclusive is ABSENT by default and DERIVED from whether a
          ;; devices list was given: listed devices are inclusive unless the
          ;; caller opts out (upstream's default is true).
          (if (cond ((eq? device-inclusive 'unset) (pair? devices))
                    (device-inclusive #t)
                    (else #f))
              (enum-value bind-flag-enum 'device-inclusive)
              0)
          ;; the KEY NAME "catchall" implies CATCH_ALL — a value of the key
          ;; argument, not a flag the caller sets (upstream LuaBindingsToplevel.cpp)
          (if (equal? key "catchall") (enum-value bind-flag-enum 'catch-all) 0)))

(define (hl-after ms thunk) "Run THUNK once after MS milliseconds.
Returns an hl-timer handle for later control
(hl-timer-enabled-set!, hl-timer-set-timeout!, hl-timer-cancel!); a
one-shot releases its own record lock when it completes."
  (let ((rec (make-hl-timer ms thunk)))
    (if (= 0 (hl--c-timer rec (inexact->exact ms) 0))
        rec
        (error "hl-after: timer rejected, see compositor log — timeout was (ms):" ms))))

(define (hl-window? x)
  "#t when X is a window handle."
  (hl--c-window? x))

(define (hl-workspace? x)
  "#t when X is a workspace handle."
  (hl--c-workspace? x))

(define (hl-monitor? x)
  "#t when X is a monitor handle."
  (hl--c-monitor? x))

(define (hl-notification? x)
  "#t when X is a notification handle."
  (hl--c-notification? x))

;; events: the record IS the handle — the event type (a full symbol like
;; 'window-open) and the handler thunk. C++ locks the record inside the bus
;; connection (SThunkRef); when the connection is torn down at reload or
;; unload, the record unlocks and the handler becomes collectable.

(define (make-hl-event type thunk) (make-struct/no-tail hl-event-rtd type thunk))

(define (make-hl-timer interval thunk) (make-struct/no-tail hl-timer-rtd interval thunk))

(define (make-hl-bind tokens thunk flags devices description)
  (make-struct/no-tail hl-bind-rtd tokens thunk flags devices description))

(define* (hl-make-bind tokens thunk
                        #:key (description "")
                        (devices #f) (device-inclusive 'unset)
                        (locked #f) (release #f) (repeat #f) (long-press #f)
                        (non-consuming #f) (auto-consuming #f) (transparent #f)
                        (ignore-mods #f) (dont-inhibit #f) (click #f) (drag #f)
                        (submap-universal #f) (allow-input-capture #f) (mouse #f))
  "Build a bind OBJECT — everything intrinsic to the binding — without
registering it. TOKENS is the key token list (modifiers first, key last) as
(hl-kbd \"C-M-a\") returns, and THUNK is the action.

The keyword options are exactly the ones hl-bind-add! used to take:
#:description, #:devices, #:device-inclusive and the boolean flags #:locked,
#:release, #:repeat, #:long-press, #:non-consuming, #:auto-consuming,
#:transparent, #:ignore-mods, #:dont-inhibit, #:click, #:drag,
#:submap-universal, #:allow-input-capture, #:mouse.

What is NOT here is PLACEMENT: the submap the bind lives in and where it
resets to belong to a REGISTRATION, not to the bind, so they are arguments to
hl-bind-add! instead:

  (hl-bind-add! (hl-make-bind (hl-kbd \"C-x\") thunk #:locked #t))

That split is the point: the same bind object can be registered in more than
one place, and each registration is a separate bind in the compositor with its
own handle.

Exclusivity is checked HERE, because this is the call that takes the options:
click and drag are mutually exclusive, long-press/release are incompatible with
repeat, and mouse is exclusive with repeat/locked/release. Returns the bind
object; see hl-bind-add! to register it."
  ;; ONE CHORD ONLY. The compositor binds a single key+modifier set and has no
  ;; notion of a sequence. hl-kbd returns a LIST OF CHORDS for a multi-key spec
  ;; like "C-x C-f"; feeding that here would otherwise die deep in C++ as "A
  ;; bind requires a trigger" — non-string tokens are skipped there — with a
  ;; stale message attached. Say what is actually wrong instead.
  (unless (every string? tokens)
    (error "hl-make-bind: tokens must be a list of key-name strings, but got (see the value below). \
If that came from (hl-kbd \"...\") with a space in it, it is a SEQUENCE \
(a list of chords) — bind those with global-set-key!, which registers the \
submaps to chain them. The value was:"
           tokens))
  (make-hl-bind tokens thunk
                (hl--bind-flag-mask #:click click #:drag drag
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
                                    #:mouse mouse
                                    #:key (car (reverse tokens)))
                (if (list? devices) devices '())
                description))

(define (hl--special-name s)
  ;; normalize a special-workspace name/selector to its bare name, so an
  ;; open/close comparison survives a leading "special:" on either side
  (let ((s (hl--str s)))
    (if (and (> (string-length s) 8) (string=? (substring s 0 8) "special:"))
        (substring s 8 (string-length s))
        s)))

(define (hl--hyprland-name s)
  ;; A Scheme identifier as the compositor spells it: '-' becomes '_'. Scheme
  ;; names use dashes and the compositor's registries use underscores
  ;; ("border-color" vs "border_color"), so every name compared against one of
  ;; those tables goes through here first — in Scheme, where the dispatch
  ;; happens, as well as in C++ where the lookup does.
  ;;
  ;; Only '-' changes, so a name already written with underscores passes
  ;; through untouched and both spellings keep working.
  (let* ((s (hl--str s))
         (n (string-length s)))
    (let loop ((i 0) (acc '()))
      (if (= i n)
          (list->string (reverse acc))
          (let ((c (string-ref s i)))
            (loop (+ i 1) (cons (if (char=? c #\-) #\_ c) acc)))))))

(define (hl--push-val v)
  (cond ((number? v)
         ;; exact integers must reach INT options as integers (upstream
         ;; rejects doubles for them); FLOAT options accept integers too
         (if (exact? v)
             (hl--c-config-push-int (exact->inexact v))
             (hl--c-config-push-num v)))
        ((boolean? v) (hl--c-config-push-bool v))
        ((string? v) (hl--c-config-push-str v))
        ((and (pair? v) (symbol? (car v)))
         ;; plist → hash table (the API's nested named-fields form)
         (hl--c-config-tbl-open 1)
         (hl--plist-fold (lambda (k val _)
                           (hl--c-config-tbl-key k)
                           (hl--push-val val)
                           (hl--c-config-tbl-set-hash))
                         0 v))
        ((and (list? v) (or (null? v) (not (pair? (car v)))))
         ;; array table (e.g. a vec2 '(20 20))
         (hl--c-config-tbl-open 0)
         (let loop ((rest v) (i 1))
           (unless (null? rest)
             (hl--push-val (car rest))
             (hl--c-config-tbl-seti i)
             (loop (cdr rest) (+ i 1)))))
        (else (error "hl-config-add!: unsupported value" v))))

(define (make-hl-rule kind name) (make-struct/no-tail hl-rule-rtd kind name))

;; ---- colour ------------------------------------------------------------------
;; A COLOUR IS A NUMBER, everywhere in this API: 0xAARRGGBB, alpha first, the
;; packed-ARGB32 layout the compositor uses. Scheme writes it as a hex literal
;; (#xffffa500 is opaque orange), so nothing has to be parsed and no string
;; format has to be learned.
;;
;; This is the ONE way in for a colour written as a string, and it is a separate
;; function on purpose: it makes the string form something a config asks for by
;; name, instead of every colour argument quietly accepting one. The parser is
;; the compositor's OWN (ParserUtils::parseColor, via the hl--c-string-color
;; gsubr) — the same one the config file, the rule engine, the IPC commands and
;; the scripting API use — so what it accepts here is exactly what Hyprland
;; accepts anywhere else: "0x…", "#rgb", "#rrggbb", "#rrggbbaa", "rgba(...)",
;; "rgb(...)", comma forms, and a bare decimal.
;;
;; The name is `string->color`, not `hl-*`, because it is a CONVERSION rather
;; than a compositor operation, and the convention for that in this tree is the
;; un-prefixed arrow form (the same shape as string->symbol). It is in core, not
;; a family, so every module and every config can reach it.
(define (string->color s)
  "Parse a colour STRING and return the 0xAARRGGBB integer the colour
   arguments of this API take — or #f when the string is not a colour.

   Every colour argument here wants the NUMBER (#xffffa500 for opaque
   orange), so this exists only for the case where a colour arrives as
   text — out of the environment, a file, or a value copied from a
   hyprland.conf. Prefer the literal:

     (hl-notify! \"hi\" 5000 #:color #xffffa500)
     (hl-notify! \"hi\" 5000 #:color (string->color \"rgba(ffa500ff)\"))

   Accepts what Hyprland itself accepts wherever it reads a colour:
   \"0xFFA500FF\", \"#fa5f\", \"#ffa500\", \"#ffa500ff\", \"rgba(ffa500ff)\",
   \"rgba(255,165,0,1.0)\", \"rgb(255,165,0)\", or decimal digits. Note the
   orderings differ, which is the whole reason to prefer the number: the
   integer is ALPHA FIRST (#xAARRGGBB), while \"#RRGGBBAA\" and
   \"rgba(RRGGBBAA)\" are RGB first. The parser reorders those for you."
  (hl--c-string-color s))
(define hl--rule-kind (record-accessor hl-rule-rtd 'kind))

(define hl--rule-name (record-accessor hl-rule-rtd 'name))

(define (hl--rule-spec-value v)
  (cond ((string? v) v)
        ((boolean? v) (if v "true" "false"))
        ((number? v) (number->string v))
        (else #f)))

(define (hl--window-rule-mk name spec begin-fn effect-fn commit-fn what kind)
  (let ((enabled (hl--plist-get spec 'enabled #t)))
    (if (not (= 0 (begin-fn (if name name "") enabled)))
        (error (format #f "~a: ~a" what (hl--c-config-last-error)))
        (let loop ((rest spec))
          (cond ((null? rest)
                 ;; done: make the handle record, commit, index it C++-side
                 (let ((rec (make-hl-rule kind (or name ""))))
                   (if (not (= 0 (commit-fn rec)))
                       (error (format #f "~a: ~a" what (hl--c-config-last-error)))
                       rec)))
                (else
                 (let* ((tail (hl--plist-cdr rest))
                        ;; a plist KEY, as a string: the branches below compare
                        ;; it against string literals ("match", "enabled", ...)
                        (k  (hl--str (car rest)))
                        (v  (car tail)))
                   (cond ((equal? k "match")
                          (let mloop ((m v))
                            (cond ((null? m) (loop (cdr tail)))
                                  (else
                                   (let* ((mtail (hl--plist-cdr m))
                                          (mk (car m))
                                          (sv (hl--rule-spec-value (car mtail))))
                                     (if (not sv)
                                         (error (format #f "~a: bad match value for" what) mk)
                                         (if (= 0 (hl--c-rule-match mk sv))
                                             (mloop (cdr mtail))
                                             (error (format #f "~a: ~a" what (hl--c-config-last-error))))))))))
                         ((equal? k "enabled")
                          (loop (cdr tail)))
                         (else
                          (let ((sv (hl--rule-spec-value v)))
                            (if (not sv)
                                (error (format #f "~a: bad effect value for" what) k)
                                (if (= 0 (effect-fn k sv))
                                    (loop (cdr tail))
                                    (error (format #f "~a: ~a" what (hl--c-config-last-error)))))))))))))))

(define (hl-group? x)
  "#t when X is a group handle."
  (hl--c-group? x))

(define (hl--ws-arg ws)
  (if (hl-workspace? ws)
      (or (hl--c-workspace-selector ws) "")
      (hl--str ws)))

(define (hl--mon-arg m)
  (if (hl-monitor? m)
      (or (hl--c-monitor-selector m) "")
      (hl--str m)))

;; -- workspace getters

(define (hl-layer? x)
  "#t when X is a layer-surface handle."
  (hl--c-layer? x))

(define (hl--notif-act who act)
  (if (= 0 act) #t (error (format #f "~a: ~a" who (hl--c-config-last-error)))))

(define (hl--gesture-mode who mode allowed)
  "Validate a gesture maker's #:mode against ALLOWED, for WHO; the value."
  (unless (memq mode allowed)
    (error (format #f "~a: #:mode must be one of ~a, got" who allowed) mode))
  mode)

(define (hl--window-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (hl--c-window-event-listen rec which))
        rec
        (error (format #f "~a: listener rejected, see compositor log" who)))))

(define (hl--workspace-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (hl--c-workspace-event-listen rec which))
        rec
        (error (format #f "~a: listener rejected, see compositor log" who)))))

(define (hl--monitor-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (hl--c-monitor-event-listen rec which))
        rec
        (error (format #f "~a: listener rejected, see compositor log" who)))))

(define (hl-current-submap )
  "The active submap's name, or the empty string when no submap is
   active."
  (or (hl--c-current-submap) ""))

(define (hl-state-set! k v) "Cross-reload state: store VALUE under KEY.
The state survives config reloads (the one deliberate cross-generation
bridge — the state survives, resources die with their generation); a
re-set keeps the original position. Returns VALUE."
  (define (put l)
    (cond ((null? l) (list k v))
          ((eq? (car l) k) (append (list k v) (cddr l)))
          (else (cons (car l) (cons (cadr l) (put (cddr l)))))))
  (hl--c-state-set (put (hl--c-state-get)))
  v)

(define (hl-state-ref k . default) "Cross-reload state: KEY's value, or
DEFAULT (#f when omitted) when the key is missing."
  (let ((v (hl--plist-get (hl--c-state-get) k the-eof-object)))
    (if (eq? v the-eof-object) (if (null? default) #f (car default)) v)))

(define (hl-state-keys ) "Cross-reload state: the list of stored keys."
  (let loop ((l (hl--c-state-get)) (acc '()))
    (if (null? l) (reverse acc) (loop (cddr l) (cons (car l) acc)))))

(define (hl-state-remove! k) "Remove KEY from the cross-reload state;
#t when it was there, #f if not."
  (let loop ((l (hl--c-state-get)) (acc '()))
    (cond ((null? l)
           (hl--c-state-set (reverse acc))
           #f)
          ((eq? (car l) k)
           (hl--c-state-set (append (reverse acc) (cddr l)))
           #t)
          (else (loop (cddr l)
                      (cons (cadr l) (cons (car l) acc)))))))


;; the after-gc-hook that pumps the collector's finalizer queue is installed
;; ONCE by the host (Host.cpp), with the finalizer gsubr itself as the thunk.
;; It cannot be registered here: a hook is process-global, so one per generation
;; would stack a fresh closure on every reload.

;; hl--ready is the KERNEL's flag and the HOST owns it — it clears the flag
;; before init.scm is read and sets it once every module has loaded. Clearing it
;; matters now that the kernel is loaded once: on the second generation on the
;; flag would still be #t from last time, and the host's check would pass
;; without the API having loaded at all. Nothing here sets it — a set! of an
;; imported binding is a syntax error in a declarative module, and the host is
;; the one that actually knows loading finished.

;; ---- loading a file into the generation --------------------------------------
;; A config's (load "x.scm") has to land in the CURRENT GENERATION, not in
;; whichever module the interpreter happened to start in. That is the whole
;; reason this exists — and it is a plain function rather than a redefinition of
;; Guile's `load`, because silently redefining a core binding for every later
;; file is a trap, and nobody needs it: the one call that must aim at the
;; generation says so by name.
;;
;; The host keeps the generation reachable through hl--c-generation, which is
;; the only correct answer when this is called from a callback rather than from
;; the config body — the current module is the generation then too, but relying
;; on that would be relying on a detail of how the host leaves the interpreter.
(define (hl--load-into-generation path keep-stack!)
  "Load PATH into the current generation, handing a backtrace to KEEP-STACK!
before the unwind discards it — see hl-load, which is the API this serves, for
why that is needed.

Split out as its own procedure for the reason given at its call site: the
four-argument `catch` nested directly in a guard's body warns \"possibly wrong
number of arguments\" in this module, and a named procedure does not.

The file goes into whatever module is current when this runs. A config's own
file is loaded while the GENERATION is current, so that is where its defines
land; with no generation — a plain `guile`, a lint harness — there is nothing
to aim at and the ambient module is used. primitive-load rather than `load`:
this module is DECLARATIVE, and `load` in one is a compile warning, because
adding bindings at runtime is exactly what it does."
  (catch #t
    (lambda ()
      (save-module-excursion
        (lambda ()
          (let ((gen (hl--c-generation)))
            (when gen (set-current-module gen))
            (primitive-load path)))))
    ;; re-raised so the caller's guard still handles the same condition: the
    ;; stack is a side-channel and changes no error-handling behaviour
    (lambda (key . args) (apply throw key args))
    (lambda (key . args)
      (keep-stack! (guard (e2 (#t #f)) (make-stack #t)))
      #f)))

(define (hl-load path)
  "Load the Scheme file PATH into the current config generation; #t when it
   loaded, #f when it raised.

   This is the call to use when a config has files to pull in. Guile's own
   (load PATH) is not intercepted — it evaluates into whatever module is
   current, which during a config load is the generation anyway — so this is
   the same load said out loud, with the target named rather than assumed.

   What the file defines lands in the generation and is discarded on the next
   reload, like everything else a config sets up. An error in it is reported to
   the compositor log and does NOT stop the rest of the config — but it IS a
   failure here, and this answers #f for it. That is what lets hl-load-config
   tell a file that died halfway through from one that loaded.

   PATH is used exactly as given: no search, and no \".scm\" appended.
   hl-load-config is the one that resolves a NAME under the config directory."
  (let* ((stack #f)                              ; the backtrace, once we have one
         (ok (guard (e (#t (hl--report-config e stack) #f))
               ;; A BACKTRACE IS CAPTURED BEFORE THE UNWIND, the same way
               ;; hl--load-config does it and for the same reason: after the
               ;; unwind the stack is gone, so a `guard` alone can report the
               ;; message and nothing about WHERE. This path matters as much as
               ;; that one — a file pulled in by hl-load-config arrives HERE —
               ;; and without it the banner for a nested file named nothing,
               ;; which is the exact gap the arity work was meant to close.
               (hl--load-into-generation
                 path (lambda (s) (unless stack (set! stack s)))))))
    (and ok #t)))
