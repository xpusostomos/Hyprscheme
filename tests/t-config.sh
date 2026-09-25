# config set/get round-trips
$SCHEME '(hl-config-add! "general:gaps_in" 5)' | grep -q '#t' || { echo "set int failed"; exit 1; }
out=$($SCHEME '(hl-config-get "general:gaps_in")')
# gaps_in is a per-side table; the getter returns it as a PLIST (top 5 ...)
echo "$out" | grep -qE '^5$|^\(' || { echo "get int gave: $out"; exit 1; }

# exact integers must reach INT options as integers (a double is rejected
# upstream — regression: hl--push-val used to send every number as a double)
okv() { out=$($SCHEME "$1" 2>&1); [[ "$out" == "$2" ]] || { echo "$3: got [$out] want [$2]"; exit 1; }; }
okv '(hl-config-add! "binds:drag_threshold" 10)' '#t' 'int option write'
okv '(hl-config-add! "decoration:rounding" 12)' '#t' 'int option 2'

# regression: exact padding zeros broke spring curves (foreign double slots)
okv '(hl-curve-add! "cfg-spring" (quote spring) 250 25 1)' '#t' 'spring curve'

# workspace-rule gap fields accept scalars and per-side plists
okv '(hl-workspace-rule-add! "cfg-gaps" #:gaps_out 0 #:gaps_in 8)' '#t' 'ws-rule scalar gaps'
okv '(hl-workspace-rule-add! "cfg-gaps2" #:gaps_in (quote (top 8 bottom 4)))' '#t' 'ws-rule plist gaps'

# bad type raises with the config system's own message
out=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (display-condition e p))) (hl-config-add! "general:gaps_in" "abc"))))')
[[ "$out" == *"css_gap"* ]] || { echo "bad-type gave: $out"; exit 1; }

# unknown key raises
out=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (display-condition e p))) (hl-config-add! "nonsense:key" 1))))')
[[ "$out" == *"unknown config key"* ]] || { echo "bad-key gave: $out"; exit 1; }
