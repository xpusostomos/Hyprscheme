;; hyprscheme/layer.scm — the layer family — raw wrappers over the compositor's layer operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme layer)
  #:export (
           hl-layer-rule-add! hl-layers hl-layer-alive? hl-layer=?
           hl-layer-address hl-layer-pid hl-layer-monitor
           hl-layer-namespace hl-layer-level hl-layer-mapped?
           hl-layer-kb-interactivity hl-layer-above-fullscreen?
           hl-layer-position hl-layer-size
           hl-layer-open-notification-add!
           hl-layer-close-notification-add!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define* (hl-layer-rule-add! name #:key #:allow-other-keys #:rest fields)
  "Add a layer rule: the same keyword-rule-spec shape as
   hl-window-rule-add!, matching on the namespace property inside
   #:match."
  (hl--window-rule-mk name
    (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)
    hl--c-layer-rule-begin hl--c-layer-rule-effect hl--c-layer-rule-commit
    'hl-layer-rule-add! 'layer))

(define* (hl-layers #:key monitor namespace)
  "The layer surfaces, as a list of layer handles. Optional keyword
   filters: #:monitor MON, #:namespace \"ns\", or both combined."
  (let* ((mon  monitor)
         (ns   namespace)
         (monId (if (and mon (hl-monitor? mon)) mon 0))
         (l (hl--c-layers (or (and (hl-monitor? mon) mon) #f) (and ns (hl--str ns)))))
    (if l l '())))

(define (hl-layer-alive? s)
  "#t when the layer surface still exists."
  (= 1 (hl--c-layer-alive s)))

(define (hl-layer=? a b)
  "#t when A and B refer to the same layer surface."
  (= 1 (hl--c-layer-same a b)))

(define (hl-layer-address s)
  "The surface's stable \"0x...\"address (same idea as
   hl-window-address)."
  (hl--c-layer-address s))

(define (hl-layer-pid s)
  "The surface's owning process id."
  (hl--c-layer-pid s))

(define (hl-layer-monitor s)
  "The monitor the surface sits on, as a handle."
  (hl--c-layer-monitor s))

(define (hl-layer-namespace s)
  "The surface's namespace."
  (hl--c-layer-namespace s))

(define (hl-layer-level s)
  "The surface's shell layer: 0 background, 1 bottom, 2 top, 3 overlay."
  (hl--c-layer-level s))

(define (hl-layer-mapped? s)
  "#t when the surface is mapped."
  (eq? (hl--c-layer-mapped s) #t))

(define (hl-layer-kb-interactivity s)
  "The surface's keyboard interactivity: 0 none, 1 exclusive, 2
   on-demand (lock screens use exclusive)."
  (hl--c-layer-kb-interactivity s))

(define (hl-layer-above-fullscreen? s)
  "#t when the surface renders above fullscreen windows."
  (eq? (hl--c-layer-above-fs s) #t))

(define (hl-layer-position s)
  "The surface's position, as (x . y), monitor-local."
  (hl--c-layer-position s))

(define (hl-layer-size s)
  "The surface's size, as (width . height)."
  (hl--c-layer-size s))

(define (hl-layer-open-notification-add! handler)
  "Register a handler that fires when a layer surface opens. The handler
   receives the layer's namespace string."
  (let ((rec (make-hl-event 'layer-open handler)))
    (if (= 0 (hl--c-layer-listen rec 0))
        rec
        (hl--error 'hl-layer-open-notification-add! "listener rejected, see compositor log"))))

(define (hl-layer-close-notification-add! handler)
  "Register a handler that fires when a layer surface closes. The
   handler receives the layer's namespace string."
  (let ((rec (make-hl-event 'layer-close handler)))
    (if (= 0 (hl--c-layer-listen rec 1))
        rec
        (hl--error 'hl-layer-close-notification-add! "listener rejected, see compositor log"))))

