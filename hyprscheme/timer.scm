;; hyprscheme/timer.scm — the timer family — raw wrappers over the compositor's timer operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme timer)
  #:export (
           hl-timer? hl-timer-enabled? hl-timer-enabled-set!
           hl-timer-set-timeout! hl-timer-cancel!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-timer? x) (and (struct? x) (eq? (struct-vtable x) hl-timer-rtd)))

(define (hl-timer-enabled? t)
  "#t when the timer T is enabled."
  (= 1 (hl--c-timer-enabled t)))

(define* (hl-timer-enabled-set! t #:key (on? 'unset))
  "Enable, disable or toggle the timer T: absent #:on? toggles, #t/#f set.

IT LIVES HERE, NOT IN (hyprscheme extras): it reaches the compositor through
hl--c-timer-set-enabled and otherwise calls only this module's own
hl-timer-enabled?. See hl-rule-enabled-set! in rule.scm for the full note — the
rule is that whatever reaches Hyprland C or C++ belongs to the family that owns
the object, and extras is for things built on top of the families."
  (if (= 0 (hl--c-timer-set-enabled t
                                   (if (eq? on? 'unset)
                                       (not (hl-timer-enabled? t))
                                       on?)))
      #t
      (error "hl-timer-enabled-set!: unknown timer")))

(define (hl-timer-set-timeout! t ms)
  "Re-tune the timer T's timeout to MS (>= 1)."
  (if (= 0 (hl--c-timer-set-timeout t (exact->inexact ms)))
      #t
      (error "hl-timer-set-timeout!: timeout must be >= 1ms")))

(define (hl-timer-cancel! t)
  "Cancel a repeating timer before reload (beyond-upstream extension).
   The index entry erases itself; the record lock releases with the
   timer."
  (= 0 (hl--c-timer-cancel t)))


;; ---- gestures ----------------------------------------------------------------------
;;

