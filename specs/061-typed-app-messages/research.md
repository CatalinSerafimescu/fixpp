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
  - **D** `Messages.hpp:18016`, **8** `Messages.hpp:5368` — flat-scalar exemplars (built with groups emitted
    count-0); reuse the 020 builders; exercise string/char/int/decimal/UTCTimestamp typing.
  - **9** `Messages.hpp:6188` — **genuinely group-free** app body → exercises the emitter's "no group
    definitions at all" path, distinct from "groups present, count-0".
  - **E** `Messages.hpp:18620` — one top-level `NoOrders(73)` nesting `NoPartyIDs(453)` nesting
    `NoPartySubIDs(802)` → **the pivotal 3-level nested exemplar** (cleanest nesting in the candidate set).
  - **AS** `Messages.hpp:13206` — **multi-char MsgType** (`35=AS`) + nested (73/78/453→802/555/711).
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
  - `group_begin(uint16_t no_tag) → group handle` · `add_entry() → entry handle` ·
    `entry.set_{string,char,int,decimal}(tag, v)` · `entry.group_begin(no_tag)` (nested) · `group_end()` (LIFO).
  - **Accumulate → serialize** (two-pass): collect entries, then emit `NoXXX=<count>\x01` FIRST followed by
    each instance's fields (delimiter-first, INV-4) — because the `NoXXX` count must precede entries and is
    unknown until all entries are added (the exact reason the C-ABI accumulator buffers).
- **Rationale**: the C-ABI `OutboundAccumulator` (`src/capi/message_write.cpp`) already solved count-
  precedence, LIFO discipline (`open_builders` stack, `group_end` requires `stack.back()==b`), nested groups
  (`entry_group_begin`, `b->parent=e`), and the delimiter-first grammar. Mirroring its shape keeps the two
  serializers conceptually convergent (and eases the tracked v1.x convergence debt).
- **Simplification vs the C-ABI**: the hand-written exemplar author supplies fields in dictionary order, so
  `body_builder` is a **dumb ordered serializer** — it does NOT need dictionary group-grammar validation
  (`validate_group_grammar`) or `dict->group_first_field` lookups. Field-order correctness is guaranteed by
  the exemplar author and **checked by the external golden** (Decision 5). This keeps `body_builder` in the
  wire→core layer with no dictionary dependency. (If a later need arises, wire→dictionary is layer-legal.)
- **`wire::Writer` rejected**: it always injects `8=`/`9=`/`10=` (full frame) — no body-only mode
  (`business_messages.cpp:24-27`). Not reusable here.

## Decision 4 — buffer/allocation policy: caller-span, fail-closed atomic

- **Decision**: builders take a caller-provided `std::span<std::byte> out` and return
  `expected_t<std::span<std::byte>>`; on any invalid input or overflow, return a typed error and leave `out`
  untouched (all-or-nothing, INV-4). `body_builder` assembles into an internal buffer and copies to `out`
  only on full success — matching the 020 discipline (`scratch[1024]` → copy). The internal buffer is sized
  for the exemplars; group-heavy E/AS may exceed 1024B, so the internal buffer is either a larger fixed
  scratch or an accumulate-then-serialize vector bounded by a commit cap analogous to the C-ABI `kFrameCap`
  (3800B). **No per-call monotonic arena** ([[feedback_monotonic_arena_percall_pmr_vector_leaks]]) and no
  unbounded growth ([[feedback_build_resource_cap_oom]] is a build concern, but the anti-OOM discipline
  applies to buffers too).
- **Rationale**: caller-span is the 020 precedent, avoids allocator surprises, and is what FR-015a's
  generated builders will inherit.

## Decision 5 — external golden anchor (uniform, in-submodule, body-only)

- **Decision**: all 5 exemplars anchored by **static checked-in body-only golden `.fix` files** under
  `tests/session/golden/`, each authored once by serializing the same seed via **QuickFIX-cpp offline**
  (reference engine cloned per [[project_reference_engines_setup]]). Diff via
  `tests/interop/support/golden_diff.hpp::diff_transcripts` (decimal-by-value; non-deterministic tags
  normalized/excluded). D & 8 get **new** body-only goldens too — the parent `phase-9-harness/BM-*`
  transcripts are full-frame + out-of-submodule and are NOT consumed here.
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
- Exact per-exemplar seed field lists (representative shape-oracle, not full-field) → data-model.md seed tables.
- `body_builder` internal-buffer sizing (larger fixed scratch vs bounded vector) → decide at first E impl.
- Golden provenance format (inline comment vs sidecar `.provenance`) → task detail.
