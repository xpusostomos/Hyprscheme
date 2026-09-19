# selectors and workspace queries
$SCHEME '(hl-exec-shell! "foot -a sel-win-unique")' >/dev/null
out=$(WAIT_FOR 10 '(let ((w (hl-window-from "class:^sel-win-unique$"))) (if w (hl-window-class w) #f))')
[[ "$out" == '"sel-win-unique"' ]] || { echo "selector gave: $out"; exit 1; }

out=$($SCHEME '(hl-active-workspace)')
[[ -n "$out" && "$out" != '#f' ]] || { echo "active-ws gave: $out"; exit 1; }

out=$($SCHEME '(hl-workspace-name (hl-active-workspace))')
[[ -n "$out" && "$out" != '#f' ]] || { echo "ws-name gave: $out"; exit 1; }

out=$($SCHEME '(hl-version)')
[[ "$out" != '#f' && -n "$out" ]] || { echo "version gave: $out"; exit 1; }
