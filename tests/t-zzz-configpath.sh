# The scheme config is discovered the way the compositor discovers its own:
# $XDG_CONFIG_HOME/hypr/hyprland.scm, else each $XDG_CONFIG_DIRS entry, else
# /etc/xdg — the same search Jeremy::getMainConfigPath makes for hyprland.lua
# (Hyprutils::Path::findConfig). The path is resolved ONCE, at plugin load, so
# this rule launches its own compositors with the environment each case needs;
# the suite's compositor cannot be reconfigured for it.
#
# HYPRSCHEME_CONFIG is deliberately UNSET here: the whole point is the default
# search, and run.sh exports the override, which would mask it.

# run.sh sources this file, so $0 is run.sh, not this script — derive the repo
# from the working directory in that case (run.sh runs from the repo root) and
# from $0 when run standalone. BIN and PLUGIN are run.sh's own variables and
# are NOT exported, so they are re-derived here rather than read.
if [[ -f $PWD/tests/t-zzz-configpath.sh ]]; then
  REPO=$PWD
else
  REPO=$(cd "$(dirname "$0")/.." && pwd)
fi
BIN=${BIN:-$REPO/../Hyprland/build/Hyprland}
PLUGIN=${PLUGIN:-$REPO/scheme-plugin-guile.so}
case $PLUGIN in /*) ;; *) PLUGIN=$REPO/$PLUGIN ;; esac

[[ -x $BIN ]] || { echo "no compositor at $BIN"; exit 1; }
[[ -f $PLUGIN ]] || { echo "no plugin at $PLUGIN"; exit 1; }

# run one compositor with the given environment and report which config it
# found. Args: "home"|"dirs"|"both" saying where to place the marker config,
# and the marker text. When the dirs case runs, XDG_CONFIG_DIRS is built with a
# junk leading entry (an empty one and a relative one) so the case also proves
# invalid entries are skipped rather than searched. Prints the marker the
# running config set, or "NONE" if no config was found/ran.
probe() {
  local where=$1 marker=$2
  local W SIG="" P d dirs=""
  local -a BEFORE
  W=$(mktemp -d /tmp/hyprscheme-cfgpath.XXXXXX)
  mkdir -p "$W/home/.config/hypr"
  touch "$W/home/.config/hypr/hyprland.lua"

  [[ $where == home || $where == both ]] && \
    printf '(hl-state-set! (quote cfgpath) "%s")\n' "$marker" > "$W/home/.config/hypr/hyprland.scm"
  if [[ $where == dirs || $where == both ]]; then
    mkdir -p "$W/dirs/hypr"
    printf '(hl-state-set! (quote cfgpath) "%s")\n' "$marker" > "$W/dirs/hypr/hyprland.scm"
    dirs=":relative-junk:$W/dirs"   # empty + relative entries: invalid, skipped
  fi

  # mapfile + "${BEFORE[*]}" (joined with SPACES) — a plain $(ls) is
  # newline-separated, and matching " $d " against it only ever matches the
  # first and last entry, so the suite's own compositor looked like a new one
  # and the probe attached to it. run.sh does it this way for the same reason.
  mapfile -t BEFORE < <(ls "$XDG_RUNTIME_DIR/hypr" 2>/dev/null)
  if [[ -n $dirs ]]; then
    env -u HYPRSCHEME_CONFIG XDG_CONFIG_HOME="$W/home/.config" XDG_CONFIG_DIRS="$dirs" HOME="$W/home" \
      "$BIN" --config "$W/home/.config/hypr/hyprland.lua" > "$W/log" 2>&1 &
  else
    env -u HYPRSCHEME_CONFIG XDG_CONFIG_HOME="$W/home/.config" HOME="$W/home" \
      "$BIN" --config "$W/home/.config/hypr/hyprland.lua" > "$W/log" 2>&1 &
  fi
  P=$!

  for _ in $(seq 1 80); do
    for d in $(ls "$XDG_RUNTIME_DIR/hypr" 2>/dev/null); do
      # ${BEFORE[*]}, not $BEFORE: BEFORE is an array, and a bare $BEFORE is
      # only its FIRST element — so the membership test would match almost
      # nothing and the probe would attach to the suite's compositor.
      if [[ " ${BEFORE[*]} " != *" $d "* ]] && env HYPRLAND_INSTANCE_SIGNATURE=$d timeout 2 hyprctl version >/dev/null 2>&1; then
        SIG=$d; break
      fi
    done
    [[ -n $SIG ]] && break
    sleep 0.5
  done

  if [[ -z $SIG ]]; then
    echo "NOCOMPOSITOR"
  else
    env HYPRLAND_INSTANCE_SIGNATURE=$SIG hyprctl plugin load "$PLUGIN" >/dev/null 2>&1
    sleep 1
    env HYPRLAND_INSTANCE_SIGNATURE=$SIG hyprctl scheme '(hl-state-ref (quote cfgpath) "NONE")' 2>/dev/null
  fi
  # note: every hyprctl above names $SIG explicitly. run.sh exports
  # HYPRLAND_INSTANCE_SIGNATURE for the SUITE's compositor, and a bare hyprctl
  # would talk to that one instead of the compositor this case just launched —
  # which is how this test first "passed" standalone and failed under the suite
  # with "Cannot load a plugin twice!".
  kill -9 $P 2>/dev/null
  rm -rf "$W"
}

# 1. the config is in XDG_CONFIG_HOME/hypr — the ordinary case, and what the
#    suite's own compositor uses
out=$(probe home "HOME")
[[ $out == '"HOME"' ]] || { echo "XDG_CONFIG_HOME config not found: got [$out] want [\"HOME\"]"; exit 1; }

# 2. the config is NOT in XDG_CONFIG_HOME but IS in XDG_CONFIG_DIRS — the case
#    this search exists for: a system-wide config under /etc/xdg or a
#    distribution's config dir, with the user's own directory absent. The
#    leading junk entries prove invalid ones are skipped, not searched.
out=$(probe dirs "DIRS")
[[ $out == '"DIRS"' ]] || { echo "XDG_CONFIG_DIRS config not found: got [$out] want [\"DIRS\"]"; exit 1; }

# 3. a user config SHADOWS the system one — first base wins, as for hyprland.lua.
#    Both files exist and say different things, so the marker names the winner.
W=$(mktemp -d /tmp/hyprscheme-cfgpath.XXXXXX)
mkdir -p "$W/home/.config/hypr" "$W/dirs/hypr"
touch "$W/home/.config/hypr/hyprland.lua"
printf '(hl-state-set! (quote cfgpath) "HOME-WINS")\n' > "$W/home/.config/hypr/hyprland.scm"
printf '(hl-state-set! (quote cfgpath) "DIRS-LOSES")\n' > "$W/dirs/hypr/hyprland.scm"
mapfile -t BEFORE < <(ls "$XDG_RUNTIME_DIR/hypr" 2>/dev/null)
env -u HYPRSCHEME_CONFIG XDG_CONFIG_HOME="$W/home/.config" XDG_CONFIG_DIRS="$W/dirs" HOME="$W/home" \
  "$BIN" --config "$W/home/.config/hypr/hyprland.lua" > "$W/log" 2>&1 &
P=$!
SIG=""
for _ in $(seq 1 80); do
  for d in $(ls "$XDG_RUNTIME_DIR/hypr" 2>/dev/null); do
    if [[ " ${BEFORE[*]} " != *" $d "* ]] && env HYPRLAND_INSTANCE_SIGNATURE=$d timeout 2 hyprctl version >/dev/null 2>&1; then SIG=$d; break; fi
  done
  [[ -n $SIG ]] && break
  sleep 0.5
done
if [[ -z $SIG ]]; then
  echo "precedence case: compositor never became responsive"
  kill -9 $P 2>/dev/null; rm -rf "$W"; exit 1
fi
env HYPRLAND_INSTANCE_SIGNATURE=$SIG hyprctl plugin load "$PLUGIN" >/dev/null 2>&1
sleep 1
out=$(env HYPRLAND_INSTANCE_SIGNATURE=$SIG hyprctl scheme '(hl-state-ref (quote cfgpath) "NONE")' 2>/dev/null)
kill -9 $P 2>/dev/null
rm -rf "$W"
[[ $out == '"HOME-WINS"' ]] || { echo "precedence wrong: got [$out] want [\"HOME-WINS\"]"; exit 1; }
