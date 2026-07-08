# Phase 0 Research: Typed App Messages — write shape-oracle (061-slim)

**Date**: 2026-07-08 | **Branch**: `061-typed-app-messages`
**Supersedes** the 2026-07-05 paused research (33-message scope, blocked-on-062). Prereqs 062 (PR #168) +
063 (PR #176) merged; this re-scope follows the user-signed D3 decision (2026-07-07). All source claims
verified 2026-07-08 via CodeGraph/read against real headers + dictionary XML.

## Decision 1 — exemplar set (5 messages, all `fixpp::v44`)

- **Decision**: D (NewOrderSingle), 8 (ExecutionReport), 9 (OrderCancelReject), E (NewOrderList),
  AS (AllocationReport) — all in `fixpp::v44`.
- **Rationale**: spans every hard write axis with minimal overlap while staying inside the OFFICIAL set
  (rows A-001/A-006/A-007/A-002/P-003). Verified group structure (generated `v44/Messages.hpp`, dict `FIX44.xml`):
  - **D** `Messages.hpp:18016`, **8** `Messages.hpp:5368` — flat-scalar exemplars that emit **no group tags
    at all** (not `No<Group>=0`); the count-0 case is exercised only on the optional `NoPartyIDs(453)` group
    in E/AS. Reuse the 020 builders; exercise string/char/int/decimal/UTCTimestamp typing.
  - **9** `Messages.hpp:6188` — **genuinely group-free** app body → exercises the emitter's "no group
    definitions at all" path, distinct from "groups present, count-0".
  - **E** `Messages.hpp:18620` — one top-level `NoOrders(73)` nesting `NoPartyIDs(453)` nesting
    `NoPartySubIDs(802)` → **the pivotal 3-level nested exemplar** (cleanest nesting in the candidate set).
  - **AS** `Messages.hpp:13206` — **multi-char MsgType** (`35=AS`) + nested. AS carries several separate
    top-level groups (`OrdAllocGrp NoOrders(73)`, `AllocGrp NoAllocs(78)`, `Parties NoPartyIDs(453)`, legs,
    underlyings); the exemplar seeds only the **2-level** `Parties` chain `NoPartyIDs(453)→NoPartySubIDs(802)`
    (verified tags, reused from E) as its representative nesting — distinct from E's 3-level
    `NoOrders(73)→NoPartyIDs(453)→NoPartySubIDs(802)`.
- **Alternatives rejected**: (a) the 100pct-plan's original MassQuote(v42)/AllocationInstruction picks —
  **invalidated**: v42 codegen emits ZERO typed groups (see Decision 2); AllocationInstruction is "busy"
  (10 top-level groups) vs E's clean single-chain nesting. (b) Full 33-message hand-write — superseded by D3
  (that is FR-015a's job).
- **MUST re-verify at implement start**: grep `v44/Messages.hpp` for each flyweight + its group accessors
  (planning existence claims are unreliable — [[feedback_planning_explore_existence_claims_unreliable]]).

## Decision 2 — namespace forced to v44 (v42 emits zero typed groups)

- **Finding (verified)**: the v42 generated `Messages.hpp` emits `group_view` occurrences = 0 and
  `nested_group_slices` = 0; `NoQuoteSets(296)` etc. surface only as scalar `int32` counts. The 296→295
  nesting exists in `FIX42.xml` but never reaches a typed flyweight.
- **Consequence**: no grouped/nested exemplar is expressible in v42 → all 5 exemplars use v44. Market-data
  (M-row) grouped writes become an **FR-015a prerequisite** (v42 group codegen), out of 061-slim scope.
  Flagged in spec Out-of-Scope + parent tracker.

## Decision 3 — `wire::body_builder` design (mirror the C-ABI `OutboundAccumulator`)

- **Decision**: a new wire-layer, body-only serializer that (a) lifts 020's flat helpers
  `wfield`/`wchar`/`wdecimal` out of `business_messages.cpp`'s anonymous namespace into a shared header,
  and (b) adds a repeating-group API **structurally mirroring** the proven C-ABI accumulator so there is
  no third group-write idiom:
  - `group_begin(uint16_t no_tag, uint16_t delimiter_tag) → group handle` · `add_entry() → entry handle` ·
    `entry.set_{string,char,int,decimal}(tag, v)` · `entry.group_begin(no_tag, delimiter_tag)` (nested) ·
    `group_end()` (LIFO). The `delimiter_tag` is the group's first-field tag, author-supplied (no dictionary
    lookup) and enforced at `commit()`.
  - **Accumulate → serialize** (two-pass): collect entries, then emit `NoXXX=<count>\x01` FIRST followed by
    each instance's fields (delimiter-first, INV-5) — because the `NoXXX` count must precede entries and is
    unknown until all entries are added (the exact reason the C-ABI accumulator buffers).
- **Rationale**: the C-ABI `OutboundAccumulator` (`src/capi/message_write.cpp`) already solved count-
  precedence, LIFO discipline (`open_builders` stack, `group_end` requires `stack.back()==b`), nested groups
  (`entry_group_begin`, `b->parent=e`), and the delimiter-first grammar. Mirroring its shape keeps the two
  serializers conceptually convergent (and eases the tracked v1.x convergence debt).
- **Simplification vs the C-ABI**: the hand-written exemplar author supplies fields in dictionary order AND
  supplies each group's `delimiter_tag`, so `body_builder` needs **no dictionary dependency** — no
  `dict->group_first_field` lookup. It is NOT, however, a fully "dumb" serializer: it still **enforces the
  two INV-5 grammar legs at `commit()`** — reject an empty group instance and reject an instance whose first
  field ≠ the author-supplied `delimiter_tag` — mirroring the C-ABI `validate_group_grammar`
  (`message_write.cpp:682-701`). The difference from the C-ABI is only the *source* of the delimiter (author
  arg vs `dict->group_first_field`), not whether grammar is enforced. This keeps `body_builder` in the
  wire→core layer with no `wire→dictionary` edge while still fail-closing on a wrong delimiter/empty entry —
  which the golden alone cannot backstop once FR-015a's *generated* builders call `body_builder`. Dictionary-
  order correctness *within* an entry (beyond the delimiter) is still checked by the external golden
  (Decision 5). (If a later need arises, wire→dictionary is layer-legal.)
- **`wire::Writer` rejected**: it always injects `8=`/`9=`/`10=` (full frame) — no body-only mode
  (`business_messages.cpp:24-27`). Not reusable here.

## Decision 4 — buffer/allocation policy: caller-span, fail-closed atomic

- **Decision**: builders take a caller-provided `std::span<std::byte> out` and return
  `expected_t<std::span<std::byte>>`; on any invalid input or overflow, return a typed error and leave `out`
  untouched (all-or-nothing, INV-4). `body_builder` assembles into an internal buffer and copies to `out`
  only on full success — matching the 020 discipline (`scratch[1024]` → copy). **Buffer policy PINNED (was
  an open item — the NEW-P2 allocation gap):** the internal buffer is a **fixed scratch capped at the C-ABI
  `kFrameCap` (3800 B)**, and `body_builder` carries **no `memory_resource`** on its ctor or `commit()`. A
  body that would exceed 3800 B fails closed (typed error, `out` untouched). This is the simplest lifetime-
  correct option — no per-call monotonic arena ([[feedback_monotonic_arena_percall_pmr_vector_leaks]]) and no
  unbounded growth ([[feedback_build_resource_cap_oom]]). The 5 exemplar bodies (incl. group-heavy E/AS) are
  well under 3800 B. A growable accumulator is **rejected** here: it would force an `mr` onto the API and
  ripple to all 5 builder signatures + FR-015a; if FR-015a ever needs one, its `mr` must be a reused/session
  resource, never per-call — but that is an FR-015a decision, out of 061 scope.
- **Rationale**: caller-span is the 020 precedent, avoids allocator surprises, and is what FR-015a's
  generated builders will inherit.

## Decision 5 — external golden anchor (uniform, in-submodule, body-only)

- **Decision**: all 5 exemplars anchored by **static checked-in body-only golden `.fix` files** under
  `tests/session/golden/`, each authored once by serializing the same seed via **QuickFIX-cpp offline**
  (reference engine cloned per [[project_reference_engines_setup]]). Diff via
  `tests/interop/support/golden_diff.hpp::diff_transcripts` with a **new 061-specific
  `shape_oracle_profile()`** exclusion set (mirrors the existing `admin_profile_excluded_tags()` pattern in
  that header). For byte-exact write shape-oracles the profile excludes **no business tags** — only framing
  `{8,9,10,34,52}`, which a body-only golden never contains — so seeded business fields incl.
  `TransactTime(60)` are matched verbatim. The interop `default_normalization_tags()`
  (`{9,10,34,52,60,112,122}`) is **NOT** reused: it drops business tag `60` and is calibrated for
  live-interop transcript diffing, structurally the wrong tool to freeze exact body bytes. Decimal
  comparison stays by-value; the canonical decimal *format* is pinned separately by a **byte-exact decimal
  assertion** per exemplar (≥1 decimal field's raw `<tag>=<ascii>\x01` bytes compared exactly), because a
  by-value diff cannot catch `1.9E2`/trailing-zero format defects. D & 8 get **new** body-only goldens
  too — the parent `phase-9-harness/BM-*` transcripts are full-frame + out-of-submodule and are NOT
  consumed here.
- **Rationale**: a pure build→parse round-trip is tautological (builder+parser co-wrong stay green — the
  [[feedback_coverage_push_enshrines_bugs]] class); the external golden is the independent oracle that makes
  each exemplar trustworthy as FR-015a's shape reference. Uniform in-submodule keeps the harness hermetic
  (no runtime QuickFIX dependency, no cross-submodule reach).
- **Authoring note**: the QuickFIX serialization emits a full frame; the golden is the extracted **body**
  (fields after `9=` through before `10=`, minus session header tags) normalized to fixpp's canonical decimal
  form. Record the exact QuickFIX version + seed in a golden-provenance comment/sidecar for reproducibility.

## Decision 6 — read-back path: 5-arg dict-backed `Parser<Index>::parse`

- **Decision**: the round-trip and inbound-read witnesses parse via the **5-arg dict-backed**
  `MessageView`/`Parser<Index>{dict}.parse(frame, mr)` (`parser.hpp:95-115`), NOT the 2-arg heuristic
  (`parser.hpp:135`). Factor a `tests/support/app_message_read_scaffold.hpp` (make_frame + dict-parse,
  BeginString-parameterized) so ~10 witnesses don't duplicate it.
- **Rationale**: only the dict-backed path seeds root `group_context` + `group_member_fn`, so
  `nol.orders()[i].parties()[j].party_id()`-style nested entry reads enumerate correctly (062/063). The
  2-arg heuristic cannot slice groups. Entry accessor shapes (062/063): strings `[[clang::lifetimebound]]`
  no-arg; decimals take `std::pmr::memory_resource*`; char/int via `decode_field<T>`.

## Decision 7 — FR-008 header-install mechanics

- **Decision**: add `install(DIRECTORY "${CMAKE_BINARY_DIR}/_codegen/include/" DESTINATION
  "${CMAKE_INSTALL_INCLUDEDIR}")` with `PATTERN "_dispatch" EXCLUDE` (build-tree-private reify bridge) and
  `vt11` excluded (FIXT.1.1, not part of the public v42/v44/v50sp2 set). Add a `tests/consumer/` external-TU
  compile witness that includes an installed typed header. Generated headers currently attach to codegen
  INTERFACE targets via BUILD_INTERFACE only (`cmake/Codegen.cmake`), with no install rule.
- **Rationale**: §XVIII.7 "ships in v1.0" is only true if the generated headers are on the installed public
  include path; this is orthogonal to builder count, so it belongs in 061-slim.

## Open items for `/speckit-plan` Phase 1 / `/tasks`
- ~~Exact per-exemplar seed field lists~~ → **RESOLVED** (Gate A round 1): required-field-complete,
  `FIX44.xml`-cited seed tables now in `data-model.md` §3.1.
- ~~`body_builder` internal-buffer sizing~~ → **RESOLVED** (Gate A round 1): fixed scratch capped at
  `kFrameCap` (3800 B), no `memory_resource` (Decision 4).
- Golden provenance format (inline comment vs sidecar `.provenance`) → task detail.
