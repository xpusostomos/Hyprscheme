;; hyprscheme/timer.scm — the timer family — raw wrappers over the compositor's timer operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme timer)
  #:export (
           hl-timer? hl-timer-enabled? hl-timer-set-timeout
           hl-timer-cancel!))

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

(define (hl-timer-set-timeout t ms)
  "Re-tune the timer T's timeout to MS (>= 1)."
  (if (= 0 (hl--c-timer-set-timeout t (exact->inexact ms)))
      #t
      (hl--error 'hl-timer-set-timeout "timeout must be >= 1ms")))

(define (hl-timer-cancel! t)
  "Cancel a repeating timer before reload (beyond-upstream extension).
   The index entry erases itself; the record lock releases with the
   timer."
  (= 0 (hl--c-timer-cancel t)))


;; ---- gestures ----------------------------------------------------------------------
;;

