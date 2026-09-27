;; hyprscheme/group.scm — the group family — raw wrappers over the compositor's group operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme group)
  #:export (
           hl-group-cycle! hl-group-window-active!
           hl-group-window-move-next! hl-groups-lock-set!
           hl-groups-locked? hl-group-members hl-group-size
           hl-group-current hl-group-current-index hl-group-locked?
           hl-group-denied? hl-group-alive? hl-group=? hl-group-add!
           hl-group-remove!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-group-cycle! w . opt)
  "Switch to the next window in WINDOW's group; 'prev goes backwards."
  (= 0 (hl--c-group-cycle w (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

(define (hl-group-window-active! w index)
  "Switch to the member at 1-based INDEX in WINDOW's group."
  (= 0 (hl--c-group-index w index)))

(define (hl-group-window-move-next! w . opt)
  "Move WINDOW within its group; 'prev moves backwards."
  (= 0 (hl--c-group-move-window w (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

(define* (hl-groups-lock-set! #:key (on? 'unset))
  "Lock or unlock ALL groups compositor-wide (no window involved). See
   hl-groups-locked?."
  (= 0 (hl--c-group-lock (hl--bool-act on?))))

(define (hl-groups-locked? )
  "#t when the compositor-wide group lock is active."
  (= 1 (hl--c-groups-locked)))

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

(define (hl-group-locked? g)
  "#t when the group is locked."
  (eq? (hl--c-group-locked g) #t))

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
      (hl--error 'hl-group-add! "~a" (hl--c-config-last-error))))

(define (hl-group-remove! g window)
  "Remove WINDOW from the group G; errors when it is not a member."
  (if (= 0 (hl--c-group-remove g window))
      #t
      (hl--error 'hl-group-remove! "~a" (hl--c-config-last-error))))

