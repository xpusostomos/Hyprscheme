;; hyprscheme/config.scm — the config family — raw wrappers over the compositor's config operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme config)
  #:export (
           refresh-enum hl-refresh
           hl-config-reload! hl-config-add! hl-config-get hl-device-add!
           hl-bezier-curve-add! hl-spring-curve-add! hl-animation-add!
           hl-permission-add!
           hl-config-reloaded-notification-add!
           hl-config-unload-notification-add!
           hl-config-props-refreshed-notification-add!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))
;; enums is a LEAF — it imports the kernel and defines no enum of its own — so a
;; family importing it is leaf-to-leaf, not a family reaching into a family.
;; Needed for refresh-enum and enum-value, below.
(use-modules (hyprscheme enums))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-config-reload! )
  "Reload the configuration, like hyprctl reload."
  (= 0 (hl--c-reload-config)))

;; ---- the deferred refresh pass: which bits, and running it now ----------------
;;
;; THE COMPOSITOR'S OWN SET OF REFRESH BITS, copied line for line from
;; Config/supplementary/propRefresher/PropRefresher.hpp — same names (modulo the
;; case a symbol forces), SAME ORDER, and the same (ash 1 N) rather than a
;; decimal, so the two columns can be diffed by eye. A number like 16 tells you
;; nothing; (ash 1 4) sits next to the header's (1 << 4).
;;
;; IT LIVES HERE, not in (hyprscheme core), because ONE family uses it —
;; hl-refresh, below. core's bind-flag-enum is in core for its own reason
;; (hl--bind-flag-mask is core's own data); the placement rule is "used by one
;; family -> that family", and this set is config's.
;;
;; THE TWO COMPOSITES ARE WRITTEN AS THE HEADER WRITES THEM, and they are not
;; decoration: REFRESH_WINDOW_STATES is (1<<5) | REFRESH_RULES, so asking for
;; "window-states" gets you the rules pass too. Flattening them to bare numbers
;; would hide exactly the thing that makes them worth having, which is that
;; re-decorating windows is a superset of re-applying rules.
;;
;; THERE IS DELIBERATELY NO `all`. The header has REFRESH_ALL =
;; numeric_limits<uint16_t>::max() = #xffff — every bit, including the ones with
;; no meaning — which is a different thing from the union of the named bits.
;; Exposing it under a name that reads like "all the ones listed" would be a
;; trap; a caller who wants everything can name what they want.
(define-enum-values refresh-enum
  `((layouts            . ,(ash 1 0))       ; recalculate + damage every monitor
    (input-devices      . ,(ash 1 1))       ; keyboard layout, pointer/touch/tablet
    (screen-shader      . ,(ash 1 2))       ; reload the screen shader
    (blur-fb            . ,(ash 1 3))       ; rebuild the blur framebuffers
    (rules              . ,(ash 1 4))       ; re-apply the rule engine
    (window-states      . ,(logior (ash 1 5) (ash 1 4)))       ; | REFRESH_RULES
    (monitor-states     . ,(logior (ash 1 6) (ash 1 0)))       ; | REFRESH_LAYOUTS
    (cursor-zooms       . ,(ash 1 7))
    (config-watcher     . ,(ash 1 8))
    (gradients-groupbar . ,(ash 1 9))))

(define* (hl-refresh #:key (layouts #f) (input-devices #f) (screen-shader #f)
                     (blur-fb #f) (rules #f) (window-states #f)
                     (monitor-states #f) (cursor-zooms #f)
                     (config-watcher #f) (gradients-groupbar #f)
                     (immediately #f))
  "Ask the compositor to re-do some of its config-derived work, and optionally
run that pass NOW instead of on the next event-loop tick.

  (hl-refresh #:rules #t)                              ; re-apply rules
  (hl-refresh #:window-states #t #:immediately #t)     ; …and re-decorate the
                                                       ;    windows, straight away

Each option is one bit of the compositor's own refresh set (see refresh-enum
for what each one re-does); any combination is accepted, and the composites
carry their parts — #:window-states implies #:rules, exactly as the C++ does.
COMBINED WITH logior, never +: two options naming the same bit would otherwise
accumulate into the NEXT bit, which is a mistake this tree has already paid for
once (click and drag both imply release, and `+` turned `(hl-bind-add! … #:click
#t #:release #t)` into an armed autorepeat).

NO OPTIONS AT ALL PASSES 0 THROUGH, which the compositor accepts: it arms a tick
that trips no bits, and the pass runs and does nothing. That mirrors the C++
rather than inventing a guard for it.

#:immediately #f (the default) is the compositor's own behaviour — the pass is
DEFERRED, which is what you want when several changes are about to be made, since
the bits accumulate and the pass runs once. #:immediately #t cancels the pending
tick and runs the pass synchronously, which is what you want when the next thing
you do depends on the result. It answers #t when the pass ran, #f when there was
nothing pending to run."
  ;; logior over the chosen bits, each fetched through enum-value so a name that
  ;; is not in the set is an error naming the set rather than a wrong number.
  (let ((bits (logior (if layouts            (enum-value refresh-enum 'layouts)            0)
                      (if input-devices      (enum-value refresh-enum 'input-devices)      0)
                      (if screen-shader      (enum-value refresh-enum 'screen-shader)      0)
                      (if blur-fb            (enum-value refresh-enum 'blur-fb)            0)
                      (if rules              (enum-value refresh-enum 'rules)              0)
                      (if window-states      (enum-value refresh-enum 'window-states)      0)
                      (if monitor-states     (enum-value refresh-enum 'monitor-states)     0)
                      (if cursor-zooms       (enum-value refresh-enum 'cursor-zooms)       0)
                      (if config-watcher     (enum-value refresh-enum 'config-watcher)     0)
                      (if gradients-groupbar (enum-value refresh-enum 'gradients-groupbar) 0))))
    (if (= 0 (hl--c-schedule-refresh bits))
        ;; the flush is the GSUBRS's, not (hyprscheme exec)'s — a family may not
        ;; import another family, and hl-exec-scheduled-prop-refresh-immediately!
        ;; lives there. Calling the entry point directly is the same call that
        ;; wrapper makes, and the only other name it needs is the kernel's.
        (if immediately
            (= 0 (hl--c-scheduled-prop-refresh-immediately))
            #t)
        (error "hl-refresh: the compositor rejected the refresh" bits))))

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

