#!/usr/bin/env bash
# Doc-example live test — extracts every ```scheme block from the
# Hyprscheme wiki (via tests/wiki-examples.awk) and evaluates it through
# hyprctl scheme, so documented examples cannot rot. Runs last of the
# t- tests (zzz) because registering doc binds/rules pollutes the session.
#
# The wiki is found relative to the repo; a missing wiki is a warning,
# not a failure (fresh clones / CI checkouts without the submodule).
WIKI="$PWD/../Hyprscheme.wiki"
if [[ ! -d "$WIKI" ]]; then
  echo "warn: $WIKI not found — doc examples not tested"
  exit 0
fi

BLOCKS=$WORK/wiki-blocks
mkdir -p "$BLOCKS"
if ! awk -v out="$BLOCKS" -v manifest="$BLOCKS/MANIFEST" \
     -f tests/wiki-examples.awk "$WIKI"/*.md; then
  echo "FAIL: wiki-examples.awk extraction error"
  exit 1
fi

# core.md's split-config example loads keybinds.scm — give it an empty one
touch "$XDG_CONFIG_HOME/hypr/keybinds.scm"

FAILED=0
while read -r n f l bal; do
  label="$(basename "$f"):$l"
  if [[ "$bal" != ok ]]; then
    echo "FAIL: $label unbalanced parens/strings in block"
    FAILED=1
    continue
  fi
  code=$(cat "$BLOCKS/$n.scheme")
  skip=""
  while IFS= read -r pat; do
    pat="${pat%%#*}"            # strip trailing reason comments
    pat="${pat%${pat##*[![:space:]]}}"  # rstrip
    [[ -z "$pat" ]] && continue
    if [[ "$code" == *"$pat"* ]]; then skip="$pat"; break; fi
  done < tests/wiki-examples.skip
  [[ -n "$skip" ]] && continue
  out=$($SCHEME "$code" 2>&1)
  if [[ "$out" == "error:"* ]]; then
    echo "FAIL: $label => [$out]"
    FAILED=1
  fi
done < "$BLOCKS/MANIFEST"
[[ $FAILED -eq 0 ]]
