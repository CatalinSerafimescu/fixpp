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
# run-to-run for any timing- or iteration-dependent loop. Hashing them makes the
# digest report a difference on every run. Measured on #267 campaign run
# 33951801400, `linux-clang-coverage`: the two SERIAL passes had an IDENTICAL
# covered-line set (9042 lines, zero differences) and **871 `DA:` records
# differing in count alone** — enough to void the sample. Both are reduced here
# to a covered/not bit.
#
# ⚠️ BRANCH RECORDS (`BRDA:`/`BRH:`/`BRF:`) ARE EXCLUDED, AND THAT IS A REAL
# SCOPE LIMIT, NOT A DETAIL. This key covers LINE and FUNCTION coverage only.
# The reason is measured, not assumed: on the same two serial passes — same
# binary, same concurrency, same machine — line coverage and function coverage
# agreed exactly while **16 of 8458 branches flipped their taken-bit**. Branch
# coverage is nondeterministic in this suite at FIXED concurrency, so including
# it leaves the baseline unable to agree with itself and item 4 can never
# conclude anything in either direction. Excluding it is what makes the
# remaining comparison mean something.
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
