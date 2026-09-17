# eval arithmetic and quoting
out=$($SCHEME '(+ 40 2)')
[[ "$out" == "42" ]] || { echo "arith gave: $out"; exit 1; }

out=$($SCHEME '(string-append "a" "b")')
[[ "$out" == '"ab"' ]] || { echo "string gave: $out"; exit 1; }
