;; hyprscheme/group.scm — the group family — raw wrappers over the compositor's group operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme group)
  #:export (
           hl-group-cycle! hl-group-window-active!
           hl-group-window-move-next! hl-group-lock-all-set! hl-group-lock-all?
           hl-group-lock-set! hl-group-lock?
           hl-group-members hl-group-size
           hl-group-current hl-group-current-index
           hl-group-denied? hl-group-alive? hl-group=? hl-group-add!
           hl-group-remove!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define* (hl-group-cycle! w #:key (prev #f))
  "Switch to the next window in WINDOW's group; #:prev #t goes backwards."
  (= 0 (hl--c-group-cycle w prev)))

(define (hl-group-window-active! w index)
  "Switch to the member at 1-based INDEX in WINDOW's group."
  (= 0 (hl--c-group-index w index)))

(define* (hl-group-window-move-next! w #:key (prev #f))
  "Move WINDOW within its group; #:prev #t moves backwards."
  (= 0 (hl--c-group-move-window w prev)))

;; THE THREE LOCKS, and the names line up three ways:
;;
;;   hl-group-lock? / hl-group-lock-set!              one group, by its handle
;;   hl-group-lock-all? / hl-group-lock-all-set!      every group, compositor-wide
;;   hl-window-group-lock? / hl-window-group-lock-set!  the group a window is in
;;
;; The middle pair used to be hl-groups-locked? / hl-groups-lock-set!, which read
;; as a fourth thing rather than as the "all" scope of the first, and the
;; per-group setter did not exist at all — locking a group meant reaching it
;; through one of its windows, which an empty group cannot offer.

(define* (hl-group-lock-all-set! #:key (on? 'unset))
  "Lock or unlock EVERY group compositor-wide — the compositor's own global
switch, with no window involved, and the reason a lock is a property of the
compositor as well as of a group. #:on? #t locks, #f unlocks, absent toggles.
See hl-group-lock-all?.

For one group use hl-group-lock-set!, which takes the group handle."
  (= 0 (hl--c-group-lock-all-set (hl--bool-act on?))))

(define (hl-group-lock-all?)
  "#t when the compositor-wide group lock is active — the global switch, which
applies to groups regardless of their own lock. See hl-group-lock-all-set!."
  (= 1 (hl--c-group-lock-all)))

(define (hl-group-members g)
  "The group's windows, as a list of window handles."
  (let ((l (hl--c-group-members g)))
    (if l l '())))

(define (hl-group-size g)
  "How many windows the group has."
  (hl--c-group-size g))

(define (hl-group-current g)
  "The group's currently-focused member, as a window handle, or #f."
  (hl--c-group-current g))

(define (hl-group-current-index g)
  "The 1-based position of the group's focused member."
  (hl--c-group-current-idx g))

(define (hl-group-lock? g)
  "#t when THIS group is locked. #:on? #t/#f sets, absent toggles — see
hl-group-lock-set!.

Distinct from hl-group-lock-all?, which is the compositor-wide switch: a group
can be locked on its own with the global one off, and vice versa."
  (eq? (hl--c-group-lock g) #t))

(define* (hl-group-lock-set! g #:key (on? 'unset))
  "Lock or unlock the group G, by its handle. #:on? #t locks, #f unlocks, absent
toggles. Returns #t, or #f when G is not a live group.

The same operation as hl-window-group-lock-set!, which reaches a group through
one of its windows — that one is for a window you already hold, this one for a
group you already hold, and this is the only one that works for a group with no
windows in it. See hl-group-lock? for the read, and hl-group-lock-all-set! for
the compositor-wide switch."
  (= 0 (hl--c-group-lock-set g (hl--bool-act on?))))

(define (hl-group-denied? g)
  "#t when the group denies new members."
  (eq? (hl--c-group-denied g) #t))

(define (hl-group-alive? g)
  "#t when the group still exists (groups dissolve behind your back;
   stale handles report #f)."
  ;; the C entry returns an int (1 alive / 0 gone / -1 before g_up), not a
  ;; scheme boolean — comparing it with eq? #t made this always #f
  (= 1 (hl--c-group-alive g)))

(define (hl-group=? a b)
  "#t when A and B refer to the same underlying group."
  (= 1 (hl--c-group-same a b)))

(define (hl-group-add! g window . index)
  "Add WINDOW to the group G, optionally at a 1-based INDEX. Errors when
   the group denies new members or the window cannot be grouped into it."
  (if (= 0 (hl--c-group-add g window
                           (if (null? index) -1 (inexact->exact (car index)))))
      #t
      (error "hl-group-add!:" (hl--c-config-last-error))))

(define (hl-group-remove! g window)
  "Remove WINDOW from the group G; errors when it is not a member."
  (if (= 0 (hl--c-group-remove g window))
      #t
      (error "hl-group-remove!:" (hl--c-config-last-error))))

