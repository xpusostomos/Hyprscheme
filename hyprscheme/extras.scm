;; hyprscheme/extras.scm — composed conveniences built on the raw API
;;
;; THE TWO RULES THAT DECIDE WHAT LIVES HERE, and they are different questions
;; that are easy to conflate (they were):
;;
;;   1. DOES IT REACH HYPRLAND C OR C++? Then it is a FAMILY's, not extras'.
;;      A wrapper that calls one gsubr and maybe its own module's own query
;;      belongs with the object it wraps — hl-rule-enabled-set! in rule.scm,
;;      hl-timer-enabled-set! in timer.scm, hl-gesture-add! in gesture.scm.
;;      Those five sat here until Chris asked why, and the answer was that
;;      nothing about them composes anything.
;;   2. DOES IT CROSS FAMILIES? Then it is extras'. hl-window-monitor-set!
;;      needs window AND monitor; hl-workspace-special-set! needs workspace,
;;      monitor and query. A family may not import another family, so a
;;      convenience that spans two has nowhere else to live.
;;
;; "Policy in Scheme" is NOT the test — it is a separate rule that lives in
;; CLAUDE.md (C++ exists to reach a compositor API, decisions belong in
;; Scheme). hl-rule-enabled-set!'s absent-argument toggle is policy, and it
;; still belongs in rule.scm.
;;
;; THE HIERARCHY, from Chris: core -> extras -> emacs-keymaps, each more
;; specific than the last. extras is not "complex behaviour", it is simply
;; everything that is not a raw API; emacs-keymaps is where complex behaviour
;; lives and may freely use extras.
;;
;; imports the kernel [the C++ boundary] and core [shared plumbing], plus the
;; families it composes — a family imports only the kernel and core, which is
;; exactly why composition has to happen out here.

(define-module (hyprscheme extras)
  #:export (
           hl-window-monitor-set! hl-workspace-special-set!
           hl-load-config))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))
(use-modules (hyprscheme monitor))
(use-modules (hyprscheme notification))
(use-modules (hyprscheme query))
(use-modules (hyprscheme workspace))
(use-modules (hyprscheme window))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define* (hl-window-monitor-set! w mon #:key (follow #t))
  "Move the window to another monitor MON. There is no direct
   window-to-monitor dispatch, so this moves to the target's active
   workspace.

   #:follow is passed through to hl-window-workspace-set!, and exists for the
   same reason it does there — the scripting API's move takes it for a monitor
   target too. With #:follow #f the window goes to the other monitor and the
   VIEW STAYS; with the default it follows the window there."
  (hl-window-workspace-set! w (hl-monitor-active-workspace mon) #:follow follow))

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

;; hl-rule-enabled-set!, hl-timer-enabled-set!, hl-notification-paused-set!,
;; hl-gesture-add! and hl-gesture-remove! USED TO BE HERE and are now in their
;; own families — rule.scm, timer.scm, notification.scm and gesture.scm. Each
;; one reaches the compositor through a gsubr and otherwise calls only its own
;; module's query, so none of them composed anything; see the note at the top of
;; this file for the two rules that decide what belongs here.

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
   raised a notification, when there is no such file OR when the file is
   there but raised while loading.

   Both failures notify, and they are worth telling apart: a missing file is
   usually a typo or a file you have not written yet, while a file that RAISES
   has already defined some of itself — so the config ends up half-applied,
   which is the state that is hard to diagnose. (It was, until this: a file
   that died at its 300th form left everything after it unbound, and the only
   sign was a line in the compositor log.)

   NAME is looked up under the config directory the way Hyprland finds its
   own: $XDG_CONFIG_HOME/hypr/NAME.scm, then $HOME/.config/hypr/NAME.scm.
   \".scm\" is appended unless NAME already ends in it, so
   (hl-load-config \"keybinds\") reads ~/.config/hypr/keybinds.scm.

   The file is loaded INTO THE CURRENT GENERATION, exactly as (load \"…\")
   from a config is (the kernel shadows load for this), so what it defines is
   reachable afterwards and is discarded on the next reload like everything
   else a config sets up. An error in it is reported to the compositor log and
   does not stop the rest of the config — but it IS a failure here, and this
   returns #f for it."
  (let ((found (find file-exists? (hl--config-candidates name))))
    (cond
      ((not found)
       (hl-notification-add!
         #:text (format #f "hyprscheme: config not found: ~a" name)
         #:timeout 5000
         #:icon "warning")
       #f)
      ;; hl-load, not `load`: both would work, but naming it says which one is
      ;; meant — the one that targets the generation by name instead of relying
      ;; on the current module happening to be it. It answers whether the file
      ;; LOADED, so a file that raises is reported here rather than looking like
      ;; a success.
      ((hl-load found) found)
      (else
       (hl-notification-add!
         #:text (format #f "hyprscheme: config failed to load: ~a" found)
         #:timeout 5000
         #:icon "error")
       #f))))

;; ---- monitors, curves, animations, permissions ------------------------------

