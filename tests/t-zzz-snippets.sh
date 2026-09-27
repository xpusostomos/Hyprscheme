#!/usr/bin/env bash
# The wiki's snippets, RUN — the third and last tier of doc verification.
#
#   t-zzz-docs.sh          evaluates every ```scheme block in the wiki
#   tools/doc-audit.py     every hl-* name the wiki uses exists
#   tools/doc-exercise.scm every callback the wiki registers RUNS, so a name
#                          inside a thunk cannot hide
#   this file              a snippet's callback is run for real and what it DOES
#                          is checked
#
# The first three can all be satisfied by a snippet that never does anything
# useful — they answer "does it exist" and "does it run". This answers "does it
# work", which is the only one that can catch a snippet whose parts are all real
# and whose composition is wrong.
#
# It drives the WIKI's code rather than a copy of it: the block is extracted,
# the thunk is pulled out of the form, and that thunk is called (tests/
# doc-snippets.scm). So a snippet that stops working fails here, in the suite,
# rather than in the config of whoever copied it.

FAILED=0
ok()  { out=$($SCHEME "$1" 2>&1); [[ "$out" == "#t" ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
val() { out=$($SCHEME "$1" 2>&1); [[ "$out" == "$2" ]] || { echo "FAIL: $1 => [$out] want [$2]"; FAILED=1; }; }

WIKI=$PWD/../Hyprscheme.wiki
[[ -d $WIKI ]] || { echo "warn: no wiki — snippets not driven"; exit 0; }

# ---- the blocks, extracted exactly as the doc test extracts them -----------
BLOCKS=$WORK/snippet-blocks
rm -rf "$BLOCKS"; mkdir -p "$BLOCKS"
awk -v out="$BLOCKS" -v manifest="$BLOCKS/MANIFEST" \
    -f tests/wiki-examples.awk "$WIKI"/*.md || { echo "extraction failed"; exit 1; }
pick() { grep -l "$1" "$BLOCKS"/*.scheme 2>/dev/null | head -1; }

MIN=$(pick 'special:minimized')
ZOOM=$(pick 'zoom-toggle-factor')
EVENT=$(pick 'hl-workspace-active-notification-add!')
if [[ -z $MIN || -z $ZOOM || -z $EVENT ]]; then
  echo "could not find the snippet blocks in the wiki — did a page move?"
  exit 1
fi

# the driver, into the generation (the kernel shadows load to land there)
$SCHEME "(load \"$PWD/tests/doc-snippets.scm\")" >/dev/null 2>&1

# ---- "Minimize windows using special workspaces" ---------------------------
# the most composition-heavy snippet on the page: a tag, a special workspace,
# and a toggle that has to undo both
$SCHEME '(hl-exec! "foot -a snippet-win")' >/dev/null 2>&1
WAIT_FOR 10 '(let ((w (hl-window-from "class:^snippet-win$"))) (if w #t #f))' >/dev/null \
  || { echo "FAIL: the snippet fixture window never appeared"; FAILED=1; }
$SCHEME '(begin (hl-window-workspace-set! (hl-window-from "class:^snippet-win$") (hl-active-workspace))
                (hl-window-focus! (hl-window-from "class:^snippet-win$")) #t)' >/dev/null 2>&1

val '(length (hl-workspace-windows "special:minimized"))' '0'

# first press: minimize
ok "(run-snippet-thunk \"$MIN\" \"s-X\")"
WAIT_FOR 10 '(> (length (hl-workspace-windows "special:minimized")) 0)' >/dev/null \
  || { echo "FAIL: the minimize snippet did not park the window"; FAILED=1; }
ok '(> (length (hl-workspace-windows "special:minimized")) 0)'
# ...and it carries the tag the snippet sets, which is how the other half finds it
ok '(let ((w (car (hl-workspace-windows "special:minimized"))))
       (and (member "minimized" (hl-window-tags w)) #t))'

# second press: restore
ok "(run-snippet-thunk \"$MIN\" \"s-X\")"
WAIT_FOR 10 '(= (length (hl-workspace-windows "special:minimized")) 0)' >/dev/null \
  || { echo "FAIL: the restore half did not bring the window back"; FAILED=1; }
ok '(= (length (hl-workspace-windows "special:minimized")) 0)'
# the tag is cleared, so the next press minimizes again rather than doing nothing
ok '(let ((w (hl-window-from "class:^snippet-win$")))
       (not (member "minimized" (hl-window-tags w))))'

# ---- "Windows Magnifier-like cursor zoom" ---------------------------------
# three binds over one helper defined in the same block: it exercises the
# snippet's own arithmetic, not just its names
$SCHEME '(hl-config-add! "cursor:zoom_factor" 1)' >/dev/null 2>&1
ok '(= (hl-config-get "cursor:zoom_factor") 1)'

ok "(run-snippet-thunk \"$ZOOM\" \"s-PLUS\")"
WAIT_FOR 10 '(= (hl-config-get "cursor:zoom_factor") 1.5)' >/dev/null \
  || { echo "FAIL: the zoom-in bind did not raise the zoom factor"; FAILED=1; }
ok '(= (hl-config-get "cursor:zoom_factor") 1.5)'

ok "(run-snippet-thunk \"$ZOOM\" \"s-MINUS\")"
ok '(= (hl-config-get "cursor:zoom_factor") 1)'

# the toggle: at the minimum it jumps to the toggle factor
ok "(run-snippet-thunk \"$ZOOM\" \"s-Z\")"
ok '(= (hl-config-get "cursor:zoom_factor") 1.5)'
ok "(run-snippet-thunk \"$ZOOM\" \"s-Z\")"
ok '(= (hl-config-get "cursor:zoom_factor") 1)'

# ---- "Events": a callback and the type of what it is handed ----------------
# events.md told the reader the workspace event "passes a name" and passed the
# argument straight to string-append. It passes a HANDLE, so every reader who
# copied it got `Wrong type (expecting string)`. Nothing above can see that —
# the names all exist, the block registers cleanly, and the exercise tier can
# only hand a callback a fabricated #f, which makes every type error ambiguous.
# Running it against a real workspace is what settles it.
ok "(run-snippet-fn \"$EVENT\" 'hl-workspace-active-notification-add! (hl-active-workspace))"

exit $FAILED
