# window rules: registration, effect, handle toggle
$SCHEME '(hl-window-rule-add! "float-check" (quote match) (quote (class "^rule-check$")) (quote float) #t)' >/dev/null || { echo "rule registration failed"; exit 1; }
$SCHEME '(hl-exec-shell! "foot -a rule-check")' >/dev/null
for _ in $(seq 1 20); do
  $HYP clients -j > /tmp/hs-clients-rules.json 2>/dev/null
  python3 - <<'EOF' && break || true
import json
for c in json.load(open('/tmp/hs-clients-rules.json')):
    if c.get('class') == 'rule-check' and c.get('floating') is True:
        exit(0)
raise SystemExit("not floating yet")
EOF
  sleep 0.5
done
$HYP clients -j > /tmp/hs-clients-rules.json
python3 - <<'EOF' || exit 1
import json
for c in json.load(open('/tmp/hs-clients-rules.json')):
    if c.get('class') == 'rule-check':
        assert c.get('floating') is True, "rule-check not floating after rule"
        exit(0)
raise SystemExit("rule-check window not found")
EOF

# disable the rule, new window must not float
$SCHEME '(define rule-check2 (hl-window-rule-add! "float-check2" (quote match) (quote (class "^rule-check2$")) (quote float) #t))' >/dev/null
$SCHEME '(hl-rule-enabled-set! rule-check2 #f)' >/dev/null
$SCHEME '(hl-exec-shell! "foot -a rule-check2")' >/dev/null
sleep 1
$HYP clients -j > /tmp/hs-clients-rules2.json
python3 - <<'EOF' || exit 1
import json
for c in json.load(open('/tmp/hs-clients-rules2.json')):
    if c.get('class') == 'rule-check2':
        assert c.get('floating') is False, "disabled rule still applied"
        exit(0)
raise SystemExit("rule-check2 window not found")
EOF
