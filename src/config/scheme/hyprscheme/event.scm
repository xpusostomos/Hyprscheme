;; hyprscheme/event.scm — the event family — raw wrappers over the compositor's event operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme event)
  #:export (
           hl-start-notification-add! hl-shutdown-notification-add!
           hl-keyboard-key-notification-add!
           hl-screenshare-state-notification-add!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-start-notification-add! handler)
  "Register a handler that fires once when the session starts (its first
   render frame). Handlers registered after startup fire immediately."
  (let ((rec (make-hl-event 'start handler)))
    (if (= 0 (hl--c-lifecycle-listen rec 0))
        rec
        (hl--error 'hl-start-notification-add! "listener rejected, see compositor log"))))

(define (hl-shutdown-notification-add! handler)
  "Register a handler that fires once before the session exits."
  (let ((rec (make-hl-event 'shutdown handler)))
    (if (= 0 (hl--c-lifecycle-listen rec 1))
        rec
        (hl--error 'hl-shutdown-notification-add! "listener rejected, see compositor log"))))

(define (hl-keyboard-key-notification-add! handler)
  "Register a handler that fires on every key event - high-frequency;
   keep handlers trivial."
  (let ((rec (make-hl-event 'keyboard-key handler)))
    (if (= 0 (hl--c-keyboard-key-listen rec))
        rec
        (hl--error 'hl-keyboard-key-notification-add! "listener rejected, see compositor log"))))

(define (hl-screenshare-state-notification-add! handler)
  "Register a handler that fires when a screenshare session starts or
   ends."
  (let ((rec (make-hl-event 'screenshare-state handler)))
    (if (= 0 (hl--c-screenshare-listen rec))
        rec
        (hl--error 'hl-screenshare-state-notification-add! "listener rejected, see compositor log"))))

