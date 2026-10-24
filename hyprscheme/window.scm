;; hyprscheme/window.scm — the window family — raw wrappers over the compositor's window operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme window)
  #:export (
           hl-active-window hl-windows hl-window-title hl-window-alive?
           hl-window-close! hl-window-class hl-window-workspace
           hl-window-monitor hl-window-floating? hl-window-size
           hl-window-pid hl-window-focus! hl-window-float-set!
           hl-window-workspace-set! hl-window-move!
           hl-window-swap! hl-window-swap-next!
           hl-window-swap-with! hl-window-cycle! hl-window-center!
           hl-window-size-set! hl-window-position-set!
           hl-window-pinned-set! hl-window-pseudo-set! hl-window-kill!
           hl-window-signal! hl-window-zorder-set! hl-window-prop-set!
           hl-window-tag-add! hl-window-tags-clear!
           hl-window-swallow-toggle! hl-window-group-set!
           hl-window-group? hl-window-group hl-window-group-lock-set!
           hl-window-group-lock? hl-window-move-into-group!
           hl-window-move-out-of-group! hl-window-move-into-or-create-group!
           hl-window-deny-from-group-set! hl-window-pass-shortcut!
           hl-window-send-shortcut! hl-window-send-key-state!
           hl-window-rule-add! hl-window-from hl-urgent-window
           hl-last-window hl-windows-from hl-window-fullscreen-handler
           hl-window-fullscreen-set! hl-window-fullscreen-sync!
           hl-window-fullscreen
           hl-window-fullscreen-client
           hl-window-hidden? hl-window-address hl-window-mapped?
           hl-window-visible? hl-window-accepts-input? hl-window-position
           hl-window-pin-fullscreened? hl-window-allowed-over-fullscreen?
           hl-window-tearing-hint? hl-window-inhibiting-idle?
           hl-window-focus-history-id hl-window-content-type
           hl-window-stable-id hl-window-tags hl-window-tags-raw
           hl--tag-bare hl-window-swallowing
           hl-window-xdg-tag hl-window-xdg-description hl-window-layout
           hl-window-pinned? hl-window-pseudo? hl-window-maximized?
           hl-window-deny-from-group? hl-window-prop
           hl-window-initial-class hl-window-initial-title hl-window-x11?
           hl-window-open-notification-add!
           hl-window-close-notification-add!
           hl-window-title-notification-add!
           hl-window-class-notification-add!
           hl-window-urgent-notification-add!
           hl-window-pin-notification-add!
           hl-window-fullscreen-notification-add!
           hl-window-move-to-workspace-notification-add!
           hl-window-active-notification-add!
           hl-window-minimize-notification-add!
           hl-window-open-early-notification-add!
           hl-window-kill-notification-add!
           hl-window-bell-notification-add!
           hl-window-update-rules-notification-add!
           hl-window-destroy-notification-add! hl-window=?))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))
(use-modules (hyprscheme enums))
(use-modules (hyprscheme values))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

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

(define* (hl-window-move! #:optional w #:key (direction 'right))
  "Move WINDOW in DIRECTION, a direction-enum: 'up, 'right, 'down or 'left.
   WINDOW may be omitted or #f for the focused window.

   The direction is settled HERE: DIRECTION is an enum, so it can only be one of
   the four (enum-value refuses anything else, naming the set), and the gsubr is
   handed the compositor's own integer. Nothing downstream parses a string —
   which is what makes a bad direction fail at the call rather than reach the
   compositor, where an unrecognised one used to abort the session."
  (= 0 (hl--c-window-move-direction (if w w #f)
                                    (enum-value direction-enum direction))))

(define* (hl-window-swap! #:optional w #:key (direction 'right))
  "Swap WINDOW with the window in DIRECTION ('up, 'right, 'down or 'left).
   WINDOW may be omitted or #f for the focused window."
  (= 0 (hl--c-window-swap-direction (if w w #f)
                                    (enum-value direction-enum direction))))

(define* (hl-window-swap-next! w #:key (prev #f))
  "Swap WINDOW with the next window in its group; #:prev #t swaps
   backwards instead. The dispatcher it mirrors takes a boolean flag —
   the compositor's own lua is swap({prev = true}) — so the option names
   the flag it turns on."
  ;; The positional `'prev`/truthy form this used to take is gone: the option
  ;; IS the flag now, and one documented spelling is the rule (see the wiki's
  ;; dispatchers page). The trap it replaces was real in both directions — the
  ;; docstring said "'prev or #t" while the code took anything truthy, and a
  ;; "fix" that made 'prev the only backwards spelling silently removed #t.
  (= 0 (hl--c-window-swap-next w prev)))

(define (hl-window-swap-with! w other)
  "Swap WINDOW with OTHER."
  (= 0 (hl--c-window-swap-with w other)))

(define* (hl-window-cycle! #:key (prev #f) (tiled #f) (floating #f))
  "Cycle focus to the next window; #:prev #t cycles the other way.
   #:tiled #t or #:floating #t restricts the cycle to windows of that
   kind — they are exclusive, as click/drag are for hl-bind-add!."
  (when (and tiled floating)
    (error "hl-window-cycle!: #:tiled and #:floating are exclusive"))
  (= 0 (hl--c-window-cycle #f (not prev) (cond (tiled 1) (floating 2) (else 0)))))

(define (hl-window-center! w)
  "Center WINDOW."
  (= 0 (hl--c-window-center w)))

(define* (hl-window-size-set! w width height #:key (relative #f))
  "Resize WINDOW to WIDTH x HEIGHT pixels; #:relative #t makes them
   deltas to its current size."
  (= 0 (hl--c-window-resize-px w (exact->inexact width) (exact->inexact height)
         relative)))

(define* (hl-window-position-set! w x y #:key (relative #f))
  "Move WINDOW to (X, Y); #:relative #t makes the coordinates deltas
   from its current position."
  (= 0 (hl--c-window-move-px w (exact->inexact x) (exact->inexact y)
         relative)))

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
  (= 0 (hl--c-window-zorder w mode)))

(define (hl-window-prop-set! w prop val)
  "Set a dynamic window prop on WINDOW (e.g. 'opacity \"0.8\"). Read
   back with hl-window-prop."
  (= 0 (hl--c-window-set-prop w prop val)))

(define (hl-window-tag-add! w tag)
  "Change one of WINDOW's tags — the same operation as the `tag` WINDOW RULE
and the tag dispatcher, which is a TOGGLE unless TAG says otherwise:

  (hl-window-tag-add! w \"+code\")   ; set
  (hl-window-tag-add! w \"-code\")   ; unset
  (hl-window-tag-add! w \"code\")    ; toggle: a second call REMOVES it

\"ADD\" IS A MISLEADING NAME, and the bare spelling is the one that misleads:
TAG is the compositor's own string, so the +/- prefixes are part of the argument
rather than options, and a bare name flips the tag on every call. Pass \"+\"
when you mean set.

So a config may call this on every window open, but must not call it on every
event: setting the same tag twice is the same as setting it once, while
TOGGLING it twice is the same as never setting it at all.

These are STATIC tags, stored bare. A tag a window rule applies is DYNAMIC and
stored with a trailing \"*\" — see hl-window-tags-raw for why that mark exists,
and hl-window-tags for reading either spelling back as a plain name."
  (= 0 (hl--c-window-tag w tag)))

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
      (= 0 (hl--c-group-set w on?))))

(define (hl-window-group w)
  "The group W is in, as a handle, or #f when it is in none. W may be a window
   handle or #f for the focused window; hl-window-group? is the predicate."
  (hl--c-window-group w))

(define (hl-window-group? w)
  "#t when WINDOW is in a group."
  (= 1 (hl--c-window-in-group w)))

(define* (hl-window-group-lock-set! w #:key (on? 'unset))
  "Lock or unlock the group of window W (#f = the active window): absent
   #:on? toggles, #t/#f set. See hl-window-group-lock?."
  (= 0 (hl--c-window-group-lock w (hl--bool-act on?))))

(define (hl-window-group-lock? w)
  "#t when WINDOW's group is locked."
  (= 1 (hl--c-window-group-locked w)))

(define* (hl-window-move-into-group! #:optional w #:key (direction 'right))
  "Move WINDOW into the group in DIRECTION ('up, 'right, 'down or 'left).
   WINDOW may be omitted or #f for the focused window."
  (= 0 (hl--c-window-into-group (if w w #f)
                                (enum-value direction-enum direction))))

(define* (hl-window-move-out-of-group! #:optional w #:key (direction 'none))
  "Move WINDOW out of its group. WINDOW may be omitted or #f for the focused
   window.

   DIRECTION is a direction-or-none-enum and defaults to 'none, which releases
   the window from the group WITHOUT aiming it anywhere — the compositor's own
   directionless form, not a fallback: the value it stands for is the one
   CGroup::remove tests for, and the one moveWindowOutOfGroupHelper assumes by
   default. Give 'up, 'right, 'down or 'left to drop it towards that edge
   instead."
  (= 0 (hl--c-window-out-of-group (if w w #f)
                                  (enum-value direction-or-none-enum direction))))

(define* (hl-window-move-into-or-create-group! #:optional w #:key (direction 'right))
  "Move WINDOW into the group in DIRECTION ('up, 'right, 'down or 'left),
   creating a group when there is none. WINDOW may be omitted or #f for the
   focused window."
  (= 0 (hl--c-window-into-or-create-group (if w w #f)
                                          (enum-value direction-enum direction))))

(define* (hl-window-deny-from-group-set! w #:key (on? 'unset))
  "Set whether WINDOW denies being grouped (absent #:on? toggles)."
  (= 0 (hl--c-window-deny-from-group w (hl--bool-act on?))))

;; ---- actions: workspaces and monitors ---------------------------------------

(define (hl-window-pass-shortcut! w)
  "Pass the currently-pressed keybind through to WINDOW."
  (= 0 (hl--c-pass w)))

(define* (hl-window-send-shortcut! #:optional w #:key (keys '()))
  "Send KEYS to WINDOW as if pressed. WINDOW may be omitted or #f for the
   focused window.

   KEYS is one chord: the flat token list hl-kbd or hl-key returns, modifiers
   first and the key last — (hl-kbd \"C-l\"), (hl-key \"SUPER+F10\"), or a
   literal '(\"SUPER\" \"F10\"). It is split here (hl--chord-mods /
   hl--chord-key) into the mods mask and the keysym the compositor wants."
  (unless (and (list? keys) (pair? keys) (every string? keys))
    (error "hl-window-send-shortcut!: #:keys must be a chord — the token list from hl-kbd/hl-key, e.g. (hl-kbd \"C-l\"). Got:" keys))
  (= 0 (hl--c-send-shortcut (hl--chord-mods keys)
                            (hl--chord-key keys)
                            (if w w #f))))

(define* (hl-window-send-key-state! #:optional w #:key (keys '()) (state 'down))
  "Send KEYS to WINDOW as if pressed, with STATE saying which key state to
   synthesise: a key-state-enum — 'down (the default), 'up, or 'repeat.
   WINDOW may be omitted or #f for the focused window.

   'repeat is a second press rather than an autorepeat flag (the compositor
   runs pass() twice for it), which is how a synthetic autorepeat is spelled.

   KEYS is one chord, exactly as hl-window-send-shortcut! takes it."
  (unless (and (list? keys) (pair? keys) (every string? keys))
    (error "hl-window-send-key-state!: #:keys must be a chord — the token list from hl-kbd/hl-key, e.g. (hl-kbd \"C-l\"). Got:" keys))
  (= 0 (hl--c-send-key-state (hl--chord-mods keys)
                             (hl--chord-key keys)
                             (enum-value key-state-enum state)
                             (if w w #f))))

(define* (hl-window-rule-add! name #:key #:rest fields)
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

(define (hl-window-from sel)
  "The window matching SELECTOR, or #f when none. Selectors use the
   config selector syntax: \"class:^foot$\", \"title:foo\", \"pid:123\",
   \"address:0x...\", \"workspace:3\", \"floating\", \"tiled\", ..."
  (hl--c-window-from sel))

(define (hl-urgent-window )
  "The most urgent window, as a handle, or #f when none."
  (hl--c-urgent-window))

(define (hl-last-window )
  "The previously-focused window, as a handle, or #f."
  (hl--c-last-window))

(define (hl-windows-from sel)
  "All windows matching SELECTOR, as a list of window handles."
  (let ((s (hl--c-windows-from sel)))
    (if (not s)
        '()
        s)))

;; The fullscreen modes, as the compositor numbers them. 'keep is the WRITE-side
;; "leave this alone", so it appears in a setter's list of acceptable values but
;; never comes back from a getter.
(define-enum-values fullscreen-mode
  '((keep       . -1)
    (none       . 0)
    (maximized  . 1)
    (fullscreen . 2)))

(define (hl-window-fullscreen-handler w)
  "Which handler owns W's fullscreen, as a symbol: 'default, or a layout
   that takes fullscreen over for its windows ('scrolling), or 'unknown.
   W may be #f for the focused window; #f when there is no such window.

   The symbol comes from the compositor itself, which reports the handler
   by name — so the vocabulary is theirs and a new handler appears here
   without a change on our side."
  (hl--c-window-fullscreen-handler w))

;; ---- notifications ------------------------------------------------------------

(define* (hl-window-fullscreen-set! w #:key (mode 'fullscreen) (action 'toggle) (layout-aware #t))
  "Set W's fullscreen state. W may be #f for the focused window.

   #:mode is the mode to act on — 'fullscreen (the default) or
   'maximized — and #:action says how: 'toggle flips it (the default),
   'set makes it that mode, 'unset takes THAT mode off and leaves any
   other one alone (un-fullscreening a maximized window must not
   un-maximize it).

   READ, DECIDE, SET: the current mode is read once, the target follows
   from it, and the compositor is told to ASSIGN that. So every action is
   idempotent by construction — asking for the state the window is
   already in re-assigns the same value and changes nothing — and the
   compositor is never asked to re-derive a decision already made here.

   Everything here is in mode SYMBOLS; the one conversion to the
   compositor's integers happens at the gsubr call, which is what
   enum-value is for.

   The app IS told what happened: the compositor syncs the client side
   by default (the window property `sync_fullscreen`, on unless a rule
   or hl-window-prop-set! turns it off), so hl-window-fullscreen-client
   follows. To set the two sides independently use
   hl-window-fullscreen-sync!, whose 'keep leaves a side alone —
   that is how to fullscreen a window WITHOUT the app being told.

   #:layout-aware defaults to #t, the dispatcher's own default."
  (let* ((current (hl-window-fullscreen w))
         (target (case action
                   ;; each action's TARGET, from the mode just read, then
                   ;; assigned. CURRENT is a mode symbol like MODE, so the two
                   ;; compare with eq? — and 'unset keeps CURRENT rather than
                   ;; forcing 'none when the window is in some OTHER mode.
                   ((toggle) (if (eq? mode current) 'none mode))
                   ((set) mode)
                   ((unset)  (if (eq? mode current) 'none current))
                   (else (error "hl-window-fullscreen-set!: #:action must be 'toggle, 'set or 'unset, got" action)))))
    (= 0 (hl--c-window-fullscreen-set w (enum-value fullscreen-mode target)
                                     layout-aware))))

(define* (hl-window-fullscreen-sync! w internal client #:key (action 'set) (layout-aware #t))
  "Set BOTH sides of the fullscreen state at once: INTERNAL is what the
   compositor does, CLIENT is what the app is told, each a mode symbol
   from fullscreen-mode — 'none, 'maximized, 'fullscreen — or 'keep to
   leave that side alone. W may be #f for the focused window.

   This is the only way to move the two sides INDEPENDENTLY, and so the
   only way to fullscreen a window without telling the app (internal
   'fullscreen, client 'keep). hl-window-fullscreen-set! keeps them in
   step, because the compositor syncs the client side by default.

   #:action works on the PAIR, as the dispatcher's does, and defaults to
   'set:
     'set     assign the pair, unless it is already exactly that
     'unset   clear to no-fullscreen, when the pair IS what you named
     'toggle  flip EACH side separately — the internal one clears if it
              is already what you asked for, and likewise the client one
   #:layout-aware defaults to #t, the dispatcher's own default.

   'set and 'unset are the dispatcher's own guards, and they are what
   make this idempotent: the underlying action CLEARS a window whose
   pair already matches what it was asked for, so a bare 'set called
   twice would fullscreen and then unfullscreen."
  (let* ((want-i (enum-value fullscreen-mode internal))
         (want-c (enum-value fullscreen-mode client))
         (cur-i (hl--c-window-fullscreen-mode w))
         (cur-c (hl--c-window-fullscreen-client w))
         (none (enum-value fullscreen-mode 'none))
         (at (and (= cur-i want-i) (= cur-c want-c)))
         (assign (lambda (i c) (= 0 (hl--c-window-fullscreen-state w i c layout-aware)))))
    (case action
      ((set)    (if at #t (assign want-i want-c)))
      ((unset)  (if at (assign want-i want-c) #t))
      ((toggle) (assign (if (= cur-i want-i) none want-i)
                        (if (= cur-c want-c) none want-c)))
      (else (error "hl-window-fullscreen-sync!: #:action must be 'set, 'toggle or 'unset, got" action)))))

(define (hl-window-fullscreen w)
  "W's fullscreen mode, as a symbol: 'none, 'maximized or 'fullscreen.
   W may be #f for the focused window. #f when there is no such window."
  (let ((mode (hl--c-window-fullscreen-mode w)))
    (and (>= mode 0) (enum-symbol fullscreen-mode mode))))

(define (hl-window-fullscreen-client w)
  "The fullscreen mode the APP has been told W is in, as a symbol — the
   other side of hl-window-fullscreen. The compositor syncs this to the
   internal state by default, so the two usually agree; they part when
   `sync_fullscreen` is off for the window, or when
   hl-window-fullscreen-sync! set the sides independently (its 'keep is
   exactly how you leave the app's belief alone).
   W may be #f for the focused window; #f when there is no such window."
  (let ((mode (hl--c-window-fullscreen-client w)))
    (and (>= mode 0) (enum-symbol fullscreen-mode mode))))

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

;; TAGS COME IN TWO SPELLINGS, and the difference is a trailing "*".
;;
;; The compositor stores the string it was given, and marks a tag that a WINDOW
;; RULE applied by appending "*" (CTagKeeper::applyTag's `dynamic` flag). The
;; mark is not decoration: it is what ties the tag to the rule that applied it,
;; so the compositor can take it away again when the rule stops matching
;; (removeDynamicTag erases "name*" and nothing else). A tag set by hand with
;; hl-window-tag-add! has no owner and no mark.
;;
;; The two spellings are therefore both real, and they are what the raw list
;; holds. Everyone downstream then has to decide whether to care, and the tree
;; had been deciding differently in every place it read a tag — the API returned
;; the mark, a config's comparison did not want it, and the only reader in the
;; compositor that forgives it is the rule matcher. Hence two functions with the
;; split made explicit rather than left to each caller.
(define (hl-window-tags-raw w)
  "The window's tags EXACTLY as the compositor stores them, as a list of
   strings. A tag a window rule applied carries a trailing \"*\", the mark that
   says a rule put it there and a rule may take it away again; a tag set by hand
   with hl-window-tag-add! does not:

     (hl-window-tags-raw w)   ; -> (\"browser*\" \"pinned\")

   That mark is information, so it is not thrown away here: it is the only way
   to tell a tag the compositor will retire on its own from one only you can
   remove. When you just want a window's tags as names, use hl-window-tags."
  (hl--c-window-tags w))

(define (hl--tag-bare tag)
  "TAG without a trailing \"*\" — see hl-window-tags-raw for what the mark
means. ONE is stripped, which is all the compositor ever appends, and a tag
that genuinely ended in \"*\" could not be stored without colliding with the
mark anyway."
  (if (and (> (string-length tag) 0)
           (char=? (string-ref tag (- (string-length tag) 1)) #\*))
      (substring tag 0 (- (string-length tag) 1))
      tag))

(define (hl-window-tags w)
  "The window's tags as plain names, with the trailing \"*\" the compositor
   puts on a RULE-APPLIED tag stripped off, so a tag reads the same however it
   got there:

     (hl-window-tags w)   ; -> (\"browser\" \"pinned\")

   THIS is the list to compare against. A window rule is written
   `#:tag \"browser\"` and the config reads `\"browser\"`, but what the
   compositor stores is \"browser*\", so the raw list matches nothing you would
   ever write — comparing it against a name is a comparison that is silently
   always false. Reach for hl-window-tags-raw only when the ownership question
   is the one you are asking."
  (let ((tags (hl--c-window-tags w)))
    ;; `and`, not `or`: a window with no tags answers '(), while a handle the
    ;; compositor cannot resolve still answers #f. Both are what the raw call
    ;; answers, so the pair never disagree about the window itself.
    (and tags (map hl--tag-bare tags))))

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
  (hl--c-window-prop w prop))

(define (hl-window-initial-class w)
  "The class the window opened with (what static rules match on)."
  (hl--c-window-initial-class w))

(define (hl-window-initial-title w)
  "The title the window opened with (what static rules match on)."
  (hl--c-window-initial-title w))

(define (hl-window-x11? w)
  "#t when the window is an Xwayland window."
  (= 1 (hl--c-window-x11 w)))

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
   workspace. The handler receives the window handle AND the workspace
   it moved to — (lambda (w ws) ...) — as the compositor's own Lua
   does. Either may be #f."
  (hl--window-listen 7 'window-move-to-workspace handler 'hl-window-move-to-workspace-notification-add!))

(define (hl-window-active-notification-add! handler)
  "Register a handler that fires when the focused window changes. The
   handler receives the window handle, or #f when focus is CLEARED —
   when there is no window focused at all (the last one on the
   workspace closed, you switched to an empty one).

   The #f case is not decoration: it is the only way to learn that
   nothing is focused any more, so a handler that armed something for
   the window that just went away would otherwise never arm down. Check
   it before using the handle:

     (hl-window-active-notification-add!
       (lambda (w) (if w (arm-for w) (disarm-all))))"
  (hl--window-listen 8 'window-active handler 'hl-window-active-notification-add!))

(define (hl-window-minimize-notification-add! handler)
  "Register a handler that fires when a window is minimized or restored:
   (lambda (w state) ...) - state is #t when minimized."
  (let ((rec (make-hl-event 'window-minimize handler)))
    (if (= 0 (hl--c-window-minimize-listen rec))
        rec
        (error "hl-window-minimize-notification-add!: listener rejected, see compositor log"))))

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

(define (hl-window-destroy-notification-add! handler)
  "Register a handler that fires when a window is destroyed. The
   handler receives the window handle — (lambda (w) ...) — as the
   compositor's own Lua does.

   The handle is BORN DEAD: this fires from the window's teardown, so
   every getter on it answers #f and it identifies the window only by
   identity. For a last look at the window's data use the window-close
   notification, which fires while it is still alive."
  (let ((rec (make-hl-event 'window-destroy handler)))
    (if (= 0 (hl--c-window-destroy-listen rec))
        rec
        (error "hl-window-destroy-notification-add!: listener rejected, see compositor log"))))

(define (hl-window=? a b)
  "#t when A and B refer to the same live window."
  (= 1 (hl--c-window-same a b)))

