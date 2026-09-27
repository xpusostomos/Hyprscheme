;; hyprscheme.scm — the PUBLIC API: `(hyprscheme)`.
;;
;; A curated re-export of the public hl-* definitions from the family modules.
;; The families hold the raw API (one compositor operation each, plus argument
;; tolerance); (hyprscheme extras) holds the few conveniences that compose
;; several calls; (hyprscheme core) holds the shared plumbing.
;; THIS LIST IS THE SOURCE OF TRUTH for "what is public": tests/t-coverage.sh
;; reads it, and a generated reference or `,apropos` would too. Internal hl--
;; helpers are deliberately absent — add a name here only when it is meant for
;; configs.
;;
;; The generation a config runs in does NOT import this module: it imports the
;; family modules and the kernel directly, so internals stay reachable there.

(define-module (hyprscheme)
  ;; the names below must already be IN this module: #:re-export re-exports
  ;; what the module has imported, it does not go looking for them
  #:use-module (hyprscheme bind)
  #:use-module (hyprscheme config)
  #:use-module (hyprscheme core)
  #:use-module (hyprscheme event)
  #:use-module (hyprscheme exec)
  #:use-module (hyprscheme extras)
  #:use-module (hyprscheme gesture)
  #:use-module (hyprscheme group)
  #:use-module (hyprscheme layer)
  #:use-module (hyprscheme monitor)
  #:use-module (hyprscheme notification)
  #:use-module (hyprscheme query)
  #:use-module (hyprscheme rule)
  #:use-module (hyprscheme timer)
  #:use-module (hyprscheme window)
  #:use-module (hyprscheme workspace)
  #:use-module (hyprscheme kernel)
  #:re-export (
    hl-active-monitor hl-active-special-workspace hl-active-title
    hl-active-window hl-active-workspace hl-after hl-animation-add!
    hl-bind-add! hl-bind-thunk hl-bind-tokens hl-bind? hl-box hl-box-h
    hl-box-w hl-box-x hl-box-y hl-clear-crashed-lockscreen!
    hl-config-add! hl-config-get
    hl-config-props-refreshed-notification-add! hl-config-reload!
    hl-config-reloaded-notification-add!
    hl-config-unload-notification-add! hl-current-submap hl-cursor-move!
    hl-cursor-move-to-corner! hl-cursor-pos hl-curve-add! hl-device-add!
    hl-event! hl-event? hl-exec!
    hl-exec-scheduled-prop-refresh-immediately hl-exit!
    hl-focus-direction-set! hl-focus-last! hl-focus-urgent!
    hl-force-idle! hl-force-renderer-reload! hl-gesture-action?
    hl-gesture-add! hl-gesture-remove! hl-gesture? hl-global!
    hl-group-add! hl-group-alive? hl-group-current hl-group-current-index
    hl-group-cycle! hl-group-denied? hl-group-locked? hl-group-members
    hl-group-remove! hl-group-size hl-group-window-active!
    hl-group-window-move-next! hl-group=? hl-group? hl-groups-lock-set!
    hl-groups-locked? hl-is-key-down hl-kbd hl-key
    hl-keyboard-key-notification-add! hl-last-window hl-last-workspace
    hl-layer-above-fullscreen? hl-layer-address hl-layer-alive?
    hl-layer-close-notification-add! hl-layer-kb-interactivity
    hl-layer-level hl-layer-mapped? hl-layer-monitor hl-layer-namespace
    hl-layer-open-notification-add! hl-layer-pid hl-layer-position
    hl-layer-rule-add! hl-layer-size hl-layer=? hl-layer? hl-layers
    hl-layout-add! hl-layout-msg hl-loaded-plugins hl-make-close-gesture
    hl-make-cursor-zoom-gesture hl-make-custom-gesture
    hl-make-float-gesture hl-make-fullscreen-gesture hl-make-move-gesture
    hl-make-resize-gesture hl-make-scroll-move-gesture
    hl-make-special-workspace-gesture hl-make-workspace-swipe-gesture
    hl-monitor-10bit? hl-monitor-active-special-workspace
    hl-monitor-active-workspace hl-monitor-added-notification-add!
    hl-monitor-alive? hl-monitor-at hl-monitor-at-cursor
    hl-monitor-available-modes hl-monitor-description hl-monitor-enabled?
    hl-monitor-focus! hl-monitor-focused-notification-add!
    hl-monitor-focused? hl-monitor-from hl-monitor-hardware-details
    hl-monitor-height hl-monitor-layout-changed-notification-add!
    hl-monitor-mirror-of hl-monitor-mirrors hl-monitor-mode
    hl-monitor-name hl-monitor-number hl-monitor-physical-size
    hl-monitor-power-set! hl-monitor-power? hl-monitor-refresh-rate
    hl-monitor-removed-notification-add! hl-monitor-reserved
    hl-monitor-rule-add! hl-monitor-scale hl-monitor-serial
    hl-monitor-swap! hl-monitor-transform hl-monitor-vrr?
    hl-monitor-width hl-monitor-workspace-special-set! hl-monitor-x
    hl-monitor-y hl-monitor=? hl-monitor? hl-monitors hl-mouse-action!
    hl-notification-active? hl-notification-add! hl-notification-age
    hl-notification-alive? hl-notification-color
    hl-notification-color-set! hl-notification-dismiss!
    hl-notification-elapsed hl-notification-font-size
    hl-notification-font-size-set! hl-notification-icon
    hl-notification-icon-set! hl-notification-paused-set!
    hl-notification-paused? hl-notification-remove! hl-notification-text
    hl-notification-text-set! hl-notification-timeout
    hl-notification-timeout-set! hl-notification=? hl-notification?
    hl-notifications hl-notify! hl-permission-add! hl-plist-get
    hl-release-input-capture! hl-repeat hl-rule-enabled-set!
    hl-rule-enabled? hl-rule? hl-screenshare-state-notification-add!
    hl-shutdown-notification-add! hl-start-notification-add!
    hl-state-keys hl-state-ref hl-state-remove! hl-state-set! hl-submap
    hl-submap-activate! hl-submap-exit! hl-submap-notification-add!
    hl-timer-cancel! hl-timer-enabled-set! hl-timer-enabled?
    hl-timer-set-timeout hl-timer? hl-unbind! hl-unbind-key!
    hl-urgent-window hl-version hl-window-accepts-input?
    hl-window-active-notification-add! hl-window-address hl-window-alive?
    hl-window-allowed-over-fullscreen? hl-window-bell-notification-add!
    hl-window-center! hl-window-class hl-window-class-notification-add!
    hl-window-close! hl-window-close-notification-add!
    hl-window-content-type hl-window-cycle!
    hl-window-deny-from-group-set! hl-window-deny-from-group?
    hl-window-destroy-notification-add! hl-window-float-set!
    hl-window-floating? hl-window-focus! hl-window-focus-history-id
    hl-window-from hl-window-fullscreen-handler hl-window-fullscreen-mode
    hl-window-fullscreen-notification-add! hl-window-fullscreen-set!
    hl-window-fullscreen-state hl-window-group hl-window-group-lock-set!
    hl-window-group-lock? hl-window-group-move-in!
    hl-window-group-move-in-or-create! hl-window-group-move-out!
    hl-window-group-set! hl-window-group? hl-window-hidden?
    hl-window-inhibiting-idle? hl-window-initial-class
    hl-window-initial-title hl-window-kill!
    hl-window-kill-notification-add! hl-window-layout hl-window-mapped?
    hl-window-maximized-set! hl-window-maximized?
    hl-window-minimize-notification-add! hl-window-monitor
    hl-window-monitor-set! hl-window-move-direction!
    hl-window-move-to-workspace-notification-add!
    hl-window-open-early-notification-add!
    hl-window-open-notification-add! hl-window-pass-shortcut!
    hl-window-pid hl-window-pin-fullscreened?
    hl-window-pin-notification-add! hl-window-pinned-set!
    hl-window-pinned? hl-window-position hl-window-position-set!
    hl-window-prop hl-window-prop-set! hl-window-pseudo-set!
    hl-window-pseudo? hl-window-rule-add! hl-window-send-key-state!
    hl-window-send-shortcut! hl-window-signal! hl-window-size
    hl-window-size-set! hl-window-stable-id hl-window-swallow-toggle!
    hl-window-swallowing hl-window-swap-direction! hl-window-swap-next!
    hl-window-swap-with! hl-window-tag-add! hl-window-tags
    hl-window-tags-clear! hl-window-tearing-hint? hl-window-title
    hl-window-title-notification-add!
    hl-window-update-rules-notification-add!
    hl-window-urgent-notification-add! hl-window-visible?
    hl-window-workspace hl-window-workspace-set! hl-window-x11?
    hl-window-xdg-description hl-window-xdg-tag hl-window-zorder-set!
    hl-window=? hl-window? hl-windows hl-windows-from
    hl-workspace-active-notification-add! hl-workspace-active?
    hl-workspace-addressable-name hl-workspace-alive?
    hl-workspace-created-notification-add! hl-workspace-empty?
    hl-workspace-focus! hl-workspace-from hl-workspace-fullscreen-mode
    hl-workspace-fullscreen-window hl-workspace-group-count
    hl-workspace-groups hl-workspace-has-fullscreen?
    hl-workspace-has-urgent? hl-workspace-id-set!
    hl-workspace-last-window hl-workspace-monitor
    hl-workspace-monitor-set!
    hl-workspace-move-to-monitor-notification-add! hl-workspace-name
    hl-workspace-name-set! hl-workspace-number hl-workspace-persistent?
    hl-workspace-removed-notification-add! hl-workspace-rule-add!
    hl-workspace-special-active-notification-add!
    hl-workspace-special-set! hl-workspace-special-toggle!
    hl-workspace-special? hl-workspace-tiled-layout hl-workspace-visible?
    hl-workspace-window-count hl-workspace-windows hl-workspace=?
    hl-workspace? hl-workspaces))

