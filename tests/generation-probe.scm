;; Driver for tests/t-generation.sh.
;;
;; Loaded into the GENERATION by the test (the kernel shadows `load` so a load
;; lands there, which is where a config runs). It answers one question, the one
;; the test exists to ask:
;;
;;   did init.scm import the machinery into the GENERATION itself?
;;
;; The obvious checks cannot answer it. Guile resolves a name through a used
;; module's OWN use-list, transitively (verified), so if init.scm is read with
;; the kernel current — the order this test exists to catch — the families land
;; in the KERNEL and the generation still reaches every name through it. Every
;; call works. The suite passes. Only the generation's own uses-list shows it.
;;
;; And `module-obarray` is not the answer either: `use-modules` does NOT create a
;; local binding, it adds the module to the use-list (verified — an imported name
;; is absent from the obarray and present in `module-uses`). So the use-list is
;; not a proxy for the question, it IS the question: `use-modules` is exactly the
;; operation that grows it, and init.scm is the only thing that runs it here.

(define (generation-uses? name)
  "Is the module NAME in the current module's own use-list?"
  (and (member name (map module-name (module-uses (current-module)))) #t))

(define (generation-missing names)
  "The NAMES not in the current module's use-list — or #t when none are."
  (let loop ((ns names) (missing '()))
    (cond ((null? ns) (if (null? missing) #t (reverse missing)))
          ((generation-uses? (car ns)) (loop (cdr ns) missing))
          (else (loop (cdr ns) (cons (car ns) missing))))))

;; The generation imports the families and the kernel directly, never the
;; umbrella — internals stay reachable through the families. This marker is
;; defined HERE, in the generation, so the test can prove the probe itself is
;; running where it thinks it is rather than in some other module.
(define hl--generation-probe-here #t)
