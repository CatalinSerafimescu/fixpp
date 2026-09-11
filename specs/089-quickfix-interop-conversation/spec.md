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
| 2 | **The interop sessions run against a sentinel dictionary.** Every cell except one gets `make_minimal_dictionary()` — a **FIX 4.2** dictionary containing a **single Heartbeat message**, whose own header reads *"Do NOT use for dictionary-semantic tests."* These are FIX 4.4 cells. Peer side: `UseDataDictionary=N` on every plain TLS config; `=Y` only on the FIXT variants (`*-tls-fixt*.cfg.in`), which set it through the `TransportDataDictionary`/`AppDataDictionary` pair rather than the single `DataDictionary` key. | `tests/interop/happy/hp_support.hpp:184`; `tests/support/minimal_dictionary.hpp:1-12`; `phase-9-harness/configs/*.cfg.in` |
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

- Q: What set of fields must the peer read back, and what must the comparator check against? → A: **Exact set equality** — the peer reports every body field it parsed; the comparator asserts that set is exactly the intent set, failing on a missing field and on a spurious one. ~~Header/trailer fields (8, 9, 35, 34, 49, 56, 52, 10) are excluded as session-managed.~~ ⚠️ **The exclusion clause is SUPERSEDED at Gate A round 1**: the rule is **structural**, not an eight-tag enumeration — see the Gate A round 1 session below and FR-003. The exact-set-equality half stands unchanged.
- Q: How is a peer readback record paired with the specific message fixpp sent? → A: **`MsgSeqNum(34)` + direction**, using data already on the wire. Nothing is injected, so the bytes under test are unchanged. ~~A `PossDupFlag(43)` replay is disambiguated by that flag, not by a new key.~~ ⚠️ **The disambiguator is SUPERSEDED at Gate A round 1**: the key gains an emitter-assigned **occurrence ordinal** (FR-005). `PossDupFlag(43)` separates an original from a replay but not replay *n* from replay *n+1*, and `L-021-3` records that QuickFIX-cpp **strips** it from the wire — so on those combos it carries no information at all. The no-injection half stands unchanged: the ordinal is assigned by the emitter, not written into the traffic.
- Q: How does the feature guarantee the counterparty carries the readback channel, and that a stale one fails loudly? → A: **Immutable digest pin AND a startup capability handshake.** The counterparty announces a readback-protocol version; a peer that announces nothing, or a version older than the cell requires, **FAILS** the cell — it must not skip and must not pass. Absence of readback records is a failure, never vacuous agreement.
- Q: How are results structured — one cell per conversation, or one cell per message/direction? → A: **Two levels, each with its own exact-set completeness gate.** ~~A cell stays one process run~~ (4 role×flavour combos × 2 validation arms = **8 cells**), preserving what `run_interop_cell.py` means by a cell. ⚠️ **The *"one process run"* phrasing is SUPERSEDED at Gate A round 2** — under the four-config decision a cell is executed **four** times, so a cell is the *logical* identity and a **run** is the execution (FR-015a, FR-013a). The **8-valued `cell_id` space** this answer was protecting is unchanged; only the word "run" moved. Per-message, per-direction witnesses live in a separate evidence record with its own set-equality check, so a missing witness is as detectable as a missing cell.
- Q: Which build configurations must the 8 new cells run under? → A: ~~**All three — `normal`, `asan-ubsan`, `tsan`** (8 × 3 = **24 runs**).~~ **SUPERSEDED at Gate A round 1 — see Session 2026-09-10 (Gate A round 1) below.** The decision was taken on a label that does not mean what it says: `asan-ubsan` names a harness config whose preset enables **ASan only**, so "all three sanitizer configs" delivered two sanitizer kinds, and the claim *"satisfies Article IX §2 in full"* was false as recorded. ⚠️ The TSan half of the original answer stands unchanged: this feature carries a **first-ever TSan run on the paired live matrix** (`phase-9-harness/INTEROP-COVERAGE-REPORT.md:53,140`), so that arm is bring-up, not a config flip.

### Session 2026-09-10 (Gate A round 1)

- Q: `asan-ubsan` names a preset that enables ASan only — how is Article IX §2 (*"ASan, UBSan, TSan must all run and pass"*) actually satisfied on this surface? → A: **Four configurations — `normal`, `asan`, `ubsan`, `tsan`** (8 × 4 = **32 runs**). Not a combined ASan+UBSan preset: `linux-clang-ubsan` already exists, so no new preset is needed, no Tier-1 CI lane is edited, and per-sanitizer coverage is honest rather than inferred from a label. This supersedes the three-config answer above and its arithmetic everywhere it appears (FR-021, SC-010, SC-011, the disk sequencing, the evidence row count, the completeness census).
- Q: The `asan-ubsan` name is used outside this bundle. Is 089 introducing the gap or inheriting it? → A: **Inheriting it.** The misnomer is pre-existing and repo-wide: `phase-9-harness/tools/run_interop_cell.py`'s `CONFIG_TO_PRESET` maps `asan-ubsan` → `linux-clang-asan`; `phase-9-harness/INTEROP-016-DESIGN.md:130` defines the config vocabulary as `normal|asan-ubsan|tsan`; `phase-9-harness/INTEROP-COVERAGE-REPORT.md:53` states the charter requires ASan+UBSan. 089 **corrects an inherited false claim** rather than creating one. ⛔ **There is no historical `asan-ubsan` corpus** — an earlier revision of this answer asserted one in `cell_results.yaml`; measurement found that file holds only `config: normal` rows. What the retired label did produce is two **overclaims**, in `spec/behaviors-and-limitations.md` and `spec/feature-catalogue.md`, which credited UBSan coverage the ASan-only preset never ran; both are **corrected** by this feature, not exempted. FR-021a is the normative home of the distinction — prescriptive artifacts are corrected, a record that was true when written is left alone, and a record that overclaims is fixed.
- Q: How big is the exposure the UBSan gap left open? → A: **Bounded to fixpp.** Per FR-023 only fixpp is sanitizer-instrumented; the counterparties are unmodified production binaries either way. So the missing UBSan arm bounded **fixpp's** UB coverage on this surface and nothing else — it was never going to cover the peer. That does not excuse the false PASS; it is the correct size of what was missing.
- Q: FR-018 defines a "spurious hit" as *deleting the mechanism under test and asserting RED* — is that a spurious hit? → A: **No, that is the forced-miss recipe**, and the definition is inverted. A spurious-hit arm is **an arm that makes the guard report PASS for a reason other than the property it claims to measure**. FR-018 is rewritten to that definition and every arm is re-derived from it; the readback-channel-removed arm keeps its proof obligation but is relabelled as the FR-016c forced-**miss** arm it always was.
- Q: Is a peer-side **typed accessor** required, or merely available? → A: **Required, and it is a distinct assertion from generic enumeration.** A typed accessor (`NewOrderSingle::get(Symbol&)`) compiles only if the field belongs to that message in FIX 4.4 and throws `FieldNotFound` if the peer did not receive it — a *schema-conformance* check generic enumeration cannot make. Complete generic enumeration proves missing/spurious/group-shape, which typed access cannot. Both are required; the conversation script declares which fields are read each way (FR-003b).
- Q: What is the readback record's decision for non-UTF-8 bytes? → A: **Every field value is emitted as a `value` string containing only characters the JSON escaping rule admits, and any field whose raw bytes are not valid UTF-8 is emitted instead as a tagged binary form** — `{"path": …, "value_b64": "<base64 of the raw bytes>"}` with no `value` key. Base64 because it is byte-exact, has one canonical alphabet, and both a hand-rolled C++ writer and a Java writer can produce it identically. See `contracts/readback-jsonl.md` § Escaping.
- Q: `spec.md`'s header/trailer exclusion is an 8-tag enumeration; the data model and the readback contract state a structural rule. Which governs? → A: **The structural rule governs**, and the eight tags become an illustrative example. Header membership on both engines is *built-in list ∪ dictionary-declared header* (`quickfix-cpp/src/C++/Message.cpp` `isHeaderField(int, const DataDictionary*)`; `quickfixj-base/src/main/java/quickfix/Message.java` `isHeaderField`), so the partition **moves when US1 turns the dictionary on** and the two engines' built-in lists **already differ**. FR-003 is restated structurally and the contract pins the reconciliation.
- Q: Does R-5's per-cell dictionary decision govern the fixpp side as well as the peer side? → A: **No — the axes were crossed.** R-5's evidence is entirely peer-side and governs **FR-002**. FR-001 governs fixpp's own `SessionConfig::dictionary`, and R-5 says nothing about it. Both are scoped to **the cells this feature adds**; existing live cells are out of scope for either flip, and US1's Independent Test is narrowed accordingly.

---

### Session 2026-09-10 (Gate A round 3 — post-exhaustion hand-edit)

- Q: Round 3 proved `fix_type` cannot witness typed-accessor invocation — the counterparty holds bytes, dictionary and script, so every typed **output value** is derivable without calling the getter, and the one escape (a typed-parse failure on a malformed value) is closed by FR-002's peer-side `UseDataDictionary=Y`. How is FR-018's spurious-hit obligation met for this guard? → A: ~~**Instrument the counterparty with an invocation seam** (user decision 2026-09-10). Each `typed_reads` entry gains `accessor_witness`, a value obtainable **only** by holding the object the generated typed getter returned.~~ ⚠️ **SUPERSEDED at Gate A fresh loop round 1** — see *Session 2026-09-10 (Gate A fresh loop, round 1)* below: reading the generated getters in both vendored engines proved **no serialized value can witness invocation**, `accessor_witness` included. The half that stands: `fix_type` is retained as evidence of dictionary-backed resolution and is explicitly **not** accessor provenance.
- Q: Where does that seam go — the vendored engines' generated accessors, or our counterparty programs? → A: **Our counterparty programs** (`phase-9-harness/quickfix-cpp/counterparty/`, `phase-9-harness/quickfixj/.../InteropCounterparty.java`). **That half stands** and carries over to the compile-time arm: the guard binds *our* counterparty source, and the vendored engines stay unpatched. ~~The placement rule is what makes it non-vacuous: the seam must run **through the accessor's returned object**, never beside the call.~~ ⚠️ **SUPERSEDED at Gate A fresh loop round 1** — routing through the returned object does **not** make it non-vacuous, because the returned object **is the caller's own object**. See the fresh-loop session below.
- Q: Does instrumenting the counterparty violate FR-023's *"unmodified production binaries"*? → A: **No.** FR-023 scopes to the **vendored QuickFIX engines**, which remain unpatched — the differential premise (an independent implementation) is untouched. The counterparty *programs* are this harness's own code and always have been. ⚠️ **Stated limit** (unchanged by the fresh-loop supersession, and it is the same limit the compile-time arm carries): the guard binds **our call site**, not the engine's interior. It cannot detect a QuickFIX getter that internally short-circuits, and it does not claim to.

### Session 2026-09-10 (Gate A fresh loop, round 1)

- Q: Three rounds have now proposed an artifact-level observable for the typed-accessor guard (`fix_type`,
  then a decimal spelling, then `accessor_witness`) and each turned out synthesizable. How is FR-018's
  spurious-hit obligation met for this guard? → A: **It is not met at the artifact level, and it cannot
  be. The guard becomes a COMPILE-TIME arm** (user decision 2026-09-10). The artifact-level claim and the
  `accessor_witness` field are **deleted**.

- Q: Why *cannot* it be met at the artifact level — what makes this different from the other guards? → A:
  ⛔ **STATED DESIGN FACT — the impossibility, recorded so a fourth attempt is not made.**

  > **A generated typed accessor is a pure copy of message state into an object the CALLER already owns.
  > Neither the message nor the field records that the call happened. Therefore no value serialized into
  > the readback stream can witness typed-accessor invocation.**

  This is structural, not an observable we have not found yet. The evidence is the generated getters
  themselves, in the vendored engines:

  | Engine | Where | Body |
  |---|---|---|
  | QuickFIX-cpp | the `FIELD_SET` macro in `reference-engines/quickfix-cpp/include/quickfix/FieldMap.h`, which every generated message class expands once per declared field | `FIELD &get(FIELD &field) const { return (FIELD &)(MAP).getField(field); }` — returns a reference to **the caller's own object** |
  | QuickFIX-J | each per-field `get` in the generated `quickfix/fix44/*.java` | `public quickfix.field.Symbol get(quickfix.field.Symbol value) throws FieldNotFound { getField(value); return value; }` — **literally returns its parameter** |

  So of the pair the superseded design proposed as the witness:

  - **`getTag()`** (`FieldBase::getTag` in `reference-engines/quickfix-cpp/include/quickfix/Field.h`)
    returns `m_tag`, which the **caller's own constructor** set *before* `get()` was called. It is not a
    function of the invocation at all — it is the caller's literal, round-tripped.
  - **`getValue()`** (`StringField::getValue`, same header — ⚠️ **not** on `FieldBase`, which is the API
    error the superseded sentence also carried) returns the field's wire string, which generic
    enumeration already holds.

  ⚠️ **The one counterexample a later reviewer will reach for is closed.** QuickFIX-J's `getGroups(tag)`
  *does* mutate the message (`computeIfAbsent`) and would leave a trace — but `readback-jsonl.md` C-5
  forbids exactly that mutation, it is QFJ-only, and using it as a witness would require the emitter to
  cause the corruption C-5 exists to detect. **It is not an escape. Do not re-propose it.**

- Q: What does the compile-time arm assert instead, and what proves *it* is not vacuous? → A: FR-003b
  already named the right observable and never used it — a typed accessor *"compiles only if the field
  belongs to that message in FIX 4.4"*. So:

  - **The assertion**: the counterparty source calls the **per-message generated accessor** for each field
    the script declares as a typed read. That call **is** the schema-conformance check, enforced by the
    compiler at build time.
  - **The anti-vacuity arm** is a **negative-compilation** arm: mutate the counterparty source to call the
    generated accessor with a field that does **not** belong to that message, and assert the **build
    FAILS**. Named mechanism and both arms: `plan.md` § *External obligations* → *the typed-accessor
    compile arm*.
  - ⭐ **The instrument was proven able to report non-zero, on every toolchain the matcher claims, before
    this decision was written.** Re-derivation recipe (run it; do not trust this paragraph):
    `-fsyntax-only -I <the host's own include path>` — ⛔ **no `-std=` flag is pinned here**; run it under the
    **language standard the arm's own host resolves** (the counterparty CMake project sets it, and a value
    written down here would be wrong the moment that moves) — over a TU calling
    `FIX44::NewOrderSingle::get(FIX::Symbol&)` (declared on that message ⇒ must compile) and then
    `FIX::LastPx&` (not declared on it ⇒ must fail), under **each** C++ compiler the arm's execution host
    may resolve to; and the `javac` equivalent against the QuickFIX-J build output with
    `quickfix.field.Symbol` / `quickfix.field.LastPx`. **Re-run 2026-09-10 on the pinned vendored trees**
    under g++ 13.3.0, clang 22.1.2 and javac 21.0.12, reading each **process exit status** rather than a
    filtered log: every positive control exited **0**, every mutant exited **non-zero**.
    ⛔ **THE POLARITY IS THE RESULT; NO DIAGNOSTIC TEXT IS RECORDED HERE.** Three consecutive revisions of
    this bundle pinned a diagnostic literal in this paragraph and each was falsified by the next round —
    the text depends on the **locale** and on the mutated call site's **receiver constness** as well as on
    the compiler, so a literal is under-determined even for one compiler — FR-003b's matcher clause below
    states both variables. **Derive; do not transcribe.**
    ⛔ **RUN EVERY TOOLCHAIN THE HOST MAY RESOLVE TO, NOT A SUBSET.** An earlier revision of this paragraph
    proved the instrument on `g++` and `javac` while the matcher claimed *"(clang/gcc)"* — proving an
    instrument on a subset of the toolchains it claims is this repository's dominant defect class occurring
    **inside** the paragraph written to prevent it. ⚠️ **Which toolchains those are follows from the arm's
    EXECUTION HOST**, stated in one place: `plan.md` § *External obligations* → the typed-accessor
    compile-arm row. ⚠️ There is **no generic `get` overload** on
    `FieldMap`, `Message`, or either generated class that could swallow the mutant; that absence is what
    makes the arm work and is the thing to re-check if an engine is ever re-pinned.

- Q: What is the honest scope of the compile-time arm? → A: ⚠️ **State it plainly; it is less than the
  deleted claim pretended to be.** It proves the **schema-conformance property is really checked** — that
  the counterparty reaches the field through the per-message generated accessor, which the compiler
  refuses for a field the message does not declare. It does **not** prove runtime invocation.
  **FR-018's spurious-hit obligation for *runtime* typed-accessor invocation is recorded as
  STRUCTURALLY UNSATISFIABLE**, on the source evidence above — not deferred, not waived pending a better
  idea. SC-003 is scoped accordingly.

  This is the repository's own idiom for a property a build can decide:
  `tests/session/test_quickfix_compat_path_b_guard.cpp` pins a decision with a file-scope `static_assert`
  whose comment reads *"No runtime assertion is needed — the BUILD IS THE TEST."* ⚠️ Cite it for the
  **idiom only**: that guard is a *positive* `static_assert`, while this arm asserts a mutation must
  **not** compile, which needs its own mechanism (see `plan.md`).

### Session 2026-09-10 (republish ordering — post-Gate-A, during checklist audit close-out)

- Q: FR-026 enumerated two republish orderings and recommended pinning the existing consumers to the
  pre-089 digest first. Which ordering does this feature actually take? → A: **Neither. The ordering is
  `publish → verify → depend`, with NOTHING pinned.** Publish the rebuilt image, run FR-020's regression
  against it, and only then let anything be pinned to it or built on it.
  ⛔ **Option 1 was rejected because it obstructs the verification it exists to enable** — FR-020 asks
  whether the existing cells still pass *against the new counterparty*, and a pin to the pre-089 digest
  means nothing exercises it, so the pin would have to be lifted to test. It also cost two pins **plus**
  an override mechanism for 089's own cells that was never designed.
  ⛔ **Option 2 (publish under a new tag, move `:latest` after FR-020 is green) was rejected** because a
  deferred move nobody performs leaves `:latest` stale indefinitely — it trades a loud exposure for a
  silent one.
  ⭐ **Both were remedies for a risk whose measured population was EMPTY** (zero open pull requests in
  either repository, 2026-09-10), so the residual is recorded as a **sequencing rule, not a count**: open
  no pull request touching the consumer paths between the publish and FR-020 reporting green, re-deriving
  the open set at publish time. ⚠️ **`research.md` R-11's prescription is superseded by this**; its concern
  — that nothing should silently depend on an unverified image — is what the new ordering discharges.

### Session 2026-09-11 (FR-016b — the consumers' failure direction)

- Q: FR-016b said an unpullable pin is a skip and a skip is green, and keyed its step-log obligation on
  every site that pins a digest. Both halves were checked at source; what does the requirement become?
  → A: **A CONDITION with a re-derivation recipe, and no recorded result.** Measured 2026-09-11: the
  stated trigger was false — an image that will not pull fails the pull step and reddens both consumers;
  the scope was empty — after FR-026 neither consumer pins; and the correction first written into
  `plan.md` was itself partly false — it attributed a silent runtime skip to both consumers when only
  one consumer's outcome handling tolerated one, and it cited this bundle's FR-023, which is the
  sanitizer bound. ⛔ **Each revision had replaced a wrong claim about the workflows with a new one, so
  the claim is deleted rather than corrected again**: FR-016b now states the condition (a consumer whose
  outcome handling lets a cell that did not run leave the job green) and how to re-derive it. The digest
  requirement itself is unchanged.

### Session 2026-09-11 (`/speckit-implement` — the build unit R-1 measures, and UBSan's recoverable mode)

- Q: R-1 / T001 said to build each configuration **from clean**, but the matrix never builds from clean:
  it builds the interop driver targets inside the per-configuration trees that already exist. Which does
  T001 measure? → A: **The targeted build as the matrix performs it — incrementally, in the existing
  per-configuration trees** (user decision). A configuration whose tree is absent or partial is built from
  whatever exists, which is then the measured cost. The four binaries the new cells name do not exist
  until T052, so the measurement uses the interop driver targets the existing cells name, and T052a
  re-derives it as soon as T052's targets exist, before any later gated build.
- Q: The `ubsan` configuration runs cells through `run_interop_cell.py`, which launches the gtest binary
  directly and so never receives the test preset's `UBSAN_OPTIONS=halt_on_error=1` (#268). Fix it here?
  → A: **Yes, in this feature** (user decision). FR-021a now requires each cell to run with its
  configuration's test-preset environment, read from `CMakePresets.json`, with a forced-miss arm, a
  spurious-hit arm and a mutation in `quickstart.md` § *Step 4*.
- Q: FR-024 (b) justified running the hello gate *"before the conversation"* with *"only the shim is
  running at that point"*, and T021/T023 said *"before the gtest is launched"*. Implementing it showed both
  hold only where fixpp **initiates**: a fixpp **acceptor** must bind before the peer can connect, so its
  gtest is already running when the peer's hello arrives. → A: FR-024 (b) now states the role condition
  instead of the false reason; the gate's substance — shim-side, and a failure is a FAIL, never a skip —
  is unchanged. The sites repeating the old wording point at FR-024 (b).
- Q: Both counterparties read `conversation_script.yaml` only to hash it; the messages they originate are
  hardcoded literals that differ from the script's declared values (B-07's `ClOrdID` is `PRCL-B07-0001` in
  the script). How does the peer originate from the script? → A: **The shim renders the peer's messages
  into a flat intent file both counterparties parse by hand** (user decision) — FR-008d (a), widened to
  fixpp's cell as well, since the library has no YAML parser either. Implementing
  the arm showed nothing could see an originator ignoring the script, on either side, since FR-006
  compares a `sent` record only with the readback of the same frame; FR-008d (b) is that check.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Interop sessions run against the real dictionary (Priority: P1)

A maintainer running any live interop cell gets a session configured with the production FIX 4.4
dictionary on the fixpp side and a dictionary-validating QuickFIX peer on the other side, instead of
a FIX 4.2 single-Heartbeat sentinel and `UseDataDictionary=N`.

**Why this priority**: Every other story depends on it. A "typed readback" from a peer parsing
without a data dictionary is generic field-map access, not typed parsing; and fixpp's inbound
validation cannot be exercised at all against a Heartbeat-only FIX 4.2 dictionary while the cells
carry FIX 4.4 traffic. This story alone also removes a standing misconfiguration.

**Independent Test**: Run **this feature's** `logon-hb-logout` and `NOS->ExecRpt` conversation cells with
the real dictionary on both sides and confirm they pass, with goldens captured and reviewed.

⚠️ **Scope, corrected at Gate A round 1.** This test previously said *"the **existing** cells"*, which
required a peer-side dictionary flip on cells R-5 explicitly declines to touch, and a fixpp-side flip
whose collateral drift R-5's argument applies to with equal force (swapping `c.dictionary` from the FIX
4.2 single-Heartbeat sentinel to real FIX44 changes fixpp's own **inbound** parse for those cells, since
group detection is dictionary-driven on the read path). Both flips are scoped to **the cells this
feature adds** — FR-001 (fixpp side) and FR-002 (peer side) say so — and existing live cells stay as
they are, covered instead by FR-020's regression obligation.

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
   received-count — where *"the values the peer set"* is the peer's own **`sent` record** (FR-003a),
   emitted from the values handed to the peer's builder, **not** a script-declared expectation.
   ⚠️ This is why FR-003a exists: the counterparty generates `OrderID`/`ExecID` at run time
   (`interop_counterparty_main.cpp` — `const int seq = ++id_counter_;` then `"ORD" + …` / `"EXC" + …`),
   so those values are knowable only inside the peer and no declarative script can supply them.
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

⚠️ **The divergence probe is a deliverable, not prose** (Gate A round 1). Agreement between the two arms
is exactly what a **dead** validator produces, and FR-011 does not close that: verifying a production
dictionary is *loaded* establishes presence, not that the validator **ran** or that it **can reject**. A
loaded dictionary with the validator short-circuited satisfies FR-011 and yields identical sets. The
probe is therefore promoted to **FR-010a** and **SC-004a** and enumerated in `quickstart.md` Step 4 —
an Independent Test in a user story is not something `/speckit-tasks` derives from.

**Acceptance Scenarios**:

1. **Given** the conversation runs with validation off and on, **When** both arms complete, **Then**
   the set of accepted messages is identical and the assertion states so explicitly.
2. **Given** the arms diverge, **When** the cell reports, **Then** it names the message and the
   validator finding, because a divergence is a finding in either direction — our dictionary
   rejecting legitimate peer traffic, or failing to reject what it should.
3. **Given** the validation-on arm, **When** it runs, **Then** the **live session** is confirmed to have
   the inbound validator constructed — `Session::has_validator_for_test()`, which is true iff
   `validate_inbound_messages` **and** a non-null dictionary — and that value is asserted equal to the arm
   the row claims (**FR-011a**), so a silently-misconfigured arm cannot read as agreement.
   ⚠️ **A loaded dictionary is NOT this evidence.** Per FR-001/R-5a the production FIX 4.4 dictionary is on
   in **both** arms of every cell this feature adds, so FR-011's predicate is true in the validation-off arm
   too — its population equals its complement. FR-011 is cited here for what it does establish (the cell is
   not running the FIX 4.2 sentinel) and for nothing else.

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
   and a **machine-independent reference to the ledger entry** that corroborates it (FR-014b) — never an
   absolute path into a run directory.
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

- **FR-001**: The live FIX 4.4 interop cells **this feature adds** MUST configure the fixpp session with
  the production FIX 4.4 dictionary rather than the `make_minimal_dictionary()` sentinel. ⚠️ Scope is the
  feature's own cells: flipping `SessionConfig::dictionary` on an **existing** cell changes fixpp's own
  inbound parse for that cell (group detection is dictionary-driven on the read path), which is the same
  class of collateral change R-5 declines to make peer-side. Existing cells are governed by FR-020.
- **FR-002**: QuickFIX counterparty configurations used by **this feature's cells** MUST enable
  data-dictionary validation with the corresponding FIX 4.4 dictionary. Per R-5 this is **per-cell, not
  global**; existing cells' counterparty configs are unchanged.
- **FR-003**: Both counterparty applications MUST emit, for every application message they receive, a
  structured readback record naming **every body field the peer parsed** and the value it decoded for
  each. **The header/trailer exclusion is structural, not a fixed tag list.**

  ⚠️ **What each ENGINE classifies is the PROBLEM STATEMENT, not the rule** — the same demotion
  `contracts/readback-jsonl.md` § *⚠️ The header/body partition is specified HERE, not delegated to the
  engines* already makes, and this requirement previously stated the receiver-specific rule as an **iff**,
  which made the receiving engine's classifier necessary *and sufficient* and contradicted FR-004 sixty
  lines below. On both engines an engine's **own** membership is *its built-in list ∪ dictionary-declared
  header*, computed by its two-argument `isHeaderField` overload —
  `quickfix-cpp/src/C++/Message.cpp`'s `Message::isHeaderField(int field, const DataDictionary *pD)`,
  which returns true on the built-in list and otherwise consults `pD` when one is present, and
  `quickfixj-base/src/main/java/quickfix/Message.java`'s equivalent, which spells the same rule as one
  disjunction over its own built-in list. **The two built-in lists differ** (tag `1156`), so a
  receiver-specific rule yields two different `fields` sets for identical bytes and breaks FR-004.

  ⛔ **The normative inclusion rule is the CANONICAL UNION PARTITION**, defined in
  `contracts/readback-jsonl.md` § *⛔ THE CANONICAL PARTITION — the decision, as a value* and restated at
  FR-004: `header` = (QuickFIX-cpp's built-in list) ∪ (QuickFIX-J's built-in list) ∪ (the fields declared
  in the `<header>` block of the dictionary loaded for that cell); a field in that set is excluded from
  `fields` by **both** emitters, **whichever engine's own classifier would have called it body**. An
  engine's own classification survives only as **captured diagnostic evidence** — useful for explaining a
  divergence, never the inclusion rule. So the partition still **moves when US1 turns the dictionary on**,
  because the `<header>` block is one of the union's operands. Tags `8, 9, 35, 34, 49, 56, 52, 10` are an
  illustrative subset, **not the definition**.
- **FR-003a**: Each side MUST also emit a **`sent` record**, symmetric to the readback record — same
  `fields` grammar, same **format**, same correlation key — whose `fields` are written from the values
  handed to **its own builder**. ⚠️ **It is produced in TWO stages, and the stages MUST be separated**,
  because no application on either side holds both halves at one point in time:

  | Stage | What is captured | Where — **named per side** |
  |---|---|---|
  | 1 — **body intent** | the declared `fields` set, from the builder inputs | at the call site that builds the message, before it is handed to the session |
  | 2 — **header identity** | `seq_num` (`MsgSeqNum(34)`) and `direction` — **nothing else** | *counterparties*: `Application::toApp` / `toAdmin`, which both engines invoke **after** the header is filled and **before** the frame is serialized (QuickFIX-cpp `Session::sendRaw` calls `fill(header)` then `m_application.toApp`; QuickFIX-J `Session.sendRaw` calls `initializeHeader` then `application.toApp`). *fixpp*: the `Application::toApp` callback, whose `MessageView` is built after the complete frame — reading tag `34` from it is header identity, which C-8 permits, and is the only application-visible copy at that instant |

  The two stages are joined by an emitter-local token; the record is finalised and written when stage 2
  supplies the key. ⚠️ **Stage 2 MUST NOT re-read any body field.** Deriving the `fields` set from the
  serialized frame is the C-8 violation FR-018's first spurious-hit arm forces; reading the correlation
  key from the header is not, and C-8 says so explicitly.

  ⚠️ **Receiver-produced readbacks carry NO `script_step_id`.** No step identifier is on the wire and no
  side holds the other's script, so a receiver cannot stamp the sender's step. `script_step_id` is
  **inherited from the paired `sent` record after correlation**. ⚠️ Same format, **not the same file**: each process writes its own stream,
  because both are opened in truncate mode and two processes truncating one path means whichever opens
  second destroys the other's records. The comparator reads both and pairs on
  `(seq_num, direction, occurrence)`. The counterparty emits one for every application message it sends; fixpp
  emits one for every application message it sends (this is the *intent record*, now one half of a
  symmetric pair) **and** a record of what its **typed read tier decoded** for every application message
  it receives. Without this, the peer→fixpp direction has no left-hand side: the peer generates
  `OrderID`/`ExecID` at run time, so no script-declared expectation can substitute, and the comparison
  would degrade either to no witness at all or to fixpp asserted against fixpp — both outcomes this spec
  forbids in writing (Context § *Why fidelity must be proven against an independent implementation*).
- **FR-003b**: The conversation script MUST declare, per message, which fields are read via the peer's
  **typed accessors** and which via generic enumeration, and both MUST be exercised. They are independent
  assertions: a typed accessor is a **schema-conformance** check (it compiles only if the field belongs to
  that message in FIX 4.4, and raises `FieldNotFound` if the peer did not receive it) that generic
  enumeration cannot make; complete generic enumeration detects missing/spurious fields and group shape,
  which typed access cannot.

  ⛔ **The typed tier's anti-vacuity arm is a COMPILE-TIME arm, not a record arm** (user decision, Gate A
  fresh loop round 1 — see § *Clarifications* → *Session 2026-09-10 (Gate A fresh loop, round 1)* for the
  source evidence that no record-level witness can exist).

  - **What is asserted**: for every field the script declares as a typed read, the counterparty source
    MUST reach it through that message's **generated per-message accessor**
    (`<Message>::get(<Field>&)` in QuickFIX-cpp; `<Message>.get(<Field>)` in QuickFIX-J). Because those
    overloads exist only for fields the message declares in FIX 4.4, the **compiler** performs the
    schema-conformance check at build time.
  - **The anti-vacuity arm**: a **negative-compilation** arm. Mutate the counterparty **source file
    itself** — `interop_counterparty_main.cpp` / `InteropCounterparty.java`, or the shared typed-read
    adapter TU the production call site depends on — to call the generated accessor with a field the
    message does **not** declare; the build of **that file** MUST **fail**. ⛔ **Not a standalone snippet**:
    a detached translation unit proves the *pinned engine API* accepts `Symbol` and rejects `LastPx`, and
    proves nothing about the counterparty whose conformance is claimed. The mechanism is named in `plan.md`
    § *External obligations*, and both directions MUST be shown — the unmutated source compiles, the mutant
    does not.
  - ⛔ **The failure MUST MATCH THE EXPECTED MISSING-OVERLOAD DIAGNOSTIC**, not merely be a non-zero exit.
    ⚠️ *A forced MISS cannot catch a spurious HIT*: a typo, a missing include or a wrong namespace also
    makes the mutated build fail, and the arm would report RED for a reason that has nothing to do with the
    schema. A snippet is strictly **worse** on this than the real file, because a snippet missing an
    `#include` fails "correctly" for entirely the wrong reason.
  - ⛔ **THE MATCHER IS A CONDITION PLUS A PER-TOOLCHAIN RECIPE — NOT A LITERAL STRING.** The **condition**
    is: *the diagnostic identifies the call to `get` as having no viable overload, **and** the mutated field
    type is named **somewhere in that diagnostic** — on the error line or in its candidate notes*. Each
    toolchain's exact text is **derived by running the recipe in the arm's execution host**, never copied
    from here, from a review, or from a commit message.
    ⛔ **MATCH THE WHOLE DIAGNOSTIC, NOT ITS FIRST LINE.** The toolchains do not share a shape and do not
    carry the field identity in the same place — clang, today, puts it in the **candidate notes only**, so a
    matcher anchored on the error line alone cannot distinguish a schema failure from any other
    overload-resolution failure on `get`. ⛔ **Where the identity lands is itself DERIVED, not assumed**: the
    derivation must inspect the full diagnostic and record which line carried it.
  - ⛔ **WHICH TOOLCHAINS THE ARM MUST SATISFY FOLLOWS FROM ITS EXECUTION HOST, STATED IN ONE PLACE** —
    `plan.md` § *External obligations* → the typed-accessor compile-arm row, which names the host, the
    build-time derivation of its compilers, and the trigger on which the arm actually runs. ⚠️ **A toolchain
    list is a measurement and rots on every image rebuild; a host is structural.** Do not restate a list
    here, and do not reason about this arm through FR-021's configuration matrix — that matrix maps the
    **fixpp** build, not this one.
  - ⛔ **A PINNED DIAGNOSTIC LITERAL IS UNDER-DETERMINED EVEN FOR ONE COMPILER.** Two free variables, both
    measured on the pinned trees 2026-09-10, neither visible in any literal:
    - **Locale.** g++ quotes identifiers `‘…’` (U+2018/U+2019) under a UTF-8 locale and `'…'` under
      `LC_ALL=C`; clang uses ASCII `'` in both. **Pin the locale in the arm** and derive the pattern under
      that same locale, or match quote-agnostically — otherwise the arm passes on one runner and fails on a
      developer box, for a reason that has nothing to do with the schema.
    - **Receiver constness.** g++ appends ` const` to the printed call signature when the mutated call's
      object expression is const-qualified and omits it when it is not — so the *same compiler* prints two
      different strings for the same mutation, decided by how the call site happens to be written.
      (Identical under `-std=c++17`, `c++20` and `c++23`.)
    ⚠️ These are why a literal transcribed from a previous revision has been wrong three rounds running.
    **Derive under the arm's own pinned locale, against the arm's own mutated call site.**
    ⛔ **AND THE REPAIR THAT MUST NOT BE MADE**: when this arm goes red under a toolchain whose text was not
    anticipated, do **not** loosen the matcher to *"the build failed"*. That reinstates exactly the
    spurious-hit hole this clause exists to close, and it would be invisible. Add the toolchain's row.
  - ⚠️ **Per-site coverage — a STATED LIMIT, not silence.** FR-003b(a) binds **every** field the script
    declares as a typed read; the *positive* direction covers all of them by construction (the whole file
    compiles, so every typed read in it is schema-conformant). The **negative** arm mutates **one**
    `(Message, Field)` site per language. That is sufficient *for what it proves* — that the compiler is
    really the check — because the property is **structural, not per-message**: there is no generic `get`
    overload on `FieldMap`, on `Message`, or on either generated class, so no message can have an escape
    the mutated one lacks. ⚠️ **This argument is engine-pin-bound.** On any re-pin of either vendored
    engine, re-check the inherited overload set, generic fallbacks and field-type conversions before
    relying on one site (`plan.md` § *External obligations*). Parameterizing the mutation over the script's
    typed-read declarations would remove the limit; it is **not** required here.
  - ⚠️ **Scope limit, stated rather than implied**: this binds the counterparty's **source**, not any
    record. It does not prove runtime invocation, and FR-018's spurious-hit obligation for *runtime*
    typed-accessor invocation is recorded as **structurally unsatisfiable** (SC-003).
  - ⚠️ **`fix_type` is NOT accessor provenance and no longer claims to be.** It is retained solely as
    evidence of **dictionary-backed resolution**: a generic enumeration performs no dictionary lookup, so
    an entry with `fix_type` absent or empty shows the dictionary was not consulted. That is a different
    property from *the getter was called*, and conflating them is what rounds 1–3 kept doing.
- **FR-004**: The readback and `sent` record formats MUST be identical across the QuickFIX-cpp and
  QuickFIX-J counterparties, so a single comparator serves both. ⚠️ Format identity is **not free**: the
  two engines' built-in header lists differ (QuickFIX-J's contains `ApplExtID(1156)`, QuickFIX-cpp's does
  not), so for identical bytes carrying tag 1156 one engine would classify it header and the other body.
  The contract **specifies the partition itself** where the built-ins disagree rather than delegating it to
  each engine. **The canonical partition is: `header` = (QuickFIX-cpp's built-in list) ∪ (QuickFIX-J's
  built-in list) ∪ (the fields declared in the `<header>` block of the dictionary loaded for that cell).**
  A field in that set is excluded from `fields` by **both** emitters, whichever engine's own classifier
  would have called it body.

  **Tag 1156 is therefore HEADER and is excluded on both engines** — the disposition, as a value.
  Verified in source: QuickFIX-J's `Message.isHeaderField` switch contains `ApplExtID.FIELD`;
  QuickFIX-cpp's `Message::isHeaderField(int)` switch does not. Union, not intersection, because the
  QuickFIX-cpp emitter closes the gap by *adding* a tag to a static exclusion set — one line — whereas the
  intersection rule would require the QuickFIX-J emitter to *recover* a field its parser has already routed
  into the header `FieldMap` and re-inject it into `fields`, and would have to be re-litigated for every
  future built-in divergence. `ApplExtID(1156)` is additionally not a FIX 4.4 field at all, so under this
  feature's own `UseDataDictionary=Y` cells it would be an undefined tag in the body.

  The contract MUST carry a cross-engine fixture exercising the reconciliation (C-7).
- **FR-005**: Records MUST be keyed on `MsgSeqNum(34)`, the direction the message travelled, and an
  **occurrence ordinal** distinguishing repeated emissions of the same `(seq_num, direction)`. The harness
  MUST NOT inject any correlation field into the traffic — the bytes under test must be exactly the bytes
  the sender would emit in production. ⚠️ `PossDupFlag(43)` is **not** sufficient as the disambiguator, for
  two reasons: it separates an original from a replay but not replay *n* from replay *n+1*; and `L-021-3`
  (live in `spec/behaviors-and-limitations.md`) records that QuickFIX-cpp **strips**
  `PossDupFlag(43)`/`OrigSendingTime(122)`, so on those combos the flag is not on the wire at all. The
  ordinal is assigned by the emitter from its own per-`(seq_num, direction)` counter and costs nothing.

  ⚠️ **The two sides compute the ordinal independently and it never travels on the wire**, so under the
  replay steps FR-008 mandates the counters can legitimately diverge — a receiving engine may not deliver
  a frame the sender counted. Pairing on *the peer's* count would then produce a false RED (an unpaired
  `sent` record) or an off-by-one pairing that compares record *n* against record *n+1*. **The comparator
  MUST therefore reconcile against the DECLARED occurrence values**, not against the peer's count: the
  census (§ *Conversation census*) declares, per `(step, combo)`, exactly which occurrence values are
  expected, and the comparator asserts the observed pairing realises that declared set. An observed count
  differing from the declared count is its own named failure class — **`occurrence_count_mismatch`** —
  distinct from a per-field `missing`, so a lost replay is reported as a lost replay rather than as a
  field-level mismatch. This keeps the no-injection rule intact: nothing new goes on the wire.
- **FR-006**: The harness MUST compare each **readback** record against its paired **sent** record
  (FR-003a) by **exact set equality**, **in both directions using the same comparator**, and MUST fail the
  cell naming the offending field on any of: a value mismatch, a field the sent record declares that the
  receiver did not report, or a field the receiver reported that the sent record does not declare. A subset
  comparison MUST NOT be used — it cannot detect a builder emitting a field it never intended. ⚠️ The sent
  record MUST be derived from the builder inputs and never re-read from the serialized frame; FR-018
  requires an arm that forces that mis-derivation and demands RED, because a sent record read back from the
  frame makes the witness pass by comparing one engine's writer to its own reader.
- **FR-007**: For every business message in the narrow set, the cell MUST assert exact field values in
  the direction the message travelled — including the fixpp-acceptor direction, which today asserts
  only counters.
- **FR-008**: The scripted conversation MUST run within a single session per cell: Logon, the admin
  repertoire (TestRequest/Heartbeat, ResendRequest, SequenceReset-GapFill, Reject), the five business
  messages, Logout.
- **FR-008a**: The conversation script MUST be **declarative down to field values**: for every business
  step it declares the message type, the originator, the intended field set with **concrete values**
  (including discriminating decimal and timestamp values), the expected typed reads (FR-003b), and any
  request/reply dependency on a prior step. A script that declares only `{step, phase, msg_type,
  originator, expects_witness}` is satisfiable by scalar-only minimal messages, which would make three of
  this feature's own mandated witnesses unreachable and would leave the dictionary flip changing nothing
  observable.
- **FR-008b**: The script MUST include **at least one multi-instance nested repeating group** supported by
  both pinned engines. This is what gives `readback-jsonl.md` C-4 (the C++ `m_groups`/`m_fields` trap) and
  C-5 (the QFJ `computeIfAbsent` mutation trap) a subject, what makes data-model §1's
  `dictionary_enabled` rule decidable, and what makes US1's dictionary flip observable at all — both
  engines gate group parsing on a non-null dictionary.
- **FR-008c**: The conversation script MUST be content-addressed, and its **digest MUST be recorded in
  every run's hello record**, so a result can be bound to the exact script that produced it.
- **FR-008d**: Every message the script declares a process originates MUST be built from that message's
  declared `intent_fields`, read at run time — for a counterparty, never from literals in its own source.
  (a) **Delivery to both processes**: the shim renders, for one cell, the messages of every step that applies
  to the cell's combo, in script step order, into a flat per-run intent file — one line per field,
  `step_id` TAB `originator` TAB `msg_type` TAB `path` TAB `value` — which both counterparties and fixpp's
  conversation cell parse with hand-rolled code, each building the messages its own `originator` value
  names, so none of the three carries a YAML parser (the same precedent as FR-025's three hand-rolled JSON
  emitters). ⚠️ A message the **engine** generates in reply — the Heartbeat answering a TestRequest,
  SequenceReset-GapFill, Reject — is not built by application code, so (a) does not apply to it; (b) does,
  and there it checks what the engine produced. It is rendered from the same script bytes whose digest FR-008c records, and
  the shim **refuses to render** a value containing TAB, LF or SOH rather than escaping it. (b) **The check
  that makes (a) observable**: for every `sent` record of either process, each path the script declares
  for that step MUST be present with the declared value; a missing or different one FAILS the cell. Paths
  the script does not declare, and paths it lists as `runtime_generated`, are outside this check — the
  run-time-minted `OrderID`/`ExecID` (FR-003a) among them — and remain covered by FR-006's
  sent-versus-readback comparison. ⚠️ **Without (b), (a) has
  no witness**: FR-006 compares each `sent` record against the *readback* of the same message, and both
  are derived from what the originator actually built, so an originator that ignored the script agrees
  with itself and passes.
- **FR-009**: The conversation MUST run in all four role x flavour combinations, and any combination
  that cannot run a given step or arm MUST be declared with a reason rather than silently skipped. ⚠️ **The
  declaration is an INPUT to the census, not a note beside it**: § *Conversation census* § *Declared
  inapplicable combinations* carries the exemptions with their reasons, and the expansion rule consumes
  them, so an exempted combination contributes **zero** expected witnesses. Without that slot the census
  would over-count and drive the completeness gate permanently RED, and the implementer would drop the
  combination silently — reinstating the silence this requirement exists to remove.
- **FR-010**: Each cell MUST run under two arms — inbound validation off and on — and MUST assert the
  two arms accept an identical set of messages.
- **FR-010a**: The two-arm comparison MUST be exercised by a **divergence probe**: a message the loaded
  dictionary should reject is seeded, and the two arms MUST be shown to **disagree**, with the diagnostic
  naming the message identity **and** the validator's objection (FR-012). ⚠️ **The probe is a separate
  execution and MUST NOT be seeded into the normal conversation.** Seeding it into the script would make
  FR-010's identical-accepted-sets assertion fail on all 32 runs by construction. Required: the run record
  carries **`kind` ∈ {`conformance`, `validator-positive-control`}** and, for a positive control, an
  **`expected_verdict`** (`diverged`); positive-control executions get their own `run_id` and sit
  **outside** the 32-slot run inventory, so they neither satisfy nor disturb FR-021/SC-010 (§ *Conversation
  census*, run inventory).
  ⛔ **AND THE PROBE'S EXISTENCE IS GATED, not merely required** — `contracts/witness-evidence.md` **E-7b**
  obliges the committed `validation_pairs:` section to carry at least one `kind: validator-positive-control`
  pair with `expected_verdict: diverged` and `verdict: diverged`, resolving to two control ledger rows with
  opposite `has_validator`. ⚠️ **Stated because a requirement is not a gate**: E-7a excludes control pairs
  from its equality by design and E-7 is vacuous over a `kind` nothing emitted, so before E-7b an artifact
  with 16 conformance pairs and **zero** control pairs passed every standing check — while data-model §10
  makes the control pair the *precondition of admissibility* for all 16. Without it, SC-004's green state
  is indistinguishable from a validator that never ran — a dead validator produces identical sets by
  construction, and this feature exists partly because the validator is exercised today by exactly one
  interop cell.
  ⛔ **AND THE PAIR'S REFERENCES ARE GATED FOR FRESHNESS, not merely checked once when it is built** —
  `contracts/witness-evidence.md` **E-7c** re-evaluates, in the committed schema check on every CI run,
  that both runs referenced by every `authoritative: true` pair are **still** `authoritative: true`.
  ⚠️ **Stated here for the same reason E-7b is — a requirement is not a gate**, and this one has a
  temporal edge the others do not: `authoritative` is **mutable after a pair is written**, so a retry
  landing later supersedes a referenced run and the construction-time check does not re-run. The scope and
  the normative text are in `contracts/witness-evidence.md` § *Obligations*; this clause is the FR/SC
  anchor, not a second statement of the rule.
- **FR-011**: Both arms MUST verify at run time that a production dictionary is loaded, so that a cell
  running against the FIX 4.2 sentinel is detected. ⚠️ **FR-011 is NOT arm evidence and MUST NOT be cited
  as any**: per FR-001/R-5a the production FIX 4.4 dictionary is loaded on **both** arms of every cell this
  feature adds, so its predicate is true in the validation-off arm too — a check whose population equals its
  complement cannot discriminate the arms it was being offered to protect. It establishes the dictionary is
  *present*, not that the validator *ran*. **FR-011a** is the arm attestation; **FR-010a** shows the
  validator can reject.
- **FR-011a**: Each run MUST carry a **runtime attestation of the arm axis** — the value of fixpp's
  `validate_inbound_messages` **as the live session resolved it**, not as the cell definition declared it —
  and the gate MUST assert it equals the arm the row claims. ⚠️ Without this the entire US4 deliverable can
  go **vacuously green**: US4 settles that the arm axis is fixpp's `validate_inbound_messages`
  (`include/fixpp/session/session_config.hpp:477`), and today the only attestation of which arm ran is the
  `arm` label the harness writes from its own cell definition — the row asserts what the harness *intended*,
  not what the session *did*. A validation-on arm launched with the flag false produces trivially identical
  accepted sets and passes SC-004 and US4 AC-1/AC-3 with nothing able to detect it.

  **The seam exists today and MUST be used rather than invented**: `Session::has_validator_for_test()`
  (`include/fixpp/session/session.hpp`) is a public const accessor returning true iff the inbound validator
  was constructed at `open()` — i.e. iff `validate_inbound_messages == true` **and** the dictionary is
  non-null. That is the live-session state, not the config struct the test filled in, and it is exactly the
  predicate that differs between the two arms (unlike FR-011's). Required: fixpp's stream carries this
  value, the dictionary's content digest, `arm`, `cell_id`, `config` and `run_id`; and an arm forcing a
  validation-on cell to launch with the flag false MUST go RED.
- **FR-012**: Any divergence between the two validation arms MUST be reported as a named finding
  identifying the message and the validator's objection.
- **FR-012a**: The named promotion command (FR-014b, `phase-9-harness/tools/promote_interop_evidence.py`)
  MUST **construct** a **validation-pair** record for every `(combo_id, config)` and **write** it to the
  `validation_pairs:` section of the committed `library/tests/interop/witness_evidence.yaml`
  (data-model §10), carrying the paired-arm identity, each arm's accepted-message set, each arm's validator
  disposition per message, and the cross-arm verdict. The witness `mismatch` vocabulary
  (`value_mismatch` / `missing` / `spurious`) is field-level and cannot express *"the on arm rejected a
  message the off arm accepted, and here is the objection"*.
  - ⛔ **The `kind: conformance` pair set MUST EQUAL the 16-pair inventory** — set equality, not
    containment — checked by the committed schema check (`contracts/witness-evidence.md` **E-7a** / W-3d).
    **16 is derived, never an independent count**: the 32 conformance slots quotiented by the arm axis
    (`cell_id ≡ (combo_id, arm)`) = 4 combos × 4 configs.
  - ⚠️ **This requirement previously read *"a validation-pair entity MUST exist"*** — verbatim the shape
    `plan.md` § *THE COMPLETION RULE* scores **ABSENT**, and it was: four artifacts described the pair and
    nothing produced one, so **zero pairs passed every gate** while SC-004 and FR-010/FR-010a ranged over
    an entity that could legally not exist.
- **FR-013**: Each emitted cell result MUST carry evidence binding it to an actual run: a run
  identifier, a timestamp, the counterparty flavour and version, the counterparty image digest, the
  conversation-script digest, and a **`ledger_ref`** naming the run-ledger entry that corroborates it
  (FR-014b, data-model §11). ⚠️ **Not an absolute `artifact_path`**: the row is committed and the check that
  reads it runs in CI on hosts holding no run artifacts (FR-014).
- **FR-013a**: The identity model MUST separate the **logical cell** from the **run**. A `cell_id ≡
  (combo_id, arm)` names one (role × flavour × validation arm) identity — 8 of them; a `run_id` uniquely
  names one execution. Manifest row identity is **`(cell_id, config)`** — the 32-slot inventory — with
  exactly **one `kind: conformance` run carrying `authoritative: true` per slot** (FR-015c); the committed manifest carries one row per slot and
  no retry rows, so its shipped `id` field is derived as `"<cell_id>@<config>"` and is unique across the 32.
  ⚠️ **`test_ids_unique` therefore does NOT need to be replaced** — an earlier reading of this requirement
  claimed it did, on the assumption that 8 ids had to serve 32 rows. Retry runs are recorded in the run
  ledger (FR-014b), never in the committed manifest, so the shipped assertion keeps a well-defined subject.
  Witness-row identities are the three named in FR-015c.
- **FR-013b**: The `hello` record MUST carry `run_id`, the conversation-script digest, the configuration
  name, and the counterparty image digest, so an evidence row joins **structurally** to the stream it
  claims rather than by a hand-written pointer. ⚠️ A pointer to a **stale** run directory otherwise
  satisfies every evidence field — the same hazard R-4 closes one level down with truncate mode, left open
  at the artifact-pointer level.
- **FR-014**: A `status: pass` MUST be corroborated by a stream that agrees with it, and **the
  corroboration MUST run where the artifacts are**. ⚠️ **The committed manifest and the run ledger are two
  artifacts and MUST NOT be one** — that conflation is unsatisfiable, not merely awkward:
  `tests/interop/cell_results_schema_check_test.py` is a **ctest** (`tests/interop/CMakeLists.txt:459`)
  provisioned in **three CI tiers** (`tier1.yml`, `tier2.yml`, `tier3-libcxx.yml`) on hosted runners that
  have never executed an interop cell and hold **no run artifacts at all**, and it resolves a **committed**
  manifest relative to its own file. So *"the check opens every referenced artifact"* fails on every hosted
  runner, for every 089 row; and extending the global `REQUIRED_FIELDS` unconditionally breaks the
  **pre-existing `status: pass` rows already committed**, colliding head-on with FR-020. Required split:

  | Artifact | Where it is checked | What it asserts |
  |---|---|---|
  | `tests/interop/cell_results.yaml` — **committed expected inventory** | the shipped ctest, in all three CI tiers, **opening nothing** | structure only. New evidence fields are required **conditionally on `kind: conversation`**, so the pre-existing rows are untouched (FR-020). Each `status: pass` conversation row must name a ledger entry that exists, whose `terminal_state` is `completed`, and whose `witness_count` equals the census figure for that slot |
  | the **run ledger** (a `runs:` section of the witness-evidence record, FR-015b) — **committed**, machine-independent | the same ctest | one **`kind: conformance`**, `authoritative: true` entry per `(cell_id, config)`; the set of those slots equals the 32-slot inventory exactly; `run_id`, `run_timestamp`, counterparty flavour/version/digest, `script_digest`, `terminal_state`, `witness_count`, `evidence_digest`, `authoritative`, `kind`. **No absolute path**. ⚠️ `validator-positive-control` and retry rows are **recorded here and excluded from that equality** (data-model §11 § *THE TWO DISCRIMINATORS*) |
  | the **validation pairs** (a `validation_pairs:` section of the same record, FR-012a) — **committed** | the same ctest | ⛔ **NO ROSTER IN THIS CELL** — the pair obligations this ctest evaluates are stated in `contracts/witness-evidence.md` § *Obligations*, their normative home. ⚠️ A roster stood here naming **E-7a** and reference resolution only; it was stale from the commit that added E-7b and E-7c, and an implementer building the ctest from it would have built one gate of three. Deleted rather than corrected |
  | the **run artifact** — machine-local, never committed | the **promotion step** (FR-014b), on the machine that ran the cell | the stream is opened, **each of the run's two processes'** `hello` **and `terminal`** records are read (⚠️ **TWO is the process count, not the emitter count** — `contracts/readback-jsonl.md` § *THE THREE EMITTERS* carries the distinction), their `run_id` / `script_digest` / `config` are checked against each other and against the row, the completeness gate is evaluated, and `evidence_digest` is computed over the persisted bundle |

  ⚠️ Requiring the manifest fields to be merely *present* is not corroboration — six hand-editable strings
  are as easy to type as one. Corroboration is what the promotion step performs; what CI re-checks is the
  machine-independent record of it.
- **FR-014a**: An infrastructure abort MUST have its own terminal state. A run killed by `ENOSPC` is
  recorded as **`error:enospc`** (with `aborted` as the general class), never as `pass`, `skip`, `n/a` or
  `fail`. ⚠️ The status vocabulary is closed by a shipped assertion — `{pass, fail, skip,
  known-limitation, n/a}`, with `n/a` bound to a `deferred:*` disposition and `known-limitation:*` bound to
  a tracking issue — so with no new state the implementer reaches for `fail` and an infrastructure abort
  becomes indistinguishable from a fidelity defect, inside the evidence artifact this feature exists to
  make trustworthy. Artifact finalisation MUST be atomic, and a **`terminal` record MUST be written to BOTH streams —
  the counterparty's and fixpp's — whatever the outcome**, carrying the same join keys as the `hello`
  (`run_id`, `cell_id`, `config`, `script_digest`) plus `terminal_state` and the number of `sent` and
  `readback` records the writer emitted. ⚠️ The record vocabulary is extended to
  **`{hello, sent, readback, terminal}`** for this reason: `hello` is emitted **before any message is
  processed**, so a counterparty that starts, writes its hello and conversates not at all satisfies every
  field a hello-only corroboration inspects. A `pass` requires `terminal_state: completed` on **both**
  streams; a stream with no `terminal` record is an incomplete run, never a passing one.
- **FR-014b**: The promotion step and the evidence location are **named values**, not an obligation to
  name them:

  - **The persisted evidence root is `$FIXPP_INTEROP_EVIDENCE_ROOT`, defaulting to
    `/mnt/wsl/fixppbuild/interop-evidence/`.** That directory is on `/dev/sde` — the separate VHD that
    already holds `CCACHE_DIR`, whose backing file is **not** on the Windows host drive `E:` — so it is
    outside **every** build tree, survives `rm -rf build/<preset>`, and is outside **both** disk predicates
    (`contracts/disk-preflight.md` D-11). Confirm the mount and its free space before a matrix run with
    `df -k /mnt/wsl/fixppbuild`; a reading is not recorded here because it moves.
  - **The promotion command is**
    `python3 phase-9-harness/tools/promote_interop_evidence.py --run-dir <run directory> --evidence-root "$FIXPP_INTEROP_EVIDENCE_ROOT" --ledger library/tests/interop/witness_evidence.yaml --manifest library/tests/interop/cell_results.yaml`.
    It runs **after** a configuration's 8 cells complete and **before** that configuration's build tree is
    reclaimed. It copies the run bundle under the evidence root, performs the corroboration FR-014 assigns
    to it, and writes the ledger entry and the manifest row. Nothing else may write a `status: pass`
    conversation row.
  - **The ledger records `evidence_relpath` (relative to the root) and `evidence_digest`, never an absolute
    path** — the ledger is committed and must stay machine-independent.

  ⚠️ A manifest committed with rows pointing into a reclaimed build tree would fail for an infrastructure
  reason and be indistinguishable from a falsified row — a check right for the wrong reason is not a check.
  The split above is what removes that case: the committed side never opens a path at all.
- **FR-015**: Evidence MUST be emitted per message, per direction, and per role x flavour x validation
  arm — not as a single aggregate verdict — so a catalogue row can cite one named witness.
- **FR-015a**: Results MUST be structured at **two levels**, and the vocabulary MUST be used
  consistently: a **cell** is the *logical* (role × flavour × validation arm) identity the runner
  dispatches on — 4 combos × 2 arms = **8 cells**, named by `cell_id ≡ (combo_id, arm)`; a **run** is one
  *execution* of one cell under one configuration, named by a `run_id`. Under the four-config decision a
  cell is executed **four** times, so *"a cell is one process run"* — the phrasing this requirement
  previously carried — is false and is withdrawn; what it was defending is the **8-valued `cell_id` space**,
  which is unchanged. *Witnesses* (step × direction × occurrence) live in a separate evidence record.
  ⚠️ The shipped `id` field on a `cell_results.yaml` row is **retained and is not `cell_id`**: `id` stays
  the runner's existing per-row identifier and MUST be derived as `"<cell_id>@<config>"`, unique across the
  32 rows, so the shipped `test_ids_unique` assertion keeps a well-defined subject while `(cell_id, config,
  run_id)` carries the identity model.
- **FR-015b**: The witness evidence record MUST carry its own **exact-set completeness gate**, in the
  same spirit as the existing cell-id set-equality check, so that a witness which silently stops being
  produced is detected rather than absent. A missing witness MUST fail that gate.
- **FR-015c**: Witness rows MUST carry `combo_id`, `cell_id`, `config`, `run_id`, `arm`, **`kind`** and **`authoritative`**. ⚠️ The last two were omitted here while identity 3's projection and the 32-slot rules **filter on them** — a required-field list that omits the discriminator its own projection uses. **Three
  distinct identities MUST be named and MUST NOT be conflated** — the previous single "key" was
  unsatisfiable as an equality and blind on the role×flavour axis:

  | # | Identity | Value | What it is for |
  |---|---|---|---|
  | 1 | **Observed-row uniqueness** | `(run_id, cell_id, config, arm, script_step_id, direction, occurrence)` | no two rows in the accumulated record may share it |
  | 2 | **Stable completeness key** | **`(cell_id, script_step_id, direction, occurrence)`** | the identity a witness has independently of which run or which config produced it |
  | 3 | **Cross-config comparison projection** `π` | drop `run_id`, `config` and every payload column from an observed row, keeping identity 2 | the set the equalities in this requirement are taken over |

  `cell_id ≡ (combo_id, arm)`, so identity 2 **retains the role × flavour axis**. That is load-bearing:
  `(arm, script_step_id, direction, occurrence)` — the obvious remainder after dropping `run_id` and
  `config` — collapses all four role×flavour combos into one set, under which a configuration that ran
  **one** of its four combos projects to the same set as one that ran all four. That is FR-015c's own
  config-blindness reproduced one axis over.

  ⚠️ **The equality is across `config`, at fixed everything else** — never across `arm`. `arm` lives
  inside `cell_id`, so a cross-arm equality would be unsatisfiable by construction; cross-arm agreement is
  SC-004's accepted-message-set property over a different entity. Required:

  - **π(`kind: conformance` authoritative rows of config `c`) = π(same, config `c'`)** for every ordered
    pair of the four configs, **and** each equals the **census-declared set of completeness keys** —
    **100** of them, a pointer to § *Conversation census*'s arithmetic rather than an independent count.
    Exact set equality, not containment.
  - **Exactly one `kind: conformance` run with `authoritative: true` per `(cell_id, config)` slot.** Each
    slot designates one run (`authoritative: true`); a retry mints a new `run_id` and its rows are recorded with
    `authoritative: false` and are **excluded from every gate**. Without this, `(cell_id, config, run_id)`
    uniqueness permits unbounded rows per slot, and because π drops `run_id` the duplicates *collapse* —
    a retry becomes invisible, and a failed run and a retried pass can both sit in the record with no rule
    saying which governs. That fails toward green.
  - **The set of `(cell_id, config)` slots carrying a `kind: conformance` authoritative run MUST equal the
    32-slot run inventory exactly** (8 cells × 4 configs) — not be a subset of it. ⚠️ **Scoped**: a
    `validator-positive-control` run occupies no slot (FR-010a, data-model §11), so unscoped it would read
    as a 33rd slot and this equality would be unsatisfiable the moment the divergence probe ran.
  - The gate also runs over the union after the last configuration, as a second reading, never as the only
    one. ⚠️ A union-only gate over rows carrying no `config` is **structurally blind** to the axis
    FR-021/SC-010/SC-011 exist to protect: if one configuration produces **zero** witnesses the union is
    unchanged and exact-set equality passes, so the gate goes green over a matrix that ran a fraction of
    itself. SC-011's comparison of the `tsan` set to the `normal` set is unimplementable without `config`
    on the row.
- **FR-015d**: The expected witness set MUST be checked against a **second, independent declarative
  census** pinned to this spec, and the two MUST be asserted **exactly equal**.

  ⚠️ **The census MUST be counted in the unit of the COMPLETENESS KEY** (FR-015c identity 2 —
  `(cell_id, script_step_id, direction, occurrence)`), or the equality is comparing incomparable things —
  the failure this spec already names for the `path` grammar. So the census is enumerated **over script
  steps**, not over message types, and **`config` is deliberately not one of its axes**: `config` is the
  axis the equality is taken *over*, not an axis the key carries. The expansion rule is in
  § *Conversation census*:

  > for each **business script step** *s*, for each **role×flavour combo applicable to *s***, for each of
  > *s*'s **declared occurrence values for that combo**, for each of 2 validation arms — one expected
  > completeness key. Each config must then realise that same set.

  Counting it as `{D, 8, F, G, 9} × both directions × …` is wrong in **both** directions and neither error
  is hypothetical: it **undercounts**, because two steps carrying the same `msg_type` are distinct
  witnesses and a replayed step yields more than one `occurrence` — FR-008 mandates ResendRequest in the
  admin repertoire, which is precisely why `occurrence` exists; and it **overcounts**, because SC-001 and
  US3 scenario 3 both scope witnesses to *"every **applicable** … combination"* and not every business
  message travels both ways in every combo.

  **The census is the table in § *Conversation census*** — the business step table, its declared
  inapplicable combinations, and the expansion rule, totalling **100 expected completeness keys**. That
  section is the artifact; this requirement points at it and adds nothing to it. It is a second
  **statement of intent**, not a second derivation from the same source; if it were re-derived from the
  script it would agree by construction and would not be a second opinion at all. ⚠️ Script-derivation alone is
  self-consistent under step deletion: one source drives both production and expectation, so deleting a
  business step removes the witness *and* its expectation and the gate stays green over a conversation
  that no longer covers that message. Two sources that must agree cannot both drift silently. The census
  is mutation-tested against step **deletion and addition**. This does **not** revert W-2 to a
  hand-maintained list — the per-run projection stays script-derived; the census is the disagreeing
  second opinion.
- **FR-016**: A counterparty-unavailable outcome MUST remain distinguishable from a pass in both the
  cell result and the evidence record.
- **FR-016a**: Each counterparty MUST announce a readback-protocol version at startup, and the harness
  MUST refuse to run a cell against a peer that announces nothing or announces a version older than the
  cell requires. That refusal MUST be a **failure**, not a skip and not a pass.
- **FR-016b**: The counterparty image MUST be referenced by immutable digest, not by a mutable tag, so
  that the exact counterparty build behind any result is recoverable after the fact.
  ⛔ **A GREEN JOB IS NOT EVIDENCE THAT A CELL RAN.** ⚠️ *Ask what ELSE satisfies "the cell passed"*:
  **the cell not having run**. So wherever a consumer's handling of a cell's outcome lets a cell that did
  not run leave the job green, any result cited from that consumer's run MUST be evidenced as having
  **actually executed**, read from that run's own **step log** and never from the job conclusion.
  ⚠️ **The condition is keyed on the consumer's OUTCOME HANDLING, not on pinning — so no trigger,
  workflow, line or shell behaviour is recorded here.** Earlier revisions each recorded one, and each was
  false at source (Clarifications, 2026-09-11). **Re-derive it per consumer**: walk every step and script
  from resolving the image to the job's verdict, and ask which non-`pass` outcomes — a skip, a missing
  result, an image that will not pull — still let the job exit 0. The consumers are enumerated by recipe
  in `plan.md` § *External obligations*, which carries this obligation per consumer.
- **FR-016c**: A message for which **no** readback record arrived MUST fail its cell. An empty or absent
  readback set MUST NOT satisfy any fidelity comparison — the comparator MUST assert that a record was
  received before comparing its contents.
- **FR-017**: Every new assertion MUST be accompanied by a forced-failure demonstration proving it can
  report RED: a wrong field value, an omitted required field, and a message the peer should reject.
  ⛔ **THE POPULATION THIS CLAUSE RANGES OVER IS CLOSED, AND `quickstart.md` § *Step 4* IS THE
  ENUMERATION** — its forced-miss, spurious-hit and controls tables, together with THE COMPLETION RULE's
  four closed inventories (`plan.md` § *External obligations*). *"Every new assertion"* is an open noun
  phrase and was one: a new assertion is not yet subject to this clause **until it is added to one of
  them**, and adding it there is the same edit that states it. ⚠️ Two rules bind **every** arm in all
  three of Step 4's tables — *an arm is not written until its OBSERVABLE exists*, and *each RED cell
  asserts its OWN diagnostic, never a bare non-zero exit, and is run against the unmutated tree and
  confirmed GREEN there first*. **`quickstart.md` § *Step 4* → § *THE THREE RULES BELOW BIND ALL THREE TABLES* is their normative home** — a named `###` heading, cited by name because six pointers in this bundle once resolved to a section that had no heading; this
  requirement adopts both by reference, with the same force, and does not restate their reasoning —
  three restatements are three fossils.
- **FR-018**: In addition to forced-miss arms, **every guard** MUST carry at least one **spurious-hit**
  arm. A spurious-hit arm is defined as: **an arm that makes the guard report PASS for a reason other than
  the property it claims to measure.** ⚠️ This definition replaces the one this spec previously carried
  (*"deleting the mechanism under test and asserting the witness goes RED"*), which is the **forced-miss**
  recipe — it proves a guard *can* fire and says nothing about what else could satisfy the condition the
  guard is watching. Every arm derived from the old definition inherited the inversion and must be
  re-derived. ⛔ **The arms discharging this clause are the same CLOSED enumeration FR-017 names** —
  `quickstart.md` § *Step 4* — and the two rules FR-017 adopts by reference bind the spurious-hit table
  no differently from the other two.

  ⛔ **ONE declared exception to *"every guard"*, and it is the same one SC-003 names — the typed-accessor
  guard (FR-003b).** Its arm is a **negative-compilation** arm covering **schema conformance**; a
  spurious-hit arm for *runtime* typed-accessor invocation is **structurally unsatisfiable**, because the
  generated getter returns the caller's own object and no serialized value can witness the call
  (§ *Clarifications* → *Session 2026-09-10 (Gate A fresh loop, round 1)*). ⚠️ **An exception with a
  proof, not a waiver.** It is stated here as well as in SC-003 so the FR and the success criterion cannot
  contradict each other.

  The minimum spurious-hit set:

  ⚠️ **An arm is not written until its OBSERVABLE exists.** An arm whose forced defect produces no
  observable difference measures its own setup and stays green — the repository's named class
  `feedback_an_arm_whose_forced_defect_stays_green_measures_its_own_setup`. Each row below therefore names
  **what changes** when the mutation is applied, as a value.

  | Guard | The arm | **The observable — what differs** |
  |---|---|---|
  | Fidelity witness (FR-006) | Derive the `sent` record's `fields` **from the serialized frame** instead of from the builder inputs, **and** apply a post-capture frame mutation so the two derivations can differ at all | A test-only hook in the fixpp interop driver rewrites `Account(1)` from `ACCT0001` to `ACCT0009` in the outbound **`B-03`** frame, **after** stage-1 intent capture and **before** transmission. Three properties make the observable unambiguous: **same length**, with `CheckSum(10)` recomputed and `BodyLength(9)` unchanged, so the frame stays well-formed and the RED is attributable to fidelity rather than framing; `Account(1)` is **dictionary-declared for 35=F** so the validation-on arm accepts it; and nothing on either engine branches on it. ⚠️ **`B-03` rather than `B-01` because `B-03` has exactly ONE declared occurrence on all four combos** — `B-01` carries a replay at occurrence `1` on C3/C4, and whether the replayed frame is the stored pre- or post-mutation bytes is an implementation detail, which would make this arm's expected observable ambiguous. **Correct (builder-derived) implementation ⇒ witness RED with `value_mismatch` on path `1`, sent `ACCT0001`, readback `ACCT0009`. Mis-derived (frame-derived) implementation ⇒ GREEN.** The arm asserts the RED *and* that the mis-derivation yields GREEN — without the second half it is not discriminating. ⚠️ Without the frame mutation an unmutated serializer round-trips to the same field set and the two derivations are indistinguishable; that version of this arm is the defect, not the test |
  | Fidelity witness (typed tier, FR-003b) — ⛔ **a COMPILE-TIME arm, not a record arm** | Mutate the counterparty **source** so a declared typed read calls the generated per-message accessor with a field that message does **not** declare in FIX 4.4 | **The BUILD FAILS.** The generated accessor is overloaded only over the fields the message declares (`FIELD_SET` in QuickFIX-cpp's `FieldMap.h`; one `get` per field in QuickFIX-J's generated class), and there is **no generic `get` overload** on `FieldMap`, `Message` or either generated class to swallow the mutant — verified on both engines, both directions, § *Clarifications* → *Session 2026-09-10 (Gate A fresh loop, round 1)*. ⚠️ **No serialized value can witness this guard** — the getter returns the caller's own object, so `accessor_witness` was deleted rather than re-specified. `fix_type` is retained as evidence of **dictionary-backed resolution** only. ⚠️ **Scope**: this proves the schema-conformance check is really made; it does **not** prove runtime invocation, which is recorded as structurally unsatisfiable (SC-003). |
  | Fidelity witness (FR-016c) | **Empty intent vs empty readback.** Emit a message whose declared intent set is empty | the comparator must **reject** rather than pass on `∅ == ∅` |
  | Script-intent check (FR-008d (b)) | A counterparty that **ignores the intent file** and originates from literals equal to the script's values | only with a copy of the script whose peer-declared value is changed: the peer's `sent` record keeps the old value — against the unmodified script nothing differs |
  | Completeness gate (FR-015b/FR-015c) | **Drop one whole configuration** | the per-config projection π for that config is empty while the other three are the census's 100 keys ⇒ RED. Deleting a single witness row leaves the union unchanged and therefore cannot discriminate |
  | Arm attestation (FR-011a) | Launch a **validation-on** cell with `validate_inbound_messages` forced **false** | fixpp's `hello` carries `has_validator: false` while the row claims `arm: validation-on` ⇒ RED. ⚠️ FR-011's *"a dictionary is loaded"* is true in **both** arms and cannot produce this observable |
  | Level-1 corroboration (FR-014) | A stream carrying a `hello` and **no `terminal` record** | promotion RED — a counterparty that starts, writes its hello and conversates not at all supplies everything a hello-only check inspects |
  | Validation-arm equality (SC-004) | The **empty-arm** mutation — a validator that never runs | FR-010a's divergence probe reports `identical` where `expected_verdict: diverged` ⇒ RED |
  | Disk gate (`proceed`) | Threshold **unset or unparseable** with both mounts nearly full; and, separately, the host reading **falling back** to the build reading (the same mount resolved twice) | `proceed` is true while `required_internal_free`/`required_host_growth` measure nothing, or while the host predicate is satisfied by the build number. Both require RED |

- **FR-018a**: Running the cells against a counterparty build with the readback channel removed MUST turn
  every affected cell RED — zero pass, zero skip. ⚠️ This arm is retained in full but **relabelled**: it
  forces the *absence* of records, which is a forced **miss** and is the demonstration for FR-016a and
  FR-016c. It is not a spurious hit and must not be counted as one.
- **FR-019**: Golden re-captures caused by the dictionary flip MUST be performed per cell with the
  prior golden diffed and the change recorded; a matrix-wide auto-update MUST NOT be used.
- **FR-020**: Existing passing cells outside the narrow business set MUST continue to pass, or any
  change in their status MUST be explained and recorded. ⛔ **THIS IS A MEASUREMENT — RUN THE CELLS AND
  COMPARE — AND AN ARGUMENT IS NOT ONE.** Specifically: reasoning that the counterparty republish is safe
  *because* the new emitter code activates only when `INTEROP_CP_RUN_ID` is set argues from an opt-in
  gate; it reasons about a mechanism and runs **zero** existing cells, so it cannot report the status
  change this clause is written to catch. The inherited work made exactly that substitution once, which
  is why the prohibition is stated here rather than only where the mistake was found. ⚠️ **The producer
  of the measurement is `tasks.md` T099**, which executes it and records the result; no argument, and no
  other task, discharges this requirement.
- **FR-021**: Every cell MUST run under **all four** build configurations — `normal`, `asan`, `ubsan` and
  `tsan` — giving 4 role x flavour x 2 validation arms x 4 configs = **32 runs**, and each MUST emit its
  own result row rather than being folded into another config's row. ⚠️ The configuration names must map
  to presets that actually enable what they claim: `normal` → `linux-clang-debug`, `asan` →
  `linux-clang-asan`, `ubsan` → `linux-clang-ubsan`, `tsan` → `linux-clang-tsan`. The pre-existing
  `asan-ubsan` config name maps to an ASan-only preset (`cmake/Sanitizers.cmake` adds `-fsanitize=address`
  and `-fsanitize=undefined` from **independent** `if()` blocks; the ASan preset sets only
  `FIXPP_ENABLE_ASAN`), so it MUST be retired from this feature's vocabulary rather than reinterpreted.
  ⛔ `linux-clang-asan` is a Tier-1 CI lane; this feature MUST NOT add `FIXPP_ENABLE_UBSAN` to it.
- **FR-021a**: The harness's configuration vocabulary MUST be corrected as part of this feature:
  `run_interop_cell.py`'s `CONFIG_TO_PRESET` gains the four entries above and loses `asan-ubsan`, and the
  schema check's `CONFIGS` set follows. The harness design documents that define the old vocabulary
  (`phase-9-harness/INTEROP-016-DESIGN.md`, `phase-9-harness/INTEROP-COVERAGE-REPORT.md`) MUST be
  corrected in the same change, as is the vocabulary comment in `tests/interop/cell_results.yaml`, which
  is prescriptive rather than a record. ⛔ **THERE IS NO `asan-ubsan` CORPUS, AND AN EARLIER REVISION OF
  THIS CLAUSE INVENTED ONE.** It asserted *"historical `asan-ubsan` rows already in `cell_results.yaml`"*
  and deferred them to *"a filed issue"*. **Measured: that file holds only `config: normal` rows and no
  `asan-ubsan` row at all**, no such issue existed, and a sweep of the parent harness found the retired
  label **only** in prescriptive artifacts. Both halves of the deferral were false — the corpus and the
  issue — so the clause is deleted rather than re-scoped, and nothing is filed.
  ⚠️ **Re-derive, do not trust this sentence**: the rows are inline flow-mappings (`- { id: …, config:
  normal, … }`), so an anchored `^ *config:` pattern returns zero and reads as confirmation. Use
  `grep -o "config: [a-z-]*" tests/interop/cell_results.yaml | sort | uniq -c`.
  ⭐ **What did exist was two overclaims, and they are CORRECTED, not exempted.** `run_interop_cell.py`
  mapped `asan-ubsan` → the **ASan-only** preset, so evidence cells reading *"green under `normal` +
  `asan-ubsan`"* credited UBSan coverage that never ran. The two live sites —
  `spec/behaviors-and-limitations.md` and `spec/feature-catalogue.md` — now state the preset that
  actually ran while keeping the historical label. **A record that was true when written is left alone;
  a record that overclaims is fixed.** See *External obligations* in `plan.md`.
  ⛔ **A SANITIZER FINDING MUST FAIL THE CELL, IN EVERY CONFIGURATION.** Each cell's process MUST run with
  the environment its configuration's **test preset** in `CMakePresets.json` carries — **read from that
  file, never copied**, so the value that makes a sanitizer's finding fatal lives in one place. ⚠️ Why this
  is not already true: UBSan's default mode is recoverable — a finding prints `runtime error:` and the
  process exits 0 — and #268 fixed that for ctest by putting `UBSAN_OPTIONS` in the test preset, which
  only ctest reads. `run_interop_cell.py` launches the gtest binary directly, so without this clause the
  `ubsan` configuration this feature adds could not fail a cell on a UBSan finding: the label-vs-substance
  error this clause exists to remove, reintroduced under a new name. Its arms are in `quickstart.md`
  § *Step 4*.
- **FR-022**: The `tsan` configuration MUST be treated as **bring-up, not a config flip**. TSan has never
  been run on the paired live matrix, so the feature MUST establish that the TSan arm actually executes
  the conversation — a TSan run that skips, aborts during setup, or produces no witnesses MUST NOT be
  recorded as a pass. Any TSan finding is a real defect until disproven; none may be dismissed as a test
  artifact without a reproduction or a client-path analysis.
- **FR-023**: Only fixpp is sanitizer-instrumented; the QuickFIX counterparties run as unmodified
  production binaries. The feature MUST state that bound explicitly wherever a sanitizer result is
  reported, so a clean run is not read as covering the peer.
- **FR-023a**: Each configuration MUST be built as a **targeted build of the interop driver targets only**,
  never `cmake --build <preset>` over `all`. `run_interop_cell.py` builds nothing — it expects a pre-built
  tree and runs one named gtest binary per cell — so the build unit is this feature's choice, and a full
  build produces executables the matrix never opens, while the cells name **four** binaries.
  ⚠️ **No figure is recorded here, deliberately** — a stated disk result rots silently, because nothing
  ever re-runs a sentence, and the earlier wording did not survive its own arithmetic (it subtracted all
  `interop_*` binaries while the matrix opens only the four). **Re-derive** with
  `du -sh build/<preset>/bin`, `du -ch build/<preset>/bin/interop_*` and `du -ch` over the four binaries
  the cells name; the only permitted dated illustration in this bundle is the one enumerated in `plan.md`.
- **FR-023b**: The **second** bound MUST be stated wherever a sanitizer result is reported, alongside
  FR-023's: a targeted build gives sanitizer coverage of **exactly the paths these cells exercise**, which
  is what FR-021 and SC-010 assert and all they assert. It is **not** repo-wide coverage — Tier 1 runs full
  suites under sanitizers separately. ⚠️ Reporting a targeted UBSan pass as repo-wide UBSan coverage would
  repeat, at the reporting layer, the same label-vs-substance error that made the retired `asan-ubsan`
  claim false; this feature exists partly to remove that class of claim, so it must not introduce a new one.
- **FR-024**: The **consumption seam** for the readback stream MUST be specified, not deferred. R-4's
  *"nothing changes"* is true **for collection only**; the consumer has no path today. Required:
  (a) the fixpp-side comparator receives the run's readback path through a new environment variable
  alongside the existing `INTEROP_<TOKEN>_PORT` / `_HOST` / `FIXPP_TLS_FIXTURE_DIR` /
  `FIXPP_FIX44_DICT_XML` that `run_interop_cell.py` already sets on the gtest; (b) the FR-016a hello gate
  runs **shim-side, before the conversation**. ⚠️ *Before the conversation* means **before the gtest is
  launched** only where fixpp **initiates** (the peer is listening first). Where fixpp is the **acceptor**
  it must bind before the peer can connect, so its gtest is already running when the peer's hello
  arrives: the gate then runs as soon as the counterparty is launched, and a failed gate still FAILS the
  cell. In neither role may the gate move gtest-side.
  ⚠️ The hello-gate failure MUST NOT be phrased in the `unavailable:` vocabulary: `parse_gtest_status`
  greps `unavailable: .*` out of gtest stdout and returns `skip:<reason>`, so reusing the idiom already in
  that file silently converts a **stale peer failure** into a skip — precisely the collapse FR-016a exists
  to prevent, reached by imitation rather than carelessness.
- **FR-025**: The readback record format MUST state a **single decision** for non-UTF-8 bytes and **all
  three emitters** MUST implement it, together with a canonical key order and a canonical escaping form.
  The topology is **three implementations in two languages** — QuickFIX/C++ counterparty, fixpp, and
  QuickFIX/J counterparty — with different string models (Java `String` is UTF-16; C++ `std::string` is
  bytes), so an unstated decision diverges and breaks FR-004. ⚠️ **It is 2 C++ : 1 Java, and that is why
  the fixture is three-way rather than two-way**: fixpp's emitter can silently diverge from the
  QuickFIX-cpp counterparty's *despite sharing `std::string`*, so "one per language" would leave fixpp's
  byte-level form unguarded. ⛔ **Do not "fix" the PROCESS count to three** — a run has **two** processes
  (`contracts/readback-jsonl.md` § *THE THREE EMITTERS*, which carries the distinction as a table at its
  head; `checklists/requirements.md` § *Re-validation — 2026-09-10, after Gate A round 3 + the
  post-exhaustion hand-edit + fresh loop round 1* records it explicitly as **not** to be "fixed"); it is
  the **emitter** count that is three. A **cross-language golden
  fixture** — one consumer, **all three emitters** (the QuickFIX-cpp counterparty, the QuickFIX-J
  counterparty, and fixpp), one committed expected artifact — MUST exist; C-7's byte-compatibility clause
  is otherwise a sentence with nothing behind it. ⛔ **Three-way, not two-way**: FR-006 compares parsed
  field *sets* and is blind to sort order and escaping, so C-7 is the **only** guard on fixpp's byte-level
  canonical form (`contracts/readback-jsonl.md` § *THE THREE EMITTERS*).
- **FR-026**: The counterparty republish MUST be ordered **publish → verify → depend**: the new image is
  published, FR-020's regression run is executed against it, and only then may anything be pinned to it or
  built on it. ⛔ **NOTHING IS PINNED TO THE OLD IMAGE.** Both consumers
  (`.github/workflows/interop-smoke.yml`, the parent's `.github/workflows/interop-matrix.yml`) keep naming
  `:latest` and therefore pull the new image as soon as it is published — which is **required**, not
  tolerated: FR-020 asks whether the existing cells still pass **against the new counterparty**, and they
  cannot exercise it while pinned away from it.
  ⭐ **USER DECISION 2026-09-10, and it REPLACES an earlier two-option formulation that was wrong in both
  branches.** That version offered (1) pin every existing consumer to the pre-089 digest before
  republishing, or (2) publish under a new tag and move `:latest` only after FR-020 is green, and called
  the first *"the safer shape [costing] one line"*. Both were rejected on measurement:
  - ⛔ **Option 1 obstructs the verification it exists to enable.** Pinning the existing cells to the
    pre-089 digest means nothing exercises the new image, so **FR-020 cannot be discharged** through the
    normal path; the pin would have to be lifted to test, restoring the exposure it deferred. It also
    cost far more than "one line" — two pins **plus** an override mechanism for 089's own cells, which was
    never designed and was recorded as an open obligation.
  - ⛔ **Option 2 traded a loud exposure for a silent one.** Deferring the `:latest` move leaves it stale
    until someone performs a step nothing forces — the *spent valve nobody refills* hazard this bundle
    already names for the disk reserve, and it fails quietly.
  - ⭐ **Both were remedies for a risk whose population was EMPTY**: measured 2026-09-10, **zero open pull
    requests in either repository**. The in-flight-PR reachability finding is real — both consumers
    trigger on `pull_request` with `branches: ["**"]`, so a pin merged to a default branch never reaches a
    PR resolving the image against its own base — but it describes a hazard that only exists when work is
    in flight.
  ⚠️ **THE RESIDUAL IS A SEQUENCING RULE, NOT A COUNT.** "Zero open PRs" is a fact about one moment and
  re-arms the moment someone opens one. The obligation is therefore stated as a condition: **no pull
  request touching the counterparty-image consumer paths may be opened between the publish and FR-020
  reporting green.** ⛔ Do not restate the count; re-derive it (`gh pr list --state open`) at the moment of
  publishing.
  ⛔ **AND THE ROLLBACK IS PART OF THE ORDER, NOT AN IMPROVISATION.** If FR-020 goes red against the new
  image, `:latest` is re-tagged to the pre-089 digest **before** anything else is attempted; GHCR retains
  superseded versions, so the old digest remains addressable. A recovery decided under pressure is when
  the wrong version gets deleted.

### Key Entities

- **Combo**: one role × flavour pairing, named by a `combo_id` ∈ {`C1`, `C2`, `C3`, `C4`}
  (§ *Conversation census*). Four of them.
- **Cell**: one **logical** (role × flavour × validation arm) identity, named by `cell_id ≡ (combo_id,
  arm)`. **Eight logical cells.** A cell is *not* a process run — it is executed once per configuration.
- **Run**: one **execution** of one cell under one configuration, named by a unique `run_id`. The join key
  binding an evidence row to the stream that produced it. Exactly one **`kind: conformance`** run per
  `(cell_id, config)` slot is **authoritative**; the 32 slots are the conformance run inventory. A
  `validator-positive-control` run is authoritative too but occupies **no** slot.
- **Run ledger**: the committed, machine-independent record of **every** run — one `kind: conformance`
  authoritative entry per slot, plus the retry rows (`authoritative: false`) and the control rows
  (`kind: validator-positive-control`, no slot) — carrying identity, `terminal_state`, `witness_count`,
  `evidence_relpath` and `evidence_digest`, and **no absolute path**. It is the bridge FR-014b names between a machine-local artifact and a CI-checkable row.
- **Witness**: one (step × direction × occurrence) fidelity result inside a run; the unit a catalogue row
  cites. Its three identities are named in FR-015c; the **completeness key** is `(cell_id, script_step_id,
  direction, occurrence)`.
- **Readback record**: an engine's report of what it **parsed** — a message identity plus the
  **complete** set of body field/value pairs it decoded.
- **Sent record**: the symmetric counterpart — the field set an engine **intended to send**, derived from
  the values handed to its builder and never re-read from the serialized frame, finalised in two stages
  (FR-003a) so the header identity can be attached from the outbound seam once `MsgSeqNum(34)` is assigned.
  Emitted by both sides, in the same grammar as the readback record and into its own file. fixpp's outbound sent record is what was previously
  called the *intent record*; the peer's sent record is what makes the peer→fixpp direction comparable at
  all.
- **Validation pair**: the two arms of one (role × flavour × config) combination, carrying each arm's
  accepted-message set, each arm's per-message validator disposition, and the cross-arm verdict.
  **Constructed by the promotion command and written to `validation_pairs:` in the committed
  `witness_evidence.yaml`** (FR-012a, data-model §10). There are **16** conformance pairs — the 32 slots
  quotiented by the arm axis — plus any control pairs.
- **Evidence row**: the per-message, per-direction, per-combination, **per-configuration** result a
  catalogue row cites, bound to a specific run.
- **Terminal record**: the last line of each stream, carrying the `hello`'s join keys plus
  `terminal_state` and the counts of records emitted. It is what makes a `pass` corroborable — a `hello` is
  written before any message is processed and so cannot attest that a conversation happened.
- **Conversation script**: the ordered, **declarative** message sequence a cell drives within one session
  — message types, originators, concrete field values, expected typed reads, and step dependencies —
  content-addressed by a digest pinned into every run.

## Conversation census — the declarative expected-witness population (FR-015d)

⚠️ **This section IS the census.** FR-015d requires *"a second, independent declarative census"*; the
tables below are that census, stated as values. They are **not** derived from the conversation script
file — they are the statement of intent the script is later checked against, which is the only thing that
makes the equality a second opinion rather than a tautology.

⚠️ **A hand-written table is normally the rot class this repository fights.** It is admissible *here, and
only here*, because it is never read on its own: SC-009a asserts it **exactly equal** to the script-derived
set on every run, in both directions (step deletion and step addition). A drift in either source is a RED,
not a stale sentence. Deleting the table does not make the gate lenient — it makes it fail.

### Role × flavour combinations

| id | fixpp role | counterparty |
|---|---|---|
| `C1` | initiator | QuickFIX-cpp |
| `C2` | acceptor | QuickFIX-cpp |
| `C3` | initiator | QuickFIX-J |
| `C4` | acceptor | QuickFIX-J |

`combo_id` ∈ {C1, C2, C3, C4}; `arm` ∈ {`validation-off`, `validation-on`}; `cell_id` ≡ `(combo_id, arm)`
— **8** logical cells (FR-013a, FR-015a). `config` ∈ {`normal`, `asan`, `ubsan`, `tsan`}.

### Business steps — every step that carries a witness

Direction is written from fixpp's point of view, and these two strings are the enum's **wire values**, not
a shorthand for the arrows: `→` = `fixpp-to-peer`, `←` = `peer-to-fixpp`. The value is **absolute** — it
does not depend on which process emits the record, so every emitter writes the same value for the same
message.

| `step_id` | `msg_type` | originator | `direction` | applicable combos | declared `occurrence` values | `depends_on` |
|---|---|---|---|---|---|---|
| `B-01` | `D` NewOrderSingle | fixpp | → | C1 C2 C3 C4 | `0` on C1 C2 · **`0,1`** on C3 C4 | — |
| `B-02` | `8` ExecutionReport *(New ack)* | peer | ← | C1 C2 C3 C4 | `0` | `B-01` |
| `B-03` | `F` OrderCancelRequest | fixpp | → | C1 C2 C3 C4 | `0` | `B-02` |
| `B-04` | `9` OrderCancelReject | peer | ← | C1 C2 C3 C4 | `0` | `B-03` |
| `B-05` | `G` OrderCancelReplaceRequest | fixpp | → | C1 C2 C3 C4 | `0` | `B-04` |
| `B-06` | `8` ExecutionReport *(Replaced)* | peer | ← | C1 C2 C3 C4 | `0` | `B-05` |
| `B-07` | `D` NewOrderSingle | peer | ← | C1 C2 C3 C4 | `0` | — |
| `B-08` | `8` ExecutionReport *(New ack)* | fixpp | → | C1 C2 C3 C4 | `0` | `B-07` |
| `B-09` | `F` OrderCancelRequest | peer | ← | C1 C2 C3 C4 | `0` | `B-08` |
| `B-10` | `9` OrderCancelReject | fixpp | → | C1 C2 C3 C4 | `0` | `B-09` |
| `B-11` | `G` OrderCancelReplaceRequest | peer | ← | C1 C2 C3 C4 | `0` | `B-10` |
| `B-12` | `8` ExecutionReport *(Replaced)* | fixpp | → | C1 C2 C3 C4 | `0` | `B-11` |

Each of the five business message types therefore carries a witness in **both** directions
(`D`: B-01/B-07 · `8`: B-02, B-06/B-08, B-12 · `F`: B-03/B-09 · `G`: B-05/B-11 · `9`: B-04/B-10), which is
what SC-001 and SC-002 assert. `8` deliberately appears as **three distinct steps per direction pair** —
that is why the census is enumerated over **steps** and not over message types (FR-015d): a per-`msg_type`
census would count `8` once and undercount by four.

**`B-01` occurrence `1` is the replay.** After `B-08` the peer issues a ResendRequest covering `B-01`'s
`MsgSeqNum`; fixpp replays the stored NewOrderSingle with `PossDupFlag(43)=Y` at the **same** `seq_num`, so
it is occurrence `1` of the same `(seq_num, direction)` — not a new step. This is the case `occurrence`
exists for and the reason FR-008 mandates ResendRequest inside the conversation.

### Declared inapplicable combinations (FR-009) — with reasons, not silence

| What | Combos | Reason |
|---|---|---|
| `B-01` occurrence `1` (the fixpp-side app replay) | **not applicable to C1, C2** | The replay needs a counterparty that will rewind its expected-target sequence number and issue a ResendRequest for an already-delivered application message. That induction seam exists only in the QuickFIX-J counterparty (`INTEROP_CP_RESEND_APP`, the `HP-QFj-*-recovery-outbound` cells, which are QF-J-only for exactly this reason). QuickFIX-cpp *"never resends an already-seen frame"* and strips `PossDupFlag(43)`/`OrigSendingTime(122)` — recorded as `L-021-3` and as the shipped `deferred:qfcpp-no-possdup-injection` disposition |
| peer-originated replay (any step) | **not applicable to any combo** | Out of scope for this feature. On QuickFIX-cpp it is impossible (`L-021-3`); on QuickFIX-J it is the `PD-QFj-*` cells' subject, not this conversation's |

⚠️ **A declared inapplicability is an INPUT to the census, not a note beside it.** The expansion below
consumes the *applicable combos* and *declared occurrence values* columns directly, so an exempted
combination contributes **zero** expected witnesses rather than making the census over-count and driving
the completeness gate permanently RED. Silently dropping it instead would reinstate exactly the silence
FR-009 exists to remove.

### Admin steps — mandated by FR-008, carrying no witness

These are part of the script's step inventory (so a step deletion or addition is defined over the whole
script) but `expects_witness: false`: they assert session behaviour, not field fidelity.

| `step_id` | exchange | applicable combos |
|---|---|---|
| `A-LOGON` | Logon / Logon | C1 C2 C3 C4 |
| `A-TESTREQ` | TestRequest → Heartbeat | C1 C2 C3 C4 |
| `A-RESEND` | ResendRequest covering `B-01` (drives `B-01` occurrence `1`) | C3 C4 |
| `A-GAPFILL` | SequenceReset-GapFill | C1 C2 C3 C4 |
| `A-REJECT` | Reject (35=3) | C1 C2 C3 C4 |
| `A-LOGOUT` | Logout / Logout | C1 C2 C3 C4 |

### The expansion rule — one census row to N completeness keys

For each business step *s*, for each combo *c* in *s*'s **applicable combos**, for each *o* in *s*'s
**declared occurrence values for that combo**, for each `arm` ∈ {`validation-off`, `validation-on`}:

> one expected witness with completeness key **`(cell_id, script_step_id, direction, occurrence)`**, where
> `cell_id = (c, arm)`, `script_step_id = s`, `direction` = *s*'s direction, `occurrence` = *o*.

That is the **completeness key** of FR-015c — it carries the role × flavour axis inside `cell_id` and it
carries **no** `run_id` and **no** `config`, which is what makes the cross-config equality satisfiable at
all (see FR-015c).

**Arithmetic, stated once, here only:**

| | per `cell_id` | count |
|---|---|---|
| C1/C2 cells (QuickFIX-cpp, 4 of them = 2 combos × 2 arms) | 12 steps × 1 occurrence | 12 each → **48** |
| C3/C4 cells (QuickFIX-J, 4 of them) | 12 steps + `B-01` occurrence `1` | 13 each → **52** |
| **Census total — expected completeness keys** | | **100** |
| Run inventory — `(cell_id, config)` slots | 8 cells × 4 configs | **32** |
| Witness rows across the whole matrix | 100 × 4 configs | **400** |

⚠️ **These numbers are census outputs, not remembered figures**, and they are asserted rather than trusted:
SC-009a compares 100 against the script-derived projection every run, and FR-015c compares the 32-slot
inventory against the `kind: conformance` runs that actually produced authoritative rows. **The arithmetic is derived once,
here.** Where `100` or `32` appears elsewhere (FR-015c, `witness-evidence.md` W-2a, `quickstart.md`) it is a
pointer back to this section, never an independent count — and each of those places says so.

### Field content the census presumes

The census counts witnesses; FR-008a and FR-008b govern what each step carries. Two content decisions are
load-bearing for the anti-vacuity arms and are fixed here so the arms have a subject:

- **`B-01` carries `NoPartyIDs(453)` with at least two instances, each carrying a nested
  `NoPartySubIDs(802)` instance** — the multi-instance nested repeating group FR-008b requires. It is what
  gives `readback-jsonl.md` C-4 and C-5 a subject and what makes US1's dictionary flip observable (both
  engines gate group parsing on a non-null dictionary).
- **`B-03` declares `Account(1) = ACCT0001`.** That is the value FR-018's frame-derivation arm mutates
  (to `ACCT0009` — same length, dictionary-declared for 35=F, and nothing branches on it). `B-03` is chosen
  because it has exactly **one** declared occurrence on all four combos, unlike `B-01`.
- ⚠️ **No seed value is declared for the typed-accessor arm, and no seed value CAN be.** That arm is a
  **compile-time** arm over the counterparty source (FR-003b, FR-018): its observable is a build failure,
  not a field. No message content participates in it. Two seed-based observables were tried and rejected —
  a decimal spelling (fixpp's decimal writer strips trailing fractional zeros, `src/core/decimal.cpp`,
  `to_chars`, AC-S4, so both paths read `100.1` and the arm was inert) and `accessor_witness` (the getter
  returns the caller's own object, so the value is synthesizable) — see § *Clarifications* →
  *Session 2026-09-10 (Gate A fresh loop, round 1)*.
- **`B-05` declares `EncodedTextLen(354)`/`EncodedText(355)` with a value containing the byte `0xff`.**
  That is C-11's live-path charset arm (`contracts/readback-jsonl.md` § *C-11*). Both tags are
  dictionary-declared **body** fields on all five in-scope message types in `FIX44.xml` and are not in its
  `<header>` block, so validation-on accepts them and C-6 does not exclude them. `B-05` is
  fixpp-originated with exactly **one** declared occurrence on all four combos, so census cardinality is
  unchanged and no completeness key is added.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For all five business message types, in both directions, in every applicable role x
  flavour combination, a named witness asserts exact field values — with **zero** message/direction
  pairs resting on a received-count or a session-stayed-up assertion.
- **SC-002**: The fixpp-acceptor direction, which today has no field-value assertion, has one for
  every business message in the set.
- **SC-003**: Every new assertion has a recorded forced-failure demonstration showing it reporting
  RED, and **every guard** carries at least one **spurious-hit** demonstration — *an arm that makes the
  guard report PASS for a reason other than the property it claims to measure* — per FR-018's table. A
  forced-miss arm does not satisfy this criterion.
  ⛔ **ONE declared exception, scoped and named — the typed-accessor guard (FR-003b).** Its spurious-hit
  arm is a **negative-compilation** arm rather than a record arm, and that arm covers **schema
  conformance** only. A spurious-hit demonstration for *runtime* typed-accessor invocation is recorded as
  **structurally unsatisfiable** — no value serialized into the readback stream can witness invocation,
  proven from the generated getters in both vendored engines (§ *Clarifications* →
  *Session 2026-09-10 (Gate A fresh loop, round 1)*). ⚠️ This is an **exception with a proof**, not a
  waiver pending a better idea; do not re-open it by proposing a fourth artifact-level observable.
- **SC-004**: Both validation arms run for every cell, and their accepted-message sets are asserted
  identical; any divergence is reported as a named finding rather than absorbed.
- **SC-004a**: The divergence probe (FR-010a) is demonstrated: a message the dictionary should reject
  makes the two arms **disagree**, with the diagnostic naming the message and the validator's objection.
  A validator that never runs is thereby shown to be detectable.
- **SC-005**: A cell result claiming a pass without run evidence is rejected by the schema check —
  demonstrated by a deliberately falsified row.
- **SC-005a**: Running the cells against a counterparty build with the readback channel removed turns
  every affected cell RED — demonstrated, not argued. Zero cells pass or skip in that configuration.
  ⚠️ **Relabelled at Gate A round 1**: this is a forced-**miss** demonstration (it forces the *absence* of
  records) for FR-016a and FR-016c, not a spurious hit. The obligation is unchanged; SC-003 carries the
  spurious-hit criterion separately.
- **SC-006**: Every cell in the feature's set runs against a production FIX 4.4 dictionary on both the
  fixpp side and the peer side; no cell in the set uses the single-Heartbeat FIX 4.2 sentinel.
- **SC-007**: The evidence output is sufficient for row 4c to cite a specific named witness per
  catalogue row, demonstrated by producing the citation end to end for at least one row. ⚠️ The narrow
  message set maps to `A-001`, `A-003`, `A-004`, `A-006`, `A-007`, all of which are **already `done`** —
  so this feature closes **zero** tail rows by itself and the demonstration is of the citation
  mechanism, not of a status flip. The 10 QuickFIX-route tail rows close in the breadth follow-on.
- **SC-009**: The witness completeness gate fails when a witness stops being produced — demonstrated by
  deleting one witness and observing the gate go RED, **and** by dropping one whole configuration and
  observing the gate go RED. The second demonstration is the discriminating one: a single-row deletion
  passes under a union-only gate, so it cannot tell a correct gate from a config-blind one.
- **SC-009a**: The declarative census in § *Conversation census* (FR-015d) and the script-derived
  expected set are asserted **exactly equal** — 100 completeness keys — and the assertion is shown RED under
  both a script **step deletion** and a script **step addition**.
- **SC-009b**: Exactly one `kind: conformance` run with `authoritative: true` exists per
  `(cell_id, config)` slot, the set of slots carrying one equals the 32-slot inventory exactly, and a
  second (retry) run for a slot is shown **not** to multiply witness rows in any gate. ⚠️ A
  `validator-positive-control` run is recorded in the ledger, occupies **no** slot, and is shown not to
  perturb this equality.
- **SC-009d**: The `validation_pairs:` section of the committed record carries **exactly** the 16
  conformance pairs, demonstrated RED by an **empty** section and by a **15-of-16** section (E-7a). Without
  it, SC-004 and FR-010/FR-010a range over an entity whose existence nothing checks. ⛔ **The claim that
  stood here — that an implementation emitting no pairs at all *"satisfies every other gate"* — is
  DELETED, not corrected**: E-7b falsified it, and any replacement enumeration is falsified by the next
  obligation over this entity. The demonstrations are unchanged; what is removed is a count of what stays
  green, which is a property of the obligation set at the moment of writing. `contracts/witness-evidence.md`
  § *Proof obligations for Level 1* carries each fixture with its co-fires named.
- **SC-009c**: Each run's arm attestation (FR-011a) equals the arm its row claims, demonstrated RED by
  launching a validation-on cell with `validate_inbound_messages` forced false.
- **SC-010**: All 32 runs (8 cells x 4 configs) complete and emit their own result row, with **zero**
  configurations silently folded into another's row.
- **SC-011**: Each sanitizer arm is shown to have genuinely executed the conversation — every
  configuration produces the **same witness set** as the `normal` arm, not an empty one, asserted by the
  cross-config completeness gate (FR-015c) — each configuration's projection asserted equal to every other
  configuration's and to the census's 100 keys. A configuration yielding no witnesses is a failure.
  ⚠️ For `tsan` this is bring-up (FR-022); for `ubsan` it is the arm whose absence made the previous
  Article IX §2 claim false.
- **SC-008**: All cells outside the feature's set retain their prior status, or each change is
  explained in the feature's records.

## Assumptions

- The GHCR counterparty image (`ghcr.io/catalinserafimescu/fixpp-interop-counterparties`) remains the
  distribution vehicle. It carries QuickFIX-cpp **v1.16.0** and QuickFIX-J **3.0.1**; it was last
  published 2026-06-19 and **must be rebuilt and republished**, because this feature changes both
  counterparty applications. The existing reference is the mutable tag `:latest` — the `IMAGE:` key in
  `.github/workflows/interop-smoke.yml`; FR-016b replaces it with a digest **for this feature's cells**,
  and FR-026 governs the blast radius for everything the republish touches that is *not* pinned.
- QuickFIX-J's shaded counterparty jar is present in that image but is **not currently extracted** by
  `interop-smoke.yml`; any CI reach for QuickFIX-J cells requires extracting it.
- The narrow business set (35=D, 8, F, G, 9) is supported by both QuickFIX flavours at their pinned
  versions and needs no peer-side message-definition work.
- FIX Latest is excluded — there is no QuickFIX peer for it. Rows `A-035..A-065` close by differential
  verification against the 181-message baseline under row 4b.
- The typed builders for the narrow set already exist and are correct; this feature verifies fidelity,
  it does not add or change builders.
- Cells continue to run over TLS `one_way_ca`; mutual mTLS stays `deferred:v1.1-mtls`.
- The **four-config** decision (user, 2026-09-10, Gate A round 1) supersedes both the recommendation to
  defer TSan and the earlier three-config answer. The cost is accepted deliberately: **32 runs** instead of
  16, with a first-ever TSan bring-up on the paired live matrix inside this feature's scope. If that
  bring-up proves to be its own investigation, it is escalated as a filed issue rather than quietly
  downgraded to `n/a`. The fourth configuration (`ubsan`) is not a new build kind — `linux-clang-ubsan`
  already exists as a preset — so the increment is run time and sequencing, not a new toolchain.
  ⚠️ **Correction — the CONDITION, with every figure DELETED.** An earlier draft justified this by
  calling `ubsan` *"the cheapest of the four to keep resident"* on the strength of the size of its build
  directory. **A directory's current size is not a build's cost**: that tree is essentially unpopulated,
  so its size measures what has not been built rather than what will be. What makes the fourth
  configuration affordable is the **targeted build unit** (`plan.md` § *The build unit is TARGETED*).
  ⚠️ **Re-derive, never cite a figure from this bundle**: `du -sh build/*/` for occupancy and
  `plan.md` § *Disk preflight*'s recipe for the two ceilings. Every disk figure this bundle once carried
  was false within the same day it was written, which is why the numbers are deleted rather than
  corrected.
- Sanitizer instrumentation covers fixpp only; the counterparties are unmodified production binaries
  (`tests/interop/KNOWN-LIMITATIONS.md:108-115`). A clean sanitizer run bounds fixpp, not the peer.
- The parent harness at `research/G19-fix-fpml-iso20022/phase-9-harness/` is git-tracked in the parent
  repository, so counterparty changes span two repositories — the library submodule and the parent.

## Normative References

*Added at Gate A round 1. Its absence was a direct `[const §VI.5]` violation, and the same violation was
corrected at this same gate on **085, 086, 087 and 088** — five bundles running. That pattern, not the
missing lines, is the finding.*

Per `[const §VI.5]` (`.specify/constitution.md`, Article VI §5) — *"Every `/specify` artifact must include
a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage
index that inform the spec."* The clause is unconditional and it is a **presence** obligation.

⚠️ **089 cannot discharge it the way 086 and 087 did.** Those recorded the FIX-normative set as *empty* —
legitimately, since a CMake include-directory property engages no FIX section. This feature's SC-007 names
five catalogue rows and every one of them carries a canonical reference, so the set here is **non-empty**.

**Business messages** — the narrow set (35=D, 8, F, G, 9), mapped to catalogue rows by
`spec/coverage-index.md` § *Application Messages — Trade (FIX 4.0+)* and carried on the rows themselves in
`spec/feature-catalogue.md`:

- **`[FIX50SP2] Single General Order Handling`** — catalogue rows **A-001** (NewOrderSingle 35=D),
  **A-003** (OrderCancelRequest 35=F), **A-004** (OrderCancelReplaceRequest 35=G), **A-006**
  (ExecutionReport 35=8), **A-007** (OrderCancelReject 35=9). One canonical entry covers all five; it is
  the reference each of those five `spec/feature-catalogue.md` rows already records. These are the rows
  SC-007 demonstrates the citation mechanism against — **all already `done`**, so this feature flips none
  of them.

**Repeating-group structure** — the section FR-008b's mandated multi-instance nested group engages, and the
one that makes US1's dictionary flip observable at all (both engines gate group parsing on a non-null
dictionary):

- **`[FIX50SP2 §3.2] Repeating groups (NoXxx delimiter, ordered field list, nested groups)`** —
  `spec/coverage-index.md:184`, catalogue rows **W-006 / W-007 / D-010**. The *nested groups* clause is why
  FR-008b requires a **nested** instance rather than a flat one, and it is what gives the readback
  contract's C-4 (the C++ `m_groups`/`m_fields` trap) and C-5 (the QFJ `computeIfAbsent` trap) a subject.
  089 adds no behaviour to this row; it produces interop evidence for it.

**Admin repertoire** — the session-level sections the scripted conversation exercises within one session
(FR-008), each an entry in `spec/coverage-index.md`'s FIX-SL §4 table:

- **`[FIX-SL §4.3] Establishing a FIX connection`** — the Logon step. Covered by **S-001, S-015, S-021,
  S-022**.
- **`[FIX-SL §4.5.1] FIX connection keep-alive (heartbeat)`** — the Heartbeat exchange. Covered by
  **S-003, S-004**.
- **`[FIX-SL §4.5.5] Test Request processing`** — the TestRequest/Heartbeat step. Covered by **S-004,
  S-003**.
- **`[FIX-SL §4.8.2] Request retransmission of messages (ResendRequest)`** — the ResendRequest step.
  Covered by **S-005, S-024, S-041**.
- **`[FIX-SL §4.8.5] Gap fill process (SequenceReset-GapFill)`** — the SequenceReset-GapFill step. Covered
  by **S-006, S-041**.
- **`[FIX-SL §4.5.4] Rejecting invalid messages (Reject 35=3)`** — the Reject step and the FR-010a
  divergence probe's expected disposition. Covered by **S-007, S-033, S-034**.
- **`[FIX-SL §4.6.1] Normal logout processing`** — the Logout step. Covered by **S-002**.

**Correlation and replay** — the clause FR-005's occurrence ordinal exists because of:

- **`[FIX-SL §4.8.4] Possible duplicates (PossDupFlag semantics)`** — covered by **S-010, S-033**. Read
  together with **`L-021-3`** (live in `spec/behaviors-and-limitations.md`), which records that
  QuickFIX-cpp strips `PossDupFlag(43)`/`OrigSendingTime(122)`: on those combos the flag is not on the
  wire, so it cannot serve as the pairing key.

**No new OFFICIAL catalogue rows are introduced.** This feature produces *evidence* for rows that are
already `done` and changes nothing about message semantics, encoding or validation, so `[const §VI.4]`'s
coverage-index obligation is not triggered and no row's status moves (SC-007, and *What success does NOT
mean* in `quickstart.md`).

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
