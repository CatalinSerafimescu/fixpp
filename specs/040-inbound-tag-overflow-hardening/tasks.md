# Tasks: Inbound tag-overflow hardening

**Feature**: `040-inbound-tag-overflow-hardening` | **Plan**: [plan.md](./plan.md) | **Spec**: [spec.md](./spec.md)

Security fix: one shared `constexpr` in-loop `0xFFFF` bounded-tag helper applied at all 5 live-inbound
hand-rolled tag scanners (fixing the defective `scan_frame_header:1493` guard); each site keeps its
disposition. Witnesses are REQUIRED (FR-007/FR-007a) → test tasks are generated. `build_replay_frame`
(site 6) is a documented exclusion.

**Per-feature discipline**: one implementer invocation per phase; build + targeted ctest after each.

## Phase 1: Setup

- [ ] T001 Confirm a configured build preset exists (e.g. `linux-clang-debug`) and `tests/wire/` +
  `tests/session/` CMake test targets are discoverable; no new build infra needed.

## Phase 2: Foundational — the shared helper (BLOCKS US1 + US2)

- [ ] T002 Create the leaf header `include/fixpp/wire/tag_scan.hpp` with
  `namespace fixpp::wire { [[nodiscard]] constexpr bool accumulate_tag_digit(std::uint32_t& tag, unsigned char c) noexcept; }`
  implementing the in-loop pre-multiply `0xFFFF` bound (`if (tag > (0xFFFFu - d)/10u) return false; tag = tag*10u + d; return true;`),
  `[[gnu::always_inline]]`, `<cstdint>` only, no other deps. Per `contracts/tag-scan-helper.md`.
- [ ] T003 Add the compile-time boundary `static_assert` block adjacent to the helper in
  `include/fixpp/wire/tag_scan.hpp`: `65535` accepted, `65536` rejected, wrap-and-continue
  (`429496729649`) rejected, zero-padded (`000000000034`) → 34. (FR-001; compile-time guarantee.)
- [ ] T004 [P] Add a runtime helper boundary unit test `tests/wire/tag_scan_test.cpp` (register in
  `tests/wire/CMakeLists.txt`): `65535` ok, `65536` reject, `429496729634`/`429496729649` reject,
  zero-padded ok, single-digit ok. (FR-007.)

**Checkpoint**: helper compiles (incl. `static_assert`); `tag_scan_test` passes. US1 + US2 unblocked.

## Phase 3: User Story 1 — fix the defective central-scanner guard (Priority: P1) 🎯 MVP

**Goal**: `scan_frame_header` rejects forged wrap-and-continue tokens; 52/49/56/34 can't alias.
**Independent test**: drive `429496729649`→49, `429496729652`→52 through `scan_frame_header`; assert
not surfaced; `4294967330` still rejects; `65535` parses.

- [ ] T005 [US1] Add a RED wrap-and-continue witness `tests/session/scan_frame_header_overflow_test.cpp`
  (register in `tests/session/CMakeLists.txt`): assert `scan_frame_header` does NOT surface
  `429496729649` as SenderCompID(49), `429496729652` as SendingTime(52), `429496729634` as
  MsgSeqNum(34); assert `4294967330` still rejected and `65535`/ordinary tags parse. (Demonstrates the
  defect RED against the current `>429496729U` guard.)
- [ ] T006 [US1] Replace the defective guard at `src/session/session.cpp:1493-1496` (`scan_frame_header`)
  with the digit-check-before-helper shape:
  `if (c<'0'||c>'9') { tag_ok=false; } else if (!fixpp::wire::accumulate_tag_digit(tag,c)) { tag_ok=false; }`
  (include `fixpp/wire/tag_scan.hpp`); preserve the existing `tag_ok` skip-field disposition and the
  `:1518` switch. Turn T005 GREEN. (FR-002/FR-003/FR-007a.)
- [ ] T007 [US1] Add the non-digit negative witness for `scan_frame_header` to
  `tests/session/scan_frame_header_overflow_test.cpp`: a token containing a non-digit (e.g. `"3a5="`)
  is rejected, never dispatched. (FR-007a — guards against folding the digit check into the helper.)

**Checkpoint**: `scan_frame_header_overflow_test` GREEN; the 038 SendingTime-guard regression vector
(52-aliasing) is closed.

## Phase 4: User Story 2 — apply the helper to the other four scanners (Priority: P1)

**Goal**: no forged out-of-range tag aliases a small tag at the two wire twins, `interpret_logon`, or
`scan_first_frame_ids`.
**Independent test**: per-site wrap-and-continue token rejected / not queryable under the aliased tag.

- [ ] T008 [P] [US2] Site 1 (Index): in `src/wire/offset_table.cpp:160-176`, replace the per-digit
  `tag = tag*10+d` with `accumulate_tag_digit`; on `false` set
  `status_ = err_tag_out_of_range(); entries_.clear(); return;` and DROP the now-redundant post-loop
  `if (tag > 0xFFFFU)` at `:176` (the in-loop helper subsumes it). Keep the non-digit
  `err_invalid_field_format()` path distinct. (FR-002.)
- [ ] T009 [P] [US2] Site 2 (Scan): in `include/fixpp/wire/parser.hpp:333-346`
  (`field_iterator::advance`), add `accumulate_tag_digit` in the digit loop; on `false` →
  `done_ = true; return;` (matches existing malformed termination). (FR-002.)
- [ ] T010 [P] [US2] Site 3 (`interpret_logon`): in `src/session/admin_messages.cpp:255-266`, use the
  helper in the loop; on `false` → `goto next_field;` (existing skip-malformed disposition; the
  existing immediate non-digit `goto` already satisfies the digit precondition). (FR-002.)
- [ ] T011 [P] [US2] Site 4 (`scan_first_frame_ids`): in `src/session/engine.cpp:349-353`, use the
  digit-check-before-helper `if/else-if` shape → `tag_ok=false` on either branch. (FR-002/FR-007a.)
- [ ] T012 [US2] Wrap-and-continue + non-digit witnesses: `tests/wire/offset_table_overflow_test.cpp`
  (Index — forged field not queryable under aliased tag; whole message all-absent),
  `tests/wire/parser_overflow_test.cpp` (Scan — forged field never yielded), and
  `tests/session/interpret_logon_overflow_test.cpp` (forged `…1137`/`…49`/`…56` not consumed) +
  `tests/session/scan_first_frame_ids_overflow_test.cpp` (forged `…49`/`…56` not used for resolution;
  non-digit token rejected). **Pin the canonical verified vectors `429496729634`→34 and
  `429496729649`→49 explicitly at the Index and Scan sites** (not just a site-relevant alias) so
  SC-001's "reject everywhere" is directly traceable. Register all in the respective `CMakeLists.txt`.
  Conforming-corpus tags unchanged. (FR-004/FR-005/FR-007/FR-007a; analyze E3.)

**Checkpoint**: all five scanners reject the verified wrap vectors; SC-001 met across the full set.

## Phase 5: User Story 3 — document the build_replay_frame exclusion (Priority: P3)

- [ ] T013 [US3] Add a justified-exclusion comment at `src/session/session.cpp:1639`
  (`build_replay_frame`): exempt from the inbound tag-overflow guard because it parses stored
  own-outbound replay frames, not received bytes. (FR-008.)

## Phase 6: Polish & cross-cutting

- [ ] T014 [P] Add a behaviors-and-limitations row to `spec/behaviors-and-limitations.md`: forged-tag
  overflow aliasing hardened across the 5 live-inbound scanners (TLS-auth-bounded threat per 015; one
  shared `0xFFFF` bound helper); note the `build_replay_frame` justified exclusion. (FR-008.)
- [ ] T015 Run `tools/check_layers.py` to confirm the new `include/fixpp/wire/tag_scan.hpp` leaf header
  does not invert layers (wire leaf included by session is allowed). **SC-004 centralization gate**:
  after the helper lands, confirm NO residual per-site tag bound remains — grep the 5 scanner sites
  (`offset_table.cpp`, `parser.hpp`, `admin_messages.cpp`, `engine.cpp`, `session.cpp` `scan_frame_header`)
  for a `tag > 0xFFFF`/`tag > 429496729U` bound; expect zero (the only `429496729U` left is the
  legit seqnum guard at `session.cpp:1588`, NOT a tag bound). (Plan Constitution Check; analyze E2.)
- [ ] T016 Full targeted regression: build + `ctest` for `tests/wire/` and `tests/session/` on at
  least `linux-clang-debug` + `linux-clang-asan` (all new overflow witnesses GREEN; conforming
  wire/session corpora unchanged — SC-003; the full 6-preset matrix runs at `/speckit-verify`).
  **Perf-neutrality (FR-006 / Article VIII §3):** run `bench/wire/parser_bench` +
  `bench/wire/offset_table_bench` and confirm no regression vs `bench/baselines/` (the in-loop helper
  inlines to the same code). **Fuzz coverage (Article VII §7):** confirm the existing
  `tests/fuzz/fuzz_wire_parser.cpp` reaches the modified `field_iterator::advance` + `OffsetTable::build`
  paths (≥60s smoke), and assess whether a session-side fuzz harness reaches `scan_frame_header` /
  `scan_first_frame_ids` — record the assessment in `.specify/decisions/040-verify.md`. Confirm no
  unticked-box drift. (Pre-`/simplify`; analyze E1/D1/B1.)

## Dependencies

- Phase 2 (T002–T004, helper) BLOCKS Phase 3 and Phase 4 (all sites call the helper).
- Phase 3 (US1) and Phase 4 (US2) are independent of each other once the helper exists, but both
  depend on Phase 2. US1 is the MVP (the shipped defect).
- Phase 5 (US3) and Phase 6 (polish) depend on the code phases.

## Parallel opportunities

- T004 (helper unit test) ∥ T003 (static_assert) once T002 lands.
- Within US2: T008/T009/T010/T011 touch different files → parallelizable; T012 (witnesses) after them.

## Implementation strategy (MVP first)

1. **MVP**: Phase 2 (helper) + Phase 3 (US1 — fix `scan_frame_header`, the shipped defect + 038
   regression vector). Shippable and independently valuable on its own.
2. Then Phase 4 (US2 — the other 4 scanners) completes the security outcome (SC-001).
3. Phase 5 + 6 (exclusion doc, B&L, check_layers, regression).

## Notes

- Live cross-engine (QFcpp/QFJ) forged-frame witness is DEFERRED (FR-007; 038 L-038-2 family) — unit
  witnesses carry the proof.
- Helper digit-only precondition: sites 4 & 5 MUST keep the explicit non-digit check before the helper
  (FR-007a) — do NOT fold it into the helper (would accept a non-numeric tag token).
