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

# ---- a BROKEN FILE PULLED IN BY THE CONFIG raises the banner TOO -------------
# THE NESTED CASE, and the one Chris actually hit: his hyprland.scm is one line,
# (hl-load-config "bindings"), and garbage at the end of bindings.scm produced a
# notification but NO OVERLAY. The cause was not who caught the error — it was
# that the banner was raised and then ERASED by the same load:
#
#   hl-load guards the raise (it must: the rest of the config has to still load),
#   so the raise never reached hl--load-config, which answered #t;
#   the host saw #t and called hl--report-config-ok, clearing the list
#   hl--report-config had filled one call earlier.
#
# Fix: hl--load-config clears the list at the START and answers "nothing was
# RECORDED", so the nested failure makes it answer #f and nothing clears the
# banner. The host was already right — it just needed an answer it could trust.
# See hl--load-config in kernel.scm.
NESTED=$XDG_CONFIG_HOME/hypr/cfg-nested-broken.scm
printf '(define cfg-nested-ok #t)\n\n(define (cfg-nested-half\n' > "$NESTED"
printf '\n(hl-load-config "cfg-nested-broken")\n' >> "$REAL"

out=""
for _ in $(seq 1 20); do
  out=$($SCHEME '(length hl--config-errors)')
  [[ "$out" != "0" ]] && break
  sleep 0.5
done
[[ "$out" != "0" ]] || { echo "a broken file loaded via hl-load-config did not raise the banner (hl--config-errors=$out)"; exit 1; }

# AND IT STAYS UP. This is the assertion the old code fails: it cleared the list
# synchronously inside the same load, so no poll could ever see it — a
# notification and no overlay, exactly as reported.
sleep 2
out=$($SCHEME '(length hl--config-errors)')
[[ "$out" != "0" ]] || { echo "the banner raised for a nested broken file was CLEARED again"; exit 1; }

# a file that fails to load is still WATCHED, which is what lets you fix it in
# place and get a reload — worth pinning, and the next step depends on it
out=$($SCHEME "(member \"$NESTED\" (hl-loaded-files))")
[[ -n "$out" && "$out" != "#f" ]] || { echo "a broken file is not in hl-loaded-files, so fixing it cannot reload (got [$out])"; exit 1; }

# FIXING it clears the banner, the same as for the main config
printf '(define cfg-nested-ok #t)\n' > "$NESTED"
out=""
for _ in $(seq 1 20); do
  out=$($SCHEME '(length hl--config-errors)')
  [[ "$out" == "0" ]] && break
  sleep 0.5
done
[[ "$out" == "0" ]] || { echo "fixing a nested file did not clear the banner (hl--config-errors=$out)"; exit 1; }

# restore, the way the tests above do: drop the appended line, then the blank
# line before it. Then remove the nested file — nothing is left loading it, but a
# stray half-broken file in the config directory is a trap for the next reader.
sed -i '$ d' "$REAL"
sed -i -e :a -e '/^\n*$/{$d;N;};/\n$/ba' "$REAL"
rm -f "$NESTED"
# let that reload land, so the next file in the suite starts from a clean state
out=""
for _ in $(seq 1 20); do
  out=$($SCHEME '(length hl--config-errors)')
  [[ "$out" == "0" ]] && break
  sleep 0.5
done
[[ "$out" == "0" ]] || { echo "the config did not come back clean after the nested test (hl--config-errors=$out)"; exit 1; }
