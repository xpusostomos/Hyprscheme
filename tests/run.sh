#!/usr/bin/env bash
# Hyprscheme test harness — runs the rules in tests/t-*.sh against a nested
# compositor with a controlled config, then reports pass/fail per rule.
#
#   tests/run.sh [name-filter]      # e.g. tests/run.sh watchdog
#
# A rule is a t-*.sh script whose stdout lines are asserted by the script
# itself; exit 0 = pass, nonzero = fail. HYP and HYS (hyprctl wrappers for
# this instance) plus SCHEME (single hyprctl scheme call) are exported.
set -u
cd "$(dirname "$0")/.."

BIN=${BIN:-$HOME/.local/bin/hyprland-scheme}
FILTER=${1:-}
WORK=$(mktemp -d /tmp/hyprscheme-test.XXXXXX)
export XDG_CONFIG_HOME=$WORK/config XDG_STATE_HOME=$WORK/state
mkdir -p "$XDG_CONFIG_HOME/hypr" "$XDG_STATE_HOME"
cp tests/config/hyprland.lua "$XDG_CONFIG_HOME/hypr/"
cp tests/config/hyprland.scm "$XDG_CONFIG_HOME/hypr/"
PASS=0 FAIL=0

cleanup() {
  [[ -n ${CPID:-} ]] && kill -9 "$CPID" 2>/dev/null
  [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]] && rm -rf "$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE" 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

echo "launching nested compositor (config: $XDG_CONFIG_HOME/hypr)"
"$BIN" --config "$XDG_CONFIG_HOME/hypr/hyprland.lua" > "$WORK/compositor.log" 2>&1 &
CPID=$!
for _ in $(seq 1 60); do
  HYPRLAND_INSTANCE_SIGNATURE=$(ls -t "$XDG_RUNTIME_DIR/hypr" 2>/dev/null | head -1)
  [[ -S "$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock" ]] && break
  sleep 0.5
done
export HYPRLAND_INSTANCE_SIGNATURE
if ! timeout 5 env HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE hyprctl plugin load "$HOME/.local/lib/hyprscheme/scheme-plugin.so" | grep -q ok; then
  echo "FAIL: plugin load"; tail -20 "$WORK/compositor.log"; exit 1
fi
sleep 1

export HYPRLAND_INSTANCE_SIGNATURE
export HYP="timeout 10 hyprctl"
export SCHEME="timeout 15 env HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE hyprctl scheme"
# rules source a shared prelude instead of relying on function exports
cat > "$WORK/rules-prelude.sh" <<'PRELUDE'
WAIT_FOR() {
  local secs=$1 expr=$2 out
  for _ in $(seq 1 $((secs*2))); do
    out=$($SCHEME "$expr" 2>/dev/null)
    [[ -n "$out" && "$out" != "#f" ]] && { echo "$out"; return 0; }
    sleep 0.5
  done
  return 1
}
PRELUDE

run_one() {
  local f=$1 name
  name=$(basename "$f" .sh)
  local out
  out=$(
    HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE \
    timeout 60 bash -c "source '$WORK/rules-prelude.sh'; source '$f'" 2>&1
  )
  if [[ $? -eq 0 ]]; then
    PASS=$((PASS+1)); echo "ok   $name"
  else
    FAIL=$((FAIL+1)); echo "FAIL $name"
    [[ -n ${out:-} ]] && echo "$out" | sed 's/^/    /' | head -10
  fi
}

for f in tests/t-*.sh; do
  [[ -n $FILTER && ! $f =~ $FILTER ]] && continue
  run_one "$f"
done

echo
echo "passed: $PASS  failed: $FAIL"
[[ $FAIL -eq 0 ]]
