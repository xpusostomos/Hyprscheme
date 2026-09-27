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
           hl-window-workspace-set! hl-window-move-direction!
           hl-window-swap-direction! hl-window-swap-next!
           hl-window-swap-with! hl-window-cycle! hl-window-center!
           hl-window-size-set! hl-window-position-set!
           hl-window-pinned-set! hl-window-pseudo-set! hl-window-kill!
           hl-window-signal! hl-window-zorder-set! hl-window-prop-set!
           hl-window-tag-add! hl-window-tags-clear!
           hl-window-swallow-toggle! hl-window-group-set!
           hl-window-group? hl-window-group hl-window-group-lock-set!
           hl-window-group-lock? hl-window-group-move-in!
           hl-window-group-move-out! hl-window-group-move-in-or-create!
           hl-window-deny-from-group-set! hl-window-pass-shortcut!
           hl-window-send-shortcut! hl-window-send-key-state!
           hl-window-rule-add! hl-window-from hl-urgent-window
           hl-last-window hl-windows-from hl-window-fullscreen-handler
           hl-window-fullscreen-set! hl-window-maximized-set!
           hl-window-fullscreen-state hl-window-fullscreen-mode
           hl-window-hidden? hl-window-address hl-window-mapped?
           hl-window-visible? hl-window-accepts-input? hl-window-position
           hl-window-pin-fullscreened? hl-window-allowed-over-fullscreen?
           hl-window-tearing-hint? hl-window-inhibiting-idle?
           hl-window-focus-history-id hl-window-content-type
           hl-window-stable-id hl-window-tags hl-window-swallowing
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

(define (hl-window-destroy-notification-add! handler)
  "Register a handler that fires when a window is destroyed.
   Zero-argument callback (the bus event is a weak ref; window identity
   belongs to the window-close notification)."
  (let ((rec (make-hl-event 'window-destroy handler)))
    (if (= 0 (hl--c-window-destroy-listen rec))
        rec
        (hl--error 'hl-window-destroy-notification-add! "listener rejected, see compositor log"))))

(define (hl-window=? a b)
  "#t when A and B refer to the same live window."
  (= 1 (hl--c-window-same a b)))

