;; hyprscheme/gesture.scm — the gesture family — raw wrappers over the compositor's gesture operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme gesture)
  #:export (
           hl-gesture-action? hl-gesture-add! hl-gesture-remove!
           hl-make-workspace-swipe-gesture
           hl-make-move-gesture hl-make-resize-gesture
           hl-make-close-gesture hl-make-scroll-move-gesture
           hl-make-float-gesture hl-make-fullscreen-gesture
           hl-make-special-workspace-gesture hl-make-cursor-zoom-gesture
           hl-make-custom-gesture hl-gesture?))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-gesture-action? x)
  "#t when X is an hl-gesture-action - the opaque action value built by
   the hl-make-*-gesture constructors."
  (and (pair? x) (integer? (car x)) (exact? (car x)) (positive? (car x)) (list? (cdr x))))

;; one optional mode argument, restricted to the allowed symbols

(define (hl-make-workspace-swipe-gesture )
  "A gesture action that swipes workspaces."
  (cons (hl--c-gesture-maker-workspace-swipe) '()))

(define (hl-make-move-gesture )
  "A gesture action that moves the active window."
  (cons (hl--c-gesture-maker-move) '()))

(define (hl-make-resize-gesture )
  "A gesture action that resizes the active window."
  (cons (hl--c-gesture-maker-resize) '()))

(define (hl-make-close-gesture )
  "A gesture action that closes the active window."
  (cons (hl--c-gesture-maker-close) '()))

(define (hl-make-scroll-move-gesture )
  "A gesture action that scrolls the tape (when the current layout
   supports it, e.g. scrolling)."
  (cons (hl--c-gesture-maker-scroll-move) '()))

(define* (hl-make-float-gesture #:key (mode 'toggle))
  "A gesture action that floats/tiles the active window. #:mode:
   'toggle (default), 'float, 'tile."
  (cons (hl--c-gesture-maker-float)
        (list 'mode (hl--gesture-mode 'hl-make-float-gesture mode '(toggle float tile)))))

(define* (hl-make-fullscreen-gesture #:key (mode 'fullscreen))
  "A gesture action that fullscreens the active window. #:mode:
   'fullscreen (default), 'maximize."
  (cons (hl--c-gesture-maker-fullscreen)
        (list 'mode (hl--gesture-mode 'hl-make-fullscreen-gesture mode '(fullscreen maximize)))))

(define (hl-make-special-workspace-gesture name)
  "A gesture action that toggles the named special workspace (empty
   string = the default special workspace)."
  (unless (string? name)
    (error "hl-make-special-workspace-gesture: workspace name must be a string, got" name))
  (cons (hl--c-gesture-maker-special) (list 'name name)))

(define* (hl-make-cursor-zoom-gesture zoom #:key (mode 'toggle))
  "A gesture action that zooms the cursor's view. ZOOM is a number;
   optional MODE: 'toggle (default), 'mult, 'live - the numeric argument
   is unused in live mode, so 1 is a good placeholder there."
  (unless (real? zoom)
    (error "hl-make-cursor-zoom-gesture: zoom must be a number, got" zoom))
  (let ((m (hl--gesture-mode 'hl-make-cursor-zoom-gesture mode '(toggle mult live))))
    (cons (hl--c-gesture-maker-cursor-zoom) (list 'zoom (exact->inexact zoom) 'mode m))))

(define* (hl-make-custom-gesture #:key start update finish)
  "A callback-backed gesture action: (hl-make-custom-gesture #:start FN
   #:update FN #:finish FN) - at least one procedure required; each
   receives the gesture event plist as spread args (see bind-gestures
   for the fields). The three closures share state naturally by closing
   over a let - wrap the constructor in a function to get fresh state
   per registration."
  (when (and (not start) (not update) (not finish))
    (error "hl-make-custom-gesture: at least one of #:start, #:update, #:finish is required"))
  (for-each (lambda (k fn)
              (when (and fn (not (procedure? fn)))
                (error "hl-make-custom-gesture: this field needs a procedure:" k)))
            '(#:start #:update #:finish) (list start update finish))
  (cons (hl--c-gesture-maker-custom)
        (append (if start (list 'start start) '())
                (if update (list 'update update) '())
                (if finish (list 'finish finish) '()))))

(define (hl-gesture? x)
  "#t when X is an hl-gesture handle - the registration spec: (fingers
   direction mods scale disable-inhibit)."
  (and (pair? x) (list? x) (= 5 (length x)) (integer? (car x)) (string? (cadr x)) (list? (caddr x))))

;; ---- registering and removing gestures ---------------------------------------
;;
;; THESE TWO LIVE HERE, NOT IN (hyprscheme extras): they reach the compositor
;; through hl--c-gesture / hl--c-gesture-remove, and the only other names they
;; call are this module's own hl-gesture-action? and hl-gesture?. See
;; hl-rule-enabled-set! in rule.scm for the full note — whatever reaches
;; Hyprland C or C++ belongs to the family that owns the object.
;;
;; The handle is the exact registration spec, and that is deliberate: the
;; manager matches removal on the spec, never on the action, so hl-gesture-remove!
;; always hits the gesture that was registered rather than a same-shaped one.

(define* (hl-gesture-add! #:key fingers direction action (mods '()) (scale 1.0) (disable-inhibit #f))
  "Register a gesture: (hl-gesture-add! #:fingers N #:direction \"dir\"
   #:action ACTION [#:mods (hl-key \"SUPER\")] [#:scale 1.0]
   [#:disable-inhibit #t]). #:fingers, #:direction and #:action are
   required; the optional keywords describe the gesture INPUT,
   independent of the action. ACTION is an hl-gesture-action built by
   the hl-make-*-gesture constructors - built-in and custom actions are
   indistinguishable from the caller's side, and a recipe is a pure
   value (registering it under two specs constructs two independent
   gestures). Returns an hl-gesture handle for hl-gesture-remove! -
   removal matches on the registration spec, never on the action."
  (cond ((not fingers)
         (error "hl-gesture-add!: field 'fingers' is required"))
        ((not (and (integer? fingers) (exact? fingers) (>= fingers 2)))
         (error "hl-gesture-add!: field 'fingers' must be an integer >= 2"))
        ((not direction)
         (error "hl-gesture-add!: field 'direction' is required"))
        ((not (string? direction))
         (error "hl-gesture-add!: field 'direction' must be a string"))
        ((not action)
         (error "hl-gesture-add!: an action is required - #:action (hl-make-...-gesture ...)"))
        ((not (hl-gesture-action? action))
         (error "hl-gesture-add!: field 'action' must be an hl-gesture-action (see the hl-make-*-gesture constructors)"))
        ((not (and (list? mods) (every (lambda (m) (string? m)) mods)))
         (error "hl-gesture-add!: 'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\") or '(\"SUPER\" \"SHIFT\")"))
        ((not (or (<= -10.0 scale -0.1) (<= 0.1 scale 10.0)))
         (error "hl-gesture-add!: field 'scale' must be between -10 and -0.1 or between 0.1 and 10 - it is currently:" scale))
        (else
         (let ((rc (hl--c-gesture action fingers direction mods (exact->inexact scale) disable-inhibit)))
           (cond ((not (= 0 rc))
                  (error "hl-gesture-add!:" (hl--c-config-last-error)))
                 (else
                  ;; the handle is the exact registration spec — the manager
                  ;; matches removal on it, so remove! always hits our gesture
                  (list fingers direction mods scale disable-inhibit)))))))

(define (hl-gesture-remove! g)
  "Remove a registered gesture: #t when removed, #f when nothing is
   registered under that spec, error otherwise."
  (unless (hl-gesture? g)
    (error "hl-gesture-remove!: not an hl-gesture handle"))
  (let ((rc (hl--c-gesture-remove (car g) (cadr g) (caddr g) (exact->inexact (cadddr g))
                                 (list-ref g 4))))
    (cond ((= rc 0) #t)
          ((= rc 1) #f)
          (else (error "hl-gesture-remove!:" (hl--c-config-last-error))))))

