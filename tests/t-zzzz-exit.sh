# hl-exit! quits the compositor — this file must sort STRICTLY LAST, because
# the rules run in filename order and nothing after it can work. The name is
# FOUR z's for that reason: as t-zz-exit.sh it sorted BEFORE t-zzz-docs.sh
# ('e' < 'z'), so the 120-block doc-example test ran against a dead compositor
# and reported success. Anything that quits the compositor belongs here.
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
