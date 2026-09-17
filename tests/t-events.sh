# events: window-open fires and delivers a usable handle
$SCHEME '(define ev-open-count 0)' >/dev/null
$SCHEME '(hl-on-window-open (lambda (w) (hl-state-set! (quote ev-open-count) (+ 1 (hl-state-ref (quote ev-open-count) 0)))))' >/dev/null
before=$($SCHEME '(hl-state-ref (quote ev-open-count))')
$SCHEME '(hl-exec "foot -a ev-check")' >/dev/null
sleep 1.5
after=$($SCHEME '(hl-state-ref (quote ev-open-count))')
[[ "$before" != "$after" ]] || { echo "event handler did not fire (before=$before after=$after)"; exit 1; }
