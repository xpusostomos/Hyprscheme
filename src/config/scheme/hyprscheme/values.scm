;; hyprscheme/values.scm — the CONCRETE enums the API speaks, where more than
;; one family needs them.
;;
;; (hyprscheme enums) is the MACHINERY: make-enum-values, enum-value,
;; enum-symbol and the record they work on. It holds no enum of its own, and it
;; should not: a facility that names a kind of value wants no opinion about
;; which values exist.
;;
;; An enum used by ONE family lives with that family — `fullscreen-mode` is in
;; window.scm, next to every caller. This module is for the other case: a set
;; that TWO families must agree on. `direction` is the example that created it —
;; (hyprscheme window) takes it for the window-move and group-move dispatchers
;; and (hyprscheme exec) for the focus dispatcher — and a family may not import
;; another family, so it can live in neither. kernel, core, enums and this
;; module are the four things a family may import.
;;
;; A LEAF, like core and enums: it imports the kernel (for nothing but the
;; module convention) and enums (the macro), and holds no compositor knowledge.
;; The integers below come from the compositor, but that is data, not a call
;; into it — nothing here reaches C++.
;;
;; WHY ENUMS AND NOT THE STRINGS. Every one of these sets is, in C++, a bare
;; integer with the meaning recorded in a comment, and in the scripting API a
;; bare string parsed by hand at each call site. Both let a value outside the
;; set through. A name-out-of-a-fixed-set is what an enum is for: the caller
;; writes 'right, the API answers 'right, and anything else is refused with the
;; set named rather than read as something else.
;;
;; That is not only tidiness. `direction` is also how a compositor crash was
;; reachable: the old code passed the caller's string down to
;; CLayoutManager::moveInDirection, which asks the derived word for its FIRST
;; CHARACTER ("left" -> "l", and "" -> a throw from .at(0) that aborted the
;; process). A direction that is an enum never becomes a string at all.

(define-module (hyprscheme values)
  #:export (
           direction-enum direction-or-none-enum
           corner-enum key-state-enum))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme enums))

;; Which way: up, right, down or left.
;;
;; The integers are the compositor's own Math::eDirection, and the declaration
;; order is the compositor's too, so the rnrs enum's indices coincide with them
;; here. 'none does NOT belong to this set: a function taking direction-enum
;; must refuse it, and only the one caller that has a directionless case takes
;; direction-or-none-enum instead.
;;
;; No abbreviations. The compositor also accepts "l"/"r"/"u"/"d"/"t"/"b" as
;; FIRST LETTERS of the string, which is how "lizard" came to mean "left" — a
;; permissive spelling rule that no longer applies now a value cannot be an
;; arbitrary string.
(define-enum-values direction-enum
  '((up    . 0)
    (right . 1)
    (down  . 2)
    (left  . 3)))

;; The same four, plus 'none for "no direction at all".
;;
;; This exists because one dispatcher genuinely has a directionless form:
;; moving a window out of a group can drop it towards a named direction or just
;; release it, and the compositor distinguishes the two (Math::DIRECTION_DEFAULT
;; — the CGroup::remove path tests for it explicitly). `none` is that value, and
;; it is a VALUE OF THE ENUM rather than an out-of-band #f so that the caller
;; writes one kind of thing in one argument: 'none, or a direction.
;;
;; A separate set from direction-enum on purpose. They are different questions —
;; "which way" and "which way, or not at all" — and a function that takes the
;; first must not accept 'none by accident.
(define-enum-values direction-or-none-enum
  '((up    . 0)
    (right . 1)
    (down  . 2)
    (left  . 3)
    (none  . -1)))

;; Which corner of a window, for the cursor warp.
;;
;; The integers are the compositor's, from the switch in
;; Actions::moveCursorToCorner, and they are NOT in reading order: the arms run
;; bottom-left, bottom-right, top-right, top-left. That is worth keeping exactly
;; as it is — the point of an enum is that the caller never sees the number, so
;; the number's oddity is harmless while getting it wrong would place the cursor
;; in the wrong corner.
;;
;; The NAMES come from the compositor's eRectCorner, which spells the four that
;; way (CORNER_BOTTOMLEFT, CORNER_TOPRIGHT, ...). Its numbers are bit flags and
;; do NOT apply here — a different corner set, used by the custom-layout resize
;; callback, which is exposed as a bare int and is not this enum's business.
(define-enum-values corner-enum
  '((bottom-left  . 0)
    (bottom-right . 1)
    (top-right    . 2)
    (top-left     . 3)))

;; Which state of a key press, for the synthetic key senders.
;;
;; The integers are the compositor's, named in a comment beside
;; Actions::sendKeyState's parameter: 0 = up, 1 = down, 2 = repeat. "repeat" is
;; not an autorepeat flag but a second press — the action calls pass() twice for
;; it, which is how a synthetic autorepeat is spelled.
;;
;; The scripting API takes these as the strings "down"/"up"/"repeat" and maps
;; them in its dispatcher, which is the same shape as this enum one level up:
;; a name for a fixed set, converted at exactly one place.
(define-enum-values key-state-enum
  '((up     . 0)
    (down   . 1)
    (repeat . 2)))
