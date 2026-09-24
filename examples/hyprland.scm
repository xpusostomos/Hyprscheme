;; Hyprscheme example config — a tour of the API.
;; Copy to $XDG_CONFIG_HOME/hypr/hyprland.scm (or point HYPRSCHEME_CONFIG at it).

;; ---- binds ----------------------------------------------------------------
;; hl-bind-add! takes a TOKEN LIST (mods first, key last) and a thunk.
;; (hl-kbd ...) is emacs syntax, (hl-key ...) is hyprland syntax; both return the list.
(hl-bind-add! (hl-kbd "s-<Return>") (lambda () (hl-exec! "foot")))
(hl-bind-add! (hl-kbd "s-d")        (lambda () (hl-exec! "wofi --show drun")))
(hl-bind-add! (hl-kbd "s-<Tab>")    (lambda () (hl-window-cycle!)))

;; options follow the thunk as a plist
(hl-bind-add! (hl-kbd "s-q") (lambda () (hl-window-close!))
         'description "Close the focused window")

;; a submap: binds inside are scoped to it, no modifiers needed
(hl-bind-add! (hl-kbd "s-g") (lambda () (hl-submap-activate! "resize")))
(hl-submap "resize"
  (lambda ()
    (hl-bind-add! (hl-kbd "left")  (lambda () (hl-window-size-set! #f -20 0 'relative)) 'repeat #t)
    (hl-bind-add! (hl-kbd "right") (lambda () (hl-window-size-set! #f 20 0 'relative))  'repeat #t)
    (hl-bind-add! (hl-kbd "g")     (lambda () (hl-submap-exit!)))))

;; ---- events ---------------------------------------------------------------
(hl-window-open-notification-add! (lambda (w)
  (hl-notify! (string-append "opened: " (hl-window-class w)) 3000)))
(hl-workspace-active-notification-add! (lambda (ws)
  (hl-notify! (string-append "workspace: " (hl-workspace-name ws)) 1500 'icon "info")))

;; ---- window rules (a plist; match is a nested plist too) ------------------
(hl-window-rule-add! "float-htop" 'match '(class "^htop$") 'float #t 'center #t)
(hl-window-rule-add! "priv-browsers"
  'match '(class "^(Tor Browser|Mullvad Browser)$")
  'float #t 'fullscreen_state "0 0")

;; ---- per-device input config ----------------------------------------------
(hl-device-add! "my-touchpad" 'natural_scroll #t 'tap_to_click #t)

;; ---- config options -------------------------------------------------------
(hl-config-add! "general:gaps_in" 5)
(hl-config-add! "general:gaps_out" 10)
(hl-config-add! "input:kb_options" "ctrl:nocaps")   ; Caps Lock → Ctrl

;; ---- a custom layout ------------------------------------------------------
;; state lives in the closure; keys are quoted identifiers, values are the
;; procedure expressions — an ordinary call, no backquote.
(let ((mfact (vector 0.55)))
  (hl-layout-add! "master-stack"
    'recalculate
    (lambda (count W H windows)
      (cond ((= count 0) '())
            ((= count 1) (list (list 0 0 W H)))
            (else
             (let* ((mw (exact (floor (* W (vector-ref mfact 0)))))
                    (slaves (- count 1)))
               (cons (list 0 0 mw H)
                     (let loop ((i 0) (y 0) (boxes '()))
                       (if (= i slaves)
                           (reverse boxes)
                           (let ((h (quotient (- H y) (- slaves i))))
                             (loop (+ i 1) (+ y h)
                                   (cons (list mw y (- W mw) h) boxes))))))))))
    'layout-msg
    (lambda (msg)
      (cond ((equal? msg "wider")
             (vector-set! mfact 0 (min 0.9 (+ (vector-ref mfact 0) 0.05)))
             #t)
            ((equal? msg "narrower")
             (vector-set! mfact 0 (max 0.1 (- (vector-ref mfact 0) 0.05)))
             #t)
            (else #f)))))
(hl-config-add! "general:layout" "scheme:master-stack")

;; ---- timers ---------------------------------------------------------------
(hl-repeat 60000 (lambda ()
  (let ((w (hl-active-window)))
    (when (and w (equal? (hl-window-class w) "foot"))
      (hl-exec! "notify-send 'still there?'")))))

;; ---- state surviving reloads ----------------------------------------------
(hl-state-set! (quote loaded-at) "boot")