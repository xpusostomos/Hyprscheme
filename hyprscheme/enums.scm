;; hyprscheme/enums.scm — a NAME for a set of values
;;
;; Wherever the API takes or returns one value out of a fixed set — a window's
;; fullscreen mode, the handler that owns its fullscreen — that set is a NAMED
;; ENUM rather than a bare integer or a string. The caller writes 'maximized and
;; the API answers 'maximized, and a value outside the set is REFUSED instead of
;; being read as something else. (A bare 1 is a number; 'maximized cannot be
;; misread, and a wrong one is an error that names the set.)
;;
;; RULE, and it is a rule rather than a preference: a function that takes or
;; returns one value from a fixed set speaks in these symbols. It does not take
;; an integer, and it does not return a string. See CLAUDE.md, "Enums".
;;
;; A LEAF module, like core: it imports the kernel and nothing else of ours, and
;; calls nothing another family owns. Families may import it (module-audit
;; allows kernel, core and enums) because it is shared plumbing of the same kind
;; core is — it holds data and no compositor knowledge.

(define-module (hyprscheme enums)
  #:export (
           define-enum-values enum-symbol enum-value enum-values?
           enum-values->enum hl--enum-alist hl-enum-values-rtd make-enum-values))

(use-modules (hyprscheme kernel))

(use-modules (ice-9 exceptions)
             (rnrs enums)
             (rnrs lists)
             (srfi srfi-1))

;; An <enum-values> carries TWO things:
;;
;;   - the ass list of symbol -> integer, which is what the API speaks and what
;;     the C++ boundary wants;
;;   - an rnrs enum built from the same symbols, so the whole (rnrs enums)
;;     toolkit — enum-set-union, enum-set-member?, enum-set-indexer — applies to
;;     it as well.
;;
;; The two are independent on purpose: the rnrs enum numbers its members 0..n-1
;; in declaration order, while the integers the API speaks are whatever the
;; compositor uses (fullscreen modes are -1/0/1/2, where -1 means "leave it").
;; Neither can be derived from the other, so both are kept.

(define hl-enum-values-rtd (make-record-type 'hl-enum-values '(alist enum)))

(define (make-enum-values alist)
  "Build an <enum-values> from ALIST, an ass list of symbol -> integer:
   (make-enum-values '((none . 0) (maximized . 1)))."
  (make-struct/no-tail hl-enum-values-rtd alist (make-enumeration (map car alist))))

(define-syntax-rule (define-enum-values name alist)
  "Define NAME as an <enum-values> built from ALIST — sugar for
   (define NAME (make-enum-values ALIST))."
  (define name (make-enum-values alist)))

(define (enum-values? x) "Is X an <enum-values>?"
  ((record-predicate hl-enum-values-rtd) x))

(define hl--enum-alist (record-accessor hl-enum-values-rtd 'alist))

(define enum-values->enum (record-accessor hl-enum-values-rtd 'enum))
(set-procedure-property! enum-values->enum 'documentation
  "The rnrs enum behind EV, for the (rnrs enums) operations — enum-set->list,
   enum-set-member?, enum-set-indexer and the rest. Its indices are 0..n-1 in
   declaration order; for the integers the API speaks use enum-value.")

(define (enum-value ev v)
  "The integer V stands for in EV. V is either a symbol of the enum or the
   integer itself, so a caller may pass either and the function needs no
   conversion of its own; anything else is an error naming the enum."
  (let ((alist (hl--enum-alist ev)))
    (cond ((symbol? v)
           (let ((pair (assq v alist)))
             (if pair (cdr pair)
                 (error "enum-value: not a value of this enum. The offending value, then the valid ones:" v (map car alist)))))
          ((integer? v)
           (if (member v (map cdr alist)) v
               (error "enum-value: not a value of this enum. The offending value, then the valid ones:" v (map cdr alist))))
          (else (error "enum-value: expected a symbol or an integer, got" v)))))

(define (enum-symbol ev n)
  "The symbol N stands for in EV — the inverse of enum-value, and what a getter
   returns. An integer outside the enum is an error."
  (let ((pair (find (lambda (p) (equal? (cdr p) n)) (hl--enum-alist ev))))
    (if pair (car pair)
        (error "enum-symbol: not a value of this enum. The offending value, then the valid ones:" n (map cdr (hl--enum-alist ev))))))

