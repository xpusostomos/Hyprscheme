# hl-exit! quits the compositor — this file must sort STRICTLY LAST, because
# the rules run in filename order and nothing after it can work. The name is
# FOUR z's for that reason: as t-zz-exit.sh it sorted BEFORE t-zzz-docs.sh
# ('e' < 'z'), so the 120-block doc-example test ran against a dead compositor
# and reported success. Anything that quits the compositor belongs here.
# ---- and one last thing: is the log clean? ---------------------------------
# A scheme error that reaches the compositor log is the ONLY signal for a whole
# class of bug that every other check is blind to: a snippet or a callback whose
# names all exist and which runs, but which fails when it is actually called.
# Two were found that way in one pass — the kernel calling a name it could not
# see (every bind returning a plist silently DECLINED instead of consuming its
# key), and a doc snippet passing a workspace handle to `string-append`.
#
#   t-zzz-docs         evaluates a block      — registering never calls it
#   doc-audit          the names exist        — textually, inside thunks too
#   doc-exercise       the callbacks run      — but with fabricated arguments,
#                                               so a type error is ambiguous
#   t-zzz-snippets     a snippet DOES work    — only the snippets it drives
#   this check         nothing errored at all — the backstop for the rest
#
# This file sorts last and the compositor is now down, so the log is complete.
# Errors the tests ASK for are listed; anything else fails the suite. Measuring
# the list against a green run is the point: it held exactly these three.
log_clean() {
  local log=$WORK/compositor.log
  [[ -f $log ]] || { echo "no compositor log to check"; return 1; }
  local intended='watchdog: (eval|layout callback|callback) abandoned|^\[scheme\] error: boom$'
  local unexpected
  unexpected=$(grep '^\[scheme\] error:' "$log" | grep -vE "$intended")
  if [[ -n $unexpected ]]; then
    echo "FAIL: the compositor log carries scheme errors no test asked for:"
    printf '%s\n' "$unexpected" | sed 's/^/    /'
    return 1
  fi
  return 0
}

ok=$($SCHEME '(hl-exit!)')
[[ "$ok" == "#t" ]] || { echo "hl-exit! gave: $ok"; exit 1; }
# the compositor may leave a stale socket file behind on exit — liveness
# is whether IPC still answers, not whether the file exists
for _ in $(seq 1 20); do
  if ! timeout 3 hyprctl version >/dev/null 2>&1; then
    log_clean
    exit $?
  fi
  sleep 0.5
done
echo "compositor still alive after hl-exit!"
exit 1
