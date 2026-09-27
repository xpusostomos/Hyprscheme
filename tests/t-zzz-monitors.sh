#!/usr/bin/env bash
# Multi-monitor: the geometry a custom layout works in is GLOBAL.
#
# Every other test in the suite runs on one nested output sitting at the
# origin, where a layout's coordinates and the work area's origin are
# indistinguishable. That is exactly the blind spot FABLE §2.5 describes: a
# layout was handed only the work area's SIZE, so a layout written on the
# primary monitor placed windows wrongly on any other one, and nothing could
# see it.
#
# So: a second output is created (headless — the one step with no API, see
# `output create` in the compositor's IPC), the primary is moved OFF the
# origin, and a layout that places every window at the work area's own corner
# is driven. Its boxes are global, so the windows must come out at the
# primary's corner, not at 0.
#
# This runs late (after the doc test, before the exit test) because it changes
# the monitor topology for everything after it. It tears that down itself.

MON=T-MON          # the headless output

# ---- the second output ------------------------------------------------------
# the printed form of a Scheme string carries its quotes
PRIM=$($SCHEME '(hl-monitor-name (hl-active-monitor))' 2>/dev/null | tr -d '"')
[[ -n $PRIM ]] || { echo "no active monitor to work with"; exit 1; }

# teardown runs even on failure: without it the rest of the suite would run on
# two monitors, and the primary would stay at x=1920
cleanup() {
  $HYP output remove "$MON" >/dev/null 2>&1
  $SCHEME "(hl-monitor-rule-add! \"$PRIM\" #:position \"0x0\")" >/dev/null 2>&1
}
trap cleanup EXIT

$HYP output create headless "$MON" >/dev/null 2>&1
n=0
for _ in $(seq 1 20); do
  n=$($SCHEME '(length (hl-monitors))' 2>/dev/null)
  [[ "$n" == "2" ]] && break
  sleep 0.5
done
[[ "$n" == "2" ]] || { echo "the headless output never appeared (monitors: $n)"; exit 1; }

# the primary at 1920 and the second at 0: a work area that does not start at
# the origin, which is the whole point. These are CONFIG WRITES, and a config
# write rebuilds the Scheme generation — so the layout below is registered
# after them, never before.
$SCHEME "(hl-monitor-rule-add! \"$MON\" #:position \"0x0\")" >/dev/null 2>&1
$SCHEME "(hl-monitor-rule-add! \"$PRIM\" #:position \"1920x0\")" >/dev/null 2>&1
sleep 3

mx=$($SCHEME "(hl-monitor-x (hl-monitor-from \"$PRIM\"))" 2>/dev/null)
[[ -n "$mx" && "$mx" -gt 0 ]] || { echo "the primary did not move off the origin (x=$mx)"; exit 1; }

# ---- a layout that places windows at the work area's own corner -------------
$SCHEME '(hl-layout-add! "t-multi" (quote recalculate)
           (lambda (area placements)
             (hl-state-set! (quote multi-area)
                            (list (hl-box-x area) (hl-box-y area) (hl-box-w area) (hl-box-h area)))
             (hl-state-set! (quote multi-calls) (1+ (or (hl-state-ref (quote multi-calls)) 0)))
             (map (lambda (p)
                    (cons (car p) (hl-box (hl-box-x area) (hl-box-y area) 200 150)))
                  placements))
           (quote layout-msg) (lambda (msg) #t))' >/dev/null 2>&1
$SCHEME '(hl-workspace-rule-add! "8" #:layout "scheme:t-multi")' >/dev/null 2>&1

# Focus the workspace FIRST: `hl-layout-msg` below goes to the ACTIVE
# workspace's layout, so ws 8 has to be the active one for the drive to reach
# ours.
$SCHEME '(hl-workspace-focus! "8")' >/dev/null 2>&1

# A window on that workspace — MOVED there, not opened there: the compositor
# opens new windows on the monitor under the cursor, and the cursor is on the
# output at the origin.
$SCHEME '(hl-exec! "foot -a multi-win")' >/dev/null 2>&1
W=""
for _ in $(seq 1 20); do
  W=$($SCHEME '(hl-window-from "class:^multi-win$")' 2>/dev/null)
  [[ -n "$W" && "$W" != "#f" ]] && break
  sleep 0.5
done
[[ -n "$W" && "$W" != "#f" ]] || { echo "the fixture window never appeared"; exit 1; }
$SCHEME '(hl-window-workspace-set! (hl-window-from "class:^multi-win$") (hl-workspace-from "8"))' >/dev/null 2>&1

# drive the recalculate: a window arriving does not reliably trigger one
calls=0
for _ in $(seq 1 15); do
  $SCHEME '(hl-layout-msg "noop")' >/dev/null 2>&1
  calls=$($SCHEME '(or (hl-state-ref (quote multi-calls)) 0)' 2>/dev/null)
  [[ -n "$calls" && "$calls" -gt 0 ]] && break
  sleep 0.5
done

# 1. the layout was driven at all — otherwise everything below passes vacuously
[[ -n "$calls" && "$calls" -gt 0 ]] \
  || { echo "the layout was never driven on the second monitor (calls=$calls)"; exit 1; }

# 2. THE assertion: the work area the layout was handed starts at the PRIMARY's
#    x, not at the origin. Under the old payload a layout could not know this —
#    it was handed the size and nothing else.
area=$($SCHEME '(hl-state-ref (quote multi-area))' 2>/dev/null)
ax=$(echo "$area" | tr -d '()' | awk '{print $1}')
[[ -n "$ax" && "$ax" -ge "$mx" ]] \
  || { echo "work area x: want >= $mx (the primary), got [$ax] from $area"; exit 1; }

# 3. and the boxes it returned were applied in that SAME global space: a layout
#    placing windows at the work area corner put them on the primary monitor.
#    The old origin-dropping behaviour would land them at 0 — the other output.
pos=$($SCHEME '(let ((w (hl-window-from "class:^multi-win$"))) (and w (hl-window-position w)))' 2>/dev/null)
px=$(echo "$pos" | tr -d '()' | awk -F. '{print $1}')
[[ -n "$px" && "$px" -ge "$mx" ]] \
  || { echo "window x: want >= $mx (on the monitor at $mx), got [$px] from $pos"; exit 1; }

# 4. and the geometry really is relative to that origin: the window sits at the
#    work area's corner, within the inset the compositor applies (border+gaps)
inset=$(( ax - mx ))
dx=$(( px - ax ))
[[ "$dx" -ge 0 && "$dx" -le 6 ]] \
  || { echo "window x [$px] is not at the work area corner [$ax] (+<=6 inset)"; exit 1; }

# ---- put the world back, and CHECK that it went back ------------------------
# This test changes the monitor topology for everything after it, so it does
# not get to assume its own teardown worked. (The trap still covers the
# failure paths above; cleanup is idempotent.)
cleanup
sleep 3
nm=$($SCHEME '(length (hl-monitors))' 2>/dev/null)
[[ "$nm" == "1" ]] || { echo "teardown left $nm monitors, not 1"; exit 1; }
rx=$($SCHEME "(hl-monitor-x (hl-monitor-from \"$PRIM\"))" 2>/dev/null)
[[ "$rx" == "0" ]] || { echo "teardown left the primary at x=$rx, not 0"; exit 1; }

echo "multi-monitor: area x=$ax (monitor x=$mx), window x=$px, $calls recalculates"
