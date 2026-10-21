;; hyprscheme/extras.scm — composed conveniences built on the raw API
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme extras)
  #:export (
           hl-window-monitor-set! hl-workspace-special-set!
           hl-rule-enabled-set! hl-notification-paused-set!
           hl-timer-enabled-set! hl-gesture-add! hl-gesture-remove!
           hl-load-config))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))
(use-modules (hyprscheme bind))
(use-modules (hyprscheme config))
(use-modules (hyprscheme event))
(use-modules (hyprscheme exec))
(use-modules (hyprscheme gesture))
(use-modules (hyprscheme group))
(use-modules (hyprscheme layer))
(use-modules (hyprscheme monitor))
(use-modules (hyprscheme notification))
(use-modules (hyprscheme query))
(use-modules (hyprscheme rule))
(use-modules (hyprscheme timer))
(use-modules (hyprscheme window))
(use-modules (hyprscheme workspace))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-window-monitor-set! w mon)
  "Move the window to another monitor MON. There is no direct
   window-to-monitor dispatch, so this moves to the target's active
   workspace."
  (hl-window-workspace-set! w (hl-monitor-active-workspace mon)))

;; ---- actions: navigation and geometry --------------------------------------
;; directions: "l"/"r"/"u"/"d" or 'left/'right/'up/'down. Window args accept
;; a handle or #f (= active window). Action results: #t on success, #f on
;; rejection (message in the compositor log).

(define* (hl-workspace-special-set! ws #:key (on? 'unset))
  "Open, close or toggle the special workspace WS. Absent #:on? toggles
   the named special workspace on the FOCUSED monitor (created on
   demand); #t/#f force open/close through the same toggle when the
   focused monitor's active special workspace already equals/differs
   from the target."
  (if (eq? on? 'unset)
      (= 0 (hl--c-workspace-toggle-special (hl--ws-arg ws)))
      (let* ((mon (hl-active-monitor))
             (active (and mon (hl-monitor-active-special-workspace mon)))
             (cur (and active (hl--special-name (hl-workspace-name active))))
             (target (hl--special-name (hl--ws-arg ws))))
        (if (eqv? on? (and cur (string=? cur target)))
            0
            (= 0 (hl--c-workspace-toggle-special (hl--ws-arg ws)))))))

(define* (hl-rule-enabled-set! rule #:key (on? 'unset))
  "Enable, disable or toggle RULE: absent #:on? toggles, #t/#f set."
  (if (= 0 (hl--c-rule-set-enabled rule
                            (if (if (eq? on? 'unset) (not (hl-rule-enabled? rule)) on?) 1 0)))
      #t
      (hl--error 'hl-rule-enabled-set! "unknown rule")))

(define* (hl-notification-paused-set! n #:key (on? 'unset))
  "Pause or resume the notification's timeout timer: absent #:on? toggles,
   #t/#f set."
  (= 0 (hl--c-notification-paused-set n
         (if (if (eq? on? 'unset) (not (eq? (hl-notification-paused? n) #t)) (eq? on? #t)) 1 0))))

(define* (hl-timer-enabled-set! t #:key (on? 'unset))
  "Enable, disable or toggle the timer T: absent #:on? toggles, #t/#f set."
  (if (= 0 (hl--c-timer-set-enabled t
                                   (if (if (eq? on? 'unset) (not (hl-timer-enabled? t)) on?) 1 0)))
      #t
      (hl--error 'hl-timer-enabled-set! "unknown timer")))

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
  (let ()
    (cond ((not fingers)
           (hl--error 'hl-gesture-add! "field 'fingers' is required"))
          ((not (and (integer? fingers) (exact? fingers) (>= fingers 2)))
           (hl--error 'hl-gesture-add! "field 'fingers' must be an integer >= 2"))
          ((not direction)
           (hl--error 'hl-gesture-add! "field 'direction' is required"))
          ((not (string? direction))
           (hl--error 'hl-gesture-add! "field 'direction' must be a string"))
          ((not action)
           (hl--error 'hl-gesture-add! "an action is required — #:action (hl-make-...-gesture ...)"))
          ((not (hl-gesture-action? action))
           (hl--error 'hl-gesture-add! "field 'action' must be an hl-gesture-action (see the hl-make-*-gesture constructors)"))
          ((not (and (list? mods) (every (lambda (m) (string? m)) mods)))
           (hl--error 'hl-gesture-add! "'mods must be a list of modifier tokens, e.g. (hl-key \"SUPER\") or '(\"SUPER\" \"SHIFT\")"))
          ((not (or (<= -10.0 scale -0.1) (<= 0.1 scale 10.0)))
           (hl--error 'hl-gesture-add! "field 'scale' must be between -10 and -0.1 or between 0.1 and 10 - it is currently: ~a" scale))
          (else
           (let ((rc (hl--c-gesture action fingers (hl--str direction) mods (exact->inexact scale) (if disable-inhibit 1 0))))
             (cond ((not (= 0 rc))
                    (hl--error 'hl-gesture-add! "~a" (hl--c-config-last-error)))
                   (else
                    ;; the handle is the exact registration spec — the manager
                    ;; matches removal on it, so remove! always hits our gesture
                    (list fingers direction mods scale disable-inhibit))))))))

(define (hl-gesture-remove! g)
  "Remove a registered gesture: #t when removed, #f when nothing is
   registered under that spec, error otherwise."
  (unless (hl-gesture? g)
    (hl--error 'hl-gesture-remove! "not an hl-gesture handle"))
  (let ((rc (hl--c-gesture-remove (car g) (cadr g) (caddr g) (exact->inexact (cadddr g))
                                 (if (list-ref g 4) 1 0))))
    (cond ((= rc 0) #t)
          ((= rc 1) #f)
          (else (hl--error 'hl-gesture-remove! "~a" (hl--c-config-last-error))))))

;; ---- loading other config files ---------------------------------------------

(define (hl--config-candidates name)
  "The paths a bare NAME (or a NAME ending in .scm) is looked for under:
   $XDG_CONFIG_HOME/hypr/ then $HOME/.config/hypr/. Per the XDG spec an unset,
   empty or RELATIVE XDG_CONFIG_HOME is invalid and ignored, and $HOME/.config
   is what it means — but the two are tried in that order regardless, so an
   XDG_CONFIG_HOME that is set yet does not hold the file still finds it in the
   plain ~/.config, which is where a config written before XDG was set lives."
  (let* ((file (if (string-suffix? ".scm" name) name (string-append name ".scm")))
         (xdg (getenv "XDG_CONFIG_HOME"))
         (home (getenv "HOME"))
         (xdg-base (and xdg (not (string-null? xdg)) (string-prefix? "/" xdg) xdg))
         (home-base (and home (not (string-null? home))
                         (string-append home "/.config"))))
    (delete-duplicates
      (filter (lambda (p) p)
              (list (and xdg-base (string-append xdg-base "/hypr/" file))
                    (and home-base (string-append home-base "/hypr/" file))))
      string=?)))

(define (hl-load-config name)
  "Load another config file by NAME, and return its path — or #f, having
   raised a notification, when there is no such file.

   NAME is looked up under the config directory the way Hyprland finds its
   own: $XDG_CONFIG_HOME/hypr/NAME.scm, then $HOME/.config/hypr/NAME.scm.
   \".scm\" is appended unless NAME already ends in it, so
   (hl-load-config \"keybinds\") reads ~/.config/hypr/keybinds.scm.

   The file is loaded INTO THE CURRENT GENERATION, exactly as (load \"…\")
   from a config is (the kernel shadows load for this), so what it defines is
   reachable afterwards and is discarded on the next reload like everything
   else a config sets up. A file that raises is reported to the compositor log
   and does not stop the rest of the config — but this returns #f only for a
   file that is NOT THERE; a file that loaded and then raised still returns its
   path, because it was found and its forms did run."
  (let ((found (find file-exists? (hl--config-candidates name))))
    (if found
        ;; hl--load, not `load`: it is the same function the kernel shadows
        ;; `load` with, but naming it directly says which one is meant and
        ;; avoids a compile warning about the kernel's `load` overriding the
        ;; core binding in this module.
        (begin (hl--load found) found)
        (begin
          (hl-notification-add!
            #:text (format #f "hyprscheme: config not found: ~a" name)
            #:timeout 5000
            #:icon "warning")
          #f))))

;; ---- monitors, curves, animations, permissions ------------------------------

