;; hyprscheme/core.scm — shared Scheme plumbing: argument coercion, the plist convention, key-spec parsing,
;; the cross-reload state accessors, and the listen helpers
;;
;; imports (hyprscheme kernel) [the C++ boundary] and nothing else in the tree.
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme core)
  #:export (
           hl--bind-flags hl--bool-act hl--chord-key hl--chord-mods
           hl--modifier-token? hl--modifier-tokens
           hl--gesture-mode
           hl--mon-arg hl--monitor-listen hl--notif-act hl--plist-fold
           hl--plist-map hl--push-val hl--rule-kind
           hl--hyprland-name hl--rule-name hl--rule-spec-value
           hl--special-name hl--str
           hl--window-listen hl--window-rule-mk hl--workspace-listen
           hl--ws-arg hl-after hl-box hl-box-h hl-box-rtd hl-box-w
           hl-box-x hl-box-y hl-current-submap hl-group?
           hl-layer? hl-layout-add! hl-monitor? hl-notification?
           hl-plist-get hl-state-keys hl-state-ref hl-state-remove!
           hl-state-set! hl-window? hl-workspace? make-hl-bind
           make-hl-event make-hl-rule make-hl-timer))

(use-modules (hyprscheme kernel))

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

;; eBindFlags bits from src/keybinds/Bind.hpp
;; keyword options → eBindFlags bits, mirrored from upstream Keybinds/Bind.hpp
;; (BIND_FLAG_* = 1 << n). Derived bits are added separately in hl--bind-flags:
;; click/drag imply RELEASE, #:device-inclusive defaults from the devices list,
;; and the key name "catchall" implies CATCH_ALL (upstream LuaBindingsToplevel.cpp).

(define* (hl--bind-flags #:key (click #f) (drag #f) (devices #f)
                         (device-inclusive 'unset) (locked #f) (release #f)
                         (repeat #f) (long-press #f) (non-consuming #f)
                         (auto-consuming #f) (transparent #f)
                         (ignore-mods #f) (dont-inhibit #f)
                         (submap-universal #f) (allow-input-capture #f)
                         (mouse #f) (key ""))
  (when (and click drag)
    (error "hl-bind-add!: click and drag are exclusive"))
  (when (and (or long-press release) repeat)
    (error "hl-bind-add!: long-press / release is incompatible with repeat"))
  (when (and mouse (or repeat locked release))
    (error "hl-bind-add!: mouse is exclusive with repeat/locked/release"))
  (+ (if (or click drag) 2 0)                    ; click/drag imply release: upstream also sets BIND_FLAG_RELEASE for them
     (if (cond ((eq? device-inclusive 'unset) (pair? devices))  ; absent: inclusive when devices are listed (upstream default true)
               (device-inclusive #t)                          ; explicit #t
               (else #f))                                     ; explicit #:device-inclusive #f opts OUT of inclusivity
         8192
         0)
     (if (equal? key "catchall") 16384 0)      ; upstream: key "catchall" ⇒ BIND_FLAG_CATCH_ALL
     (+ (if locked 1 0) (if release 2 0) (if repeat 4 0) (if long-press 8 0)
        (if non-consuming 16 0) (if auto-consuming 32 0) (if transparent 64 0)
        (if ignore-mods 128 0) (if dont-inhibit 256 0) (if click 512 0)
        (if drag 1024 0) (if submap-universal 2048 0)
        (if allow-input-capture 4096 0) (if mouse 32768 0))))

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

(define (make-hl-bind tokens thunk) (make-struct/no-tail hl-bind-rtd tokens thunk))

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
;; ONCE by the host (Host.cpp), with the finalizer gsubr itself as the thunk:
;; this file is rebuilt on every reload, so a hook registered here would stack
;; a fresh closure per reload.

;; hl--ready is the KERNEL's flag and the host sets it once every module has
;; loaded — a set! of an imported binding is a syntax error in a declarative
;; module, and the host is the one that actually knows loading finished.

