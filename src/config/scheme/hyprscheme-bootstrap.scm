;; hl-bind-add!: record first (locked + tagged C++-side), then the tokens
;; the bind is built from, flags, description, devices
(define c-hl-bind (foreign-procedure "hl-scheme-bind" (scheme-object scheme-object int string scheme-object) int))
(define c-hl-timer (foreign-procedure "hl-scheme-timer" (scheme-object int int) int))
(define c-hl-active-title (foreign-procedure "hl-scheme-active-title" () scheme-object))
(define c-hl-workspace-names (foreign-procedure "hl-scheme-workspace-names" () scheme-object))
(define c-hl-submap-listen (foreign-procedure "hl-scheme-submap-listen" (scheme-object) int))
(define c-hl-active-window-id (foreign-procedure "hl-scheme-active-window-id" () double))
(define c-hl-window-ids (foreign-procedure "hl-scheme-window-ids" () scheme-object))
(define c-hl-window-title (foreign-procedure "hl-scheme-window-title" (integer-64) scheme-object))
(define c-hl-window-alive (foreign-procedure "hl-scheme-window-alive" (integer-64) int))
(define c-hl-window-close (foreign-procedure "hl-scheme-window-close" (integer-64) int))
(define c-hl-window-class (foreign-procedure "hl-scheme-window-class" (integer-64) scheme-object))
(define c-hl-window-workspace-id (foreign-procedure "hl-scheme-window-workspace-id" (integer-64) scheme-object))
(define c-hl-window-monitor-id (foreign-procedure "hl-scheme-window-monitor-id" (integer-64) scheme-object))
(define c-hl-window-floating (foreign-procedure "hl-scheme-window-floating" (integer-64) int))
(define c-hl-window-size (foreign-procedure "hl-scheme-window-size" (integer-64) scheme-object))
(define c-hl-window-pid (foreign-procedure "hl-scheme-window-pid" (integer-64) int))
(define c-hl-window-focus (foreign-procedure "hl-scheme-window-focus" (integer-64) int))
(define c-hl-window-float (foreign-procedure "hl-scheme-window-float" (integer-64) int))
(define c-hl-window-move-to-workspace (foreign-procedure "hl-scheme-window-move-to-workspace" (integer-64 string) int))
(define c-hl-monitor-names (foreign-procedure "hl-scheme-monitor-names" () scheme-object))
(define c-hl-window-event-listen (foreign-procedure "hl-scheme-window-event-listen" (scheme-object int) int))
(define c-hl-window-minimize-listen (foreign-procedure "hl-scheme-window-minimize-listen" (scheme-object) int))
(define c-hl-lifecycle-listen (foreign-procedure "hl-scheme-lifecycle-listen" (scheme-object int) int))
(define c-hl-config-reloaded-listen (foreign-procedure "hl-scheme-config-reloaded-listen" (scheme-object) int))
(define c-hl-config-unload-listen (foreign-procedure "hl-scheme-config-unload-listen" (scheme-object) int))
(define c-hl-config-props-refreshed-listen (foreign-procedure "hl-scheme-config-props-refreshed-listen" (scheme-object) int))
(define c-hl-window-destroy-listen (foreign-procedure "hl-scheme-window-destroy-listen" (scheme-object) int))
(define c-hl-layer-listen (foreign-procedure "hl-scheme-layer-listen" (scheme-object int) int))
(define c-hl-screenshare-listen (foreign-procedure "hl-scheme-screenshare-listen" (scheme-object) int))
(define c-hl-keyboard-key-listen (foreign-procedure "hl-scheme-keyboard-key-listen" (scheme-object) int))
(define c-hl-unbind-rec (foreign-procedure "hl-scheme-unbind-rec" (scheme-object) int))
(define c-hl-unbind-key (foreign-procedure "hl-scheme-unbind-key" (string) int))
(define c-hl-event-cancel (foreign-procedure "hl-scheme-event-cancel" (scheme-object) int))
(define c-hl-event-active (foreign-procedure "hl-scheme-event-active" (scheme-object) int))
(define c-hl-window-same (foreign-procedure "hl-scheme-window-same" (integer-64 integer-64) int))
(define c-hl-current-submap (foreign-procedure "hl-scheme-current-submap" () scheme-object))
(define c-hl-cursor-pos (foreign-procedure "hl-scheme-cursor-pos" () scheme-object))
(define c-hl-workspace-active-listen (foreign-procedure "hl-scheme-workspace-active-listen" (scheme-object) int))
(define c-hl-monitor-event-listen (foreign-procedure "hl-scheme-monitor-event-listen" (scheme-object int) int))
(define c-hl-workspace-event-listen (foreign-procedure "hl-scheme-workspace-event-listen" (scheme-object int) int))
(define c-hl-workspace-change-id (foreign-procedure "hl-scheme-workspace-change-id" (string double) int))
(define c-hl-workspace-groups (foreign-procedure "hl-scheme-workspace-groups" (integer-64) scheme-object))
(define c-hl-group-members (foreign-procedure "hl-scheme-group-members" (integer-64) scheme-object))
(define c-hl-group-current (foreign-procedure "hl-scheme-group-current" (integer-64) scheme-object))
(define c-hl-group-current-idx (foreign-procedure "hl-scheme-group-current-idx" (integer-64) scheme-object))
(define c-hl-group-size (foreign-procedure "hl-scheme-group-size" (integer-64) scheme-object))
(define c-hl-group-locked (foreign-procedure "hl-scheme-group-locked" (integer-64) scheme-object))
(define c-hl-group-denied (foreign-procedure "hl-scheme-group-denied" (integer-64) scheme-object))
(define c-hl-group-alive (foreign-procedure "hl-scheme-group-alive" (integer-64) int))
(define c-hl-group-same (foreign-procedure "hl-scheme-group-same" (integer-64 integer-64) int))
(define c-hl-group-add (foreign-procedure "hl-scheme-group-add" (integer-64 integer-64 integer-64) int))
(define c-hl-group-remove (foreign-procedure "hl-scheme-group-remove" (integer-64 integer-64) int))
(define c-hl-layout-add (foreign-procedure "hl-scheme-layout-add" (string scheme-object) int))

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
    (errorf 'hl-layout-add!
            "the callbacks must be named, e.g. (hl-layout-add! ~s
             'recalculate LAMBDA ...)"
            name))
  (if (= 0 (c-hl-layout-add name spec))
      name
      (errorf 'hl-layout-add! "layout ~a rejected, see compositor log" name)))

(define c-hl-get-submap-ctx (foreign-procedure "hl-scheme-get-submap-ctx" () scheme-object))
(define c-hl-set-submap-ctx (foreign-procedure "hl-scheme-set-submap-ctx" (string string) void))
(define c-hl-enter-submap (foreign-procedure "hl-scheme-enter-submap" (string) int))

(define (hl-submap name fn . reset) "Run FN to define a submap: binds
registered inside FN are scoped to it, and the optional RESET names
the submap returned to on exit. The submap exists only if FN registers
at least one bind."
  (let* ((ctx        (c-hl-get-submap-ctx))
         (prev-name  (car ctx))
         (prev-reset (cdr ctx)))
    (dynamic-wind
      (lambda () (c-hl-set-submap-ctx name (if (null? reset) "" (car reset))))
      (lambda () (fn))
      (lambda () (c-hl-set-submap-ctx prev-name prev-reset)))))

(define (hl-submap-activate! name) "Switch the active submap to NAME
(\"\" or \"reset\" returns to the default map)."
  (= 0 (c-hl-enter-submap name)))

(define (hl-submap-exit! ) "Leave the active submap and return to the
default map."
  (= 0 (c-hl-enter-submap "")))
(define c-hl-window-fullscreen-toggle (foreign-procedure "hl-scheme-window-fullscreen-toggle" (integer-64 int) int))
(define c-hl-window-fullscreen-set (foreign-procedure "hl-scheme-window-fullscreen-set" (integer-64 int) int))
(define c-hl-window-fullscreen-mode (foreign-procedure "hl-scheme-window-fullscreen-mode" (integer-64) int))
(define c-hl-window-hidden (foreign-procedure "hl-scheme-window-hidden" (integer-64) int))
;; window read-side fields (LuaWindow parity)
(define c-hl-window-address (foreign-procedure "hl-scheme-window-address" (integer-64) scheme-object))
(define c-hl-window-mapped (foreign-procedure "hl-scheme-window-mapped" (integer-64) int))
(define c-hl-window-visible (foreign-procedure "hl-scheme-window-visible" (integer-64) int))
(define c-hl-window-accepts-input (foreign-procedure "hl-scheme-window-accepts-input" (integer-64) int))
(define c-hl-window-position (foreign-procedure "hl-scheme-window-position" (integer-64) scheme-object))
(define c-hl-window-pin-fullscreened (foreign-procedure "hl-scheme-window-pin-fullscreened" (integer-64) int))
(define c-hl-window-allowed-over-fullscreen (foreign-procedure "hl-scheme-window-allowed-over-fullscreen" (integer-64) int))
(define c-hl-window-tearing-hint (foreign-procedure "hl-scheme-window-tearing-hint" (integer-64) int))
(define c-hl-window-inhibiting-idle (foreign-procedure "hl-scheme-window-inhibiting-idle" (integer-64) int))
(define c-hl-window-focus-history-id (foreign-procedure "hl-scheme-window-focus-history-id" (integer-64) scheme-object))
(define c-hl-window-content-type (foreign-procedure "hl-scheme-window-content-type" (integer-64) scheme-object))
(define c-hl-window-stable-id (foreign-procedure "hl-scheme-window-stable-id" (integer-64) scheme-object))
(define c-hl-window-tags (foreign-procedure "hl-scheme-window-tags" (integer-64) scheme-object))
(define c-hl-window-swallowing-id (foreign-procedure "hl-scheme-window-swallowing-id" (integer-64) scheme-object))
(define c-hl-window-xdg-tag (foreign-procedure "hl-scheme-window-xdg-tag" (integer-64) scheme-object))
(define c-hl-window-xdg-description (foreign-procedure "hl-scheme-window-xdg-description" (integer-64) scheme-object))
(define c-hl-window-layout (foreign-procedure "hl-scheme-window-layout" (integer-64) scheme-object))
(define c-hl-window-pinned (foreign-procedure "hl-scheme-window-pinned" (integer-64) int))
(define c-hl-window-pseudo-query (foreign-procedure "hl-scheme-window-pseudo-query" (integer-64) int))
(define c-hl-window-maximized-query (foreign-procedure "hl-scheme-window-maximized-query" (integer-64) int))
(define c-hl-window-in-group (foreign-procedure "hl-scheme-window-in-group" (integer-64) int))
(define c-hl-window-group-denied (foreign-procedure "hl-scheme-window-group-denied" (integer-64) int))
(define c-hl-window-group-locked (foreign-procedure "hl-scheme-window-group-locked" (integer-64) int))
(define c-hl-groups-locked (foreign-procedure "hl-scheme-groups-locked" () int))
(define c-hl-window-group-lock (foreign-procedure "hl-scheme-window-group-lock" (integer-64 int) int))
(define c-hl-window-prop (foreign-procedure "hl-scheme-window-prop" (integer-64 string) scheme-object))
(define c-hl-window-initial-class (foreign-procedure "hl-scheme-window-initial-class" (integer-64) scheme-object))
(define c-hl-window-initial-title (foreign-procedure "hl-scheme-window-initial-title" (integer-64) scheme-object))

;; ---- actions: the dispatcher surface (window id -1 = active window) --------
(define c-hl-focus-workspace (foreign-procedure "hl-scheme-focus-workspace" (string) int))
(define c-hl-focus-direction (foreign-procedure "hl-scheme-focus-direction" (string) int))
(define c-hl-focus-monitor (foreign-procedure "hl-scheme-focus-monitor" (string) int))
(define c-hl-focus-last (foreign-procedure "hl-scheme-focus-last" () int))
(define c-hl-focus-urgent (foreign-procedure "hl-scheme-focus-urgent" () int))
(define c-hl-window-move-direction (foreign-procedure "hl-scheme-window-move-direction" (integer-64 string) int))
(define c-hl-window-swap-direction (foreign-procedure "hl-scheme-window-swap-direction" (integer-64 string) int))
(define c-hl-window-swap-next (foreign-procedure "hl-scheme-window-swap-next" (integer-64 int) int))
(define c-hl-window-swap-with (foreign-procedure "hl-scheme-window-swap-with" (integer-64 integer-64) int))
(define c-hl-window-float-act (foreign-procedure "hl-scheme-window-float-act" (integer-64 int) int))
(define c-hl-window-cycle (foreign-procedure "hl-scheme-window-cycle" (integer-64 int int) int))
(define c-hl-window-center (foreign-procedure "hl-scheme-window-center" (integer-64) int))
(define c-hl-window-resize-px (foreign-procedure "hl-scheme-window-resize-px" (integer-64 double double int) int))
(define c-hl-window-move-px (foreign-procedure "hl-scheme-window-move-px" (integer-64 double double int) int))
(define c-hl-window-pin-act (foreign-procedure "hl-scheme-window-pin-act" (integer-64 int) int))
(define c-hl-window-pseudo (foreign-procedure "hl-scheme-window-pseudo" (integer-64 int) int))
(define c-hl-window-kill (foreign-procedure "hl-scheme-window-kill" (integer-64) int))
(define c-hl-window-signal (foreign-procedure "hl-scheme-window-signal" (integer-64 int) int))
(define c-hl-window-zorder (foreign-procedure "hl-scheme-window-zorder" (integer-64 string) int))
(define c-hl-window-set-prop (foreign-procedure "hl-scheme-window-set-prop" (integer-64 string string) int))
(define c-hl-window-tag (foreign-procedure "hl-scheme-window-tag" (integer-64 string) int))
(define c-hl-window-clear-tags (foreign-procedure "hl-scheme-window-clear-tags" (integer-64) int))
(define c-hl-toggle-swallow (foreign-procedure "hl-scheme-toggle-swallow" () int))
(define c-hl-group-toggle (foreign-procedure "hl-scheme-group-toggle" (integer-64) int))
(define c-hl-group-set (foreign-procedure "hl-scheme-group-set" (integer-64 int) int))
(define c-hl-monitor-set-special (foreign-procedure "hl-scheme-monitor-set-special" (string string) int))
(define c-hl-group-cycle (foreign-procedure "hl-scheme-group-cycle" (integer-64 int) int))
(define c-hl-group-index (foreign-procedure "hl-scheme-group-index" (integer-64 int) int))
(define c-hl-group-move-window (foreign-procedure "hl-scheme-group-move-window" (integer-64 int) int))
(define c-hl-group-lock (foreign-procedure "hl-scheme-group-lock" (int) int))
(define c-hl-group-lock-active (foreign-procedure "hl-scheme-group-lock-active" (int) int))
(define c-hl-window-into-group (foreign-procedure "hl-scheme-window-into-group" (integer-64 string) int))
(define c-hl-window-out-of-group (foreign-procedure "hl-scheme-window-out-of-group" (integer-64 string) int))
(define c-hl-window-into-or-create-group (foreign-procedure "hl-scheme-window-into-or-create-group" (integer-64 string) int))
(define c-hl-window-deny-from-group (foreign-procedure "hl-scheme-window-deny-from-group" (integer-64 int) int))
(define c-hl-workspace-rename (foreign-procedure "hl-scheme-workspace-rename" (string string) int))
(define c-hl-workspace-move-monitor (foreign-procedure "hl-scheme-workspace-move-monitor" (string string) int))
(define c-hl-workspace-toggle-special (foreign-procedure "hl-scheme-workspace-toggle-special" (string) int))
(define c-hl-workspace-swap-monitors (foreign-procedure "hl-scheme-workspace-swap-monitors" (string string) int))
(define c-hl-cursor-move (foreign-procedure "hl-scheme-cursor-move" (double double) int))
(define c-hl-cursor-corner (foreign-procedure "hl-scheme-cursor-corner" (integer-64 int) int))
(define c-hl-exit (foreign-procedure "hl-scheme-exit" () int))
(define c-hl-reload-config (foreign-procedure "hl-scheme-reload-config" () int))
(define c-hl-force-renderer-reload (foreign-procedure "hl-scheme-force-renderer-reload" () int))
(define c-hl-dpms (foreign-procedure "hl-scheme-dpms" (int string) int))
(define c-hl-force-idle (foreign-procedure "hl-scheme-force-idle" (double) int))
(define c-hl-global (foreign-procedure "hl-scheme-global" (string) int))
(define c-hl-event (foreign-procedure "hl-scheme-event" (string) int))
(define c-hl-pass (foreign-procedure "hl-scheme-pass" (integer-64) int))
(define c-hl-send-shortcut (foreign-procedure "hl-scheme-send-shortcut" (scheme-object string integer-64) int))
(define c-hl-send-key-state (foreign-procedure "hl-scheme-send-key-state" (scheme-object string int integer-64) int))
(define c-hl-mouse (foreign-procedure "hl-scheme-mouse" (string) int))
(define c-hl-clear-crashed-lockscreen (foreign-procedure "hl-scheme-clear-crashed-lockscreen" () int))
(define c-hl-scheduled-prop-refresh-immediately (foreign-procedure "hl-scheme-scheduled-prop-refresh-immediately" () int))
(define c-hl-release-input-capture (foreign-procedure "hl-scheme-release-input-capture" () int))
(define c-hl-window-fullscreen-state (foreign-procedure "hl-scheme-window-fullscreen-state" (integer-64 int int int) int))
(define c-hl-layout-message (foreign-procedure "hl-scheme-layout-message" (string) int))
;; ---- config: set/get config options ----------------------------------------
(define c-hl-config-begin (foreign-procedure "hl-config-begin" () int))
(define c-hl-config-push-int (foreign-procedure "hl-config-push-int" (double) int))
(define c-hl-config-push-num (foreign-procedure "hl-config-push-num" (double) int))
(define c-hl-config-push-bool (foreign-procedure "hl-config-push-bool" (int) int))
(define c-hl-config-push-str (foreign-procedure "hl-config-push-str" (string) int))
(define c-hl-config-tbl-open (foreign-procedure "hl-config-tbl-open" (int) int))
(define c-hl-config-tbl-key (foreign-procedure "hl-config-tbl-key" (string) int))
(define c-hl-config-tbl-set-hash (foreign-procedure "hl-config-tbl-set-hash" () int))
(define c-hl-config-tbl-seti (foreign-procedure "hl-config-tbl-seti" (int) int))
(define c-hl-config-set (foreign-procedure "hl-config-set" (string) int))
(define c-hl-config-last-error (foreign-procedure "hl-config-last-error" () scheme-object))
(define c-hl-config-get (foreign-procedure "hl-config-get" (string) scheme-object))
(define c-hl-monitor-begin (foreign-procedure "hl-monitor-begin" (string) int))
(define c-hl-monitor-field-str (foreign-procedure "hl-monitor-field-str" (string string) int))
(define c-hl-monitor-field-num (foreign-procedure "hl-monitor-field-num" (string double) int))
(define c-hl-monitor-field-gap (foreign-procedure "hl-monitor-field-gap" (string) int))
(define c-hl-monitor-field-bool (foreign-procedure "hl-monitor-field-bool" (string int) int))
(define c-hl-monitor-commit (foreign-procedure "hl-monitor-commit" () int))
(define c-hl-curve-add (foreign-procedure "hl-curve-add" (string int double double double double) int))
(define c-hl-animation-set (foreign-procedure "hl-animation-set" (string int double string string) int))
(define c-hl-permission-add (foreign-procedure "hl-permission-add" (string string string) int))
(define c-hl-window-rule-begin (foreign-procedure "hl-window-rule-begin" (string int) int))
(define c-hl-layer-rule-begin (foreign-procedure "hl-layer-rule-begin" (string int) int))
(define c-hl-rule-match (foreign-procedure "hl-rule-match" (string string) int))
(define c-hl-window-rule-effect (foreign-procedure "hl-window-rule-effect" (string string) int))
(define c-hl-layer-rule-effect (foreign-procedure "hl-layer-rule-effect" (string string) int))
(define c-hl-window-rule-commit (foreign-procedure "hl-window-rule-commit" (scheme-object) int))
(define c-hl-layer-rule-commit (foreign-procedure "hl-layer-rule-commit" (scheme-object) int))
(define c-hl-rule-set-enabled (foreign-procedure "hl-rule-set-enabled" (scheme-object int) int))
(define c-hl-rule-enabled (foreign-procedure "hl-rule-enabled" (scheme-object) int))
(define c-hl-workspace-rule-begin (foreign-procedure "hl-workspace-rule-begin" (string int) int))
(define c-hl-workspace-rule-str (foreign-procedure "hl-workspace-rule-str" (string string) int))
(define c-hl-workspace-rule-num (foreign-procedure "hl-workspace-rule-num" (string double) int))
(define c-hl-workspace-rule-bool (foreign-procedure "hl-workspace-rule-bool" (string int) int))
(define c-hl-workspace-rule-gap (foreign-procedure "hl-workspace-rule-gap" (string) int))
(define c-hl-workspace-rule-layout-opt (foreign-procedure "hl-workspace-rule-layout-opt" (string string) int))
(define c-hl-workspace-rule-commit (foreign-procedure "hl-workspace-rule-commit" () int))
(define c-hl-window-from (foreign-procedure "hl-window-from" (string) double))
(define c-hl-urgent-window (foreign-procedure "hl-urgent-window" () double))
(define c-hl-last-window (foreign-procedure "hl-last-window" () double))
(define c-hl-monitor-from (foreign-procedure "hl-monitor-from" (string) scheme-object))
(define c-hl-monitor-at (foreign-procedure "hl-monitor-at" (double double) scheme-object))
(define c-hl-monitor-at-cursor (foreign-procedure "hl-monitor-at-cursor" () scheme-object))
(define c-hl-active-monitor (foreign-procedure "hl-active-monitor" () scheme-object))
(define c-hl-active-workspace (foreign-procedure "hl-active-workspace" () scheme-object))
(define c-hl-active-special-workspace (foreign-procedure "hl-active-special-workspace" () scheme-object))
(define c-hl-last-workspace (foreign-procedure "hl-last-workspace" () scheme-object))
(define c-hl-workspace-windows (foreign-procedure "hl-workspace-windows" (string) scheme-object))
(define c-hl-layers (foreign-procedure "hl-layers" (integer-64 scheme-object) scheme-object))
(define c-hl-layer-alive (foreign-procedure "hl-layer-alive" (integer-64) int))
(define c-hl-layer-same (foreign-procedure "hl-layer-same" (integer-64 integer-64) int))
(define c-hl-layer-address (foreign-procedure "hl-layer-address" (integer-64) scheme-object))
(define c-hl-layer-pid (foreign-procedure "hl-layer-pid" (integer-64) scheme-object))
(define c-hl-layer-monitor (foreign-procedure "hl-layer-monitor" (integer-64) scheme-object))
(define c-hl-layer-namespace (foreign-procedure "hl-layer-namespace" (integer-64) scheme-object))
(define c-hl-layer-level (foreign-procedure "hl-layer-level" (integer-64) scheme-object))
(define c-hl-layer-mapped (foreign-procedure "hl-layer-mapped" (integer-64) scheme-object))
(define c-hl-layer-kb-interactivity (foreign-procedure "hl-layer-kb-interactivity" (integer-64) scheme-object))
(define c-hl-layer-above-fs (foreign-procedure "hl-layer-above-fs" (integer-64) scheme-object))
(define c-hl-layer-position (foreign-procedure "hl-layer-position" (integer-64) scheme-object))
(define c-hl-layer-size (foreign-procedure "hl-layer-size" (integer-64) scheme-object))
(define c-hl-is-key-down (foreign-procedure "hl-is-key-down" (string) int))
(define c-hl-loaded-plugins (foreign-procedure "hl-loaded-plugins" () scheme-object))
(define c-hl-version (foreign-procedure "hl-version" () scheme-object))
(define c-hl-windows-from (foreign-procedure "hl-windows-from" (string) scheme-object))
(define c-hl-window-fullscreen-handler (foreign-procedure "hl-window-fullscreen-handler" (integer-64) scheme-object))
(define c-hl-notify (foreign-procedure "hl-notify!" (string double string string double) scheme-object))
(define c-hl-timer-set-enabled (foreign-procedure "hl-timer-set-enabled" (scheme-object int) int))
(define c-hl-timer-enabled (foreign-procedure "hl-timer-enabled" (scheme-object) int))
(define c-hl-timer-set-timeout (foreign-procedure "hl-timer-set-timeout" (scheme-object double) int))
(define c-hl-timer-cancel (foreign-procedure "hl-timer-cancel" (scheme-object) int))
(define c-hl-exec! (foreign-procedure "hl-exec!" (string scheme-object) int))
;; one maker address per constructor, fetched from its own one-line C
;; accessor — no name strings, no dispatch
(define c-hl-gesture-maker-workspace-swipe (foreign-procedure "hl-scheme-gesture-maker-workspace-swipe" () integer-64))
(define c-hl-gesture-maker-move (foreign-procedure "hl-scheme-gesture-maker-move" () integer-64))
(define c-hl-gesture-maker-resize (foreign-procedure "hl-scheme-gesture-maker-resize" () integer-64))
(define c-hl-gesture-maker-close (foreign-procedure "hl-scheme-gesture-maker-close" () integer-64))
(define c-hl-gesture-maker-scroll-move (foreign-procedure "hl-scheme-gesture-maker-scroll-move" () integer-64))
(define c-hl-gesture-maker-float (foreign-procedure "hl-scheme-gesture-maker-float" () integer-64))
(define c-hl-gesture-maker-fullscreen (foreign-procedure "hl-scheme-gesture-maker-fullscreen" () integer-64))
(define c-hl-gesture-maker-special (foreign-procedure "hl-scheme-gesture-maker-special" () integer-64))
(define c-hl-gesture-maker-cursor-zoom (foreign-procedure "hl-scheme-gesture-maker-cursor-zoom" () integer-64))
(define c-hl-gesture-maker-custom (foreign-procedure "hl-scheme-gesture-maker-custom" () integer-64))
(define c-hl-gesture (foreign-procedure "hl-scheme-gesture" (scheme-object int string scheme-object double int) int))
(define c-hl-gesture-remove (foreign-procedure "hl-scheme-gesture-remove" (int string scheme-object double int) int))

;; helpers for the action wrappers: window #f = active; actions 'toggle/'on/'off;
;; directions "l"/"r"/"u"/"d" or the symbols left/right/up/down
(define (hl--wid w)
  (if w (hl-window-id w) -1))

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
(define c-hl-window-x11 (foreign-procedure "hl-scheme-window-x11" (integer-64) int))

;; flat option list -> assoc list: ('release #t 'description "x")
;; ---- plist helpers ----------------------------------------------------------
;; The API's one convention for named fields: a flat "keyword" list
;;   'release #t 'description "x"
;; — the same shape Guile uses for #:keyword arguments. A MISSING value
;; reports the DEFAULT; pass (eof-object) when absence must be told apart from an
;; explicit #f (an option that was given the value #f).

(define (hl--plist-cdr l)
  ;; (key value rest ...) → (value rest ...); a trailing bare KEY is an error
  (let ((tail (cdr l)))
    (if (null? tail)
        (errorf 'hl--plist "odd plist: ~s" l)
        tail)))

(define (hl-plist-get plist key . default) "Read KEY out of PLIST (the
plist convention used across the API: alternating key symbols and
values — gesture events, config tables). A missing key reports
DEFAULT; pass (eof-object) to tell an absent key apart from a stored
#f."
  (apply hl--plist-get plist key default))

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
    (errorf 'hl-bind-add! "click and drag are exclusive"))
  (when (and (or long-press release) repeat)
    (errorf 'hl-bind-add! "long-press / release is incompatible with repeat"))
  (when (and mouse (or repeat locked release))
    (errorf 'hl-bind-add! "mouse is exclusive with repeat/locked/release"))
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
         (rc (c-hl-bind rec
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
          (errorf 'hl-bind-add! "~a" (c-hl-config-last-error)))))
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
    (let ((dash (hl--string-index str #\-)))
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
  (map hl--trim (hl--split-string spec #\+)))

(define (hl-after ms thunk) "Run THUNK once after MS milliseconds.
Returns an hl-timer handle for later control
(hl-timer-enabled-set!, hl-timer-set-timeout, hl-timer-cancel!); a
one-shot releases its own record lock when it completes."
  (let ((rec (make-hl-timer ms thunk)))
    (if (= 0 (c-hl-timer rec (exact ms) 0))
        rec
        (errorf 'hl-after "timer ~ams rejected, see compositor log" ms))))

(define (hl-repeat ms thunk) "Run THUNK every MS milliseconds until
cancelled with (hl-timer-cancel!) or the config reloads. Returns an
hl-timer handle like hl-after's."
  (let ((rec (make-hl-timer ms thunk)))
    (if (= 0 (c-hl-timer rec (exact ms) 1))
        rec
        (errorf 'hl-repeat "timer ~ams rejected, see compositor log" ms))))

(define (hl-active-title ) "Title of the focused window, or #f when no
window has focus."
  (c-hl-active-title))

(define (hl-workspaces )
  "All workspaces, as a list of workspace handles."
  (let ((ids (c-hl-workspace-names)))
    (if (eq? ids #f)
        '()
        (map hl--mint-workspace ids))))



;; handles are opaque records whose single field is a guardian CELL
;; ((address . 0) — see the mint helpers and the C++ IHandle comment): the
;; address is a heap weak ref C++-side, and the record's death deletes it
;; through the guardian. Stale handles simply report #f from every getter;
;; the address is the only thing crossing FFI.
(define-record-type hl-window (fields cell))
(define-record-type hl-workspace (fields cell))
(define-record-type hl-monitor (fields cell))

(define (hl-window-id w)
  "The handle's internal id (rarely needed)." (car (hl-window-cell w)))
(define (hl-workspace-id w)
  "The handle's internal id (rarely needed)." (car (hl-workspace-cell w)))
(define (hl-monitor-id w)
  "The handle's internal id (rarely needed)." (car (hl-monitor-cell w)))

;; ---- handle lifetime ---------------------------------------------------------
;; mint = record + guardian cell; the cell dies with the record, the guardian
;; yields it, and the collect-request-handler below frees the C++ weak ref.
;; (Upstream: userdata embed the weak ref and the Lua GC __gc destructs it.)
(define hl--handle-guardian (make-guardian))

(define (hl--mint-window v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-window "implausible handle value ~s" v)))
      (r (make-hl-window cell)))
    (hl--handle-guardian cell)
    r))

(define (hl--mint-workspace v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-workspace "implausible handle value ~s" v)))
      (r (make-hl-workspace cell)))
    (hl--handle-guardian cell)
    r))

(define (hl--mint-monitor v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-monitor "implausible handle value ~s" v)))
      (r (make-hl-monitor cell)))
    (hl--handle-guardian cell)
    r))

;; notifications use the same handle model; the per-handle paused state
;; lives in the C++ handle object (see SNotificationHandle)
(define-record-type hl-notification (fields cell))
(define (hl-notification-id n)
  "The handle's internal id (rarely needed)." (car (hl-notification-cell n)))

(define (hl--mint-notification v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-notification "implausible handle value ~s" v)))
      (r (make-hl-notification cell)))
    (hl--handle-guardian cell)
    r))

;; events: the record IS the handle — the event type (a full symbol like
;; 'window-open) and the handler thunk. C++ locks the record inside the bus
;; connection (SThunkRef); when the connection is torn down at reload or
;; unload, the record unlocks and the handler becomes collectable.
(define-record-type hl-event (fields type thunk))

;; timers: the record IS the handle — the initial interval and the thunk.
;; C++ locks it inside the timer's fire callback; a one-shot timer releases
;; its own lock when it completes, and reload tears the rest down.
(define-record-type hl-timer (fields interval thunk))

;; binds: the record IS the handle — the token LIST (the bind's meaning)
;; and the thunk. C++ locks the record while the bind is registered (see
;; SThunkRef) and stamps its pinned address into the bind's argument tag;
;; the record becomes collectable when the bind is unbound (or dies at
;; reload) and the user drops it. No bind ids, no registries.
(define-record-type hl-bind (fields tokens thunk))

(define c-hl-workspace-name (foreign-procedure "hl-workspace-name" (integer-64) scheme-object))
(define c-hl-workspace-addressable-name (foreign-procedure "hl-workspace-addressable-name" (integer-64) scheme-object))
(define c-hl-workspace-number (foreign-procedure "hl-workspace-number" (integer-64) scheme-object))
(define c-hl-workspace-monitor (foreign-procedure "hl-workspace-monitor" (integer-64) scheme-object))
(define c-hl-workspace-special (foreign-procedure "hl-workspace-special" (integer-64) scheme-object))
(define c-hl-workspace-active (foreign-procedure "hl-workspace-active" (integer-64) scheme-object))
(define c-hl-workspace-visible (foreign-procedure "hl-workspace-visible" (integer-64) scheme-object))
(define c-hl-workspace-empty (foreign-procedure "hl-workspace-empty" (integer-64) scheme-object))
(define c-hl-workspace-persistent (foreign-procedure "hl-workspace-persistent" (integer-64) scheme-object))
(define c-hl-workspace-has-urgent (foreign-procedure "hl-workspace-has-urgent" (integer-64) scheme-object))
(define c-hl-workspace-has-fullscreen (foreign-procedure "hl-workspace-has-fullscreen" (integer-64) scheme-object))
(define c-hl-workspace-fullscreen-mode (foreign-procedure "hl-workspace-fullscreen-mode" (integer-64) scheme-object))
(define c-hl-workspace-fullscreen-window (foreign-procedure "hl-workspace-fullscreen-window" (integer-64) scheme-object))
(define c-hl-workspace-last-window (foreign-procedure "hl-workspace-last-window" (integer-64) scheme-object))
(define c-hl-workspace-window-count (foreign-procedure "hl-workspace-window-count" (integer-64) scheme-object))
(define c-hl-workspace-group-count (foreign-procedure "hl-workspace-group-count" (integer-64) scheme-object))
(define c-hl-workspace-tiled-layout (foreign-procedure "hl-workspace-tiled-layout" (integer-64) scheme-object))
(define c-hl-workspace-alive (foreign-procedure "hl-workspace-alive" (integer-64) scheme-object))
(define c-hl-workspace-same (foreign-procedure "hl-workspace-same" (integer-64 integer-64) scheme-object))
(define c-hl-workspace-selector (foreign-procedure "hl-workspace-selector" (integer-64) scheme-object))

(define c-hl-monitor-name (foreign-procedure "hl-monitor-name" (integer-64) scheme-object))
(define c-hl-monitor-description (foreign-procedure "hl-monitor-description" (integer-64) scheme-object))
(define c-hl-monitor-number (foreign-procedure "hl-monitor-number" (integer-64) scheme-object))
(define c-hl-monitor-enabled (foreign-procedure "hl-monitor-enabled" (integer-64) scheme-object))
(define c-hl-monitor-focused (foreign-procedure "hl-monitor-focused" (integer-64) scheme-object))
(define c-hl-monitor-x (foreign-procedure "hl-monitor-x" (integer-64) scheme-object))
(define c-hl-monitor-y (foreign-procedure "hl-monitor-y" (integer-64) scheme-object))
(define c-hl-monitor-width (foreign-procedure "hl-monitor-width" (integer-64) scheme-object))
(define c-hl-monitor-height (foreign-procedure "hl-monitor-height" (integer-64) scheme-object))
(define c-hl-monitor-scale (foreign-procedure "hl-monitor-scale" (integer-64) scheme-object))
(define c-hl-monitor-transform (foreign-procedure "hl-monitor-transform" (integer-64) scheme-object))
(define c-hl-monitor-refresh-rate (foreign-procedure "hl-monitor-refresh-rate" (integer-64) scheme-object))
(define c-hl-monitor-mode (foreign-procedure "hl-monitor-mode" (integer-64) scheme-object))
(define c-hl-monitor-dpms (foreign-procedure "hl-monitor-dpms" (integer-64) scheme-object))
(define c-hl-monitor-vrr (foreign-procedure "hl-monitor-vrr" (integer-64) scheme-object))
(define c-hl-monitor-10bit (foreign-procedure "hl-monitor-10bit" (integer-64) scheme-object))
(define c-hl-monitor-reserved (foreign-procedure "hl-monitor-reserved" (integer-64) scheme-object))
(define c-hl-monitor-serial (foreign-procedure "hl-monitor-serial" (integer-64) scheme-object))
(define c-hl-monitor-physical-size (foreign-procedure "hl-monitor-physical-size" (integer-64) scheme-object))
(define c-hl-monitor-mirrors (foreign-procedure "hl-monitor-mirrors" (integer-64) scheme-object))
(define c-hl-monitor-available-modes (foreign-procedure "hl-monitor-available-modes" (integer-64) scheme-object))
(define c-hl-monitor-hardware-details (foreign-procedure "hl-monitor-hardware-details" (integer-64) scheme-object))
(define c-hl-monitor-mirror-of (foreign-procedure "hl-monitor-mirror-of" (integer-64) scheme-object))
(define c-hl-monitor-active-workspace (foreign-procedure "hl-monitor-active-workspace" (integer-64) scheme-object))
(define c-hl-monitor-active-special-workspace (foreign-procedure "hl-monitor-active-special-workspace" (integer-64) scheme-object))
(define c-hl-monitor-alive (foreign-procedure "hl-monitor-alive" (integer-64) scheme-object))
(define c-hl-monitor-same (foreign-procedure "hl-monitor-same" (integer-64 integer-64) scheme-object))
(define c-hl-monitor-selector (foreign-procedure "hl-monitor-selector" (integer-64) scheme-object))

(define (hl-active-window )
  "The focused window, or #f when no window has focus."
  (let ((id (c-hl-active-window-id)))
    (if (< id 0) #f (hl--mint-window id))))

(define (hl-windows )
  "All windows, as a list of window handles."
  (let ((ids (c-hl-window-ids)))
    (if (eq? ids #f)
        '()
        (map hl--mint-window ids))))

(define (hl-window-title w)
  "The window's title."
  (c-hl-window-title (hl-window-id w)))

(define (hl-window-alive? w)
  "#t when the window still exists."
  (= 1 (c-hl-window-alive (hl-window-id w))))

(define (hl-window-close! w)
  "Close the window."
  (= 0 (c-hl-window-close (hl-window-id w))))

(define (hl-window-class w)
  "The window's class."
  (c-hl-window-class (hl-window-id w)))

(define (hl-window-workspace w)
  "The workspace the window is on, as a handle, or #f."
  (let ((id (c-hl-window-workspace-id (hl-window-id w))))
    (and id (hl--mint-workspace id))))

(define (hl-window-monitor w)
  "The monitor the window is on, as a handle, or #f."
  (let ((id (c-hl-window-monitor-id (hl-window-id w))))
    (and id (hl--mint-monitor id))))

(define (hl-window-floating? w)
  "#t when the window is floating."
  (= 1 (c-hl-window-floating (hl-window-id w))))

(define (hl-window-size w)
  "The window's size, as (width . height), or #f when stale."
  (c-hl-window-size (hl-window-id w)))   ; (width . height), or #f

(define (hl-window-pid w)
  "The window's owning process id."
  (c-hl-window-pid (hl-window-id w)))

(define (hl-window-focus! w)
  "Focus the window."
  (= 0 (c-hl-window-focus (hl-window-id w))))

(define* (hl-window-float-set! w #:key (on? 'unset))
  "Float or unfloat WINDOW: absent #:on? toggles, #t floats, #f tiles."
  (= 0 (c-hl-window-float-act (hl--wid w) (hl--bool-act on?))))

(define (hl-window-workspace-set! w ws)
  "Move the window to workspace WS."
  (= 0 (c-hl-window-move-to-workspace (hl--wid w) (hl--ws-arg ws))))

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
  (= 0 (c-hl-focus-workspace (hl--ws-arg ws))))

(define (hl-focus-direction-set! dir)
  "Move the focus in direction DIR (left/right/up/down)."
  (= 0 (c-hl-focus-direction (hl--dir dir))))

(define (hl-monitor-focus! mon)
  "Move the focus to monitor MON."
  (= 0 (c-hl-focus-monitor (hl--mon-arg mon))))

(define (hl-focus-last! )
  "Move the focus to the previously-focused window."
  (= 0 (c-hl-focus-last)))

(define (hl-focus-urgent! )
  "Move the focus to an urgent window, or the last one."
  (= 0 (c-hl-focus-urgent)))

(define (hl-window-move-direction! w dir)
  "Move WINDOW in direction DIR."
  (= 0 (c-hl-window-move-direction (hl--wid w) (hl--dir dir))))

(define (hl-window-swap-direction! w dir)
  "Swap WINDOW with the window in direction DIR."
  (= 0 (c-hl-window-swap-direction (hl--wid w) (hl--dir dir))))

(define (hl-window-swap-next! w . opt)
  "Swap WINDOW with the next window in its group; 'prev or #t swaps
   backwards."
  (= 0 (c-hl-window-swap-next (hl--wid w) (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 (if (car opt) 1 0))))))

(define (hl-window-swap-with! w other)
  "Swap WINDOW with OTHER."
  (= 0 (c-hl-window-swap-with (hl--wid w) (hl-window-id other))))

(define (hl-window-cycle! . opt)
  "Cycle focus to the next window. Options: 'prev (backwards), 'tiled,
   'floating (combinable symbols)."
  (let loop ((rest opt) (next 1) (filter 0))
    (cond ((null? rest)
           (= 0 (c-hl-window-cycle -1 next filter)))
          ((eq? (car rest) 'prev) (loop (cdr rest) 0 filter))
          ((eq? (car rest) 'tiled) (loop (cdr rest) next 1))
          ((eq? (car rest) 'floating) (loop (cdr rest) next 2))
          (else (loop (cdr rest) next filter)))))

(define (hl-window-center! w)
  "Center WINDOW."
  (= 0 (c-hl-window-center (hl--wid w))))

(define (hl-window-size-set! w width height . opt)
  "Resize WINDOW to WIDTH x HEIGHT pixels; 'relative (or 'rel) makes
   them deltas."
  (= 0 (c-hl-window-resize-px (hl--wid w) (exact->inexact width) (exact->inexact height)
         (if (null? opt) 0 (if (memq (car opt) '(relative rel)) 1 0)))))

(define (hl-window-position-set! w x y . opt)
  "Move WINDOW to (X, Y); 'relative (or 'rel) makes the coordinates
   deltas."
  (= 0 (c-hl-window-move-px (hl--wid w) (exact->inexact x) (exact->inexact y)
         (if (null? opt) 0 (if (memq (car opt) '(relative rel)) 1 0)))))

(define* (hl-window-pinned-set! w #:key (on? 'unset))
  "Pin or unpin WINDOW: absent #:on? toggles, #t/#f set."
  (= 0 (c-hl-window-pin-act (hl--wid w) (hl--bool-act on?))))

(define* (hl-window-pseudo-set! w #:key (on? 'unset))
  "Toggle or set pseudo-tile on WINDOW (absent #:on? toggles)."
  (= 0 (c-hl-window-pseudo (hl--wid w) (hl--bool-act on?))))

(define (hl-window-kill! w)
  "Forcefully kill WINDOW."
  (= 0 (c-hl-window-kill (hl--wid w))))

(define (hl-window-signal! w sig)
  "Send WINDOW's process the signal SIG."
  (= 0 (c-hl-window-signal (hl--wid w) sig)))

(define (hl-window-zorder-set! w mode)
  "Restack WINDOW per MODE: \"up\", \"down\", \"top\" or \"bottom\"."
  (= 0 (c-hl-window-zorder (hl--wid w) (hl--str mode))))

(define (hl-window-prop-set! w prop val)
  "Set a dynamic window prop on WINDOW (e.g. 'opacity \"0.8\"). Read
   back with hl-window-prop."
  (= 0 (c-hl-window-set-prop (hl--wid w) (hl--str prop) (hl--str val))))

(define (hl-window-tag-add! w tag)
  "Add the static TAG string to WINDOW's tags."
  (= 0 (c-hl-window-tag (hl--wid w) (hl--str tag))))

(define (hl-window-tags-clear! w)
  "Clear WINDOW's tags."
  (= 0 (c-hl-window-clear-tags (hl--wid w))))

(define (hl-window-swallow-toggle! )
  "Toggle swallowing for the active window."
  (= 0 (c-hl-toggle-swallow)))

;; ---- actions: groups --------------------------------------------------------

(define* (hl-window-group-set! w #:key (on? 'unset))
  "Put WINDOW into a group or take it out: absent #:on? uses the dedicated
   membership toggle, #t/#f set explicitly. W may be #f for the active
   window. See hl-window-group?."
  (if (eq? on? 'unset)
      (= 0 (c-hl-group-toggle (hl--wid w)))
      (= 0 (c-hl-group-set (hl--wid w) (if on? 1 0)))))

(define (hl-window-group? w)
  "#t when WINDOW is in a group."
  (= 1 (c-hl-window-in-group (hl-window-id w))))

(define (hl-group-cycle! w . opt)
  "Switch to the next window in WINDOW's group; 'prev goes backwards."
  (= 0 (c-hl-group-cycle (hl--wid w) (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

(define (hl-group-window-active! w index)
  "Switch to the member at 1-based INDEX in WINDOW's group."
  (= 0 (c-hl-group-index (hl--wid w) index)))

(define (hl-group-window-move-next! w . opt)
  "Move WINDOW within its group; 'prev moves backwards."
  (= 0 (c-hl-group-move-window (hl--wid w) (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

(define* (hl-groups-lock-set! #:key (on? 'unset))
  "Lock or unlock ALL groups compositor-wide (no window involved). See
   hl-groups-locked?."
  (= 0 (c-hl-group-lock (hl--bool-act on?))))

(define (hl-groups-locked? )
  "#t when the compositor-wide group lock is active."
  (= 1 (c-hl-groups-locked)))

(define* (hl-window-group-lock-set! w #:key (on? 'unset))
  "Lock or unlock the group of window W (#f = the active window): absent
   #:on? toggles, #t/#f set. See hl-window-group-lock?."
  (= 0 (c-hl-window-group-lock (hl--wid w) (hl--bool-act on?))))

(define (hl-window-group-lock? w)
  "#t when WINDOW's group is locked."
  (= 1 (c-hl-window-group-locked (hl-window-id w))))

(define (hl-window-group-move-in! w dir)
  "Move WINDOW into the group in direction DIR."
  (= 0 (c-hl-window-into-group (hl--wid w) (hl--dir dir))))

(define (hl-window-group-move-out! w dir)
  "Move WINDOW out of its group in direction DIR."
  (= 0 (c-hl-window-out-of-group (hl--wid w) (hl--dir dir))))

(define (hl-window-group-move-in-or-create! w dir)
  "Move WINDOW into the group in direction DIR, creating one when there
   is none."
  (= 0 (c-hl-window-into-or-create-group (hl--wid w) (hl--dir dir))))

(define* (hl-window-deny-from-group-set! w #:key (on? 'unset))
  "Set whether WINDOW denies being grouped (absent #:on? toggles)."
  (= 0 (c-hl-window-deny-from-group (hl--wid w) (hl--bool-act on?))))

;; ---- actions: workspaces and monitors ---------------------------------------

(define (hl-workspace-name-set! old new)
  "Rename the workspace OLD to NEW."
  (= 0 (c-hl-workspace-rename (hl--ws-arg old) (hl--str new))))

(define (hl-workspace-monitor-set! ws mon)
  "Move the workspace WS to monitor MON."
  (= 0 (c-hl-workspace-move-monitor (hl--ws-arg ws) (hl--mon-arg mon))))

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
      (= 0 (c-hl-workspace-toggle-special (hl--ws-arg ws)))
      (let* ((mon (hl-active-monitor))
             (active (and mon (hl-monitor-active-special-workspace mon)))
             (cur (and active (hl--special-name (hl-workspace-name active))))
             (target (hl--special-name (hl--ws-arg ws))))
        (if (eqv? on? (and cur (string=? cur target)))
            0
            (= 0 (c-hl-workspace-toggle-special (hl--ws-arg ws)))))))

(define (hl-monitor-workspace-special-set! mon ws)
  "Open the special workspace WS on MON (created when missing); #f
   closes whatever special workspace is open there. WS must be named - a
   closed monitor keeps no record of its last special workspace."
  (= 0 (c-hl-monitor-set-special (hl--mon-arg mon) (if ws (hl--ws-arg ws) ""))))

(define (hl-monitor-swap! mon1 mon2)
  "Swap the current workspaces of monitors MON1 and MON2."
  (= 0 (c-hl-workspace-swap-monitors (hl--mon-arg mon1) (hl--mon-arg mon2))))

(define (hl-workspace-id-set! ws new-id)
  "Change the numbered workspace WS's ID to NEW-ID. Only numbered
   workspaces can be re-IDed (named/special cannot); the new ID must be
   > 0 and not in use."
  (= 0 (c-hl-workspace-change-id (hl--ws-arg ws) (exact->inexact new-id))))

;; ---- actions: cursor and misc -----------------------------------------------

(define (hl-cursor-move! x y)
  "Move the cursor to (X, Y)."
  (= 0 (c-hl-cursor-move (exact->inexact x) (exact->inexact y))))

(define (hl-cursor-move-to-corner! w corner)
  "Move the cursor to CORNER (0-3) of WINDOW."
  (= 0 (c-hl-cursor-corner (hl--wid w) corner)))

(define (hl-exit! )
  "Quit the compositor."
  (= 0 (c-hl-exit)))

(define (hl-config-reload! )
  "Reload the configuration, like hyprctl reload."
  (= 0 (c-hl-reload-config)))

(define (hl-force-renderer-reload! )
  "Force the renderer to reload on all monitors."
  (= 0 (c-hl-force-renderer-reload)))

(define* (hl-monitor-power-set! mon #:key (on? 'unset))
  "Switch MON's power (a monitor, or #f for all monitors): absent #:on?
   toggles, #t on, #f off."
  (= 0 (c-hl-dpms (hl--bool-act on?) (if mon (hl--mon-arg mon) ""))))

(define (hl-force-idle! seconds)
  "Pretend SECONDS of idle time have elapsed for all idle timers."
  (= 0 (c-hl-force-idle (exact->inexact seconds))))

(define (hl-global! action)
  "Activate the global shortcut named ACTION (D-Bus GlobalShortcuts; see
   bind-globals)."
  (= 0 (c-hl-global (hl--str action))))

(define (hl-event! data)
  "Send DATA as an event on socket2, visible to other IPC clients."
  (= 0 (c-hl-event (hl--str data))))

(define (hl-window-pass-shortcut! w)
  "Pass the currently-pressed keybind through to WINDOW."
  (= 0 (c-hl-pass (hl--wid w))))

(define (hl-window-send-shortcut! mods key . w)
  "Send a keypress to WINDOW (default: the active window) as if pressed:
   MODS is a list of modifier tokens, as built by hl-kbd/hl-key (e.g.
   (hl-key \"SUPER\")), and KEY an xkb keysym name."
  (unless (and (list? mods) (andmap (lambda (m) (string? m)) mods))
    (errorf 'hl-window-send-shortcut! "'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\")"))
  (= 0 (c-hl-send-shortcut mods key (if (null? w) -1 (hl--wid (car w))))))

(define (hl-window-send-key-state! mods key state . w)
  "Like hl-window-send-shortcut!, but STATE selects the key state
   explicitly (press or release)."
  (unless (and (list? mods) (andmap (lambda (m) (string? m)) mods))
    (errorf 'hl-window-send-key-state! "'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\")"))
  (= 0 (c-hl-send-key-state mods key state (if (null? w) -1 (hl--wid (car w))))))

(define (hl-mouse-action! action)
  "Begin an interactive mouse action for mouse binds: (hl-mouse-action!
   \"drag\") or (hl-mouse-action! \"resize\")."
  (= 0 (c-hl-mouse (hl--str action))))

(define (hl-clear-crashed-lockscreen! )
  "Manual escape hatch when a lock screen has crashed: clears the
   session lock so the session is usable again. Refused while a lock
   client is attached or the session isn't locked - it can never unlock
   a live lock screen."
  (let ((rc (c-hl-clear-crashed-lockscreen)))
    (if (= 0 rc) #t (errorf 'hl-clear-crashed-lockscreen! "~a" (c-hl-config-last-error)))))

(define (hl-exec-scheduled-prop-refresh-immediately )
  "Run the config-time deferred refresh pass NOW (rule/prop/layout
   changes schedule it). #t when it executed as scheduled, #f otherwise."
  (= 0 (c-hl-scheduled-prop-refresh-immediately)))

(define (hl-release-input-capture! )
  "Release any active input capture session."
  (= 0 (c-hl-release-input-capture)))

(define (hl-layout-msg msg)
  "Send MSG to the active workspace's layout (see custom layouts in the
   wiki)."
  (= 0 (c-hl-layout-message (hl--str msg))))

;; ---- config -----------------------------------------------------------------
(define (hl-config-add! key val)
  "Set the config option KEY to VAL. Keys accept \"general:gaps_in\"or
   \"general.gaps_in\". Values: numbers, strings, booleans, lists
   (vec2-style arrays) or a plist for tables (e.g. '(top 10 bottom 10)).
   Writes propagate like a runtime config change - the affected
   subsystems refresh immediately."
  (c-hl-config-begin)
  (hl--push-val val)
  (if (= 0 (c-hl-config-set (hl--str key)))
      #t
      (errorf 'hl-config-add! "~a" (c-hl-config-last-error))))

(define (hl--push-val v)
  (cond ((number? v)
         ;; exact integers must reach INT options as integers (upstream
         ;; rejects doubles for them); FLOAT options accept integers too
         (if (exact? v)
             (c-hl-config-push-int (exact->inexact v))
             (c-hl-config-push-num v)))
        ((boolean? v) (c-hl-config-push-bool (if v 1 0)))
        ((string? v) (c-hl-config-push-str v))
        ((and (pair? v) (symbol? (car v)))
         ;; plist → hash table (the API's nested named-fields form)
         (c-hl-config-tbl-open 1)
         (hl--plist-fold (lambda (k val _)
                           (c-hl-config-tbl-key (hl--str k))
                           (hl--push-val val)
                           (c-hl-config-tbl-set-hash))
                         0 v))
        ((and (list? v) (or (null? v) (not (pair? (car v)))))
         ;; array table (e.g. a vec2 '(20 20))
         (c-hl-config-tbl-open 0)
         (let loop ((rest v) (i 1))
           (unless (null? rest)
             (hl--push-val (car rest))
             (c-hl-config-tbl-seti i)
             (loop (cdr rest) (+ i 1)))))
        (else (errorf 'hl-config-add! "unsupported value ~s" v))))

(define (hl-config-get key)
  "Return the current value of the config option KEY, as Scheme data
   (number, string, boolean, or a plist for tables)."
  (c-hl-config-get (hl--str key)))   ; #t/#f, number, string, or a plist for tables

(define c-hl-device-add (foreign-procedure "hl-scheme-device-add" (string scheme-object) int))

(define* (hl-device-add! name #:key #:allow-other-keys #:rest fields)
  "Configure a device: NAME plus keyword fields, e.g.
   (hl-device-add! \"tablet\" #:repeat_rate 25 #:natural_scroll #t).
   Write-only (no device read side, no per-field unset); validated
   against the field table and applied to the live device. Unknown
   keywords are rejected by the field table."
  (let ((plist (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)))
    (if (= 0 (c-hl-device-add (hl--str name) plist))
        #t
        (errorf 'hl-device-add! "~a" (c-hl-config-last-error)))))


(define (hl-exec! cmd . effects) "The one exec. Spawns CMD asynchronously
through the compositor's executor (shell, env injection; never blocks the
config). Optional rule EFFECTS as a plist build a one-shot window rule
pinned to the spawned window by pid; values go through the same rule-spec
coercion as hl-window-rule-add!. With no effects the command may still
carry the legacy inline rule prefix (\"[float size 800 500] mygame\") —
the C++ layer parses it on the plain path. Returns the new pid."
  (define (flat l)
    (cond ((null? l) '())
          ((null? (cdr l)) (errorf 'hl-exec! "odd plist of rule effects"))
          (else (list* (hl--str (car l))
                       (hl--rule-spec-value (cadr l))
                       (flat (cddr l))))))
  (let ((pid (c-hl-exec! (hl--str cmd) (flat effects))))
    (if (> pid 0)
        pid
        (errorf 'hl-exec! "~a" (c-hl-config-last-error)))))

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
(define-record-type hl-rule (fields kind name))

(define (hl--rule-spec-value v)
  (cond ((string? v) v)
        ((boolean? v) (if v "true" "false"))
        ((number? v) (number->string v))
        (else #f)))

(define (hl--window-rule-mk name spec begin-fn effect-fn commit-fn what kind)
  (let ((enabled (hl--plist-get spec 'enabled #t)))
    (if (not (= 0 (begin-fn (if name (hl--str name) "") (if enabled 1 0))))
        (errorf what "~a" (c-hl-config-last-error))
        (let loop ((rest spec))
          (cond ((null? rest)
                 ;; done: make the handle record, commit, index it C++-side
                 (let ((rec (make-hl-rule kind (or name ""))))
                   (if (not (= 0 (commit-fn rec)))
                       (errorf what "~a" (c-hl-config-last-error))
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
                                         (errorf what "bad match value for ~a" mk)
                                         (if (= 0 (c-hl-rule-match mk sv))
                                             (mloop (cdr mtail))
                                             (errorf what "~a" (c-hl-config-last-error)))))))))
                         ((equal? k "enabled")
                          (loop (cdr tail)))
                         (else
                          (let ((sv (hl--rule-spec-value v)))
                            (if (not sv)
                                (errorf what "bad effect value for ~a" k)
                                (if (= 0 (effect-fn k sv))
                                    (loop (cdr tail))
                                    (errorf what "~a" (c-hl-config-last-error))))))))))))))

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
    c-hl-window-rule-begin c-hl-window-rule-effect c-hl-window-rule-commit
    'hl-window-rule-add! 'window))

(define* (hl-layer-rule-add! name #:key #:allow-other-keys #:rest fields)
  "Add a layer rule: the same keyword-rule-spec shape as
   hl-window-rule-add!, matching on the namespace property inside
   #:match."
  (hl--window-rule-mk name
    (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)
    c-hl-layer-rule-begin c-hl-layer-rule-effect c-hl-layer-rule-commit
    'hl-layer-rule-add! 'layer))

(define* (hl-rule-enabled-set! rule #:key (on? 'unset))
  "Enable, disable or toggle RULE: absent #:on? toggles, #t/#f set."
  (if (= 0 (c-hl-rule-set-enabled rule
                            (if (if (eq? on? 'unset) (not (hl-rule-enabled? rule)) on?) 1 0)))
      #t
      (errorf 'hl-rule-enabled-set! "unknown rule")))

(define (hl-rule-enabled? rule)
  "#t when RULE is enabled."
  (= 1 (c-hl-rule-enabled rule)))

;; ---- groups as objects (upstream HL.Group parity) ----------------------------
;; a group is the tabbed-window arrangement on a workspace; groups dissolve
;; behind our backs, so handles follow the weak-handle model (stale -> #f).
;; passives only: state reads + two mutators; NO callbacks (upstream parity).
(define-record-type hl-group (fields cell))
(define (hl-group-id g)
  "The handle's internal id (rarely needed)." (car (hl-group-cell g)))

(define (hl--mint-group v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-group "implausible handle value ~s" v)))
      (r (make-hl-group cell)))
    (hl--handle-guardian cell)
    r))

(define (hl-workspace-groups WS)
  "The groups on workspace WS, as a list of group handles."
  (let ((l (c-hl-workspace-groups (hl-workspace-id WS))))
    (if l (map hl--mint-group l) '())))

(define (hl-group-members g)
  "The group's windows, as a list of window handles."
  (let ((l (c-hl-group-members (hl-group-id g))))
    (if l (map hl--mint-window l) '())))

(define (hl-group-size g)
  "How many windows the group has."
  (c-hl-group-size (hl-group-id g)))

(define (hl-group-current g)
  "The group's currently-focused member, as a window handle, or #f."
  (let ((id (c-hl-group-current (hl-group-id g))))
    (and id (hl--mint-window id))))

(define (hl-group-current-index g)
  "The 1-based position of the group's focused member."
  (c-hl-group-current-idx (hl-group-id g)))

(define (hl-group-locked? g)
  "#t when the group is locked."
  (eq? (c-hl-group-locked (hl-group-id g)) #t))

(define (hl-group-denied? g)
  "#t when the group denies new members."
  (eq? (c-hl-group-denied (hl-group-id g)) #t))

(define (hl-group-alive? g)
  "#t when the group still exists (groups dissolve behind your back;
   stale handles report #f)."
  (eq? (c-hl-group-alive (hl-group-id g)) #t))

(define (hl-group=? a b)
  "#t when A and B refer to the same underlying group."
  (= 1 (c-hl-group-same (hl-group-id a) (hl-group-id b))))

(define (hl-group-add! g window . index)
  "Add WINDOW to the group G, optionally at a 1-based INDEX. Errors when
   the group denies new members or the window cannot be grouped into it."
  (if (= 0 (c-hl-group-add (hl-group-id g) (hl-window-id window)
                           (if (null? index) -1 (exact (car index)))))
      #t
      (errorf 'hl-group-add! "~a" (c-hl-config-last-error))))

(define (hl-group-remove! g window)
  "Remove WINDOW from the group G; errors when it is not a member."
  (if (= 0 (c-hl-group-remove (hl-group-id g) (hl-window-id window)))
      #t
      (errorf 'hl-group-remove! "~a" (c-hl-config-last-error))))

(define* (hl-workspace-rule-add! ws #:key #:allow-other-keys #:rest fields)
  "Add a workspace rule for WS (a selector or handle):
   (hl-workspace-rule-add! \"3\" #:monitor \"DP-1\" #:layout \"master\").
   #:enabled toggles the rule (a rule is enabled unless you pass
   #:enabled #f). Fields as keywords: monitor default persistent
   gaps_in gaps_out float_gaps border_size no_border no_rounding
   decorate no_shadow on_created_empty default_name layout animation
   layout_opts."
  (let ((spec (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)))
    (if (not (= 0 (c-hl-workspace-rule-begin (hl--ws-arg ws)
                (if (hl--plist-get spec 'enabled #t) 1 0))))
        (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))
        (let loop ((rest spec))
          (cond ((null? rest)
                 (if (= 0 (c-hl-workspace-rule-commit))
                     #t
                     (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
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
                                         (errorf 'hl-workspace-rule-add! "bad layout_opts value")
                                         (if (= 0 (c-hl-workspace-rule-layout-opt (hl--str (car o)) sv))
                                             (oloop (cdr otail))
                                             (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error)))))))))
                         ((member k (list "gaps_in" "gaps_out" "float_gaps"))
                          ;; css-gap fields: scalars and per-side plists both
                          ;; go through the gap parser, whatever their type
                          (c-hl-config-begin)
                          (hl--push-val v)
                          (if (= 0 (c-hl-workspace-rule-gap k))
                              (loop (cdr tail))
                              (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                         ((string? v)
                          (if (= 0 (c-hl-workspace-rule-str k v))
                              (loop (cdr tail))
                              (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                         ((number? v)
                          (if (= 0 (c-hl-workspace-rule-num k (exact->inexact v)))
                              (loop (cdr tail))
                              (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                         ((boolean? v)
                          (if (= 0 (c-hl-workspace-rule-bool k (if v 1 0)))
                              (loop (cdr tail))
                              (errorf 'hl-workspace-rule-add! "~a" (c-hl-config-last-error))))
                         (else (errorf 'hl-workspace-rule-add! "unsupported value for ~a" k))))))))))

;; ---- queries ------------------------------------------------------------------
(define (hl-window-from sel)
  "The window matching SELECTOR, or #f when none. Selectors use the
   config selector syntax: \"class:^foot$\", \"title:foo\", \"pid:123\",
   \"address:0x...\", \"workspace:3\", \"floating\", \"tiled\", ..."
  (let ((id (c-hl-window-from (hl--str sel))))
    (if (< id 0) #f (hl--mint-window (inexact->exact id)))))

(define (hl-urgent-window )
  "The most urgent window, as a handle, or #f when none."
  (let ((id (c-hl-urgent-window)))
    (if (< id 0) #f (hl--mint-window (inexact->exact id)))))

(define (hl-last-window )
  "The previously-focused window, as a handle, or #f."
  (let ((id (c-hl-last-window)))
    (if (< id 0) #f (hl--mint-window (inexact->exact id)))))

(define (hl-monitor-from sel)
  "The monitor matching SELECTOR (a name, or \"desc:DESCRIPTION\"in
   config monitor syntax), as a handle, or #f."
  (let ((id (c-hl-monitor-from (hl--str sel))))
    (and id (hl--mint-monitor id))))

(define (hl-monitor-at x y)
  "The monitor at layout position (X, Y), as a handle, or #f."
  (let ((id (c-hl-monitor-at (exact->inexact x) (exact->inexact y))))
    (and id (hl--mint-monitor id))))

(define (hl-monitor-at-cursor )
  "The monitor under the cursor, as a handle, or #f."
  (let ((id (c-hl-monitor-at-cursor)))
    (and id (hl--mint-monitor id))))

(define (hl-active-monitor )
  "The focused monitor, as a handle, or #f."
  (let ((id (c-hl-active-monitor)))
    (and id (hl--mint-monitor id))))

(define (hl-active-workspace )
  "The active workspace, as a handle, or #f."
  (let ((id (c-hl-active-workspace)))
    (and id (hl--mint-workspace id))))

(define (hl-active-special-workspace )
  "The currently open special workspace, as a handle, or #f."
  (let ((id (c-hl-active-special-workspace)))
    (and id (hl--mint-workspace id))))

(define (hl-last-workspace )
  "The most recently active workspace, as a handle, or #f."
  (let ((id (c-hl-last-workspace)))
    (and id (hl--mint-workspace id))))

(define (hl-workspace-windows ws)
  "The windows on workspace WS (a handle, or a selector like \"3\",
   \"name:foo\", \"special:bar\"), as a list of window handles."
  (let ((s (c-hl-workspace-windows (hl--ws-arg ws))))
    (if (not s)
        '()
        (map hl--mint-window s))))

;; ---- workspace/monitor handle getters ---------------------------------------
;; every getter takes a handle; a stale or dead handle yields #f from every
;; getter, like an expired object in upstream Lua. Getters mirror Lua's
;; workspace/monitor object fields 1:1.

;; handles are accepted anywhere a selector string is: the handle resolves
;; through its canonical selector, like upstream's selector-or-object
;; helpers. A dead handle resolves to "" and the action fails cleanly.
(define (hl--ws-arg ws)
  (if (hl-workspace? ws)
      (or (c-hl-workspace-selector (hl-workspace-id ws)) "")
      (hl--str ws)))

(define (hl--mon-arg m)
  (if (hl-monitor? m)
      (or (c-hl-monitor-selector (hl-monitor-id m)) "")
      (hl--str m)))

;; -- workspace getters
(define (hl-workspace-name w)
  "The workspace's name."
  (c-hl-workspace-name (hl-workspace-id w)))

(define (hl-workspace-addressable-name w)
  "The workspace's config-addressable name (usable in selectors and
   rules)."
  (c-hl-workspace-addressable-name (hl-workspace-id w)))

(define (hl-workspace-number w)
  "The workspace's numbered ID, or #f for named/special workspaces."
  (c-hl-workspace-number (hl-workspace-id w)))

(define (hl-workspace-monitor w)
  "The monitor the workspace is on, as a handle, or #f."
  (let ((id (c-hl-workspace-monitor (hl-workspace-id w))))
    (and id (hl--mint-monitor id))))

(define (hl-workspace-special? w)
  "#t when the workspace is a special workspace."
  (eq? (c-hl-workspace-special (hl-workspace-id w)) #t))

(define (hl-workspace-active? w)
  "#t when the workspace is the active (or active special) one on its
   monitor."
  (eq? (c-hl-workspace-active (hl-workspace-id w)) #t))

(define (hl-workspace-visible? w)
  "#t when the workspace is currently visible."
  (eq? (c-hl-workspace-visible (hl-workspace-id w)) #t))

(define (hl-workspace-empty? w)
  "#t when the workspace has no windows."
  (eq? (c-hl-workspace-empty (hl-workspace-id w)) #t))

(define (hl-workspace-persistent? w)
  "#t when the workspace is persistent."
  (eq? (c-hl-workspace-persistent (hl-workspace-id w)) #t))

(define (hl-workspace-has-urgent? w)
  "#t when some window on the workspace is urgent."
  (eq? (c-hl-workspace-has-urgent (hl-workspace-id w)) #t))

(define (hl-workspace-has-fullscreen? w)
  "#t when a window on the workspace is fullscreen."
  (eq? (c-hl-workspace-has-fullscreen (hl-workspace-id w)) #t))

(define (hl-workspace-fullscreen-mode w)
  "The workspace's internal fullscreen mode: 0 none, 1 maximized, 2
   fullscreen; -1 when none."
  (c-hl-workspace-fullscreen-mode (hl-workspace-id w)))

(define (hl-workspace-fullscreen-window w)
  "The workspace's fullscreen window, as a handle, or #f."
  (let ((id (c-hl-workspace-fullscreen-window (hl-workspace-id w))))
    (and id (hl--mint-window id))))

(define (hl-workspace-last-window w)
  "The workspace's most recently focused window, as a handle, or #f."
  (let ((id (c-hl-workspace-last-window (hl-workspace-id w))))
    (and id (hl--mint-window id))))

(define (hl-workspace-window-count w)
  "The number of windows on the workspace."
  (c-hl-workspace-window-count (hl-workspace-id w)))

(define (hl-workspace-group-count w)
  "The number of groups on the workspace."
  (c-hl-workspace-group-count (hl-workspace-id w)))

(define (hl-workspace-tiled-layout w)
  "The tiled layout currently serving the workspace, as a string."
  (c-hl-workspace-tiled-layout (hl-workspace-id w)))

(define (hl-workspace-alive? w)
  "#t when the workspace still exists."
  (eq? (c-hl-workspace-alive (hl-workspace-id w)) #t))

(define (hl-workspace=? a b)
  "#t when A and B refer to the same workspace."
  (eq? (c-hl-workspace-same (hl-workspace-id a) (hl-workspace-id b)) #t))

;; -- monitor getters
(define (hl-monitor-name m)
  "The monitor's name."
  (c-hl-monitor-name (hl-monitor-id m)))

(define (hl-monitor-description m)
  "The monitor's short description."
  (c-hl-monitor-description (hl-monitor-id m)))

(define (hl-monitor-number m)
  "The monitor's id."
  (c-hl-monitor-number (hl-monitor-id m)))

(define (hl-monitor-enabled? m)
  "#t when the monitor is enabled."
  (eq? (c-hl-monitor-enabled (hl-monitor-id m)) #t))

(define (hl-monitor-focused? m)
  "#t when the monitor has focus."
  (eq? (c-hl-monitor-focused (hl-monitor-id m)) #t))

(define (hl-monitor-x m)
  "The monitor's X position."
  (c-hl-monitor-x (hl-monitor-id m)))

(define (hl-monitor-y m)
  "The monitor's Y position."
  (c-hl-monitor-y (hl-monitor-id m)))

(define (hl-monitor-width m)
  "The monitor's width in pixels."
  (c-hl-monitor-width (hl-monitor-id m)))

(define (hl-monitor-height m)
  "The monitor's height in pixels."
  (c-hl-monitor-height (hl-monitor-id m)))

(define (hl-monitor-scale m)
  "The monitor's scale factor."
  (c-hl-monitor-scale (hl-monitor-id m)))

(define (hl-monitor-transform m)
  "The monitor's transform (rotation/flip, 0-7; see monitor-positioning
   in the wiki)."
  (c-hl-monitor-transform (hl-monitor-id m)))

(define (hl-monitor-refresh-rate m)
  "The monitor's refresh rate, in Hz."
  (c-hl-monitor-refresh-rate (hl-monitor-id m)))

(define (hl-monitor-mode m)
  "The monitor's current mode, as \"WIDTHxHEIGHT@RATE\"."
  (c-hl-monitor-mode (hl-monitor-id m)))

(define (hl-monitor-power? m)
  "#t when the monitor is powered on."
  (eq? (c-hl-monitor-dpms (hl-monitor-id m)) #t))

(define (hl-monitor-vrr? m)
  "#t when VRR is currently active on the monitor."
  (eq? (c-hl-monitor-vrr (hl-monitor-id m)) #t))

(define (hl-monitor-10bit? m)
  "#t when the monitor runs at 10-bit color depth."
  (eq? (c-hl-monitor-10bit (hl-monitor-id m)) #t))

(define (hl-monitor-reserved m)
  "The monitor's reserved area, as a plist: (top n left n right n bottom
   n)."
  (c-hl-monitor-reserved (hl-monitor-id m)))

(define (hl-monitor-serial m)
  "The monitor's serial, as a string, or #f when stale."
  (c-hl-monitor-serial (hl-monitor-id m)))

(define (hl-monitor-physical-size m)
  "The monitor's physical size in mm, as (w . h), or #f."
  (c-hl-monitor-physical-size (hl-monitor-id m)))

(define (hl-monitor-mirrors m)
  "The monitors mirroring this one, as a list of handles (empty when
   none)."
  (or (c-hl-monitor-mirrors (hl-monitor-id m)) '()))

(define (hl-monitor-available-modes m)
  "The monitor's available modes, as ((width w height h refresh-rate r
   preferred b) ...)."
  (or (c-hl-monitor-available-modes (hl-monitor-id m)) '()))

(define (hl-monitor-hardware-details m)
  "The monitor's hardware details, as a plist (backend \"...\"hdr b
   chroma b bt2020 b vrr-capable b)."
  (c-hl-monitor-hardware-details (hl-monitor-id m)))

(define (hl-monitor-mirror-of m)
  "The monitor this one mirrors, as a handle; #f when not a mirror."
  (let ((id (c-hl-monitor-mirror-of (hl-monitor-id m))))
    (and id (hl--mint-monitor id))))

(define (hl-monitor-active-workspace m)
  "The monitor's active workspace, as a handle, or #f."
  (let ((id (c-hl-monitor-active-workspace (hl-monitor-id m))))
    (and id (hl--mint-workspace id))))

(define (hl-monitor-active-special-workspace m)
  "The monitor's open special workspace, as a handle, or #f when none is
   open."
  (let ((id (c-hl-monitor-active-special-workspace (hl-monitor-id m))))
    (and id (hl--mint-workspace id))))

(define (hl-monitor-alive? m)
  "#t when the monitor still exists."
  (eq? (c-hl-monitor-alive (hl-monitor-id m)) #t))

(define (hl-monitor-rule-add!=? a b)
  "#t when A and B refer to the same monitor."
  (eq? (c-hl-monitor-same (hl-monitor-id a) (hl-monitor-id b)) #t))

;; => list of (monitor . namespace) pairs
;; ---- layer surfaces as objects (upstream HL.LayerSurface parity) -------------
;; layer surfaces die independently -> weak handles via the guardian;
;; every getter returns #f when the surface is gone. NO callbacks.
;; filters are plist-style: (hl-layers), (hl-layers 'monitor MON),
;; (hl-layers 'namespace "ns"), or combined.
(define-record-type hl-layer (fields cell))
(define (hl-layer-id s)
  "The handle's internal id (rarely needed)." (car (hl-layer-cell s)))

(define (hl--mint-layer v)
  (let* ((v (inexact->exact v))
      (cell (if (and (integer? v) (exact? v) (>= v 0) (< v 140737488355328))
             (cons v 0)
             (errorf 'hl--mint-layer "implausible handle value ~s" v)))
      (r (make-hl-layer cell)))
    (hl--handle-guardian cell)
    r))

(define* (hl-layers #:key monitor namespace)
  "The layer surfaces, as a list of layer handles. Optional keyword
   filters: #:monitor MON, #:namespace \"ns\", or both combined."
  (let* ((mon  monitor)
         (ns   namespace)
         (monId (if (and mon (hl-monitor? mon)) (hl-monitor-id mon) 0))
         (l (c-hl-layers monId (and ns (hl--str ns)))))
    (if l (map hl--mint-layer l) '())))

(define (hl-layer-alive? s)
  "#t when the layer surface still exists."
  (= 1 (c-hl-layer-alive (hl-layer-id s))))

(define (hl-layer=? a b)
  "#t when A and B refer to the same layer surface."
  (= 1 (c-hl-layer-same (hl-layer-id a) (hl-layer-id b))))

(define (hl-layer-address s)
  "The surface's stable \"0x...\"address (same idea as
   hl-window-address)."
  (c-hl-layer-address (hl-layer-id s)))

(define (hl-layer-pid s)
  "The surface's owning process id."
  (c-hl-layer-pid (hl-layer-id s)))

(define (hl-layer-monitor s)
  "The monitor the surface sits on, as a handle."
  (let ((id (c-hl-layer-monitor (hl-layer-id s))))
    (and id (hl--mint-monitor id))))

(define (hl-layer-namespace s)
  "The surface's namespace."
  (c-hl-layer-namespace (hl-layer-id s)))

(define (hl-layer-level s)
  "The surface's shell layer: 0 background, 1 bottom, 2 top, 3 overlay."
  (c-hl-layer-level (hl-layer-id s)))

(define (hl-layer-mapped? s)
  "#t when the surface is mapped."
  (eq? (c-hl-layer-mapped (hl-layer-id s)) #t))

(define (hl-layer-kb-interactivity s)
  "The surface's keyboard interactivity: 0 none, 1 exclusive, 2
   on-demand (lock screens use exclusive)."
  (c-hl-layer-kb-interactivity (hl-layer-id s)))

(define (hl-layer-above-fullscreen? s)
  "#t when the surface renders above fullscreen windows."
  (eq? (c-hl-layer-above-fs (hl-layer-id s)) #t))

(define (hl-layer-position s)
  "The surface's position, as (x . y), monitor-local."
  (c-hl-layer-position (hl-layer-id s)))

(define (hl-layer-size s)
  "The surface's size, as (width . height)."
  (c-hl-layer-size (hl-layer-id s)))

(define (hl-is-key-down key)
  "#t when the key named by the xkb keysym KEY (e.g. \"Return\") is
   currently pressed."
  (= 1 (c-hl-is-key-down key)))

(define (hl-loaded-plugins )
  "The names of the loaded plugins."
  (or (c-hl-loaded-plugins) '()))

(define (hl-version )
  "The compositor's version string."
  (c-hl-version))

(define (hl-windows-from sel)
  "All windows matching SELECTOR, as a list of window handles."
  (let ((s (c-hl-windows-from (hl--str sel))))
    (if (not s)
        '()
        (map hl--mint-window s))))

(define (hl-window-fullscreen-handler w)
  "Which fullscreen handler the window uses, as a string."
  (c-hl-window-fullscreen-handler (hl--wid w)))

;; ---- notifications ------------------------------------------------------------
(define* (hl-notify! text duration #:key (icon "none") (color "0") (font-size 13))
  "Show a one-shot notification: (hl-notify! \"text\" 5000), or with
   keyword options: (hl-notify! \"text\" 5000 #:icon \"info\" #:color
   \"0x80FF80FF\" #:font-size 13). Icons: none warn info hint error
   confused ok. Color: 0xAARRGGBB hex string; 0 = the icon's default
   color."
  (let ((s (c-hl-notify (hl--str text) (exact->inexact duration)
                        (hl--str icon)
                        (hl--str color)
                        (exact->inexact font-size))))
    (if s #t #f)))

;; ---- live notification objects (upstream hl.notification parity) ------------
;; (hl-notification-add! 'text "…" 'timeout ms ['icon "info"] ['color "…"]
;;   ['font-size n]) => a notification HANDLE. 'timeout is required; pause
;; freezes the timeout timer (the bubble stays until dismissed); expired
;; handles read #f and mutate as no-ops.
(define c-hl-notification-add (foreign-procedure "hl-notification-add" (scheme-object) scheme-object))
(define c-hl-notification-list (foreign-procedure "hl-notification-list" () scheme-object))
(define c-hl-notification-text (foreign-procedure "hl-notification-text" (integer-64) scheme-object))
(define c-hl-notification-timeout (foreign-procedure "hl-notification-timeout" (integer-64) scheme-object))
(define c-hl-notification-color (foreign-procedure "hl-notification-color" (integer-64) scheme-object))
(define c-hl-notification-icon (foreign-procedure "hl-notification-icon" (integer-64) scheme-object))
(define c-hl-notification-font-size (foreign-procedure "hl-notification-font-size" (integer-64) scheme-object))
(define c-hl-notification-elapsed (foreign-procedure "hl-notification-elapsed" (integer-64) scheme-object))
(define c-hl-notification-age (foreign-procedure "hl-notification-age" (integer-64) scheme-object))
(define c-hl-notification-alive (foreign-procedure "hl-notification-alive" (integer-64) scheme-object))
(define c-hl-notification-same (foreign-procedure "hl-notification-same" (integer-64 integer-64) scheme-object))
(define c-hl-notification-text-set (foreign-procedure "hl-notification-text-set" (integer-64 string) int))
(define c-hl-notification-timeout-set (foreign-procedure "hl-notification-timeout-set" (integer-64 double) int))
(define c-hl-notification-color-set (foreign-procedure "hl-notification-color-set" (integer-64 string) int))
(define c-hl-notification-icon-set (foreign-procedure "hl-notification-icon-set" (integer-64 scheme-object) int))
(define c-hl-notification-font-size-set (foreign-procedure "hl-notification-font-size-set" (integer-64 double) int))
(define c-hl-notification-paused-set (foreign-procedure "hl-notification-paused-set" (integer-64 int) int))
(define c-hl-notification-paused-q (foreign-procedure "hl-notification-paused-q" (integer-64) scheme-object))
(define c-hl-notification-dismiss (foreign-procedure "hl-notification-dismiss" (integer-64) int))

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
    (let ((id (c-hl-notification-add fields)))
      (if id
          (hl--mint-notification id)
          (errorf 'hl-notification-add! "~a" (c-hl-config-last-error))))))

(define (hl-notifications )
  "The live notifications, as a list of handles."
  (let ((l (c-hl-notification-list)))
    (if l (map hl--mint-notification l) '())))

(define (hl-notification-text n)
  "The notification's text."
  (c-hl-notification-text (hl-notification-id n)))

(define (hl-notification-timeout n)
  "The notification's timeout, in ms."
  (c-hl-notification-timeout (hl-notification-id n)))

(define (hl-notification-color n)
  "The notification's color, as the raw 0xAARRGGBB integer (readback
   parity with upstream)."
  (c-hl-notification-color (hl-notification-id n)))

(define (hl-notification-icon n)
  "The notification's icon, as its hyprctl id (names exist on the write
   side only)."
  (c-hl-notification-icon (hl-notification-id n)))

(define (hl-notification-font-size n)
  "The notification's font size."
  (c-hl-notification-font-size (hl-notification-id n)))

(define (hl-notification-elapsed n)
  "How long the notification has been live, in ms, excluding paused
   spans."
  (c-hl-notification-elapsed (hl-notification-id n)))

(define (hl-notification-age n)
  "Wall time since the notification was created, in ms."
  (c-hl-notification-age (hl-notification-id n)))

(define (hl-notification-alive? n)
  "#t when the notification is still live."
  (eq? (c-hl-notification-alive (hl-notification-id n)) #t))

(define (hl-notification=? a b)
  "#t when A and B are the same notification."
  (eq? (c-hl-notification-same (hl-notification-id a) (hl-notification-id b)) #t))

;; failed setters raise with the config system's own message
(define (hl--notif-act who act)
  (if (= 0 act) #t (errorf who "~a" (c-hl-config-last-error))))

(define (hl-notification-text-set! n text)
  "Set the notification's text."
  (hl--notif-act 'hl-notification-text-set!
    (c-hl-notification-text-set (hl-notification-id n) (hl--str text))))

(define (hl-notification-timeout-set! n ms)
  "Set the notification's timeout, in ms."
  (hl--notif-act 'hl-notification-timeout-set!
    (c-hl-notification-timeout-set (hl-notification-id n) (exact->inexact ms))))

(define (hl-notification-color-set! n color)
  "Set the notification's color (0xAARRGGBB hex string)."
  (hl--notif-act 'hl-notification-color-set!
    (c-hl-notification-color-set (hl-notification-id n) (hl--str color))))

(define (hl-notification-icon-set! n icon)
  "Set the notification's icon (icon name, e.g. \"warn\")."
  (hl--notif-act 'hl-notification-icon-set!
    (c-hl-notification-icon-set (hl-notification-id n) icon)))

(define (hl-notification-font-size-set! n size)
  "Set the notification's font size."
  (hl--notif-act 'hl-notification-font-size-set!
    (c-hl-notification-font-size-set (hl-notification-id n) (exact->inexact size))))

(define* (hl-notification-paused-set! n #:key (on? 'unset))
  "Pause or resume the notification's timeout timer: absent #:on? toggles,
   #t/#f set."
  (= 0 (c-hl-notification-paused-set (hl-notification-id n)
         (if (if (eq? on? 'unset) (not (eq? (hl-notification-paused? n) #t)) (eq? on? #t)) 1 0))))

(define (hl-notification-paused? n)
  "#t when the notification is paused (its timeout timer frozen)."
  (eq? (c-hl-notification-paused-q (hl-notification-id n)) #t))

(define (hl-notification-dismiss! n)
  "Dismiss the notification."
  (= 0 (c-hl-notification-dismiss (hl-notification-id n))))

;; ---- timer handles --------------------------------------------------------------
(define* (hl-timer-enabled-set! t #:key (on? 'unset))
  "Enable, disable or toggle the timer T: absent #:on? toggles, #t/#f set."
  (if (= 0 (c-hl-timer-set-enabled t
                                   (if (if (eq? on? 'unset) (not (hl-timer-enabled? t)) on?) 1 0)))
      #t
      (errorf 'hl-timer-enabled-set! "unknown timer")))
(define (hl-timer-enabled? t)
  "#t when the timer T is enabled."
  (= 1 (c-hl-timer-enabled t)))
(define (hl-timer-set-timeout t ms)
  "Re-tune the timer T's timeout to MS (>= 1)."
  (if (= 0 (c-hl-timer-set-timeout t (exact->inexact ms)))
      #t
      (errorf 'hl-timer-set-timeout "timeout must be >= 1ms")))

(define (hl-timer-cancel! t)
  "Cancel a repeating timer before reload (beyond-upstream extension).
   The index entry erases itself; the record lock releases with the
   timer."
  (= 0 (c-hl-timer-cancel t)))


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
        (else (errorf name "mode must be one of ~a" allowed))))

(define (hl-make-workspace-swipe-gesture )
  "A gesture action that swipes workspaces."
  (cons (c-hl-gesture-maker-workspace-swipe) '()))

(define (hl-make-move-gesture )
  "A gesture action that moves the active window."
  (cons (c-hl-gesture-maker-move) '()))

(define (hl-make-resize-gesture )
  "A gesture action that resizes the active window."
  (cons (c-hl-gesture-maker-resize) '()))

(define (hl-make-close-gesture )
  "A gesture action that closes the active window."
  (cons (c-hl-gesture-maker-close) '()))

(define (hl-make-scroll-move-gesture )
  "A gesture action that scrolls the tape (when the current layout
   supports it, e.g. scrolling)."
  (cons (c-hl-gesture-maker-scroll-move) '()))

(define (hl-make-float-gesture . mode)
  "A gesture action that floats/tiles the active window. Optional MODE:
   'toggle (default), 'float, 'tile."
  (let ((m (hl--gesture-mode 'hl-make-float-gesture mode '(toggle float tile) 'toggle)))
    (cons (c-hl-gesture-maker-float) (list 'mode m))))

(define (hl-make-fullscreen-gesture . mode)
  "A gesture action that fullscreens the active window. Optional MODE:
   'fullscreen (default), 'maximize."
  (let ((m (hl--gesture-mode 'hl-make-fullscreen-gesture mode '(fullscreen maximize) 'fullscreen)))
    (cons (c-hl-gesture-maker-fullscreen) (list 'mode m))))

(define (hl-make-special-workspace-gesture name)
  "A gesture action that toggles the named special workspace (empty
   string = the default special workspace)."
  (unless (string? name)
    (errorf 'hl-make-special-workspace-gesture "workspace name must be a string, got ~a" name))
  (cons (c-hl-gesture-maker-special) (list 'name name)))

(define (hl-make-cursor-zoom-gesture zoom . mode)
  "A gesture action that zooms the cursor's view. ZOOM is a number;
   optional MODE: 'toggle (default), 'mult, 'live - the numeric argument
   is unused in live mode, so 1 is a good placeholder there."
  (unless (real? zoom)
    (errorf 'hl-make-cursor-zoom-gesture "zoom must be a number, got ~a" zoom))
  (let ((m (hl--gesture-mode 'hl-make-cursor-zoom-gesture mode '(toggle mult live) 'toggle)))
    (cons (c-hl-gesture-maker-cursor-zoom) (list 'zoom (exact->inexact zoom) 'mode m))))

(define* (hl-make-custom-gesture #:key start update finish)
  "A callback-backed gesture action: (hl-make-custom-gesture #:start FN
   #:update FN #:finish FN) - at least one procedure required; each
   receives the gesture event plist as spread args (see bind-gestures
   for the fields). The three closures share state naturally by closing
   over a let - wrap the constructor in a function to get fresh state
   per registration."
  (when (and (not start) (not update) (not finish))
    (errorf 'hl-make-custom-gesture "at least one of #:start, #:update, #:finish is required"))
  (for-each (lambda (k fn)
              (when (and fn (not (procedure? fn)))
                (errorf 'hl-make-custom-gesture "field ~a needs a procedure" k)))
            '(#:start #:update #:finish) (list start update finish))
  (cons (c-hl-gesture-maker-custom)
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
           (errorf 'hl-gesture-add! "field 'fingers' is required"))
          ((not (and (integer? fingers) (exact? fingers) (>= fingers 2)))
           (errorf 'hl-gesture-add! "field 'fingers' must be an integer >= 2"))
          ((not direction)
           (errorf 'hl-gesture-add! "field 'direction' is required"))
          ((not (string? direction))
           (errorf 'hl-gesture-add! "field 'direction' must be a string"))
          ((not action)
           (errorf 'hl-gesture-add! "an action is required — #:action (hl-make-...-gesture ...)"))
          ((not (hl-gesture-action? action))
           (errorf 'hl-gesture-add! "field 'action' must be an hl-gesture-action (see the hl-make-*-gesture constructors)"))
          ((not (and (list? mods) (andmap (lambda (m) (string? m)) mods)))
           (errorf 'hl-gesture-add! "'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\") or '(\"SUPER\" \"SHIFT\")"))
          ((not (or (<= -10.0 scale -0.1) (<= 0.1 scale 10.0)))
           (errorf 'hl-gesture-add! "field 'scale' must be between -10 and -0.1 or between 0.1 and 10 - it is currently: ~a" scale))
          (else
           (let ((rc (c-hl-gesture action fingers (hl--str direction) mods (exact->inexact scale) (if disable-inhibit 1 0))))
             (cond ((not (= 0 rc))
                    (errorf 'hl-gesture-add! "~a" (c-hl-config-last-error)))
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
    (errorf 'hl-gesture-remove! "not an hl-gesture handle"))
  (let ((rc (c-hl-gesture-remove (car g) (cadr g) (caddr g) (exact->inexact (cadddr g))
                                 (if (list-ref g 4) 1 0))))
    (cond ((= rc 0) #t)
          ((= rc 1) #f)
          (else (errorf 'hl-gesture-remove! "~a" (c-hl-config-last-error))))))

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
    (if (not (= 0 (c-hl-monitor-begin (hl--mon-arg output))))
        (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))
        (let loop ((rest plist))
        (cond ((null? rest)
               (if (= 0 (c-hl-monitor-commit))
                   #t
                   (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
              (else
               (let* ((tail (hl--plist-cdr rest))
                      (f  (hl--str (car rest)))
                      (v  (car tail)))
                 (cond ((string? v)
                        (if (= 0 (c-hl-monitor-field-str f v))
                            (loop (cdr tail))
                            (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
                       ((number? v)
                        (if (= 0 (c-hl-monitor-field-num f (exact->inexact v)))
                            (loop (cdr tail))
                            (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
                       ((boolean? v)
                        (if (= 0 (c-hl-monitor-field-bool f (if v 1 0)))
                            (loop (cdr tail))
                            (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
                       ((pair? v)
                        (c-hl-config-begin)
                        (hl--push-val v)
                        (if (= 0 (c-hl-monitor-field-gap f))
                            (loop (cdr tail))
                            (errorf 'hl-monitor-rule-add! "~a" (c-hl-config-last-error))))
                       (else (errorf 'hl-monitor-rule-add! "unsupported value for ~a" f))))))))))

(define (hl-curve-add! name type . vals)
  "Declare an animation curve: (hl-curve-add! \"mycurve\"'bezier 0.25
   0.1 0.25 1.0) or (hl-curve-add! \"myspring\"'spring 250 25 1)."
  (let* ((t  (if (eq? type 'spring) 1 0))
         ;; pad THEN coerce: exact zeros are invalid in foreign double slots
         (vs (map exact->inexact (append vals (list 0 0 0 0)))))
    (if (= 0 (c-hl-curve-add (hl--str name) t
                             (list-ref vs 0) (list-ref vs 1) (list-ref vs 2) (list-ref vs 3)))
        #t
        (errorf 'hl-curve-add! "~a" (c-hl-config-last-error)))))

(define* (hl-animation-add! leaf #:key (enabled #t) (speed 8) (curve "") (style ""))
  "Configure an animation: (hl-animation-add! \"windowsIn\" #:enabled #t
   #:speed 8 #:curve \"mycurve\" #:style \"popin 80%\"). Speed defaults
   to 8; declare curves with hl-curve-add! first (or use builtins like
   \"default\")."
  (let* ((enabled enabled)
         (speed   speed)
         (curve   curve)
         (style   style))
    (if (= 0 (c-hl-animation-set (hl--str leaf) (if enabled 1 0)
                                 (exact->inexact speed) (hl--str curve) (hl--str style)))
        #t
        (errorf 'hl-animation-add! "~a" (c-hl-config-last-error)))))

(define (hl-permission-add! binary type mode)
  "Set a permission rule: (hl-permission-add! \"/usr/bin/grim\"
   'screencopy 'allow). Only takes effect at first launch - permission
   rules require a compositor restart."
  (if (= 0 (c-hl-permission-add binary (hl--str type) (hl--str mode)))
      #t
      (errorf 'hl-permission-add! "~a" (c-hl-config-last-error))))


(define* (hl-window-fullscreen-set! w #:key (on? 'unset))
  "Toggle or set WINDOW's fullscreen: absent #:on? runs the fullscreen
   toggle; #t sets fullscreen mode, #f unsets. Modes mirror the internal
   fullscreen modes: 1 maximized, 2 fullscreen."
  (if (eq? on? 'unset)
      (= 0 (c-hl-window-fullscreen-toggle (hl--wid w) 2))
      (= 0 (c-hl-window-fullscreen-set (hl--wid w) (if on? 2 0)))))

(define* (hl-window-maximized-set! w #:key (on? 'unset))
  "Toggle or set WINDOW's maximized state: the maximized flavour of
   hl-window-fullscreen-set!."
  (if (eq? on? 'unset)
      (= 0 (c-hl-window-fullscreen-toggle (hl--wid w) 1))
      (= 0 (c-hl-window-fullscreen-set (hl--wid w) (if on? 1 0)))))

(define (hl-window-fullscreen-state w internal client . layout-aware)
  "Set the explicit fullscreen state: INTERNAL and CLIENT are modes
   0/1/2; LAYOUT-AWARE? (absent = #f) makes the internal mode
   layout-aware."
  (= 0 (c-hl-window-fullscreen-state (hl--wid w) internal client
         (if (null? layout-aware) 0 (if (car layout-aware) 1 0)))))

(define (hl-window-fullscreen-mode w)
  "The window's fullscreen mode: 0 none, 1 maximized, 2 fullscreen; -1
   when stale."
  (c-hl-window-fullscreen-mode (hl-window-id w)))

(define (hl-window-hidden? w)
  "#t when the window is hidden."
  (= 1 (c-hl-window-hidden (hl-window-id w))))

;; ---- window read-side fields (upstream LuaWindow field parity) --------------
(define (hl-window-address w)
  "The window's stable \"0x...\"address - identity across queries:
   handles differ per call, the address does not."
  (c-hl-window-address (hl-window-id w)))

(define (hl-window-mapped? w)
  "#t when the window is mapped."
  (= 1 (c-hl-window-mapped (hl-window-id w))))

(define (hl-window-visible? w)
  "#t when the window is mapped, accepting input and with non-zero
   alpha."
  (= 1 (c-hl-window-visible (hl-window-id w))))

(define (hl-window-accepts-input? w)
  "#t when the window accepts input."
  (= 1 (c-hl-window-accepts-input (hl-window-id w))))

(define (hl-window-position w)
  "The window's position, as (x . y), or #f when stale."
  (c-hl-window-position (hl-window-id w)))

(define (hl-window-pin-fullscreened? w)
  "#t when the window stays pinned even when fullscreened."
  (= 1 (c-hl-window-pin-fullscreened (hl-window-id w))))

(define (hl-window-allowed-over-fullscreen? w)
  "#t when the window renders above fullscreen windows."
  (= 1 (c-hl-window-allowed-over-fullscreen (hl-window-id w))))

(define (hl-window-tearing-hint? w)
  "#t when the window hints it wants tearing."
  (= 1 (c-hl-window-tearing-hint (hl-window-id w))))

(define (hl-window-inhibiting-idle? w)
  "#t when the window holds an idle inhibitor."
  (= 1 (c-hl-window-inhibiting-idle (hl-window-id w))))

(define (hl-window-focus-history-id w)
  "The window's position in the focus history: 0 is the most recently
   focused; -1 when not in history."
  (c-hl-window-focus-history-id (hl-window-id w)))

(define (hl-window-content-type w)
  "The window's content type: \"none\", \"photo\", \"video\"or \"game\"."
  (c-hl-window-content-type (hl-window-id w)))

(define (hl-window-stable-id w)
  "The window's metadata stable id, as a hex string."
  (c-hl-window-stable-id (hl-window-id w)))

(define (hl-window-tags w)
  "The window's static tags, as a list of strings (set with
   hl-window-tag-add!)."
  (c-hl-window-tags (hl-window-id w)))

(define (hl-window-swallowing w)
  "The window this one is swallowing, as a handle, or #f."
  (let ((id (c-hl-window-swallowing-id (hl-window-id w))))
    (and id (hl--mint-window id))))

(define (hl-window-xdg-tag w)
  "The window's xdg-shell tag metadata, or #f when unset."
  (c-hl-window-xdg-tag (hl-window-id w)))

(define (hl-window-xdg-description w)
  "The window's xdg-shell description, or #f when unset."
  (c-hl-window-xdg-description (hl-window-id w)))

(define (hl-window-layout w)
  "The window's tiled layout state, as a plist: ('name \"master\"
   'is-master #f 'perc-master 0.5 'perc-size 1.0) or ('name
   \"scrolling\"'column (...) 'index-in-column n); #f when the window is
   floating or has no tiled layout target."
  (c-hl-window-layout (hl-window-id w)))

(define (hl-window-pinned? w)
  "#t when the window is pinned."
  (= 1 (c-hl-window-pinned (hl-window-id w))))

(define (hl-window-pseudo? w)
  "#t when the window is pseudo-tiled."
  (= 1 (c-hl-window-pseudo-query (hl--wid w))))

(define (hl-window-maximized? w)
  "#t when the window is maximized."
  (= 1 (c-hl-window-maximized-query (hl--wid w))))

(define (hl-window-deny-from-group? w)
  "#t when the window denies being grouped."
  (= 1 (c-hl-window-group-denied (hl--wid w))))

(define (hl-window-prop w prop)
  "Read back a dynamic window prop set with hl-window-prop-set!: #t/#f
   for booleans, numbers for opacities and border/rounding; #f for
   unknown props."
  (c-hl-window-prop (hl--wid w) (hl--str prop)))

(define (hl-window-initial-class w)
  "The class the window opened with (what static rules match on)."
  (c-hl-window-initial-class (hl-window-id w)))

(define (hl-window-initial-title w)
  "The title the window opened with (what static rules match on)."
  (c-hl-window-initial-title (hl-window-id w)))

(define (hl-window-x11? w)
  "#t when the window is an Xwayland window."
  (= 1 (c-hl-window-x11 (hl-window-id w))))

(define (hl-monitors )
  "All monitors, as a list of monitor handles."
  (let ((ids (c-hl-monitor-names)))
    (if (eq? ids #f)
        '()
        (map hl--mint-monitor ids))))

;; ---- events ------------------------------------------------------------------
;; each notification-add! creates an hl-event record (type symbol + handler),
;; hands it to C++ (locked inside the bus connection), and returns the record.
;; handler shapes are per event — see the events wiki page.

;; ---- window events (one C++ entry, a which-number per kind) -------------------
(define (hl--window-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (c-hl-window-event-listen rec which))
        rec
        (errorf who "listener rejected, see compositor log"))))

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
    (if (= 0 (c-hl-window-minimize-listen rec))
        rec
        (errorf 'hl-window-minimize-notification-add! "listener rejected, see compositor log"))))

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
    (if (= 0 (c-hl-lifecycle-listen rec 0))
        rec
        (errorf 'hl-start-notification-add! "listener rejected, see compositor log"))))

(define (hl-shutdown-notification-add! handler)
  "Register a handler that fires once before the session exits."
  (let ((rec (make-hl-event 'shutdown handler)))
    (if (= 0 (c-hl-lifecycle-listen rec 1))
        rec
        (errorf 'hl-shutdown-notification-add! "listener rejected, see compositor log"))))

(define (hl-config-reloaded-notification-add! handler)
  "Register a handler that fires after the config has been reloaded."
  (let ((rec (make-hl-event 'config-reloaded handler)))
    (if (= 0 (c-hl-config-reloaded-listen rec))
        rec
        (errorf 'hl-config-reloaded-notification-add! "listener rejected, see compositor log"))))

(define (hl-config-unload-notification-add! handler)
  "Register a handler that fires BEFORE a config reload."
  (let ((rec (make-hl-event 'config-unload handler)))
    (if (= 0 (c-hl-config-unload-listen rec))
        rec
        (errorf 'hl-config-unload-notification-add! "listener rejected, see compositor log"))))

(define (hl-window-destroy-notification-add! handler)
  "Register a handler that fires when a window is destroyed.
   Zero-argument callback (the bus event is a weak ref; window identity
   belongs to the window-close notification)."
  (let ((rec (make-hl-event 'window-destroy handler)))
    (if (= 0 (c-hl-window-destroy-listen rec))
        rec
        (errorf 'hl-window-destroy-notification-add! "listener rejected, see compositor log"))))

(define (hl-layer-open-notification-add! handler)
  "Register a handler that fires when a layer surface opens. The handler
   receives the layer's namespace string."
  (let ((rec (make-hl-event 'layer-open handler)))
    (if (= 0 (c-hl-layer-listen rec 0))
        rec
        (errorf 'hl-layer-open-notification-add! "listener rejected, see compositor log"))))

(define (hl-layer-close-notification-add! handler)
  "Register a handler that fires when a layer surface closes. The
   handler receives the layer's namespace string."
  (let ((rec (make-hl-event 'layer-close handler)))
    (if (= 0 (c-hl-layer-listen rec 1))
        rec
        (errorf 'hl-layer-close-notification-add! "listener rejected, see compositor log"))))

(define (hl-keyboard-key-notification-add! handler)
  "Register a handler that fires on every key event - high-frequency;
   keep handlers trivial."
  (let ((rec (make-hl-event 'keyboard-key handler)))
    (if (= 0 (c-hl-keyboard-key-listen rec))
        rec
        (errorf 'hl-keyboard-key-notification-add! "listener rejected, see compositor log"))))

(define (hl-screenshare-state-notification-add! handler)
  "Register a handler that fires when a screenshare session starts or
   ends."
  (let ((rec (make-hl-event 'screenshare-state handler)))
    (if (= 0 (c-hl-screenshare-listen rec))
        rec
        (errorf 'hl-screenshare-state-notification-add! "listener rejected, see compositor log"))))

(define (hl-config-props-refreshed-notification-add! handler)
  "Register a handler that fires after the deferred prop-refresh pass
   runs: (lambda (scheduled?) ...) - #t when it ran as scheduled, #f
   when executed prematurely."
  (let ((rec (make-hl-event 'config-props-refreshed handler)))
    (if (= 0 (c-hl-config-props-refreshed-listen rec))
        rec
        (errorf 'hl-config-props-refreshed-notification-add! "listener rejected, see compositor log"))))

(define (hl-submap-notification-add! handler)
  "Register a handler that fires when the active submap changes. The
   handler receives the submap name; an empty string means the default
   submap was restored."
  (let ((rec (make-hl-event 'submap handler)))
    (if (= 0 (c-hl-submap-listen rec))
        rec
        (errorf 'hl-submap-notification-add! "listener rejected, see compositor log"))))

;; ---- workspace events ---------------------------------------------------------
(define (hl-workspace-active-notification-add! handler)
  "Register a handler that fires when the active workspace changes. The
   handler receives the workspace handle."
  (let ((rec (make-hl-event 'workspace-active handler)))
    (if (= 0 (c-hl-workspace-active-listen rec))
        rec
        (errorf 'hl-workspace-active-notification-add! "listener rejected, see compositor log"))))

;; handler signature: (lambda (ws) ...) — ws is a workspace handle
(define (hl--workspace-listen which type thunk who)
  (let ((rec (make-hl-event type thunk)))
    (if (= 0 (c-hl-workspace-event-listen rec which))
        rec
        (errorf who "listener rejected, see compositor log"))))

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
    (if (= 0 (c-hl-monitor-event-listen rec which))
        rec
        (errorf who "listener rejected, see compositor log"))))

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
  (= 1 (c-hl-window-same (hl-window-id a) (hl-window-id b))))

(define (hl-unbind! b)
  "Remove a bind: pass back the hl-bind record hl-bind-add! returned."
  (= 0 (c-hl-unbind-rec b)))

(define (hl-unbind-key! key)
  "Remove EVERY bind whose display key matches KEY (case- and
   whitespace-insensitive, manager-side). For precise removal use
   hl-unbind!."
  (= 0 (c-hl-unbind-key (hl--str key))))

(define (hl-notification-remove! rec)
  "Remove an event listener: drop the connection; the record goes inert.
   #f when already gone."
  (= 0 (c-hl-event-cancel rec)))
(define (hl-notification-active? rec)
  "#t when the event listener is still connected."
  (= 1 (c-hl-event-active rec)))

(define (hl-current-submap )
  "The active submap's name, or the empty string when no submap is
   active."
  (or (c-hl-current-submap) ""))

(define (hl-cursor-pos )
  "The cursor position, as (x . y), or #f."
  (c-hl-cursor-pos))   ; (x . y), or #f

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
  (set! hl--generation (copy-environment (interaction-environment))))

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
  (let ((v (hl--plist-get hl--state k (eof-object))))
    (if (eq? v (eof-object)) (if (null? default) #f (car default)) v)))

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

(define c-hl-handle-free (foreign-procedure "hl-handle-free" (unsigned-64) int))

;; deletes the C++ weak ref behind a dead handle's cell. Allocation-free by
;; design: this runs inside the GC rendezvous, where consing would re-enter
;; the collector.
(define (hl--drain-handles!)
  (let loop ()
    (let ((dead (hl--handle-guardian)))
      (when dead
        (c-hl-handle-free (car dead))
        (loop)))))

;; the Scheme translation of upstream's Lua GC hook: drain dead handles at
;; every GC request, then let the collector proceed.
(collect-request-handler
  (lambda ()
    (hl--drain-handles!)
    (collect)))

(set! hl--ready #t)
