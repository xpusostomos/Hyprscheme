# exec spawns (side-effect file), legacy bracket-prefix rules, plist rule object
rm -f /tmp/hs-test-exec
$SCHEME '(hl-exec! "touch /tmp/hs-test-exec")' | grep -qE '^[0-9]+$' || { echo "exec pid failed"; exit 1; }
for _ in $(seq 1 20); do [[ -f /tmp/hs-test-exec ]] && break; sleep 0.2; done
[[ -f /tmp/hs-test-exec ]] || { echo "exec side effect missing"; exit 1; }

out=$($SCHEME '(hl-exec! "/usr/bin/touch /tmp/hs-test-raw")')
echo "exec gave: $out"
[[ "$out" =~ ^[0-9]+$ ]] || { echo "exec pid failed"; exit 1; }
for _ in $(seq 1 20); do [[ -f /tmp/hs-test-raw ]] && break; sleep 0.2; done
[[ -f /tmp/hs-test-raw ]] || { echo "raw side effect missing"; exit 1; }

$SCHEME '(hl-exec! "[float] foot -a rules-test")' | grep -qE '^[0-9]+$' || { echo "exec bracket-rules failed"; exit 1; }
sleep 1
$HYP clients -j > /tmp/hs-clients.json
python3 - <<'EOF' || exit 1
import json
for c in json.load(open('/tmp/hs-clients.json')):
    if c.get('class') == 'rules-test':
        assert c.get('floating') is True, "rules-test window not floating"
        exit(0)
raise SystemExit("rules-test window not found")
EOF
