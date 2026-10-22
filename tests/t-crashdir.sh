# A bad DIRECTION must not be able to take the compositor down.
#
# THE CRASH THIS PINS, which really happened (live session, 2026-10-22 21:44):
# (hl-window-move-direction! w "") passed the string straight down. Math::fromChar
# reads the FIRST CHARACTER only and answers DIRECTION_DEFAULT (-1) for anything
# it cannot map, so "" and "north" and "z" all became "no direction".
# Actions::moveInDirection then converted that back to a string via dirToString
# — which returns "" for DEFAULT — and CLayoutManager::moveInDirection called
# .at(0) on it. std::out_of_range, thrown out through the gsubr trampoline
# (which catches nothing) into libstdc++'s terminate: SIGABRT, whole session.
#
# So this is not only about tidiness. Every assertion here must be true AND the
# compositor must be alive at the end for the file to pass — the alive check is
# the last thing asserted, and t-zzzz-exit asserts the log is clean afterwards.
#
# It is NOT enough that these fail loudly: they must fail in SCHEME, at the call,
# with a message naming the set. A value that reaches C++ unvalidated is the bug.
set -u

fail=0
failures=()          # collected, then printed in one block at the END — run.sh
                     # shows only the first 10 lines of a failing test's output,
                     # and a truncated list reads as "some unspecified failure"
alive() {
  # the nesting compositor this suite launched. If it died, every later
  # assertion is meaningless — say so once, loudly, and stop.
  if ! $SCHEME '(hl-version)' >/dev/null 2>&1; then
    echo "FATAL: the compositor is GONE (it crashed — that is the bug this file pins)"
    echo "       see the compositor log in \$WORK"
    exit 1
  fi
}
refused() {   # refused LABEL EXPR NEEDLE -> EXPR must raise, mentioning NEEDLE
  local label=$1 expr=$2 needle=$3 out
  out=$($SCHEME "(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) $expr)))" 2>&1 | head -2 | tr '\n' ' ')
  alive
  if [[ "$out" != *"$needle"* ]]; then
    failures+=("REJECT $label: wanted [$needle], got [$out]")
    fail=1
  fi
}
works() {     # works LABEL EXPR -> EXPR must answer #t
  local label=$1 expr=$2 out
  out=$($SCHEME "$expr" 2>&1 | head -1)
  alive
  [[ "$out" == "#t" ]] || { failures+=("ACCEPT $label: got [$out]"); fail=1; }
}

# a real window, so the action takes the real path rather than returning early
# on a missing/dead handle (a stale handle would pass this file vacuously)
$SCHEME '(hl-exec! "foot -a crashprobe")' >/dev/null
for _ in $(seq 1 20); do
  [[ "$($SCHEME '(if (hl-window-from "class:^crashprobe$") #t #f)' 2>/dev/null)" == "#t" ]] && break
  sleep 0.5
done
$SCHEME '(define cw (hl-window-from "class:^crashprobe$"))' >/dev/null
[[ "$($SCHEME '(if cw #t #f)' 2>&1 | head -1)" == "#t" ]] || { echo "FAIL: no fixture window"; exit 1; }
echo "  fixture: $($SCHEME '(hl-window-class cw)' 2>&1 | head -1)"

# ---- an out-of-set symbol is refused, naming the set ------------------------
refused "#:direction 'sideways" '(hl-window-move! cw #:direction (quote sideways))' "not a value of this enum"
refused "corner 'middle"        '(hl-cursor-move-to-corner! cw #:corner (quote middle))' "not a value of this enum"
refused "#:state 'sideways"     '(hl-window-send-key-state! cw #:keys (quote ("CTRL" "l")) #:state (quote sideways))' "not a value of this enum"

# ---- the OLD string spellings, INCLUDING the exact input that crashed it -----
# "" is the one that killed the session; the rest are the same class. All of
# them are now refused in Scheme before anything reaches the compositor, and the
# per-letter shorthands ("l"/"r") are gone with the permissive first-character
# rule that made "lizard" mean "left".
for bad in '' 'z' 'north' 'lizard' 'l' 'r' 'u' 'd' 'leftt'; do
  refused "direction \"$bad\"" "(hl-window-move! cw #:direction \"$bad\")" "expected a symbol or an integer"
done

# ---- the same for every other direction taker -------------------------------
refused "swap \"\" "         '(hl-window-swap! cw #:direction "")' "expected a symbol or an integer"
refused "focus \"r\""        '(hl-focus-direction-set! "r")' "expected a symbol or an integer"
refused "focus \"\""         '(hl-focus-direction-set! "")' "expected a symbol or an integer"
refused "into-group \"z\""   '(hl-window-move-into-group! cw #:direction "z")' "expected a symbol or an integer"
refused "into-or-create \"z\"" '(hl-window-move-into-or-create-group! cw #:direction "z")' "expected a symbol or an integer"
refused "out-of-group \"z\"" '(hl-window-move-out-of-group! cw #:direction "z")' "expected a symbol or an integer"
# 'none belongs to direction-or-none-enum and NOT to direction-enum: a function
# taking a plain direction must refuse it, or the two sets are pointless
refused "move #:direction 'none" '(hl-window-move! cw #:direction (quote none))' "not a value of this enum"

# ---- and the VALID spellings still work -------------------------------------
for d in up right down left; do
  works "move #:direction '$d" "(hl-window-move! cw #:direction (quote $d))"
done
works "(hl-window-move!) — default direction + focused window" '(hl-window-move!)'
# out-of-group: the compositor refuses with "Window not in a group" (a WARNING,
# not a crash) and the wrapper answers #f when the window is not grouped, which
# a single-window fixture usually is not. What this file cares about is that the
# DIRECTION VALUE is accepted and reaches the compositor at all, so a boolean is
# the assertion — the refusal, when it comes, is about grouping state rather
# than about the direction (and t-api covers the grouped success path, where its
# fixture has two windows to form a group from).
out=$($SCHEME '(hl-window-move-out-of-group!)' 2>&1 | head -1)
alive
[[ "$out" == "#t" || "$out" == "#f" ]] || { failures+=("out-of-group default 'none: got [$out] — wanted a boolean (a refusal here should be about grouping, not direction)"); fail=1; }
for d in none up right down left; do
  out=$($SCHEME "(hl-window-move-out-of-group! cw #:direction (quote $d))" 2>&1 | head -1)
  alive
  [[ "$out" == "#t" || "$out" == "#f" ]] || { failures+=("out-of-group '$d: got [$out] — wanted a boolean"); fail=1; }
done
works "corner default 'top-left"    '(hl-cursor-move-to-corner!)'
for c in top-left top-right bottom-right bottom-left; do
  works "corner '$c" "(hl-cursor-move-to-corner! cw #:corner (quote $c))"
done

# ---- the ORIGINAL bug this all started from: a translated chord ---------------
# C-f/C-b cursor movement, C-v/M-v paging, C-x C-f — all route through
# send-shortcut -> send-key-state, which used to raise "argument 3 is not an
# integer" on EVERY call and so did nothing at all.
for k in "C-f" "C-b" "C-v" "M-v" "C-x"; do
  works "send-shortcut (hl-kbd \"$k\")" "(hl-window-send-shortcut! cw #:keys (hl-kbd \"$k\"))"
done
for st in down up repeat; do
  works "send-key-state '$st" "(hl-window-send-key-state! cw #:keys (hl-kbd \"C-f\") #:state (quote $st))"
done
# a modifier-only chord has no key to send: (hl-kbd "C-M-") is ("CTRL" "ALT"),
# and sending its last token would send ALT as if it were a keypress
refused "modifier-only chord" '(hl-window-send-shortcut! cw #:keys (hl-kbd "C-M-"))' "needs a key, not only modifiers"

# THE LAST WORD: everything above is worthless if the compositor died somewhere
# along the way, so assert it survived the whole file.
alive
if [[ $fail -eq 0 ]]; then
  echo "  all assertions held; the compositor survived every rejected direction"
  exit 0
fi
# one line per failure, and NOT more than run.sh will print — a list that gets
# truncated reads as "some unspecified failure", which is the state this file
# exists to make impossible
printf 'CRASH-DIR FAILURES (%d):\n' "${#failures[@]}"
printf '  %s\n' "${failures[@]:0:8}"
[[ ${#failures[@]} -gt 8 ]] && printf '  ... and %d more\n' "$(( ${#failures[@]} - 8 ))"
exit 1
