;; hyprscheme/config.scm — the config family — raw wrappers over the compositor's config operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme config)
  #:export (
           hl-config-reload! hl-config-add! hl-config-get hl-device-add!
           hl-bezier-curve-add! hl-spring-curve-add! hl-animation-add!
           hl-permission-add!
           hl-config-reloaded-notification-add!
           hl-config-unload-notification-add!
           hl-config-props-refreshed-notification-add!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-config-reload! )
  "Reload the configuration, like hyprctl reload."
  (= 0 (hl--c-reload-config)))

(define (hl-config-add! key val)
  "Set the config option KEY to VAL. KEY is the compositor's own config
   path — the string you would write in hyprland.conf — so it keeps the
   compositor's spelling: \"general:gaps_in\" or \"general.gaps_in\",
   with an underscore, NOT a dash. Values: numbers, strings, booleans, lists
   (vec2-style arrays) or a plist for tables (e.g. '(top 10 bottom 10)).
   Writes propagate like a runtime config change - the affected
   subsystems refresh immediately."
  (hl--c-config-begin)
  (hl--push-val val)
  (if (= 0 (hl--c-config-set key))
      #t
      (error "hl-config-add!:" (hl--c-config-last-error))))

(define (hl-config-get key)
  "Return the current value of the config option KEY, as Scheme data
   (number, string, boolean, or a plist for tables)."
  (hl--c-config-get key))   ; #t/#f, number, string, or a plist for tables

(define* (hl-device-add! name #:key #:rest fields)
  "Configure a device: NAME plus keyword fields, e.g.
   (hl-device-add! \"tablet\" #:repeat-rate 25 #:natural-scroll #t).
   Write-only (no device read side, no per-field unset); validated
   against the field table and applied to the live device. Unknown
   keywords are rejected by the field table. Scheme spells the fields
   with dashes; the compositor's own underscore spelling is accepted too."
  (let ((plist (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)))
    (if (= 0 (hl--c-device-add name plist))
        #t
        (error "hl-device-add!:" (hl--c-config-last-error)))))

(define (hl-bezier-curve-add! name x1 y1 x2 y2)
  "Declare a BEZIER animation curve from its two control points:
   (hl-bezier-curve-add! \"mycurve\" 0.25 0.1 0.25 1.0)."
  (if (= 0 (hl--c-bezier-curve-add name x1 y1 x2 y2))
      #t
      (error "hl-bezier-curve-add!:" (hl--c-config-last-error))))

(define (hl-spring-curve-add! name stiffness damping mass)
  "Declare a SPRING animation curve: (hl-spring-curve-add! \"myspring\"
   250 25 1). Stiffness, damping and mass must all be >= 0.5."
  (if (= 0 (hl--c-spring-curve-add name stiffness damping mass))
      #t
      (error "hl-spring-curve-add!:" (hl--c-config-last-error))))

(define* (hl-animation-add! leaf #:key (enabled #t) (speed 8) (curve "") (style ""))
  "Configure an animation: (hl-animation-add! \"windowsIn\" #:enabled #t
   #:speed 8 #:curve \"mycurve\" #:style \"popin 80%\"). Speed defaults
   to 8; declare curves with hl-bezier-curve-add! first (or use builtins like
   \"default\")."
  (let* ((enabled enabled)
         (speed   speed)
         (curve   curve)
         (style   style))
    (if (= 0 (hl--c-animation-set leaf enabled
                                 (exact->inexact speed) curve style))
        #t
        (error "hl-animation-add!:" (hl--c-config-last-error)))))

(define (hl-permission-add! binary type mode)
  "Set a permission rule: (hl-permission-add! \"/usr/bin/grim\"
   'screencopy 'allow). Only takes effect at first launch - permission
   rules require a compositor restart."
  (if (= 0 (hl--c-permission-add binary type mode))
      #t
      (error "hl-permission-add!:" (hl--c-config-last-error))))

(define (hl-config-reloaded-notification-add! handler)
  "Register a handler that fires after the config has been reloaded."
  (let ((rec (make-hl-event 'config-reloaded handler)))
    (if (= 0 (hl--c-config-reloaded-listen rec))
        rec
        (error "hl-config-reloaded-notification-add!: listener rejected, see compositor log"))))

(define (hl-config-unload-notification-add! handler)
  "Register a handler that fires BEFORE a config reload."
  (let ((rec (make-hl-event 'config-unload handler)))
    (if (= 0 (hl--c-config-unload-listen rec))
        rec
        (error "hl-config-unload-notification-add!: listener rejected, see compositor log"))))

(define (hl-config-props-refreshed-notification-add! handler)
  "Register a handler that fires after the deferred prop-refresh pass
   runs: (lambda (scheduled?) ...) - #t when it ran as scheduled, #f
   when executed prematurely."
  (let ((rec (make-hl-event 'config-props-refreshed handler)))
    (if (= 0 (hl--c-config-props-refreshed-listen rec))
        rec
        (error "hl-config-props-refreshed-notification-add!: listener rejected, see compositor log"))))

