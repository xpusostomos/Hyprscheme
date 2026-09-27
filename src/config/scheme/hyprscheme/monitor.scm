;; hyprscheme/monitor.scm — the monitor family — raw wrappers over the compositor's monitor operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme monitor)
  #:export (
           hl-monitor-focus! hl-monitor-workspace-special-set!
           hl-monitor-swap! hl-monitor-power-set! hl-monitor-from
           hl-monitor-at hl-monitor-at-cursor hl-monitor-name
           hl-monitor-description hl-monitor-number hl-monitor-enabled?
           hl-monitor-focused? hl-monitor-x hl-monitor-y hl-monitor-width
           hl-monitor-height hl-monitor-scale hl-monitor-transform
           hl-monitor-refresh-rate hl-monitor-mode hl-monitor-power?
           hl-monitor-vrr? hl-monitor-10bit? hl-monitor-reserved
           hl-monitor-serial hl-monitor-physical-size hl-monitor-mirrors
           hl-monitor-available-modes hl-monitor-hardware-details
           hl-monitor-mirror-of hl-monitor-active-workspace
           hl-monitor-active-special-workspace hl-monitor-alive?
           hl-monitor=? hl-monitor-rule-add! hl-monitors
           hl-monitor-added-notification-add!
           hl-monitor-removed-notification-add!
           hl-monitor-focused-notification-add!
           hl-monitor-layout-changed-notification-add!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-monitor-focus! mon)
  "Move the focus to monitor MON."
  (= 0 (hl--c-focus-monitor mon)))

(define (hl-monitor-workspace-special-set! mon ws)
  "Open the special workspace WS on MON (created when missing); #f
   closes whatever special workspace is open there. WS must be named - a
   closed monitor keeps no record of its last special workspace."
  (= 0 (hl--c-monitor-set-special (hl--mon-arg mon) (if ws (hl--ws-arg ws) ""))))

(define (hl-monitor-swap! mon1 mon2)
  "Swap the current workspaces of monitors MON1 and MON2."
  (= 0 (hl--c-workspace-swap-monitors mon1 mon2)))

(define* (hl-monitor-power-set! mon #:key (on? 'unset))
  "Switch MON's power (a monitor, or #f for all monitors): absent #:on?
   toggles, #t on, #f off."
  (= 0 (hl--c-dpms (hl--bool-act on?) (if mon mon #f))))

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

(define (hl-monitor=? a b)
  "#t when A and B refer to the same monitor."
  (eq? (hl--c-monitor-same a b) #t))

;; => list of (monitor . namespace) pairs
;; ---- layer surfaces as objects (upstream HL.LayerSurface parity) -------------
;; layer surfaces die independently -> weak handles via the guardian;
;; every getter returns #f when the surface is gone. NO callbacks.
;; filters are plist-style: (hl-layers), (hl-layers 'monitor MON),
;; (hl-layers 'namespace "ns"), or combined.

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

(define (hl-monitors )
  "All monitors, as a list of monitor handles."
  (or (hl--c-monitor-names) '()))

;; ---- events ------------------------------------------------------------------
;; each notification-add! creates an hl-event record (type symbol + handler),
;; hands it to C++ (locked inside the bus connection), and returns the record.
;; handler shapes are per event — see the events wiki page.

;; ---- window events (one C++ entry, a which-number per kind) -------------------

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

