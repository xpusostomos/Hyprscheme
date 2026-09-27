;; hyprscheme/core.scm — shared Scheme plumbing: argument coercion, the plist convention, key-spec parsing,
;; the cross-reload state accessors, and the listen helpers
;;
;; imports (hyprscheme kernel) [the C++ boundary] and nothing else in the tree.
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme core)
  #:export (
           hl--bind-flags hl--bool-act hl--dir hl--emacs-key
           hl--emacs-keys hl--emacs-mods hl--emacs-mouse hl--gesture-mode
           hl--mon-arg hl--monitor-listen hl--notif-act hl--plist-fold
           hl--plist-has? hl--plist-map hl--push-val hl--rule-kind
           hl--rule-name hl--rule-spec-value hl--special-name hl--str
           hl--window-listen hl--window-rule-mk hl--workspace-listen
           hl--ws-arg hl-after hl-box hl-box-h hl-box-rtd hl-box-w
           hl-box-x hl-box-y hl-current-submap hl-group? hl-kbd hl-key
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

(define (hl-box-x b) "The box's left edge." (hl--record-field 'hl-box-x hl-box-rtd 0 b))
(define (hl-box-y b) "The box's top edge." (hl--record-field 'hl-box-y hl-box-rtd 1 b))
(define (hl-box-w b) "The box's width." (hl--record-field 'hl-box-w hl-box-rtd 2 b))
(define (hl-box-h b) "The box's height." (hl--record-field 'hl-box-h hl-box-rtd 3 b))

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
    (hl--error 'hl-layout-add!
            "the callbacks must be named, e.g. (hl-layout-add! ~s
             'recalculate LAMBDA ...)"
            name))
  (if (= 0 (hl--c-layout-add name spec))
      name
      (hl--error 'hl-layout-add! "layout ~a rejected, see compositor log" name)))

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
  (if (and (> (string-length s) 8) (string=? (substring s 0 8) "special:"))
      (substring s 8 (string-length s))
      s))

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
  (if (= 0 act) #t (hl--error who "~a" (hl--c-config-last-error))))

(define (hl--gesture-mode name args allowed default)
  (cond ((null? args) default)
        ((and (= 1 (length args)) (memq (car args) allowed)) (car args))
        (else (hl--error name "mode must be one of ~a" allowed))))

(define (hl--window-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (hl--c-window-event-listen rec which))
        rec
        (hl--error who "listener rejected, see compositor log"))))

(define (hl--workspace-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (hl--c-workspace-event-listen rec which))
        rec
        (hl--error who "listener rejected, see compositor log"))))

(define (hl--monitor-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (hl--c-monitor-event-listen rec which))
        rec
        (hl--error who "listener rejected, see compositor log"))))

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

