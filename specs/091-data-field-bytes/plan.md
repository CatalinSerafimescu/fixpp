# Implementation Plan: A Data field can carry any octets — atomic Length+Data emit

**Branch**: `091-data-field-bytes` | **Date**: 2026-09-24 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/091-data-field-bytes/spec.md` (fixpp#418, batch B8)

## Summary

Add one atomic operation to the C++ outbound builder:
- `body_builder::field_data(data_tag, bytes)` and `entry_handle::set_data(data_tag, bytes)`;
- it derives the Length tag through the builder's `dict_hooks` (standard pairs first, dictionary
  pairs after, B-426-3);
- it emits Length = octet count, then the Data octets verbatim, both or neither.

`commit()` gains the #428 pair rule (`wire::length_data_checker`), folded into the INV-5 tree walk it
already does.

The code generator:
- routes every coupled member through the new operation in both the top-level and nested arms, with a
  per-call-site `static_assert` census;
- adds an optional `message_encoding` (347) Args member to messages that can carry `Encoded*` fields.

Goldens are regenerated, the stage-one pins flip to success, and L-067-2 closes.

## Technical Context

**Language/Version**: C++23 (clang 18+ / GCC 14 / MSVC 19.4x per Article II)

**Primary Dependencies**: none new. It reuses `wire::dict_hooks`, `wire::length_data_checker`,
`core/length_data_pairs.hpp`, and Google Benchmark for the bench.

**Storage**: N/A

**Testing**: GoogleTest whole-binary grouped executables (Article VII §8). Mutants run in a scratch
copy (§XVI.6).

**Target Platform**: Linux (clang, GCC) and Windows (MSVC). `body_builder` is header- and
source-portable, with no platform code.

**Project Type**: library (fixpp wire and codegen layers)

**Performance Goals**: SC-005. At most +3 % on `builder_bench`'s NoGroup/WithGroup/Raw cases against
the frozen baseline. The A/A floor is ≤ 0.6 % (R-10).

**Constraints**:
- Zero global heap in `body_builder`: both new nodes come from the existing null-upstream arena.
- `noexcept` everywhere, as today.
- Refusals use existing `core::error` variants only (FR-004a), so the C-ABI error map is untouched.

**Scale/Scope**:
- ~2 public members plus a constructor parameter.
- One emitter branch plus one Args member.
- 834 golden files with a coupled emission, plus `message_encoding` on most application messages
  (R-5).

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Applies? | Status |
|---|---|---|
| **XVI §3** clarify for wire/codegen | yes | ✅ `/speckit-clarify` ran 2026-09-24 (3 Q); specify-time owner decisions FR-008/FR-009 recorded |
| **XVII §1** Gate A: public C++ API + codegen layout | **yes, both triggers** | ⏳ `/gate-a 091-data-field-bytes` before `/speckit-tasks` |
| **XVI §6** orchestrator does not implement | yes | Code, tests, bench and generated goldens go through `phase-implementer`. The orchestrator authors only the spec bundle, B&L text and brain |
| **VIII §1–2** bench + paired regression budget | yes | `builder_bench` added (`4749f589`). This feature's own budget (+3 %) is stricter than the constitutional +5 %. Added to `bench/ci-suite.txt` as a candidate-only row (§2a) |
| **VIII / XV** zero-allocation discipline | yes | New nodes use the builder's arena. No `new`/`malloc`. B15 (#497) blind spots (over-aligned allocation, MSan/LSan) are not engaged: no over-aligned type is added |
| **VI** 100 % FIX rule | yes | TagValue §4.2.5 / §4.3.7 conformance for outbound Data, cited in spec. The interop residual is disclosed (FR-009a) |
| **VII** testing | yes | TDD order: RED stage-one flip, then implementation. Every C-1/C-2 clause has a test. Each mutant in quickstart §3 is proven RED |
| **IX** coverage / sanitizers / static analysis | yes | Every new line in `body_builder.cpp` covered. ASan/UBSan presets run in `/speckit-verify`. clang-tidy on the changed `src/`, `include/` **and `tools/codegen/`** files (#265) |
| **X** C-ABI | **no** | No C-ABI symbol, header or error-map change (FR-004a). `[const §X.7]` is not engaged |
| **XI** concurrency | no | No threading change. `send_impl` is read, not modified (R-9) |
| **XIX** documentation | yes | B&L rows, brain `wire.md` (the Length+Data section's "#418 must reuse" line becomes "did"), `CLAUDE-history.md` at close-out |

**Result:** no violations. Gate A is **required** (not waivable by triviality: a public API plus a
codegen layout change).

## Project Structure

### Documentation (this feature)

```text
specs/091-data-field-bytes/
├── spec.md
├── plan.md              # this file
├── research.md          # R-1..R-10, measured
├── data-model.md        # API delta, invariants INV-6/INV-7, refusal table, ledger rows
├── quickstart.md        # validation recipe incl. mutants and the paired bench
├── contracts/
│   ├── body-builder-data.md   # C-1: public C++ API
│   └── codegen-builders.md    # C-2: emitter output, census, goldens
├── checklists/requirements.md
└── tasks.md             # /speckit-tasks (after Gate A)
```

### Source Code (repository root)

```text
include/fixpp/wire/body_builder.hpp     # field_data, set_data, ctor(dict_hooks), hooks_
src/wire/body_builder.cpp               # R-3 append+rollback, R-4 combined walk
tools/codegen/fixpp-codegen/
├── emit_builders.cpp                   # R-7 coupled arms + static_assert; R-8 message_encoding
└── gen_util.hpp                        # stale "already String" comment
specs/078-precompiled-builder-libs/contracts/golden/{v42,v44,v50sp2,vlatest}/   # regenerated
bench/wire/builder_bench.cpp            # added (4749f589)
bench/ci-suite.txt                      # + builder_bench row
tests/session/test_067_builder_failclosed.cpp   # stage-one pins brought over (68c8c769), then flipped
tests/wire/test_body_builder.cpp                # C-1 clauses
tests/codegen/...                               # C-2.2 count census + message_encoding presence
tests/session/...                               # R-9 send_impl header-pair witness; R-6 v50sp2 Length-delimited group
spec/behaviors-and-limitations{,-closed}.md     # L-067-2 move; new B-/L-091 rows
brain/components/wire.md                        # Length+Data section update
```

**Structure Decision**: existing single-library layout. No new target except `builder_bench`.

## Phasing (input to /speckit-tasks)

1. **Stage-one carry-over, then flip (RED).** Bring `68c8c769`'s test hunk onto the branch, **tests
   only**, without the superseded design note.
   - Re-run the four rejection pins on the current base: they must be GREEN, confirming the
     limitation still behaves as pinned.
   - **Flip them in the same phase** to assert success, verbatim bytes and a re-parse. They are now
     **RED** against today's builder, which is the TDD red step.
   - Phase 3's codegen change is what turns them GREEN.
   - The pre-flip rejection form is recorded in the commit, so the limitation's pinned-then-fixed
     history stays visible.
2. **`body_builder` API + commit check** (C-1). The C-1 unit tests are written RED first, then
   implemented to GREEN. The flipped pins stay RED: the generated builder still routes through
   `field()`.
3. **Codegen** (C-2.1, C-2.3). Regenerate goldens; run the C-2.2 census and C-2.4 diff filter;
   confirm the digest pins. The flipped pins turn **GREEN** here.
4. **Mutants** (quickstart §3), each in a scratch copy, each proven RED.
5. **Session witnesses** (R-9 header pair, R-6 Length-delimited group) and the v50sp2/vlatest
   builder round trips. The **`message_encoding` witness lives here, on the `send_impl` path**:
   - 347 is a header tag, so a body-only build-and-validate round trip may refuse it as undeclared
     in the message;
   - through `send_impl` it must land in the header and re-parse.
   - The compile-time surface is re-measured with `vlatest_builders_compile_bench` (R-10).
6. **Paired bench** (quickstart §6); `ci-suite.txt` row.
7. **Ledger + brain** (FR-016, B-091-*, L-091-1).

## Complexity Tracking

None. No constitution violation to justify.
