# The generation's environment comes from init.scm, not from a list in C++.
#
# Host.cpp used to assemble the generation by importing twenty-three modules
# itself — a list that duplicated init.scm's and could drift from it. Now
# init.scm is read WITH THE GENERATION CURRENT, so its `use-modules` ARE the
# generation's environment, and the host names no module at all.
#
# Two ways that can silently rot, and this test is for both:
#
#   1. init.scm read with the KERNEL current again (the old order). Its imports
#      land in the kernel, and the generation still REACHES every name, because
#      Guile resolves through a used module's own use-list, transitively — so
#      every other test in the suite still passes while the generation's own
#      environment is empty. Only the generation's use-list shows it.
#   2. a module dropped from init.scm's list. Its names stop being reachable at
#      all, which surfaces as an unbound variable the first time a config calls
#      one.
#
# The assertion is therefore the generation's OWN USE-LIST — the list
# `use-modules` grows, and the thing that would be empty in case 1. A plain
# "can I call this function" check passes in both cases and would be worthless
# here.
FAILED=0
ok() { out=$($SCHEME "$1" 2>&1); [[ "$out" == "#t" ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
val() { out=$($SCHEME "$1" 2>&1); [[ "$out" == "$2" ]] || { echo "FAIL: $1 => [$out] want [$2]"; FAILED=1; }; }

# `load` is shadowed by the kernel to target the current generation, so a load
# from hyprctl lands in the generation — which is where a config runs, and the
# only place this question can be asked from.
$SCHEME '(load "'"$PWD"'/tests/generation-probe.scm")' >/dev/null 2>&1

# the probe is running in the generation, not somewhere else — otherwise every
# assertion below could be answered by the wrong module
ok 'hl--generation-probe-here'

# every family, plus core, extras, emacs-keymaps and the kernel, must be in the
# generation's use-list: one list, so a missing module is NAMED in the failure
ok '(generation-missing (quote
      ((hyprscheme core) (hyprscheme bind) (hyprscheme config)
       (hyprscheme event) (hyprscheme exec) (hyprscheme gesture)
       (hyprscheme group) (hyprscheme layer) (hyprscheme monitor)
       (hyprscheme notification) (hyprscheme query) (hyprscheme rule)
       (hyprscheme timer) (hyprscheme window) (hyprscheme workspace)
       (hyprscheme extras) (hyprscheme emacs-keymaps) (hyprscheme kernel))))'

# the standard library a config has always had is there too, and init.scm is now
# what supplies it — a config using filter/exists must not break
ok '(generation-uses? (quote (srfi srfi-1)))'
ok '(generation-uses? (quote (rnrs io ports)))'
ok '(generation-uses? (quote (ice-9 optargs)))'
# and it WORKS, not merely that the module is listed: call-with-string-output-port
# returns the accumulated string, so the display has to be observed that way
# (asserting the thunk's value would compare against "" — see t-keydesc.sh)
val '(call-with-string-output-port (lambda (p) (display "ok" p)))' '"ok"'

# the umbrella is NOT imported: the generation takes the families and the kernel
# directly, so hl-- helpers stay reachable. This is the check that the module
# init.scm ran in is the GENERATION and not something with a different list.
ok '(not (generation-uses? (quote (hyprscheme))))'

# and a real call through an imported family still works, end to end
ok '(let ((b (hl-bind-add! (hl-kbd "C-<f20>") (lambda () #f))))
       (and (hl-bind? b) (hl-unbind! b)))'

# `hl--ready` is the kernel's flag the host sets once everything loaded; the
# generation reads it through its import, so this also proves the kernel import
# is live rather than a name that happens to resolve
ok 'hl--ready'

exit $FAILED
