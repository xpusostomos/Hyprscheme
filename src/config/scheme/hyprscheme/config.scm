;; hyprscheme/config.scm — the config family — raw wrappers over the compositor's config operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme config)
  #:export (
           hl-config-reload! hl-config-add! hl-config-get hl-device-add!
           hl-curve-add! hl-animation-add! hl-permission-add!
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

(define (hl-config-props-refreshed-notification-add! handler)
  "Register a handler that fires after the deferred prop-refresh pass
   runs: (lambda (scheduled?) ...) - #t when it ran as scheduled, #f
   when executed prematurely."
  (let ((rec (make-hl-event 'config-props-refreshed handler)))
    (if (= 0 (hl--c-config-props-refreshed-listen rec))
        rec
        (hl--error 'hl-config-props-refreshed-notification-add! "listener rejected, see compositor log"))))

