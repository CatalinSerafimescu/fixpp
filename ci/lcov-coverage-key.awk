# Canonical coverage KEY for #267 acceptance item 4 — read by
# ci/run-parallelism-aba.sh:coverage_digest and by ci/test-parallelism-aba-seam.sh.
#
# ── WHY THIS IS A SEPARATE FILE ──────────────────────────────────────────────
#
# It lives outside the driver so a fixture can be run through it directly, which
# is what the seam cells do. It was also written inline first, and the inline
# version was WRONG in a way that only a direct test could show: an awk statement
# ends at a newline unless the line ends in an operator, and a `print` whose last
# token was the string `","` silently terminated there — dropping the taken-bit
# from every BRDA record, so every branch compared EQUAL. That failure made the
# data look MORE stable than it is, which is the direction that ships.
#
# ── WHAT IS IN THE KEY, AND WHAT IS DELIBERATELY NOT ─────────────────────────
#
# EXECUTION COUNTS ARE NOT A COVERAGE FACT. `DA:<line>,<count>` and
# `FNDA:<count>,<name>` carry how many times a line or function ran, which moves
# run-to-run for any timing- or iteration-dependent loop. Hashing them made the
# digest report a difference on every run: it voided its own serial baseline on
# a pair of passes that had covered exactly the same code, which reads as a
# suite defect rather than as the instrument fault it was. Both are reduced here
# to a covered/not bit.
#
# ⚠️ BRANCH RECORDS (`BRDA:`/`BRH:`/`BRF:`) ARE EXCLUDED, AND THAT IS A REAL
# SCOPE LIMIT, NOT A DETAIL. This key covers LINE and FUNCTION coverage only.
# The reason is a measured CONDITION: branch coverage has been observed to move
# between two passes at FIXED concurrency — same binary, same machine, same
# `j=1` — while line and function coverage did not. Including it therefore
# leaves the baseline unable to agree with itself, and item 4 can never conclude
# anything in either direction.
#
# ⚠️ THE COUNTS THAT WERE HERE ARE DELETED ON PURPOSE. They were results read
# off one uploaded artifact that is not in this tree, copied into three separate
# files — unverifiable where they sat, and rotting from the moment they were
# written. "It voided every sample" also does not follow from one observed pair.
# Re-derive on any sample instead; this recipe cannot go stale:
#
#   # do the two SERIAL passes agree on WHAT is covered, and differ on counts?
#   for p in 1 3; do awk '/^SF:/{sf=$0} /^DA:/{split(substr($0,4),a,",");
#     if (a[2]+0>0) print sf"|"a[1]}' pass$p.lcov | sort -u > /tmp/cov$p; done
#   diff /tmp/cov1 /tmp/cov3            # empty  => the covered SET agrees
#   diff <(grep ^DA: pass1.lcov) <(grep ^DA: pass3.lcov) | grep -c '^<'
#   # and for the branch axis, the same two passes:
#   for p in 1 3; do grep ^BRDA: pass$p.lcov | sort > /tmp/br$p; done
#   diff /tmp/br1 /tmp/br3 | grep -c '^<'
#
# The consequence, stated so nobody has to rediscover it: **a widening that
# changed only branch coverage would not be caught by item 4.** If that ever
# matters, the thing to build is a separate branch-stability measurement, not a
# re-inclusion here — re-including it restores the false void, it does not
# restore the signal.
#
# ── WHAT IS PRESERVED ────────────────────────────────────────────────────────
#
# Every emitted line is prefixed with its `SF:` record. A hostile review showed
# two genuinely different reports hashing IDENTICALLY without that: move
# `DA:1,1` from a.cc to b.cc and `DA:2,0` the other way, and the sorted line
# multiset is unchanged — coverage could migrate between files while all three
# digests "agree", which is the one thing item 4 exists to detect. The caller
# sorts; per-object section order follows the object list and the filesystem,
# neither of which is a coverage fact.
#
# `LH:`/`LF:`/`FNH:`/`FNF:` are kept. They are derived from the records above,
# so they cannot drift independently — they are a free redundancy check on this
# normalisation rather than an input to it.

/^SF:/ { sf = $0 }

# Branch data: see the scope limit above.
/^(BRDA|BRH|BRF):/ { next }

# DA:<line>,<count>[,<checksum>] -> covered/not
/^DA:/ {
    split(substr($0, 4), a, ",")
    print sf "\001DA:" a[1] "," (a[2] + 0 > 0 ? 1 : 0)
    next
}

# FNDA:<count>,<mangled name> -> covered/not, name preserved
/^FNDA:/ {
    s = substr($0, 6)
    i = index(s, ",")
    print sf "\001FNDA:" (substr(s, 1, i - 1) + 0 > 0 ? 1 : 0) "," substr(s, i + 1)
    next
}

{ print sf "\001" $0 }
