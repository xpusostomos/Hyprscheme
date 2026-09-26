# meta-coverage: every public hl- API defined in the bootstrap must be
# referenced by at least one test in this suite. Internal hl-- helpers are
# exempt. Adding a public API without a test fails here by design.
#
# The API list comes from the bootstrap itself — it has lived in its own .scm
# file since the machinery was extracted out of SchemeManager.cpp, and this
# test spent that whole time reading the old in-C++ copy, matching nothing and
# passing on an empty list. Three things keep it honest now:
#   - it reads src/config/scheme/hyprscheme-bootstrap.scm
#   - the pattern is POSIX-safe and matches define AND define*, with '-' LAST in
#     the bracket expression and LC_ALL=C, so it means the same thing under
#     every grep and locale (this machine's grep is ugrep, which rejects
#     [a-z0-9-?] outright as "Invalid range end"; it also misses '=' and '!',
#     so hl-exec! and the hl-*-=? predicates would never be listed)
#   - a vacuity floor: a list below 200 names means the extraction broke, not
#     that the API shrank
# Coverage is a substring search for the name, so it proves "referenced by a
# test", not "asserted at runtime" — good enough to force a test to exist.
apis=$(LC_ALL=C grep -ohE '^\(define\*? \(hl-[a-z0-9=?!*-]+' \
         src/config/scheme/hyprscheme-bootstrap.scm \
       | sed -E 's/^\(define\*? \(//' \
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
