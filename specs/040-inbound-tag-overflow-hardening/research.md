# Phase 0 Research: Inbound tag-overflow hardening

## D-1 — Helper shape: per-digit accumulate, not a full-token parser

**Decision**: A single `constexpr [[gnu::always_inline]] noexcept` helper that performs ONE bounded
accumulate step:

```cpp
// include/fixpp/wire/tag_scan.hpp  (sketch — final form in contracts/)
namespace fixpp::wire {
// Appends decimal digit `c` ('0'..'9') to `tag`, bounding to the 16-bit FIX tag
// space. Returns false (without mutating beyond the in-progress value) if the
// result would exceed 0xFFFF — i.e. BEFORE any multiply that could wrap a
// fixed-width accumulator. Caller disposes on false per its own control flow.
[[nodiscard]] constexpr bool accumulate_tag_digit(std::uint32_t& tag,
                                                  unsigned char c) noexcept {
    std::uint32_t const d = static_cast<std::uint32_t>(c - '0');
    if (tag > (0xFFFFu - d) / 10u) {   // pre-multiply bound — framer.cpp:120 shape
        return false;
    }
    tag = tag * 10u + d;
    return true;
}
}
```

**Rationale**: The five sites have **different loop structures and dispositions** (immediate return,
`done_`, `tag_ok` flag, `goto next_field`, whole-table-clear) and different scan terminators (some
stop at `=`, some at `=` or SOH; offset_table also handles Length/Data). A full-token parser would
have to unify all of that — large, risky churn. A per-digit accumulate helper centralizes ONLY the
bound arithmetic (the exact thing that diverged and was gotten wrong at `scan_frame_header`), letting
each site keep its loop and disposition verbatim: replace `tag = tag*10 + digit` with
`if (!accumulate_tag_digit(tag, c)) { <existing disposition> }`. Minimal diff, single source of bound
truth (SC-004).

**Bound = `0xFFFF`** (not `UINT32_MAX/10`): a tag `> 0xFFFF` is invalid by the 16-bit field width, so
the tighter bound rejects no legitimate tag and is simpler/correct. The pre-multiply form
`tag > (0xFFFF - d)/10` cannot itself overflow and catches the boundary the `scan_frame_header` guard
missed.

**Alternatives considered**: (a) full-token `parse_tag(span,pos)->{tag,ok,next}` — rejected: unifies
too much divergent control flow. (b) fix each site's `> 0xFFFF` post-or-in-loop check independently
(no shared helper) — rejected: that is exactly the per-site-divergence that produced the defect (Opus
census: 1-of-1 hand-rolled guard was wrong). (c) keep `UINT32_MAX/10` bound but fix the boundary
clause — rejected: needlessly admits 17-bit..32-bit tags that are invalid anyway.

## D-2 — Helper location: wire-layer leaf header

**Decision**: `include/fixpp/wire/tag_scan.hpp` — a dependency-free leaf header (just `<cstdint>`).
`fixpp::session` already depends on `fixpp::wire`, so `admin_messages.cpp`, `engine.cpp`, and
`session.cpp` including it does not invert layers; `offset_table.cpp` and `parser.hpp` are already in
`fixpp::wire`. Re-run `tools/check_layers.py` post-implementation (Gate-B obligation).

**Rationale**: lowest common layer that all five sites can include without a layering violation.

## D-3 — Per-site application + the scan_frame_header fix

| # | Site | Edit |
|---|------|------|
| 1 | `offset_table.cpp:160-176` (Index) | replace per-digit `tag = tag*10+digit` with the helper; DROP the now-redundant post-loop `if (tag > 0xFFFFU)` at `:176` (the in-loop helper subsumes it), keep `status_=err_tag_out_of_range(); entries_.clear()` on overflow |
| 2 | `parser.hpp:333-346` (Scan) | add the helper in the digit loop; on false → `done_ = true; return;` (matches the existing non-digit reject) |
| 3 | `admin_messages.cpp:255-266` (`interpret_logon`) | helper in the loop; on false → `goto next_field;` (existing skip-malformed disposition) |
| 4 | `scan_first_frame_ids.hpp` (extracted from `engine.cpp` anon-ns) (`scan_first_frame_ids`) | **digit-class clause FIRST in a single short-circuit condition** — `if (c<'0'\|\|c>'9' \|\| !accumulate_tag_digit(tag,c)) tag_ok=false;` |
| 5 | `scan_frame_header.hpp` (extracted from `session.cpp` anon-ns) (`scan_frame_header`) | **REPLACE** the defective `if (tag > 429496729U) tag_ok=false;` + `tag=tag*10+d` with the same single short-circuit condition — fixes the wrap-and-continue admission |

**Helper digit-only precondition — load-bearing for sites 4 & 5 (Gate A round 1 P3, tightened by
Opus).** `scan_first_frame_ids` and `scan_frame_header` originally set `tag_ok=false` on a non-digit but
**fell through** and still executed the accumulate. A *literal* "replace `tag = tag*10+d` with
`if (!accumulate_tag_digit(tag,c)) tag_ok=false`" would call the helper on a non-digit, breaching its
`'0'..'9'` precondition. **The shipped fix puts the digit-class clause FIRST in a single short-circuit
condition** `if (c<'0' || c>'9' || !accumulate_tag_digit(tag,c)) tag_ok=false;` — `||` short-circuit
guarantees the helper is only called on a `'0'..'9'` digit. (Implemented as the single condition rather
than an `if/else-if` with two identical `tag_ok=false` bodies, which `clang-tidy bugprone-branch-clone`
correctly flagged; the single form is equivalent and cleaner.) **DO NOT** drop the leading digit-class
clause and lean on the helper: if an implementer "simplifies" to `if (!accumulate_tag_digit(...))`, a
token like `"3a5="` would be **accepted and dispatched** (a NEW acceptance/aliasing bug) — and FR-007's
all-digit witnesses would not catch it. Hence FR-007a adds a **non-digit negative witness** at sites
4/5. (Sites 1/2/3 already reject non-digits with an immediate `return`/`goto` before any accumulate, so
they satisfy the precondition unchanged.)

**scan_frame_header is the headline**: its `:1493` guard uses `> 429496729U` (UINT32_MAX/10) and is
`>` not `>=`-with-boundary, so the accumulator can reach `429496729`, then `*10+6` wraps to 0 and
rebuilds a small aliased tag (verified: `429496729649` → 49 admitted). The correct shape is 100 lines
down at `:1588` (the seqnum guard, with the `|| (val==LIMIT && digit>N)` boundary clause) — the tag
guard simply failed to copy it. The helper's `0xFFFF` pre-multiply bound is the clean fix.

## D-3a — Census completeness: non-idiom sweep (closed)

The Opus census swept the `tag = tag*10 + digit` idiom. To close the "all 5 scanners" completeness
claim, a second sweep covered **non-idiom** numeric tag extraction (`from_chars`, `strtoul`/`strtol`,
`atoi`, `sscanf`, `stoul`/`stoi`) across `src/` + `include/`. Result: **no inbound wire-frame tag
scanner uses a non-idiom path.** The `std::from_chars` hits are all out of scope — dictionary XML
loading at config time (`xml_loader.cpp:173/:324`, parsing the data-dictionary file's `tag_i`, not
received bytes), decimal value parsing (`decimal.cpp`, `capi/decimal.cpp`), and field-value type
parsing (`field_traits.cpp`). The five hand-rolled scanners are the complete live-inbound set.
(`scan_first_frame_ids` dispatches via `if (tag == 8/49/56)` rather than `switch`, which is why a
literal `switch (tag)` grep finds only two sites — it is still site 4.)

## D-4 — build_replay_frame (site 6): justified exclusion

**Decision**: add a code comment at `session.cpp:1639` + a research/B&L note: this scanner parses
**stored own-outbound** frames during resend replay (the `stored` span, not received bytes), so it is
not a forged-tag inbound vector under the 015 threat model (an attacker who can rewrite our own store
has already won). FR-008 documents it so a future maintainer does not rediscover it as a "missed
scanner." No behavior change.

## D-5 — Disposition (clarified 2026-06-15)

Each site keeps its existing disposition on overflow (per-site, NOT a uniform whole-frame reject) —
the forged field is rejected/skipped so it can never be consumed under the aliased tag; where a
skipped field was a required header field, the session's existing missing-required-field handling
provides frame-level rejection. Lowest blast radius (Opus census rec).

## D-6 — Witnesses (clarified 2026-06-15)

Per-scanner wrap-and-continue **unit** witnesses (`429496729634`→34, `429496729649`→49, plus a
site-relevant alias such as `…1137` for `interpret_logon`) + a helper boundary unit test (`65535` ok,
`65536` reject, wrap-and-continue reject, zero-padded ok). A **live** cross-engine forged-frame
witness is DEFERRED to the Item-1 live-golden workstream (038 L-038-2 / L-021-3 / L-037-2 family;
reference engines do not emit forged overflow tags).
