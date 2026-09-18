;; Hyprscheme example configuration — a tour of the API.
;; Loaded from ~/.config/hypr/hyprland.scm; re-loaded on every edit.

;; ---- Keybinds -------------------------------------------------------------

;; (hl-bind mods key thunk [options...]) -> id
(hl-bind "SUPER" "U" (lambda () (hl-exec "foot")))
(hl-bind "SUPER" "N" (lambda () (hl-exec "notify-send 'Hello from Chez Scheme!'")))

;; options: release repeat locked non-consuming long-press transparent
;;          ignore-mods mouse click drag devices device-inclusive
(hl-bind "SUPER" "T" (lambda () (hl-exec "notify-send 'on release'"))
         'release #t 'description "show a note on key release")

;; ---- Timers ---------------------------------------------------------------

;; (hl-after ms thunk) fires once; (hl-repeat ms thunk) repeats until reload.
;; A repeating timer stops itself if its callback errors.
;; (hl-repeat 10000 (lambda () (hl-exec "notify-send '10s heartbeat'")))

;; ---- Queries --------------------------------------------------------------

;; (hl-active-title)  -> string | #f
;; (hl-workspaces)    -> list of workspace handles
;; (hl-monitors)      -> list of monitor handles
;; (hl-windows)       -> list of window handles
;; (hl-window-class w) (hl-window-title w) (hl-window-workspace w)
;; (hl-window-floating? w) (hl-window-size w) (hl-window-pid w) ...
(hl-bind "SUPER" "A" (lambda ()
  (hl-exec (string-append "notify-send 'active: " (or (hl-active-title) "none") "'"))))

;; ---- Events ---------------------------------------------------------------

;; (hl-on-window-open (lambda (w) ...))     ; w is a window handle
;; (hl-on-window-close (lambda (w) ...))
;; (hl-on-workspace-active (lambda (ws) ...))  ; ws = workspace handle
;; (hl-on-submap (lambda (name) ...))
(hl-on-window-open (lambda (w)
  (when (equal? (hl-window-class w) "foot")
    (hl-exec "notify-send 'terminal opened'"))))

;; ---- Window actions -------------------------------------------------------

;; (hl-window-focus w) (hl-window-float w) (hl-window-close w)
;; (hl-window-move-to-workspace w "name:project")  ; auto-creates
;; (hl-window-fullscreen w) / (hl-window-maximize w)
;; (hl-window=? a b) — identity across distinct handles

;; ---- Custom layouts -------------------------------------------------------

;; The fn receives (count W H windows) and returns one (x y w h) box per
;; window, within the work area. The `windows` handles can be queried
;; mid-recalculate (class, title, ...) to weight the geometry.
(hl-define-layout "wide-master"
  (lambda (count W H windows)
    (let* ((master-w (quotient W 2))
           (rest (- W master-w))
           (n (max 1 (- count 1)))
           (sh (quotient H n)))
      (if (= count 1)
          (list (list 0 0 W H))
          (cons (list 0 0 master-w H)
                (let loop ((i 1) (y 0) (acc '()))
                  (if (= i count)
                      (reverse acc)
                      (loop (+ i 1) (+ y sh)
                            (cons (list master-w y rest sh) acc))))))))))

;; select with:  hl.config({ general = { layout = "scheme:wide-master" } })

;; Stateful variant: wrap in a (let ((state (vector init))) ...) and add a
;; (resize . fn) callback — returning #f from it re-runs recalculate with
;; the updated state.

;; ---- Submaps --------------------------------------------------------------

;; Binds registered inside the fn are scoped to the submap.
;; A bind can conditionally enter different submaps.
(hl-submap "apps"
  (lambda ()
    (hl-bind "" "f" (lambda () (hl-enter-submap "files")))
    (hl-bind "" "ESCAPE" hl-exit-submap)))

(hl-submap "files"
  (lambda ()
    (hl-bind "" "f" (lambda () (hl-exec "foot")))
    (hl-bind "" "ESCAPE" hl-exit-submap)))

;; ---- Cross-reload state ---------------------------------------------------

;; Survives config re-loads; dies with the compositor. Stored values that
;; are generation-bound (window handles) still go stale with their generation.
(hl-state-set! 'reload-count (+ 1 (hl-state-ref 'reload-count 0)))
