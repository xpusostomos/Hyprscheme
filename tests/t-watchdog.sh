# the watchdog: a runaway eval must be abandoned (error reply), the
# compositor must recover, and the budget must be scheme-configurable.
out=$($SCHEME 'hl--watchdog-ms')
[[ "$out" == "5000" ]] || { echo "default budget gave: $out"; exit 1; }

# shrink the budget so the test is fast, then run a runaway
$SCHEME '(set! hl--watchdog-ms 800)' >/dev/null
out=$($SCHEME '(let loop () (loop))')
[[ "$out" == "watchdog: eval abandoned (see the compositor log)" ]] || { echo "runaway gave: $out"; exit 1; }

# compositor must still be alive and the budget restored for later tests
$SCHEME '(set! hl--watchdog-ms 5000)' >/dev/null
out=$($SCHEME '(+ 40 2)')
[[ "$out" == "42" ]] || { echo "compositor did not recover: $out"; exit 1; }
