# events: window-open fires and delivers a usable handle
#
# The workspace this file starts on, captured BEFORE anything below moves
# windows around. The move test switches the session to workspace 9, and a
# capture taken after that would record "9" — so "restoring" would restore the
# wrong one and leave the rest of the suite sitting where this file left it.
ws0=$($SCHEME '(hl-workspace-name (hl-active-workspace))' 2>/dev/null | tr -d '"')

$SCHEME '(define ev-open-count 0)' >/dev/null
$SCHEME '(hl-window-open-notification-add! (lambda (w) (hl-state-set! (quote ev-open-count) (+ 1 (hl-state-ref (quote ev-open-count) 0)))))' >/dev/null
before=$($SCHEME '(hl-state-ref (quote ev-open-count))')
$SCHEME '(hl-exec! "foot -a ev-check")' >/dev/null
sleep 1.5
after=$($SCHEME '(hl-state-ref (quote ev-open-count))')
[[ "$before" != "$after" ]] || { echo "event handler did not fire (before=$before after=$after)"; exit 1; }

# ---- THE PAYLOAD RULE: an event that fires is DELIVERED ---------------------
# A missing payload crosses as #f and the event is NEVER swallowed. Three cases
# here, all of which were wrong in different ways and all of which the
# compositor's own Lua delivers:
#   - window.destroy delivered NO argument at all
#   - window.move_to_workspace captured the workspace and threw it away
#   - window.active was DROPPED ENTIRELY when focus was cleared
# The third is the one that matters most: a dropped event is not one fewer call,
# it is a state change the handler never learns about.

# window.destroy: ONE argument, a window handle. Born dead — it fires from the
# window's teardown — but it is a handle, so the handler can at least tell WHICH
# window it is. A zero-argument handler leaves the value unset and the assertion
# below reports that rather than passing vacuously.
$SCHEME '(hl-state-set! (quote ev-destroy) (quote none))' >/dev/null
$SCHEME '(hl-window-destroy-notification-add! (lambda (w) (hl-state-set! (quote ev-destroy) (if (hl-window? w) (quote handle) (quote not-a-handle)))))' >/dev/null
$SCHEME '(hl-exec! "foot -a ev-destroy-me")' >/dev/null
sleep 1.5
$SCHEME '(let ((w (hl-window-from "class:^ev-destroy-me$"))) (if w (hl-window-close! w)))' >/dev/null
sleep 2
out=$($SCHEME '(hl-state-ref (quote ev-destroy))')
[[ "$out" == "handle" ]] || { echo "window.destroy: want a window handle, got [$out]"; exit 1; }

# window.move_to_workspace: TWO arguments — the window and WHERE IT WENT.
# The workspace is the half that used to be dropped, so it is asserted BY NAME:
# a handler that only ever hears "it moved" cannot act on where it went.
$SCHEME '(hl-state-set! (quote ev-move) (quote none))' >/dev/null
$SCHEME '(hl-window-move-to-workspace-notification-add! (lambda (w ws) (hl-state-set! (quote ev-move) (list (if (hl-window? w) 1 0) (if (hl-workspace? ws) (hl-workspace-name ws) #f)))))' >/dev/null
$SCHEME '(hl-exec! "foot -a ev-move-me")' >/dev/null
sleep 1.5
moved=$($SCHEME '(let ((w (hl-window-from "class:^ev-move-me$"))) (if w (hl-window-workspace-set! w "9") #f))')
sleep 1.5
out=$($SCHEME '(hl-state-ref (quote ev-move))')
[[ "$out" == "(1 \"9\")" ]] || { echo "window.move_to_workspace: want (1 \"9\") for (window workspace-name), got [$out] (move said $moved)"; exit 1; }
$SCHEME '(let ((w (hl-window-from "class:^ev-move-me$"))) (if w (hl-window-close! w)))' >/dev/null
sleep 1
# Moving that window carried the session to workspace 9. Put it back, so the
# next block starts from the workspace this file began on.
[[ -n "$ws0" ]] && $SCHEME "(hl-workspace-focus! \"$ws0\")" >/dev/null
sleep 1

# window.active: BOTH directions, because the event only fires when focus
# actually CHANGES — FocusState.cpp returns early on
# `if (m_focusWindow.expired() && !pWindow)`, so asking for "no focus" when
# there is already no focus emits NOTHING. An earlier version of this block
# only did the second half and so asserted on an event that was never going to
# arrive; it passed or failed depending on what the previous test happened to
# leave focused. The first half guarantees the transition the second half
# needs, and proves the handler is alive before the #f assertion leans on it.
# The handler records EVERY delivery, newest first, not just the last one: the
# assertion is on the newest, and a failure prints the whole sequence — which is
# what distinguishes "the clear never fired" from "the clear fired and something
# re-focused afterwards".
$SCHEME '(hl-state-set! (quote ev-active) (quote ()))' >/dev/null
$SCHEME '(hl-window-active-notification-add! (lambda (w) (hl-state-set! (quote ev-active) (cons (if w (quote window) (quote cleared)) (hl-state-ref (quote ev-active) (quote ()))))))' >/dev/null
$SCHEME '(hl-exec! "foot -a ev-active-me")' >/dev/null
sleep 1.5
out=$($SCHEME '(hl-state-ref (quote ev-active))')
[[ "${out:0:7}" == "(window" ]] || { echo "window.active: focusing a window should deliver a handle, got [$out]"; exit 1; }

# Focus away to a workspace with nothing on it and the callback must hear about
# it as #f rather than not at all.
#
# "name:ev-empty-ws", NOT "ev-empty-ws". A BARE name is not a way to reach a
# workspace that does not exist yet: the resolver's fallback is
# `targetFor(query().input(in).run())`, a lookup among EXISTING workspaces, so an
# unknown bare name yields an invalid target and changeWorkspace answers "Bad
# workspace" without switching — the whole block then silently tests nothing,
# which is exactly how an earlier version of this failed. The `name:` prefix is
# the spelling that creates. The workspace is fresh, so nothing can be on it and
# nothing there can take focus on the way out.
$SCHEME '(hl-workspace-focus! "name:ev-empty-ws")' >/dev/null
sleep 1.5
out=$($SCHEME '(hl-state-ref (quote ev-active))')
[[ "${out:0:8}" == "(cleared" ]] || { echo "window.active: focus clearing should deliver #f, got [$out] (on $($SCHEME '(hl-workspace-name (hl-active-workspace))'))"; exit 1; }

$SCHEME '(let ((w (hl-window-from "class:^ev-active-me$"))) (if w (hl-window-close! w)))' >/dev/null
sleep 1
[[ -n "$ws0" ]] && $SCHEME "(hl-workspace-focus! \"$ws0\")" >/dev/null
