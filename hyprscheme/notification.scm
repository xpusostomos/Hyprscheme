;; hyprscheme/notification.scm — the notification family — raw wrappers over the compositor's notification operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme notification)
  #:export (
           hl-notify! hl-notification-add! hl-notifications
           hl-notification-text hl-notification-timeout
           hl-notification-color hl-notification-icon
           hl-notification-font-size hl-notification-elapsed
           hl-notification-age hl-notification-alive? hl-notification=?
           hl-notification-text-set! hl-notification-timeout-set!
           hl-notification-color-set! hl-notification-icon-set!
           hl-notification-font-size-set! hl-notification-paused?
           hl-notification-paused-set!
           hl-notification-dismiss! hl-notification-remove!
           hl-notification-active?))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define* (hl-notify! text duration #:key (icon "none") (color #f) (font-size 13))
  "Show a one-shot notification: (hl-notify! \"text\" 5000), or with
   keyword options: (hl-notify! \"text\" 5000 #:icon \"info\" #:color
   #xffffa500 #:font-size 13). Icons: none warn info hint error
   confused ok.

   #:color is a 0xAARRGGBB integer — ALPHA FIRST, then red, green and
   blue, the packed-ARGB32 layout the compositor uses. Scheme has hex
   literals, so it is written as one: dark orange is #xffa500, and
   opaque dark orange is #xffffa500. An alpha of 00 is fully
   transparent; the usual mistake is writing the RGB triplet first
   (#xffa500ff is blue, not orange).

   #:color #f (the default) means the icon's own colour, not black."
  (let ((s (hl--c-notify text (exact->inexact duration)
                        icon
                        color
                        (exact->inexact font-size))))
    (if s #t #f)))

;; ---- live notification objects (upstream hl.notification parity) ------------
;; (hl-notification-add! 'text "…" 'timeout ms ['icon "info"] ['color "…"]
;;   ['font-size n]) => a notification HANDLE. 'timeout is required; pause
;; freezes the timeout timer (the bubble stays until dismissed); expired
;; handles read #f and mutate as no-ops.

(define* (hl-notification-add! #:key text timeout icon color font-size)
  "Create a live notification and return its handle. #:text (string)
   and #:timeout (ms) are required; #:icon, #:color and #:font-size are
   optional. Omitted keywords are sent as absent, and the notification
   subsystem applies its own defaults (icon \"none\", color 0, font
   size 13) — Scheme holds no defaults here. Pausing freezes the
   timeout timer (the bubble stays until dismissed); expired handles
   read #f and mutate as no-ops.

   #:color is a 0xAARRGGBB integer, alpha first — #xffffa500, not
   #xffa500ff. It is the same number hl-notification-color gives back,
   so a colour can be read and written without converting anything."
  (let ((fields (append
                  (if text (list 'text text) '())
                  (if timeout (list 'timeout timeout) '())
                  (if icon (list 'icon icon) '())
                  (if color (list 'color color) '())
                  (if font-size (list 'font-size font-size) '()))))
    (let ((n (hl--c-notification-add fields)))
      (if n
          n
          (error "hl-notification-add!:" (hl--c-config-last-error))))))

(define (hl-notifications )
  "The live notifications, as a list of handles."
  (or (hl--c-notification-list) '()))

(define (hl-notification-text n)
  "The notification's text."
  (hl--c-notification-text n))

(define (hl-notification-timeout n)
  "The notification's timeout, in ms."
  (hl--c-notification-timeout n))

(define (hl-notification-color n)
  "The notification's color, as the raw 0xAARRGGBB integer — the same
   number hl-notification-add! and hl-notification-color-set! take, so
   it round-trips."
  (hl--c-notification-color n))

(define (hl-notification-icon n)
  "The notification's icon, as its hyprctl id (names exist on the write
   side only)."
  (hl--c-notification-icon n))

(define (hl-notification-font-size n)
  "The notification's font size."
  (hl--c-notification-font-size n))

(define (hl-notification-elapsed n)
  "How long the notification has been live, in ms, excluding paused
   spans."
  (hl--c-notification-elapsed n))

(define (hl-notification-age n)
  "Wall time since the notification was created, in ms."
  (hl--c-notification-age n))

(define (hl-notification-alive? n)
  "#t when the notification is still live."
  (eq? (hl--c-notification-alive n) #t))

(define (hl-notification=? a b)
  "#t when A and B are the same notification."
  (eq? (hl--c-notification-same a b) #t))

;; failed setters raise with the config system's own message

(define (hl-notification-text-set! n text)
  "Set the notification's text."
  (hl--notif-act 'hl-notification-text-set!
    (hl--c-notification-text-set n text)))

(define (hl-notification-timeout-set! n ms)
  "Set the notification's timeout, in ms."
  (hl--notif-act 'hl-notification-timeout-set!
    (hl--c-notification-timeout-set n (exact->inexact ms))))

(define (hl-notification-color-set! n color)
  "Set the notification's color, as a 0xAARRGGBB integer (alpha first,
   e.g. #xffffa500). Takes the same number hl-notification-color
   returns."
  (hl--notif-act 'hl-notification-color-set!
    (hl--c-notification-color-set n color)))

(define (hl-notification-icon-set! n icon)
  "Set the notification's icon (icon name, e.g. \"warn\")."
  (hl--notif-act 'hl-notification-icon-set!
    (hl--c-notification-icon-set n icon)))

(define (hl-notification-font-size-set! n size)
  "Set the notification's font size."
  (hl--notif-act 'hl-notification-font-size-set!
    (hl--c-notification-font-size-set n (exact->inexact size))))

(define (hl-notification-paused? n)
  "#t when the notification is paused (its timeout timer frozen)."
  (eq? (hl--c-notification-paused-q n) #t))

(define* (hl-notification-paused-set! n #:key (on? 'unset))
  "Pause or resume the notification's timeout timer: absent #:on? toggles,
#t/#f set. A paused notification stays on screen until dismissed, which is
what makes it usable as a pin.

IT LIVES HERE, NOT IN (hyprscheme extras): it reaches the compositor through
hl--c-notification-paused-set and otherwise calls only this module's own
hl-notification-paused?. See hl-rule-enabled-set! in rule.scm for the full
note — whatever reaches Hyprland C or C++ belongs to the family that owns the
object, and extras is for things built on top of the families."
  (= 0 (hl--c-notification-paused-set
         n
         (if (eq? on? 'unset)
             (not (eq? (hl-notification-paused? n) #t))
             (eq? on? #t)))))

(define (hl-notification-dismiss! n)
  "Dismiss the notification."
  (= 0 (hl--c-notification-dismiss n)))

;; ---- timer handles --------------------------------------------------------------

(define (hl-notification-remove! rec)
  "Remove an event listener: drop the connection; the record goes inert.
   #f when already gone."
  (= 0 (hl--c-event-cancel rec)))

(define (hl-notification-active? rec)
  "#t when the event listener is still connected."
  (= 1 (hl--c-event-active rec)))

