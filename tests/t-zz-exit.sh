# hl-exit! quits the compositor — this file is named zz so it runs LAST and
# the harness tears the instance down right after. Everything before the
# exit call must have completed.
ok=$($SCHEME '(hl-exit!)')
[[ "$ok" == "#t" ]] || { echo "hl-exit! gave: $ok"; exit 1; }
# the compositor may leave a stale socket file behind on exit — liveness
# is whether IPC still answers, not whether the file exists
for _ in $(seq 1 20); do
  timeout 3 hyprctl version >/dev/null 2>&1 || exit 0
  sleep 0.5
done
echo "compositor still alive after hl-exit!"
exit 1
