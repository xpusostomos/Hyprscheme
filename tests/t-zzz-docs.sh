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

# liveness guard: if the compositor is not answering, EVERY block below would
# be counted as a pass — hyprctl reports a connection failure as plain text,
# which does not start with "error:". So probe first and refuse to report
# success without a live compositor. (Belt and braces with the filename-order
# rule: this test must run before t-zzzz-exit.sh, and if the order ever breaks
# again the failure is loud instead of silent.)
probe=$($SCHEME '(hl-version)' 2>&1)
if [[ -z "$probe" || "$probe" == "error:"* || "$probe" == *"Couldn't connect"* || "$probe" == *"not running"* ]]; then
  echo "FAIL: no live compositor — doc blocks not tested (probe => [$probe])"
  exit 1
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
EVALUATED=0
SKIPPED=0
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
  if [[ -n "$skip" ]]; then SKIPPED=$((SKIPPED+1)); continue; fi
  EVALUATED=$((EVALUATED+1))
  out=$($SCHEME "$code" 2>&1)
  if [[ "$out" == "error:"* ]]; then
    echo "FAIL: $label => [$out]"
    FAILED=1
  fi
done < "$BLOCKS/MANIFEST"
# visible when this file is run directly (tests/run.sh only echoes on failure)
echo "doc blocks: evaluated=$EVALUATED skipped=$SKIPPED"
[[ $FAILED -eq 0 ]]
