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
# the plugin to load: scheme-plugin-guile.so (the maintained backend; Chez is
# deprecated, frozen in chez/). An absolute path is required — the compositor
# resolves plugin paths against ITS cwd, not the caller's, so a bare name
# like PLUGIN=scheme-plugin-guile.so fails to load.
PLUGIN=${PLUGIN:-$HOME/.local/lib/hyprscheme/scheme-plugin-guile.so}
case $PLUGIN in /*) ;; *) PLUGIN=$PWD/$PLUGIN ;; esac

# freshness warning: the .scm machinery is read from the SOURCE TREE at runtime
# (SOURCE_DIR is compiled in, and the loader prefers it) while the plugin is
# whatever $PLUGIN names — so either half can be stale. Two ways to get it
# wrong, both of which report as code bugs:
#   - a stale installed plugin tested against current sources (compared below)
#   - a .scm edited after the last `make`: the plugin registers entry points the
#     .scm no longer calls, and every C-calling API says "Unbound variable"
# A warning, not a failure: testing an installed plugin is legitimate.
newest_scm=$(ls -t "$PWD"/src/config/scheme/*.scm 2>/dev/null | head -1)
if [[ -n ${newest_scm:-} && "$newest_scm" -nt "$PLUGIN" ]]; then
  echo "warn: $(basename "$newest_scm") is newer than the plugin under test"
  echo "warn:   (expected for a Scheme-only edit; if the change touched C++ too, run make)"
fi
for cand in scheme-plugin-guile.so scheme-plugin.so; do
  if [[ -f $PWD/$cand ]]; then
    if ! cmp -s "$PWD/$cand" "$PLUGIN"; then
      echo "warn: loading $PLUGIN, which differs from the tree build $PWD/$cand"
      echo "warn:   (stale plugin against current .scm?) — for the tree build:"
      echo "warn:   PLUGIN=$PWD/$cand $0"
    fi
    break
  fi
done
FILTER=${1:-}
WORK=$(mktemp -d /tmp/hyprscheme-test.XXXXXX)
export WORK
export XDG_CONFIG_HOME=$WORK/config XDG_STATE_HOME=$WORK/state
# the scheme config reaches the plugin through HYPRSCHEME_CONFIG — dogfoods
# the override on every run (if the env handling breaks, no test passes)
export HYPRSCHEME_CONFIG=$XDG_CONFIG_HOME/hypr/hyprland.scm
# the Guile backend must not auto-compile inside the compositor (slow first
# load, ~/.cache/guile writes); harmless on the Chez backend
export GUILE_AUTO_COMPILE=0
mkdir -p "$XDG_CONFIG_HOME/hypr" "$XDG_STATE_HOME"
cp tests/config/hyprland.lua "$XDG_CONFIG_HOME/hypr/"
cp tests/config/hyprland.scm "$XDG_CONFIG_HOME/hypr/"
PASS=0 FAIL=0

cleanup() {
  [[ -n ${CPID:-} ]] && kill -9 "$CPID" 2>/dev/null
  # KEEP=1 preserves the workdir (compositor log + config) for debugging
  if [[ ${KEEP:-0} == 1 ]]; then
    echo "KEEP=1: workdir preserved at $WORK"
    return
  fi
  [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]] && rm -rf "$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE" 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

echo "launching nested compositor (config: $XDG_CONFIG_HOME/hypr)"
mapfile -t BEFORE < <(ls "$XDG_RUNTIME_DIR/hypr" 2>/dev/null)
"$BIN" --config "$XDG_CONFIG_HOME/hypr/hyprland.lua" > "$WORK/compositor.log" 2>&1 &
CPID=$!
# wait for the NEW instance's IPC to answer — the socket file appears well
# before the event loop services it (nested startup can take ~10s under
# software rendering), and the dir list may contain stale entries from
# killed instances, so only a dir that did not exist before AND answers
# hyprctl counts.
for _ in $(seq 1 240); do
  HYPRLAND_INSTANCE_SIGNATURE=""
  for d in $(ls "$XDG_RUNTIME_DIR/hypr" 2>/dev/null); do
    if [[ " ${BEFORE[*]} " != *" $d "* ]] && env HYPRLAND_INSTANCE_SIGNATURE=$d timeout 2 hyprctl version >/dev/null 2>&1; then
      HYPRLAND_INSTANCE_SIGNATURE=$d
      break
    fi
  done
  [[ -n $HYPRLAND_INSTANCE_SIGNATURE ]] && break
  sleep 0.5
done
export HYPRLAND_INSTANCE_SIGNATURE
if [[ -z $HYPRLAND_INSTANCE_SIGNATURE ]]; then
  echo "FAIL: compositor never became responsive"; tail -20 "$WORK/compositor.log"; exit 1
fi
if ! timeout 20 env HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE hyprctl plugin load "$PLUGIN" | grep -q ok; then
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

# The rules run in FILENAME ORDER, and that order is load-bearing:
#   - t-api/t-config/... register binds, rules and layouts, so the wiki
#     doc-example test runs late in the alphabet
#   - t-zzzz-exit.sh QUITS the compositor, so it must sort strictly last.
#     (It used to be t-zz-exit.sh — which sorts BEFORE t-zzz-docs.sh, so the
#     120-block doc test ran against a dead compositor and every block
#     "passed" because hyprctl's connection failure does not start with
#     "error:". That is why the doc test also carries a liveness guard.)
for f in tests/t-*.sh; do
  [[ -n $FILTER && ! $f =~ $FILTER ]] && continue
  run_one "$f"
done

echo
echo "passed: $PASS  failed: $FAIL"
[[ $FAIL -eq 0 ]]
