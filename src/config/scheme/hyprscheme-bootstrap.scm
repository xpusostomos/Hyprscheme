;; hl-bind-add!: record first (locked + tagged C++-side), then the tokens
;; the bind is built from, flags, description, devices

(define (hl-layout-add! name . spec) "Register the custom layout NAME.
SPEC is either a single recalculate procedure or a plist of named
callbacks: 'recalculate fn, 'resize fn, 'window-open fn,
'window-close fn, 'layout-msg fn. The recalculate fn receives
(count W H windows) and returns a list of (x y w h) boxes; resize
additionally gets (dx dy corner) and falls back to the recalculate fn
when absent. Keep layout state in closures around the callbacks.
Layouts register in the compositor's global registry (hence -add!),
and every callback must be NAMED — there is no shorthand that guesses
a bare lambda's role."
  (when (or (null? spec) (and (pair? spec) (procedure? (car spec))))
    (hl--error 'hl-layout-add!
            "the callbacks must be named, e.g. (hl-layout-add! ~s
             'recalculate LAMBDA ...)"
            name))
  (if (= 0 (hl--c-layout-add name spec))
      name
      (hl--error 'hl-layout-add! "layout ~a rejected, see compositor log" name)))


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
(define (hl--bool-act on?)
  (cond ((eq? on? 'unset) 0)
        (on? 1)
        (else 2)))

(define (hl--dir d)
  (if (symbol? d) (symbol->string d) d))

;; names/tags/props accept symbols or strings (numbers become strings)
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

(define (hl--plist-cdr l)
  ;; (key value rest ...) → (value rest ...); a trailing bare KEY is an error
  (let ((tail (cdr l)))
    (if (null? tail)
        (hl--error 'hl--plist "odd plist: ~s" l)
        tail)))

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

(define (hl--plist-get pl key default)
  (let loop ((l pl))
    (if (null? l)
        default
        (let ((tail (hl--plist-cdr l)))
          (if (eq? (car l) key)
              (car tail)
              (loop (cdr tail)))))))

(define (hl--plist-has? pl key)
  (let loop ((l pl))
    (if (null? l)
        #f
        (let ((tail (hl--plist-cdr l)))
          (if (eq? (car l) key) #t (loop (cdr tail)))))))

(define (hl--plist-fold f seed pl)
  (let loop ((l pl) (acc seed))
    (if (null? l)
        acc
        (let ((tail (hl--plist-cdr l)))
          (loop (cdr tail) (f (car l) (car tail) acc))))))

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
    (hl--error 'hl-bind-add! "click and drag are exclusive"))
  (when (and (or long-press release) repeat)
    (hl--error 'hl-bind-add! "long-press / release is incompatible with repeat"))
  (when (and mouse (or repeat locked release))
    (hl--error 'hl-bind-add! "mouse is exclusive with repeat/locked/release"))
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

(define hl--emacs-mods
  '(("C" . "CTRL") ("M" . "ALT") ("S" . "SHIFT")
    ("s" . "SUPER") ("A" . "ALT") ("H" . "MOD3")))

(define hl--emacs-keys
  '(("RET" . "Return") ("SPC" . "space") ("ESC" . "Escape")
    ("TAB" . "Tab") ("DEL" . "Delete") ("LFD" . "Return")))

;; emacs mouse events → hyprland mouse:CODE
;; emacs mouse-1=left, mouse-2=middle, mouse-3=right
;; hyprland: 272=left, 273=right, 274=middle
(define hl--emacs-mouse
  '(("<down-mouse-1>" . "mouse:272") ("<mouse-1>" . "mouse:272")
    ("<down-mouse-2>" . "mouse:274") ("<mouse-2>" . "mouse:274")
    ("<down-mouse-3>" . "mouse:273") ("<mouse-3>" . "mouse:273")
    ("<mouse-4>" . "mouse:275") ("<mouse-5>" . "mouse:276")))

(define (hl--emacs-key name)
  ;; translate an emacs key name to the xkb/hyprland name
  (let ((mapped (or (assoc name hl--emacs-keys) (assoc name hl--emacs-mouse))))
    (if mapped
        (cdr mapped)
        ;; bracketed keyboard keysym: <f1> → F1, <left> → Left, <kp-1> → KP_1
        (if (and (> (string-length name) 2)
                 (char=? (string-ref name 0) #\<)
                 (char=? (string-ref name (- (string-length name) 1)) #\>))
            (let* ((inner (substring name 1 (- (string-length name) 1)))
                   (n (string-length inner)))
              (let build ((i 0) (acc '()))
                (if (= i n)
                    (list->string (reverse acc))
                    (let ((c (string-ref inner i)))
                      (build (+ i 1)
                             (cons (if (char=? c #\-) #\_ c) acc))))))
            name))))

(define (hl-kbd spec) "Parse an emacs-style key specification string
into a LIST of key tokens: \"C-M-a\" → (\"CTRL\" \"ALT\" \"a\"),
\"<f1>\" → (\"F1\"). The LAST token is the key, even when it spells
like a modifier letter (\"s-M\" → (\"SUPER\" \"M\") — Super+M the
key, not Super+Alt). A TRAILING DASH makes it a pure modifier list:
\"C-M-\" → (\"CTRL\" \"ALT\")."
  (let loop ((str spec) (acc '()))
    (let ((dash (string-index str #\-)))
      (if (and dash (> dash 0))
          (let ((prefix (substring str 0 dash)))
            (if (assoc prefix hl--emacs-mods)
                ;; a spec ending in the dash ("C-M-") is a pure modifier list
                (let ((rest (substring str (+ dash 1) (string-length str))))
                  (if (= 0 (string-length rest))
                      (reverse (cons (cdr (assoc prefix hl--emacs-mods)) acc))
                      (loop rest (cons (cdr (assoc prefix hl--emacs-mods)) acc))))
                (reverse (cons (hl--emacs-key str) acc))))
          (reverse (cons (hl--emacs-key str) acc))))))

(define (hl-key spec) "Parse a hyprland-style key specification string
into a LIST of key tokens: \"SUPER+SHIFT+Q\" →
(\"SUPER\" \"SHIFT\" \"Q\")."
  (map string-trim-both (string-split spec #\+)))

(define (hl-after ms thunk) "Run THUNK once after MS milliseconds.
Returns an hl-timer handle for later control
(hl-timer-enabled-set!, hl-timer-set-timeout, hl-timer-cancel!); a
one-shot releases its own record lock when it completes."
  (let ((rec (make-hl-timer ms thunk)))
    (if (= 0 (hl--c-timer rec (inexact->exact ms) 0))
        rec
        (hl--error 'hl-after "timer ~ams rejected, see compositor log" ms))))

(define (hl-repeat ms thunk) "Run THUNK every MS milliseconds until
cancelled with (hl-timer-cancel!) or the config reloads. Returns an
hl-timer handle like hl-after's."
  (let ((rec (make-hl-timer ms thunk)))
    (if (= 0 (hl--c-timer rec (inexact->exact ms) 1))
        rec
        (hl--error 'hl-repeat "timer ~ams rejected, see compositor log" ms))))

(define (hl-active-title ) "Title of the focused window, or #f when no
window has focus."
  (hl--c-active-title))

(define (hl-workspaces )
  "All workspaces, as a list of workspace handles."
  (or (hl--c-workspace-names) '()))



;; handles are opaque records whose single field is a guardian CELL
;; ((address . 0) — see the mint helpers and the C++ IHandle comment): the
;; address is a heap weak ref C++-side, and the record's death deletes it
;; through the guardian. Stale handles simply report #f from every getter;
;; the address is the only thing crossing FFI.
;;
;; Each one is built on Guile's own record primitives, with a TYPE-CHECKING
;; accessor: these records are handed to user code, so a handle of the wrong
;; family — or a forged value — must be a clean error rather than a
;; reinterpreted pointer.
(define (hl--record-field who rtd idx x)
  (if (and (struct? x) (eq? (struct-vtable x) rtd))
      (struct-ref x idx)
      (hl--error who "not a ~a: ~s" (record-type-name rtd) x)))

;; ---- handles ----------------------------------------------------------------
;; A window/workspace/monitor/group/layer/notification handle is a Guile
;; FOREIGN OBJECT built by the C++ side (see Handles.hpp): the object IS the
;; handle, its family is its type, and its lifetime is a finalizer. So nothing
;; here mints, wraps or drains handles — the predicates below are the whole of
;; the handle machinery in Scheme.
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
(define hl-event-rtd (make-record-type 'hl-event '(type thunk)))
(define (hl-event? x) (and (struct? x) (eq? (struct-vtable x) hl-event-rtd)))
(define (make-hl-event type thunk) (make-struct/no-tail hl-event-rtd type thunk))
(define (hl--event-type b) (hl--record-field 'hl--event-type hl-event-rtd 0 b))
(define (hl--event-thunk b) (hl--record-field 'hl--event-thunk hl-event-rtd 1 b))

;; timers: the record IS the handle — the initial interval and the thunk.
;; C++ locks it inside the timer's fire callback; a one-shot timer releases
;; its own lock when it completes, and reload tears the rest down.
(define hl-timer-rtd (make-record-type 'hl-timer '(interval thunk)))
(define (hl-timer? x) (and (struct? x) (eq? (struct-vtable x) hl-timer-rtd)))
(define (make-hl-timer interval thunk) (make-struct/no-tail hl-timer-rtd interval thunk))
(define (hl--timer-interval t) (hl--record-field 'hl--timer-interval hl-timer-rtd 0 t))
(define (hl--timer-thunk t) (hl--record-field 'hl--timer-thunk hl-timer-rtd 1 t))

;; binds: the record IS the handle — the token LIST (the bind's meaning)
;; and the thunk. C++ locks the record while the bind is registered (see
;; SThunkRef) and stamps its pinned address into the bind's argument tag;
;; the record becomes collectable when the bind is unbound (or dies at
;; reload) and the user drops it. No bind ids, no registries.
(define hl-bind-rtd (make-record-type 'hl-bind '(tokens thunk)))
(define (hl-bind? x) (and (struct? x) (eq? (struct-vtable x) hl-bind-rtd)))
(define (make-hl-bind tokens thunk) (make-struct/no-tail hl-bind-rtd tokens thunk))
(define (hl-bind-tokens b) (hl--record-field 'hl-bind-tokens hl-bind-rtd 0 b))
(define (hl-bind-thunk b) (hl--record-field 'hl-bind-thunk hl-bind-rtd 1 b))



(define (hl-active-window )
  "The focused window, or #f when no window has focus."
  (hl--c-active-window))

(define (hl-windows )
  "All windows, as a list of window handles."
  (or (hl--c-window-ids) '()))

(define (hl-window-title w)
  "The window's title."
  (hl--c-window-title w))

(define (hl-window-alive? w)
  "#t when the window still exists."
  (= 1 (hl--c-window-alive w)))

(define (hl-window-close! w)
  "Close the window."
  (= 0 (hl--c-window-close w)))

(define (hl-window-class w)
  "The window's class."
  (hl--c-window-class w))

(define (hl-window-workspace w)
  "The workspace the window is on, as a handle, or #f."
  (hl--c-window-workspace-id w))

(define (hl-window-monitor w)
  "The monitor the window is on, as a handle, or #f."
  (hl--c-window-monitor-id w))

(define (hl-window-floating? w)
  "#t when the window is floating."
  (= 1 (hl--c-window-floating w)))

(define (hl-window-size w)
  "The window's size, as (width . height), or #f when stale."
  (hl--c-window-size w))   ; (width . height), or #f

(define (hl-window-pid w)
  "The window's owning process id."
  (hl--c-window-pid w))

(define (hl-window-focus! w)
  "Focus the window."
  (= 0 (hl--c-window-focus w)))

(define* (hl-window-float-set! w #:key (on? 'unset))
  "Float or unfloat WINDOW: absent #:on? toggles, #t floats, #f tiles."
  (= 0 (hl--c-window-float-act w (hl--bool-act on?))))

(define (hl-window-workspace-set! w ws)
  "Move the window to workspace WS."
  (= 0 (hl--c-window-move-to-workspace w (hl--ws-arg ws))))

(define (hl-window-monitor-set! w mon)
  "Move the window to another monitor MON. There is no direct
   window-to-monitor dispatch, so this moves to the target's active
   workspace."
  (hl-window-workspace-set! w (hl-monitor-active-workspace mon)))

;; ---- actions: navigation and geometry --------------------------------------
;; directions: "l"/"r"/"u"/"d" or 'left/'right/'up/'down. Window args accept
;; a handle or #f (= active window). Action results: #t on success, #f on
;; rejection (message in the compositor log).

(define (hl-workspace-focus! ws)
  "Focus the workspace WS."
  (= 0 (hl--c-focus-workspace (hl--ws-arg ws))))

(define (hl-focus-direction-set! dir)
  "Move the focus in direction DIR (left/right/up/down)."
  (= 0 (hl--c-focus-direction (hl--dir dir))))

(define (hl-monitor-focus! mon)
  "Move the focus to monitor MON."
  (= 0 (hl--c-focus-monitor (hl--mon-arg mon))))

(define (hl-focus-last! )
  "Move the focus to the previously-focused window."
  (= 0 (hl--c-focus-last)))

(define (hl-focus-urgent! )
  "Move the focus to an urgent window, or the last one."
  (= 0 (hl--c-focus-urgent)))

(define (hl-window-move-direction! w dir)
  "Move WINDOW in direction DIR."
  (= 0 (hl--c-window-move-direction w (hl--dir dir))))

(define (hl-window-swap-direction! w dir)
  "Swap WINDOW with the window in direction DIR."
  (= 0 (hl--c-window-swap-direction w (hl--dir dir))))

(define (hl-window-swap-next! w . opt)
  "Swap WINDOW with the next window in its group; 'prev or #t swaps
   backwards."
  (= 0 (hl--c-window-swap-next w (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 (if (car opt) 1 0))))))

(define (hl-window-swap-with! w other)
  "Swap WINDOW with OTHER."
  (= 0 (hl--c-window-swap-with w other)))

(define (hl-window-cycle! . opt)
  "Cycle focus to the next window. Options: 'prev (backwards), 'tiled,
   'floating (combinable symbols)."
  (let loop ((rest opt) (next 1) (filter 0))
    (cond ((null? rest)
           (= 0 (hl--c-window-cycle #f next filter)))
          ((eq? (car rest) 'prev) (loop (cdr rest) 0 filter))
          ((eq? (car rest) 'tiled) (loop (cdr rest) next 1))
          ((eq? (car rest) 'floating) (loop (cdr rest) next 2))
          (else (loop (cdr rest) next filter)))))

(define (hl-window-center! w)
  "Center WINDOW."
  (= 0 (hl--c-window-center w)))

(define (hl-window-size-set! w width height . opt)
  "Resize WINDOW to WIDTH x HEIGHT pixels; 'relative (or 'rel) makes
   them deltas."
  (= 0 (hl--c-window-resize-px w (exact->inexact width) (exact->inexact height)
         (if (null? opt) 0 (if (memq (car opt) '(relative rel)) 1 0)))))

(define (hl-window-position-set! w x y . opt)
  "Move WINDOW to (X, Y); 'relative (or 'rel) makes the coordinates
   deltas."
  (= 0 (hl--c-window-move-px w (exact->inexact x) (exact->inexact y)
         (if (null? opt) 0 (if (memq (car opt) '(relative rel)) 1 0)))))

(define* (hl-window-pinned-set! w #:key (on? 'unset))
  "Pin or unpin WINDOW: absent #:on? toggles, #t/#f set."
  (= 0 (hl--c-window-pin-act w (hl--bool-act on?))))

(define* (hl-window-pseudo-set! w #:key (on? 'unset))
  "Toggle or set pseudo-tile on WINDOW (absent #:on? toggles)."
  (= 0 (hl--c-window-pseudo w (hl--bool-act on?))))

(define (hl-window-kill! w)
  "Forcefully kill WINDOW."
  (= 0 (hl--c-window-kill w)))

(define (hl-window-signal! w sig)
  "Send WINDOW's process the signal SIG."
  (= 0 (hl--c-window-signal w sig)))

(define (hl-window-zorder-set! w mode)
  "Restack WINDOW per MODE: \"up\", \"down\", \"top\" or \"bottom\"."
  (= 0 (hl--c-window-zorder w (hl--str mode))))

(define (hl-window-prop-set! w prop val)
  "Set a dynamic window prop on WINDOW (e.g. 'opacity \"0.8\"). Read
   back with hl-window-prop."
  (= 0 (hl--c-window-set-prop w (hl--str prop) (hl--str val))))

(define (hl-window-tag-add! w tag)
  "Add the static TAG string to WINDOW's tags."
  (= 0 (hl--c-window-tag w (hl--str tag))))

(define (hl-window-tags-clear! w)
  "Clear WINDOW's tags."
  (= 0 (hl--c-window-clear-tags w)))

(define (hl-window-swallow-toggle! )
  "Toggle swallowing for the active window."
  (= 0 (hl--c-toggle-swallow)))

;; ---- actions: groups --------------------------------------------------------

(define* (hl-window-group-set! w #:key (on? 'unset))
  "Put WINDOW into a group or take it out: absent #:on? uses the dedicated
   membership toggle, #t/#f set explicitly. W may be #f for the active
   window. See hl-window-group?."
  (if (eq? on? 'unset)
      (= 0 (hl--c-group-toggle w))
      (= 0 (hl--c-group-set w (if on? 1 0)))))

(define (hl-window-group? w)
  "#t when WINDOW is in a group."
  (= 1 (hl--c-window-in-group w)))

(define (hl-group-cycle! w . opt)
  "Switch to the next window in WINDOW's group; 'prev goes backwards."
  (= 0 (hl--c-group-cycle w (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

(define (hl-group-window-active! w index)
  "Switch to the member at 1-based INDEX in WINDOW's group."
  (= 0 (hl--c-group-index w index)))

(define (hl-group-window-move-next! w . opt)
  "Move WINDOW within its group; 'prev moves backwards."
  (= 0 (hl--c-group-move-window w (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

(define* (hl-groups-lock-set! #:key (on? 'unset))
  "Lock or unlock ALL groups compositor-wide (no window involved). See
   hl-groups-locked?."
  (= 0 (hl--c-group-lock (hl--bool-act on?))))

(define (hl-groups-locked? )
  "#t when the compositor-wide group lock is active."
  (= 1 (hl--c-groups-locked)))

(define* (hl-window-group-lock-set! w #:key (on? 'unset))
  "Lock or unlock the group of window W (#f = the active window): absent
   #:on? toggles, #t/#f set. See hl-window-group-lock?."
  (= 0 (hl--c-window-group-lock w (hl--bool-act on?))))

(define (hl-window-group-lock? w)
  "#t when WINDOW's group is locked."
  (= 1 (hl--c-window-group-locked w)))

(define (hl-window-group-move-in! w dir)
  "Move WINDOW into the group in direction DIR."
  (= 0 (hl--c-window-into-group w (hl--dir dir))))

(define (hl-window-group-move-out! w dir)
  "Move WINDOW out of its group in direction DIR."
  (= 0 (hl--c-window-out-of-group w (hl--dir dir))))

(define (hl-window-group-move-in-or-create! w dir)
  "Move WINDOW into the group in direction DIR, creating one when there
   is none."
  (= 0 (hl--c-window-into-or-create-group w (hl--dir dir))))

(define* (hl-window-deny-from-group-set! w #:key (on? 'unset))
  "Set whether WINDOW denies being grouped (absent #:on? toggles)."
  (= 0 (hl--c-window-deny-from-group w (hl--bool-act on?))))

;; ---- actions: workspaces and monitors ---------------------------------------

(define (hl-workspace-name-set! old new)
  "Rename the workspace OLD to NEW."
  (= 0 (hl--c-workspace-rename (hl--ws-arg old) (hl--str new))))

(define (hl-workspace-monitor-set! ws mon)
  "Move the workspace WS to monitor MON."
  (= 0 (hl--c-workspace-move-monitor (hl--ws-arg ws) (hl--mon-arg mon))))

(define (hl--special-name s)
  ;; normalize a special-workspace name/selector to its bare name, so an
  ;; open/close comparison survives a leading "special:" on either side
  (if (and (> (string-length s) 8) (string=? (substring s 0 8) "special:"))
      (substring s 8 (string-length s))
      s))

(define* (hl-workspace-special-set! ws #:key (on? 'unset))
  "Open, close or toggle the special workspace WS. Absent #:on? toggles
   the named special workspace on the FOCUSED monitor (created on
   demand); #t/#f force open/close through the same toggle when the
   focused monitor's active special workspace already equals/differs
   from the target."
  (if (eq? on? 'unset)
      (= 0 (hl--c-workspace-toggle-special (hl--ws-arg ws)))
      (let* ((mon (hl-active-monitor))
             (active (and mon (hl-monitor-active-special-workspace mon)))
             (cur (and active (hl--special-name (hl-workspace-name active))))
             (target (hl--special-name (hl--ws-arg ws))))
        (if (eqv? on? (and cur (string=? cur target)))
            0
            (= 0 (hl--c-workspace-toggle-special (hl--ws-arg ws)))))))

(define (hl-monitor-workspace-special-set! mon ws)
  "Open the special workspace WS on MON (created when missing); #f
   closes whatever special workspace is open there. WS must be named - a
   closed monitor keeps no record of its last special workspace."
  (= 0 (hl--c-monitor-set-special (hl--mon-arg mon) (if ws (hl--ws-arg ws) ""))))

(define (hl-monitor-swap! mon1 mon2)
  "Swap the current workspaces of monitors MON1 and MON2."
  (= 0 (hl--c-workspace-swap-monitors (hl--mon-arg mon1) (hl--mon-arg mon2))))

(define (hl-workspace-id-set! ws new-id)
  "Change the numbered workspace WS's ID to NEW-ID. Only numbered
   workspaces can be re-IDed (named/special cannot); the new ID must be
   > 0 and not in use."
  (= 0 (hl--c-workspace-change-id (hl--ws-arg ws) (exact->inexact new-id))))

;; ---- actions: cursor and misc -----------------------------------------------

(define (hl-cursor-move! x y)
  "Move the cursor to (X, Y)."
  (= 0 (hl--c-cursor-move (exact->inexact x) (exact->inexact y))))

(define (hl-cursor-move-to-corner! w corner)
  "Move the cursor to CORNER (0-3) of WINDOW."
  (= 0 (hl--c-cursor-corner w corner)))

(define (hl-exit! )
  "Quit the compositor."
  (= 0 (hl--c-exit)))

(define (hl-config-reload! )
  "Reload the configuration, like hyprctl reload."
  (= 0 (hl--c-reload-config)))

(define (hl-force-renderer-reload! )
  "Force the renderer to reload on all monitors."
  (= 0 (hl--c-force-renderer-reload)))

(define* (hl-monitor-power-set! mon #:key (on? 'unset))
  "Switch MON's power (a monitor, or #f for all monitors): absent #:on?
   toggles, #t on, #f off."
  (= 0 (hl--c-dpms (hl--bool-act on?) (if mon (hl--mon-arg mon) ""))))

(define (hl-force-idle! seconds)
  "Pretend SECONDS of idle time have elapsed for all idle timers."
  (= 0 (hl--c-force-idle (exact->inexact seconds))))

(define (hl-global! action)
  "Activate the global shortcut named ACTION (D-Bus GlobalShortcuts; see
   bind-globals)."
  (= 0 (hl--c-global (hl--str action))))

(define (hl-event! data)
  "Send DATA as an event on socket2, visible to other IPC clients."
  (= 0 (hl--c-event (hl--str data))))

(define (hl-window-pass-shortcut! w)
  "Pass the currently-pressed keybind through to WINDOW."
  (= 0 (hl--c-pass w)))

(define (hl-window-send-shortcut! mods key . w)
  "Send a keypress to WINDOW (default: the active window) as if pressed:
   MODS is a list of modifier tokens, as built by hl-kbd/hl-key (e.g.
   (hl-key \"SUPER\")), and KEY an xkb keysym name."
  (unless (and (list? mods) (every (lambda (m) (string? m)) mods))
    (hl--error 'hl-window-send-shortcut! "'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\")"))
  (= 0 (hl--c-send-shortcut mods key (if (null? w) -1 (car w)))))

(define (hl-window-send-key-state! mods key state . w)
  "Like hl-window-send-shortcut!, but STATE selects the key state
   explicitly (press or release)."
  (unless (and (list? mods) (every (lambda (m) (string? m)) mods))
    (hl--error 'hl-window-send-key-state! "'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\")"))
  (= 0 (hl--c-send-key-state mods key state (if (null? w) -1 (car w)))))

(define (hl-mouse-action! action)
  "Begin an interactive mouse action for mouse binds: (hl-mouse-action!
   \"drag\") or (hl-mouse-action! \"resize\")."
  (= 0 (hl--c-mouse (hl--str action))))

(define (hl-clear-crashed-lockscreen! )
  "Manual escape hatch when a lock screen has crashed: clears the
   session lock so the session is usable again. Refused while a lock
   client is attached or the session isn't locked - it can never unlock
   a live lock screen."
  (let ((rc (hl--c-clear-crashed-lockscreen)))
    (if (= 0 rc) #t (hl--error 'hl-clear-crashed-lockscreen! "~a" (hl--c-config-last-error)))))

(define (hl-exec-scheduled-prop-refresh-immediately )
  "Run the config-time deferred refresh pass NOW (rule/prop/layout
   changes schedule it). #t when it executed as scheduled, #f otherwise."
  (= 0 (hl--c-scheduled-prop-refresh-immediately)))

(define (hl-release-input-capture! )
  "Release any active input capture session."
  (= 0 (hl--c-release-input-capture)))

(define (hl-layout-msg msg)
  "Send MSG to the active workspace's layout (see custom layouts in the
   wiki)."
  (= 0 (hl--c-layout-message (hl--str msg))))

;; ---- config -----------------------------------------------------------------
(define (hl-config-add! key val)
  "Set the config option KEY to VAL. Keys accept \"general:gaps_in\"or
   \"general.gaps_in\". Values: numbers, strings, booleans, lists
   (vec2-style arrays) or a plist for tables (e.g. '(top 10 bottom 10)).
   Writes propagate like a runtime config change - the affected
   subsystems refresh immediately."
  (hl--c-config-begin)
  (hl--push-val val)
  (if (= 0 (hl--c-config-set (hl--str key)))
      #t
      (hl--error 'hl-config-add! "~a" (hl--c-config-last-error))))

(define (hl--push-val v)
  (cond ((number? v)
         ;; exact integers must reach INT options as integers (upstream
         ;; rejects doubles for them); FLOAT options accept integers too
         (if (exact? v)
             (hl--c-config-push-int (exact->inexact v))
             (hl--c-config-push-num v)))
        ((boolean? v) (hl--c-config-push-bool (if v 1 0)))
        ((string? v) (hl--c-config-push-str v))
        ((and (pair? v) (symbol? (car v)))
         ;; plist → hash table (the API's nested named-fields form)
         (hl--c-config-tbl-open 1)
         (hl--plist-fold (lambda (k val _)
                           (hl--c-config-tbl-key (hl--str k))
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
        (else (hl--error 'hl-config-add! "unsupported value ~s" v))))

(define (hl-config-get key)
  "Return the current value of the config option KEY, as Scheme data
   (number, string, boolean, or a plist for tables)."
  (hl--c-config-get (hl--str key)))   ; #t/#f, number, string, or a plist for tables


(define* (hl-device-add! name #:key #:allow-other-keys #:rest fields)
  "Configure a device: NAME plus keyword fields, e.g.
   (hl-device-add! \"tablet\" #:repeat_rate 25 #:natural_scroll #t).
   Write-only (no device read side, no per-field unset); validated
   against the field table and applied to the live device. Unknown
   keywords are rejected by the field table."
  (let ((plist (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)))
    (if (= 0 (hl--c-device-add (hl--str name) plist))
        #t
        (hl--error 'hl-device-add! "~a" (hl--c-config-last-error)))))


(define* (hl-exec! cmd #:key #:allow-other-keys #:rest effects)
  "The one exec. Spawns CMD asynchronously through the compositor's
executor (shell, env injection; never blocks the config). Keyword
EFFECTS build a one-shot window rule pinned to the spawned window by
pid - e.g. (hl-exec! \"foot\" #:float #t #:workspace \"games\"); the
keys and value forms are the window-rule effects (see window-rules),
with the same coercion as hl-window-rule-add!. With no effects the
command may still carry the legacy inline rule prefix
(\"[float size 800 500] mygame\") - the C++ layer parses it on the
plain path. Returns the new pid."
  (define (flat l)
    (cond ((null? l) '())
          ((null? (cdr l)) (hl--error 'hl-exec! "odd plist of rule effects"))
          (else (cons* (hl--str (car l))
                       (hl--rule-spec-value (cadr l))
                       (flat (cddr l))))))
  (let ((pid (hl--c-exec! (hl--str cmd)
                         (flat (map (lambda (x)
                                      (if (keyword? x) (keyword->symbol x) x))
                                    effects)))))
    (if (> pid 0)
        pid
        (hl--error 'hl-exec! "~a" (hl--c-config-last-error)))))

;; ---- rules -------------------------------------------------------------------
;; window/layer rules: a plist with 'match (a plist of property → value),
;; optional 'name and 'enabled, and every other key being an EFFECT (its
;; config string form). Returns a rule handle for hl-rule-set-enabled /
;; hl-rule-enabled?. Anonymous rules (name #f) are re-created on each
;; config reload; named rules are reused across calls.
;;   (hl-window-rule-add! "term" '((match . ((class . "foot"))) (float . #t)
;;                            (opacity . "0.8") (workspace . "3")))
;;   (hl-window-rule-add! #f '((match . ((class . "(?i)games"))) (monitor . "DP-1")))
;; match properties: class title initial_class initial_title floating tag
;;   xwayland fullscreen pinned focus group modal on_workspace content
;;   namespace exec_token exec_pid ...
;; effects: float tile fullscreen maximize fullscreen_state move size center
;;   pseudo monitor workspace no_initial_focus pin group suppress_event
;;   content no_close_for scrolling_width rounding opacity border_color
;;   idle_inhibit animation tag min_size max_size ... (the window-rule page
;;   has the full list with value forms)
;; rules: the record IS the handle - the rule KIND ('window or 'layer) and
;; the NAME (or "" for anonymous). The index C++-side locks the record while
;; the rule is registered (rules live for their config generation); a stale
;; record from a dead generation just fails to resolve.
(define hl-rule-rtd (make-record-type 'hl-rule '(kind name)))
(define (hl-rule? x) (and (struct? x) (eq? (struct-vtable x) hl-rule-rtd)))
(define (make-hl-rule kind name) (make-struct/no-tail hl-rule-rtd kind name))
(define (hl--rule-kind r) (hl--record-field 'hl--rule-kind hl-rule-rtd 0 r))
(define (hl--rule-name r) (hl--record-field 'hl--rule-name hl-rule-rtd 1 r))

(define (hl--rule-spec-value v)
  (cond ((string? v) v)
        ((boolean? v) (if v "true" "false"))
        ((number? v) (number->string v))
        (else #f)))

(define (hl--window-rule-mk name spec begin-fn effect-fn commit-fn what kind)
  (let ((enabled (hl--plist-get spec 'enabled #t)))
    (if (not (= 0 (begin-fn (if name (hl--str name) "") (if enabled 1 0))))
        (hl--error what "~a" (hl--c-config-last-error))
        (let loop ((rest spec))
          (cond ((null? rest)
                 ;; done: make the handle record, commit, index it C++-side
                 (let ((rec (make-hl-rule kind (or name ""))))
                   (if (not (= 0 (commit-fn rec)))
                       (hl--error what "~a" (hl--c-config-last-error))
                       rec)))
                (else
                 (let* ((tail (hl--plist-cdr rest))
                        (k  (hl--str (car rest)))
                        (v  (car tail)))
                   (cond ((equal? k "match")
                          (let mloop ((m v))
                            (cond ((null? m) (loop (cdr tail)))
                                  (else
                                   (let* ((mtail (hl--plist-cdr m))
                                          (mk (hl--str (car m)))
                                          (sv (hl--rule-spec-value (car mtail))))
                                     (if (not sv)
                                         (hl--error what "bad match value for ~a" mk)
                                         (if (= 0 (hl--c-rule-match mk sv))
                                             (mloop (cdr mtail))
                                             (hl--error what "~a" (hl--c-config-last-error)))))))))
                         ((equal? k "enabled")
                          (loop (cdr tail)))
                         (else
                          (let ((sv (hl--rule-spec-value v)))
                            (if (not sv)
                                (hl--error what "bad effect value for ~a" k)
                                (if (= 0 (effect-fn k sv))
                                    (loop (cdr tail))
                                    (hl--error what "~a" (hl--c-config-last-error))))))))))))))

(define* (hl-window-rule-add! name #:key #:allow-other-keys #:rest fields)
  "Add a window rule and return its rule handle. NAME (#f = anonymous).
   #:match takes a plist of match properties to values: ((class .
   \"foot\") (title . \"foo\") (floating . #t) ...). #:enabled toggles
   the rule (a rule is enabled unless you pass #:enabled #f). Every
   other keyword is an effect: #:float #t, #:monitor \"DP-1\",
   #:workspace \"3\", #:opacity \"0.8\", ... The window-rule page has
   the full lists. Anonymous rules are re-created on each config
   reload; named rules are reused."
  (hl--window-rule-mk name
    (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)
    hl--c-window-rule-begin hl--c-window-rule-effect hl--c-window-rule-commit
    'hl-window-rule-add! 'window))

(define* (hl-layer-rule-add! name #:key #:allow-other-keys #:rest fields)
  "Add a layer rule: the same keyword-rule-spec shape as
   hl-window-rule-add!, matching on the namespace property inside
   #:match."
  (hl--window-rule-mk name
    (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)
    hl--c-layer-rule-begin hl--c-layer-rule-effect hl--c-layer-rule-commit
    'hl-layer-rule-add! 'layer))

(define* (hl-rule-enabled-set! rule #:key (on? 'unset))
  "Enable, disable or toggle RULE: absent #:on? toggles, #t/#f set."
  (if (= 0 (hl--c-rule-set-enabled rule
                            (if (if (eq? on? 'unset) (not (hl-rule-enabled? rule)) on?) 1 0)))
      #t
      (hl--error 'hl-rule-enabled-set! "unknown rule")))

(define (hl-rule-enabled? rule)
  "#t when RULE is enabled."
  (= 1 (hl--c-rule-enabled rule)))

;; ---- groups as objects (upstream HL.Group parity) ----------------------------
;; a group is the tabbed-window arrangement on a workspace; groups dissolve
;; behind our backs, so handles follow the weak-handle model (stale -> #f).
;; passives only: state reads + two mutators; NO callbacks (upstream parity).
(define (hl-group? x)
  "#t when X is a group handle."
  (hl--c-group? x))

(define (hl-workspace-groups WS)
  "The groups on workspace WS, as a list of group handles."
  (let ((l (hl--c-workspace-groups WS)))
    (if l l '())))

(define (hl-group-members g)
  "The group's windows, as a list of window handles."
  (let ((l (hl--c-group-members g)))
    (if l l '())))

(define (hl-group-size g)
  "How many windows the group has."
  (hl--c-group-size g))

(define (hl-group-current g)
  "The group's currently-focused member, as a window handle, or #f."
  (hl--c-group-current g))

(define (hl-group-current-index g)
  "The 1-based position of the group's focused member."
  (hl--c-group-current-idx g))

(define (hl-group-locked? g)
  "#t when the group is locked."
  (eq? (hl--c-group-locked g) #t))

(define (hl-group-denied? g)
  "#t when the group denies new members."
  (eq? (hl--c-group-denied g) #t))

(define (hl-group-alive? g)
  "#t when the group still exists (groups dissolve behind your back;
   stale handles report #f)."
  ;; the C entry returns an int (1 alive / 0 gone / -1 before g_up), not a
  ;; scheme boolean — comparing it with eq? #t made this always #f
  (= 1 (hl--c-group-alive g)))

(define (hl-group=? a b)
  "#t when A and B refer to the same underlying group."
  (= 1 (hl--c-group-same a b)))

(define (hl-group-add! g window . index)
  "Add WINDOW to the group G, optionally at a 1-based INDEX. Errors when
   the group denies new members or the window cannot be grouped into it."
  (if (= 0 (hl--c-group-add g window
                           (if (null? index) -1 (inexact->exact (car index)))))
      #t
      (hl--error 'hl-group-add! "~a" (hl--c-config-last-error))))

(define (hl-group-remove! g window)
  "Remove WINDOW from the group G; errors when it is not a member."
  (if (= 0 (hl--c-group-remove g window))
      #t
      (hl--error 'hl-group-remove! "~a" (hl--c-config-last-error))))

(define* (hl-workspace-rule-add! ws #:key #:allow-other-keys #:rest fields)
  "Add a workspace rule for WS (a selector or handle):
   (hl-workspace-rule-add! \"3\" #:monitor \"DP-1\" #:layout \"master\").
   #:enabled toggles the rule (a rule is enabled unless you pass
   #:enabled #f). Fields as keywords: monitor default persistent
   gaps_in gaps_out float_gaps border_size no_border no_rounding
   decorate no_shadow on_created_empty default_name layout animation
   layout_opts."
  (let ((spec (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)))
    (if (not (= 0 (hl--c-workspace-rule-begin (hl--ws-arg ws)
                (if (hl--plist-get spec 'enabled #t) 1 0))))
        (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))
        (let loop ((rest spec))
          (cond ((null? rest)
                 (if (= 0 (hl--c-workspace-rule-commit))
                     #t
                     (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                (else
                 (let* ((tail (hl--plist-cdr rest))
                        (k  (hl--str (car rest)))
                        (v  (car tail)))
                   (cond ((member k (list "workspace" "enabled"))
                          (loop (cdr tail)))
                         ((equal? k "layout_opts")
                          (let oloop ((o v))
                            (cond ((null? o) (loop (cdr tail)))
                                  (else
                                   (let* ((otail (hl--plist-cdr o))
                                          (sv (hl--rule-spec-value (car otail))))
                                     (if (not sv)
                                         (hl--error 'hl-workspace-rule-add! "bad layout_opts value")
                                         (if (= 0 (hl--c-workspace-rule-layout-opt (hl--str (car o)) sv))
                                             (oloop (cdr otail))
                                             (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error)))))))))
                         ((member k (list "gaps_in" "gaps_out" "float_gaps"))
                          ;; css-gap fields: scalars and per-side plists both
                          ;; go through the gap parser, whatever their type
                          (hl--c-config-begin)
                          (hl--push-val v)
                          (if (= 0 (hl--c-workspace-rule-gap k))
                              (loop (cdr tail))
                              (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                         ((string? v)
                          (if (= 0 (hl--c-workspace-rule-str k v))
                              (loop (cdr tail))
                              (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                         ((number? v)
                          (if (= 0 (hl--c-workspace-rule-num k (exact->inexact v)))
                              (loop (cdr tail))
                              (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                         ((boolean? v)
                          (if (= 0 (hl--c-workspace-rule-bool k (if v 1 0)))
                              (loop (cdr tail))
                              (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                         (else (hl--error 'hl-workspace-rule-add! "unsupported value for ~a" k))))))))))

;; ---- queries ------------------------------------------------------------------
(define (hl-window-from sel)
  "The window matching SELECTOR, or #f when none. Selectors use the
   config selector syntax: \"class:^foot$\", \"title:foo\", \"pid:123\",
   \"address:0x...\", \"workspace:3\", \"floating\", \"tiled\", ..."
  (hl--c-window-from (hl--str sel)))

(define (hl-urgent-window )
  "The most urgent window, as a handle, or #f when none."
  (hl--c-urgent-window))

(define (hl-last-window )
  "The previously-focused window, as a handle, or #f."
  (hl--c-last-window))

(define (hl-monitor-from sel)
  "The monitor matching SELECTOR (a name, or \"desc:DESCRIPTION\"in
   config monitor syntax), as a handle, or #f."
  (hl--c-monitor-from (hl--str sel)))

(define (hl-monitor-at x y)
  "The monitor at layout position (X, Y), as a handle, or #f."
  (hl--c-monitor-at (exact->inexact x) (exact->inexact y)))

(define (hl-monitor-at-cursor )
  "The monitor under the cursor, as a handle, or #f."
  (hl--c-monitor-at-cursor))

(define (hl-active-monitor )
  "The focused monitor, as a handle, or #f."
  (hl--c-active-monitor))

(define (hl-active-workspace )
  "The active workspace, as a handle, or #f."
  (hl--c-active-workspace))

(define (hl-active-special-workspace )
  "The currently open special workspace, as a handle, or #f."
  (hl--c-active-special-workspace))

(define (hl-last-workspace )
  "The most recently active workspace, as a handle, or #f."
  (hl--c-last-workspace))

(define (hl-workspace-windows ws)
  "The windows on workspace WS (a handle, or a selector like \"3\",
   \"name:foo\", \"special:bar\"), as a list of window handles."
  (let ((s (hl--c-workspace-windows (hl--ws-arg ws))))
    (if (not s)
        '()
        s)))

;; ---- workspace/monitor handle getters ---------------------------------------
;; every getter takes a handle; a stale or dead handle yields #f from every
;; getter, like an expired object in upstream Lua. Getters mirror Lua's
;; workspace/monitor object fields 1:1.

;; handles are accepted anywhere a selector string is: the handle resolves
;; through its canonical selector, like upstream's selector-or-object
;; helpers. A dead handle resolves to "" and the action fails cleanly.
(define (hl--ws-arg ws)
  (if (hl-workspace? ws)
      (or (hl--c-workspace-selector ws) "")
      (hl--str ws)))

(define (hl--mon-arg m)
  (if (hl-monitor? m)
      (or (hl--c-monitor-selector m) "")
      (hl--str m)))

;; -- workspace getters
(define (hl-workspace-name w)
  "The workspace's name."
  (hl--c-workspace-name w))

(define (hl-workspace-addressable-name w)
  "The workspace's config-addressable name (usable in selectors and
   rules)."
  (hl--c-workspace-addressable-name w))

(define (hl-workspace-number w)
  "The workspace's numbered ID, or #f for named/special workspaces."
  (hl--c-workspace-number w))

(define (hl-workspace-monitor w)
  "The monitor the workspace is on, as a handle, or #f."
  (hl--c-workspace-monitor w))

(define (hl-workspace-special? w)
  "#t when the workspace is a special workspace."
  (eq? (hl--c-workspace-special w) #t))

(define (hl-workspace-active? w)
  "#t when the workspace is the active (or active special) one on its
   monitor."
  (eq? (hl--c-workspace-active w) #t))

(define (hl-workspace-visible? w)
  "#t when the workspace is currently visible."
  (eq? (hl--c-workspace-visible w) #t))

(define (hl-workspace-empty? w)
  "#t when the workspace has no windows."
  (eq? (hl--c-workspace-empty w) #t))

(define (hl-workspace-persistent? w)
  "#t when the workspace is persistent."
  (eq? (hl--c-workspace-persistent w) #t))

(define (hl-workspace-has-urgent? w)
  "#t when some window on the workspace is urgent."
  (eq? (hl--c-workspace-has-urgent w) #t))

(define (hl-workspace-has-fullscreen? w)
  "#t when a window on the workspace is fullscreen."
  (eq? (hl--c-workspace-has-fullscreen w) #t))

(define (hl-workspace-fullscreen-mode w)
  "The workspace's internal fullscreen mode: 0 none, 1 maximized, 2
   fullscreen; -1 when none."
  (hl--c-workspace-fullscreen-mode w))

(define (hl-workspace-fullscreen-window w)
  "The workspace's fullscreen window, as a handle, or #f."
  (hl--c-workspace-fullscreen-window w))

(define (hl-workspace-last-window w)
  "The workspace's most recently focused window, as a handle, or #f."
  (hl--c-workspace-last-window w))

(define (hl-workspace-window-count w)
  "The number of windows on the workspace."
  (hl--c-workspace-window-count w))

(define (hl-workspace-group-count w)
  "The number of groups on the workspace."
  (hl--c-workspace-group-count w))

(define (hl-workspace-tiled-layout w)
  "The tiled layout currently serving the workspace, as a string."
  (hl--c-workspace-tiled-layout w))

(define (hl-workspace-alive? w)
  "#t when the workspace still exists."
  (eq? (hl--c-workspace-alive w) #t))

(define (hl-workspace=? a b)
  "#t when A and B refer to the same workspace."
  (eq? (hl--c-workspace-same a b) #t))

;; -- monitor getters
(define (hl-monitor-name m)
  "The monitor's name."
  (hl--c-monitor-name m))

(define (hl-monitor-description m)
  "The monitor's short description."
  (hl--c-monitor-description m))

(define (hl-monitor-number m)
  "The monitor's id."
  (hl--c-monitor-number m))

(define (hl-monitor-enabled? m)
  "#t when the monitor is enabled."
  (eq? (hl--c-monitor-enabled m) #t))

(define (hl-monitor-focused? m)
  "#t when the monitor has focus."
  (eq? (hl--c-monitor-focused m) #t))

(define (hl-monitor-x m)
  "The monitor's X position."
  (hl--c-monitor-x m))

(define (hl-monitor-y m)
  "The monitor's Y position."
  (hl--c-monitor-y m))

(define (hl-monitor-width m)
  "The monitor's width in pixels."
  (hl--c-monitor-width m))

(define (hl-monitor-height m)
  "The monitor's height in pixels."
  (hl--c-monitor-height m))

(define (hl-monitor-scale m)
  "The monitor's scale factor."
  (hl--c-monitor-scale m))

(define (hl-monitor-transform m)
  "The monitor's transform (rotation/flip, 0-7; see monitor-positioning
   in the wiki)."
  (hl--c-monitor-transform m))

(define (hl-monitor-refresh-rate m)
  "The monitor's refresh rate, in Hz."
  (hl--c-monitor-refresh-rate m))

(define (hl-monitor-mode m)
  "The monitor's current mode, as \"WIDTHxHEIGHT@RATE\"."
  (hl--c-monitor-mode m))

(define (hl-monitor-power? m)
  "#t when the monitor is powered on."
  (eq? (hl--c-monitor-dpms m) #t))

(define (hl-monitor-vrr? m)
  "#t when VRR is currently active on the monitor."
  (eq? (hl--c-monitor-vrr m) #t))

(define (hl-monitor-10bit? m)
  "#t when the monitor runs at 10-bit color depth."
  (eq? (hl--c-monitor-10bit m) #t))

(define (hl-monitor-reserved m)
  "The monitor's reserved area, as a plist: (top n left n right n bottom
   n)."
  (hl--c-monitor-reserved m))

(define (hl-monitor-serial m)
  "The monitor's serial, as a string, or #f when stale."
  (hl--c-monitor-serial m))

(define (hl-monitor-physical-size m)
  "The monitor's physical size in mm, as (w . h), or #f."
  (hl--c-monitor-physical-size m))

(define (hl-monitor-mirrors m)
  "The monitors mirroring this one, as a list of handles (empty when
   none)."
  (or (hl--c-monitor-mirrors m) '()))

(define (hl-monitor-available-modes m)
  "The monitor's available modes, as ((width w height h refresh-rate r
   preferred b) ...)."
  (or (hl--c-monitor-available-modes m) '()))

(define (hl-monitor-hardware-details m)
  "The monitor's hardware details, as a plist (backend \"...\"hdr b
   chroma b bt2020 b vrr-capable b)."
  (hl--c-monitor-hardware-details m))

(define (hl-monitor-mirror-of m)
  "The monitor this one mirrors, as a handle; #f when not a mirror."
  (hl--c-monitor-mirror-of m))

(define (hl-monitor-active-workspace m)
  "The monitor's active workspace, as a handle, or #f."
  (hl--c-monitor-active-workspace m))

(define (hl-monitor-active-special-workspace m)
  "The monitor's open special workspace, as a handle, or #f when none is
   open."
  (hl--c-monitor-active-special-workspace m))

(define (hl-monitor-alive? m)
  "#t when the monitor still exists."
  (eq? (hl--c-monitor-alive m) #t))

(define (hl-monitor-rule-add!=? a b)
  "#t when A and B refer to the same monitor."
  (eq? (hl--c-monitor-same a b) #t))

;; => list of (monitor . namespace) pairs
;; ---- layer surfaces as objects (upstream HL.LayerSurface parity) -------------
;; layer surfaces die independently -> weak handles via the guardian;
;; every getter returns #f when the surface is gone. NO callbacks.
;; filters are plist-style: (hl-layers), (hl-layers 'monitor MON),
;; (hl-layers 'namespace "ns"), or combined.
(define (hl-layer? x)
  "#t when X is a layer-surface handle."
  (hl--c-layer? x))


(define* (hl-layers #:key monitor namespace)
  "The layer surfaces, as a list of layer handles. Optional keyword
   filters: #:monitor MON, #:namespace \"ns\", or both combined."
  (let* ((mon  monitor)
         (ns   namespace)
         (monId (if (and mon (hl-monitor? mon)) mon 0))
         (l (hl--c-layers (or (and (hl-monitor? mon) mon) #f) (and ns (hl--str ns)))))
    (if l l '())))

(define (hl-layer-alive? s)
  "#t when the layer surface still exists."
  (= 1 (hl--c-layer-alive s)))

(define (hl-layer=? a b)
  "#t when A and B refer to the same layer surface."
  (= 1 (hl--c-layer-same a b)))

(define (hl-layer-address s)
  "The surface's stable \"0x...\"address (same idea as
   hl-window-address)."
  (hl--c-layer-address s))

(define (hl-layer-pid s)
  "The surface's owning process id."
  (hl--c-layer-pid s))

(define (hl-layer-monitor s)
  "The monitor the surface sits on, as a handle."
  (hl--c-layer-monitor s))

(define (hl-layer-namespace s)
  "The surface's namespace."
  (hl--c-layer-namespace s))

(define (hl-layer-level s)
  "The surface's shell layer: 0 background, 1 bottom, 2 top, 3 overlay."
  (hl--c-layer-level s))

(define (hl-layer-mapped? s)
  "#t when the surface is mapped."
  (eq? (hl--c-layer-mapped s) #t))

(define (hl-layer-kb-interactivity s)
  "The surface's keyboard interactivity: 0 none, 1 exclusive, 2
   on-demand (lock screens use exclusive)."
  (hl--c-layer-kb-interactivity s))

(define (hl-layer-above-fullscreen? s)
  "#t when the surface renders above fullscreen windows."
  (eq? (hl--c-layer-above-fs s) #t))

(define (hl-layer-position s)
  "The surface's position, as (x . y), monitor-local."
  (hl--c-layer-position s))

(define (hl-layer-size s)
  "The surface's size, as (width . height)."
  (hl--c-layer-size s))

(define (hl-is-key-down key)
  "#t when the key named by the xkb keysym KEY (e.g. \"Return\") is
   currently pressed."
  (= 1 (hl--c-is-key-down key)))

(define (hl-loaded-plugins )
  "The names of the loaded plugins."
  (or (hl--c-loaded-plugins) '()))

(define (hl-version )
  "The compositor's version string."
  (hl--c-version))

(define (hl-windows-from sel)
  "All windows matching SELECTOR, as a list of window handles."
  (let ((s (hl--c-windows-from (hl--str sel))))
    (if (not s)
        '()
        s)))

(define (hl-window-fullscreen-handler w)
  "Which fullscreen handler the window uses, as a string."
  (hl--c-window-fullscreen-handler w))

;; ---- notifications ------------------------------------------------------------
(define* (hl-notify! text duration #:key (icon "none") (color "0") (font-size 13))
  "Show a one-shot notification: (hl-notify! \"text\" 5000), or with
   keyword options: (hl-notify! \"text\" 5000 #:icon \"info\" #:color
   \"0x80FF80FF\" #:font-size 13). Icons: none warn info hint error
   confused ok. Color: 0xAARRGGBB hex string; 0 = the icon's default
   color."
  (let ((s (hl--c-notify (hl--str text) (exact->inexact duration)
                        (hl--str icon)
                        (hl--str color)
                        (exact->inexact font-size))))
    (if s #t #f)))

;; ---- live notification objects (upstream hl.notification parity) ------------
;; (hl-notification-add! 'text "…" 'timeout ms ['icon "info"] ['color "…"]
;;   ['font-size n]) => a notification HANDLE. 'timeout is required; pause
;; freezes the timeout timer (the bubble stays until dismissed); expired
;; handles read #f and mutate as no-ops.

(define* (hl-notification-add! #:key text timeout icon color font-size)
  "Create a live notification and return its handle. #:text (string)
   and #:timeout (ms) are required; #:icon, #:color and #:font-size are
   optional. Omitted keywords are sent as absent, and the notification
   subsystem applies its own defaults (icon \"none\", color 0, font
   size 13) — Scheme holds no defaults here. Pausing freezes the
   timeout timer (the bubble stays until dismissed); expired handles
   read #f and mutate as no-ops."
  (let ((fields (append
                  (if text (list 'text text) '())
                  (if timeout (list 'timeout timeout) '())
                  (if icon (list 'icon icon) '())
                  (if color (list 'color color) '())
                  (if font-size (list 'font-size font-size) '()))))
    (let ((n (hl--c-notification-add fields)))
      (if n
          n
          (hl--error 'hl-notification-add! "~a" (hl--c-config-last-error))))))

(define (hl-notifications )
  "The live notifications, as a list of handles."
  (or (hl--c-notification-list) '()))

(define (hl-notification-text n)
  "The notification's text."
  (hl--c-notification-text n))

(define (hl-notification-timeout n)
  "The notification's timeout, in ms."
  (hl--c-notification-timeout n))

(define (hl-notification-color n)
  "The notification's color, as the raw 0xAARRGGBB integer (readback
   parity with upstream)."
  (hl--c-notification-color n))

(define (hl-notification-icon n)
  "The notification's icon, as its hyprctl id (names exist on the write
   side only)."
  (hl--c-notification-icon n))

(define (hl-notification-font-size n)
  "The notification's font size."
  (hl--c-notification-font-size n))

(define (hl-notification-elapsed n)
  "How long the notification has been live, in ms, excluding paused
   spans."
  (hl--c-notification-elapsed n))

(define (hl-notification-age n)
  "Wall time since the notification was created, in ms."
  (hl--c-notification-age n))

(define (hl-notification-alive? n)
  "#t when the notification is still live."
  (eq? (hl--c-notification-alive n) #t))

(define (hl-notification=? a b)
  "#t when A and B are the same notification."
  (eq? (hl--c-notification-same a b) #t))

;; failed setters raise with the config system's own message
(define (hl--notif-act who act)
  (if (= 0 act) #t (hl--error who "~a" (hl--c-config-last-error))))

(define (hl-notification-text-set! n text)
  "Set the notification's text."
  (hl--notif-act 'hl-notification-text-set!
    (hl--c-notification-text-set n (hl--str text))))

(define (hl-notification-timeout-set! n ms)
  "Set the notification's timeout, in ms."
  (hl--notif-act 'hl-notification-timeout-set!
    (hl--c-notification-timeout-set n (exact->inexact ms))))

(define (hl-notification-color-set! n color)
  "Set the notification's color (0xAARRGGBB hex string)."
  (hl--notif-act 'hl-notification-color-set!
    (hl--c-notification-color-set n (hl--str color))))

(define (hl-notification-icon-set! n icon)
  "Set the notification's icon (icon name, e.g. \"warn\")."
  (hl--notif-act 'hl-notification-icon-set!
    (hl--c-notification-icon-set n icon)))

(define (hl-notification-font-size-set! n size)
  "Set the notification's font size."
  (hl--notif-act 'hl-notification-font-size-set!
    (hl--c-notification-font-size-set n (exact->inexact size))))

(define* (hl-notification-paused-set! n #:key (on? 'unset))
  "Pause or resume the notification's timeout timer: absent #:on? toggles,
   #t/#f set."
  (= 0 (hl--c-notification-paused-set n
         (if (if (eq? on? 'unset) (not (eq? (hl-notification-paused? n) #t)) (eq? on? #t)) 1 0))))

(define (hl-notification-paused? n)
  "#t when the notification is paused (its timeout timer frozen)."
  (eq? (hl--c-notification-paused-q n) #t))

(define (hl-notification-dismiss! n)
  "Dismiss the notification."
  (= 0 (hl--c-notification-dismiss n)))

;; ---- timer handles --------------------------------------------------------------
(define* (hl-timer-enabled-set! t #:key (on? 'unset))
  "Enable, disable or toggle the timer T: absent #:on? toggles, #t/#f set."
  (if (= 0 (hl--c-timer-set-enabled t
                                   (if (if (eq? on? 'unset) (not (hl-timer-enabled? t)) on?) 1 0)))
      #t
      (hl--error 'hl-timer-enabled-set! "unknown timer")))
(define (hl-timer-enabled? t)
  "#t when the timer T is enabled."
  (= 1 (hl--c-timer-enabled t)))
(define (hl-timer-set-timeout t ms)
  "Re-tune the timer T's timeout to MS (>= 1)."
  (if (= 0 (hl--c-timer-set-timeout t (exact->inexact ms)))
      #t
      (hl--error 'hl-timer-set-timeout "timeout must be >= 1ms")))

(define (hl-timer-cancel! t)
  "Cancel a repeating timer before reload (beyond-upstream extension).
   The index entry erases itself; the record lock releases with the
   timer."
  (= 0 (hl--c-timer-cancel t)))


;; ---- gestures ----------------------------------------------------------------------
;;
(define (hl-gesture-action? x)
  "#t when X is an hl-gesture-action - the opaque action value built by
   the hl-make-*-gesture constructors."
  (and (pair? x) (integer? (car x)) (exact? (car x)) (positive? (car x)) (list? (cdr x))))

;; one optional mode argument, restricted to the allowed symbols
(define (hl--gesture-mode name args allowed default)
  (cond ((null? args) default)
        ((and (= 1 (length args)) (memq (car args) allowed)) (car args))
        (else (hl--error name "mode must be one of ~a" allowed))))

(define (hl-make-workspace-swipe-gesture )
  "A gesture action that swipes workspaces."
  (cons (hl--c-gesture-maker-workspace-swipe) '()))

(define (hl-make-move-gesture )
  "A gesture action that moves the active window."
  (cons (hl--c-gesture-maker-move) '()))

(define (hl-make-resize-gesture )
  "A gesture action that resizes the active window."
  (cons (hl--c-gesture-maker-resize) '()))

(define (hl-make-close-gesture )
  "A gesture action that closes the active window."
  (cons (hl--c-gesture-maker-close) '()))

(define (hl-make-scroll-move-gesture )
  "A gesture action that scrolls the tape (when the current layout
   supports it, e.g. scrolling)."
  (cons (hl--c-gesture-maker-scroll-move) '()))

(define (hl-make-float-gesture . mode)
  "A gesture action that floats/tiles the active window. Optional MODE:
   'toggle (default), 'float, 'tile."
  (let ((m (hl--gesture-mode 'hl-make-float-gesture mode '(toggle float tile) 'toggle)))
    (cons (hl--c-gesture-maker-float) (list 'mode m))))

(define (hl-make-fullscreen-gesture . mode)
  "A gesture action that fullscreens the active window. Optional MODE:
   'fullscreen (default), 'maximize."
  (let ((m (hl--gesture-mode 'hl-make-fullscreen-gesture mode '(fullscreen maximize) 'fullscreen)))
    (cons (hl--c-gesture-maker-fullscreen) (list 'mode m))))

(define (hl-make-special-workspace-gesture name)
  "A gesture action that toggles the named special workspace (empty
   string = the default special workspace)."
  (unless (string? name)
    (hl--error 'hl-make-special-workspace-gesture "workspace name must be a string, got ~a" name))
  (cons (hl--c-gesture-maker-special) (list 'name name)))

(define (hl-make-cursor-zoom-gesture zoom . mode)
  "A gesture action that zooms the cursor's view. ZOOM is a number;
   optional MODE: 'toggle (default), 'mult, 'live - the numeric argument
   is unused in live mode, so 1 is a good placeholder there."
  (unless (real? zoom)
    (hl--error 'hl-make-cursor-zoom-gesture "zoom must be a number, got ~a" zoom))
  (let ((m (hl--gesture-mode 'hl-make-cursor-zoom-gesture mode '(toggle mult live) 'toggle)))
    (cons (hl--c-gesture-maker-cursor-zoom) (list 'zoom (exact->inexact zoom) 'mode m))))

(define* (hl-make-custom-gesture #:key start update finish)
  "A callback-backed gesture action: (hl-make-custom-gesture #:start FN
   #:update FN #:finish FN) - at least one procedure required; each
   receives the gesture event plist as spread args (see bind-gestures
   for the fields). The three closures share state naturally by closing
   over a let - wrap the constructor in a function to get fresh state
   per registration."
  (when (and (not start) (not update) (not finish))
    (hl--error 'hl-make-custom-gesture "at least one of #:start, #:update, #:finish is required"))
  (for-each (lambda (k fn)
              (when (and fn (not (procedure? fn)))
                (hl--error 'hl-make-custom-gesture "field ~a needs a procedure" k)))
            '(#:start #:update #:finish) (list start update finish))
  (cons (hl--c-gesture-maker-custom)
        (append (if start (list 'start start) '())
                (if update (list 'update update) '())
                (if finish (list 'finish finish) '()))))

(define* (hl-gesture-add! #:key fingers direction action (mods '()) (scale 1.0) (disable-inhibit #f))
  "Register a gesture: (hl-gesture-add! #:fingers N #:direction \"dir\"
   #:action ACTION [#:mods (hl-key \"SUPER\")] [#:scale 1.0]
   [#:disable-inhibit #t]). #:fingers, #:direction and #:action are
   required; the optional keywords describe the gesture INPUT,
   independent of the action. ACTION is an hl-gesture-action built by
   the hl-make-*-gesture constructors - built-in and custom actions are
   indistinguishable from the caller's side, and a recipe is a pure
   value (registering it under two specs constructs two independent
   gestures). Returns an hl-gesture handle for hl-gesture-remove! -
   removal matches on the registration spec, never on the action."
  (let ()
    (cond ((not fingers)
           (hl--error 'hl-gesture-add! "field 'fingers' is required"))
          ((not (and (integer? fingers) (exact? fingers) (>= fingers 2)))
           (hl--error 'hl-gesture-add! "field 'fingers' must be an integer >= 2"))
          ((not direction)
           (hl--error 'hl-gesture-add! "field 'direction' is required"))
          ((not (string? direction))
           (hl--error 'hl-gesture-add! "field 'direction' must be a string"))
          ((not action)
           (hl--error 'hl-gesture-add! "an action is required — #:action (hl-make-...-gesture ...)"))
          ((not (hl-gesture-action? action))
           (hl--error 'hl-gesture-add! "field 'action' must be an hl-gesture-action (see the hl-make-*-gesture constructors)"))
          ((not (and (list? mods) (every (lambda (m) (string? m)) mods)))
           (hl--error 'hl-gesture-add! "'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\") or '(\"SUPER\" \"SHIFT\")"))
          ((not (or (<= -10.0 scale -0.1) (<= 0.1 scale 10.0)))
           (hl--error 'hl-gesture-add! "field 'scale' must be between -10 and -0.1 or between 0.1 and 10 - it is currently: ~a" scale))
          (else
           (let ((rc (hl--c-gesture action fingers (hl--str direction) mods (exact->inexact scale) (if disable-inhibit 1 0))))
             (cond ((not (= 0 rc))
                    (hl--error 'hl-gesture-add! "~a" (hl--c-config-last-error)))
                   (else
                    ;; the handle is the exact registration spec — the manager
                    ;; matches removal on it, so remove! always hits our gesture
                    (list fingers direction mods scale disable-inhibit))))))))

(define (hl-gesture? x)
  "#t when X is an hl-gesture handle - the registration spec: (fingers
   direction mods scale disable-inhibit)."
  (and (pair? x) (list? x) (= 5 (length x)) (integer? (car x)) (string? (cadr x)) (list? (caddr x))))

(define (hl-gesture-remove! g)
  "Remove a registered gesture: #t when removed, #f when nothing is
   registered under that spec, error otherwise."
  (unless (hl-gesture? g)
    (hl--error 'hl-gesture-remove! "not an hl-gesture handle"))
  (let ((rc (hl--c-gesture-remove (car g) (cadr g) (caddr g) (exact->inexact (cadddr g))
                                 (if (list-ref g 4) 1 0))))
    (cond ((= rc 0) #t)
          ((= rc 1) #f)
          (else (hl--error 'hl-gesture-remove! "~a" (hl--c-config-last-error))))))

;; ---- monitors, curves, animations, permissions ------------------------------

(define* (hl-monitor-rule-add! output #:key #:allow-other-keys #:rest fields)
  "Add a monitor rule for OUTPUT (name or desc: selector), e.g.
   (hl-monitor-rule-add! \"DP-1\" #:mode \"preferred\" #:scale \"1.6\"
   #:position \"0x0\" #:transform 0 #:bitdepth 10 #:vrr 1 #:reserved
   '((top . 60))). String fields: mode position scale mirror cm icc
   sdr_eotf. Numeric fields: transform bitdepth vrr supports_wide_color
   supports_hdr sdrbrightness sdrsaturation sdr_min_luminance
   sdr_max_luminance min_luminance max_luminance max_avg_luminance. Gap
   fields (plist): reserved / reserved_area. Bool: disabled."
  (let ((plist (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)))
    (if (not (= 0 (hl--c-monitor-begin (hl--mon-arg output))))
        (hl--error 'hl-monitor-rule-add! "~a" (hl--c-config-last-error))
        (let loop ((rest plist))
        (cond ((null? rest)
               (if (= 0 (hl--c-monitor-commit))
                   #t
                   (hl--error 'hl-monitor-rule-add! "~a" (hl--c-config-last-error))))
              (else
               (let* ((tail (hl--plist-cdr rest))
                      (f  (hl--str (car rest)))
                      (v  (car tail)))
                 (cond ((string? v)
                        (if (= 0 (hl--c-monitor-field-str f v))
                            (loop (cdr tail))
                            (hl--error 'hl-monitor-rule-add! "~a" (hl--c-config-last-error))))
                       ((number? v)
                        (if (= 0 (hl--c-monitor-field-num f (exact->inexact v)))
                            (loop (cdr tail))
                            (hl--error 'hl-monitor-rule-add! "~a" (hl--c-config-last-error))))
                       ((boolean? v)
                        (if (= 0 (hl--c-monitor-field-bool f (if v 1 0)))
                            (loop (cdr tail))
                            (hl--error 'hl-monitor-rule-add! "~a" (hl--c-config-last-error))))
                       ((pair? v)
                        (hl--c-config-begin)
                        (hl--push-val v)
                        (if (= 0 (hl--c-monitor-field-gap f))
                            (loop (cdr tail))
                            (hl--error 'hl-monitor-rule-add! "~a" (hl--c-config-last-error))))
                       (else (hl--error 'hl-monitor-rule-add! "unsupported value for ~a" f))))))))))

(define (hl-curve-add! name type . vals)
  "Declare an animation curve: (hl-curve-add! \"mycurve\"'bezier 0.25
   0.1 0.25 1.0) or (hl-curve-add! \"myspring\"'spring 250 25 1)."
  (let* ((t  (if (eq? type 'spring) 1 0))
         ;; pad THEN coerce: exact zeros are invalid in foreign double slots
         (vs (map exact->inexact (append vals (list 0 0 0 0)))))
    (if (= 0 (hl--c-curve-add (hl--str name) t
                             (list-ref vs 0) (list-ref vs 1) (list-ref vs 2) (list-ref vs 3)))
        #t
        (hl--error 'hl-curve-add! "~a" (hl--c-config-last-error)))))

(define* (hl-animation-add! leaf #:key (enabled #t) (speed 8) (curve "") (style ""))
  "Configure an animation: (hl-animation-add! \"windowsIn\" #:enabled #t
   #:speed 8 #:curve \"mycurve\" #:style \"popin 80%\"). Speed defaults
   to 8; declare curves with hl-curve-add! first (or use builtins like
   \"default\")."
  (let* ((enabled enabled)
         (speed   speed)
         (curve   curve)
         (style   style))
    (if (= 0 (hl--c-animation-set (hl--str leaf) (if enabled 1 0)
                                 (exact->inexact speed) (hl--str curve) (hl--str style)))
        #t
        (hl--error 'hl-animation-add! "~a" (hl--c-config-last-error)))))

(define (hl-permission-add! binary type mode)
  "Set a permission rule: (hl-permission-add! \"/usr/bin/grim\"
   'screencopy 'allow). Only takes effect at first launch - permission
   rules require a compositor restart."
  (if (= 0 (hl--c-permission-add binary (hl--str type) (hl--str mode)))
      #t
      (hl--error 'hl-permission-add! "~a" (hl--c-config-last-error))))


(define* (hl-window-fullscreen-set! w #:key (on? 'unset))
  "Toggle or set WINDOW's fullscreen: absent #:on? runs the fullscreen
   toggle; #t sets fullscreen mode, #f unsets. Modes mirror the internal
   fullscreen modes: 1 maximized, 2 fullscreen."
  (if (eq? on? 'unset)
      (= 0 (hl--c-window-fullscreen-toggle w 2))
      (= 0 (hl--c-window-fullscreen-set w (if on? 2 0)))))

(define* (hl-window-maximized-set! w #:key (on? 'unset))
  "Toggle or set WINDOW's maximized state: the maximized flavour of
   hl-window-fullscreen-set!."
  (if (eq? on? 'unset)
      (= 0 (hl--c-window-fullscreen-toggle w 1))
      (= 0 (hl--c-window-fullscreen-set w (if on? 1 0)))))

(define (hl-window-fullscreen-state w internal client . layout-aware)
  "Set the explicit fullscreen state: INTERNAL and CLIENT are modes
   0/1/2; LAYOUT-AWARE? (absent = #f) makes the internal mode
   layout-aware."
  (= 0 (hl--c-window-fullscreen-state w internal client
         (if (null? layout-aware) 0 (if (car layout-aware) 1 0)))))

(define (hl-window-fullscreen-mode w)
  "The window's fullscreen mode: 0 none, 1 maximized, 2 fullscreen; -1
   when stale."
  (hl--c-window-fullscreen-mode w))

(define (hl-window-hidden? w)
  "#t when the window is hidden."
  (= 1 (hl--c-window-hidden w)))

;; ---- window read-side fields (upstream LuaWindow field parity) --------------
(define (hl-window-address w)
  "The window's stable \"0x...\"address - identity across queries:
   handles differ per call, the address does not."
  (hl--c-window-address w))

(define (hl-window-mapped? w)
  "#t when the window is mapped."
  (= 1 (hl--c-window-mapped w)))

(define (hl-window-visible? w)
  "#t when the window is mapped, accepting input and with non-zero
   alpha."
  (= 1 (hl--c-window-visible w)))

(define (hl-window-accepts-input? w)
  "#t when the window accepts input."
  (= 1 (hl--c-window-accepts-input w)))

(define (hl-window-position w)
  "The window's position, as (x . y), or #f when stale."
  (hl--c-window-position w))

(define (hl-window-pin-fullscreened? w)
  "#t when the window stays pinned even when fullscreened."
  (= 1 (hl--c-window-pin-fullscreened w)))

(define (hl-window-allowed-over-fullscreen? w)
  "#t when the window renders above fullscreen windows."
  (= 1 (hl--c-window-allowed-over-fullscreen w)))

(define (hl-window-tearing-hint? w)
  "#t when the window hints it wants tearing."
  (= 1 (hl--c-window-tearing-hint w)))

(define (hl-window-inhibiting-idle? w)
  "#t when the window holds an idle inhibitor."
  (= 1 (hl--c-window-inhibiting-idle w)))

(define (hl-window-focus-history-id w)
  "The window's position in the focus history: 0 is the most recently
   focused; -1 when not in history."
  (hl--c-window-focus-history-id w))

(define (hl-window-content-type w)
  "The window's content type: \"none\", \"photo\", \"video\"or \"game\"."
  (hl--c-window-content-type w))

(define (hl-window-stable-id w)
  "The window's metadata stable id, as a hex string."
  (hl--c-window-stable-id w))

(define (hl-window-tags w)
  "The window's static tags, as a list of strings (set with
   hl-window-tag-add!)."
  (hl--c-window-tags w))

(define (hl-window-swallowing w)
  "The window this one is swallowing, as a handle, or #f."
  (hl--c-window-swallowing-id w))

(define (hl-window-xdg-tag w)
  "The window's xdg-shell tag metadata, or #f when unset."
  (hl--c-window-xdg-tag w))

(define (hl-window-xdg-description w)
  "The window's xdg-shell description, or #f when unset."
  (hl--c-window-xdg-description w))

(define (hl-window-layout w)
  "The window's tiled layout state, as a plist: ('name \"master\"
   'is-master #f 'perc-master 0.5 'perc-size 1.0) or ('name
   \"scrolling\"'column (...) 'index-in-column n); #f when the window is
   floating or has no tiled layout target."
  (hl--c-window-layout w))

(define (hl-window-pinned? w)
  "#t when the window is pinned."
  (= 1 (hl--c-window-pinned w)))

(define (hl-window-pseudo? w)
  "#t when the window is pseudo-tiled."
  (= 1 (hl--c-window-pseudo-query w)))

(define (hl-window-maximized? w)
  "#t when the window is maximized."
  (= 1 (hl--c-window-maximized-query w)))

(define (hl-window-deny-from-group? w)
  "#t when the window denies being grouped."
  (= 1 (hl--c-window-group-denied w)))

(define (hl-window-prop w prop)
  "Read back a dynamic window prop set with hl-window-prop-set!: #t/#f
   for booleans, numbers for opacities and border/rounding; #f for
   unknown props."
  (hl--c-window-prop w (hl--str prop)))

(define (hl-window-initial-class w)
  "The class the window opened with (what static rules match on)."
  (hl--c-window-initial-class w))

(define (hl-window-initial-title w)
  "The title the window opened with (what static rules match on)."
  (hl--c-window-initial-title w))

(define (hl-window-x11? w)
  "#t when the window is an Xwayland window."
  (= 1 (hl--c-window-x11 w)))

(define (hl-monitors )
  "All monitors, as a list of monitor handles."
  (or (hl--c-monitor-names) '()))

;; ---- events ------------------------------------------------------------------
;; each notification-add! creates an hl-event record (type symbol + handler),
;; hands it to C++ (locked inside the bus connection), and returns the record.
;; handler shapes are per event — see the events wiki page.

;; ---- window events (one C++ entry, a which-number per kind) -------------------
(define (hl--window-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (hl--c-window-event-listen rec which))
        rec
        (hl--error who "listener rejected, see compositor log"))))

(define (hl-window-open-notification-add! handler)
  "Register a handler that fires when a window opens (fully initialized,
   window rules applied). The handler receives the window handle."
  (hl--window-listen 0 'window-open handler 'hl-window-open-notification-add!))

(define (hl-window-close-notification-add! handler)
  "Register a handler that fires when a window is closed. The handler
   receives the window handle."
  (hl--window-listen 1 'window-close handler 'hl-window-close-notification-add!))

(define (hl-window-title-notification-add! handler)
  "Register a handler that fires when a window's title changes. The
   handler receives the window handle."
  (hl--window-listen 2 'window-title handler 'hl-window-title-notification-add!))

(define (hl-window-class-notification-add! handler)
  "Register a handler that fires when a window's class changes. The
   handler receives the window handle."
  (hl--window-listen 3 'window-class handler 'hl-window-class-notification-add!))

(define (hl-window-urgent-notification-add! handler)
  "Register a handler that fires when a window requests urgent
   attention. The handler receives the window handle."
  (hl--window-listen 4 'window-urgent handler 'hl-window-urgent-notification-add!))

(define (hl-window-pin-notification-add! handler)
  "Register a handler that fires when a window is pinned or unpinned.
   The handler receives the window handle."
  (hl--window-listen 5 'window-pin handler 'hl-window-pin-notification-add!))

(define (hl-window-fullscreen-notification-add! handler)
  "Register a handler that fires when a window's fullscreen state
   changes. The handler receives the window handle."
  (hl--window-listen 6 'window-fullscreen handler 'hl-window-fullscreen-notification-add!))

(define (hl-window-move-to-workspace-notification-add! handler)
  "Register a handler that fires when a window moves to a different
   workspace. The handler receives the window handle."
  (hl--window-listen 7 'window-move-to-workspace handler 'hl-window-move-to-workspace-notification-add!))

(define (hl-window-active-notification-add! handler)
  "Register a handler that fires when the focused window changes. The
   handler receives the window handle."
  (hl--window-listen 8 'window-active handler 'hl-window-active-notification-add!))

(define (hl-window-minimize-notification-add! handler)
  "Register a handler that fires when a window is minimized or restored:
   (lambda (w state) ...) - state is #t when minimized."
  (let ((rec (make-hl-event 'window-minimize handler)))
    (if (= 0 (hl--c-window-minimize-listen rec))
        rec
        (hl--error 'hl-window-minimize-notification-add! "listener rejected, see compositor log"))))

(define (hl-window-open-early-notification-add! handler)
  "Register a handler that fires when a window is created and mapped,
   but before window rules are applied (window-open waits for full
   initialization). The handler receives the window handle."
  (hl--window-listen 9 'window-open-early handler 'hl-window-open-early-notification-add!))

(define (hl-window-kill-notification-add! handler)
  "Register a handler that fires when a window is forcefully killed,
   e.g. via hyprctl kill. The handler receives the window handle."
  (hl--window-listen 10 'window-kill handler 'hl-window-kill-notification-add!))

(define (hl-window-bell-notification-add! handler)
  "Register a handler that fires when a window rings the system bell,
   even when muted. The handler receives the window handle."
  (hl--window-listen 11 'window-bell handler 'hl-window-bell-notification-add!))

(define (hl-window-update-rules-notification-add! handler)
  "Register a handler that fires when a window's rules are re-evaluated,
   e.g. on a title change. The handler receives the window handle."
  (hl--window-listen 12 'window-update-rules handler 'hl-window-update-rules-notification-add!))

(define (hl-start-notification-add! handler)
  "Register a handler that fires once when the session starts (its first
   render frame). Handlers registered after startup fire immediately."
  (let ((rec (make-hl-event 'start handler)))
    (if (= 0 (hl--c-lifecycle-listen rec 0))
        rec
        (hl--error 'hl-start-notification-add! "listener rejected, see compositor log"))))

(define (hl-shutdown-notification-add! handler)
  "Register a handler that fires once before the session exits."
  (let ((rec (make-hl-event 'shutdown handler)))
    (if (= 0 (hl--c-lifecycle-listen rec 1))
        rec
        (hl--error 'hl-shutdown-notification-add! "listener rejected, see compositor log"))))

(define (hl-config-reloaded-notification-add! handler)
  "Register a handler that fires after the config has been reloaded."
  (let ((rec (make-hl-event 'config-reloaded handler)))
    (if (= 0 (hl--c-config-reloaded-listen rec))
        rec
        (hl--error 'hl-config-reloaded-notification-add! "listener rejected, see compositor log"))))

(define (hl-config-unload-notification-add! handler)
  "Register a handler that fires BEFORE a config reload."
  (let ((rec (make-hl-event 'config-unload handler)))
    (if (= 0 (hl--c-config-unload-listen rec))
        rec
        (hl--error 'hl-config-unload-notification-add! "listener rejected, see compositor log"))))

(define (hl-window-destroy-notification-add! handler)
  "Register a handler that fires when a window is destroyed.
   Zero-argument callback (the bus event is a weak ref; window identity
   belongs to the window-close notification)."
  (let ((rec (make-hl-event 'window-destroy handler)))
    (if (= 0 (hl--c-window-destroy-listen rec))
        rec
        (hl--error 'hl-window-destroy-notification-add! "listener rejected, see compositor log"))))

(define (hl-layer-open-notification-add! handler)
  "Register a handler that fires when a layer surface opens. The handler
   receives the layer's namespace string."
  (let ((rec (make-hl-event 'layer-open handler)))
    (if (= 0 (hl--c-layer-listen rec 0))
        rec
        (hl--error 'hl-layer-open-notification-add! "listener rejected, see compositor log"))))

(define (hl-layer-close-notification-add! handler)
  "Register a handler that fires when a layer surface closes. The
   handler receives the layer's namespace string."
  (let ((rec (make-hl-event 'layer-close handler)))
    (if (= 0 (hl--c-layer-listen rec 1))
        rec
        (hl--error 'hl-layer-close-notification-add! "listener rejected, see compositor log"))))

(define (hl-keyboard-key-notification-add! handler)
  "Register a handler that fires on every key event - high-frequency;
   keep handlers trivial."
  (let ((rec (make-hl-event 'keyboard-key handler)))
    (if (= 0 (hl--c-keyboard-key-listen rec))
        rec
        (hl--error 'hl-keyboard-key-notification-add! "listener rejected, see compositor log"))))

(define (hl-screenshare-state-notification-add! handler)
  "Register a handler that fires when a screenshare session starts or
   ends."
  (let ((rec (make-hl-event 'screenshare-state handler)))
    (if (= 0 (hl--c-screenshare-listen rec))
        rec
        (hl--error 'hl-screenshare-state-notification-add! "listener rejected, see compositor log"))))

(define (hl-config-props-refreshed-notification-add! handler)
  "Register a handler that fires after the deferred prop-refresh pass
   runs: (lambda (scheduled?) ...) - #t when it ran as scheduled, #f
   when executed prematurely."
  (let ((rec (make-hl-event 'config-props-refreshed handler)))
    (if (= 0 (hl--c-config-props-refreshed-listen rec))
        rec
        (hl--error 'hl-config-props-refreshed-notification-add! "listener rejected, see compositor log"))))

(define (hl-submap-notification-add! handler)
  "Register a handler that fires when the active submap changes. The
   handler receives the submap name; an empty string means the default
   submap was restored."
  (let ((rec (make-hl-event 'submap handler)))
    (if (= 0 (hl--c-submap-listen rec))
        rec
        (hl--error 'hl-submap-notification-add! "listener rejected, see compositor log"))))

;; ---- workspace events ---------------------------------------------------------
(define (hl-workspace-active-notification-add! handler)
  "Register a handler that fires when the active workspace changes. The
   handler receives the workspace handle."
  (let ((rec (make-hl-event 'workspace-active handler)))
    (if (= 0 (hl--c-workspace-active-listen rec))
        rec
        (hl--error 'hl-workspace-active-notification-add! "listener rejected, see compositor log"))))

;; handler signature: (lambda (ws) ...) — ws is a workspace handle
(define (hl--workspace-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (hl--c-workspace-event-listen rec which))
        rec
        (hl--error who "listener rejected, see compositor log"))))

(define (hl-workspace-created-notification-add! handler)
  "Register a handler that fires when a workspace is created. The
   handler receives the workspace handle."
  (hl--workspace-listen 0 'workspace-created handler 'hl-workspace-created-notification-add!))

(define (hl-workspace-removed-notification-add! handler)
  "Register a handler that fires when a workspace is removed. The handle
   it passes is BORN DEAD - every getter on it returns #f, exactly like
   an expired object."
  (hl--workspace-listen 1 'workspace-removed handler 'hl-workspace-removed-notification-add!))

(define (hl-workspace-special-active-notification-add! handler)
  "Register a handler that fires when the opened special workspace on a
   monitor changes: (lambda (ws mon) ...) - ws is #f when no special
   workspace is open on that monitor."
  (hl--workspace-listen 2 'workspace-special-active handler 'hl-workspace-special-active-notification-add!))

(define (hl-workspace-move-to-monitor-notification-add! handler)
  "Register a handler that fires when a workspace moves to a different
   monitor: (lambda (ws mon) ...)."
  (hl--workspace-listen 3 'workspace-move-to-monitor handler 'hl-workspace-move-to-monitor-notification-add!))

;; ---- monitor events -----------------------------------------------------------
;; handler signature: (lambda (mon) ...) — mon is a monitor handle
(define (hl--monitor-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (hl--c-monitor-event-listen rec which))
        rec
        (hl--error who "listener rejected, see compositor log"))))

(define (hl-monitor-added-notification-add! handler)
  "Register a handler that fires when a monitor is added. The handler
   receives the monitor handle."
  (hl--monitor-listen 0 'monitor-added handler 'hl-monitor-added-notification-add!))

(define (hl-monitor-removed-notification-add! handler)
  "Register a handler that fires when a monitor is removed. The handler
   receives the monitor handle."
  (hl--monitor-listen 1 'monitor-removed handler 'hl-monitor-removed-notification-add!))

(define (hl-monitor-focused-notification-add! handler)
  "Register a handler that fires when the focused monitor changes. The
   handler receives the monitor handle."
  (hl--monitor-listen 2 'monitor-focused handler 'hl-monitor-focused-notification-add!))

(define (hl-monitor-layout-changed-notification-add! handler)
  "Register a handler that fires when the monitor arrangement changes
   (no payload)."
  (hl--monitor-listen 3 'monitor-layout-changed handler 'hl-monitor-layout-changed-notification-add!))

(define (hl-window=? a b)
  "#t when A and B refer to the same live window."
  (= 1 (hl--c-window-same a b)))

(define (hl-unbind! b)
  "Remove a bind: pass back the hl-bind record hl-bind-add! returned."
  (= 0 (hl--c-unbind-rec b)))

(define (hl-unbind-key! key)
  "Remove EVERY bind whose display key matches KEY (case- and
   whitespace-insensitive, manager-side). For precise removal use
   hl-unbind!."
  (= 0 (hl--c-unbind-key (hl--str key))))

(define (hl-notification-remove! rec)
  "Remove an event listener: drop the connection; the record goes inert.
   #f when already gone."
  (= 0 (hl--c-event-cancel rec)))
(define (hl-notification-active? rec)
  "#t when the event listener is still connected."
  (= 1 (hl--c-event-active rec)))

(define (hl-current-submap )
  "The active submap's name, or the empty string when no submap is
   active."
  (or (hl--c-current-submap) ""))

(define (hl-cursor-pos )
  "The cursor position, as (x . y), or #f."
  (hl--c-cursor-pos))   ; (x . y), or #f

;; reload boundary: drop the previous generation's handler table and install
;; a FRESH environment for the new one. The copy inherits the API (defined
;; once in the persistent environment) but user definitions from previous
;; generations become unreachable when the copy is replaced — the same
;; clean-slate semantics as upstream's per-generation lua_State. hl--state
;; is the one deliberate cross-generation bridge (it lives in the persistent
;; environment).
(define (hl--reset)
  ;; break the chain: clear the slot BEFORE copying so the new copy does
  ;; not retain the previous generation's environment through it (that
  ;; would keep every old generation alive), and user code never sees a
  ;; stale env reference
  (set! hl--generation #f)
  (set! hl--generation (hl--generation-copy (interaction-environment))))

;; cross-reload state: lives in the bootstrap (evaluated ONCE), deliberately
;; OUTSIDE hl--reset's reach. Reloads wipe binds/timers/listeners/handles;
;; this assoc survives for as long as the interpreter does. Note that
;; generation-bound values stored here (e.g. window handles) still die with
;; their generation — the state survives, the resources do not.
(define hl--state '())

(define (hl-state-set! k v) "Cross-reload state: store VALUE under KEY.
The state survives config reloads (the one deliberate cross-generation
bridge — the state survives, resources die with their generation); a
re-set keeps the original position. Returns VALUE."
  (define (put l)
    (cond ((null? l) (list k v))
          ((eq? (car l) k) (append (list k v) (cddr l)))
          (else (cons (car l) (cons (cadr l) (put (cddr l)))))))
  (set! hl--state (put hl--state))
  v)

(define (hl-state-ref k . default) "Cross-reload state: KEY's value, or
DEFAULT (#f when omitted) when the key is missing."
  (let ((v (hl--plist-get hl--state k the-eof-object)))
    (if (eq? v the-eof-object) (if (null? default) #f (car default)) v)))

(define (hl-state-keys ) "Cross-reload state: the list of stored keys."
  (let loop ((l hl--state) (acc '()))
    (if (null? l) (reverse acc) (loop (cddr l) (cons (car l) acc)))))

(define (hl-state-remove! k) "Remove KEY from the cross-reload state;
#t when it was there, #f if not."
  (let loop ((l hl--state) (acc '()))
    (cond ((null? l)
           (set! hl--state (reverse acc))
           #f)
          ((eq? (car l) k)
           (set! hl--state (append (reverse acc) (cddr l)))
           #t)
          (else (loop (cddr l)
                      (cons (cadr l) (cons (car l) acc)))))))


;; deletes the C++ weak ref behind a dead handle's cell. Allocation-free by
;; design: this runs inside the GC rendezvous, where consing would re-enter
;; the collector.
;; Handles are freed by Guile's finalizers, which must run on this thread
;; (deleting a handle releases a weak ref — compositor state), so automatic
;; finalization is off (Guile.cpp) and the collector's queue is pumped here
;; after each collection. Guarded: a failing pump must not kill the GC that
;; triggered it.
(add-hook! after-gc-hook
  (lambda ()
    (guard (e (#t (hl--report e)))
      (hl--c-run-finalizers))))

(set! hl--ready #t)
