;; hyprscheme/gesture.scm — the gesture family — raw wrappers over the compositor's gesture operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme gesture)
  #:export (
           hl-gesture-action? hl-make-workspace-swipe-gesture
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
    (hl--error 'hl-make-special-workspace-gesture "workspace name must be a string, got ~a" name))
  (cons (hl--c-gesture-maker-special) (list 'name name)))

(define* (hl-make-cursor-zoom-gesture zoom #:key (mode 'toggle))
  "A gesture action that zooms the cursor's view. ZOOM is a number;
   optional MODE: 'toggle (default), 'mult, 'live - the numeric argument
   is unused in live mode, so 1 is a good placeholder there."
  (unless (real? zoom)
    (hl--error 'hl-make-cursor-zoom-gesture "zoom must be a number, got ~a" zoom))
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
    (hl--error 'hl-make-custom-gesture "at least one of #:start, #:update, #:finish is required"))
  (for-each (lambda (k fn)
              (when (and fn (not (procedure? fn)))
                (hl--error 'hl-make-custom-gesture "field ~a needs a procedure" k)))
            '(#:start #:update #:finish) (list start update finish))
  (cons (hl--c-gesture-maker-custom)
        (append (if start (list 'start start) '())
                (if update (list 'update update) '())
                (if finish (list 'finish finish) '()))))

(define (hl-gesture? x)
  "#t when X is an hl-gesture handle - the registration spec: (fingers
   direction mods scale disable-inhibit)."
  (and (pair? x) (list? x) (= 5 (length x)) (integer? (car x)) (string? (cadr x)) (list? (caddr x))))

