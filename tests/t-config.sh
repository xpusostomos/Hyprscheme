# config set/get round-trips
$SCHEME '(hl-config-add! "general:gaps_in" 5)' | grep -q '#t' || { echo "set int failed"; exit 1; }
out=$($SCHEME '(hl-config-get "general:gaps_in")')
# gaps_in is a per-side table; the getter returns it as a PLIST (top 5 ...)
echo "$out" | grep -qE '^5$|^\(' || { echo "get int gave: $out"; exit 1; }

# bad type raises with the config system's own message
out=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (display-condition e p))) (hl-config-add! "general:gaps_in" "abc"))))')
[[ "$out" == *"css_gap"* ]] || { echo "bad-type gave: $out"; exit 1; }

# unknown key raises
out=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (display-condition e p))) (hl-config-add! "nonsense:key" 1))))')
[[ "$out" == *"unknown config key"* ]] || { echo "bad-key gave: $out"; exit 1; }
