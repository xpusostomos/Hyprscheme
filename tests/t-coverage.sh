# meta-coverage: every public hl- API defined in the bootstrap must be
# referenced by at least one test in this suite. Internal hl-- helpers are
# exempt. Adding a public API without a test fails here by design.
#
# The API list comes from `(hyprscheme)` — the public module's #:re-export list,
# which is now the single source of truth for "what is public". It used to be
# scraped out of the bootstrap with a define pattern, which failed silently for
# this test's whole life (it read an in-C++ copy that no longer existed and
# passed on an empty list). Reading an explicit list is the point of the module
# work: nothing is inferred from a naming convention any more.
# Two things keep it honest:
#   - a vacuity floor: a list below 200 names means the extraction broke, not
#     that the API shrank
#   - the umbrellas's list is checked to be non-empty AND to have no hl-- names,
#     since an internal leaking into the public list is the failure mode a
#     curated list is meant to prevent
# Coverage is a substring search for the name, so it proves "referenced by a
# test", not "asserted at runtime" — good enough to force a test to exist.
apis=$(LC_ALL=C sed -n '/#:re-export (/,/))/p' src/config/scheme/hyprscheme.scm \
       | sed -e 's/#:re-export (//' -e 's/))//g' -e 's/(//g' \
       | tr -s '[:space:]' '\n' \
       | LC_ALL=C grep -v '^$' \
       | LC_ALL=C grep -v '^hl--' \
       | sort -u)

count=$(printf '%s\n' "$apis" | LC_ALL=C grep -c .)
if [[ $count -lt 200 ]]; then
  echo "coverage: extracted only $count public APIs from the bootstrap — the extraction is broken, not the API"
  exit 1
fi

missing=0
for a in $apis; do
  LC_ALL=C grep -qF -- "$a" tests/t-*.sh || { echo "API not covered by any test: $a"; missing=1; }
done
# hl-kbd has no hl- prefix; pin it explicitly
LC_ALL=C grep -qF '(hl-kbd ' tests/t-api.sh || { echo "hl-kbd not covered"; missing=1; }
[[ $missing -eq 0 ]]
