# full API surface coverage: every public hl- function is called and its
# return value checked for rationality (type / success shape). Companion
# meta-test t-coverage.sh fails if any API is never referenced by the suite.
FAILED=0
ok()    { out=$($SCHEME "$1" 2>&1); [[ "$out" == "#t" ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
noerr() { out=$($SCHEME "$1" 2>&1); [[ "$out" != "error:"* ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
val()   { out=$($SCHEME "$1" 2>&1); [[ "$out" == "$2" ]] || { echo "FAIL: $1 => [$out] want [$2]"; FAILED=1; }; }
idok()  { out=$($SCHEME "$1" 2>&1); [[ "$out" =~ ^[0-9]+$ ]] || { echo "FAIL: $1 => [$out] want id"; FAILED=1; }; }

# ---- fixtures: one window under test, one sacrificial ----------------------
$SCHEME '(hl-exec-shell! "foot -a api-main")' >/dev/null
WAIT_FOR 10 '(let ((w (hl-window-from "class:^api-main$"))) (if w #t #f))' >/dev/null || { echo "fixture window never appeared"; exit 1; }
$SCHEME '(define w (hl-window-from "class:^api-main$"))
(define aw (hl-active-workspace))
(define am (hl-active-monitor))
(define w2 (begin (hl-exec-shell! "foot -a api-second") #t))' >/dev/null
WAIT_FOR 10 '(let ((w2 (hl-window-from "class:^api-second$"))) (if w2 #t #f))' >/dev/null
$SCHEME '(define w2 (hl-window-from "class:^api-second$"))' >/dev/null

# ---- key specification helpers ---------------------------------------------
ok '(pair? (kbd "C-M-a"))'
val '(car (kbd "RET"))' '"Return"'
val '(list-ref (hl-kbd "SUPER+SHIFT+Q") 2)' '"Q"'

# ---- state ------------------------------------------------------------------
val '(hl-state-set! (quote api-x) 1)' '1'
val '(hl-state-ref (quote api-x))' '1'
val '(hl-state-ref (quote api-missing) (quote dflt))' 'dflt'
ok '(list? (hl-state-keys))'

# ---- window queries ---------------------------------------------------------
ok '(string? (hl-window-title w))'
ok '(string? (hl-window-class w))'
ok '(string? (hl-window-initial-class w))'
ok '(string? (hl-window-initial-title w))'
noerr '(hl-window-workspace w)'
noerr '(hl-window-monitor w)'
ok '(boolean? (hl-window-floating? w))'
ok '(let ((s (hl-window-size w))) (and (pair? s) (number? (car s)) (number? (cdr s))))'
ok '(integer? (hl-window-pid w))'
ok '(boolean? (hl-window-hidden? w))'
ok '(boolean? (hl-window-pinned? w))'
ok '(boolean? (hl-window-x11? w))'
ok '(boolean? (hl-window-alive? w))'
ok '(hl-window=? w w)'
noerr '(hl-window-fullscreen-handler w)'

# ---- window actions ---------------------------------------------------------
ok '(hl-window-focus! w)'
ok '(hl-window-float w)'
ok '(hl-window-size-set! w 300 200 (quote relative))'
ok '(hl-window-move-xy! w 60 60 (quote relative))'
ok '(hl-window-float w (quote off))'
ok '(hl-window-fullscreen-toggle! w)'
ok '(= 2 (hl-window-fullscreen-mode w))'
ok '(begin (hl-window-fullscreen-set! w #f) (= 0 (hl-window-fullscreen-mode w)))'
ok '(begin (hl-window-fullscreen-set! w #t) (= 2 (hl-window-fullscreen-mode w)))'
ok '(begin (hl-window-fullscreen-set! w #f) (= 0 (hl-window-fullscreen-mode w)))'
ok '(hl-window-maximize-toggle! w)'
ok '(= 1 (hl-window-fullscreen-mode w))'
ok '(begin (hl-window-maximized-set! w #f) (= 0 (hl-window-fullscreen-mode w)))'
ok '(begin (hl-window-maximized-set! w #t) (= 1 (hl-window-fullscreen-mode w)))'
ok '(begin (hl-window-maximized-set! w #f) (= 0 (hl-window-fullscreen-mode w)))'
ok '(hl-window-fullscreen-state w 0 0)'
ok '(hl-window-pseudo-toggle! w)'
ok '(hl-window-pseudo-set! w #f)'
ok '(hl-window-move-direction! w "r")'
ok '(boolean? (hl-window-swap-direction! w "r"))'
ok '(hl-window-swap-next! w)'
ok '(hl-window-swap-with! w w2)'
ok '(boolean? (hl-window-center! w))'
ok '(hl-window-cycle!)'
ok '(hl-window-zorder-set! w (quote top))'
ok '(hl-window-tag-add! w "api-tag")'
ok '(hl-window-tags-clear! w)'
ok '(boolean? (hl-window-pin-toggle! w))'
ok '(hl-window-float w)'
ok '(begin (hl-window-pinned-set! w #t) (hl-window-pinned? w))'
ok '(begin (hl-window-pinned-set! w #f) (not (hl-window-pinned? w)))'
ok '(hl-window-float w (quote off))'
ok '(hl-window-swallow-toggle!)'
ok '(hl-window-prop-set! w "opacity" "0.9")'
ok '(hl-window-deny-from-group! w (quote toggle))'
ok '(hl-window-group-move-in! w "r")'
ok '(boolean? (hl-window-group-move-out! w "r"))'
ok '(hl-window-group-move-in-or-create! w "r")'
ok '(hl-window-signal! w 28)'

# groups
ok '(hl-group-toggle! w)'
ok '(hl-group-set! w #t)'
ok '(hl-group-set! w #t)'
ok '(begin (hl-window-group-move-in! w2 "r") #t)'
ok '(hl-group-set! w #f)'
ok '(hl-group-set! w #f)'
ok '(boolean? (hl-group-window-active! w 1))'
ok '(boolean? (hl-group-window-move-next! w))'
ok '(hl-group-lock! (quote on))'
ok '(boolean? (hl-group-lock-active! (quote off)))'
ok '(hl-group-toggle! w)'

# sacrificial window: kill, then close
$SCHEME '(hl-exec-shell! "foot -a api-kill")' >/dev/null
WAIT_FOR 10 '(let ((k (hl-window-from "class:^api-kill$"))) (if k #t #f))' >/dev/null
$SCHEME '(define api-kill-w (hl-window-from "class:^api-kill$"))' >/dev/null
ok '(hl-window-kill! api-kill-w)'
WAIT_FOR 5 '(not (hl-window-alive? api-kill-w))' >/dev/null || { echo "killed window still alive"; FAILED=1; }
ok '(hl-window-close! w2)'
WAIT_FOR 5 '(not (hl-window-alive? w2))' >/dev/null || { echo "closed window still alive"; FAILED=1; }

# ---- workspaces -------------------------------------------------------------
ok '(let ((l (hl-workspaces))) (and (list? l) (pair? l)))'
noerr '(hl-active-workspace)'
noerr '(hl-last-workspace)'
noerr '(hl-active-special-workspace)'
ok '(hl-workspace-special-toggle! "api-special")'
ok '(hl-workspace-special-toggle! "api-special")'
noerr '(hl-workspace-name-set! "api-named" "api-named2")'
ok '(hl-workspace-focus! 50)'
ok '(hl-workspace-id-set! 50 5051)'
ok '(boolean? (hl-workspace-monitor-set! 5051 (hl-active-monitor)))'
ok '(hl-window-workspace-set! w (hl-active-workspace))'
ok '(hl-monitor-special-workspace-set! (hl-active-monitor) "api-special")'
ok '(hl-workspace-special? (hl-monitor-active-special-workspace (hl-active-monitor)))'
ok '(begin (hl-monitor-special-workspace-set! (hl-active-monitor) #f) (not (hl-monitor-active-special-workspace (hl-active-monitor))))'
ok '(boolean? (hl-monitor-swap! (hl-active-monitor) (hl-active-monitor)))'
ok '(let ((l (hl-workspace-windows (hl-active-workspace)))) (list? l))'

# workspace getters
ok '(string? (hl-workspace-name aw))'
ok '(string? (hl-workspace-addressable-name aw))'
ok '(let ((n (hl-workspace-number aw))) (or (integer? n) (eq? n #f)))'
noerr '(hl-workspace-monitor aw)'
ok '(boolean? (hl-workspace-special? aw))'
ok '(boolean? (hl-workspace-active? aw))'
ok '(boolean? (hl-workspace-visible? aw))'
ok '(boolean? (hl-workspace-empty? aw))'
ok '(boolean? (hl-workspace-persistent? aw))'
ok '(boolean? (hl-workspace-has-urgent? aw))'
ok '(boolean? (hl-workspace-has-fullscreen? aw))'
ok '(integer? (hl-workspace-fullscreen-mode aw))'
noerr '(hl-workspace-fullscreen-window aw)'
noerr '(hl-workspace-last-window aw)'
ok '(integer? (hl-workspace-window-count aw))'
ok '(integer? (hl-workspace-group-count aw))'
ok '(string? (hl-workspace-tiled-layout aw))'
ok '(boolean? (hl-workspace-alive? aw))'
ok '(hl-workspace=? aw aw)'

# ---- monitors ---------------------------------------------------------------
ok '(let ((l (hl-monitors))) (and (list? l) (pair? l)))'
noerr '(hl-active-monitor)'
noerr '(hl-monitor-from (hl-monitor-name am))'
noerr '(hl-monitor-at 10 10)'
noerr '(hl-monitor-at-cursor)'
ok '(string? (hl-monitor-name am))'
ok '(string? (hl-monitor-description am))'
ok '(integer? (hl-monitor-number am))'
ok '(boolean? (hl-monitor-enabled? am))'
ok '(boolean? (hl-monitor-focused? am))'
ok '(integer? (hl-monitor-x am))'
ok '(integer? (hl-monitor-y am))'
ok '(integer? (hl-monitor-width am))'
ok '(integer? (hl-monitor-height am))'
ok '(number? (hl-monitor-scale am))'
ok '(integer? (hl-monitor-transform am))'
ok '(number? (hl-monitor-refresh-rate am))'
ok '(string? (hl-monitor-mode am))'
ok '(boolean? (hl-monitor-dpms? am))'
ok '(boolean? (hl-monitor-vrr? am))'
ok '(boolean? (hl-monitor-10bit? am))'
ok '(let ((r (hl-monitor-reserved am))) (and (list? r) (pair? (assq (quote top) r))))'
noerr '(hl-monitor-mirror-of am)'
noerr '(hl-monitor-active-workspace am)'
noerr '(hl-monitor-active-special-workspace am)'
ok '(boolean? (hl-monitor-alive? am))'
ok '(hl-monitor=? am am)'
ok '(hl-monitor (hl-monitor-name am) (quote ((reserved . ((top . 0))))))'

# ---- config -----------------------------------------------------------------
ok '(hl-config-add! "general:gaps_in" 5)'
noerr '(hl-config-get "general:gaps_in")'

# ---- cursor -----------------------------------------------------------------
noerr '(hl-cursor-pos)'
ok '(hl-cursor-move! 40 40)'
ok '(hl-cursor-move-to-corner! w 0)'

# ---- exec -------------------------------------------------------------------
ok '(> (hl-exec-shell! "true") 0)'
ok '(hl-exec! "true")'
ok '(hl-exec-shell-with-rules! "[float] true")'

# ---- notifications / misc ---------------------------------------------------
ok '(hl-notify! "api coverage" 100)'
ok '(boolean? (hl-is-key-down "Return"))'
ok '(list? (hl-loaded-plugins))'
ok '(string? (hl-version))'
ok '(list? (hl-layers))'
ok '(boolean? (hl-window-pass-shortcut! w))'
ok '(boolean? (hl-window-send-shortcut! "SUPER" "F10" w))'
ok '(boolean? (hl-window-send-key-state! "SUPER" "F10" 1 w))'
ok '(boolean? (hl-event! "apicoverage"))'
ok '(boolean? (hl-force-idle! 0))'
ok '(boolean? (hl-force-renderer-reload!))'
ok '(boolean? (hl-release-input-capture!))'
ok '(boolean? (hl-global! "hyprscheme:test"))'
out=$($SCHEME '(hl-permission-add! "/nonexistent/api-test" (quote screencopy) (quote allow))' 2>&1)
[[ "$out" == *startup* ]] || { echo "FAIL: hl-permission-add! runtime reply => [$out]"; FAILED=1; }
ok '(hl-dpms (quote on))'
noerr '(hl-urgent-window)'
noerr '(hl-last-window)'
noerr '(hl-window-from "class:^api-main$")'
ok '(let ((l (hl-windows))) (and (list? l) (pair? l)))'
noerr '(hl-active-title)'
noerr '(hl-active-window)'
ok '(boolean? (hl-mouse "drag"))'

# ---- navigation -------------------------------------------------------------
ok '(hl-workspace-focus! (hl-active-workspace))'
ok '(hl-monitor-focus! (hl-active-monitor))'
ok '(hl-focus-direction-set! "r")'
ok '(hl-focus-last!)'
ok '(hl-focus-urgent!)'

# ---- curves, animations, rules ----------------------------------------------
ok '(hl-curve-add! "api-curve" (quote bezier) 0.25 0.1 0.25 1.0)'
ok '(hl-animation-add! "fadeIn" (quote ((speed . 2) (curve . "api-curve"))))'
ok '(hl-rule? (hl-window-rule-add! "api-rule" (quote ((match . ((class . "^api-main$"))) (opacity . "0.9")))))'
$SCHEME '(define api-layer-rule (hl-layer-rule-add! "api-layer-rule" (quote ((match . ((namespace . "^nope$"))) (blur . #f))))))' >/dev/null 2>&1
$SCHEME '(define api-layer-rule (hl-layer-rule-add! "api-layer-rule" (quote ((match . ((namespace . "^nope$"))) (blur . #f)))))' >/dev/null
ok '(hl-rule? api-layer-rule)'
ok '(boolean? (hl-rule-enabled? api-layer-rule))'
ok '(hl-rule-set-enabled api-layer-rule #f)'

# ---- layouts ----------------------------------------------------------------
idok '(hl-define-layout "api-layout" (lambda (count W H wins) (quote ())))'
noerr '(hl-layout-msg "noop")'

# ---- submaps ----------------------------------------------------------------
idok '(hl-submap "api-sub" (lambda () (hl-bind "" "g" (lambda () #f))))'
ok '(hl-submap-activate! "api-sub")'
val '(hl-current-submap)' '"api-sub"'
ok '(hl-submap-exit!)'
val '(hl-current-submap)' '""'

# ---- binds ------------------------------------------------------------------
idok '(hl-bind "SUPER" "F13" (lambda () #f))'
idok '(hl-bind (kbd "C-M-F15") (lambda () #f))'
idok '(hl-bind "SUPER" "F16" (lambda () #f) (quote release) #t (quote description) "api")'
ok '(let ((b (hl-bind "SUPER" "F18" (lambda () #f)))) (hl-unbind b))'
ok '(let ((b (hl-bind "SUPER" "F19" (lambda () #f)))) b (hl-unbind-key "SUPER F19"))'

# ---- timers -----------------------------------------------------------------
idok '(hl-after 5000 (lambda () #f))'
ok '(let ((t (hl-repeat 5000 (lambda () #f))))
     (and (boolean? (hl-timer-set-enabled t #f))
          (eq? (hl-timer-enabled? t) #f)
          (boolean? (hl-timer-set-timeout t 6000))
          (boolean? (hl-timer-set-enabled t #t))))'

# ---- events: registration returns a listener id -----------------------------
ok '(let ((id (hl-on-window-open (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-open-early (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-close (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-kill (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-title (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-class (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-urgent (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-pin (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-fullscreen (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-move-to-workspace (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-active (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-minimize (lambda (w s) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-bell (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-window-update-rules (lambda (w) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-workspace-active (lambda (ws) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-workspace-created (lambda (ws) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-workspace-removed (lambda (ws) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-workspace-special-active (lambda (ws m) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-workspace-move-to-monitor (lambda (ws m) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-monitor-added (lambda (m) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-monitor-removed (lambda (m) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-monitor-focused (lambda (m) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-monitor-layout-changed (lambda () #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-submap (lambda (s) #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-start (lambda () #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-shutdown (lambda () #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-config-reloaded (lambda () #f)))) (and (integer? id) (>= id 0)))'
ok '(let ((id (hl-on-config-props-refreshed (lambda (b) #f)))) (and (integer? id) (>= id 0)))'

# ---- gestures (registration only; no trackpad in the harness) ----------------
idok '(hl-gesture 4 "swipe" (lambda () #f))'
ok '(hl-gesture-live 4 "swipe" (lambda () #f) (lambda (dx dy s) #f) (lambda () #f))'

# ---- reload (LAST: the animation checks above must precede it) ---------------
ok '(hl-config-reload!)'
val '(+ 40 2)' '42'

[[ $FAILED -eq 0 ]]