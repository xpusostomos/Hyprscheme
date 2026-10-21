# meta-coverage: every public hl- API defined in the bootstrap must be
# referenced by at least one test in this suite. Internal hl-- helpers are
# exempt. Adding a public API without a test fails here by design.
#
# The API list is DERIVED BY INTROSPECTION, not read from a list:
# tools/api-names.scm loads the machinery the way the host does and then asks
# each module what it publishes (module-public-interface). That cannot disagree
# with the code the way a hand-kept list can.
#
# What it replaces: the `#:re-export` list of an umbrella module `(hyprscheme)`,
# which was deleted. That list had already drifted — it named hl-bind-thunk and
# hl-bind-tokens, two internal record accessors no config should see. Before
# that, the list was scraped from the bootstrap with a define pattern, which
# failed silently for this test's whole life (it read an in-C++ copy that no
# longer existed and passed on an empty list).
#
# The vacuity floor stays: fewer than 200 names means the DERIVATION broke, not
# that the API shrank. It earns its place more than ever now, because a failed
# load produces an empty list rather than an error.
#
# Coverage is a substring search for the name, so it proves "referenced by a
# test", not "asserted at runtime" — good enough to force a test to exist.
apis=$(guile --no-auto-compile -s tools/api-names.scm 2>/dev/null | LC_ALL=C sort -u)

count=$(printf '%s\n' "$apis" | LC_ALL=C grep -c .)
if [[ $count -lt 200 ]]; then
  echo "coverage: derived only $count public APIs — the DERIVATION is broken, not the API"
  exit 1
fi

missing=0
for a in $apis; do
  LC_ALL=C grep -qF -- "$a" tests/t-*.sh || { echo "API not covered by any test: $a"; missing=1; }
done
# hl-kbd has no hl- prefix; pin it explicitly
LC_ALL=C grep -qF '(hl-kbd ' tests/t-api.sh || { echo "hl-kbd not covered"; missing=1; }
[[ $missing -eq 0 ]]
