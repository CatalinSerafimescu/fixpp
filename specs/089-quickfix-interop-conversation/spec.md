# Feature Specification: Live QuickFIX interop fidelity — peer-side typed readback + dictionary-backed conversation

**Feature Branch**: `089-quickfix-interop-conversation`

**Created**: 2026-09-10

**Status**: Draft

**Input**: User description: "Comprehensive live QuickFIX interop conversation (REMAINING-WORK row 4d) — one long scripted live FIX session between fixpp and real QuickFIX across all four role x flavour combos, asserting structural round-trip fidelity per message in both directions, run under both inbound-validation arms, emitting per-row named-witness evidence for row 4c."

**Tracking**: `research/G19-fix-fpml-iso20022/REMAINING-WORK.md` row **4d** (Tier 1, item 1.1 — "the critical path; gates 4c"). Detail file: `remaining-work/interop-verification-vehicle.md`. No GitHub issue; the tracker row is the anchor.

---

## Context — why this is machinery work, not a breadth pass

The row-4d detail file states the premise this feature must correct:

> "All four role x flavour combos are **already COVERED** by the existing bidirectional harness for
> `logon-hb-logout` and `NOS->ExecRpt` — this feature extends the message breadth, **not the plumbing**."

**That premise is false.** Four capabilities the stated closure bar depends on do not exist in the
tree as of `main` @ `e798a0f9` (2026-09-10). Each was verified against source, not inferred:

| # | Gap | Evidence |
|---|---|---|
| 1 | **No peer-side field readback.** Both counterparties emit one unstructured text line per message — the whole message, SOH replaced by `\|`. No field is named, no value is reported, nothing is compared peer-side. | `phase-9-harness/quickfix-cpp/counterparty/interop_counterparty_main.cpp:211-218`; `phase-9-harness/quickfixj/src/main/java/io/fixpp/phase9harness/quickfixj/InteropCounterparty.java:493-495` |
| 2 | **The interop sessions run against a sentinel dictionary.** Every cell except one gets `make_minimal_dictionary()` — a **FIX 4.2** dictionary containing a **single Heartbeat message**, whose own header reads *"Do NOT use for dictionary-semantic tests."* These are FIX 4.4 cells. Peer side: `UseDataDictionary=N` on every plain TLS config; `=Y` only on the four FIXT variants. | `tests/interop/happy/hp_support.hpp:184`; `tests/support/minimal_dictionary.hpp:1-12`; `phase-9-harness/configs/*.cfg.in` |
| 3 | **`cell_results.yaml` carries no evidence.** `REQUIRED_FIELDS = {id, config, kind, status, matrix_disposition, spec_ref}` — no timestamp, no run identifier, no transcript pointer, no counterparty version. A hand-edited `status: pass` satisfies all nine schema tests. | `tests/interop/cell_results_schema_check_test.py:23` |
| 4 | **The full live matrix is not gated anywhere.** Only `interop-smoke.yml` exists; it runs **one** cell (`HP-QFcpp-init-fix44-logon-hb-logout`), QuickFIX-cpp only, initiator only. The `interop-full-matrix` / `interop-release-prep` named checks the contract references **do not exist as workflows** — the strings occur only inside `interop-smoke.yml` as a forward reference. | `.github/workflows/`; `.github/workflows/interop-smoke.yml:5-8` |

A fifth finding sizes the actual fidelity deficit. The existing business-message test asserts field
values **in one direction only**:

- **fixpp-initiator** (`tests/interop/test_business_message_interop.cpp:317-405`) — real value equality
  on seven fields of the ExecutionReport the peer built (`cap_exec_type`, `cap_ord_status`,
  `cap_symbol`, `cap_side`, `cap_cum_qty`, `cap_avg_px`, `cap_leaves_qty`).
- **fixpp-acceptor** (`:407-443`) — **counters only**: `EXPECT_GE(nos_received, 1)`,
  `EXPECT_GE(er_sent, 1)`, `EXPECT_TRUE(send_ok)`, `EXPECT_EQ(state, Active)`. **No field-value check
  of any kind.** Nothing verifies what the peer received, and nothing verifies that a message fixpp
  emitted was read correctly by the peer.

So for the fixpp-acceptor half of the four role x flavour combos there is **no round-trip fidelity
evidence at all** today. That asymmetry, not message breadth, is the gap this feature closes.

### Why fidelity must be proven against an independent implementation

fixpp's typed message tier carries **no per-message business logic**, so there is no semantic
behaviour to assert — only shape, and shape is only meaningfully proven against another engine:

- `validate_<Msg>` is a required-field-presence walk with exactly two rejection sites
  (`include/fixpp/wire/builder_validate.hpp:74-96`), and its body is a fixed string the emitter
  writes identically for every message (`tools/codegen/fixpp-codegen/emit_builders.cpp:587-596`).
- `build_<Msg>` never calls it — enforced at generation time by
  `assert_builder_surface_validator_free` (`emit_builders.cpp:1250-1265`), which throws if a builder
  file so much as mentions `validate_`.
- Read accessors decode the FIX datatype and stop (`include/fixpp/dict/field_traits.hpp:50-98`);
  `ord_type()` returns `char`, not an enum.
- The generated banner states the design intent verbatim (`Validator.hpp:4-5`): *"Shape/exhaustiveness
  only; behavioural validation is out of scope."*

An assertion written only against fixpp's own accessors therefore compares fixpp to itself. The bar
must be: **the peer's own dictionary-backed parse reports the field values, and those are compared to
what fixpp intended.**

### Scope cut — user decision 2026-09-10

Given the four gaps, this feature is scoped **machinery-first, breadth-second**:

- **IN**: peer-side typed readback (both counterparties), dictionary-backed sessions on both sides,
  both inbound-validation arms, the per-cell evidence field, and end-to-end proof over a **narrow
  business set** — NewOrderSingle (35=D), ExecutionReport (35=8), OrderCancelRequest (35=F),
  OrderCancelReplaceRequest (35=G), OrderCancelReject (35=9) — across **all four** role x flavour
  combos and the full admin repertoire.
- **OUT, to a mechanical follow-on**: the wide sweep across everything QuickFIX-cpp/J support.
  Rationale: the harness has recorded batch-run hazards — `emit_matrix.py --update-goldens` over the
  full matrix *"overwrites the 18 verified HP goldens and races on port-bind"*
  (`phase-9-harness/INTEROP-016-ROADMAP.md:44`). Combining new assertion machinery with a wide sweep
  means debugging both at once.
- **OUT, to a separate workstream**: the `interop-full-matrix` CI tier. This feature makes the cells
  correct and runnable; giving the whole matrix an automated gate is its own decision with its own
  runner-minute cost.

---

## Clarifications

### Session 2026-09-10

- Q: What set of fields must the peer read back, and what must the comparator check against? → A: **Exact set equality** — the peer reports every body field it parsed; the comparator asserts that set is exactly the intent set, failing on a missing field and on a spurious one. Header/trailer fields (8, 9, 35, 34, 49, 56, 52, 10) are excluded as session-managed.
- Q: How is a peer readback record paired with the specific message fixpp sent? → A: **`MsgSeqNum(34)` + direction**, using data already on the wire. Nothing is injected, so the bytes under test are unchanged. A `PossDupFlag(43)` replay is disambiguated by that flag, not by a new key.
- Q: How does the feature guarantee the counterparty carries the readback channel, and that a stale one fails loudly? → A: **Immutable digest pin AND a startup capability handshake.** The counterparty announces a readback-protocol version; a peer that announces nothing, or a version older than the cell requires, **FAILS** the cell — it must not skip and must not pass. Absence of readback records is a failure, never vacuous agreement.
- Q: How are results structured — one cell per conversation, or one cell per message/direction? → A: **Two levels, each with its own exact-set completeness gate.** A cell stays one process run (4 role×flavour combos × 2 validation arms = **8 cells**), preserving what `run_interop_cell.py` means by a cell. Per-message, per-direction witnesses live in a separate evidence record with its own set-equality check, so a missing witness is as detectable as a missing cell.
- Q: Which build configurations must the 8 new cells run under? → A: **All three — `normal`, `asan-ubsan`, `tsan`** (8 × 3 = **24 runs**). This satisfies Article IX §2 on this surface in full. ⚠️ It also makes a **first-ever TSan run on the paired live matrix** part of this feature: `phase-9-harness/INTEROP-COVERAGE-REPORT.md:53,140` records TSan has never been run there, so this is bring-up, not a config flip.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Interop sessions run against the real dictionary (Priority: P1)

A maintainer running any live interop cell gets a session configured with the production FIX 4.4
dictionary on the fixpp side and a dictionary-validating QuickFIX peer on the other side, instead of
a FIX 4.2 single-Heartbeat sentinel and `UseDataDictionary=N`.

**Why this priority**: Every other story depends on it. A "typed readback" from a peer parsing
without a data dictionary is generic field-map access, not typed parsing; and fixpp's inbound
validation cannot be exercised at all against a Heartbeat-only FIX 4.2 dictionary while the cells
carry FIX 4.4 traffic. This story alone also removes a standing misconfiguration.

**Independent Test**: Run the existing `logon-hb-logout` and `NOS->ExecRpt` cells with the real
dictionary on both sides and confirm they still pass, with goldens re-captured and reviewed.

**Acceptance Scenarios**:

1. **Given** a live interop cell for FIX 4.4, **When** the fixpp session is constructed, **Then** its
   `SessionConfig::dictionary` is the production `dictionaries/FIX44.xml`, not
   `make_minimal_dictionary()`.
2. **Given** a live interop cell for FIX 4.4, **When** the QuickFIX counterparty config is rendered,
   **Then** it carries `UseDataDictionary=Y` with a FIX 4.4 `DataDictionary` path.
3. **Given** the dictionary flip changes a peer's serialization or ordering, **When** the golden is
   re-captured, **Then** the diff against the prior golden is reviewed and recorded, not
   auto-accepted.

---

### User Story 2 - The peer reports what it parsed, field by field (Priority: P1)

A maintainer can see, for any message fixpp sent, exactly which fields the QuickFIX peer parsed and
what values it decoded — as structured, machine-comparable data, not as a re-serialized blob.

**Why this priority**: This is the capability the closure bar requires and the one that does not
exist. Without it, "live QuickFIX interop" means "the peer did not disconnect."

**Independent Test**: Send one NewOrderSingle from fixpp with a known field set; assert the peer's
emitted readback record names each field and reports each value exactly. Force a mutation in the
sent value and confirm the comparison goes RED.

**Acceptance Scenarios**:

1. **Given** fixpp sends a message with a known field set, **When** the peer's application callback
   receives it, **Then** the peer emits a structured record naming each expected field and the value
   it decoded, keyed to the message's identity.
2. **Given** the peer emitted a readback record, **When** the harness compares it to what fixpp
   intended to send, **Then** a mismatch in any single field fails the cell with that field named.
3. **Given** the readback path is present, **When** a field fixpp set is absent from the peer's
   record, **Then** that is a failure, distinguishable from a value mismatch.
4. **Given** both counterparty flavours, **When** the same message is sent to each, **Then** both
   emit records in the same format, so one comparator serves both.

---

### User Story 3 - Round-trip fidelity in both directions, all four combos (Priority: P2)

A maintainer runs one scripted conversation — Logon, the admin repertoire, the five business
messages, Logout — in each of the four role x flavour combinations, and every business message is
verified for exact field values in the direction it travelled.

**Why this priority**: This is the deliverable row 4c consumes. It depends on US1 and US2 but is
independently valuable: it closes the fixpp-acceptor fidelity hole even for the messages already
covered.

**Independent Test**: Run the conversation in all four combos; confirm each business message has a
named per-field witness in both directions, including the fixpp-acceptor direction that today has
none.

**Acceptance Scenarios**:

1. **Given** fixpp is the acceptor, **When** the QuickFIX initiator sends a business message,
   **Then** fixpp's typed read tier is asserted to return the exact field values the peer set — not a
   received-count.
2. **Given** fixpp is the acceptor, **When** fixpp emits a business message in reply, **Then** the
   peer's readback record is compared field-by-field against what fixpp intended.
3. **Given** any of the five business message types, **When** the conversation runs, **Then** each has
   a witness in both directions in every combo where that direction is applicable.
4. **Given** the admin repertoire (TestRequest/Heartbeat, ResendRequest, SequenceReset-GapFill,
   Reject), **When** the conversation runs, **Then** each admin exchange is exercised within the same
   session rather than as a separate cell.

---

### User Story 4 - Both inbound-validation arms, proven equivalent (Priority: P2)

A maintainer runs the whole conversation twice per combo — once with fixpp's dictionary-driven
inbound validation off (the shipped default) and once with it on — and learns whether the two arms
accept identical traffic.

**Why this priority**: `validate_inbound_messages` defaults to `false`
(`include/fixpp/session/session_config.hpp:477`), and the dictionary-driven validator it gates
(`include/fixpp/wire/validator.hpp:175-216`, wired at `src/session/session.cpp:2024`) checks enum
validity, datatype structure, required fields and group structure — strictly more than the typed tier
checks. Today it is exercised by exactly one interop cell
(`tests/interop/happy/hp_fix44_reject_invalid_admin_test.cpp:204-208`). Against a real peer it is
otherwise untested.

**Independent Test**: Run both arms of one combo and assert the accepted-message sets are identical;
then seed a message the dictionary should reject and confirm the arms diverge.

**Acceptance Scenarios**:

1. **Given** the conversation runs with validation off and on, **When** both arms complete, **Then**
   the set of accepted messages is identical and the assertion states so explicitly.
2. **Given** the arms diverge, **When** the cell reports, **Then** it names the message and the
   validator finding, because a divergence is a finding in either direction — our dictionary
   rejecting legitimate peer traffic, or failing to reject what it should.
3. **Given** the validation-on arm, **When** it runs, **Then** it is confirmed to have a non-null
   production dictionary loaded, so a silently-skipped arm cannot read as agreement.

---

### User Story 5 - Evidence a catalogue flip can stand on (Priority: P3)

Whoever executes row 4c can point at a machine-readable record showing, per catalogue row, which
named witness proved it, in which direction, against which counterparty at which version, from a run
that demonstrably happened.

**Why this priority**: Lowest priority to build, but it is what makes the other four stories usable
downstream. The 069 rows each name their exact round-trip test; these rows must too.

**Independent Test**: Produce the evidence records, then confirm the schema check rejects a row
claiming a pass with no corroborating run artifact.

**Acceptance Scenarios**:

1. **Given** a cell ran, **When** its result row is emitted, **Then** the row carries evidence tying
   it to that run — at minimum a run identifier, a timestamp, the counterparty flavour and version,
   and a pointer to the produced artifact.
2. **Given** a row claims `status: pass` with no corroborating evidence, **When** the schema check
   runs, **Then** it fails.
3. **Given** the evidence records exist, **When** a catalogue row is flipped, **Then** the evidence
   cell names a specific witness and direction, not an aggregate.

---

### Edge Cases

- **Counterparty unavailable.** The existing `skip:counterparty-unavailable` path must remain
  distinguishable from a pass. A skip must never satisfy a fidelity assertion, and the new evidence
  rows must record a skip as a skip.
- **Golden drift from the dictionary flip.** Turning on `UseDataDictionary=Y` may change peer
  serialization or field ordering, moving committed goldens. Re-captures must be per-cell and
  verify-first; `--update-goldens` across the matrix is known to overwrite verified goldens and race
  on port-bind.
- **The TSan arm is unprecedented on this surface.** It may not come up at all on a live two-process TLS
  conversation — timing changes under instrumentation can move heartbeat and test-request cadence enough
  to break a session that passes under `normal`. That outcome is a finding to record and resolve, not a
  reason to mark the arm `n/a`.
- **Peer disconnects instead of rejecting.** `KNOWN-LIMITATIONS.md:87-106` records that only
  QuickFIX-J 3.0.1 is confirmed to emit `Reject(35=3)` on the pinned malformed input; QuickFIX-cpp may
  disconnect. Negative arms must tolerate both without treating either as a fidelity pass.
- **QuickFIX-cpp cannot inject controllable hostile frames.** `L-021-3`
  (`spec/behaviors-and-limitations.md`; described at `phase-9-harness/INTEROP-COVERAGE-REPORT.md:41`)
  records that QF-cpp strips `PossDupFlag(43)`/`OrigSendingTime(122)` and exposes no injection knob.
  Any arm needing peer-originated hostile input is QuickFIX-J-only and must be declared so, not
  silently skipped.
- **Decimal and timestamp canonicalization.** Comparisons must be value-equality on decoded values,
  not byte-equality on rendered text; existing normalization already special-cases `52`, `10`, `60`,
  `11`, `37`, `17`.
- **Port-bind races.** Cells run sequentially with leased ports; a batch run must not be the only way
  to reproduce a result.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Live FIX 4.4 interop cells MUST configure the fixpp session with the production FIX 4.4
  dictionary, replacing the `make_minimal_dictionary()` sentinel for those cells.
- **FR-002**: QuickFIX counterparty configurations used by these cells MUST enable data-dictionary
  validation with the corresponding FIX 4.4 dictionary.
- **FR-003**: Both counterparty applications MUST emit, for every application message they receive, a
  structured readback record naming **every body field the peer parsed** and the value it decoded for
  each. Header and trailer fields (8, 9, 35, 34, 49, 56, 52, 10) are excluded — they are session-managed
  and not part of the fidelity claim.
- **FR-004**: The readback record format MUST be identical across the QuickFIX-cpp and QuickFIX-J
  counterparties, so a single comparator serves both.
- **FR-005**: The readback record MUST be keyed on `MsgSeqNum(34)` plus the direction the message
  travelled, so it can be paired with the specific message fixpp sent. The harness MUST NOT inject any
  correlation field into the traffic — the bytes under test must be exactly the bytes fixpp would send
  in production. A `PossDupFlag(43)` replay carrying a repeated sequence number MUST be distinguished
  by that flag rather than by a new key.
- **FR-006**: The harness MUST compare each readback record against the intent record by **exact set
  equality**, and MUST fail the cell naming the offending field on any of: a value mismatch, a field the
  intent record declares that the peer did not report, or a field the peer reported that the intent
  record does not declare. A subset comparison MUST NOT be used — it cannot detect a builder emitting a
  field it never intended.
- **FR-007**: For every business message in the narrow set, the cell MUST assert exact field values in
  the direction the message travelled — including the fixpp-acceptor direction, which today asserts
  only counters.
- **FR-008**: The scripted conversation MUST run within a single session per cell: Logon, the admin
  repertoire (TestRequest/Heartbeat, ResendRequest, SequenceReset-GapFill, Reject), the five business
  messages, Logout.
- **FR-009**: The conversation MUST run in all four role x flavour combinations, and any combination
  that cannot run a given arm MUST be declared with a reason rather than silently skipped.
- **FR-010**: Each cell MUST run under two arms — inbound validation off and on — and MUST assert the
  two arms accept an identical set of messages.
- **FR-011**: The validation-on arm MUST verify at run time that a production dictionary is loaded, so
  that a misconfigured arm cannot report agreement by doing nothing.
- **FR-012**: Any divergence between the two validation arms MUST be reported as a named finding
  identifying the message and the validator's objection.
- **FR-013**: Each emitted cell result MUST carry evidence binding it to an actual run: a run
  identifier, a timestamp, the counterparty flavour and version, and a pointer to the run artifact.
- **FR-014**: The cell-results schema check MUST reject a row claiming a pass without that evidence.
- **FR-015**: Evidence MUST be emitted per message, per direction, and per role x flavour x validation
  arm — not as a single aggregate verdict — so a catalogue row can cite one named witness.
- **FR-015a**: Results MUST be structured at **two levels**. A *cell* remains one process run — 4 role x
  flavour combinations x 2 validation arms = **8 cells** — preserving the existing meaning of a cell as
  the unit the runner dispatches. *Witnesses* (message x direction) live in a separate evidence record.
- **FR-015b**: The witness evidence record MUST carry its own **exact-set completeness gate**, in the
  same spirit as the existing cell-id set-equality check, so that a witness which silently stops being
  produced is detected rather than absent. A missing witness MUST fail that gate.
- **FR-016**: A counterparty-unavailable outcome MUST remain distinguishable from a pass in both the
  cell result and the evidence record.
- **FR-016a**: Each counterparty MUST announce a readback-protocol version at startup, and the harness
  MUST refuse to run a cell against a peer that announces nothing or announces a version older than the
  cell requires. That refusal MUST be a **failure**, not a skip and not a pass.
- **FR-016b**: The counterparty image MUST be referenced by immutable digest, not by a mutable tag, so
  that the exact counterparty build behind any result is recoverable after the fact.
- **FR-016c**: A message for which **no** readback record arrived MUST fail its cell. An empty or absent
  readback set MUST NOT satisfy any fidelity comparison — the comparator MUST assert that a record was
  received before comparing its contents.
- **FR-017**: Every new assertion MUST be accompanied by a forced-failure demonstration proving it can
  report RED: a wrong field value, an omitted required field, and a message the peer should reject.
- **FR-018**: In addition to forced-miss arms, at least one arm MUST force a **spurious hit** —
  deleting the mechanism under test and asserting the witness goes RED — so that an assertion cannot
  pass by observing something other than the property it claims to measure. One such arm MUST be
  *run the cells against a counterparty build with the readback channel removed* and assert every
  affected cell goes RED, which jointly exercises FR-016a and FR-016c.
- **FR-019**: Golden re-captures caused by the dictionary flip MUST be performed per cell with the
  prior golden diffed and the change recorded; a matrix-wide auto-update MUST NOT be used.
- **FR-020**: Existing passing cells outside the narrow business set MUST continue to pass, or any
  change in their status MUST be explained and recorded.
- **FR-021**: Every cell MUST run under all three build configurations — `normal`, `asan-ubsan` and
  `tsan` — giving 4 role x flavour x 2 validation arms x 3 configs = **24 runs**, and each MUST emit its
  own result row rather than being folded into the `normal` row.
- **FR-022**: The `tsan` configuration MUST be treated as **bring-up, not a config flip**. TSan has never
  been run on the paired live matrix, so the feature MUST establish that the TSan arm actually executes
  the conversation — a TSan run that skips, aborts during setup, or produces no witnesses MUST NOT be
  recorded as a pass. Any TSan finding is a real defect until disproven; none may be dismissed as a test
  artifact without a reproduction or a client-path analysis.
- **FR-023**: Only fixpp is sanitizer-instrumented; the QuickFIX counterparties run as unmodified
  production binaries. The feature MUST state that bound explicitly wherever a sanitizer result is
  reported, so a clean run is not read as covering the peer.

### Key Entities

- **Cell**: one process run — role x flavour x validation arm — with an identifier, a counterparty, a
  configuration template, a status, and evidence. Eight in this feature.
- **Witness**: one (message, direction) fidelity result inside a cell; the unit a catalogue row cites.
- **Readback record**: the peer's report of what it parsed — a message identity plus the **complete** set
  of body field/value pairs the peer decoded, emitted by the counterparty application.
- **Intent record**: the field set fixpp intended to send for a given message, keyed on `MsgSeqNum(34)`
  and direction, against which a readback record is compared.
- **Evidence row**: the per-message, per-direction, per-combination result a catalogue row cites,
  bound to a specific run.
- **Conversation script**: the ordered message sequence a cell drives within one session.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For all five business message types, in both directions, in every applicable role x
  flavour combination, a named witness asserts exact field values — with **zero** message/direction
  pairs resting on a received-count or a session-stayed-up assertion.
- **SC-002**: The fixpp-acceptor direction, which today has no field-value assertion, has one for
  every business message in the set.
- **SC-003**: Every new assertion has a recorded forced-failure demonstration showing it reporting
  RED, and at least one forced **spurious-hit** demonstration exists.
- **SC-004**: Both validation arms run for every cell, and their accepted-message sets are asserted
  identical; any divergence is reported as a named finding rather than absorbed.
- **SC-005**: A cell result claiming a pass without run evidence is rejected by the schema check —
  demonstrated by a deliberately falsified row.
- **SC-005a**: Running the cells against a counterparty build with the readback channel removed turns
  every affected cell RED — demonstrated, not argued. Zero cells pass or skip in that configuration.
- **SC-006**: Every cell in the feature's set runs against a production FIX 4.4 dictionary on both the
  fixpp side and the peer side; no cell in the set uses the single-Heartbeat FIX 4.2 sentinel.
- **SC-007**: The evidence output is sufficient for row 4c to cite a specific named witness per
  catalogue row, demonstrated by producing the citation end to end for at least one row. ⚠️ The narrow
  message set maps to `A-001`, `A-003`, `A-004`, `A-006`, `A-007`, all of which are **already `done`** —
  so this feature closes **zero** tail rows by itself and the demonstration is of the citation
  mechanism, not of a status flip. The 10 QuickFIX-route tail rows close in the breadth follow-on.
- **SC-009**: The witness completeness gate fails when a witness stops being produced — demonstrated by
  deleting one witness and observing the gate go RED.
- **SC-010**: All 24 runs (8 cells x 3 configs) complete and emit their own result row, with **zero**
  configurations silently folded into another's row.
- **SC-011**: The `tsan` arm is shown to have genuinely executed the conversation — it produces the same
  witness set as the `normal` arm, not an empty one. A TSan run yielding no witnesses is a failure.
- **SC-008**: All cells outside the feature's set retain their prior status, or each change is
  explained in the feature's records.

## Assumptions

- The GHCR counterparty image (`ghcr.io/catalinserafimescu/fixpp-interop-counterparties`) remains the
  distribution vehicle. It carries QuickFIX-cpp **v1.16.0** and QuickFIX-J **3.0.1**; it was last
  published 2026-06-19 and **must be rebuilt and republished**, because this feature changes both
  counterparty applications. The existing reference is the mutable tag `:latest`
  (`.github/workflows/interop-smoke.yml:44`); FR-016b replaces it with a digest.
- QuickFIX-J's shaded counterparty jar is present in that image but is **not currently extracted** by
  `interop-smoke.yml`; any CI reach for QuickFIX-J cells requires extracting it.
- The narrow business set (35=D, 8, F, G, 9) is supported by both QuickFIX flavours at their pinned
  versions and needs no peer-side message-definition work.
- FIX Latest is excluded — there is no QuickFIX peer for it. Rows `A-035..A-065` close by differential
  verification against the 181-message baseline under row 4b.
- The typed builders for the narrow set already exist and are correct; this feature verifies fidelity,
  it does not add or change builders.
- Cells continue to run over TLS `one_way_ca`; mutual mTLS stays `deferred:v1.1-mtls`.
- The three-config decision (user, 2026-09-10) supersedes the recommendation to defer TSan. The cost is
  accepted deliberately: 24 runs instead of 16, with a first-ever TSan bring-up on the paired live matrix
  inside this feature's scope. If that bring-up proves to be its own investigation, it is escalated as a
  filed issue rather than quietly downgraded to `n/a`.
- Sanitizer instrumentation covers fixpp only; the counterparties are unmodified production binaries
  (`tests/interop/KNOWN-LIMITATIONS.md:108-115`). A clean sanitizer run bounds fixpp, not the peer.
- The parent harness at `research/G19-fix-fpml-iso20022/phase-9-harness/` is git-tracked in the parent
  repository, so counterparty changes span two repositories — the library submodule and the parent.

## Explicitly out of scope

- **The wide message sweep** across everything QuickFIX-cpp/J support — deferred to a mechanical
  follow-on once this machinery is proven (user decision 2026-09-10).
- **The `interop-full-matrix` CI tier** — a separate workstream (user decision 2026-09-10). This
  feature does not give the historical 63 live cells an automated gate.
- **Catalogue status flips.** This feature produces evidence and flips no rows; row 4c owns the flips.
- **The Article XVIII §7 / Article I §1 constitution amendment**, which the ★ scope decision assigns
  to the first catalogue-closure feature — that is row 4c, not this one (user decision 2026-09-10).
- **Interop depth extras**, post-v1.0 per REMAINING-WORK §E: G4b live Fix8 and mutual mTLS; C-103
  chunked-resend; the optional live-interop cells (025 StandbyRehydrate, the QuickFIX-cpp hostile arms
  waived per `L-021-3`).
- **FIX Latest interop** — no QuickFIX peer exists.
