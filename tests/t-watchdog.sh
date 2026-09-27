# the watchdog: a runaway eval must be abandoned (error reply), the
# compositor must recover, and the budget must be scheme-configurable.
out=$($SCHEME 'hl--watchdog-ms')
[[ "$out" == "5000" ]] || { echo "default budget gave: $out"; exit 1; }

# shrink the budget so the test is fast, then run a runaway
$SCHEME '(set! hl--watchdog-ms 800)' >/dev/null
out=$($SCHEME '(let loop () (loop))')
[[ "$out" == "watchdog: eval abandoned (see the compositor log)" ]] || { echo "runaway gave: $out"; exit 1; }

# compositor must still be alive and the budget restored for later tests
$SCHEME '(set! hl--watchdog-ms 5000)' >/dev/null
out=$($SCHEME '(+ 40 2)')
[[ "$out" == "42" ]] || { echo "compositor did not recover: $out"; exit 1; }

# a runaway LAYOUT callback is abandoned too. core.md promises this ("an
# infinite loop in a bind or a layout recalculate"), and a layout is driven
# from the compositor's layout pass, so a hang here is the worst kind: the
# workspace never lays out and the compositor is wedged.
#
# A layout needs a window to lay out — with no targets, the compositor never
# calls recalculate at all, and the test would pass without ever running the
# runaway — and switching general:layout is what fires one.
$SCHEME '(hl-exec! "foot -a wd-win")' >/dev/null
WAIT_FOR 10 '(let ((w (hl-window-from "class:^wd-win$"))) (if w #t #f))' >/dev/null \
  || { echo "watchdog fixture window never appeared"; exit 1; }

# The loop fires ONCE. Re-running it on every recalculate would leave the
# compositor costing a full watchdog budget per layout pass for as long as the
# layout is selected — alive, but crawling — which is a runtime hazard this
# test should not manufacture on top of the one it is testing.
$SCHEME '(set! hl--watchdog-ms 800)' >/dev/null
$SCHEME '(hl-layout-add! "watchdog-layout" (quote recalculate)
           (lambda (area placements)
             (hl-state-set! (quote wd-layout-entered) #t)   ; proves we got in
             (if (hl-state-ref (quote wd-layout-looped))
                 (quote ())                                ; already proved it
                 (begin (hl-state-set! (quote wd-layout-looped) #t)
                        (let loop () (loop))))))' >/dev/null
$SCHEME '(hl-config-add! "general:layout" "scheme:watchdog-layout")' >/dev/null 2>&1

out=$($SCHEME '(hl-workspace-tiled-layout (hl-active-workspace))')
[[ "$out" == '"scheme:watchdog-layout"' ]] || { echo "layout not selected: [$out]"; exit 1; }
# the runaway really happened — the callback ran, so it entered the loop
out=$($SCHEME '(hl-state-ref (quote wd-layout-entered))')
[[ "$out" == "#t" ]] || { echo "the runaway layout never ran: [$out]"; exit 1; }
# and the compositor still answers: this is the whole point — before the layout
# paths were guarded, only the C++ detector stood between a looping recalculate
# and a wedged compositor, and it could not free it.
out=$($SCHEME '(+ 40 2)')
[[ "$out" == "42" ]] || { echo "a runaway layout froze the compositor: $out"; exit 1; }

$SCHEME '(set! hl--watchdog-ms 5000)' >/dev/null
# put the stock layout back and drop the fixture: while the looping layout is
# selected, every recalculate costs the full watchdog budget before falling
# back, so leaving it in place would slow the rest of the suite.
$SCHEME '(hl-config-add! "general:layout" "dwindle")' >/dev/null 2>&1
$SCHEME '(hl-window-kill! (hl-window-from "class:^wd-win$"))' >/dev/null 2>&1
