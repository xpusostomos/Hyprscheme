;; hyprscheme/rule.scm — the rule family — raw wrappers over the compositor's rule operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme rule)
  #:export (
           hl-rule? hl-rule-enabled? hl-rule-enabled-set!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))


;; the rtd argument was missing, and Guile's eq? accepts ONE argument (it
;; returns #t), so this read as "is x any struct at all" — a bind handle, a
;; timer, a window all passed. Compare against the rule type, as hl-bind? does.
(define (hl-rule? x) (and (struct? x) (eq? (struct-vtable x) hl-rule-rtd)))

(define (hl-rule-enabled? rule)
  "#t when RULE is enabled."
  (= 1 (hl--c-rule-enabled rule)))

(define* (hl-rule-enabled-set! rule #:key (on? 'unset))
  "Enable, disable or toggle RULE: absent #:on? toggles, #t/#f set.

IT LIVES HERE, NOT IN (hyprscheme extras), and it was in extras until Chris
asked why: it reaches the compositor directly through hl--c-rule-set-enabled and
otherwise calls only this module's own hl-rule-enabled?. That is a raw wrapper,
not a composition — and the rule for the split is now explicit policy: **if it
reaches Hyprland C or C++, it belongs to the family that owns the object;
extras is for things BUILT ON TOP of the families.** The toggle convention
(absent #:on? means flip) is a POLICY decision and that is why it is Scheme and
not C++ — but policy-in-Scheme is not the same question as family-vs-extras, and
conflating the two is what put it in the wrong module.

The sibling that had it right all along is hl-bind-enabled-set!, in bind.scm."
  (if (= 0 (hl--c-rule-set-enabled rule
                                  (if (eq? on? 'unset)
                                      (not (hl-rule-enabled? rule))
                                      on?)))
      #t
      (error "hl-rule-enabled-set!: unknown rule")))

;; ---- groups as objects (upstream HL.Group parity) ----------------------------
;; a group is the tabbed-window arrangement on a workspace; groups dissolve
;; behind our backs, so handles follow the weak-handle model (stale -> #f).
;; passives only: state reads + two mutators; NO callbacks (upstream parity).

