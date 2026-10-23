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
# the list against a green run is the point: it held exactly these three, plus
# the one t-api now provokes to prove hl-load-config REPORTS a load that raised
# (api-broken.scm calls a function that does not exist, deliberately).
log_clean() {
  local log=$WORK/compositor.log
  [[ -f $log ]] || { echo "no compositor log to check"; return 1; }
  local intended='watchdog: (eval|layout callback|callback) abandoned|^\[scheme\] error: boom$'
  # t-api's deliberate broken config: the error IS the thing under test
  intended="$intended|^\[scheme\] error: .*Unbound variable: hl-this-does-not-exist"
  # t-api's deliberate RAISING CALLBACKS, which prove an error in a thunk pops a
  # notification (and that a repeat is suppressed). Both messages are asserted by
  # that test, so their log lines are asked for too.
  intended="$intended|^\[scheme\] error: probe: (a different failure|deliberate failure)$"
  # t-config's deliberate SYNTAX errors — unbalanced parens appended to a config
  # file, which prove the READER half reaches the config banner (the half the C
  # loader used to swallow). Quoted loosely because the path is a temp dir, and
  # matched for ANY file rather than naming hyprland.scm: that test now breaks
  # two of them, the main config and a NESTED file loaded through hl-load-config
  # (the case where the banner was raised and then erased by the same load).
  #
  # It can appear TWICE for the nested one, and that is the watcher rather than a
  # bug: the test creates the nested file and appends the hl-load-config line to
  # the config, both land in one inotify buffer, and the loop reloads on each
  # event — by the second, the nested file IS recorded, so it matches. Two
  # events, two reloads, which is what the watcher is supposed to do. (This check
  # is also what once noticed reloads happening twice per edit for the OPPOSITE
  # reason — a duplicate C++ watcher — so the count here is worth reading rather
  # than assuming.)
  intended="$intended|^\[scheme\] error: .*\.scm:.*unexpected end of input while searching for: \)$"
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
