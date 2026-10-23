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

# regression: spring params given as EXACT integers must reach the compositor's
# doubles intact — the boundary converts (scm_to_double), the caller does not
okv '(hl-spring-curve-add! "cfg-spring" 250 25 1)' '#t' 'spring curve'

# workspace-rule gap fields accept scalars and per-side plists
okv '(hl-workspace-rule-add! "cfg-gaps" #:gaps_out 0 #:gaps_in 8)' '#t' 'ws-rule scalar gaps'
okv '(hl-workspace-rule-add! "cfg-gaps2" #:gaps_in (quote (top 8 bottom 4)))' '#t' 'ws-rule plist gaps'

# bad type raises with the config system's own message
out=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-config-add! "general:gaps_in" "abc"))))')
[[ "$out" == *"css_gap"* ]] || { echo "bad-type gave: $out"; exit 1; }

# unknown key raises
out=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-config-add! "nonsense:key" 1))))')
[[ "$out" == *"unknown config key"* ]] || { echo "bad-key gave: $out"; exit 1; }

# ---- the config reload follows a SYMLINKED config ---------------------------
# run.sh makes $HYPRSCHEME_CONFIG a symlink to a file it owns. Editing the
# TARGET is how a dotfiles manager's config actually changes, and the events
# fire in the target's directory — so a watch on the symlink's own directory
# never saw them and the config appeared never to reload. (The fix resolves the
# realpath; this asserts the behaviour rather than the implementation.)
REAL=$(readlink -f "$HYPRSCHEME_CONFIG")
[[ -f $REAL ]] || { echo "the harness config is not a symlink to a real file"; exit 1; }

# a marker written by the config itself; hl-state outlives a reload, so the
# marker appearing means the config was RUN again
before=$($SCHEME '(hl-state-ref (quote cfg-reload-marker) 0)')
okv "$before" '0' 'reload marker starts unset'

printf '\n(hl-state-set! (quote cfg-reload-marker) 1)\n' >> "$REAL"
out=""
for _ in $(seq 1 20); do
  out=$($SCHEME '(hl-state-ref (quote cfg-reload-marker) 0)')
  [[ "$out" == "1" ]] && break
  sleep 0.5
done
[[ "$out" == "1" ]] || { echo "editing the symlink TARGET did not reload the config (marker=$out)"; exit 1; }

# put the file back: the appended line would otherwise reload again in every
# later test, and this suite runs one compositor for all of them
sed -i '$ d' "$REAL"
sed -i -e :a -e '/^\n*$/{$d;N;};/\n$/ba' "$REAL"
[[ "$($SCHEME '(hl-state-ref (quote cfg-reload-marker) 0)')" == "1" ]] || { echo "restore reloaded oddly"; }

# ---- and a file loaded from ANOTHER DIRECTORY reloads when edited ------------
# The whole point of (hyprscheme watch). The host loads ONE file — the config —
# and nothing in it says what it reached, so a (hl-load "…") pointing outside the
# config's directory was invisible: no watch, no reload, silent. The interpreter
# knows what it loaded (%load-hook), and this asserts the behaviour end to end:
# inotify watch -> event loop -> reload -> the config re-run.
#
# The scratch file is OUTSIDE the config's directory on purpose. A file beside
# the config proves nothing here — the host watched those already.
SCRATCH_DIR=$(mkdir -p "$WORK/extra" && readlink -f "$WORK/extra")
printf ';; nothing yet\n' > "$SCRATCH_DIR/scratch.scm"

# the config has to reach it; that is what puts it in the watched set
printf '\n(hl-load "%s")\n' "$SCRATCH_DIR/scratch.scm" >> "$REAL"
out=""
for _ in $(seq 1 20); do
  out=$($SCHEME "(member \"$SCRATCH_DIR/scratch.scm\" (hl-loaded-files))")
  [[ -n "$out" && "$out" != "#f" ]] && break
  sleep 0.5
done
[[ -n "$out" && "$out" != "#f" ]] || { echo "a file the config loaded is not in hl-loaded-files (got [$out])"; exit 1; }

# Now edit THAT file — not the config, and not in the config's directory. The
# marker it writes is how the reload is observed: the config re-runs on reload,
# re-loads this file, and therefore runs this line.
printf '(hl-state-set! (quote scratch-reload) 1)\n' >> "$SCRATCH_DIR/scratch.scm"
out=""
for _ in $(seq 1 20); do
  out=$($SCHEME '(hl-state-ref (quote scratch-reload) 0)')
  [[ "$out" == "1" ]] && break
  sleep 0.5
done
[[ "$out" == "1" ]] || { echo "editing a loaded file OUTSIDE the config dir did not reload (marker=$out)"; exit 1; }

# restore the config FIRST: $WORK goes away with this run, and a config still
# loading a file that no longer exists logs an error, which t-zzzz-exit counts
sed -i '$ d' "$REAL"
sed -i -e :a -e '/^\n*$/{$d;N;};/\n$/ba' "$REAL"

# ---- a SYNTAX error in the config raises the BANNER ---------------------------
# THE BUG THIS PINS. The config used to be loaded by a C loader (hl::evalFile in
# Guile.cpp), which caught the error with scm_c_catch and wrote it to fd 2 FROM
# C. A config with a syntax error never reaches EVALUATION at all — it dies in
# the READER — so no Scheme handler ever ran: hl--config-errors stayed EMPTY and
# the persistent banner the user actually looks at never appeared. A config that
# RAISES (an unbound variable) IS caught, which is why the bug survived so long:
# the two failures go through different code, and only the raising one worked.
#
# The load now runs through hl--load-config in Scheme, where ONE guard covers the
# reader and the evaluator alike. This asserts the reader half end to end, which
# nothing else does — a file that LOADS and then raises is a different path.
okv '(length hl--config-errors)' '0' 'no config errors before the broken config'

# an unbalanced paren: the reader dies partway through the file
printf '\n(hl-state-set! (quote syntax-marker) 1\n' >> "$REAL"
out=""
for _ in $(seq 1 20); do
  out=$($SCHEME '(length hl--config-errors)')
  [[ "$out" != "0" ]] && break
  sleep 0.5
done
[[ "$out" != "0" ]] || { echo "a SYNTAX error in the config did not raise the banner (hl--config-errors=$out)"; exit 1; }

# ...and the banner carries the READER'S OWN MESSAGE, which is the only part that
# makes it actionable — "your scheme config has errors" with nothing under it
# would be a banner that tells you nothing
out=$($SCHEME '(let ((e hl--config-errors)) (if (pair? e) (car e) ""))')
[[ "$out" == *"unexpected end of input"* ]] || { echo "the banner does not carry the reader's message (got [$out])"; exit 1; }

# FIXING the file CLEARS it. A banner left up after the problem is fixed is worse
# than no banner at all, so this half matters as much as the other.
sed -i '$ d' "$REAL"
sed -i -e :a -e '/^\n*$/{$d;N;};/\n$/ba' "$REAL"
out=""
for _ in $(seq 1 20); do
  out=$($SCHEME '(length hl--config-errors)')
  [[ "$out" == "0" ]] && break
  sleep 0.5
done
[[ "$out" == "0" ]] || { echo "fixing the config did not clear the banner (hl--config-errors=$out)"; exit 1; }
