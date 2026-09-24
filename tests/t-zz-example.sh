#!/usr/bin/env bash
# Live-load examples/hyprland.scm through the plugin. hl--load is guarded:
# a failing top-level form is reported to the COMPOSITOR LOG, not the eval
# reply, so the log is diffed around the load, then the example's visible
# side effects (config writes, the registered master-stack layout's
# layout-msg callback) are asserted.
FAILED=0
ok()  { out=$($SCHEME "$1" 2>&1); [[ "$out" == "#t" ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
val() { out=$($SCHEME "$1" 2>&1); [[ "$out" == "$2" ]] || { echo "FAIL: $1 => [$out] want [$2]"; FAILED=1; }; }

before=$( { grep -c 'scheme] error' "$WORK/compositor.log" 2>/dev/null || echo 0; } )
out=$($SCHEME "(load \"$(pwd)/examples/hyprland.scm\")" 2>&1)
sleep 0.3
after=$( { grep -c 'scheme] error' "$WORK/compositor.log" 2>/dev/null || echo 0; } )
[[ "$after" == "$before" ]] || {
  echo "FAIL: example load raised errors ($before -> $after in log)";
  grep -E 'scheme\] error' "$WORK/compositor.log" | tail -5;
  FAILED=1;
}

val '(hl-config-get "general:layout")' '"scheme:master-stack"'
ok '(let ((g (hl-config-get "general:gaps_out"))) (and (pair? g) (not (not (memv 10 g)))))'
ok '(string? (hl-config-get "input:kb_options"))'

# the master-stack layout is live on the active workspace: its layout-msg
# callback must see the example's "wider"/"narrower" messages
ok '(hl-layout-msg "wider")'
ok '(hl-layout-msg "narrower")'

# done poking — give the workspaces their normal tiled layout back
$SCHEME '(hl-config-add! "general:layout" "dwindle")' >/dev/null

[[ $FAILED -eq 0 ]]