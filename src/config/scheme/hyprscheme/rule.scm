;; hyprscheme/rule.scm — the rule family — raw wrappers over the compositor's rule operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme rule)
  #:export (
           hl-rule? hl-rule-enabled?))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))


(define (hl-rule? x) (and (struct? x) (eq? (struct-vtable x) )))

(define (hl-rule-enabled? rule)
  "#t when RULE is enabled."
  (= 1 (hl--c-rule-enabled rule)))

;; ---- groups as objects (upstream HL.Group parity) ----------------------------
;; a group is the tabbed-window arrangement on a workspace; groups dissolve
;; behind our backs, so handles follow the weak-handle model (stale -> #f).
;; passives only: state reads + two mutators; NO callbacks (upstream parity).

