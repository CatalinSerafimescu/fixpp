# Behaviors & Limitations

Cross-feature catalogue of **non-obvious intended behaviors** and **known limitations**
of the fixpp library. This is the **source of truth** that the operator/reference
documentation harvests into a consolidated *"Behaviors & Limitations"* section at
doc-build time (sibling to [`feature-catalogue.md`](./feature-catalogue.md); `prebuild.py`
whitelists `spec/*.md` into the mdBook `docs/src/`).

Scope and conventions:

- **Behavior (B-*)** = a deliberate, shipped behavior that is surprising, divergent from
  a naïve expectation, or divergent from another FIX engine — something a user/operator
  must know but would not guess. Each cites its origin feature + anchor.
- **Limitation (L-*)** = a known gap or sharp edge in shipped code. Carries a **Status**:
  `deferred` (intentional, tracked), `follow-up` (in the deferred-work registry), or
  `wontfix` (a documented divergence we stand behind). Limitations with a backlog item
  link to the **Deferred-work registry** in `CLAUDE.md`.
- Entries are added as each feature ships (Polish / catalogue step). **Prior features
  (001–014) were back-filled 2026-06-15** (draft — anchors cited from each `specs/<id>/spec.md` +
  `plan.md`; pending a verification pass at the operator-doc build). The out-of-band Fable 008 rows
  (`B-008-1`/`L-008-1`/`L-008-2`) were relocated into the 008 section as part of that back-fill.

---

## Decimal type (001-core-decimal)

> Anchor note: features 001–014 predate the FR-/SC- convention; rows cite the ids these
> specs actually use (`AC-*`, `NFR-*`, dated Clarifications, `§N`). Back-filled 2026-06-15.

### Behaviors

- **B-001-1 — Bare `fixpp_decimal_compare` / `_equal` do NO domain validation; the `_checked` siblings do.** The bare C-ABI entry points return `int` directly with no error channel and assume the caller already produced canonical values (`exponent ∈ [-38, 0]`) — feeding them an out-of-domain struct is undefined, not a reported error. Untrusted/foreign callers MUST use `fixpp_decimal_compare_checked` / `_equal_checked`, which validate and return `FIXPP_ERR_DECIMAL_INVALID` on out-of-domain input. C++ engine code calls the bare path by construction. *(AC-C6; Clarifications Session 2026-05-12.)*
- **B-001-2 — `.5` and `5.` are rejected, not parsed as 0.5 / 5.0.** A decimal literal with no integer digit (`.5`) or no fractional digit (`5.`) is `decimal_invalid_input`; any non-digit/non-dot/non-sign byte (including an embedded SOH `\x01`) is also rejected. A consumer expecting lenient `atof`-style parsing will be surprised. *(AC-P3 / AC-P4.)*
- **B-001-3 — The invalid sentinel sorts strictly greater than every finite value.** `pod_decimal_invalid` orders above all finite decimals and is equal only to itself, so a parse-failure sentinel never silently compares equal to a real price. Comparison is by numeric value, so `1`, `1.0`, `1.00` all compare equal. *(AC-C2 / AC-C1.)*
- **B-001-4 — Trailing fractional zeros are preserved in the encoding, but compare ignores them.** `5.500` parses to `{5500, -3}` (the exponent retains scale), yet value-equality treats it as equal to `5.5`. Storage is representation-faithful; comparison is value-faithful. *(AC-P5 / AC-C1.)*
- **B-001-5 — Swapping the decimal width is a build-time choice enforced at LINK time.** Setting `-DFIXPP_DECIMAL_T=...` widens the whole engine; two translation units built with conflicting `FIXPP_DECIMAL_T` fail with an unresolved-symbol *link* error (`decimal_alias_sentinel`), not a runtime mismatch. The alias never changes the `fixpp_decimal_t` C-ABI shape (always 16 bytes). *(AC-B3 / AC-B4.)*

### Limitations

- **L-001-1 — No arithmetic and no locale-aware formatting on `decimal<T>`.** No `+ - * /`, no thousands-separators / exponent-notation / per-locale decimal marks; it is a representation primitive only. **Status: wontfix.** *(spec §5 "Out of scope".)*
- **L-001-2 — No direct `T → U` cross-traits conversion; everything funnels through `pod_decimal`.** A value outside the `int64 × 10^[-38..0]` PoD interchange domain reports `decimal_precision_loss` rather than converting directly. **Status: deferred** (§10 Q1). *(spec §5 "Out of scope"; §10 Q1.)*
- **L-001-3 — No built-in `decimal_traits<__int128>` specialization.** High-precision consumers supply their own wider trait via `FIXPP_DECIMAL_USER_HEADER`; v1.0 ships no `__int128` trait. **Status: deferred** (§10 Q2). *(spec §5; §10 Q2.)*
- **L-001-4 — Exactly-representable values whose digit string is capacity-padded with trailing zeros are rejected as `decimal_overflow`.** Because the parser preserves every fractional digit into the mantissa (§6.1 step 5, AC-P5) and rejects int64 overflow (step 6), a padded form of an in-range value overflows even though its canonical form fits: `"1.0000000000000000000"` (1 + 19 zeros, value 1.0) and `"9223372036854775807.0"` (INT64_MAX with one decimal place) both report `decimal_overflow`, while `"1." + 18 zeros` and `"9223372036854775807"` parse fine. Counterparty-reachable via the opt-in inbound validator Float arm → per-field Reject (`373=5`); fail-closed availability quirk, not corruption. A strip-trailing-zeros-on-overflow retry would admit these but conflicts with the §6.1-step-5 preservation mandate — so **status: wontfix** (spec-forced consequence of AC-P5 + overflow rejection). *(§6.1 steps 5–6; AC-P5 / AC-P7 / AC-P8.)*

## XML data dictionary loader (002-dictionary-xml-loader)

### Behaviors

- **B-002-1 — The loader THROWS typed exceptions; a deliberate carve-out from the engine's noexcept/`expected_t` model.** `XmlLoader::load` returns a `Dictionary` by value and signals construction-time failure via typed exceptions — not `expected_t`. **Catch carefully:** `dict::xml_parse_error` and `dict::unknown_version_error` derive from `std::runtime_error`, but `dict::xml_oom_error` derives from `std::bad_alloc` (`dict/error.hpp:64`), NOT `std::runtime_error` — a `catch (std::runtime_error&)` silently MISSES the OOM path. The hot-path `Dictionary` accessors are then all `noexcept`. *(spec §1 Style note; AC-L2 / AC-L9; NFR-002-5; `include/fixpp/dict/error.hpp:33,47,64`.)*
- **B-002-2 — Walk a component's/group's fields via `component_fields()` / `group_fields()`, not by indexing `fields_`.** Under the runtime loader, `ComponentRef`/`GroupRef::first_field_index` index per-component/per-group SIDE TABLES, not the main `fields_` array (which is concatenated per `(message, field)` for O(log N) lookup), so components are not contiguous in it. This differs from the codegen-emitted layout where components ARE contiguous. *(Clarifications Session 2026-05-15 Q4.)*
- **B-002-3 — There is no context-free `field(tag)` lookup; field metadata is keyed by `(MsgType, tag)`.** One `FieldRef` exists per `(MsgType, tag)` pair (a field can carry a different presence rule per message), so a bare `field(tag)` has no canonical answer and is not in the v1.0 surface. Unknown tags return `field_presence::NotDeclared`, not an error. *(AC-D1 / AC-D2.)*
- **B-002-4 — `messages()` iteration order is bytewise-lexicographic by MsgType, locale-independent.** Ordering is fixed by `std::ranges::lexicographical_compare` over `unsigned char`, so the same XML yields a byte-stable order across runs and machines — not the host locale's collation. *(AC-D5; NFR-002-4.)*

### Limitations

- **L-002-1 — Only the four codegen-target versions (FIX42/44/50SP2/FIXT11) ship XML + headline tests.** The loader structurally accepts all nine v1.0 versions, but the five runtime-XML-only ones (FIX 4.0/4.1/4.3/5.0/5.0SP1) ship no checked-in XML data; supplying them is ~1 PR of data each, no loader code. **Status: deferred** (§10 F1). *(Clarifications Session 2026-05-14 Q1.)*
- **L-002-2 — `DialectOverlay` / `load_overlay*` is absent.** Per-session venue-dialect extension of a base dictionary is not in this feature. **Status: deferred** (§10 F2, dedicated feature). *(Clarifications Session 2026-05-14 Q2.)*
- **L-002-3 — Semantically-inconsistent-but-structurally-valid XML is accepted; only structural defects are caught.** A `<message>` referencing a field with a wrong-for-usage type is NOT a loader error; semantic validation belongs to `wire::Validator` downstream. **Status: wontfix.** *(spec §3 Edge Cases.)*
- **L-002-4 — Zero-allocation covers only the output `Dictionary` metadata, not pugixml's transient DOM.** pugixml's intermediate DOM uses `malloc/free` (not `operator new`); peak parser-side memory is not bounded by the NFR-002-2 gate. **Status: wontfix.** *(NFR-002-2.)*

## Dictionary codegen + typed messages + reify bridge (003-dictionary-codegen)

### Behaviors

- **B-003-1 — Typed-message accessors are `inline noexcept`, NOT `constexpr`.** Only `msg_type_v` / `version_v` are `constexpr`; per-tag accessors route through `wire::OffsetTable::find` (non-`constexpr`). The catalogue's "constexpr accessors" title is looser than what ships. *(AC-G11; spec §1 Style note.)*
- **B-003-2 — The decimal accessor alone takes an explicit `std::pmr::memory_resource* mr`; string/int/char accessors are zero-arg.** A typed decimal read is `price(mr)` returning `expected_t<decimal_t>` (zero-alloc for the default `pod_decimal`; an allocating substituted `FIXPP_DECIMAL_T` draws from `mr`, never raw `new`). `decimal_t` is deliberately NOT a `field_traits` specialization. *(AC-G4 / AC-G4a / AC-FT2.)*
- **B-003-3 — Codegen runs at CONFIGURE time and writes only to the build tree, never the source tree.** `fixpp::dict::generate-vXX` emits per-version headers under `build/<preset>/_codegen/...`; a source checkout never carries generated headers, so a dirty tree never ships stale codegen. Output is byte-identical run-to-run. *(AC-T1 / AC-T2 / AC-C4.)*
- **B-003-4 — `owning_<Msg>` is single-strand-only; the safe cross-thread pattern is reify-on-A → move → consume-on-B.** The lazy `view()` cache write is unsynchronized, so concurrent reads on one `owning_<Msg>` instance are UB. To cross a strand boundary, `dict::reify_as` on thread A, `std::move` the owner to thread B, then read. *(AC-T3 / AC-R5.)*
- **B-003-5 — A FIXT.1.1 message that can't resolve its application version returns a distinct error, not "unknown msg type".** When `default_appl == Unknown` and a frame lacks `ApplVerID(1128)`, `dict::reify` returns `dict_unresolved_application_version` (NOT `dict_reify_unknown_msg_type`). *(AC-D6; `src/dictionary/version_profile.cpp:20-24`, propagated `reify.cpp:225`.)*

### Limitations

- **L-003-1 — Typed messages + compile-time shape ship; the BEHAVIORAL reify/typed-read round-trip is now PARTIALLY unblocked (057).** The `owning_<Msg>` emission + compile-time shape landed at 003. **057 (2026-07-01) lifts the runtime half:** `dict::reify()` returns live `owning_message_handle`s with byte-faithful `version()` / `msg_type()` / `field_value()` for application (single- **and** multi-char MsgType) and FIXT-admin frames across v42/v44/v50sp2, and `dict::reify_as<Msg>()` returns concrete typed `owning_<Msg>` owners. **Still deferred:** the `owning_message_handle::as<Msg>()` typed-downcast half remains **AC-R6-deferred / T059-stubbed** — a still-live deferred contract, not shipped (byte storage does not foreclose a future lazily-populated owner-cache). **Status: PARTIAL** (runtime `reify`/`reify_as` shipped 057; `as<Msg>()` AC-R6-deferred). *(spec §5 last bullet; §11 R6; 057 spec/plan.)*
- **L-003-2 — Only the four codegen-target versions get typed namespaces; runtime-XML-only versions have none.** For those, `dict::reify` returns `dict_reify_unknown_msg_type`; the positive path is 002's runtime `view.get(uint16_t)`. **Status: deferred** (§10 F5). *(spec §5; §10 F5.)*
- **L-003-3 — The codegen-emitted `Validator.hpp` is shape-tested only; it does not actually reject bad messages.** This feature ships the per-message rule tables + Length/Data pair table and asserts their structure against the XML, but behavioral validation is a downstream wire-layer feature (and the hand-written `wire::Validator` it complements has no production caller — see B-004-1 / B-005-7). **Status: deferred.** *(Clarifications Session 2026-05-15 Q3; AC-V3; spec §5.)*
- **L-003-4 — The all-versions translation unit is NOT a supported default build.** The load-bearing ceiling is the single-version TU (≤ 3 s); the all-versions TU carries only a soft ≤ 15 s ceiling. **Status: wontfix.** *(NFR-003-2; spec §5.)*

## Wire codec — framer, parser, offset table, writer, validator (004-wire-codec)

### Behaviors

- **B-004-1 — Field parsing is order-independent; the only ordering rule the `wire::Validator` enforces is "MsgType(35) first after framing" — and that validator is NOT invoked on the session inbound path.** The parser indexes fields by tag (O(1)-by-tag, no positional requirement). The hand-written `wire::Validator::validate` enforces one order rule — the first non-framing offset-table entry must be `MsgType(35)`, else `wire_header_out_of_order` (`validator.hpp:108-131`). By default, `Session::on_inbound_frame` scans frames directly via `scan_frame_header` and does not run `dictionary_driven_validator`, so out-of-order header/body fields are accepted on the live session path (see B-005-7); fixpp does not enforce full FIX-SL §4.5 field-block ordering on the default path. **Updated (041):** `dictionary_driven_validator` is NO LONGER caller-less — 041 wired it onto the live inbound path under the opt-in `SessionConfig::validate_inbound_messages=true` (default `false`), which Rejects out-of-order header fields; see **B-041-1** (`src/session/session.cpp:1168`/`:1877`). *(FR-002; `validator.hpp:108-131`; error `wire_header_out_of_order` (39).)*
- **B-004-2 — CheckSum verification is mandatory with no production bypass switch.** A wrong `CheckSum(10)` or inconsistent `BodyLength(9)` is always rejected before any parser sees the frame — there is no "skip checksum" config knob (only a tests-only hook). Several FIX engines expose checksum-leniency toggles; fixpp does not. *(FR-017; spec §Edge Cases; plan.md:50.)*
- **B-004-3 — DoS caps reject some conformant venue traffic on day one by default.** Default bounds (256 KiB max frame, 4096 offset-table occurrences, 4096 group entries/instance) target FX/equities; a large options-chain MDIR or `SecurityList` (thousands of strikes) exceeds the defaults and is rejected (`wire_frame_too_large` / `wire_offset_table_full`). Such venues must explicitly raise the caps. *(FR-015; Assumptions; SC-003.)*
- **B-004-4 — A parsed view aliases the caller's buffer and traps (debug) on use-after-reuse.** `MessageView`/`field_view` are zero-copy flyweights whose lifetime is the caller-owned buffer's; a debug-only generation counter traps deterministically if the buffer is reused under a live view (compiled out in release). *(FR-016; Key Entities "View"; `[const §IX.4]`.)*
- **B-004-5 — Unknown/custom fields are preserved opaquely and round-trip byte-identically.** Tags absent from the dictionary are not dropped/rejected at parse; they are exposed via `unknown_fields()` and written back in original byte order on re-serialize (zero-alloc), so parse→serialize is byte-identical including custom fields. *(FR-008; SC-001; data-model.md:64.)*
- **B-004-6 — A `Length`+`Data` field whose declared byte count does not land on a `SOH` field boundary is now rejected (Index) / stops iteration (Iter); it was previously absorbed.** Per `[FIX50SP2 §3]` a `Length` tag (e.g. `RawDataLength(95)`) gives the *exact* byte count of the paired `Data` value (`RawData(96)`), and the byte immediately after that value must be `SOH`. A declared length that runs past the frame end, whose end byte is not `SOH`, or that lands **exactly** at the frame end (no trailing `SOH` at all, silently swallowing the trailing `10=CheckSum` field into the Data value) is a lying/malformed length — for this whole-frame scanner a legitimate counted value can never reach the frame's last byte, because a Framer-validated frame always has a trailing checksum field after the body. **Index mode** (the live session ingest path) now rejects such a frame with `wire_invalid_field_format` in all three cases instead of blindly skipping and desyncing into the following field (which silently hid or corrupted downstream fields, or swallowed the checksum). The pending `Length→Data` association is also required to be *adjacent*: if the field immediately after a `Length` tag is not its paired `Data` tag, the pending count is cleared (a later same-`Data`-tag field is no longer read by a stale count). Length/count scans are bounded (saturating) so an over-large declared value cannot wrap `uint32`/`size_t`. This closes a parser-differential vs conformant peers and the latent 32-bit heap-OOB escalation. *(wire-hostile-input-review W-P2-1; Gate B PR #166 round-1 Finding 1 (`end == n` closed); `src/wire/offset_table.cpp` build; `include/fixpp/wire/tag_scan.hpp` `accumulate_bounded`.)*

- **B-004-7 — A repeating-group count field of zero (`NoXXX=0`) is accepted as a well-formed present-but-empty group, not rejected as a malformed group.** The `dictionary_driven_validator` short-circuits the group-structure check when the declared count is `0` (`validator.hpp:210-212`), so a message carrying e.g. `NoHops(627)=0` followed by a non-member field validates OK, the trailing field is read as a normal scalar (not walked into the group), and the offset-table / `group_view` yields zero entries; the count field re-encodes byte-identically. This mirrors QuickFIX-cpp acceptance test 21 (`RepeatingGroupSpecifierWithValueOfZero`, CBOEDirect semantics), adopted as conformance fixture TC-018. NOTE the short-circuit also *tolerates* a declared-zero-but-members-present frame (the `actual_count == declared_count` equality check is skipped for count `0`), consistent with the accept-empty-group intent. *(TC-018; `validator.hpp:210-212`; witnesses `validator_per_version_test.cpp::ZeroCountGroupAccepted` (all 4 wire versions, mutation-proven) + `round_trip_property_test.cpp::ZeroCountGroupPreservedByteIdentical`; dict-aware empty-group reads already witnessed by `GroupEntryRead.EmptyGroupSizeZeroNoDeref`.)*

### Limitations

- **L-004-1 — Dialect-introduced new `Length`+`Data` (BLOB) pairs are not handled on the streaming iterator path.** Index mode resolves all dialect BLOB pairs via the runtime dictionary, but Iter (streaming) mode uses a static `constexpr` table of FIX-5.0-SP2-standard pairs only. **Status: deferred.** *(FR-005; research D-11.)*
- **L-004-2 — Binary encodings (FIXP / SOFH / SBE) are out of scope; v1.0 is Tag=Value SOH only.** **Status: deferred.** *(spec §Assumptions.)*
- **L-004-3 — The wire layer adds no C-ABI surface; the 13 new `wire_*` variants' C-ABI coalescing is deferred to feature 2i.** **Status: deferred.** *(FR-014; plan.md:74,77.)*
- **L-004-4 — Framing errors are session-fatal; there is NO in-stream resync-and-continue (diverges from FIX-TC 2d / QuickFIX).** When the framer rejects bytes (bad BeginString/BodyLength/checksum framing), the engine read-pump terminates the session (`engine.cpp:537-541`) rather than skipping the garbled region and rescanning. NOTE: a too-high *sequence* gap on a *well-framed* frame is NOT fatal — it triggers ResendRequest recovery (B-013-1); this is specifically about byte-level framing corruption. **Status: wontfix** (fail-closed on a corrupt transport stream). *(`src/wire/framer.cpp`; `engine.cpp:537-541`; FIX-TC coverage-audit 2026-06-15.)*
- **L-004-5 — The streaming `Iter` (`parse_iter`) mode is best-effort with NO error channel: a malformed field truncates iteration silently (no `malformed()` signal).** Unlike Index mode — which surfaces every malformation via `build_status()` and is the path the live session uses (`Session::on_inbound_frame` → `Parser<Index>`) — `field_iterator::advance()` reacts to a non-digit tag byte, a tag overflow, a missing `=`, or a `Length`+`Data` `SOH`-boundary mismatch (B-004-6) by ending iteration (`done_=true`) with no indication the frame was malformed. A direct `parse_iter` consumer therefore sees a truncated field set indistinguishable from a legitimately short message and must treat a short stream as suspect. Additionally, Iter does not replicate Index's empty-tag reject: a field `=value<SOH>` (no tag digits) yields a `tag == 0` field and iteration **continues**, whereas Index rejects the whole frame with `wire_invalid_field_format` (the `i == tag_start` guard). The range-for **does** always terminate on such input (a `done_` iterator compares equal to `end()`), so there is no hang; the limitation is only the absent error signal. **Status: documented (defense-in-depth); not prod-reachable** — production ingest is Index-only. *(wire-hostile-input-review W-P2-2; Gate B PR #166 round-2 P3 (empty-tag divergence, document-not-fix); `include/fixpp/wire/parser.hpp` `advance`/`operator==`.)*
- **L-004-6 — The `OffsetTable` open-address overlay uses a per-process-randomised hash seed, so the internal slot layout differs across processes (immaterial to observable behavior).** `find(tag)` / `entries()` / document order are unchanged; the seed only randomises the internal probe sequence to defeat a crafted hash-collision that would otherwise force a present required field to read false-absent (a predictable-`mix()` HashDoS). A test/fuzz hook (`detail::set_overlay_seed_for_testing`) pins the seed for reproducibility; production randomises it once per process. **Status: by design.** *(wire-hostile-input-review W-P3-2; `src/wire/offset_table.cpp` `mix`/`compute_process_seed`.)*

## Session establishment & FSM core (005-session-establishment-fsm)

### Behaviors

- **B-005-2 — There is no `RecoveryPending` half-state; the FSM is exactly the 6-state `[FIX-SL §4.10]` set.** `NotConnected → LogonSent → LogonReceived → Active → LogoutSent → Disconnected`, no invented states (a Gate-A survey found the OSS premise behind a proposed half-state was inverted). *(FR-001; Session-2026-05-18; Key Entities.)*
- **B-005-3 — Receipt of a deferred admin type (`ResendRequest`/`SequenceReset`) is a defined, bounded transition — never a silent no-op.** Even where gap-fill/resend was out of 005 scope, the FSM dispositions these (session-level `Reject` or the defined transition), never UB and never silently ignored. *(FR-017; spec §Edge Cases.)*
- **B-005-4 — `seqnum_t` overflow is session-fatal and requires operator intervention; it never silently wraps.** At the max representable outbound counter the session reports a fatal sequence-overflow rather than wrapping to zero. *(FR-009; spec §Edge Cases.)*
- **B-005-5 — Outbound sequence numbers are committed to the `MessageStore` BEFORE the frame hits transport (durable-before-transmit).** A cancelled transmit must not leave a persisted-but-unsent gap inconsistent with the contract. *(FR-010; `[2e §root cause #1]`.)*
- **B-005-6 — `HeartBtInt=0` fully disables heartbeating (no timers run at all).** When negotiated at Logon, neither heartbeat nor test-request timers run; the session is Active but emits no liveness traffic, per `[FIX-SL §4.3.4]`. *(FR-006.)*
- **B-005-7 — The live session inbound path accepts out-of-order header/body fields (incl. MsgType not first); it does NOT emit a Reject for field-order violations.** `Session::on_inbound_frame` scans fields order-independently (`scan_frame_header`) and never runs the `wire::Validator` MsgType-first check (B-004-1), so a frame with `MsgSeqNum(34)` before `MsgType(35)`, or with body fields shuffled, is accepted and processed (seqnum advances) — diverging from QuickFIX, which emits `Reject(SessionRejectReason=14)`. **[RATIFY RESOLVED 2026-06-19]** — the lenient order-independent parse is **retained as the default** (benign for well-formed frames — valid framing, required fields present — and not a forged-tag/delimiter vector); operators needing QuickFIX-parity strict §4.5 enforcement opt in via `SessionConfig::validate_inbound_messages=true` (041 / **B-041-1**), which Rejects out-of-order header fields with `373=14` (witnessed `tests/session/test_validate_gate_inbound.cpp` W1). Both modes are pinned. *(FIX-TC 2t/15 coverage-audit 2026-06-15; lenient default witnessed `tests/interop/parity/fix_tc_coverage_gaps_test.cpp` (HeaderFieldsOutOfOrder / BodyFieldsArbitraryOrder); see `fix-tc-coverage-gaps-findings.md`.)*

### Limitations

- **L-005-1 — The full `[FIX-TC]` conformance corpus is NOT satisfied; only a capability-partitioned subset ships green.** Recovery-dependent and too-high-seqnum TC cases are deferred-with-traceability; `[const §VII.5]` is explicitly NOT satisfied under a recorded waiver. **Status: deferred.** *(FR-018; SC-008; Session-2026-05-18.)*
- **L-005-5 — `OnBehalfOfCompID(115)`/`DeliverToCompID(128)` third-party addressing is not implemented; only point-to-point 49/56 validation.** **Status: deferred.** *(FR-004 scope note.)*

## Session FSM finalize (009-session-fsm-finalize)

### Behaviors

- **B-009-1 — Acceptor sessions stay in `NotConnected` at `open()` and emit no outbound Logon; only initiators emit at open.** With `role = acceptor`, `Session::open` waits for a peer `Logon` (then `NotConnected → LogonReceived → Active`). The pre-009 impl unconditionally entered `LogonSent` regardless of role. `role` defaults to `initiator`. *(FR-004/FR-005; data-model.md:19.)*
- **B-009-2 — A refused first `Logon` in `NotConnected` transitions to `Disconnected`, not back to `NotConnected`.** A failed BeginString/CompID/establishment validation moves the FSM to `Disconnected` per the matrix. *(FR-006; data-model.md:19.)*
- **B-009-3 — Missing OR malformed inbound `SendingTime(52)` is rejected, not leniently accepted.** In `Active`/`LogonReceived`, absent/unparseable `52` → `Reject(SessionRejectReason=10, RefTagID=52)` → `Logout` → `Disconnected`; in `LogonSent`, instead a `Logout(58=…)` with no standalone Reject. The prior lenient fall-through was removed. *(FR-007/FR-008/FR-009; `include/fixpp/session/sending_time.hpp:11-16`; `session.cpp:1817`, `:1858-1860`.)*
- **B-009-4 — Every outbound frame (incl. all 5 admin builders) carries the negotiated `BeginString` and a live-clock `SendingTime`, not hard-coded placeholders.** The hard-coded `"FIX.4.2"` and zero-timestamp placeholder constants were removed. *(FR-002/FR-003.)*
- **B-009-5 — In `LogoutSent` (graceful logout in progress) all inbound frames except the peer's `Logout(35=5)` are silently drained.** An inbound Logon (or any app/admin frame) received while awaiting the peer's Logout confirmation does NOT advance the inbound seqnum, is NOT Rejected, and does NOT re-establish — only the peer's Logout is acted upon (→ Disconnected). Diverges from QuickFIX, which treats a second Logon in this state as valid. *(`session.cpp:3322-3338`; FIX-TC coverage-audit 2026-06-15; `tests/interop/parity/fix_tc_coverage_gaps_test.cpp`.)*

### Limitations

- **L-009-1 — A `Session::send` whose transport `async_write` is cancelled after the store commit leaves a durable-but-unsent frame; 009 does not recover it.** The slice returns a defined error and reaches `Disconnected` (no silent success) but performs no session-level recovery of the phantom committed frame. **Status: deferred.** *(spec §Edge Cases; `[2e §3.1]`.)*
- **L-009-2 — TestRequest ID uniqueness is per-session-lifetime only and wraps at `UINT32_MAX`.** Not unique across sessions or restarts; wrap-around is acceptable. **Status: wontfix.** *(FR-010.)*
- **L-009-3 — `SeqnumManager::drain` failure during `close` is logged-then-proceed, not surfaced as a close error.** Still reports `closed_drained`; the destructor completes safely. **Status: wontfix.** *(FR-011.)*

## Session config & lifetime (010-session-cfg-lifetime)

### Behaviors

- **B-010-1 — `Session` copies its `SessionConfig` by value at construction; the caller may drop or mutate the config afterward.** `Session` holds `SessionConfig cfg_` by value (was a `const&`, a stack-use-after-scope hazard). Each `Session` owns its snapshot; post-construction mutation of the caller's config has no effect, and config sharing across sessions is unsupported (construct one config per session). *(FR-001; Clarification 2026-05-23; Key Entities.)*
- **B-010-2 — `Session::send` in a non-`Active` state returns a dedicated `session_invalid_state_for_send` error, distinct from `session_invalid_logon`.** 010 added slot 77 to de-conflate the two conditions; FSM-side Logon-refusal still uses `session_invalid_logon`. *(FR-005; SC-004.)*
- **B-010-3 — A duplicate `Logon` (`35=A`) in `Active` emits exactly one `Reject(35=3)` — a deliberate defensive divergence from QuickFIX's refresh-on-dup-Logon convention.** `"A"` is deliberately excluded from `is_admin_msgtype`, so it falls through to a single Reject per 005 FR-017 ("never silent no-op"). NOTE: `ResendRequest("2")` and `SequenceReset("4")` in `Active` are **no longer Rejected** — they were upgraded to full processing by 013 (Phase 3) / S-023 and explicitly no longer reach the Reject branch. *(FR-006/FR-008; SC-002; `session.cpp:3197-3199`.)*
- **B-010-4 — The `LogonReceived` FSM state is synchronous-transient; observe it via the always-on history ring buffer.** On the acceptor reply-Logon path the FSM passes `NotConnected → LogonReceived → Active` in one tick, so `LogonReceived` cannot be caught by a state snapshot; `Session::fsm_visit_history()` (a 16-entry ring, always written) is the only observation seam. No `#ifdef` divergence between production and test. *(FR-004; SC-002.)*

### Limitations

- **L-010-1 — The `Active×ResendRequest` / `Active×SequenceReset` Reject→Process upgrade has LANDED (013 / S-023 / 027 / 029); only the dup-Logon-in-`Active` cell still Rejects by design.** What 010 shipped as `TODO(2e-recovery)` Reject cells are now fully processed (ResendRequest → `replay_outbound_range_`; SequenceReset → Reset-mode bypass + GapFill arms), staying Active. Catalogue row 400 is thereby largely discharged; the residual is a v1.0-gate traceability-marker confirm. **Status: follow-up** (was deferred — now shipped). *(SC-002; `session.cpp:2423`, `:3160`.)*
- **L-010-2 — The `cfg_` member is not type-`const`; "no post-ctor mutation" is convention-enforced, not compiler-enforced.** Held non-const to preserve future config-hot-reload flexibility. **Status: wontfix.** *(FR-001.)*
- **L-010-3 — `SessionConfig::store_factory` is a `shared_ptr` (a small 005 amendment) to make `SessionConfig` copy-constructible.** The per-Session "one MessageStore per Session" invariant is unaffected. **Status: wontfix.** *(FR-001a; `session_config.hpp:172`.)*

## Awaitable mutex `fixpp::sync::async_mutex` (006-async-mutex)

### Behaviors

- **B-006-1 — `async_mutex` is the only legal mutex in any coroutine context; plain `std::mutex` is banned and CI-enforced.** Any header that includes `asio::awaitable<...>` and also names a `std::` mutex spelling fails the build with a diagnostic pointing at `fixpp::sync::async_mutex`. A custom-`MessageStore` author migrating from QuickFIX must swap `std::mutex` for `async_mutex`. *(FR-014 / SC-006 / US5.)*
- **B-006-2 — Destroying a mutex that still has a live holder or waiters calls `std::terminate()` by design — it is not an error return.** A documented hard precondition (RC#3), enforced in both debug and release; teardown must `cancel_and_drain()` first. *(FR-008; spec §Edge Cases.)*
- **B-006-3 — Waiter resumption is always `post`ed, never inline-`dispatch`ed, regardless of `completion_policy`.** The `completion_policy` survives only as an intent knob queryable via `policy()`; an inline `dispatch` at the resume site was a TSan-diagnosed heap-UAF (Erratum E-3). A caller expecting `dispatch`-policy inline resumption will not get it. *(FR-004; US1.3.)*

### Limitations

- **L-006-1 — No shared/RW, recursive, or timed mutex variant; LIFO-pop + FIFO-drain is the only fairness mode, and there is no public `try_lock()`.** Recursive acquire by the current holder is unsupported by construction. **Status: wontfix.** *(FR-015; spec §Edge Cases.)*
- **L-006-2 — Zero-global-allocation on the contended path holds only in steady state, after a one-time per-thread cancellation-recycler warm-up.** asio's `cancellation_slot` exposes no allocator-binding hook; the one-time first-touch is amortized and bench-soft (Erratum E-4). **Status: wontfix.** *(FR-009 / SC-004.)*

## Application threading contract & `fixpp::core::Clock` (007-threading-clock)

### Behaviors

- **B-007-1 — Application callbacks are serialised per session on an engine-derived strand and never run on the I/O recv thread.** `onLogon`/`onLogout`/`toAdmin`/`fromAdmin`/`toApp`/`fromApp` for one session never overlap; callbacks for *different* sessions may run concurrently. The engine never picks a concrete executor — the application supplies one `any_io_executor`. *(FR-007/FR-008; US1.)*
- **B-007-3 — `now()` is not promised monotonic; all elapsed/heartbeat/SendingTime-*delta* (threshold) measurements use `steady_now()`.** A benign NTP/admin backward wall-clock step never trips a SendingTime-threshold reject; wall-clock `now()` is consulted for the actual `SendingTime(52)` wire stamp (`session.cpp:4045`) and log/OTel timestamps — NOT for interval/threshold deltas. Diverges from engines that measure heartbeat intervals against wall-clock. *(FR-004; US2.3.)*
- **B-007-4 — Opting out of the per-session strand requires explicit attestation, and `direct_executor + spin` is rejected even when attested.** `threading_mode::direct_executor` skips the `make_strand` wrap but demands `already_serialized_executor == true`, else `error::executor_not_serialised`; `direct_executor` + `lock_policy::spin` is rejected with `error::invalid_session_config`. *(FR-009; US1.4.)*
- **B-007-5 — `drop_oldest` backpressure is unrepresentable.** `backpressure_mode` is a closed enum with only `block` (default) and `disconnect_and_recover`; an out-of-range cast is rejected at construction (`[const §XV.15]` no silent loss). *(FR-010; US5.3.)*
- **B-007-6 — Trace context follows the session serialisation domain, not `thread_local`, and survives resume on a different thread.** `co_await fixpp::current_trace_context` recovers a typed `Session*` and reads a `session_local<trace_context>` slot, byte-identical across a suspend/resume on another pool thread. *(FR-014/FR-015; US4.)*
- **B-007-7 — Engine/session config is frozen at open; the only reconfiguration path is close-and-reopen.** Executor, clock, dictionary, mode, locks, trace-context all freeze via `resolved = override.value_or(engine_anchor)`. *(FR-016; US5.1.)*

### Limitations

- **L-007-1 — `Session::close(partial)` is not in the v1.0 surface; only `graceful` and `terminal` ship.** **Status: deferred.** *(FR-011; Key Entities "close_mode".)*
- **L-007-2 — `direct_executor` attested over a genuinely non-serialised executor is UB in release builds.** Debug trips a strand-invariant assert on detected concurrent FSM entry; release treats a false attestation as user-contract-violation UB. **Status: wontfix.** *(spec §Edge Cases; FR-009.)*
- **L-007-3 — The cross-thread dispatch latency ceiling (250 ns) is regression-gated, not an absolute hard-fail.** CI fails only on >5% regression vs the previous tagged release; the absolute figure is a §10-Q4 tightening target. **Status: follow-up.** *(SC-004; FR-021.)*

## Message store — async API + `MemoryStore` / `FileStore` (008-message-store)

### Behaviors

- **B-008-1 — The FileStore log grows monotonically until a reset epoch (size disk accordingly).** FileStore is append-only with no compaction, rotation, or size cap: each outbound message appends ≈ `payload + 16 B` record header (+ ≤ 7 B alignment padding) and each inbound advance a 24 B counter record (16 B header + 8 B payload). The only reclamation is a reset epoch (`reset_on_{logon,logout,disconnect}` — all default `false` — or bilateral `141=Y`). Default config ⇒ monotone growth for the durable session lifetime (restart re-scans the full log). fixpp has no `SessionTime`-driven daily auto-reset. **Operator action:** size disk for one reset epoch and enable a reset knob or arrange external EOD resets. *(Fable `5.4`; `file_store.cpp:135` (kHeaderSize=16), `:375-381` (record size), `:680` (append); `file_store_factory.cpp:174`.)*
- **B-008-2 — The *default* `MemoryStore` config is rejected at construction under the *default* 1 GiB cap, by design.** The frozen defaults (`inbound = outbound = 10'000` × 256 KiB `max_frame_bytes` ≈ 5 GiB worst case) deliberately exceed `EngineConfig::max_store_memory_per_session = 1 GiB`, so a default `MemoryStoreFactory::make()` returns `store_factory_failed` and the session does not open — the operator must raise the cap or lower `max_frame_bytes`. *(FR-014a / SC-004; US2 scenario 2a; `memory_store.hpp:69-71`, `engine_config.hpp:159`, `memory_store_factory.hpp:102-103`.)*
- **B-008-3 — `MemoryStore` is test/embedded-only; production deployments must use `FileStore` (or a custom impl).** `MemoryStore` never persists and survives no host crash; there is no "production in-memory store." *(spec §Key Entities S-012; US1.)*
- **B-008-4 — `store_seqnum_overflow` is session-fatal and the store never autonomously resets.** At `seqnum_max`, `next_seqnum(increment)` returns `store_seqnum_overflow`, does not increment, and the session cannot send until `reset()`; the operator chooses `reset()` + `141=Y` vs sticky abort. The store never silently rolls over. *(FR-022; spec §Edge Cases.)*
- **B-008-5 — `Session::close(terminal)` deliberately skips the FileStore durability flush, so terminal close can lose up to N-1 batched records.** `FileStore::flush_for_session_close()` runs only under `close(graceful)` (before the Logout write); `close(terminal)` skips phase 1, leaving the documented `commit_batched(N)` up-to-N-1-record window unflushed. *(FR-028; US2 scenarios 4–5.)*
- **B-008-6 — QuickFIX `MessageStore` migration is Path B only: no runtime adapter ships.** v1.0 ships a documented incompatibility + 5-step recipe + a config-translation `cfg_loader`, but no adapter wrapping a synchronous `quickfix::MessageStore` (the five hazards compose and cannot be safely fenced). A compile-time guard rejects any implicit sync-shaped construction. *(FR-030/FR-032; US4.)*
- **B-008-7 — `MemoryStore::retrieve()` fails closed with `store_io_failure` on a `reset()` that runs mid-traversal (both capacity policies).** A `reset()` interleaved with an in-flight `retrieve()` walk (during a `visitor.on_frame()` suspension) bumps a per-instance reset epoch (`generation_`, snapshotted under the writer mutex at walk start and re-checked before each frame with no `co_await` between the check and the byte read); a stale epoch stops the walk with `store_io_failure` rather than replaying a slot the concurrent `reset()`+`store()` may have overwritten. Bounded frames are additionally materialised into a private scratch buffer before `on_frame()`, so a mid-suspension overwrite cannot corrupt the visitor's own view. Mirrors FileStore's `generation_` guard (`B-035-1`) and the `message_store.hpp` contract ("mid-traversal mutation is detected … without UB"). Production is bounded by the single-session-strand discipline (serial `on_inbound_frame` cannot interleave a `reset()` mid-resend) → defense-in-depth for a future async caller; **the unbounded policy, previously silent snapshot-replay, now also fails closed** (contract parity). *(S-P2-2; phase-9 `outbound-store-retention-review.md` §P2-2; `memory_store.hpp` `retrieve()`/`reset()`; `tests/session/test_memory_store_reset_during_retrieve.cpp`.)*

### Limitations

- **L-008-1 — The FileStore in-RAM offset index is uncapped and re-materialized at restart.** A per-record in-RAM index (≈ 24 B per retained outbound frame) with no cap/spill/eviction, rebuilt in full from a log scan at restart (≈ 24 B/entry ⇒ a week-long 1k msg/s session ≈ 14 GB index). Unlike QuickFIX/J's `FileStoreMaxCachedMsgs`, this index is mandatory and unbounded. **Status: deferred** (REMAINING-WORK). *(Fable `5.4`; `file_store.cpp:607-612` (IndexEntry), `:661-662` (index members), `:909` (restart rebuild).)*
- **L-008-2 — A bounded *volatile* `MemoryStore` past capacity keeps transmitting but stops retaining (silent resend loss).** When the per-direction cap is hit, `store()` returns `store_capacity_exhausted` and `store_then_emit` is logged-then-proceed (I-07): messages keep going on the wire but are no longer retained, so a later peer `ResendRequest` folds them into a SequenceReset-GapFill — the peer's application-level recovery silently loses them (FIX-legal). **Scope narrowed by 059:** this applies ONLY to a **volatile** store (`store_is_persistent_ == false`). On a **persistent** store (`FileStore`) a genuine retain failure now **fails closed** — the session disconnects before transmitting the un-retained frame and reconciles the outbound counter to the durable value, so no silent resend loss occurs on the durable path (see `B-059-1` below / spec 059). **Status: wontfix (volatile leg).** *(Fable `5.4`; `memory_store.hpp:159-166`; the volatile logged-then-proceed arm is the `store_is_persistent_`-gated fall-through in `store_then_emit`, `session.cpp` ~`:4804` (fatal branch is persistent-only).)*
- **B-059-1 — A persistent (`FileStore`) outbound-retain failure fails closed (disconnect-and-recover), symmetric with the durable counter-write path.** On `!store_r && store_is_persistent_` with error = any persistent store-retain-fatal code except `store_cancelled` (durability-classified); `store_io_failure`/`store_seqnum_out_of_order`/`store_capacity_exhausted` are the FileStore-reachable subset, not a closed set. A custom persistent store returning any other store-block code (`store_seqnum_gap`/`store_seqnum_overflow`/`store_visitor_aborted`/`store_seqnum_invalid`/`store_invalid_range`) also fails closed. `store_then_emit` captures the error, best-effort reconciles the outbound wire counter down to the durable value (`set_next_outbound(durable_k)`; does NOT touch `hydrated_`), and returns the error **before** transmitting — the caller (`Session::send` and the broad-guard emit sites) transitions to `Disconnected` (caller-owned, transport-failure parity). Cancellation-class `store_cancelled` is excluded (absorb→proceed, FR-005). **Status: shipped (059).** *(spec 059 FR-001..007; `session.cpp` ~`:4804-4823`; `contracts/store-then-emit-disposition.md`.)*
  - **US3 reconnect bound:** after the fail-closed + reconcile, an in-process reconnect under `reset_seqnum_policy = bilateral_strict` (the DEFAULT) re-emits a Logon carrying `34=k (k>1)` + `141=Y` — the **pre-existing, deferred `L-029-3`** malformed-Logon shape (see below). 059 neither introduces nor fixes L-029-3; it only creates a reconnect path that can reach it. Plain-persistent sessions resume cleanly at `k`; `reset_on_logon` overrides to `34=1`. (Cross-ref only — do NOT fix L-029-3 here.)
  - **Residual window (gate-b/r1 FQ-4a):** at a best-effort-emit (swallow) call site whose continuation stays `Active`, if the reconcile read itself ALSO fails, the wire counter can remain transiently past the durable value while the session stays `Active` — the next propagating emit fail-closes and reconciles it. Custom-persistent-store-only, compound, and unreachable with `FileStore` (its `next_seqnum(outbound, false)` serves the in-memory mirror).
  - **Shutdown-race window (gate-b/r1 FQ-4b):** `store_cancelled` is excluded from the fail-closed gate (D7), so a store that returns `store_cancelled` while operational transmits but does not retain — confined to the shutdown drain race.
  - **gate-b/r1 FQ-1 scope note:** `is_persistent_retain_fatal`'s `[56,65)` range also newly catches `assign_outbound()`'s reuse of `store_seqnum_overflow` (`session.cpp` ~`:4488`, an in-memory counter overflow, not itself a store-retain failure) surfacing through `Session::send` — bringing it in line with the session-fatal-on-overflow disposition already enforced on other call paths regardless of store persistence.
- **L-008-3 — `FileStore` is unsupported on network/cluster filesystems, and `make()` does not detect or warn.** Cross-host correctness depends on effective advisory-lock semantics; NFS (no lock manager), SMB/CIFS, FUSE, and cluster FSes (GPFS/Lustre/GFS2/OCFS2) are outside the v1.0 contract — operators on shared storage must verify lock semantics out of band. **Status: deferred.** *(FR-013; spec §Edge Cases.)*
- **L-008-4 — `FileStore` does not encrypt persisted bytes; at-rest encryption is the OS's responsibility** (LUKS/dm-crypt, BitLocker). **Status: wontfix.** *(spec §Assumptions.)*
- **L-008-5 — Replicable / cross-process / cloud `MessageStore` is out of v1.0 scope.** **Status: deferred.** *(spec §Assumptions.)*
- **L-008-6 — Store-layer observability (structured logs / OTel spans on `store_io_failure`, flush stalls) is not shipped by 008; it routes through the 2k Logger/Tracer module.** **Status: follow-up.** *(spec §Observability.)*

## TLS policy core (011-tls-policy)

### Behaviors

- **B-011-1 — There is NO implicit default `SecurityProfile`; the `unset` sentinel is rejected at construction.** A `SecurityProfile` enum value MUST be chosen explicitly (`mtls_ca`, `mtls_pinned`, or the deprecated `one_way_ca`); the `unset = 0` sentinel is refused by `make_ssl_ctx_config` with `error::tls_invalid_security_profile` and by `Session::open` with `error::invalid_session_config`. A session can never silently open in some "default" trust posture. *(FR-013; spec §"Security profile".)*

- **B-011-2 — `one_way_ca` is `[[deprecated]]` on the enumerator itself.** The `[[deprecated]]` attribute is applied to the `one_way_ca` declaration (not merely a comment), so selecting it raises a compiler warning at every call site — a deliberate nudge toward an mTLS profile. *(FR-013; `[const §XII.5]`.)*

- **B-011-3 — Leaf-cert pinning (`mtls_pinned`) is a fixpp-original; no reference engine has it.** SHA-256-of-leaf-DER pinning with FIXS-RC1 §5 add-then-remove rotation is new to operators migrating from QuickFIX-cpp / QuickFIX/J / Fix8 — the reference-engine sweep found NONE of them implement leaf-cert pinning. Pin rotation is the ONLY mid-session-mutable TLS surface; every other TLS knob (SecurityProfile, cert_source) freezes at session open. *(Clarifications 2026-05-23; FR-006/FR-009/FR-015/FR-016.)*

- **B-011-4 — `verify_peer` enforces DoS caps with distinct named variants, not a "TLS failed" catch-all.** Peer certs are rejected at entry for oversized DER (`tls_cert_der_too_large`, default 16 KiB — refused BEFORE parsing), oversized RSA key (`tls_rsa_key_too_large`, default cap 8192 bits — `BN_mod_exp` cost is super-linear), or too many SAN entries (`tls_san_entries_exceeded`, default 64). These caps have no QuickFIX/QuickFIX-J/Fix8 analogue. Every distinct failure mode surfaces as its own `error::tls_*` variant. *(FR-019; SC-002/SC-006; spec Edge Cases.)*

- **B-011-5 — `verify_peer` short-circuits on the FIRST violation in a fixed 10-step order; cert expiry checks the effective clock, not wall-clock.** A multi-violation cert yields exactly ONE error variant per failed handshake (no aggregate report), evaluated DER-size → RSA-low → RSA-high → ECDSA-curve → chain-depth → SAN-count → X.509-version → expiration → pinning → cipher. Expiration is evaluated against the 007 effective-clock plugin, so a test/replay clock changes which certs are considered expired. *(FR-020/FR-020a; spec §Assumptions "verify_peer multi-violation ordering".)*

- **B-011-6 — An empty-but-non-null pinset under `mtls_pinned` fails EARLY at session-open, not per-handshake.** `make_ssl_ctx_config(mtls_pinned, empty_pinset, …)` returns `unexpected{tls_pin_empty_at_open}` and the session never opens — a distinct variant from `tls_pin_mismatch` so operator logs separate "fixpp-config problem" from "peer-cert problem". *(US3 scenario 5; Clarifications 2026-05-23; FR-025.)*

### Limitations

- **L-011-1 — No mid-session swap of `SecurityProfile` or `cert_source`; the supported pattern is close + reopen.** Both freeze at session open; there is deliberately no swap API. Only the `Pinset` is mid-session-mutable. **Status: wontfix.** *(FR-015/FR-016; spec Edge Cases "Operator attempts mid-session SecurityProfile swap".)*

- **L-011-2 — `add()` to a full pinset (default `max_pins = 16`) is refused — no silent eviction.** The cap is enforced; the operator must explicitly `remove()` an old pin before adding past the cap. The engine never decides which pin to drop. **Status: wontfix.** *(FR-010; US1 scenario 4.)*

- **L-011-3 — No PSK, no CRL/OCSP revocation, no mid-handshake pinset rotation, no dlopen plugin loading in v1.0.** PSK auth (T-012) and CRL/OCSP revocation infrastructure are post-v1; a pinset rotation landing mid-handshake never affects that handshake (picked up only at the next one). HSM/TPM/KMS/vault `cert_source` impls are user-side, not shipped. **Status: deferred.** *(FR-027; spec §Assumptions "Non-goals".)*

- **L-011-4 — Partial-read/torn-handshake DoS caps bound allocation, but a peer can still force the full 10-step validation cost per attempt.** The caps bound worst-case allocation/parse, but verification is run on every connecting peer; rate/connection limiting above the Transport is the operator's responsibility (acceptor saturation is out of scope per 012). **Status: deferred.** *(FR-019; cross-ref 012 spec Edge Cases "Service-side acceptor saturation".)*

---

## 2h transport — TCP / TLS / Listener / Mock (012-2h-transport)

### Behaviors

- **B-012-1 — `TCP_NODELAY` defaults to `true` (Nagle OFF), deliberately diverging from QuickFIX-cpp.** `Transport::Config::tcp_nodelay` defaults `true` and `so_linger_enabled` defaults `false`. This matches QuickFIX/J and Fix8 but diverges from QuickFIX-cpp's Nagle-ON default — the historical anomaly that production QFC configs near-universally override, because Nagle's 40 ms delay interacts badly with FIX heartbeats and single-tag updates. *(FR-029; Clarifications 2026-05-27 Q5.)*

- **B-012-2 — Reconnect mints a FRESH `Transport` per attempt; the dead instance is destroyed first.** The FSM never reuses a `Transport` across reconnect attempts — it destroys the dead one, then mints a new instance via the same `TransportFactory`. This matches the 20-year QuickFIX-cpp / QuickFIX/J / Fix8 convergent pattern. The factory (and its cached `SSL_CTX`) is long-lived; the Transport instances it produces are short-lived. *(FR-022/FR-028; Clarifications 2026-05-27 Q1; US2.)*

- **B-012-3 — `ReconnectPolicy::defaults()` is exponential-with-jitter; `defaults_quickfix_compat()` is the industry-canonical opt-out.** The default schedule is `[100 ms … 30 s]` (10 entries) with ±10 % jitter and `max_attempts = 10` (cumulative 73–89 s envelope) — a thundering-herd defense with no reference-engine analogue. Operators wanting classic behaviour call `defaults_quickfix_compat()` → single fixed 30 s interval, no jitter, no cap. Unbounded reconnect requires explicit `max_attempts = 0` (no implicit default). *(FR-019/FR-020; Clarifications 2026-05-27 Q2; SC of US2.)*

- **B-012-4 — `Listener::cancel()` stops new accepts but leaves already-handed-off Transports ALIVE.** Cancel does exactly three things: close the listening socket, complete any not-yet-resumed `async_accept` with `transport_accept_cancelled`, and leave already-produced `unique_ptr<Transport>` results untouched (ownership has passed; the listener has no handle). This diverges from QuickFIX/J's `stopAcceptingConnections()` which also kills managed sessions — that kill lives at the session layer, which is a separate concern in fixpp. Consumers needing "close everything I produced" MUST track their own Transports. *(FR-025; Clarifications 2026-05-27 Q4.)*

- **B-012-5 — A truncated TLS close (peer omits close-notify) surfaces as the DISTINCT `transport_read_truncated`, not `transport_read_eof`, and is NOT a hard error.** `close()` waits up to `tls_close_timeout` (1 s default) for the peer's close-notify; on timeout it completes with `transport_read_truncated` (mapped to a `warn`-level log by the 2k layer), preserving SC-006's distinct-named-variant rule rather than collapsing to EOF. *(FR-006; spec Edge Cases "TLS bidirectional close-notify hangs".)*

- **B-012-6 — In-flight exclusivity is an explicit API-level contract, not just strand defence.** A second concurrent `async_read_some` / `async_write` on the same Transport returns `transport_read_in_progress` / `transport_write_in_progress` immediately; a second `async_connect`/`async_handshake` returns `transport_already_connected`. Strand serialisation is defence-in-depth only. *(FR-007; spec Edge Cases.)*

### Limitations

- **L-012-1 — A cancelled `async_write` is NOT rolled back; a torn write/read drives session disconnect + recovery.** The contract is "durable then transmit; cancel only cancels transmit" — the persisted frame stays persisted, and the FSM treats a short write or partial read under cancellation as a torn I/O and recovers via ResendRequest. Bytes lost up to the cancellation point on a partial read are gone per ASIO's contract. **Status: wontfix.** *(FR-030; spec Edge Cases "Short write under cancellation" / "Partial read under cancellation".)*

- **L-012-2 — Acceptor saturation beyond 10²–10³ sessions/port is unbenched and unbounded; backlog overflow yields OS-level RST.** v1.0 target deployments are 10²–10³ sessions per acceptor; backlog overflow at higher accept rates returns TCP RST / connection-refused per OS behaviour — fixpp does not over-promise availability under saturated accept rates. **Status: deferred.** *(FR-024; spec Edge Cases "Service-side acceptor saturation" / US3 scenario 3.)*

- **L-012-3 — PSK is rejected at runtime (`transport_psk_unsupported`); no SHM/DPDK/Onload/UDP/Schannel/non-OpenSSL transport in v1.0.** The `TlsTransport` sub-interface reserves 4 of 5 pure-virtual slots for a future PSK hook without a major-version bump, but the v1.0 default impl rejects PSK config. Kernel-bypass transports and non-OpenSSL TLS backends are post-v1. **Status: deferred.** *(FR-017/FR-042; spec Edge Cases "Operator wires PSK config".)*

- **L-012-4 — No transport-internal write queue; outbound is depth-1, block-mode only (drop-oldest banned on message paths).** Outbound writes serialise on the session strand by construction; the strand IS the queue. There is no buffering and no drop-oldest fallback for app/session frames. **Status: wontfix.** *(FR-039; FR-042.)*

- **L-012-5 — IPv6 zone-id host strings are admitted but the full conformance corpus is deferred.** `fe80::1%eth0`-style hosts resolve via `asio::ip::resolver`, but exhaustive zone-id conformance testing is post-v1. **Status: deferred.** *(FR-018; spec Edge Cases "IPv6 zone-id host strings".)*

---

## Session reconnect FSM + recovery + CompID↔TLS binding (013-session-reconnect-binding)

### Behaviors

- **B-013-1 — A sequence-number gap on re-Logon now triggers recovery (ResendRequest) instead of fatal disconnect — amends 005 FR-008.** When inbound `MsgSeqNum > next_expected_inbound`, the FSM issues ResendRequest(2) and enters a transient `AwaitingResend` sub-state rather than Logging out. The too-LOW rule is unchanged: `MsgSeqNum < next_expected_inbound` with `PossDupFlag(43)≠Y` still Logs out with `session_seqnum_too_low`. This is the single biggest v1.0-GA recovery behaviour. *(FR-009; amends 005 FR-008; US1.)*

- **B-013-2 — CompID↔TLS-identity binding is enforced at Logon; this has NO reference-engine precedent.** The `peer_identity` from the TLS handshake is bound to the asserted CompID via an operator-supplied `CompIdAuthorizationPolicy`; a mismatch rejects Logon with `session_compid_unauthorized`. No QuickFIX-cpp / QuickFIX/J / Fix8 engine binds the cert to the application-layer CompID — without this, mTLS authenticates the cert but anything can assert any CompID. *(FR-019/FR-021; FIXS §4.4; US2.)*

- **B-013-3 — `CompIdAuthorizationPolicy` is allow-list only and default-deny: an empty policy rejects EVERY Logon.** The operator MUST enumerate every `{principal → compid_set}` binding before opening a session; a misconfigured deploy fails CLOSED, never open. There is no deny-list / hybrid mode. The empty-policy rejection is per-Logon at runtime (fail-closed), not at config-build time (not fail-fast). *(FR-023; Clarifications 2026-05-28 Q3; US2 scenario 6.)*

- **B-013-4 — Principal extraction is a fixed CN → SAN-DNS → SAN-URI → SHA-256-fingerprint order with EXACT byte matching.** First-non-empty wins; the order is invariant and not operator-overridable in v1.0. Matching is exact byte comparison — no case-folding, no NFC/IDNA normalization, no URI normalization, no trimming. A no-client-cert / `one_way_ca` peer falls through to the all-zero 64-char hex fingerprint principal, which fails closed unless the operator deliberately bound it. *(FR-019/FR-022; Clarifications 2026-05-28 Q2.)*

- **B-013-5 — In-process credential rotation: `reload_credentials()` swaps the cert without tearing down active sessions.** An atomic swap on the factory-internal `cert_source_slot_` means active sessions stay Active and only the NEXT reconnect handshake observes the new cert. A rotation racing an in-flight handshake DEFERS: the in-flight handshake completes on the OLD source (captured by shared_ptr value-copy), avoiding mid-handshake `SSL_CTX` mutation (OpenSSL UB). No reference engine supports in-process cert rotation — all three require a full restart. *(FR-030/FR-031/FR-033; Clarifications 2026-05-28 Q4; US4.)*

- **B-013-6 — `ResetSeqNumFlag(141)=Y` handling is operator-selectable; the default `bilateral_strict` diverges by being security-default.** Three modes: `bilateral_strict` (default — Logout with `session_seqnum_reset_mismatch` if the peer's response lacks 141=Y), `bilateral_lenient` (auto-mirror), `unilateral` (honour unconditionally). Default is bilateral_strict per the no-implicit-default principle and 2-of-3 industry convergence (QFC mirror, QFJ strict, Fix8 unilateral). Logout disconnect timeout defaults to 2000 ms, matching QuickFIX/J and diverging from QFC/Fix8's effectively-immediate close. *(FR-017/FR-008; Clarifications 2026-05-28 Q1/Q5.)*

### Limitations

- **L-013-1 — The `AwaitingResend` recovery sub-state is NOT surfaced as a SessionEvent; observe it via the `is_awaiting_resend()` accessor.** Entry/exit of AwaitingResend emit no `SessionEvent` variant (deferred as YAGNI for v1.0) — the FSM stays in Active with a transient flag. Operator tooling polls `ReconnectFsm::is_awaiting_resend()`. **Status: wontfix.** *(FR-009 / CHK041; spec §"Recovery sub-protocol".)*

- **L-013-2 — The inbound-held-message queue during recovery is UNBOUNDED; overflow is undefined for v1.0.** Inbound messages above `next_expected_inbound` are held in-memory in `ResendState::inbound_held` with no capacity ceiling; the window is bounded only by the `ReconnectPolicy` envelope and typical venue replay limits, not by a hard cap. **Status: deferred.** *(FR-009 / CHK003; spec §"Recovery sub-protocol".)*

- **L-013-3 — `tls_validation_failed` sub_reason strings are NOT ABI-stable; use the master-enum `code` for programmatic dispatch.** Of the 6 master-enum variants, `tls_handshake_failed` is a GROUPING variant collapsing 10+ rejection reasons surfaced only via the human-readable `sub_reason` string. Consumers MUST switch on `code` first and tolerate unknown sub_reason strings by logging verbatim (treating an unknown one as fatal is forbidden). **Status: wontfix.** *(FR-026/FR-027 / CHK010/CHK030; spec §"TLS validation outcome → SessionEvent".)*

- **L-013-4 — Deny-list / hybrid authorization modes, and per-binding principal-extraction overrides, are deferred to a later feature.** v1.0 ships allow-list-only with a fixed extraction order; adding modes/overrides is backward-compatible (one-way restriction). **Status: deferred.** *(FR-022/FR-023.)*

- **L-013-5 — No fixpp-managed zeroisation of `cert_source` private-key material; it lives for the shared_ptr lifetime.** A captured `std::shared_ptr<cert_source>` keeps PEM key bytes alive until the last strong-ref drops; fixpp performs no explicit zeroisation in v1.0. Operators with strict requirements must implement a `cert_source` whose destructor overwrites its buffer. **Status: deferred.** *(FR-033 / CHK032; spec §"2j ReloadCertSource control-plane".)*

- **L-013-6 — The `SessionEvent` ring is fixed at 16 entries, drop-oldest, and snapshots are single-thread-context only.** Capacity is compile-time fixed (not operator-tunable in v1.0); the 17th emit overwrites entry 0; the `recent_events()` span is a snapshot invalidated by the next strand emit, so consumers that outlive the synchronous context MUST copy. **Status: wontfix.** *(FR-035 / CHK002/CHK003.)*

---

## Live transport wiring — reconnect / identity / rotation events (014-transport-active-binding)

### Behaviors

- **B-014-1 — An authorization failure on the live RECONNECT path is reason-agnostic retry-to-cap, NOT terminal disconnect.** Unlike 013's open-Logon path (where an authorize failure drives terminal `Disconnected`), a reconnect-path failure of ANY cause — `make()` failure, connect, TLS handshake, OR off-list/absent-identity authorization — consumes exactly one attempt and retries per the backoff schedule. Only loop-exhaustion at the cap is terminal. There is no fail-fast and no distinct cap for the authorization case. *(FR-003/FR-007; Clarifications 2026-05-29; contract C1/C2.)*

- **B-014-2 — On the live initiator reconnect path, authorization uses the REAL handshake identity — no fabricated stand-in.** 014 swaps the identity source (013's test-seam/stub → real `handshake_result.peer_id`), making the already-fail-CLOSED mTLS gate operable: it now ADMITS an on-list peer instead of unconditionally fail-closing for want of any identity. This does not introduce fail-closed from scratch and opens no fail-OPEN hole. *(FR-006; SC-003; contract C2.)*

- **B-014-3 — `credentials_rotated` carries REAL leaf fingerprints; the first-ever credential load is NOT a rotation.** The event (emitted on the session strand at the next `drive_reconnect_attempt`, before `make()`) carries the real SHA-256 leaf fingerprints of old and new `cert_source`, replacing 013's all-zero stub. The first-ever load (`last_active_source_ == nullptr`) emits NO event — it just sets the rotation baseline. A no-op rotation (`old == new`) still emits (not suppressed). *(FR-009/FR-010/FR-011; contract C3.)*

- **B-014-4 — A non-TLS or null transport from the factory is runtime-recoverable, not a crash.** The FSM reaches the TLS specialization via a single `dynamic_cast<TlsTransport*>` with a null-check; because `TlsTransport` inherits virtually from `Transport`, a `make()` returning a base/non-TLS transport yields `nullptr` → the attempt is counted and the loop continues. *(FR-001; contract C1.)*

### Limitations

- **L-014-1 — Acceptor sessions do NOT drive a reconnect loop; reconnect is initiator-side only.** Acceptors re-accept rather than reconnecting (a permanent FIX design fact). **Status: wontfix** for the reconnect asymmetry; the live acceptor accept→handshake→`authorize()` production path itself shipped in 015 (it was 014's deferred boundary, not 014). *(spec §Assumptions "Reconnect is initiator-side"; Out of Scope.)*

- **L-014-4 — `error::session_seqnum_too_high` (slot 120) replaces the vestigial slot-74 stand-in; the retired slot remains a permanent numeric hole.** The seqnum manager's too-high branch now returns a dedicated, semantically-correct code instead of reusing slot 74 (`session_test_request_unanswered`); error slots are append-only and retired slots are never reused. **Status: follow-up.** *(FR-016; contract — error-enum append `error::session_seqnum_too_high = 120`.)*

---

## Runtime engine (015-runtime-engine)

### Behaviors

- **B-015-1 — Initiator emits its Logon AFTER connect (connect-then-Logon).**
  An engine-managed initiator does **not** emit the initial Logon at `open()`; the Logon
  is sent only once the transport is live (post-connect, post-handshake), over the
  rebound outbound sink. This matches QuickFIX-cpp (`setResponder()` → `generateLogon()`)
  and Fix8 (`connect()` → `send(generate_logon())`); fixpp's pre-015 per-session-direct
  model (emit-at-open) was the outlier. *(FR-003; data-model E-1a; gated by
  `SessionConfig::engine_managed`, default `false` so non-engine sessions are unchanged.)*

- **B-015-2 — `Engine::stop()` closes live transport sockets, and is mandatory before
  destruction.** `stop()` emits total-cancellation **and** closes each session's live
  transport socket, because total-cancel alone does **not** break a blocked idle
  `async_read_some` on an established TLS session (no peer EOF). `~Engine()` is a strict
  `assert(stopped())` — you **must** `co_await stop()` before destroying an `Engine`,
  **even if it was never started**. *(FR-011; data-model E-7; T018; see
  `[[feedback_engine_stop_must_close_transports_total_cancel_insufficient]]`.)*

- **B-015-3 — `lookup()` returns null for a registered-but-not-yet-open session.**
  Sessions are constructed **lazily** inside their accept/connect loop, not at
  `register_session` or `start()`. So immediately after `start()`, and for an acceptor
  with no peer yet, `lookup(id)` legitimately returns `nullptr` — null is not an error.
  *(data-model E-7 "open() sequencing"; Gate A New-3.)*

- **B-015-4 — Acceptors resolve sessions by reversed CompID against a static registry.**
  An inbound Logon is matched by reversing its `SenderCompID(49)`/`TargetCompID(56)`
  against the registered `SessionId`s (mirrors QuickFIX `lookupSession(..., true)` /
  QuickFIX/J `getReverseSessionID()`). No dynamic session provider. *(FR-005/006;
  data-model E-2; R2/R4.)*

### Limitations

- **L-015-1 — One connect+pump per initiator; multi-cycle reconnect-respin is not
  implemented.** The engine drives a single connect → handshake → Logon → read-pump per
  initiator session. `Session::close(close_mode::terminal)` is permanent
  (`lifecycle::closed_drained`), so the same `Session` cannot reconnect; a transport drop
  on an established session is session-fatal (→ `Disconnected`), not auto-respun.
  **Status: deferred** (needs fresh-Session-per-reconnect or a lifecycle re-open redesign
  — its own future feature). *(data-model E-1a; Clarifications 2026-05-31.)*

- **L-015-3 — Bounded below the Phase-5 service wrapper (scope).** No config-file
  parsing, no `Application` user-callback ecosystem, no store/log factory abstractions,
  no C-ABI / control-plane / observability / pybind, and no user sink for inbound
  *application* messages (the read-pump delivers every frame to the admin/session layer).
  **Status: wontfix for 015** (intentional scope bound). *(FR-013; spec "Scope guard".)*

- **L-015-4 — A `Session` must outlive all work dispatched on its executor (lifetime
  contract; not enforced at `~Session`).** `Session::dispatch_app_callback` posts a
  handler that captures `this`, and in debug/sanitizer builds the re-entrancy
  `dispatch_guard` dtor stores to `in_dispatch_` *after* the user callback returns.
  Destroying the `Session` while a dispatched callback is queued/running (incl. that
  trailing store) is a use-after-scope / data race. **Production is safe today:**
  `dispatch_app_callback` has no production callers (the `Application` app-callback path is
  Phase-5, L-015-3), and the Engine drains every session on teardown (`stop()` →
  `close(terminal)` + join-before-registry-clear; `~Engine()` asserts `stop()`). The hazard
  is only reachable by **bypassing the Engine** — constructing a raw `Session`, dispatching
  on it, and destroying it without draining `exec_` (the seam tests; fixed in
  `test_executor_compat.cpp run_combo` by a guard-less post onto `executor()` + wait).
  Unlike `~Engine()`, `~Session()` does **not** assert a drained precondition. **Status:
  follow-up — Phase-5 app-callback wiring MUST drain dispatched app work on session
  teardown** (a shared keepalive, cf. the 014 detached-write fix), and should consider a
  debug `~Session` guard once the precise "no in-flight executor work" invariant is
  trackable. *(`include/fixpp/session/session.hpp dispatch_app_callback`; CI-TSan, 2026-06-01.)*

## Interop harness (016-interop-harness)

### Behaviors

- **B-016-1 — The thorny corpus reconciles to the FIX spec, not to a reference
  engine; a fixpp-vs-engine divergence is encoded against the spec mandate (FR-018).**
  Where a reference engine special-cases behavior fixpp does not, the corpus encodes
  fixpp's actual spec-defensible behavior and documents the divergence (disposition
  `pass`, not a known-limitation). Worked example: an inbound **Logout carrying a
  too-high MsgSeqNum** does **not** disconnect on fixpp (as QuickFIX-J#750 chose) —
  fixpp's uniform FIX-SL §4.5.3 gap-recovery takes precedence (ResendRequest, stays
  Active), recovering the gap before the Logout is dispatched; only a too-low Logout
  disconnects. *(`tests/interop/thorny/recovery/qfj-750-logout-seqnum-mismatch_test.cpp`;
  `tests/interop/thorny/CORPUS-INDEX.md` C-004; FR-018.)*

### Limitations

- **L-016-2 — Live interop is all-TLS with a server-auth `one_way_ca` baseline;
  mutual-certificate mTLS is deferred to v1.1.** fixpp ships TLS-only (no plaintext
  transport) *for live interop cells*, so every live cell runs over TLS; the v1.0 baseline trusts a
  counterparty server cert (`one_way_ca`). App-layer client-cert ↔ CompID mutual mTLS
  (`mtls_ca`) is `deferred:v1.1-mtls`. **Status: wontfix for v1.0** (intentional scope
  bound). *(FR-025; `tests/interop/happy/MATRIX.md`.)*
  **NOTE (2026-06-17, 043):** The general claim "fixpp ships TLS-only" no longer holds — 043 added
  `asio_plain_transport` behind the loud `insecure_plain_tcp` opt-in. The live-interop MATRIX still
  runs all-TLS (no plaintext interop cells planned); this limitation's scope is the 016 interop matrix.

---

## Async Logger + OTel Observability (017-log-otel)

### Feature Catalogue Rows (done)

| Row | Title | Status | /specify | PR | Tests |
|---|---|---|---|---|---|
| LOG-001 | Zero-alloc async MPSC logger — producer/consumer ring, `Level`/`Category` filtering, drain thread, `Logger::enqueue()` | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_compile_cutoff_zero_alloc.cpp` (TS-1: dual-gate zero-alloc, fill 10/50/95%), `tests/log/test_overflow_drop_newest.cpp` (TS-2: drop_newest + TSan), `tests/log/test_block_overflow_raw_thread.cpp` (TS-3: block mode raw thread), `bench/log/log_enqueue.cpp` (TS-9: mean ≤ 50 ns gate + p99/p999) |
| LOG-002 | `Sink` interface (4 pure-virtual: `open`/`emit`/`flush`/`close`) + `FileSink` (rotation+fsync) + `SyslogSink` | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_file_sink_rotation.cpp` (TS-4: rotation + archived-only keep-count + TSan), `tests/log/test_file_sink_async_fsync.cpp` (TS-5: fsync on drain thread) |
| LOG-003 | Trace-correlated log records — `trace_id`/`span_id` carried per `Record`; `FIXPP_SLOG`/`FIXPP_ELOG`/`FIXPP_LOG0` macros (no `thread_local`) | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_trace_correlation.cpp` (TS-6: SLOG/ELOG/LOG0 macro tier verification; SlogTimestampIsWallClock + ElogTimestampFromMockClock — ELOG mock-clock / SLOG wall-clock) |
| LOG-004 | Compile-time level cutoff (`FIXPP_LOG_MIN_LEVEL` + `if constexpr`) + runtime category bitmask filter | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_level_and_category_filter.cpp` (TS-8: combined compile+runtime filter → `filter_count()==1`, `drop_count()==0`) |
| OBS-001 | `SessionSpans` RAII helper — lifecycle span + `ParseSpan`/`StoreSpan`/`DispatchSpan` children with explicit-parent OTel context (no `Scope`/`thread_local`) | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/otel/test_session_spans.cpp` (TS-12: session+parse spans, explicit parenting, cross-thread span_id) |
| OBS-002 | `TracerProvider`/`MeterProvider` RAII wrappers + `PrometheusExporter`/`OtlpMetricExporter` dual-reader | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/otel/test_dual_metric_export.cpp` (TS-11: counter readable via `:9464` + OTLP push), `tests/otel/test_engine_close_teardown.cpp` (provider init/shutdown/no-op fallback) |
| OBS-003 | `OtlpLogSink` — `Sink` impl translating `Record → opentelemetry::logs::LogRecord` via `BatchLogRecordProcessor` (non-blocking; no double-write; capped retries) | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_otlp_log_sink.cpp` (TS-10: single-write path, severity/trace/body match) |

### Behaviors

- **B-017-1 — `overflow_policy::drop_newest` preserves the oldest in-flight record.**
  When the MPSC ring is full, the producer detects the full ring and drops the record it
  is *about to enqueue* (newest = just-arriving), not an older in-flight slot. This means
  the oldest records are always preserved with an exact `drop_count()`. The stale-read
  of `read_sequence_` under `relaxed` ordering can only cause an *early* drop — safe for
  `drop_newest`. *(FR-003/FR-004; data-model §overflow_policy; TS-2.)*

- **B-017-2 — `block` overflow mode is prohibited from session-strand coroutines.**
  `overflow_policy::block` makes the producer spin-yield until a ring slot is available.
  This pins the executor OS thread at the enqueue site, which is equivalent to holding
  `std::mutex` inside a coroutine — explicitly prohibited by `[const §XI.3]`. A debug
  `FIXPP_ASSERT` fires if `block` is used from a detected session-executor thread.
  `block` is safe only from dedicated non-coroutine producer threads (e.g. background
  control-plane threads not sharing the session executor). *(FR-004; contracts/log-core.md;
  data-model §overflow_policy.)*

### Limitations

- **L-017-1 — The three log macros (`FIXPP_SLOG`/`FIXPP_ELOG`/`FIXPP_LOG0`) take an
  explicit `logger_ptr` first argument — a deliberate deviation from FR-013's no-logger
  signature.** FR-013 specifies the three context-tier macros with no explicit logger
  parameter; the implementation instead requires an explicit `Logger*` first arg because
  loggers are per-engine (there is no global logger per `[const §XIII.1]`), and a
  no-arg form would require a `thread_local` or implicit injection mechanism that violates
  `[const §XIII.3]`. The explicit-logger form is the public API for v1.0; operators must
  hold and pass the logger pointer from their engine/session context.
  **Status: wontfix for v1.0** (deliberate; `thread_local` banned). *(FR-013; [const §XIII.3];
  data-model §Trace-correlation-macros.)*

- **L-017-2 — The MPSC ring advances `read_sequence_` AFTER the drain copies the record
  out of the slot, not before.** This means a slot is held for the full duration of the
  drain's `Sink::emit()` fan-out, costing at most 1 of 65,536 slots per in-flight drain
  iteration. The record is fully copied before the slot is released, so there is no
  use-after-free risk. The alternative (Disruptor copy-then-free) advances the sequence
  before fan-out; our variant is simpler and the 1/65,536 overhead is negligible.
  **Status: wontfix** (defensible design choice; single-consumer ring). *(logger.cpp drain
  loop; data-model §Logger ring invariants.)*

- **L-017-3 — `OtlpLogSink` lives in a separate `fixpp_log_otlp` target so the base
  `fixpp_log` library stays OTel-free.** Users who only need file/syslog logging do not
  pull in the OpenTelemetry C++ SDK. `fixpp_log_otlp` is an opt-in link target.
  **Status: wontfix for v1.0** (intentional layering). *(FR-018; CMakeLists.txt; [arch §4.7].)*

- **L-017-4 — TS-13 backend-selection disposition is PROVISIONAL; the quill comparison
  is deferred behind `FIXPP_LOG_SPIKE_QUILL=ON`.** The own lock-free MPSC ring is the
  v1.0 shipping candidate (`[arch §9.3]`; `[2k §1.2]`). TS-13 was executed and recorded:
  on WSL2 debug (Clang debug build) at 50% fill with 4 producers over 10M records, the
  own-ring p99 is ~1,062 ns and p999 is ~1,793 ns; Criterion A (zero-alloc under
  mallocnesia, 10% and 50% fill) passes (exit 0). The Criterion-B comparison (p99 ≤ 50 ns
  vs quill 11.x at 50% fill on reference CI hardware) is deferred — it is a **recorded,
  non-blocking metric** that does NOT gate v1.0 delivery. The backend is swappable behind
  the identical `Logger` facade without any public-API change.
  **Status: deferred** (Criterion B comparison, non-blocking). *(FR-021; [2k §1.2]; [arch §9.3];
  `.specify/decisions/017-log-otel-verify.md` TS-13 record; `bench/log/log_spike.cpp`.)*

- **L-017-5 — `SessionSpans` is a standalone helper; live session-FSM wiring is deferred
  to the future session-module feature.** 017 ships `SessionSpans` + parse/store/dispatch
  child-span types in the `otel` module, verified by TS-12 against a test/mock session.
  Constructing `SessionSpans` in the real session-FSM open path and emitting spans from
  the live message-processing coroutine is **out of scope for 017** (clarified boundary,
  scope question 1). The hand-off point is anchor §11 of `.specify/2k-log-otel.md`.
  **Status: deferred** (future session-module feature). *(FR-016; spec.md Clarification 1;
  [2k §11]; `tests/otel/test_session_spans.cpp`.)*

- **L-017-6 — `overflow_policy::drop_newest` is the only supported overflow mode for
  session-strand producers; `block` mode is prohibited from coroutine contexts.** The
  `block` policy spins until a ring slot opens, which pins the executor thread. This
  violates `[const §XI.3]` (no mutex/spin in coroutine context) and is equivalent to
  holding a `std::mutex` inside a `co_await` chain. A debug `FIXPP_ASSERT` fires if
  `block` is used from a detected session-executor thread. Operators who need guaranteed
  delivery (no drops) from a non-coroutine thread may use `block` on a dedicated
  non-session OS thread.
  **Status: wontfix** (constitutional constraint `[const §XI.3]`). *(FR-004;
  data-model §overflow_policy; B-017-2 is the positive-behavior counterpart.)*

- **L-017-8 — The `overflow_policy::block` session-strand debug guard is not implemented
  (T033/T034 deferral).** `contracts/log-core.md` and `logger.hpp` specify that a debug
  `FIXPP_ASSERT` fires if `block` is used from a session-executor thread. This guard is
  **not yet implemented** because `Logger` is intentionally session/engine-ref-free
  (`[2k §4.3]`): it holds no session executor reference, so there is no cheap hook to
  detect "am I on a session-executor thread". The raw-thread path (the only production
  use today) is correct. Session-strand misuse of `block` is a documented **caller
  obligation**, not a runtime-enforced invariant in v1.0. T033/T034 track a follow-up
  approach (e.g. a `LoggerConfig` flag or an injected `is_session_thread` predicate from
  the caller) that keeps Logger ref-free while enabling the guard.
  **Status: deferred** (T033/T034). *(FR-004; `[2k §4.3]`; `src/log/logger.cpp` block
  path comment; L-017-6 is the caller-obligation counterpart.)*

- **L-017-7 — `FIXPP_SLOG` uses `system_clock::now()` (wall-clock) for its timestamp;
  deterministic mock-clock control applies only to `FIXPP_ELOG`.** FR-006's "effective
  clock" determinism is scoped to the Engine-tier macro `FIXPP_ELOG`, which reads
  `engine.clock()->now()` (the injected `EngineConfig::clock`). `FIXPP_SLOG` carries
  only the caller's `trace_context` (trace_id + span_id) — there is no clock field in
  `trace_context` because adding one would widen the SLOG call-site API (`[2k §4.3]`
  locked surface). `FIXPP_LOG0` (Tier 3, zero context) is wall-clock by design. In
  practice, wall-clock timestamps are monotone-ish for log ordering; the gap only
  matters for test determinism, not production correctness. Threading the effective
  clock into SLOG is a future extension (see T033/T034).
  **Status: deferred** (T033/T034; would require macro-signature change and a clock
  field in `trace_context`; non-blocking for v1.0). *(FR-006; `[2k §4.3]`;
  `contracts/log-core.md` LOG-003 macro contract; `logger.hpp` FIXPP_SLOG comment;
  `tests/log/test_trace_correlation.cpp` `SlogTimestampIsWallClock`.)*

## Application callback layer (019-app-callbacks)

### Feature Catalogue Rows (done)

| Row | Title | Status | /specify | PR | Tests |
|---|---|---|---|---|---|
| APP-001 | Application callback interface (`onCreate`/`onLogon`/`onLogout`, `fromAdmin`/`fromApp`, `toAdmin`/`toApp`) + any-thread `Engine::send` | **done** | `019-app-callbacks` | (Gate B pending) | `tests/session/test_application_{inbound,business_reject,outbound,lifecycle,strand,throw}.cpp` + `test_019_g2_enablement_witness.cpp` |
| OSS-005 | QuickFIX-style Application callback interface (return-value reject/veto divergence) | **done** | `019-app-callbacks` | (Gate B pending) | see APP-001 |
| A-014 | `BusinessMessageReject(35=j)` builder (`build_business_message_reject`, emitted on `fromApp`-reject) | **done** | `019-app-callbacks` | (Gate B pending) | `tests/session/test_application_business_reject.cpp` |

### Behaviors

- **B-019-1 — Reject/veto is signalled by return value, never by an exception.** A
  `fromApp`/`fromAdmin` callback returns `unexpected(error)` to reject (→
  `BusinessMessageReject(35=j)` / session `Reject(35=3)` respectively); a `toApp` callback
  returns `unexpected(error::app_do_not_send)` to veto an outbound app message (DoNotSend),
  or another `error` to abort the send with that error surfaced to the `Engine::send`
  caller. `toAdmin` is inspect-only (admin messages are always sent). This is a deliberate
  divergence from QuickFIX's exception-based callback API to fit the fixpp no-throw house
  style (`[const §XV.9]`). *(FR-005/007/008/015; data-model reject/veto table; research D1/D2.)*

- **B-019-2 — A throwing user callback is a fatal user-contract violation → terminal
  session close.** Because every normal outcome (accept/reject/veto) is a return value, an
  exception escaping any of the 7 callbacks is unexpected: the engine catches it at the
  dispatch boundary, clears the re-entrancy guard, terminal-closes the session, and records
  `error::app_callback_threw`. The exception never reaches engine internals. *(FR-011;
  research D5; `tests/session/test_application_throw.cpp`.)*

- **B-019-3 — `Engine::send` is any-thread-safe and posts onto the per-session strand.**
  `co_await engine.send(id, payload)` looks the session up (capturing a `shared_ptr<Session>`
  keepalive that outlives the post — the 014 detached-write UAF class), posts onto the
  session's `exec_`, runs `toApp`, then the durable-before-transmit `Session::send` path.
  The awaited result carries the veto/store/write outcome (natural backpressure — no
  silent-drop queue, `[const §XV.15]`). A re-entrant `send` from inside an on-strand
  callback is enqueued behind the current dispatch (no deadlock). *(FR-006; research D3/D6;
  `tests/session/test_application_strand.cpp`.)*

### Limitations

- **L-019-1 — Outbound interception is inspect + veto only; in-place outbound message
  MODIFICATION is deferred to a later Phase-5 slice.** `toApp` may inspect the outbound
  `MessageView` and veto it (`app_do_not_send`); `toAdmin` may inspect it. Neither can
  MODIFY/stamp the outbound message in place this slice — a mutable outbound builder/view
  is a large, separable design (it would expose the `Writer`/builder mid-emit) and is not
  required for the G2 round-trip (the originator builds the full payload passed to
  `Engine::send`). The user stamps fields by constructing the payload before `send`.
  **Status: deferred** (mutable outbound interception, a later Phase-5 slice). *(FR-007/008;
  research D1; spec.md §FR-007/008 forward-pointer; contracts/application-interface.md
  §Out of contract.)*

- **L-019-2 — A single `Application` is registered per `Engine` (no per-session override).**
  `EngineConfig::application` holds one `Application` invoked for all of the engine's
  sessions, with the `SessionId` passed per call (the QuickFIX-C++/J + Fix8 model). A
  per-session `Application` override is out of scope for this slice.
  **Status: deferred** (per-session override, a later Phase-5 slice). *(FR-002;
  Clarifications 2026-06-03 Q2; data-model §EngineConfig::application.)*

- **L-019-4 — `toApp` is an ORIGINATE-path tap; retransmissions answering a peer
  `ResendRequest` are NOT surfaced to `toApp` and cannot be app-vetoed or app-gap-filled.**
  On a `ResendRequest`, `replay_outbound_range_` (`src/session/session.cpp`) re-transmits each
  stored **application** frame verbatim via `build_replay_frame` (stamping `PossDupFlag(43)=Y`
  + `OrigSendingTime(122)` per 037) with **no `toApp` call**; absent slots and **admin** frames
  are folded into a `SequenceReset-GapFill` (which *does* fire `toAdmin`). This wire output is
  FIX-conformant — retransmitted application messages carry `43=Y`, admin/absent ranges are
  gap-filled — and is live-proven against QuickFIX-cpp and QuickFIX-J. The divergence is at the
  **callback** layer only: QuickFIX calls `toApp` for every resent message and converts an
  application `DoNotSend` into a `SequenceReset-GapFill` over that slot; fixpp offers no
  equivalent. Operator consequence: an `Application` using `toApp` as a compliance/audit tap
  will **not observe retransmissions**, and cannot elect to gap-fill a stale application message
  on resend. **Why this is a deferral, not a quick wiring:** the originate-path `toApp` veto
  means *drop the frame and consume no outbound seqnum* (L-019-1 / B-036-1) — a semantic that is
  invalid on the resend path, where the seqnum is already consumed and the peer is waiting for
  it; reusing it would punch a sequence hole and break the peer's recovery. Correct resend
  interception must re-interpret a veto as `DoNotSend → emit a GapFill over that slot`, a
  separable Phase-5 slice, not a config flag. **Status: by-design / deferred** (the wire
  behavior is conformant today; resend observability + app-driven `DoNotSend`-on-resend gap-fill
  is a future Phase-5 slice). *(scopes 019 FR-007/008 `toApp`; resend path =
  027/037 `replay_outbound_range_`; Fable `2.4-half-restructure.md` §4 /
  `[[feedback_half_restructure_symmetric_api]]`.)*

## G2 Business Messages (020-g2-business-messages)

### Feature Catalogue Rows

No new catalogue rows. A-001 (NewOrderSingle 35=D) and A-006 (ExecutionReport 35=8)
**stay `backlog`** with a partial-G2-interop-evidence gap-note (NOT a closure) — see the
`## Application Messages — Order Management` blockquote in `spec/feature-catalogue.md` and
the A-001/A-006 gap-notes in `spec/coverage-index.md`. Mints `fixpp::core::error`
enumerator `app_payload_malformed = 131` (`tests/core/test_019_error_completeness.cpp`
forward-boundary now at slot 132; exact-SET ownership of 131 by the 020 completeness gate).

### Behaviors

- **B-020-1 — Application sends now place MsgType(35) at wire field-3 with a digit-only
  BodyLength.** `Session::send_impl` (the path under `Engine::send` and `Session::send`)
  re-frames the outbound application frame so the first three wire fields are
  `8=BeginString`, `9=BodyLength`, `35=MsgType` in that order, and `9=` is digit-only /
  unpadded (`.specify/2b-wire.md`). Previously MsgType landed 7th and `9=` was zero-padded
  (`9=000045`) — accepted by fixpp's lenient parser but rejected by QuickFIX/J/cpp + Fix8.
  This corrects 019's latent opaque-path framing for ALL app sends, not just the typed
  builders. *(FR-004a; research.md D1; data-model.md INV-1; B-020 send-path framing.)*

- **B-020-2 — Application send payloads are validated fail-closed before any seqnum/transmit.**
  `Engine::send` / `Session::send` copy arbitrary opaque app bytes; `send_impl` now validates
  the payload BEFORE stamping SendingTime, peeking/assigning a seqnum, or storing/transmitting:
  it must lead with exactly one `35=` MsgType field and carry no embedded session header/trailer
  tag (`8/9/34/49/52/56/10`); empty, no-leading-`35=`, duplicate-`35=`, or embedded-tag payloads
  are rejected with `error::app_payload_malformed` (131) and consume NO seqnum. Two additional
  defensive floors are enforced (gate-b/r1 RC#2): (a) the payload must **end with SOH**
  (`pv.back() == '\x01'`) so the final field is terminated before checksum append; (b) the
  MsgType value must be **non-empty** (`first_soh > 3`, i.e. at least one byte between `35=`
  and the first SOH). Both return `app_payload_malformed` with no seqnum consumed. *(FR-016;
  data-model.md INV-8; research.md D1 opaque-payload validation.)*

- **B-020-3 — Typed minimal builders for NewOrderSingle / ExecutionReport.**
  `build_new_order_single` (Limit-only, OrdType=2) and `build_execution_report` (fully-filled
  reply: ExecType='F'/OrdStatus='2'/LeavesQty=0/CumQty=OrderQty/AvgPx=Price) are `noexcept`,
  allocation-free (stack-scratch-then-copy, INV-4 atomicity), emit the app body only (no
  session header/trailer tags), and serialize numerics via `decimal_t::format` (canonical,
  locale-independent). The READ side consumes the already-generated `fixpp::v44` flyweights.
  *(FR-001..008; data-model.md E1/E2; INV-2/3/4; `[const §VIII.5]`.)*

### Limitations

- **L-020-1 — Minimal field set only; full FIX 4.4 field/group coverage is deferred.**
  The 020 builders emit only the minimal NOS/ExecRpt fields (NOS: 11/55/54/38/40/44/60;
  ExecRpt: 37/17/150/39/55/54/151/14/6). NewOrderSingle is **Limit-only** (OrdType fixed to
  `2`, Price always required); Market orders, optional fields, and repeating groups are not
  supported. The full-coverage path is the codegen *writer-emitter* (which would emit writers
  for the entire message set). **Status: deferred** (FR-015a — codegen writer-emitter).
  *(FR-015a; research.md D3/D5 + "Forward obligations"; Deferred-work registry in CLAUDE.md.)*

- **L-020-2 — FIX 4.4 only; all-protocol-version coverage (4.2 / 5.0SP2 / FIXT.1.1) is
  scheduled post-v1.0.** The typed builders + live interop cells negotiate FIX 4.4 only
  (matching 016/018 and the generated `fixpp::v44` flyweights). NewOrderSingle/ExecutionReport
  over 4.2 / 5.0SP2 / FIXT.1.1 (interop roadmap G4 axis) are not covered. **Status: deferred**
  (FR-015b — scheduled post-v1.0). *(FR-015b; research.md D8 + "Forward obligations";
  Deferred-work registry in CLAUDE.md.)*

- **L-020-3 — ExecType/OrdStatus enum values are not validated against the FIX 4.4 enum set;
  only printable, non-control ASCII (0x20–0x7E) is enforced.** The builders accept any
  printable char for `exec_type`/`ord_status` (e.g. `'Z'` succeeds). Caller-supplied chars
  are printable-floor-checked only; full FIX 4.4 enum-set validation (e.g. restricting
  `exec_type` to `'0'/'1'/'2'/'3'/'4'/'5'/'6'/'7'/'8'/'B'/'C'/'D'/'E'/'F'/'G'/'H'/'I'/'J'`)
  is deferred (FR-015a). The `exec_type='F'` / `ord_status='2'` (fully-filled) contract is a
  caller/harness obligation (data-model.md E2/E3), not a builder precondition.
  **Status: deferred** (FR-015a). *(gate-b/r1 RC#4; contracts/business-messages.md §Conventions.)*

<!-- 021-inbound-possdup-origsendingtime -->

- **B-021-1 — Inbound possible-duplicate (`PossDupFlag(43)=Y`) handling is tolerant and
  wire-conformant.** A too-low inbound message (`MsgSeqNum < expected`) bearing `43=Y` and a
  valid `OrigSendingTime(122)` is TOLERATED: the session stays `Active`, the expected inbound
  seqnum is NOT advanced, and the message is not re-applied (Arm A). Independently, any `43=Y`
  non-`SequenceReset(35=4)` inbound (any seqnum, including at-expected) is VALIDATED for
  OrigSendingTime: missing `122` → session `Reject(35=3)` with `RefTagID(371)=122`,
  `SessionRejectReason(373)=1` (RequiredTagMissing), session survives (Arm C); `122` present
  but unparseable → same Arm C disposition (`Reject 371=122/373=1`, session survives —
  an unusable `122` is treated identically to an absent one); `122 > 52` strict →
  `Reject(35=3)` `371=122`, `373=10` (SendingTimeAccuracyProblem) + `Logout` +
  `Disconnected` (Arm D). `122 == 52` is accepted. Validation runs AFTER the too-high arm
  (a forward-gap `43=Y` still issues `ResendRequest`, matching QuickFIX-cpp v1.16.0 +
  QuickFIX-J 3.0.1) and BEFORE the too-low/at-expected disposition. `SequenceReset(35=4)+43=Y`
  is exempt from the `122` requirement (Arm E — routed to the existing reset/gap-fill path).
  **Status: shipped** (021, updated gate-b/r1). *(FR-001..FR-007; data-model.md §1 INV-1/3/4;
  research.md D1/D4/D5/D6; engine-parity placement = user decision 2026-06-04.)*

- **B-021-2 — Guard-3 `SendingTime(52)` MaxLatency validation precedes Stage-1 possdup
  (matches QFJ `verify()` ordering).** Guard-3 (inbound SendingTime accuracy check, 120 s
  default threshold) runs BEFORE the Stage-1 possdup block. A `43=Y` replay carrying a
  stale or unparseable `SendingTime(52)` is therefore killed by Guard-3 — it emits
  `Reject(35=3, 371=52, 373=10)` + `Logout` + `Disconnect` — and never reaches Arm A.
  FR-001's "too-low `43=Y` must not disconnect" guarantee implicitly assumes a
  well-formed, recent `52` (which is the realistic retransmit case: a genuine replay
  re-stamps `52=now`, carrying only the original time in `122`). This ordering is
  byte-faithful to QFJ `Session.java` `isGoodTime`@1821 before `validatePossDup`@1843.
  **Status: shipped** (021, documented gate-b/r1). *(Guard-3 at `session.cpp:1670`;
  Stage-1 at `session.cpp:1860`.)*

- **L-021-1 — App possible-duplicate disposition is configurable (default DROP); admin
  duplicates are ALWAYS ignored.** For a validated too-low possible-duplicate APPLICATION
  message, `SessionConfig::redeliver_poss_dup` (default `false`) governs disposition: `false`
  drops it (no `Application::fromApp`); `true` redelivers it to `fromApp` (the replayed frame
  carries `43=Y`, so the callback sees it flagged possible-duplicate). ADMINISTRATIVE duplicates
  are ignored unconditionally, even when the knob is `true` — this asymmetry is operator-visible.
  Neither disposition advances the seqnum or disconnects. This is a PROTOCOL duplicate-discard
  (the message was already processed once; `MsgSeqNum < expected` proves it) — NOT a
  `[const §XV.15]` backpressure/queue drop; the sequence contract is exactly preserved.
  **Status: shipped** (021, FR-010). *(data-model.md §2; research.md D2; distinguish from
  [const §XV.15].)*

- **L-021-3 — The PossDup-replay live interop cells run against QuickFIX-J only; the
  QuickFIX-cpp half is waived with rationale (SC-004 not claimed fully met).** Witnessing
  fixpp's inbound PossDup tolerance live requires the counterparty to INJECT a too-low `43=Y`
  frame on command. Only QuickFIX-J can: its public `Session.send(message, allowPosDup=true)`
  preserves `PossDupFlag(43)`/`OrigSendingTime(122)`. QuickFIX-cpp v1.16.0 cannot —
  `Session::send()` unconditionally strips `43`/`122`, `sendRaw` is private, there is no
  `AllowPosDup` setting, and a too-low replay is not a behaviour a healthy QuickFIX-cpp session
  ever produces (it resends only the gap ranges it is asked for, never an already-seen frame).
  The four `PD-QFcpp-*` cells are therefore `deferred:qfcpp-no-possdup-injection` (status `n/a`)
  in `tests/interop/cell_results.yaml`. fixpp's tolerance is a RECEIVE-path property — the
  injected bytes are identical regardless of the sending engine — so it is proven LIVE against
  QuickFIX-J 3.0.1 (the four `PD-QFj-*` cells: replay-survives + malformed-dup-rejected ×
  initiator + acceptor, green under `normal` + `asan-ubsan`) and in-process by
  `test_inbound_poss_dup_tolerance.cpp` / `test_inbound_poss_dup_validation.cpp`. Consequently
  **SC-004's QuickFIX-cpp clause is waived-with-rationale, not met**; SC-001/SC-002 are
  satisfied by the QuickFIX-J live cells + the unit suite. **Status: shipped + waived**
  (2026-06-11, Item-1 live sweep). *(021 SC-001/SC-002/SC-004;
  `cell_results.yaml` deferred:qfcpp-no-possdup-injection; QFcpp `Session.cpp:534-537`.)*

## PossResend(97) inbound + AllowPosDup send-path strip (022-possresend-allowpossdup-send)

<!-- 022-possresend-allowpossdup-send — completes catalogue row S-010 (backlog → done) -->

- **B-022-1 — A plain `send` strips caller-supplied `PossDupFlag(43)` / `OrigSendingTime(122)`
  by default; the auto-resend path always re-adds them independently.** `SessionConfig::allow_pos_dup`
  (default `false`, QuickFIX-J `AllowPosDup` config-key parity) governs the plain `Session::send`
  path: `false` (default) STRIPS any caller-supplied `43`/`122` from the opaque application payload
  before framing; `true` RETAINS them verbatim (operator opt-in for callers that manage their own
  duplicate flags). The strip is a no-heap, boundary-anchored field excision behind a 022-owned
  per-field scanner that validates every post-`35=` field is `<non-empty digit-only tag>=<value>\x01`
  and fails the send CLOSED (`app_payload_malformed=131`, no seqnum consumed, no transmit) on the
  FIRST malformed field — a missing `=`, an empty/non-digit tag, or an empty field (cases the 020
  denylist floor admits). Only complete, SOH-boundary-anchored `43=…\x01`/`122=…\x01` fields are
  excised; a literal `43=` inside another field's value (no preceding SOH) is preserved (injection-safe,
  INV-2). The automatic resend/retransmission path (`build_replay_frame`) ALWAYS re-adds `43=Y`+`122`
  regardless of the knob — it never routes through `send_impl` (FR-007, structural). Default-strip is
  an intentional default wire-behavior change matching QuickFIX-cpp (unconditional strip) and
  QuickFIX-J (default strip). **Status: shipped** (022, supersedes L-021-2). *(FR-006..FR-009;
  data-model.md §2 INV-1..5; research.md D1/D2/D5/D6; contracts §C2/§C3; one site in `send_impl`.)*

- **L-022-1 — `PossResend(97)` carries NO session-level handling; it is delivered to the
  application for business-level duplicate determination.** An in-sequence application message bearing
  `PossResend(97)=Y` is processed normally (the expected inbound seqnum advances) and delivered to the
  registered `Application::fromApp` with the full `MessageView` (tag 97 readable); the session never
  rejects, drops, or disconnects it for `97`, and `97` does NOT trigger the `OrigSendingTime(122)`-required
  rule (that keys on `43=Y` only). fixpp adds NO session-level PossResend logic — matching QuickFIX-cpp
  v1.16.0 and QuickFIX-J 3.0.1, which define the field but never read it in their session layers. The
  application must perform business-level de-duplication on its own keys (e.g. `ClOrdID`). **Status:
  shipped as witness-only** (022, zero production code — clarify-confirmed). *(FR-001..FR-005;
  data-model.md §3; research.md D4; contracts §C4.)*

## Per-Session + Control-Plane Strand Binding (023-engine-session-strand)

### Feature Catalogue Rows

- **B-023-1 — The per-session strand binds the whole role loop + transport + both teardown
  closes.** Each engine-managed session runs its entire role loop (accept/connect, TLS
  handshake, read-pump, application callbacks, sends, and BOTH teardown closes — transport
  `close()` and terminal `Session::close()`) on a single `asio::strand` created per session
  (`SessionEntry::session_strand`); the transport I/O object is bound to that strand at every
  construction site (the four `asio_tls_transport` ctors + the listener-build + reconnect
  paths — INV-7/V-10). This serializes a session's TLS state against its in-flight read
  during teardown, fixing the flaky `BIO_ctrl` SEGV/UAF
  (`[[project_business_roundtrip_bio_ctrl_segv]]`). *(FR-005/FR-009; E-1/E-3/E-5; C-1/C-7.)*
- **B-023-2 — Engine-global control-plane state is serialized on a distinct control strand.**
  A single engine **control strand** (distinct from every session strand — INV-0) serializes
  ALL engine-global mutation AND `stop()`'s teardown reads: `registry_`, `listeners_`,
  `listener_endpoints_`, `accept_scope_signals_`, the outstanding/send counters, and the
  awaited handle publication/unpublication. `send` traverses caller→control→session; `stop`
  runs its whole teardown (snapshot/cancel/join/close-dispatch/clear) on the control strand —
  all non-blocking posts, no locks, no deadlock. *(FR-011/FR-012; E-0/E-2/E-4; C-0/C-2/C-6.)*
- **B-023-3 — Public synchronous readers are MT-safe via an atomically-published RCU
  snapshot; `lookup()` returns a bounded handle.** `lookup()` and `acceptor_bound_endpoint()`
  read an atomically-published immutable snapshot (`std::atomic<std::shared_ptr<const
  ReaderSnapshot>>`, standard C++20 — no `std::mutex` in our headers, §XV.9; republished on
  the control strand after every control-plane mutation) — never entering a session/control
  strand, a user-visible lock, or a blocking wait. **NOTE: this atomic is NOT lock-free**
  (`is_lock_free() == false` on the supported libc++/libstdc++ — `atomic<shared_ptr>` is
  implemented with an STL-internal lock pool); the read is wait-free of *engine* locks/strands
  but takes a brief STL-internal lock. The "lock-free" claim from earlier design notes is
  therefore dropped (V-6). Correctness is unaffected (no deadlock, no data race). `Engine::lookup()`
  changes from `Session*` to **`std::shared_ptr<Session>`** (the single recorded ABI change,
  FR-008/SC-004) — a **bounded handle**: the `Engine` must outlive any outstanding handle
  (`~Engine` debug-asserts zero outstanding leases; the lease is a debug-assert + caller
  obligation only, kept strictly separate from the `send_counter_` barrier — R7).
  *(FR-008/FR-014; E-7/INV-9/INV-9a; C-4/C-8.)*

### Limitations

- **L-023-1 — The bounded-handle `lookup()` lease is enforced in DEBUG only.** In debug
  builds `lookup()` returns an aliasing `std::shared_ptr<Session>` whose control block
  increments an engine-owned outstanding-lease counter (`~Engine` asserts it is zero). In
  release builds it is a plain `std::shared_ptr<Session>` with no counter; the
  Engine-outlives-handles precondition is a documented caller obligation, not enforced.
  **Status: by design** (R7 — never a `stop()` drain; draining on app-held leases would hang
  `stop()`). *(INV-9a; C-8; research.md R7.)*

- **L-023-2 — No dedicated `Engine::send` two-hop / establish-churn perf micro-bench
  (V-6 partial evidence).** The two-hop send (`caller→control→session`) and the D-SNAP
  snapshot read/republish are structurally new (no `Engine::send` bench existed pre-023), so
  there is no prior baseline to gate Article VIII ±5% against. A standalone micro-bench would
  be dominated by TLS-loopback setup (the strand hops are µs-scale), giving low signal. What
  IS recorded: the snapshot atomic `is_lock_free() == false` on the supported libc++/libstdc++
  (an STL-internal lock pool — see B-023-3). The binding correctness gate for this concurrency
  feature is TSan (full suite 388/388, exact witness set ×3 sanitizers). **Status: follow-up**
  — a dedicated send/establish-churn bench + baseline is a low-risk bench-only carry-forward
  (cf. the 012 RC#G handshake-bench scaffold precedent). *(V-6; research.md D7/D-SNAP;
  Article VIII.)*

## ResetOn{Logon,Logout,Disconnect} Lifecycle Reset Knobs (024-reset-refresh-on-logon)

### Behaviors

- **B-024-1 — Three `SessionConfig` knobs reset both sequence numbers to 1 at a session
  lifecycle event; default off.** `reset_on_logon` / `reset_on_logout` / `reset_on_disconnect`
  (all default `false`, QuickFIX cfg-key parity) trigger a durable reset to `{1,1}` —
  `SeqnumManager::reset_to_one()` then `MessageStore::reset()` — at, respectively, Logon, a
  Logout teardown (sent OR received), and ANY disconnect (incl. an abnormal drop). The reset
  reuses the `013` reset primitive via a shared `reset_seqnums_to_one_durable(disposition)`
  helper; it adds no new error slot, codegen, or wire field. **The initiator announces a
  ResetOnLogon via `ResetSeqNumFlag(141)=Y`** on its outbound Logon through an OR-of-three
  predicate (`(reset_on_logon || reset_on_logout || reset_on_disconnect) && seqnums=={1,1}`,
  evaluated against post-reset live state — matching QuickFIX-cpp `shouldSendReset()` /
  QuickFIX-J `isResetNeeded()`); a `reset_on_logout`/`reset_on_disconnect` session that reset
  to `{1,1}` at a prior teardown therefore also sets `141=Y` on its NEXT initiator Logon. The
  reset is wired at the **shared** initiator-Logon emission point (`emit_initiator_logon_()`),
  so it fires for both per-session-direct `open()` and engine-managed `drive_reconnect()`
  (initial lazy-connect + reconnect). The store-failure disposition is **cause-keyed**:
  knob-driven Logon = **fatal** (blocks `Active`); the `013`-only received-`141` path is
  **I-07 logged-then-proceed for non-persistent stores but fatal-when-persistent since 030**
  (see B-030-2); teardown = logged.
  The acceptor handles the two reset causes via a **cause-dependent split** (mutually exclusive
  arms): the knob-driven reset (`reset_on_logon==true`) runs **before** `check_inbound`
  (`session.cpp:1559`, fatal disposition) so a fresh peer `34=1` at local-expected>1 is
  admitted; the 013-only received-`141` reset (`peer_sent_reset && !reset_on_logon`) runs
  **after** `check_inbound`. The arms are mutually exclusive → exactly
  one `store_->reset()` per path. A logout+disconnect teardown double-trigger collapses via
  a single-fire guard — each teardown also yields exactly one observable `MessageStore::reset()`
  (`FileStore::reset()` is non-idempotent I/O). *The cause-dependent split is retained for
  admission semantics (the knob-driven reset must precede `check_inbound`). The earlier rationale
  that the split "preserves `next_inbound`==1 byte-identity" for the 013-only arm is **superseded
  by 030**: 030 restores the received-`141` next-expected-**inbound** to 2 (the consumed seq-1
  reset Logon is a surviving advance — QuickFIX reset-then-increment parity), so the inbound
  post-state is now intentionally 2 while only the OUTBOUND reply stays byte-identical at seq 1.
  See B-030-1.*
  **Status: shipped** (024; received-`141` inbound post-state corrected by 030). *(FR-001..FR-010; C2.1–C5.2; data-model disposition table.)*

### Limitations

## RefreshOnLogon — per-logon re-hydrate knob (025-refresh-on-logon)

### Feature Catalogue Rows

- **S-018** (session) — RefreshOnLogon — reload persisted state on reconnect — `backlog → done`.
  FIX 4.0–5.0SP2, FIXT.1.1.

### Behaviors

*(The re-hydrate-on-logon behavior is described by the S-018 catalogue row; see
`feature-catalogue.md`.)*

### Limitations

- **L-025-1 — A `refresh_on_logon` re-hydrate on an ACTIVE session can transiently set the
  manager's inbound or outbound counter to a value BELOW the previously-seen in-memory high-water
  mark (store-wins DOWN, INV-RoL-4).** This is the design intent for standby topologies where the
  store reflects a primary's authoritative counter; it is NOT a violation of the 029 INV-H1
  lower-bound (which is a store ≤ manager store-side invariant, not a manager monotonicity
  constraint). However, operators using `refresh_on_logon=true` in a configuration where the
  store can lag behind the in-memory counter (e.g. a single-node session reconnecting after a
  partial in-memory-only run) should be aware that the re-hydrate will regress the in-memory
  counter to the store's (lower) value — potentially causing duplicate-seqnum acceptance or
  replay. This is suppressed by `reset_seqnum_policy = bilateral_strict` (INV-RoL-3), which
  prevents the re-hydrate entirely; under `bilateral_strict` the knob is a no-op and the
  managed counter is monotonic. `refresh_on_logon=true` is intended for **standby-only**
  topologies where the authoritative source is the external store (shared with the primary).
  **Status: documented** (research D-RoL-6; data-model.md INV-RoL-3/INV-RoL-4;
  contracts/refresh-knob.md C4). *(catalogue S-018; `session.cpp` `refresh_active_`
  suppression; test W5a INV-RoL-3 witness.)*

- **L-025-2 — The acceptor `force=true` warm re-hydrate path at `session.cpp:1885` is not
  reachable through the current engine and has no reachable test vehicle.** The production engine
  (`engine.cpp:877`) constructs a **fresh** `Session` per accepted connection; `hydrated_` is set
  at first logon and never reset, so every acceptor Logon arrives on a Session with
  `hydrated_==false` (the force latch-bypass at `:614` is never triggered on the acceptor side).
  A 2nd Logon received in `Active` state is dispatched to the dup-Logon-in-Active `Reject` arm,
  not back through the `NotConnected` Logon handler. The acceptor `force` wiring is therefore
  **dead-but-harmless**: it is correctly wired and would function if Session reuse across acceptor
  reconnect is introduced (deferred). FR-002's per-2nd+-logon re-hydrate is witnessed for the
  **initiator** role (W1/W2/W7); the acceptor receives the same re-hydrate semantics on each new
  connection via the 029 cold-hydrate spine (fresh Session → fresh `ensure_hydrated_()` call on
  the first Logon). **Status: documented, acceptor same-connection re-Logon force-bypass deferred
  pending Session reuse.** *(data-model.md W6 scope; catalogue S-018; `session.cpp:1885`.)*

## Nanosecond-resolution SendingTime (026-nanosecond-sendingtime)

### Feature Catalogue Rows

- **S-039** (session) — Configurable SendingTime(52) emit precision incl. nanoseconds + lenient
  inbound UTCTimestamp parse — `backlog → done`.

### Behaviors

- **B-026-1 — A per-session `fix_time_precision` selects `SendingTime(52)` emit precision
  (including nanoseconds); inbound parsing is leniently width-tolerant; `OrigSendingTime(122)`
  is preserved verbatim; default `millis` is a byte-identical no-op.** `SessionConfig::sending_time_precision`
  (`fix_time_precision`, default `millis`) controls the precision of every **newly-stamped
  outbound `SendingTime(52)`**: `nanos` emits the 27-char `YYYYMMDD-HH:MM:SS.sssssssss` form,
  `micros` the 24-char form, `millis` (default) the 21-char FIX 4.x form. The precision threads
  compile-time-exhaustively (non-defaulted parameter) through both stamp helpers
  (`session::stamp_sending_time`, file-local `stamp_sending_time(Clock&)`) and all 21 call sites
  — a missed site is a build error, not a silent wrong-precision frame. The inbound parser
  (`core::fix_string_to_utc_time`) is **lenient**: it accepts a bare length-17 timestamp OR a
  `.` at index 17 followed by any 1–9 sub-second digits (total length 19–27), scaling an N-digit
  fraction to nanoseconds by `10^(9−N)` — so a counterparty's nanos (or any non-standard-width)
  `52=`/`122=` parses instead of being rejected (Postel's law; matches QuickFIX-cpp). Malformed
  fractions reject via `wire_invalid_field_format`: empty fraction (`…SS.`), a non-digit fraction
  char, or >9 digits (caught by an explicit **width/length gate** before any digit parse — a
  10-digit value fits in `int64` and would not trip an arithmetic overflow). `OrigSendingTime(122)`
  on a PossDup resend echoes the **stored original** `52=` bytes verbatim — `build_replay_frame`
  byte-copies them, never re-stamping at the configured precision. MaxLatency (S-019) operates
  correctly on the parsed ns instant with no boundary-logic change. Default `millis` ⇒ every
  outbound `52=` is byte-identical to the pre-feature baseline. No new wire field, error slot,
  codegen, or C-ABI surface (formatter reuses `decimal_buffer_too_small`; parser reuses
  `wire_invalid_field_format`). **Status: shipped** (026). *(FR-001..FR-009; SC-001..SC-005;
  contract C1–C7; data-model E1–E6 / I-NST-1..6.)*

### Limitations

- **L-026-1 — Achieved sub-second resolution is bounded by the platform `system_clock::period`;
  FIXT/version-gating of sub-second precision is deferred to G4.** When `nanos` is selected the
  wire FORMAT is always 9 digits, but the achieved resolution reflects the clock's true tick:
  full nanoseconds on libstdc++ (Tier-1 Linux), coarser on platforms whose `system_clock` ticks
  at ~100 ns (e.g. MSVC, Tier-2) — there the trailing digits are `00`, a documented platform
  nuance, not a defect. fixpp is FIX.4.4-scoped, so the QuickFIX-cpp/J FIX4.2+/FIXT
  `supportsSubSecondTimestamps` version-gate is moot here; it becomes relevant when FIXT.1.1 /
  5.0SP2 land (G4). **Status: documented** — version-gating tracked for G4. *(research D6;
  spec Edge Cases; contract C6.)*

## Per-Session NextExpectedMsgSeqNum(789) fast resume (027-next-expected-msgseqnum)

### Feature Catalogue Rows

- **S-031** (session) — NextExpectedMsgSeqNum(789) in Logon — fast session resume without
  ResendRequest round-trip — `backlog → implementation-parity-4.4`.
  FIX 4.4 parity only; FIXT.1.1 / 5.0SP2 outstanding to G4.

### Behaviors

- **B-027-1 — Per-session `NextExpectedMsgSeqNum(789)`: advertise next-expected-inbound in
  Logon; honor a peer's 789 with a proactive resend that eliminates the ResendRequest
  round-trip; X>N or present-but-invalid 789 ⇒ Logout+disconnect; default off byte-identical;
  FIX 4.4 only.**
  When `SessionConfig::enable_next_expected_msg_seq_num` is `true` (default `false`),
  fixpp appends tag `789=<next_inbound>` to every outbound Logon (both the initiator's opening
  Logon and the acceptor's reply), where `<next_inbound>` is `seqnum_mgr_.next_inbound_unsafe()`
  — plain, no `+1` (the acceptor reply is built post-`check_inbound` which already advanced
  the counter, so the read is already correct; matches research D-3/E-OBO). When an inbound
  Logon carries `789=X`: (a) present-but-invalid X (parse→0, empty, non-digit, overflow) ⇒
  `Logout`+disconnect — evaluated **before** the X<N compare to close the `[1,N-1]`
  full-history-amplification path (research D-10, contract C6); (b) X>N (peer expects more
  than we have sent) ⇒ `Logout("NextExpectedMsgSeqNum too high …")`+disconnect (FR-005);
  (c) X<N ⇒ proactive resend `replay_outbound_range_(X, N-1, through_current=true)` with
  PossDup app frames + GapFill admin frames — no `ResendRequest` round-trip; (d) X==N ⇒
  in sync, no resend. The comparison basis is outbound: N = `seqnum_mgr_.peek_outbound()`
  (I-NEX-11 — never confused with the inbound counter). The acceptor's proactive resend runs
  AFTER the reply `store_then_emit` (RC#4 ordering, `:1766`). Default off (`false`) ⇒ outbound
  Logon byte-identical to pre-feature baseline; inbound `789` ignored; existing `ResendRequest`
  recovery (013) untouched. FIX 4.4 only — no FIXT / 5.0 version-gating this slice (G4).
  **Status: shipped** (027). *(FR-001..FR-009; SC-001..SC-005; contracts C1–C10; data-model
  E1–E3, I-NEX-1..12; `tests/session/test_next_expected_msgseqnum.cpp`;
  `tests/interop/happy/hp_fix44_next_expected_test.cpp`.)*

### Limitations

- **L-027-1 — 789 is both-peers-required; there is NO automatic ResendRequest fallback at
  logon when only one side enables the knob.** When `enable_next_expected_msg_seq_num=true`
  and the peer's inbound Logon carries NO `789`, the at-logon ResendRequest is suppressed
  (FR-004 suppression is unconditional when the knob is on). If fixpp has an at-logon gap
  and the peer does not send `789`, the gap is NOT proactively filled at logon time — it will
  only self-heal when the Active too-high arm emits a `ResendRequest` on the first in-sequence
  frame whose seqnum exposes the gap. Operators MUST enable `789` on BOTH endpoints (QFcpp:
  `EnableNextExpectedMsgSeqNum=Y`; QFJ: `EnableNextExpectedMsgSeqNum=Y`). This matches
  QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1, which likewise have no automatic fallback.
  **Status: by design** (FR-004/FR-009, L-027-1 deliberate divergence from a hypothetical
  mixed-mode). *(research D-7/D-11; contract C5/C9; data-model I-NEX-10.)*

- **L-027-2 — A lost proactive resend self-heals via the Active too-high arm on the next
  inbound frame; a permanent no-recover hole cannot arise from the current codebase.**
  When the behind-side partner sent 789=X and expected to receive `[X, N-1]` proactively but
  the proactive resend was lost (e.g. a transport error after the Logon exchange), the behind
  side's `next_inbound_` is still at X. The first in-sequence active frame from the far side
  (seqnum M > X) hits the `Active` too-high arm (`:1968-2009`), which issues a `ResendRequest`
  for `[X, M-1]` — recovering the gap via the normal recovery path. A true never-recover hole
  would only arise if a future change ALSO suppressed the Active too-high arm when the knob is
  on, which the current code does not do (T017 review comment annotates `:1968` as
  recovery-of-last-resort — stays active regardless of knob state). **Status: documented**
  (research D-11, data-model I-NEX-10). *(contracts C5; `session.cpp:1968`; T017 annotation.)*

## Validation-compat toggles — CheckCompID & ValidateSequenceNumbers (028-validation-compat-toggles)

### Behaviors

- **B-028-1 — `check_comp_id=false` skips the steady-state SenderCompID/TargetCompID match;
  BeginString, Logon-establishment CompID, and 013 authz remain strict; default byte-identical.**
  `SessionConfig::check_comp_id` (default `true`) controls the per-message `49`/`56` equality
  gate in the `LogonReceived/Active` inbound handler. When `false`, a frame whose
  `SenderCompID(49)` or `TargetCompID(56)` does not match the configured pair is **accepted and
  delivered** instead of triggering a disconnect (FR-001/FR-002). Three gates are deliberately
  left strict regardless of the knob: (a) `BeginString(8)` mismatch still disconnects
  (I-VCT-1); (b) the Logon-establishment CompID check in `interpret_logon` is unaffected —
  a Logon whose `49` ≠ configured `target_comp_id` is still refused (steady-state-only scope,
  I-VCT-6, FR-012); (c) the 013 `compid_authorization_policy` allow-list still refuses a
  non-allow-listed principal at Logon time (I-VCT-2). Default `true` ⇒ byte-identical no-op.
  QuickFIX-compat: QFcpp `CheckCompID=N` / QFJ `CheckCompID=N`. *(FR-001/002/003/012;
  data-model I-VCT-1/2/6; research D-2; contracts C1; `tests/session/test_validation_compat_toggles.cpp`.)*

- **B-028-2 — `validate_sequence_numbers=false` tolerates out-of-order inbound: no
  ResendRequest on a forward gap, no disconnect on a too-low; counter advances on exact match
  only; `SequenceReset(35=4) NewSeqNo` not applied; PossDup + `seq==0` + too-low-Heartbeat
  carve-outs retained; default byte-identical.**
  `SessionConfig::validate_sequence_numbers` (default `true`) controls four inbound seqnum
  enforcement sites in the `LogonReceived/Active` handler. When `false`: (1) too-high inbound
  (`seq > next_expected`) does NOT enter AwaitingResend and does NOT emit a `ResendRequest`
  (site S2); the frame falls through to a deliver-without-advance path (site S4). (2) too-low
  inbound (`seq < next_expected`) does NOT disconnect; the frame is delivered to `fromAdmin`
  (admin `MsgType`) or `fromApp` (app `MsgType`) via `parse_and_dispatch_` — counter unchanged,
  session stays `Active` (site S4, FR-004/005). (3) reset-mode `SequenceReset(35=4)` (site S6,
  before the seqnum gate) — the `apply_inbound_sequence_reset` intercept is bypassed; frame
  delivered to `fromAdmin`, counter unchanged (FR-013/I-VCT-11). (4) gapfill-mode
  `SequenceReset(35=4, 123=Y)` (site S7, after the seqnum gate) — same bypass; `NewSeqNo` NOT
  applied; an exact-match gapfill `35=4` that already advanced the counter by +1 via S5 does
  NOT additionally apply `NewSeqNo`. The inbound counter advances on exact match only
  (unchanged S5 path). Four carve-outs are retained regardless of the knob: PossDup Stage-1/
  Stage-2 handling runs on both the exact-match and out-of-order arms (I-VCT-5); `seq==0`
  (unparseable `MsgSeqNum`) remains fatal (I-VCT-10); too-low `Heartbeat(35=0)` is still
  silently dropped pre-gate (N3 carve-out at site S3); Logon-time seqnum checks are unchanged
  (steady-state-only scope, I-VCT-6, FR-012). Default `true` ⇒ byte-identical no-op.
  QuickFIX-compat: QFJ `ValidateSequenceNumbers=N`. *(FR-004/005/006/013/012;
  data-model I-VCT-3/4/5/10/11; research D-3; contracts C2; `tests/session/test_validation_compat_toggles.cpp`.)*

### Limitations

- **L-028-1 — `validate_sequence_numbers=false` disables gap detection — real gaps are silently
  accepted and messages may be processed out of order.** With the knob off, fixpp makes no
  attempt to detect or recover a missing message range: a forward gap simply delivers the
  higher-seqnum frame without issuing a `ResendRequest`, and the missed messages are never
  requested. This means the application layer may receive frames out of order or miss frames
  entirely. This knob is intended ONLY for counterparties that are known to send out-of-order
  frames as a deliberate protocol choice (e.g. a QuickFIX-J peer configured
  `ValidateSequenceNumbers=N`); using it against a conformant FIX peer will hide real gaps.
  **Status: by design** (FR-005/FR-006; research D-0/D-3; L-028-3 is the steady-state-only
  companion). *(data-model I-VCT-3; contracts C2.2.)*

- **L-028-2 — `check_comp_id=false` removes the steady-state mis-routing guard — a message
  addressed to a different CompID pair is accepted; rely on 013 authz + transport binding.**
  With the knob off, an inbound frame bearing any `SenderCompID(49)` / `TargetCompID(56)` pair
  is delivered as long as it passes the strict gates (BeginString, Logon-establishment CompID,
  013 `compid_authorization_policy`). The steady-state mis-routing guard that would normally
  reject a cross-session frame is absent. Operators using this knob should ensure adequate
  security via mTLS transport binding (where the 013 allow-list verifies the TLS identity ↔
  CompID mapping) or by ensuring the network topology is point-to-point. This knob is intended
  for counterparties known to send inconsistent CompIDs (e.g. a QuickFIX counterparty
  configured `CheckCompID=N`). **Status: by design** (FR-002/FR-003; research D-0/D-2;
  L-028-3 is the steady-state-only companion). *(data-model I-VCT-2; contracts C1.2.)*

- **L-028-3 — Both relaxations are steady-state only — Logon establishment is unaffected by
  either knob; a counterparty needing relaxed Logon-time checks is not supported.** The
  `check_comp_id` and `validate_sequence_numbers` knobs apply exclusively to the
  `LogonReceived/Active` inbound handler. The Logon-establishment paths (`NotConnected` /
  `LogonSent`) are deliberately left strict: a Logon with a mismatched `SenderCompID(49)` is
  still refused, and a Logon-time too-high `MsgSeqNum` still disconnects, regardless of either
  knob. This is a deliberate divergence from QuickFIX-J, which routes Logon through the same
  `verify()` method and therefore relaxes at Logon too (`ValidateSequenceNumbers=N` also
  suppresses Logon-time too-high checks in QFJ). The fixpp restriction keeps Logon
  establishment strict for safe session bring-up and avoids entangling the 013/024 reset FSM.
  **Status: by design** (clarify Q3 / D-4; steady-state-only scope). *(data-model I-VCT-6;
  FR-012; plan Summary "Steady-state only".)*

## Persistent seqnum continuity — bidirectional hydrate-on-open (029-persistent-seqnum-hydrate)

### Feature Catalogue Rows

- **S-042** (session) — Persistent inbound seqnum continuity — durable inbound counter +
  bidirectional hydrate-on-open; resume both directions across restart — `backlog → done`.
  FIX 4.4.

### Behaviors

*(The hydrate-on-open and persist-inbound-advance behaviors are described by the S-042 catalogue
row; see `feature-catalogue.md`.)*

### Limitations

- **L-029-1 — Post-GapFill restart yields a bounded redundant ResendRequest when 789/reset is
  available; otherwise the too-high peer Logon fatals on the Logon gate and recovers by
  reconnect; recovery is correct at-least-once in both cases.** The `persist_inbound_advance_()`
  helper uses `+1` per-delivery writes only — there is no absolute counter-set in the
  `MessageStore` interface (preserving the 4-pure-virtual cap). A prior-run
  `SequenceReset`-GapFill absolute jump (`apply_inbound_sequence_reset`) updates the in-memory
  manager but is NOT persisted. On restart the persisted counter is a **monotonic lower bound**
  of the true in-memory value (INV-H1). If the peer's outbound counter advanced past the restart
  point (e.g. it sent messages after the GapFill that the fixpp side accepted), the peer's next
  Logon will carry a `MsgSeqNum(34)` above fixpp's hydrated `next_inbound`. Two outcomes
  depending on knob state: (a) **`enable_next_expected_msg_seq_num=true`** — fixpp advertises
  `789=<hydrated_next_inbound>` in its Logon; the peer proactively resends or fixpp emits a
  `ResendRequest` for the gap; session reaches Active, residual gap recovered; bounded redundant
  resend (at most the untracked GapFill jump range). (b) **knob off** — the Logon gate has no
  ResendRequest arm; a too-high peer `MsgSeqNum(34)` triggers a fatal
  Logout+disconnect at the Logon-path check; the session reconnects and the peer resets to `1`
  (ResetOnLogon) or the gap resolves after a further handshake. Recovery is correct (at-least-once,
  no skip) in both cases; case (b) incurs an extra reconnect cycle. Operators using persistent
  stores and SequenceReset-GapFill recovery should enable `enable_next_expected_msg_seq_num` on
  both sides (QFcpp `EnableNextExpectedMsgSeqNum=Y` / QFJ `EnableNextExpectedMsgSeqNum=Y`) to
  stay on the fast-recover path. **Status: documented** (INV-H1; research D-5; plan §VI delta
  L-029-1; data-model W5/SC-004). *(contracts/seqnum-hydrate.md C2/C3; `session.cpp`
  `apply_inbound_sequence_reset`; `tests/session/test_persistent_seqnum_hydrate.cpp` W5.)*

- **L-029-2 — A swallowed I-07 outbound store-write failure in a prior run leaves the persisted
  outbound counter behind the true last-sent value; hydrate is only as fresh as the last
  successful outbound write.** The existing outbound store write (added in 008/024) is I-07
  logged-then-proceed — an outbound `next_seqnum(outbound,true)` failure is logged but does NOT
  disconnect the session (asymmetric with the inbound path where failure is fatal, per research
  D-3). If this outbound write silently fails mid-session, the FileStore's persisted outbound
  counter is behind the true `next_outbound_`. On restart, `ensure_hydrated_()` reads the stale
  persisted value and loads it into the manager — the restarted session may replay seqnums the
  peer has already seen, causing a too-low reject or unexpected seqnum jump. This is a
  **pre-existing 008/024 property** (the I-07 policy predates 029); 029 adds the hydrate path
  that makes the stale-write scenario observable but does not alter the I-07 policy. The correct
  fix (promote the outbound write failure to fatal-disconnect, matching the inbound treatment) is
  deferred as a separate store-hardening slice. Operators relying on persisted outbound counters
  should ensure the underlying `MessageStore` (e.g. `FileStore`) operates on a reliable
  filesystem. **Status: documented** (research D-3 / plan §VI delta L-029-2; pre-existing
  008/024 I-07 policy; outbound→fatal deferred). *(contracts/seqnum-hydrate.md C3; `file_store.cpp`
  outbound-write path; `tests/session/test_persistent_seqnum_hydrate.cpp` NoHeap witness.)*

- **L-029-3 — Under `reset_seqnum_policy = bilateral_strict` with a non-1 persisted outbound
  counter, the 029 cold-open hydrate seeds the manager at the stored (non-1) outbound value
  and the cold Logon is then emitted with `141=Y` AND `34=<N>` (N > 1), which is malformed per
  FIX spec when a peer validates that `ResetSeqNumFlag(141)=Y` implies `MsgSeqNum(34)=1`.
  This is a property of the `bilateral_strict` cold-open path (029's one-shot `ensure_hydrated_`
  seeds from a non-1 store, then the strict policy adds `141=Y` unconditionally); 025 does NOT
  close this gap — the per-reconnect re-hydrate (025) is suppressed under `bilateral_strict`
  (INV-RoL-3), so 025 introduces no new exposure. QuickFIX-cpp/J peers that enforce the
  `141=Y`→`34=1` invariant will reject the cold Logon; the session will disconnect+reconnect
  until the peer or the store is reset. Operators should use `bilateral_lenient` or
  `unilateral` policy when the persisted outbound counter may be > 1 at cold open.
  **Status: documented, DEFERRED** (Gate A D-RoL-6; data-model.md W5b L-029-3 gap-witness;
  025 INV-RoL-3; NOT closed by 025). *(contracts/refresh-knob.md C4; `session.cpp` strict-policy
  cold-open path; `tests/session/test_refresh_on_logon.cpp` W5b.)*

## Received-reset inbound advance correction (030-received-reset-inbound-advance)

### Feature Catalogue Rows

- Amends **S-017** (received-`141` reset machinery), **S-031** (789 advertisement),
  **S-032** (ResetSeqNumFlag(141)). No new S-row — this is a conformance correction of the
  existing received-`141` path, found via a failed live acceptor interop cell vs QuickFIX-cpp/J.

### Behaviors

- **B-030-1 — A received `Logon(141=Y)` advances next-expected-**inbound** to 2 (not 1) on
  both the acceptor and the initiator arm; the outbound reply stays seq 1.** When a peer
  initiates a sequence reset by sending `Logon(34=1, 141=Y)` and the local `reset_on_logon`
  knob is OFF (the "received-141" path), the consumed seq-1 reset Logon is an in-sequence
  message: after the post-`check_inbound` durable reset, fixpp restores next-expected-inbound
  to `seqnum_min+1` (=2) in BOTH the in-memory `SeqnumManager` (`set_next_inbound`) AND the
  durable store (a `next_seqnum(inbound, true)` write-through → `store == manager == 2`,
  INV-H1 equality). This matches QuickFIX-cpp/J, which **reset-then-increment** (net 2);
  fixpp previously increment-then-reset (net 1), which left next-expected-inbound at 1 and
  emitted a **spurious `ResendRequest`** on the peer's next genuine message at seq 2 (and,
  with 027 enabled, advertised `789=1` instead of `2`). The correction is applied symmetrically
  on the two separate-but-identical code paths: the acceptor `NotConnected` Logon handler and
  the initiator Logon-ack `peer_ack_sent_reset_flag` arm (FR-009). The **OUTBOUND reply Logon
  `MsgSeqNum` stays seq 1** (independent counter; byte-identical); only the reply `789` content
  corrects 1→2 (acceptor-reply-specific, 027-on). The `reset_on_logon=true` knob path is
  unchanged (already produced 2). **Status: shipped** (030). *(FR-001..FR-009; reference oracle
  QFcpp `Session.cpp::nextLogon` reset-then-increment, QFJ `Session.java` lines 2202-2204/2215/
  2303; `tests/session/test_reset_on_lifecycle.cpp` discriminating triple + initiator witnesses;
  `tests/session/test_reset_seqnum_policy_matrix.cpp`, `test_next_expected_msgseqnum.cpp`,
  `test_persistent_seqnum_hydrate.cpp` value-pins.)*

- **B-030-2 — On a persistent store, a received-`141` durable-reset failure now DISCONNECTS
  (was stay-Active); non-persistent stores keep stay-Active.** This amends the 024 FR-001/C2.6
  I-07 "logged-then-proceed" contract for the persistent received-`141` sub-case. Rationale: a
  swallowed (`logged`) store-reset failure on a persistent store would leave the durable counter
  stale, and the B-030-1 persist-to-2 write-through would then advance the **stale** store
  (N→N+1) — for any session that had received messages this yields `store > manager` (INV-H1
  violation → silent inbound skip on restart, the 029 over-persist harm). Making the reset
  **fatal when the store is persistent** (`store_is_persistent_ ? fatal : logged`, on both arms)
  guarantees the reset succeeded before persist-to-2 runs, so `store == manager == 2` truly holds;
  a reset failure disconnects, the session re-opens, re-hydrates the store at its last-good value
  N (a valid INV-H1 lower bound), and the peer re-drives the reset — resuming with nothing skipped.
  Non-persistent
  stores are unaffected (the reset cannot meaningfully fail; INV-H4 makes persist-to-2 a no-op).
  Aligns with 029 D-3 ("inbound-correctness failures are fatal") and the existing fatal knob-reset
  sites. **Status: shipped** (030). *(FR-010; amends B-024-1; `tests/session/test_reset_on_lifecycle.cpp`
  fault-injection witnesses + the persistent-Disconnect / non-persistent-stay-Active contract split.)*

- **B-031-1 — As an acceptor, fixpp honors a peer initiator's `NextExpectedMsgSeqNum(789)`
  against its PRE-reply next-outbound (`N_pre`), so an in-sync peer triggers no resend and the
  session establishes with no duplicate-sequence frame.** With the `789` knob on, the acceptor
  emits its reply Logon first and then honors the peer's advertised `789` (the deliberate 027
  RC#4 ordering). Because the reply Logon consumes a sequence number, the live next-outbound at
  honor time is the **post-reply** `N_post = N_pre+1`. fixpp previously compared the peer's `789`
  against `N_post`; a conformant in-sync peer advertises `789 = N_pre` (its expected target, no
  `+1`), so `N_pre < N_post` mis-classified the peer as behind-by-one and emitted a spurious
  `SequenceReset-GapFill` at the sequence number the reply Logon had just used — a duplicate-
  `MsgSeqNum` violation that QuickFIX-cpp/J reject with a "MsgSeqNum too low" `Logout`, so the
  session never established (live-found, invisible to in-process 027 unit tests; parallels 030).
  The honor now compares against `N_pre` (captured before the reply consumes a seq; parameterized
  `honor_peer_next_expected_(…, next_outbound_ref)`) for all three arms: in-sync `X==N_pre` ⇒ no
  resend; too-high `X>N_pre` ⇒ Logout (so `X==N_pre+1` in the peer's initial Logon is correctly
  too-high, not in-sync); genuine-gap `X<N_pre` ⇒ proactive resend `[X, N_pre]` (range unchanged,
  reads live `peek_outbound()-1`). The **initiator** honor is byte-identical (its peer-reply
  `789 = target+1` already matches fixpp's post-own-Logon outbound; it passes the current
  `peek_outbound()`). Reference-engine-conformant (QFcpp `Session.cpp:228/277/687/709`; QFJ
  `Session.java:2250/2312/2334` evaluate the decision against the pre-reply sender counter).
  **Status: shipped** (031). *(FR-001..FR-009; `tests/session/test_next_expected_msgseqnum.cpp`
  W1 `Acceptor_XeqNpre_NoResend_Establishes` + W3 `Acceptor_XeqNprePlus1_TooHigh_Logout`; live
  close-out via the `NE-*-acc` interop cell vs QFcpp/QFJ.)*

- **B-032-1 — As an initiator, fixpp restores its OUTBOUND seqnum to 2 (not 1) when the peer
  echoes fixpp's own `141=Y`, so it carries one post-logon frame at `34=2` with no duplicate
  `34=1`.** A `reset_on_logon`/`reset_on_logout`/`reset_on_disconnect` initiator that reset before
  sending emits `Logon(141=Y, 34=1)` (outbound 1→2); a conformant peer (QFcpp/QFJ) echoes `141=Y`
  in its Logon ack. fixpp's `peer_ack_sent_reset_flag` arm reset-rewinds both counters to 1; 030
  restored the inbound twin but outbound regressed 2→1 → the next frame duplicated `34=1` →
  QFcpp/QFJ reject "MsgSeqNum too low" (L-024-2, live-found). The arm now restores outbound to 2
  (`set_next_outbound(seqnum_min+1)` + `persist_outbound_advance_`, manager-first/store-second,
  fatal-when-persistent — the outbound twin of B-030-1) gated on BOTH a latched emit-time fact
  (`own_logon_sent_reset_flag_` = fixpp actually emitted `141=Y`, which carries the inbound-at-1
  conjunct) AND `reset_before_send` (fixpp's Logon went at post-reset seq 1). The reset-event
  `by_peer_request` now keys on the latch ALONE — correcting the prior `bilateral_strict`-only
  classification for non-strict reset-knob initiators. Restore (latch && reset-before-send) and
  label (latch alone) are DISTINCT gates that diverge on `bilateral_strict`-at-N (latch true, no
  restore). Covers all reset knobs via the emit-time latch; acceptor / knob-off / peer-spontaneous
  / `bilateral_strict`-at-N outbound unchanged (byte-identical). Reference-engine-conformant
  (QuickFIX reset-then-increment). **Status: shipped** (032). *(FR-001/FR-003/FR-005/FR-006/FR-007;
  `tests/session/test_persistent_seqnum_hydrate.cpp` W1 + W5/W6/W8,
  `test_reset_seqnum_policy_matrix.cpp` W2/W3/W4b/W7, `test_refresh_on_logon.cpp` cross-reconnect
  latch witness; live close-out via the `RL-*-init` interop cell vs QFcpp/QFJ **LIVE-CLOSED
  2026-06-12** (T021/SC-003 done — `tests/interop/cell_results.yaml` `RL-{QFcpp,QFj}-init-fix44-reset-on-logon`
  now `status: pass, matrix_disposition: live`).)*

### Limitations

- None specific to 030/031 (both conformance corrections; no new deferred surface). The pre-existing
  L-029-1 (post-GapFill bounded redundant resend) and L-029-3 (`bilateral_strict` non-1 cold-open
  malformed Logon) are unchanged.

---

## FIXT.1.1 / FIX 5.0 SP2 Session Establishment (033-fixt-fix50sp2-session)

### Behaviors

**B-033-1 — FIXT.1.1 / FIX 5.0 SP2 session establishment (transport/application version decoupling).**
When `SessionConfig::version` selects a FIXT.1.1 profile, the session layer emits `BeginString=FIXT.1.1`
and enforces the transport/application version split. Both roles (initiator and acceptor) emit
`DefaultApplVerID(1137)` on the outbound Logon (FR-001/FR-002); the acceptor requires and validates the
peer's `1137` field (FR-003/FR-004). The `negotiated_version_profile()` accessor on `Session` exposes
the negotiated application version after establishment (FR-005); on the **initiator**, a peer Logon-ack
that omits `1137` or carries an unserviceable value leaves the profile at the unnegotiated fallback
`{session=Unknown, default_appl=Unknown}` (the initiator does not auto-reject — see L-033-3). The implementation is version-general:
any application-layer `ApplVerID` enum value accepted for `1137` is valid; acceptors reject only values
they are not configured to service (FR-004a, acceptor-scoped only). When FIXT.1.1 is **not** configured,
the FIX.4.x path is byte-identical — no wire change, no protocol divergence (FR-009/SC-002). *(FR-001
through FR-006, FR-009; `tests/session/test_fixt_logon_establishment.cpp` W1/W2/W3/W4/W5/W8.)*

**B-033-2 — Optional `Username(553)`/`Password(554)` on FIXT Logon; surfaced to `authorize_logon` seam.**
When `SessionConfig::logon_credentials` contains a username and/or password, the session layer emits
`Username(553)` and `Password(554)` on the outbound Logon (FR-006/FR-007). Inbound `553`/`554` are
parsed and surfaced to the registered `CompIdAuthorizationPolicy::authorize_logon(asserted_compid,
logon_credentials)` callback (FR-007/FR-008). The default policy implementation is accept-all; the seam
is independent of the mTLS `verify_peer` path. A credential-free FIXT Logon (no `553`/`554` received)
is accepted normally — credentials are optional per FIX-SL §4.3. `Password(554)` is redacted via the
shared `redact_tag554` utility before any persistence operation (W7). *(FR-006, FR-007, FR-008;
`tests/session/test_fixt_credentials.cpp` W6/W7.)*

### Limitations

**L-033-1 — Per-message `ApplVerID(1128)` routing (S-026) deferred.** Inbound `ApplVerID(1128)` is
tolerated (not rejected) when present on application messages (FR-010, witness W8). However, per-message
routing — using `1128` to select a message-type-specific application-layer version and dispatch
accordingly — is **not implemented** in this feature. It remains in `backlog` as a follow-on feature.
Operators relying on per-message versioning via `1128` should implement routing at the `fromApp` level.

**L-033-2 — Acceptor-side credential validation/rejection deferred (FR-008a).** The `authorize_logon`
seam is wired and surfaces parsed `553`/`554` values to the registered policy. However, the default
`CompIdAuthorizationPolicy` implementation is accept-all: no built-in credential database, no
configuration-driven reject path. Acceptors that need to reject Logons based on credential mismatch must
supply a custom `CompIdAuthorizationPolicy` implementation. A built-in config-gated validation/rejection
path is a committed future feature (FR-008a).

**L-033-3 — Initiator-side `1137` validation (unserviceable AND absent) is deliberately deferred to the
application.** FR-004a (unserviceable application version → `Reject` + Disconnect, NOT a Logout message)
is **acceptor-scoped only**. The initiator's Logon-ack arm (`src/session/session.cpp` ~`:3649-3665`)
records the peer's `DefaultApplVerID(1137)` only when it is present **and** maps to a known
`application_version`, and **never refuses on any value** — there is no automatic initiator-side disposal
path for a peer `1137` the initiator cannot service. The two non-conforming inputs and their concrete
shipped dispositions:

- **Unserviceable `1137`-ack** (present but unmappable / a version the initiator cannot service): the
  value is not recorded; `negotiated_appl_version_` stays `Unknown`; the session still reaches Active.
- **Absent `1137`-ack** (the peer Logon-ack omits `1137` entirely): the `result->default_appl_ver_id`
  optional is empty, the record-arm condition is false, `negotiated_appl_version_` stays `Unknown`, and
  the session still reaches Active. (The acceptor, by contrast, rejects a missing `1137` with
  `RequiredTagMissing(1)` per FR-004 — see B-033-1 / the FIXT establishment notes; the asymmetry is
  intentional and acceptor-scoped.)

In both initiator cases `negotiated_version_profile()` returns the unnegotiated fallback
`{session=Unknown, default_appl=Unknown}` (`session.cpp:189-192`), which a downstream `fromApp` reify
call-site can detect. An initiator requiring strict application-version negotiation must enforce it in
its `authorize_logon` / `fromAdmin` hooks. **Status: deferred-by-design** (no auto-dispose on the
initiator; not an open question — a built-in initiator-side strict-`1137` gate would be its own future
feature).

**L-033-4 — `Password(554)` redaction wired at unit-golden + interop-golden; production
logger/tap/transcript wiring is a forward obligation.** The `redact_tag554` utility is wired at the
unit-test golden layer (W7) and (US3 T026, 2026-06-12) at the interop-golden layer — a Python
`_redact_tag554` twin in `run_interop_cell.py`'s `normalize_transcript` AND `_transcript_to_inrepo_golden`
(no-op for the credential-free happy cells, but fail-closed at both persistence sites). Production
session-logger, tap-consumer, and transport-transcript wiring of the redactor is deferred — those
surfaces are no-hook stubs in 033 (see L-017-* for the logger/tap framework limitations).

## Fable assessment follow-ups (out-of-band, 2026-06-13)

These rows document **already-shipped behavior** surfaced by the independent Fable review pass
(`research/G19-fix-fpml-iso20022/fable-assessments/`). They were added out-of-band (no feature cycle)
because the underlying behavior is accepted-as-shipped for v1.0 (no code fix). Per-feature IDs are kept
so the **Tier-4 release-gate B&L back-fill** (REMAINING-WORK item 9) can relocate the `008` rows into a
proper 008 section. Code-fix follow-ups (credential masking, toAdmin coverage, GapFill 43=Y/122,
file_store offload) are tracked in `REMAINING-WORK.md` "Fable review findings (2026-06-13)", NOT here.

**B-034-1 — `Password(554)` on the outbound Logon is MASKED before persistence (at-rest exposure
mitigated).** *(was L-033-6; fixed by 034-credential-store-redaction.)* When a FIXT session has
`SessionConfig::logon_credentials.password` set AND any `store_factory` is configured, the `554` value is
overwritten in place with a same-length `'*'` run **before** the frame enters the message store, at the
single store-entry boundary `Session::store_then_emit` — so every store backend (FileStore, MemoryStore,
null) persists the masked frame on both the initiator emit and the acceptor reply, and no cleartext
password is at rest. The **wire** frame is transmitted unmasked (the peer still authenticates). The mask is
same-length (preserves `9=` BodyLength + the store's per-record CRC), zero-alloc (coroutine-frame copy),
and scoped to `35=A` only (the never-replayed-verbatim class). This is deliberate hardening **beyond**
reference-engine parity (QuickFIX-cpp/J FileStore persist the cleartext password). The embedded FIX `10=`
checksum of the stored frame is intentionally stale (stored frames are never re-validated as FIX nor
replayed). **Residual operator note:** the masked value length still reveals the original password length;
protect the store path with filesystem permissions and prefer mTLS identity over 553/554. *(034 FR-001..009;
`session.cpp` `store_then_emit`; witnesses `tests/session/test_credential_store_redaction.cpp`.)*

**L-034-1 — the at-rest mask relies on admin Logon frames never being replayed verbatim (forward
constraint).** The safety of masking the stored `35=A` Logon rests on the invariant that stored admin
frames are folded into a `SequenceReset-GapFill` on resend (`session.cpp` resend store-walk) and never
retransmitted byte-for-byte — so the masked stored copy can never reach the wire. **If a future feature
ever introduces verbatim admin-frame replay, it MUST re-derive the credential from configuration, not from
the (now-masked) store** — replaying the masked bytes would put `554=****` on the wire and break peer
authentication. App (non-admin) frames ARE replayed verbatim, which is precisely why 034's masking is
gated to `MsgType=A` only. *(034 R4/R7 / INV-034-3/5.)*

**L-015-5 — the deprecated `one_way_ca` TLS profile performs no peer-identity binding.** The `one_way_ca`
profile accepts any peer certificate that chains to the configured trust anchor — it does NOT bind the
certificate to a CN/SAN/CompID. (This is distinct from the default `verify_peer` path, where CompID↔TLS-
identity binding IS fully enforced per feature 015.) Operators MUST use an identity-binding profile for
production; `one_way_ca` is for transport-encryption-only / migration scenarios. *(Fable `5.1`; `src/tls/`
profile path.)*

**RELOCATED 2026-06-15** — the three Fable `5.4` 008 rows (FileStore monotone disk growth, uncapped RAM
offset index, bounded-`MemoryStore` post-cap silent resend loss) were minted out-of-band here
(2026-06-13) before 001–014 had B&L sections; the back-fill moved them into the proper
`## Message store … (008-message-store)` section above. See that section for the current text.

## 035-filestore-io-offload (2026-06-14)

**B-035-1 — `FileStore` disk I/O (`pwrite`/`fdatasync`/`rename`/`fsync`) now runs genuinely off the
session strand via `file_io_executor` (prior `[const §XV.4]` violation corrected).** From the initial
008-message-store delivery until 035-filestore-io-offload, the `FileStore::store(commit_per_message)`
path violated `[const §XV.4]` (the "no synchronous disk I/O on every send" rule): the shipped offload
idiom — `co_await asio::post(file_io_executor, use_awaitable)` — was inert (012 D-18). The `post` with
`use_awaitable` moved only the post-completion handler; the coroutine body, including the blocking
`pwrite` and `fdatasync`, resumed on the session strand and executed there synchronously. Under
`commit_per_message` this means every outbound message caused the session strand to block for the
duration of an `fdatasync` (~100 µs–10 ms on NVMe). As of **035-filestore-io-offload (PR #119)**,
all four disk-I/O sites in `FileStore` (`store()` pwrite/fdatasync, `next_seqnum()` pwrite/fdatasync,
`reset()` tmp-open/initialise/rename/parent-fsync, `flush_for_session_close()` fdatasync) run on the
`file_io_executor` thread pool via genuine `co_await asio::co_spawn(file_io_executor, syscall_lambda,
asio::use_awaitable)`. The outer `co_await` resumes on the session strand; the blocking syscalls are
pinned to the pool thread. `MemoryStore` is unaffected (zero-alloc, strand-only — unchanged). The
`FileStore` offload carries exactly one bounded O(1) `co_spawn` frame (~48 B, PMR-opaque global heap)
per disk op, permitted by the `[const §XV.1]` v0.2 §XV.4-offload exemption (`constitution.md:224`).
This is the project's deliberate `[const §XV.4]` async-journal posture (no strand block) — **not**
reference-engine parity: QuickFIX-cpp / QuickFIX-J perform **synchronous** FileStore flushes on the
calling thread (see `spec.md:186`); the async offload is a deliberate divergence, not a conformance
fix. [gate-b/r2 R#2: corrected PR #118→#119; replaced "parity with reference-engine behavior" with
the correct deliberate-divergence statement matching spec.md:186.]
*(035 FR-001..FR-007, FR-010; `src/session/file_store.cpp`; witnesses
`tests/session/test_file_store_offload_thread.cpp` + `test_file_store_cancellation.cpp` +
`test_file_store_concurrent_tsan.cpp`.)*

**L-035-2 — Post-rename reopen/lock failure in `FileStore::reset()` poisons the current store until
process restart.** During `reset()`, the live log is atomically replaced by a fresh log via
`rename(tmp, live)`. If the subsequent `open()`/`try_lock()` of the newly-named file fails (e.g.,
fd-limit exhaustion, permission race), the old fd names a now-unlinked inode; to avoid writing frames
that would vanish on restart, the store **fails closed**: it releases the stale fd
(`impl_->file = OsFile{}`), sets `open_ok = false`, and all further ops (`store`/`next_seqnum`/
`retrieve`/`reset`) return `store_io_failure`. The only recovery is to **restart the process**: on
restart, `FileStoreFactory::make()` reopens the now-fresh live log (the renamed file persists on
disk). Operators with aggressive fd-limit settings who observe `store_io_failure` after a session
reset should check `RLIMIT_NOFILE`. *(035 R#A.1; FR-010; `src/session/file_store.cpp:1678-1688`;
witness `tests/session/test_file_store_cancellation.cpp:750-797`.)*

**B-036-1 — `toAdmin` now fires for EVERY engine-originated administrative frame; `toApp` for the
engine-originated `BusinessMessageReject(35=j)`.** This amends the 019 FR-008 contract from partial
to full coverage. Which outbound engine emits invoke which `Application` callback:
- **`toAdmin` (inspect-only, not vetoable; a throw → `app_callback_threw` + Disconnected):** the
  complete engine-originated `Reject(35=3)` family — the shared `emit_session_reject_` helper
  (fromAdmin-veto + no-`Application` unknown-MsgType rejects), the established-session SendingTime
  Reject, the inbound-`SequenceReset`/`Logout` veto Rejects, the malformed-`OrigSendingTime(122)`
  Rejects (021 Arm C / RC#1 / Arm D), the `SequenceReset`-`NewSeqNo`-too-low Reject, the FIXT-1137
  Logon Reject — and **every** engine-originated `Logout(35=5)` including the initiator
  Logon-acknowledgement Guard-3 Logout (the last admin emit that previously bypassed observation).
- **`toApp` (vetoable; `app_do_not_send` → drop + stay Active + no outbound seqnum consumed; a throw
  → terminal close):** the engine-originated `BusinessMessageReject(35=j)` — `35=j` is an application
  message (outside the FIX admin set A/0/1/2/3/4/5), so it routes through `toApp`, matching
  QuickFIX-cpp/J `sendRaw`. A `toApp` veto suppresses the `35=j` but the rejected inbound message's
  durable sequence advance is **still persisted** (no restart reprocessing).
With no `Application` registered every site is a byte-for-byte no-op. *(036 FR-001..FR-008; amends
019 FR-008; `src/session/session.cpp` ARM-1 reject/logout sites + ARM-2 BMR site; witness
`tests/session/test_admin_emit_toadmin_coverage.cpp` — exact-count `toAdmin_calls ==
admin-frames-on-wire` + per-site throw + BMR veto-persist cells.)*

## 037-resend-reply-possdup-tags (2026-06-14)

**B-037-1 — Resend-reply `SequenceReset-GapFill(35=4, 123=Y)` now carries `PossDupFlag(43)=Y` and `OrigSendingTime(122)=SendingTime(52)`.** Every outbound GapFill emitted on a resend reply (`build_sequence_reset_gapfill`) carries exactly one `43=Y` and one `122` whose value equals the frame's own `52`. Wire-output change only; stored bytes and inbound validation are untouched. *(037 FR-001/FR-002/FR-003; `[FIX-SL §4.8.2/§4.8.5]`; `src/session/admin_messages.cpp` `build_sequence_reset_gapfill`; witness `tests/session/test_resend_reply_possdup.cpp` Cell 1.)*

**B-037-2 — Under `allow_pos_dup=true`, the auto-resend replay path now suppresses stored `43`/`122` before re-appending them, eliminating duplicate-tag emission.** When a stored application frame already carries `PossDupFlag(43)` and/or `OrigSendingTime(122)` (retained-PossDup path), `build_replay_frame` skips those stored fields before appending the engine-canonical `43=Y` + `122=<stored 52>`, so the replayed frame carries each tag exactly once. Default-path (`allow_pos_dup=false`) replay is byte-identical (stored frames are already clean). Refines 022's retain behavior. *(037 FR-004/FR-005/FR-006; `[FIX-SL §4.8.4]`; `src/session/session.cpp` `build_replay_frame`; witnesses `tests/session/test_send_allow_pos_dup_strip.cpp` Cell 2 (retain dedup) + Cell 3 (default byte-identity).)*

**L-037-1 — `43`/`122` are appended AFTER the body fields in both emitters (header-after-body placement), not in strict standard header order.** This matches the proven live-shipped pattern of `build_replay_frame` (which has shipped `43`/`122` after body fields against live QFcpp and QFJ since 013) and is safe because both reference engines parse the header into a field map making field order irrelevant to inbound validation. A strict positional-header-order validator is out of scope. *(037 research D-3.)*

**L-037-2 — The live QuickFIX-J acceptance arm of SC-004/FR-008 is DEFERRED to the Item-1 live-golden-capture workstream (→ G4).** The local ctest cell (`tests/interop/happy/hp_fix44_recovery_outbound_answer_test.cpp`) GTEST_SKIPs without the interop harness (`FIXPP_TLS_FIXTURE_DIR`/`INTEROP_QUICKFIX_J_PORT` unset). This is a deferral with an unblock condition (Item-1), NOT a permanent waiver — QFJ emits `43=Y` GapFills itself, so byte-level acceptance is structurally expected at capture time. Wire-level GapFill conformance is carried by Cell 1 (`tests/session/test_resend_reply_possdup.cpp`). The QuickFIX-cpp arm is separately waived per L-021-3. *(037 SC-004; deferral recorded in spec.md SC-004 disposition note + tasks.md T011.)*

## 038-acceptor-sendingtime-guard (2026-06-15)

**B-038-1 — The acceptor's first-Logon path now enforces the inbound `SendingTime(52)` MaxLatency guard (parity with the initiator Logon-ack and established-session paths).** Prior to this feature, the acceptor `NotConnected` arm admitted any inbound Logon without validating its `SendingTime(52)` — a pre-establishment blind spot absent on every other code path. An absent/empty, malformed, or stale-beyond-MaxLatency `52` on the acceptor's first inbound Logon now triggers a `Reject(35=3, 371=52, 373=10)` + disconnect (no Logout — pre-establishment shape, matching the in-arm `1137` sibling; QuickFIX emits a Logout first, a documented divergence). The conforming path is byte-for-byte identical to pre-feature behaviour. The guard is inserted AFTER `ensure_hydrated_` (so the outbound `Reject` carries the hydrated durable outbound seqnum `N`) and BEFORE `reset_on_logon`/`check_inbound` (so a rejected Logon does NOT advance or persist the inbound counter). *(038 FR-001..FR-005, SC-001..SC-003; `[FIX-SL §4.2.3]`; amends S-019; `src/session/session.cpp` acceptor `NotConnected` arm; witnesses `tests/session/test_acceptor_logon_sending_time.cpp` cells 1–14.)*

**L-038-1 — Absent or empty `SendingTime(52)` on the acceptor first-Logon is rejected with `SessionRejectReason=10` (SendingTimeAccuracyProblem), not `RequiredTagMissing=1`.** QuickFIX-cpp and QuickFIX-J disposition a missing `52` field as `RequiredTagMissing(1)`. fixpp uses `reason=10` (SendingTimeAccuracyProblem) for absent, empty, malformed, AND stale — a documented divergence in favour of internal consistency with the existing established-session path (which already maps empty `52` → `reason=10`). Operators interoperating with strict `RequiredTagMissing`-discriminating counterparties should note the difference. **Status: wontfix** (intentional, grounded in the spec.md Clarifications 2026-06-14 `reason=10` decision). *(038 FR-003; `[FIX-SL §4.2.3]`; `src/session/session.cpp`.)*

**L-038-2 — The acceptor first-Logon `SendingTime` reject shape is Reject + disconnect with NO Logout (diverges from QuickFIX's Logout-first shape); a dedicated LIVE bad-`SendingTime` cross-engine interop witness is DEFERRED.** fixpp's pre-establishment reject (absent/malformed/stale `52`) emits `Reject(35=3)` → `Disconnected`, mirroring the in-arm `1137` reject (live-proven vs QFcpp/QFJ in 033) and NOT the established-session Q3 path (which emits Logout first, as it is tearing down a live session). QuickFIX-cpp/QFJ `doBadTime` emit a Logout-with-text before disconnecting. This shape divergence is intentional and consistent with fixpp's own pre-establishment reject contract. A live bad-`SendingTime` cross-engine interop witness with QuickFIX-cpp and QuickFIX-J is DEFERRED to the L-021-3/L-037-2 live-interop family. **Status: deferred** (live interop witness conditioned on the Item-1 live-golden workstream; unit-level shape conformance is carried by `tests/session/test_acceptor_logon_sending_time.cpp` cells 1–9). *(038 FR-002; spec.md Clarifications 2026-06-14; `src/session/session.cpp`.)*

**L-038-3 — Benign, self-healing outbound-seqnum asymmetry under `reset_on_logon` when `52`-guard and `1137`-reject fire in sequence (Gate-A round-3 note).** When `reset_on_logon` is configured, the `52`-guard fires AFTER `ensure_hydrated_` (which seeds the outbound counter to the durable value `N`) but BEFORE the `reset_on_logon` block (which resets the outbound counter to 1). A first-Logon rejected by the `52`-guard emits a `Reject` carrying the hydrated outbound seqnum `N`, then disconnects; the `reset_on_logon` block (which would have reset the counter) is never reached, so the in-memory outbound counter remains at `N`. If the next attempt arrives with a conforming `52`, `reset_on_logon` fires and resets the counter to 1 before the Logon is processed — no net divergence. A `1137`-reject on the FIXT path has the same shape for the same reason. This asymmetry is self-healing across retry attempts and is not observable to a well-behaved counterparty (the interim state is never used to emit further frames). **Status: wontfix** (self-healing, documented, no operator action required). *(038 Gate-A round-3 commentary; `src/session/session.cpp` ordering of `ensure_hydrated_` / `52`-guard / `reset_on_logon`.)*

**G2 note — `ReconnectFsm` `credentials_rotated` callback-boundary seam hardened (2026-06-15).** The single `emit_credentials_rotated_()` call in `reconnect_fsm.cpp` is now wrapped in `try { … } catch (...) { /* contain — notification is best-effort */ }`, matching the established `authorize_logon` callback-guard shape. On the live `Session` path the callback is engine-owned and `noexcept` (a ring-buffer emit invoking no user code), so a throw is **unreachable in production**; the guard hardens the standalone-FSM injection seam. The catch falls through to the `last_active_source_`/`last_active_fp_` baseline update and the remaining attempt steps (`make()` → handshake) — the attempt is not aborted. *(038 FR-006/FR-007; `src/session/reconnect_fsm.cpp`; witness `tests/session/test_credentials_rotated_emit.cpp`.)*

**B-040-1 — All live-inbound hand-rolled FIX tag scanners are now bounded against forged-tag overflow aliasing; one of them shipped a defective guard that is now fixed.** A forged multi-digit tag token (e.g. `429496729649`) overflows the `uint32_t` tag accumulator used by the engine's hand-rolled SOH field scanners and **wraps to a small value, aliasing a security-relevant tag** (34 MsgSeqNum, 49/56 CompID, 52 SendingTime, 1137 DefaultApplVerID). 040 routes **all five** live-inbound scanners — `OffsetTable::build` (Index), `field_iterator::advance` (Scan), `interpret_logon`, `scan_first_frame_ids`, and `scan_frame_header` — through one shared `constexpr` in-loop `0xFFFF`-bound helper (`fixpp::wire::accumulate_tag_digit`, the `framer.cpp:120` reference shape); each keeps its existing disposition (the forged field is skipped/rejected and never surfaced under the aliased tag). The most central scanner, `scan_frame_header` (every inbound frame's header), had **shipped a defective `>429496729U` guard** that admitted wrap-and-continue tokens — including 52-aliasing, a regression vector against the 038 SendingTime guard — now fixed. The threat is **bounded to a TLS-authenticated, CompID-bound counterparty (015)** — no anonymous MITM (MED severity). `build_replay_frame` (which parses our own stored outbound replay frames, not inbound bytes) is a **justified exclusion** — not a forged-tag vector. The two anon-namespace session scanners were extracted to `fixpp::session::detail` headers (`scan_frame_header.hpp`, `scan_first_frame_ids.hpp`; `msgtype_classifier.hpp` pattern) for direct unit testing. A live cross-engine bad-tag interop witness is DEFERRED to the Item-1 live-golden workstream (L-038-2 / L-021-3 family). *(040 FR-001..FR-009, SC-001..SC-005; `include/fixpp/wire/tag_scan.hpp` + the 5 scanner sites; witnesses `tests/wire/tag_scan_test.cpp` + `*_overflow_test.cpp` × 5.)*

## 041-validation-gate-wiring (2026-06-16)

**B-041-1 — Opt-in dictionary-driven inbound validation is now wired into the live session path (default OFF), resolving the B-004-1 / B-005-7 unwired-validator gap under strict mode.** A new per-session flag `SessionConfig::validate_inbound_messages` (default `false`) enables the previously-dead `wire::dictionary_driven_validator` on the inbound path. When enabled, every inbound message processed in the `NotConnected`/`LogonSent`/`LogonReceived`/`Active` FSM states (including the establishing Logon) is validated against the session dictionary **before that arm's sequence-number gate** — and, in the Logon-bearing arms, **before `interpret_logon()`** (validate-first) — for: standard-header field order, undefined tags, required-field presence, field-value type conformance, and repeating-group structure. A violation emits `Reject(35=3)` with `SessionRejectReason ∈ {14 header-out-of-order, 2 unexpected-tag, 1 required-missing/group-structure, 5 type-nonconformant, 6 Float precision-loss (see L-041-3)}` and does not advance seqnum. The `LogoutSent`/`Disconnected` drain states are excluded, and the existing `Reject(35=3)`/`Logout(35=5)` no-reject-loop exemption is preserved. At default (flag `false`) the validator is never constructed and the early `MessageView` parse never runs — byte-identical to the prior release (FR-002/SC-005, witnessed by `has_validator_for_test()==false`). *(041 FR-001..FR-006/FR-009..FR-011, SC-001..SC-003/SC-005; `[2b §6.5]`; `[FIX50SP2 §2.1]` for 373; supersedes the "UNWIRED / [RATIFY]" status of B-004-1 / B-005-7 under opt-in; `include/fixpp/dict/{field_type,table_view}.hpp`, `Dictionary::as_table_view()`, `src/session/session.cpp` `on_inbound_frame`; witnesses `tests/session/test_validate_gate_{inbound,logon_arm,default_off}.cpp`.)*

**B-041-2 — The Engine clock-config gate is now wired: `Engine::start()` returns `expected_t<void>` and rejects a null time source with `clock_not_set`, resolving B-007-2.** `Engine::start()` changed from `void` to `[[nodiscard]] core::expected_t<void>` and calls `validate_engine_config()` at entry, returning `clock_not_set` (`core::error` slot 54) before any session loop is spawned when `EngineConfig::clock == nullptr`. The gate is unconditional (not configurable — FR-008): a null clock is always invalid. Zero production callers existed (no C-ABI wrapper), so the only public-API impact is the return-type change. A valid clock starts and operates unchanged (FR-009). Note: activating a real engine clock also activates the session-level `SendingTime(52)` MaxLatency guard that is inert under a null clock — test frame builders feeding live sessions must stamp `52` (the 038 pattern). *(041 FR-007/FR-008, SC-004; `[2d §4.4]`; supersedes B-007-2 "UNWIRED" status; `include/fixpp/session/engine.hpp`, `src/session/engine.cpp`; witness `tests/session/test_engine_clock_gate.cpp`.)*

**L-041-1 — Enum-value conformance is NOT validated even with strict validation enabled (deferred to the 2c enum-table work).** The production `table_view::enum_valid()` returns `true` unconditionally (Phase-1) because `FieldRef::enum_table_index` is a reserved-but-unbacked slot — no enum-value tables exist yet. A field whose value is a wrong enum constant but a correct type is **accepted** under strict mode. This is the one validator input (of six) the production dictionary cannot yet feed. **Status: deferred** (2c enum tables → back `enum_table_index` → flip `enum_valid` real). *(041 FR-005; `field_ref.hpp` `enum_table_index`; supersedes part of L-003-3.)*

**L-041-2 — FIXT application-message validation uses the session dictionary only; full two-dictionary resolution (application dictionary by `DefaultApplVerID`) is deferred.** QuickFIX validates a FIXT application message against BOTH the session (FIXT.1.1 transport/admin) dictionary AND a separate application dictionary resolved from `DefaultApplVerID` (`Session.cpp:1218-1229`). Phase-1 validates against the session-held `cfg.dictionary` only; for a FIXT session the session dictionary is FIXT.1.1, so validating an application message against it would over-reject. Therefore application-message validation parity for FIXT sessions is out of scope this feature. **Status: deferred.** *(041 Clarifications 2026-06-16; Out-of-Scope.)*

**L-041-3 — `SessionRejectReason=6` (incorrect data format, the `wire_field_value_truncated` arm) is structurally UNREACHABLE with the default `FIXPP_DECIMAL_T = pod_decimal`; it is a forward-looking guard for fixed-precision decimal traits.** The validator's Float type arm maps a `decimal_precision_loss` parse error to `wire_field_value_truncated` → reason 6. But `decimal_precision_loss` is produced **only** at the cross-traits conversion boundary (`decimal<T>::from<U>()`/`to<U>()`, `decimal.hpp:82-88/109-113`, remapping `decimal_overflow`→`decimal_precision_loss` for `T≠U`); the validator calls single-traits `decimal_t::parse` (= `pod_decimal::from_chars`), which returns only `decimal_invalid_input` / `decimal_overflow`, never `decimal_precision_loss`. Both of those now remap to `wire_field_value_out_of_range` → **reason 5** (the T009a remap closing the non-`wire_*` leak). So with the default `pod_decimal`, a malformed/overflowing Float yields reason **5**, and reason **6** is never emitted on the session path. The reason-6 map entry + the validator's `decimal_precision_loss` arm are retained as forward-looking guards for an alternate `FIXPP_DECIMAL_T` whose `parse` yields precision-loss. **Status: wontfix** (forward-looking guard; SC-003's reason-6 sub-claim is waived-with-proof in the 041 completeness audit). *(041 FR-004/SC-003; `include/fixpp/wire/validator.hpp` Float arm + `reject_reason_map.hpp`; `include/fixpp/core/decimal.hpp:82-88/109-113`.)*

## 043-plaintext-tcp-transport (2026-06-17)

### Behaviors

**B-043-1 — Inbound `EncryptMethod(98)≠0` is now enforced on every session (all profiles, not just plaintext).** Prior to this feature, `interpret_logon` silently skipped tag 98 on inbound Logon (S-021 "inbound 98≠0 not handled"; TC-017 gap). 043 T030 adds an explicit scan: a received `98` field that is present but not equal to `"0"` causes `interpret_logon` to return `session_invalid_logon` — the Logon is rejected before any sequence-number or profile check. A present-but-malformed `98` (non-numeric, empty, etc.) fails closed the same way. This is **unconditional across all SecurityProfile kinds** — `interpret_logon` is profile-agnostic. The existing outbound `98=0` emit (`build_logon`) is unchanged. *(043 T030; `[const §XII.7]`; `src/session/admin_messages.cpp` `interpret_logon`; witness `tests/session/test_interpret_logon_encrypt_method.cpp` — 4 cells, mutation-tested.)*

### Limitations

**L-043-1 — `insecure_plain_tcp` provides NO peer authentication.** When `SecurityProfile::kind::insecure_plain_tcp` is configured, the TLS handshake is skipped entirely: no peer certificate is presented, verified, or captured. As a result: (1) `CompIdAuthorizationPolicy::authorize_logon` is NOT called at Logon time (the CompID↔TLS-identity binding from 015 is TLS-only); (2) `live_peer_id_` remains `nullopt` for the session's lifetime — `last_live_peer_identity()` returns empty; (3) `session_event_tls_validation_failed` is never emitted (no TLS handshake to fail). The existing 028-era `check_comp_id` (49/56 matching) is **still enforced** — only the TLS-identity layer is absent. Operators MUST ensure link-level security (colocation cross-connect, VPN/IPsec) before using `insecure_plain_tcp`. **Status: wontfix** (by design — plaintext transport intentionally omits TLS-layer auth; the `[[deprecated]]` friction at the selection site is the construction-time warning). *(043 FR-008a, D-10; `src/session/session.cpp` `install_reconnected_transport` / `attach_accepted_transport`; witness `tests/session/test_session_plaintext_authz.cpp`.)*

**L-043-2 — Plaintext accepted transports receive no TLS-validation event hooks.** A `SecurityProfile::kind::insecure_plain_tcp` acceptor session does NOT emit `session_event_tls_validation_failed`, does NOT call `set_listener_events` with TLS validation callbacks, and returns no `handshake_result` peer-identity — those hooks are TLS-only. An operator registering a `tls_validation_failed` listener on a plaintext session's `SessionEvents` will receive zero events. **Status: wontfix** (by design — there is no TLS handshake on a plaintext transport). *(043 E-7, D-10; `src/session/engine.cpp` `run_accept_loop`; witness `tests/session/test_session_plaintext_authz.cpp`.)*

## Performance characteristics — phase-9 cross-engine benchmarking (2026-06-19)

> Cross-cutting (not a single feature). The figures below are **indicative, not publishable** — measured on WSL2 without core isolation, so *relative ordering* is sound but *absolute magnitudes* are not. Method + full data: parent `phases/phase-9/materiality-assessment.md` and `phase-9-harness/COMPARISON-fixpp-vs-quickfix.md`. Governed by `[const §VIII §2]` (session throughput parity-or-better with QuickFIX; latency measured/reported, no constitutional absolute).

### Behaviors

**B-PERF-1 — fixpp is competitive on the steady-state message path and dominates connection establishment.** Indicative depth-1 request→response latency sits between the mature native engine and the JVM engine (fixpp p50 ~423µs; QuickFIX-cpp ~349µs; QuickFIX-J ~569µs), and connection establishment is 4–33× faster than both QuickFIX engines. A throwaway multi-session scaling probe showed a single io_context worker sustains ~16k msg/s aggregate across 32 concurrent self-paired sessions with bounded p99 (~2.4ms) — ample for the target market's scale. *(phase-9 `COMPARISON-fixpp-vs-quickfix.md` + `materiality-assessment.md` §8; `[const §VIII §2]`.)*

### Limitations

**L-PERF-1 — Under deep single-session pipelining, burst-tail latency degrades ~2.4× vs QuickFIX-cpp.** With many messages in flight on ONE session (in-flight depth 64–256), fixpp's p99/p99.9 tail is ~2.4× QuickFIX-cpp's (≈ on par with QuickFIX-J's); **throughput stays at parity** (~3% of QFcpp). The cause is architectural: the engine multiplexes all session strands on a single `io_context` (vs QuickFIX's per-session OS threads), so one deeply-pipelined session cannot spread its in-flight work across cores. The depth-1 majority path (order→ack / quote / cancel) is unaffected and competitive (B-PERF-1). **Status: known limitation — tracked v1.1 candidate** (product disposition 2026-06-19, materiality "PARTIAL/HEDGE": NOT a v1.0 release gate; revisited post-v1.0 only if a burst-sensitive use case — market-making / mass-quote — materializes). The per-session-executor threading-model change that would close it is an invasive public-API change, deliberately out of the v1.0 critical path. NOTE: the per-dispatch executor-allocation optimization is NOT the lever (a partial mitigation only; the lever is the threading model). *(phase-9 `materiality-assessment.md` §2/§7/§8; root cause in `phases/phase-9/benchmark-readiness.md` + hot-path profiling `/tmp/perf-out/FINDINGS.md`.)*

## 044-toml-session-config (2026-06-19)

### Behaviors

**B-044-1 — RESOLVED (T039, PR #140): tomlplusplus 3.4.0 aborted or invoked undefined behaviour on certain malformed TOML table headers, bypassing the loader's `noexcept` boundary.** The loader wraps the tomlplusplus parse call in a `try/catch(...)`, which is sufficient for normal parse errors. However, `TOML_ASSERT_ASSUME(...)` inside the parser's `parse_key()` method fires `assert()` → `abort()` (debug/ASan builds) or `__builtin_assume(false)` → UB (release/NDEBUG builds) on attacker-controlled malformed input such as `[ [key]]` (space before the second `[`). In the debug/ASan path `abort()` is not a C++ exception and therefore escapes the `try/catch`, violating FR-012 rule-9 (the central guarantee that every input yields either a `ConfigBundle` or a vector of diagnostics, never a fatal signal). In the release path `__builtin_assume(false)` is UB. Reproducer: `tests/config/fuzz/crashes/repro_toml_assert_assume.toml`. A fuzzer-discovered defect (T037 libFuzzer harness). **Status: RESOLVED 2026-06-20 (T039, PR #140).** Fixed with `src/config/toml_include.hpp` — a single ODR-safe shim that, across the tomlplusplus include, saves & `#undef`s `NDEBUG` (so the overridable `TOML_ASSERT` path stays active in all build modes) and redefines `TOML_ASSERT` to THROW a catchable `std::logic_error` that unwinds into the loader's existing `catch` as a `parse_error` diagnostic. All config TUs route tomlplusplus through the shim (ODR). Validated RED→GREEN on the reproducer in BOTH asan/Debug and release/NDEBUG, plus a ≥10-min ASan fuzz from the seeded corpus in both build modes with zero crashes. *(044 T037/T039; FR-012 rule-9; `src/config/toml_include.hpp`; `tests/config/fuzz/crashes/repro_toml_assert_assume.toml`.)*

### Limitations

**L-044-1 — `reject_policy` is file-recognized but not file-selectable (step-1 capability limit).** `SessionConfig::reject_policy` is a recognized field in the loader's scalar mapper, but the underlying `RejectPolicy` enum (owned by feature 005) is **forward-declared only with no enumerators defined in this checkout** — no canonical token can be mapped from a string. A TOML file containing a `reject_policy` key resolves to `recognized_not_yet_supported_step2` (not `unknown_key`) so that the diagnostic is informative rather than misleading. The programmatic path to `reject_policy` is unaffected (host-built `SessionConfig` sets it directly). **Status: step-1 capability limit — resolved when feature 005 lands the `RejectPolicy` enum enumerators.** *(044 data-model E-6; `src/config/scalar_mappers.cpp`; `neg_multi.toml` negative battery fixture.)*

**L-044-2 — A missing `security_profile.kind` produces both a primary `missing_required` diagnostic AND a redundant resolver-layer `invalid_or_contradictory_selector` diagnostic.** The loader runs validation (which emits `missing_required` at `session[0].security_profile.kind`) and selector resolution (which also attempts to build the transport selector and emits `invalid_or_contradictory_selector` at `…transport` because no security-profile kind was provided) as independent passes in collect-ALL mode. Both diagnostics describe the same root cause; the second is redundant but not incorrect. An optional cleanup — suppressing the resolver arm when `security_profile.kind` is absent — would yield a single-diagnostic result for this input. **Status: known / low-priority — redundant but not contradictory; optional cleanup tracked in tasks.md T033 notes.** *(044 data-model E-3; `src/config/selector_resolver.cpp`; witnessed in the US2 negative battery.)*

## 045-observability-config (logging leg, 2026-06-20)

Extends the 044 loader to hydrate the existing `fixpp::log::Logger` (file / syslog / OTLP-log sinks) from `[logger]` / `[[logger.sinks]]`. One additive `shared_ptr<Logger>` field on `EngineEstablishment` (host wires it onto `EngineConfig::logger`); per-session via the existing `SessionConfig::logger_override`. No new dependency / public type / `reason_class` / `fixpp_error_t` / wire / codegen / C-ABI. Catalogue **T-044** (design row).

**B-045-1 — `[default.logger]` is inherited as a per-session `logger_override` by every session that has no explicit `[session.logger]` (MERGED semantics, user-ratified 2026-06-20).** The per-session logger is resolved from the *merged* session table (`[default]` deep-merged under each `[[session]]`), consistent with how `[default]` supplies every other session field. Consequence: when a file authors `[default.logger]`, each session without its own `[session.logger]` gets a distinct logger constructed from the default block, and the engine-level `[logger]` is shadowed for those sessions. Spec AC US3-2 ("a session that does not override it inherits the engine default — null override") describes only the no-`[default.logger]` case and is unaffected. The alternative (RAW — only an explicit `[session.logger]` creates an override) was rejected because it would silently ignore a recognized `[default.logger]`. *(045 US3 / data-model E-5; `src/config/toml_config_loader.cpp`; witnessed in `test_load_logger_overrides.cpp`.)*

**B-045-2 — The loader rejects an empty OTLP `endpoint` (`empty_required`) rather than silently dropping the sink.** The runtime `OtlpLogSink` treats an empty endpoint as a silent no-op (drops export); the loader is deliberately STRICTER and fails closed at load with `empty_required` on `logger.sinks[N].endpoint`, so a typo'd/blank endpoint is loud at config time rather than a silent telemetry hole. *(045 spec Edge Cases / FR-014; `src/config/logger_resolver.cpp`.)*

**L-045-1 — Inherited-017 preflight→construct TOCTOU: a sink whose `open()` fails AFTER the load-time preflight is silently disabled by the `Logger` constructor.** The loader's fail-closed guarantee ("nothing opened on a failed load") rests on a side-effect-free load-time resource preflight (file-sink directory exists + writable [stat/access only], OTLP cert readable + PEM-magic, endpoint non-empty) followed by live `Logger` construction ONLY on a clean whole-file accumulator. The `Logger` constructor (017) opens every sink and **silently disables (and counts) any sink whose `open()` fails** — so a narrow time-of-check/time-of-use window remains between the preflight and construction (e.g. the directory is removed, or the cert becomes unreadable, in that window). This is the inherited 017 logger contract, unchanged here; a future tightening could read the post-construction sink-error counters and fail closed. **Status: named, bounded inherited limitation.** *(045 research D-7 / spec FR-014/FR-015; `src/config/logger_resolver.cpp` `construct_loggers_if_clean`; `src/log/logger.cpp`.)*

**L-045-2 — Syslog sink + the file-sink writability *preflight* are POSIX-only (FileSink itself is now cross-platform).** `kind="syslog"` resolves to a real `SyslogSink` only on a build where `<syslog.h>` is present (`FIXPP_HAS_SYSLOG`, POSIX); on a non-POSIX build it fails closed with `invalid_or_contradictory_selector` (loud, never silently skipped — FR-013). **SyslogSink cannot be ported** — Windows has no syslog API (its equivalent is the Event Log, a different model); on Windows, use `FileSink` (local disk) or `OtlpLogSink` (remote collector). The file-sink directory-writability *preflight* still uses POSIX `::access(W_OK)`, guarded `#ifndef _WIN32`, so on Windows that load-time sub-check is skipped. **Update (2026-06-22, the Tier-2 Windows enablement): `FileSink` is now a single cross-platform implementation** — `std::fopen`/`std::fwrite` + a platform durability shim (`::fdatasync` POSIX / `::_commit` Windows) and `::fileno`/`::_fileno` (`src/log/file_sink.cpp`); binary mode keeps `\n` line endings and byte-exact accounting on both. So Windows DOES have local-disk logging now; only the *writability preflight* remains POSIX-only. If a non-writable directory is reached at runtime on either platform, it degrades to the named [[L-045-1]] limitation (`FileSink::open()` → `log_sink_open_failed` → the `Logger` ctor disables **and counts** the sink via `sink_error_counts_`, not a silent fail-open). A portable Windows writability probe (`_waccess`) is the only remaining deferral. **Status: SyslogSink POSIX-only by design (un-portable); FileSink cross-platform; Windows `::access` writability preflight deferred.** *(045 FR-013/FR-014 + Tier-2 Windows port; `src/config/logger_resolver.cpp`, `src/log/file_sink.cpp`; witnessed `SyslogBuildConditional` in `test_load_logger_negative.cpp` + the 6 `tests/log/test_file_sink_*` tests.)*

## 048-async-mutex-strand-reap (strand-local drain simplification, 2026-06-22)

Supersedes the unmerged 047 converging-loop approach (PR #143 to be closed). 047's `async_mutex` B&L
entries never reached `main`, so there is nothing to supersede here; this section is the canonical
record. Amends NFR-016. Branched off `main`; 046 (PR #142) rebases on this. Design: `.specify/2f-async-mutex.md`
Erratum **E-5**; `specs/048-async-mutex-strand-reap/`.

### Behaviors

**B-048-1 — `cancel_and_drain()` is a synchronous strand-local single-pass reap with a strand-local quiescence loop; it converges on every begun acquirer/holder/parked-waiter under the SUPPORTED (strand-serialized) topology, by construction.** The reaper sets `draining_`, then loops: reap both lists (`state_` LIFO + `next_drain_head_` FIFO; each parked waiter CAS `queued→cancelled`, `result_=unexpected{sync_lock_aborted}`, posted resume via `schedule_record_resume` which is the sole `in_flight_resumers_` incrementer), breaking only when `active_holders_count_==0 ∧ in_flight_resumers_==0 ∧ both lists empty in one pass`, else `co_await asio::post(executor)` to let a pre-drain holder's `unlock()` and the posted resumers run; finalize CAS `state_ locked_no_waiters→not_locked` then `draining_complete_` (release). The cross-thread "convergence" machinery (the `drain_latch_state`/`concurrent_channel` latch, `signal_release`/`signal_abort`/`async_wait`, the `active_acquirers_count_` epoch, the `draining_`↔counter handshake, the reaper cancel-slot + abort path) is REMOVED — it existed only to wait on OTHER threads, and the drain is contractually strand-confined. The residual multi-threaded orphan that the 047 converging-loop left (047 W-B1, 3/25 standalone) is eliminated by construction. A reentrant `cancel_and_drain()` on the strand AWAITS `draining_complete_` and returns the terminal result (never eager-ok). The drain is UNINTERRUPTIBLE (disables its own cancellation; no `sync_lock_aborted` drain return). Reaped/parked waiters keep `sync_lock_aborted`; new post-`draining_` acquirers keep `sync_lock_drained` (no observable result-code change, FR-007). Witnessed: `sync_drain_strand_local_reap` (SC-001 stress, self-deadline), `sync_drain_immediate_destroy` (no immediate-destroy UAF — `in_flight_resumers_==0` barrier), `sync_drain_reentrant_during_active`, `sync_drain_onstrand_cancel` (single-winner CAS), `sync_drain_predrain_holder`, `sync_drain_awaitable_cancellation` (uninterruptibility) — all GREEN debug + ASan + TSan-libstdc++. *(048 FR-001/002/004/005/007; `include/fixpp/core/sync/async_mutex.hpp`; `.specify/2f-async-mutex.md` E-5.)*

**B-048-2 — `async_lock()` lock-setup `inherited_slot.assign` fails closed (`sync_lock_alloc_failed`) instead of `std::terminate()` under OOM.** asio `cancellation_slot::assign` allocates the handler closure and can throw `bad_alloc`; escaping the `noexcept` `await_suspend` would terminate the process (killing all sessions). The site is wrapped in a `try/catch(...)` that, since the handler is already moved into the awaiter, completes via the POSTED runner with `unexpected{sync_lock_alloc_failed}` (exact ref-balance mirrors the shipped `store_executor` fail-close exit + the `draining_` branch). `reaper_slot.assign` is eliminated with the reaper park. Witnessed structurally + by the live `sync_pmr_fallback` sibling (the `store_executor` fail-close path: PMR exhausted → `sync_lock_alloc_failed`, no terminate). *(048 FR-003; `async_mutex.hpp` `async_lock`.)*

### Limitations

**L-048-1 — `async_lock`'s resumption `asio::post` and the drain holder-yield `asio::post` remain pre-existing OOM-terminate sites (deferred non-allocating-completion redesign).** Both posts sit inside `noexcept` contexts; on `bad_alloc` they `std::terminate()`. The resume post (`async_mutex.hpp` `resume_fn_`) is PRE-EXISTING on `main` (shipped behavior, unchanged by 048); the drain holder-yield post REPLACES the shipped `drain_latch_state::async_wait` allocation (same OOM-terminate class, not a net-new exposure). A non-allocating (pre-reserved / associated-allocator) completion redesign was evaluated at Gate A and DROPPED as unproven (no asio preflight seam — Gate-A P2-1; cf. E-4 `cancellation_slot` has no allocator hook); deferred. Same class/treatment as 047's L-047-2. OOM-only marginal degradation (process-wide exhaustion imminent). **Status: deferred known limitation.** *(048 D-3 / FR-003; `async_mutex.hpp` `resume_fn_` + the `cancel_and_drain` yield.)*

**L-048-2 — Genuinely-concurrent (non-strand-serialized) `cancel_and_drain()` overlap is UNSUPPORTED / UNDEFINED, documentation-enforced (no production assertion seam).** The drain contract is narrowed to the strand-serialized topology (the only one the production consumers use — 2 drain consumers, both on the session strand). Ordinary cross-thread `async_lock`/`unlock` contention stays SUPPORTED (the §1.1 cross-domain seam); ONLY drain-overlap is narrowed. A consumer that drives a concurrent drain via a `direct_executor` that attests-but-violates serialization (INV-2) is UNDEFINED. `async_mutex` stores no executor, so there is no cheap production assertion seam (Gate-A P2-4); the contract is documentation-enforced (FR-006 demoted to documentation-primary). **Status: documented unsupported topology.** *(048 FR-006 / contract §Unsupported; `async_mutex.hpp` `cancel_and_drain` doc-comment.)*

## 049-c-abi-handles-errors (C ABI Feature A — handles, error surface, version, 2026-06-23)

Foundation C-ABI surface (CA-001..004): the opaque-handle catalogue, the full bounded
`fixpp_error_t` master enum + `fixpp_strerror`, the per-symbol reentrancy contract, and the
runtime version accessors + macros. ABI-affecting; all four Article X §6 controls applied
(Gate A converged r3). Spec: `specs/049-c-abi-handles-errors/`. C-ABI version 0.1.0→0.2.0
(stays pre-1.0; GA does the 0→1 freeze).

### Behaviors

**B-049-1 — `translate(fixpp::core::error)` is a total, audited coalescing switch (116 enumerators → published `fixpp_error_t`), engine-internal; only `fixpp_strerror`/`fixpp_version`/`fixpp_library_version` cross the boundary.** The switch has NO `default` (`-Wswitch` enforces totality over all 116 enumerators); per-arm coalescing is the audited data-model E-3 decision, locked by a checked-in correctness oracle (`tests/capi/expected_error_map.csv` driven through `translate()`, mutation-tested — flipping one arm goes RED). `fixpp_strerror` returns a pointer into static storage (same pointer on repeat call, zero-alloc); out-of-range/undefined/reserved → `"unknown error"`. The provisional decimal codes were renumbered into their `[2i §4.3]` master blocks (`BUFFER_TOO_SMALL` 3→6, `DECIMAL_INVALID` 10→800, `DECIMAL_PRECISION_LOSS` 11→801); `src/capi/decimal.cpp` routes through the shared `translate()`; references are by macro name so the change is value-transparent. Exported surface is `fixpp_*`-only (0 C++ leak, nm gate); `fixpp_version()`={0,2,0}, `fixpp_library_version()`={0,0,1} (decoupled tracks). Occupancy gate (`tools/check_capi_occupancy.sh`, Check A header layout + Check B source-domain counts, never compared) + discrete reentrancy gate (`tools/check_capi_reentrancy.sh`, exactly-one class per exported symbol's doc-block) wired into Tier-1. *(049 FR-006..014/017; `include/fix/c_api/{error,version,handles,export}.h`, `src/capi/{error,version}.cpp`.)*

### Limitations

**L-049-1 — the forward-compat error downgrade (`translate_for_consumer`) is implemented as a pure function but is NOT yet wired to a live `consumer_minor`.** All current codes carry `introducing_minor=2`; the downgrade (`introducing_minor > consumer_minor → FIXPP_ERR_UNKNOWN`) is unit-tested directly (consumer_minor=1 downgrades all minor-2 codes; consumer_minor≥2 passes through), but the point that *records* `consumer_minor` — `fixpp_engine_create` — is Feature B / `[2j]`. CA-004 is "accessors + macros + downgrade rule"; the end-to-end binding lands in Feature B. **Status: by-design boundary; not falsely-complete.** *(049 FR-009 / research D-3.)*

**L-049-2 — `session_*`, `log_*`, `otel_*`, `app_*` C++ error variants (and `out_of_memory`) map to `FIXPP_ERR_UNKNOWN` at the C ABI in v1.0.** `[2i §4.3]` publishes no session block and no `#define` for the 1000–1099 log/otel block; `app_*` is annotated reserved/future; `out_of_memory` has no cross-cutting OOM code and a `switch(error)` cannot be call-site-dependent. There is no Feature-A function that produces these, so `→UNKNOWN` is unobservable here. The enumerating oracle asserts these arms `== FIXPP_ERR_UNKNOWN` EXPLICITLY so a Feature-B refinement trips a test rather than silently shifting the surface. **Updated (051): the reachable `session_*`/`app_*` arms are DISCHARGED** — 051 published the `[1400,1499]` block and `translate()` re-points ordinals 119/77/129/130/131 to named codes (see **B-051-3**; `src/capi/error.cpp:131-134`/`:218-223`). This limitation now covers ONLY `log_*`/`otel_*` (`[1000,1099]`, still reserved — L-051-1) and `out_of_memory`, which remain `FIXPP_ERR_UNKNOWN` (`error.cpp:204-213`). **Status: narrowed to log/otel + OOM; documented v1.0 behaviour.** *(049 research D-8 / data-model E-3.)*

**L-049-3 — Windows C-ABI static-link witness deferred (empirical validation, not correctness).** PR #146 corrects the `include/fix/c_api/export.h` `_WIN32` export macro so the shipped **static** archive (`fixpp_capi`) exposes plain `fixpp_*` symbols (static-archive default = empty `FIXPP_API_EXPORT`; `__declspec(dllexport)`/`dllimport` reserved behind an explicit `FIXPP_CAPI_SHARED` opt-in not used by any shipped artifact today). The corrected macro is reasoned-correct (standard static/shared ladder), leaves the POSIX `visibility("default")` branch byte-identical (Linux 6-preset verify green by construction), and is consistent with the test-only shared lib's `WINDOWS_EXPORT_ALL_SYMBOLS ON` path (the Python ctypes oracle resolves `fixpp_*` by name). What is **deferred** is the *empirical* MSVC compile+link witness (e.g. `tests/capi/capi_version_smoke.c` linking `fixpp_capi.lib`), because (a) no default per-PR check gates Windows — the `windows`-MSVC lane is **opt-in via the `windows` label** (the 045 W-1 precedent), and (b) a local MSVC link-test requires the heavy manual MSVC sandbox loop (run strictly alone, never parallel with Linux). This limitation waives **validation timing**, NOT correctness: the pre-fix macro was *wrong* for the static archive; the post-fix macro is the correct standard form pending the Windows empirical witness on the next `windows`-labeled run. **Owner action to discharge:** add an MSVC/static-consumer compile+link assertion for `fixpp_strerror` + `fixpp_version` and run it under the `windows` label. *(049 export.h; [const §X.6]; 045 W-1 precedent.)*

### 050 — C-ABI Feature B (session lifecycle / send / receive callback, CA-005/006/007)

**B-050-1 — the C-ABI engine lifecycle is register-then-start-ONCE, and the boundary owns the event loop the C++ `Engine` does not.** The C++ `Engine` owns no worker threads (`engine.hpp:222`), so the C-ABI engine owns an internal `io_context` + worker thread(s) (count via `fixpp_engine_config_set_worker_threads`, default 1; research D-2). Lifecycle: `fixpp_engine_create` → `fixpp_session_open` × N (= `Engine::register_session`, **before** start) → `fixpp_engine_start` (= `Engine::start()`, once; spawns the role loops; rejects a null clock → `FIXPP_ERR_THREAD_CONFIG`) → drive → `fixpp_session_close` → `fixpp_engine_destroy` (drives `Engine::stop()` to completion **unconditionally**, even on a never-started engine, resets the work-guard, joins the worker(s), deletes the `Engine`; idempotent / NULL-safe / never-throws). **open ≠ connected**: establishment is asynchronous after start, so a consumer polls `fixpp_session_is_established` (= `Engine::lookup(id) != null && Session::is_open()`, `FIXPP_THREAD_SAFE`, FR-022) before sending. All engine/lifecycle symbols are `SINGLE_THREAD` per handle (`[2i §4.10]`). *(050 FR-001..004/022; `include/fix/c_api/engine.h`, `session.h`; witnessed `tests/capi/lifecycle_test.cpp`.)*

**B-050-2 — `fixpp_session_send` takes an application-message PAYLOAD (not a `fixpp_msg_t`, not a full wire frame), is `FIXPP_THREAD_SAFE`, and the session stamps the header/trailer and assigns `MsgSeqNum`.** The `frame`/`len` span MUST lead with `"35=<msgtype>\x01"` (an application MsgType) and contain only application fields, SOH-terminated; the session itself stamps `8/9/49/56/52/10` and **assigns the in-sequence `34`**, so a payload carrying session framing tags (`8/9/34/49/52/56/10`) at a field boundary is rejected (the `session.cpp` fail-closed opaque-payload validation; maps to `app_payload_malformed`, currently `FIXPP_ERR_UNKNOWN` per L-050-4). Send is callable from any consumer thread (= `Engine::send`, any-thread). The reachable return set is per data-model E-4 (see L-050-3/L-050-4 for the deferred/swallowed arms). Outbound `fixpp_msg_*` construction is Feature C. *(050 FR-007..010; `include/fix/c_api/session.h`; `tests/capi/send_recv_test.cpp`.)*

**B-050-3 — the inbound receive callback runs SYNCHRONOUSLY on the session strand on a fixpp-owned worker thread, and its `fixpp_msg_t` is valid ONLY for the callback's duration.** `fixpp_session_register_callback` installs a `{cb, userdata}` slot keyed by `SessionId` (read on the session strand); for each inbound application message the engine's internal `CapiApplication` trampoline invokes `cb` with a stack `fixpp_msg_t` (FR-013). Retaining the `inbound` handle past the callback return is a use-after-free (witnessed under ASan, SC-008). The callback MUST NOT make a **blocking** C-ABI call on its own engine/session (`fixpp_session_send`/`fixpp_session_close`/`fixpp_engine_destroy`) — it holds the strand and the blocking thunk posts onto the same strand, so it deadlocks (FR-013a); the supported "reply" pattern is copy-out-then-send-from-a-non-callback-thread (witnessed end-to-end, D-11/SC-001). No callbacks are delivered after `fixpp_session_close`. A callback-safe non-blocking send and richer `onLogon`/`onLogout` lifecycle callbacks are deferred (L-050-x). *(050 FR-011..013a; `src/capi/engine.cpp` `CapiApplication`; `tests/capi/send_recv_test.cpp`.)*

**B-050-4 — `fixpp_session_close` of a once-established, now-reaped session is an idempotent `FIXPP_ERR_OK`; a never-established session closes as `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` (issue #151).** A reaped session is NOT gone from the registry — on peer disconnect the engine resets only the live transport and **retains** the registry entry (`unpublish_entry()`; only `Engine::stop()` clears it), so `Engine::lookup(id)` is still non-null. The reaped close therefore reaches `fixpp_session_close`'s **else branch**, where `Session::close(graceful)` on the now-`closed_drained` session returns `session_already_closed`. That one error conflates two lifecycle states: (a) the session was established at least once (logged on) and the peer has since disconnected — a normal idempotent close → `OK`; (b) the session was published but **never** logged on (e.g. an initiator that connected and sent Logon but the peer never acked, then drained) — the never-established outcome → `THREAD_SESSION_LIFECYCLE`. The two are distinguished by a sticky per-session `ever_established` latch (set once on the first `onLogon`, never reset — `established` alone is cleared by `onLogout`, so it cannot tell them apart). The `lookup()==nullptr` (null) branch remains the never-published never-established outcome (`THREAD_SESSION_LIFECYCLE`). Either way the handle is invalidated, so a subsequent `close` returns `FIXPP_ERR_INVALID_HANDLE`. Before this fix the reaped case returned `THREAD_SESSION_LIFECYCLE` (downgraded to `UNKNOWN` for a `consumer_minor<2` engine), surfacing as a spurious close error on a clean disconnect. **Status: fixed (issue #151); a public C-ABI return-contract change, landed before the 0→1 GA freeze.** *(`src/capi/session.cpp` `fixpp_session_close` else branch; `src/capi/engine.cpp` `CapiApplication::onLogon`; the OK arm witnessed end-to-end by `tests/capi/send_recv_test.cpp::CloseReapedSessionIsIdempotentOk` (real disconnect-first reap to `closed_drained`); the latch-gated `THREAD_SESSION_LIFECYCLE` arm by `CloseReapedNeverEstablishedIsLifecycle` (a seam-driven branch-discrimination witness — it flips the `ever_established` latch off on a really-drained session rather than naturally producing a connected-but-never-logged-on drain); and the never-published case by `tests/capi/lifecycle_negative_test.cpp::CloseNeverEstablishedIsLifecycleOutcome`.)*

**L-050-5 — Feature B's session-config builder exposes no transport-endpoint setter (by design); the round-trip injects the endpoint via a test-only seam.** Transport configuration (initiator connect target / acceptor listen address) is delegated to the 2j control plane per `[2i]` non-goal #7, so `fixpp_session_config_*` has no endpoint setter. The SC-001 loopback round-trip sets `SessionConfig::reconnect_endpoint` + a bilateral-lenient reset policy through a documented test-only cast bridge (`capi_loopback_support.hpp::set_loopback_endpoint`), parallel to the L-050-1 dictionary seam. A pure-C consumer configures transport through the 2j surface, not Feature B. **Status: by-design boundary; transport config owed to 2j.** *(050 spec SC-001; [2i] non-goal #7.)*

**L-050-y — a LIVE in-flight-`fixpp_session_send`-during-`fixpp_engine_destroy` cancellation (→ `FIXPP_ERR_CANCELLED`) is not safely expressible through the public C-ABI.** The C-ABI destroy contract is single-thread / quiesce-before-destroy: `fixpp_engine_destroy` frees the `fixpp_engine` and its `fixpp_session` storage, so a concurrent `fixpp_session_send` that dereferences `session->engine` is a use-after-free (ASan-confirmed), NOT a bridge defect — the caller MUST stop issuing session ops before destroying the owning engine. The FR-010 cancel-coalescing **mapping** (`operation_cancelled`/`connection_aborted`/… → the uniform `FIXPP_ERR_CANCELLED`) is witnessed at the `translate()` oracle (`error_block_test`/`error_surface_test`); the **safe sequential** teardown observable (send after `fixpp_session_close` → clean `FIXPP_ERR_INVALID_HANDLE`, never abort/UB) is witnessed by `lifecycle_test` (SC-007b rescoped). **Status: documented contract boundary; the live concurrent-cancel race is out of scope for the public single-thread-destroy ABI.** *(050 SC-007b; FR-010; tests/capi/lifecycle_test.cpp.)*

**L-050-3 — a store *I/O persistence* failure on the send path is NOT observable as a `fixpp_session_send` error.** The engine's durable-before-transmit store on `Session::send` is **logged-then-proceed** — the inherited I-07 invariant shared with 007/024/029/032: `store_then_emit` swallows store errors (`(void)store_r;`, `src/session/session.cpp:4790`), propagating only `operation_aborted` (as `dispatch_aborted` → `FIXPP_ERR_CANCELLED`). So a C consumer cannot observe a store I/O failure (`store_io_failure` 56 / `store_capacity_exhausted` 59) as a distinct send return; the only **store-domain** code reachable on `send` is `store_seqnum_overflow` (outbound counter at `seqnum_max`, `seqnum_manager.cpp:111-114`) → `FIXPP_ERR_STORE_RUNTIME`. This is an inherited engine behaviour, not introduced by Feature B — a wrapper feature cannot make a swallowed persistence failure surface; doing so needs a wider engine change (out of scope). Gate A round 3 corrected the round-1 E-4 mislabel of `store_io_failure` as send-reachable. **Status: inherited engine invariant; documented v1.0 behaviour.** *(050 data-model E-4; spec AC3/US2; `src/session/session.cpp:4790`.)*

**L-050-x — `onCreate`/`onLogon`/`onLogout` lifecycle callbacks at the C boundary are deferred; v1.0 ships only the FR-022 establishment poll accessor.** The C++ `Application` has the richer lifecycle callbacks, but Feature B exposes only `fixpp_session_is_established` (poll) for a consumer to wait for logon before sending (SC-001). Surfacing the establishment *transitions* as C callbacks (and the companion callback-safe non-blocking send noted in B-050-3 / FR-013a) is a v1.x follow-up. **Status: documented follow-up; out of scope for Feature B.** *(050 spec Out of Scope / FR-022 / FR-013a; clarifications 2026-06-24.)*

### 051 — C-ABI Feature C (message field/group accessors + outbound construct/commit + toApp hook, CA-008/009/010)

**B-051-1 — a pure-C consumer can read any inbound field/group and construct/commit/send any outbound message entirely through `extern "C"`; the Python bindings unblock on this 33-symbol surface.** Inbound READ (CA-008/CA-010-read, `message.h`): `fixpp_msg_get_{string,bytes,int,double,decimal}` + `has_tag`/`version`/`get_msg_type` + `fixpp_msg_get_group`/`group_get_field_*`/`get_nested_group` — thin thunks over `wire::MessageView::get`/`OffsetTable::group_slices`; string/bytes ALIAS the wire buffer (no copy); zero-global-heap (SC-003 dual gate: counting-resource + mallocnesia). Outbound CONSTRUCT (CA-009/CA-010-write): `fixpp_msg_create_outbound`→`set_*`/`remove_tag`/group builder (`msg_group_begin`/`group_builder_add_entry`/`entry_set_*`/`entry_group_begin`/`msg_group_end`)→`fixpp_msg_commit`→Feature-B `fixpp_session_send`→`fixpp_msg_destroy`. Steady-state set_*/commit are zero-global-heap (per-message monotonic arena). MINOR 0.3.0→0.4.0. *(051 FR-001..012; `include/fix/c_api/message.h`; `tests/capi/message_{read,write}_test.cpp`.)*

**B-051-2 — inbound messages are immutable; mutation requires `fixpp_msg_clone` first, which is also the only sanctioned cross-strand-handoff path. `fixpp_msg_destroy` is NULL-safe + single-destroy only: double-destroy of the same non-null pointer is UB (consumer must null their pointer after destroy).** `set_*`/group-build on an inbound flyweight → `FIXPP_ERR_INVALID_HANDLE` (`[2i §10] Q5`). `fixpp_msg_clone` produces a session-independent owner-controlled copy (own per-message arena, NOT token-expired by session close); clone reads are `FIXPP_THREAD_SAFE` (a documented runtime/handle-state guarantee OUTSIDE the static per-symbol reentrancy gate — the gate carries one conservative `FIXPP_REQUIRES_SESSION_LOCK` per shared read symbol). The outbound `fixpp_msg_t` is **token-expired on owning-session close/destroy** (FR-009a, lazy `weak_ptr<SessionLiveness>` token reset on every arena-teardown path) → post-close `set_*`/`commit` → `INVALID_HANDLE`, never a session-arena UAF (the accumulator is shell-owned; the token is the semantic validity gate). `fixpp_msg_destroy` **frees the shell** (unlike engine handles O(few), msg handles are per-send/unbounded — retaining dead shells would leak indefinitely; B-051-2 narrowed destroy contract vs. the engine pattern). *(051 FR-007/009/009a/018; seam #13 `tests/capi/msg_clone_cross_strand_test.cpp`; tombstone ASan+TSan in `message_write_test.cpp`.)*

**B-051-3 — the `[2i §4.3]` amendment publishes a dedicated Phase-4 session/app + message-construction error block `[1400,1499]`; the five reachable `session_*`/`app_*` arms now surface stable named codes, with a per-code forward-compat downgrade.** Six codes 1400–1405: `SESSION_INVALID_ARGUMENT`(1400)/`SESSION_INVALID_STATE`(1401)/`APP_DO_NOT_SEND`(1402)/`APP_CALLBACK_THREW`(1403)/`APP_PAYLOAD_MALFORMED`(1404) re-point the five C++ ordinals 119/77/129/130/131 off `FIXPP_ERR_UNKNOWN`; `MSG_FRAMING_TAG_FORBIDDEN`(1405) is a pure C-ABI construction reject (set_* of a framing tag 8/9/34/49/52/56/10). The scalar `kIntroducingMinor` is replaced by a PER-CODE lookup (existing codes minor 2, the six new minor 4) so a `consumer_minor=3` engine downgrades only the new codes and never the existing ones. **Discharges L-050-4 + L-049-2 (session/app arms; log/otel stay deferred-by-design — see L-051-1).** *(051 FR-013..017; `error.h`/`error.cpp`/`error_codes_v1.txt`/`check_capi_occupancy.sh`/`.specify/2i-capi.md`; `error_surface_test.cpp` + `error_block_test.cpp`.)*

**B-051-4 — a C consumer can register a send-side (toApp) callback to inspect/veto outbound messages on the originate path.** `fixpp_session_register_send_callback` (pre-start) routes through `CapiApplication::toApp` to a CLOSED-enum verdict (`fixpp_toapp_verdict`: SEND=0 / VETO=1 / ERROR=2; out-of-range → ERROR, a defined misuse path — NOT an alias of `fixpp_error_t`): SEND→transmit/`OK`; VETO→`FIXPP_ERR_APP_DO_NOT_SEND` (DoNotSend, nothing transmitted); ERROR→`FIXPP_ERR_APP_CALLBACK_THREW` (terminal-close). Inside the callback the outbound message is a read-only **FRAMED** view — session-stamped framing tags 8/9/34/49/52/56/10 ARE readable (distinct from the outbound accumulator, which forbids them). Scope is the **originate-path tap only** (ResendRequest retransmissions are not surfaced — L-019-4); `toAdmin` is not exposed in v1.0. *(051 FR-022..024; `session.h`/`session.cpp`/`engine.cpp`; `tests/capi/toapp_callback_test.cpp`.)*

**L-051-1 — log/otel C-ABI error arms remain `FIXPP_ERR_UNKNOWN` (no C-ABI functions yet; post-v1).** The `[1000,1099]` log+otel block stays reserved with no `#define`; `translate()` maps `log_*`/`otel_*` → `FIXPP_ERR_UNKNOWN`. L-049-2 is discharged ONLY for the reachable session/app arms — the log/otel leg is deferred-by-design (their C-ABI functions do not exist), not an open gap. **Status: documented v1.0 behaviour; awaits the log/otel C-ABI surface.** *(051 spec Out of Scope; FR-014.)*

**L-051-2 — outbound-accumulator clone (`fixpp_msg_clone` on an un-committed outbound handle) is not supported in v1.0; clone covers inbound + framed views.** `fixpp_msg_clone` produces an inbound-flavoured readable copy from the source's wire bytes; an un-committed outbound accumulator has no wire bytes to copy, so cloning one returns `FIXPP_ERR_INVALID_HANDLE` (CA-009 scope). A consumer clones AFTER commit/receive. **Status: documented v1.0 scope boundary.** *(051 FR-009; `src/capi/message_write.cpp` `fixpp_msg_clone`.)*

**L-051-3 — an empty repeating group (`NoXxx=0`) followed by more fields reads as absent rather than count-0 (pre-existing parser limitation, surfaced by CA-010-read).** The dict-aware `OffsetTable::group()` (hardened in 051 to make `TYPE_MISMATCH` reachable for a scalar tag) infers group extent from the delimiter, not the `NoXxx` count value; a zero-instance group that is not the last field returns an absent result (→ the read thunk reports `TYPE_MISMATCH`/`TAG_NOT_FOUND`) instead of an OK count-0 cursor. This is pre-existing (the parser never read the count value) and net-safer than the prior behaviour (which returned bogus slices). **Status: documented edge; a count-value-aware empty-group read is a post-v1 parser change.** *(051 wire `OffsetTable::group()`; deviation D1.)*

**(workstream, not L-) — `abidiff` layout-gate upgrade deferred to the final CA feature / release gate (research D-6).** The per-PR `abi-golden` gate stays the nm symbol-set check (golden list updated +3: `fixpp_library_version`/`fixpp_strerror`/`fixpp_version`); the decimal renumber is invisible to it (`#define`s, not symbols) — its safety net is the enumerating oracle + `error_codes_v1.txt`.

### 052 — C-ABI Python-readiness (dictionary loader + transport-endpoint config + inbound field iteration)

**B-052-1 — a pure-C / Python consumer can now load a real FIX dictionary from XML, configure a session's TCP endpoint and seqnum-reset policy, stand up a live two-engine initiator/acceptor pair, and enumerate inbound fields — entirely through public `extern "C"` headers; PY-001 unblocks on this 7-symbol surface.** `fixpp_dict_load_from_xml`/`fixpp_dict_destroy` (`dict.h`) construct the `fixpp_dict_t` that `fixpp_session_config_set_dictionary` consumes (construction-time thunk → `FIXPP_ERR_CAPI_CONFIG_INVALID` on `XmlLoader` throw; full-critical-section process-global mutex destroy + tombstone, TSan-clean concurrent double-destroy). `fixpp_session_config_set_tcp_endpoint`/`fixpp_session_acceptor_bound_endpoint`/`fixpp_session_config_set_reset_seqnum_policy` (`session.h`, +1 C11 `fixpp_reset_seqnum_policy` enum) promote the former L-050-1/L-050-5 test seams to public setters: a pure-C initiator dials a `host:port`, an acceptor reads back its OS-assigned port-0 binding, and the seqnum-reset policy is settable. **D-4 empirically settled: the production-default `bilateral_strict` establishes a fresh both-side-`reset_on_logon` pair through the public C-ABI (20/20 over loopback) — the E-4 LENIENT contingency was NOT needed.** Additive MINOR 0.4.0→0.5.0; NO new `fixpp_error_t` codes (occupancy gate UNCHANGED). *(052 FR-001..005b/012/014; `include/fix/c_api/{dict.h,session.h}`; `tests/capi/{dictionary_load_test,public_roundtrip_test}.cpp`.)*

**B-052-2 — field iteration exposes inbound fields as `(tag, value, len)` views aliasing the wire buffer, dictionary-agnostic, one entry per wire occurrence (a multiset), valid for the parent handle's own lifetime (the dispatch window for inbound; until destroy for a 051 clone).** `fixpp_msg_field_count`/`fixpp_msg_field_at` (`message.h`, + the PoD `fixpp_msg_field_t`) wrap `wire::OffsetTable::entries()` in wire/document order: `field_at(i)` returns `entries()[i]` `{tag, wire_base+offset, len}` with NO copy and zero global heap (mallocnesia LD_PRELOAD dual gate). Enumeration is a strict superset of the scalar getter (a repeating group's delimiter tag appears once per instance, vs `get_string`'s first-occurrence). A positive `tag_ != FIXPP_HANDLE_TAG_MSG` guard rejects a type-mismatched/destroyed handle (`INVALID_HANDLE`); `index >= count` → `INDEX_OUT_OF_RANGE`. Static reentrancy class `FIXPP_REQUIRES_SESSION_LOCK`; a 051 clone's iteration is the documented runtime-`THREAD_SAFE` cross-strand path. *(052 FR-006/007/008/011, SC-002/003; `include/fix/c_api/message.h`; `tests/capi/message_field_iteration_test.cpp`.)*

**L-052-1 — only the XML-path dictionary loader ships; bytes/string and builtin-by-version loaders are deferred (additive, post-v1 or PY-driven).** `fixpp_dict_load_from_xml` reads a filesystem path; `XmlLoader::load_from_string` and a builtin-by-version constructor are not surfaced in v1.0. **Status: documented v1.0 scope boundary.** *(052 spec Out of Scope; FR-001.)*

**L-052-2 — only a primitive `(host, port)` plaintext-TCP endpoint setter ships; transport-level handles, the `fixpp_endpoint_t` PoD, and `reconnect_policy`/`connect_info` configuration stay deferred to v1.x per `[2i §7.8]`.** GAP-002 is a recorded LOCAL Gate-A deviation (a primitive setter, NOT a reopened `[2i]`); set-time validation is empty-host only (`FIXPP_ERR_CAPI_CONFIG_INVALID`), with host-format rejection deferred to engine connect/accept. **Status: documented v1.0 scope boundary.** *(052 spec Out of Scope; FR-004/005a; `[2i §7.8]`.)*

**L-052-3 — field iteration is over parsed/inbound messages only; outbound-accumulator iteration is unspecified in v1.0.** `fixpp_msg_field_count`/`field_at` resolve an inbound/clone Index-mode `MessageView`; an un-committed outbound accumulator is not an enumeration source (consistent with L-051-2's outbound-clone boundary). **Status: documented v1.0 scope boundary.** *(052 spec Out of Scope; FR-006.)*

**L-052-4 — `fixpp_dict_destroy`'s dead-shell registry grows unbounded under load/destroy churn.** To guarantee that a second `fixpp_dict_destroy(same_ptr)` is a safe no-op (SC-004 double-destroy idempotency), the destroyed shell is retained in a process-global registry so `tag_==DEAD` remains readable indefinitely. Growth is O(load/destroy cycle count), not O(live dicts) — a Python or C consumer that repeatedly loads and destroys the same dictionary path will accumulate one small shell (~40 bytes) per call. **Mitigation: load a dictionary once and reuse the handle for the process lifetime; call `fixpp_dict_destroy` only at final teardown.** The mechanism is intentional and cannot be changed without violating the idempotency guarantee. **Status: documented tradeoff (double-destroy-safety vs memory growth under churn).** *(052 src/capi/dictionary.cpp; [2i §4.2.1]; gate-b/r1 F4.)*

### 054 — Python GIL discipline & typed exception translation (PY-002 + PY-003)

**B-054-1 — every non-OK `fixpp_error_t` surfaced to Python raises a block-matching typed subclass of `fixpp.FixppError` carrying `.code` (int) / `.name` (symbolic, e.g. `"FIXPP_ERR_DICT_CONFIG"`) / `.message` (`fixpp_strerror` text, == `str(exc)`); `fixpp.Error` is an alias of `fixpp.FixppError` so the 053 surface survives.** The hierarchy realizes `[2m §4.6]` verbatim (root + one subclass per `fixpp_error_t` block + the five `BindingError` subclasses) plus `AppError` for the post-`[2m]` `[1400,1499]` block ([2i §4.3]/051). A single exposed translator `fixpp._map_to_class(code)` (+ public alias `fixpp.exception_for_code`) is the source of truth the SWIG out-typemap routes through (no parallel C mapping). A code in a known populated block → that block's class (e.g. future `405`→`StoreError`); a wholly unmapped block → root `FixppError` (no `UnknownError` — would collide with `Unknown`/2). A header-sourced set-equality coverage test pins the mapping to the 47 `error.h` codes. **No `include/fix/c_api.h` change — the `0→1` freeze holds; AppError mints no new code.** *(054 FR-006..010, SC-001/002/006; `bindings/python/fixpp.i`; `tests/test_exceptions.py` + `test_error_coverage.py`.)*

**B-054-2 — the three blocking binding wrappers (`session_close`, `session_send`, `engine_destroy`) release the GIL around the native call; the one bound C→Python trampoline (`fixpp_py_recv_trampoline`) reacquires it.** A documented, exhaustive GIL-discipline audit table in `fixpp.i` classifies every `%include`d C-ABI function release/hold; the bound-trampoline census states exactly one bound trampoline (the `toApp`/send callback is `%ignore`d/unbound). A local-only `FIXPP_PY_GIL_RELEASE_CANARY` build elides the release bands; a two-mode subprocess witness proves the teardown-vs-in-flight-recv-callback scenario completes in a normal build (GREEN, in-matrix) and deadlocks under the canary (RED, local-only). The witness is parameterised over ALL THREE blocking wrappers (each op targets acc/eng_a whose single worker is parked mid-callback; eng_a is pinned to `worker_threads=1`): each op is proven RED 5/5 under the canary and GREEN in-matrix, independently witnessing that each band is load-bearing. A subprocess-watchdog test pins that a raising inbound callback (staged concurrently against a blocking teardown) never deadlocks the engine. *(054 FR-001..004/011, SC-003/004/007; Gate B r1 FQ-1; `bindings/python/fixpp.i`; `tests/test_gil_release_canary.py` + `test_callback_raise_watchdog.py` + `_gil_staging.py`.)*

**L-054-1 — blocking C-ABI calls (`session_send`, `session_close`, `engine_destroy`) from inside the inbound callback DEADLOCK as-built; the binding guarantee is documentary, not enforced, in v1.0.** This covers two structurally identical cases: (a) **send-from-callback:** the as-built 050 `fixpp_session_send` blocks on the dispatch (`co_spawn(ioc_, …, use_future)` + `fut.get()`; `src/capi/session.cpp:284-286`, rule `session.h:255-258`); called from inside the inbound callback (on the engine worker / session strand) the worker blocks in `fut.get()` waiting for the send coroutine to run on the *same* io_context/strand, which cannot progress — a strand/io_context reentrancy deadlock. (b) **close-from-callback:** `session_close`/`engine_destroy` drain the strand (`co_spawn(close_exec, …, use_future)` + `fut.get()`); called from inside the callback the same deadlock occurs — the strand is mid-callback waiting for the close to complete. Both are **distinct from the 053 GIL-teardown deadlock** (which the PY-002 GIL release fixes). Send: **current limitation** (the `[2m §4.6]` "Session.send-from-callback is legal" claim was predicated on a *non-blocking strand-dispatch* `fixpp_session_send`; restoring true legality is a deferred **engine** item). Close: **permanent as-designed** (the `[2m §6.5]` close-from-callback ban is correct; active pre-call `CallbackReentrantClose`/1204 detection is the PY-004 director mechanism). **Mitigation (send): from the callback, `msg.clone()` + `queue.put()` to drain + `session_send` on another thread (off the strand). Mitigation (close): call `close()` from a non-callback Python thread (e.g. a shutdown-coordinator thread).** The binding-level guarantees stay **documentary** (the module docstring states the hazards); active detection (`session._in_callback` + pre-call raises) is **PY-004** for both cases. The normative `[2m]` design is amended at send-from-callback sites (§1.3 rule (2), §3.12, §6.5 carve-out table, §4.6 `CallbackReentrantClose` NOTE) and close-from-callback sites (§1.3 rule (4), §6.5 table close row, §6.5 enforcement paragraph, §4.6 `CallbackReentrantClose` docstring, §4.7 table, §8 test #4) per Article XX. **Status: documented v1.0 behaviour (Gate B r1 FQ-2).** *(054 FR-005; `.specify/2m-pybind.md` §1.3/§3.12/§4.6/§4.7/§6.5/§8; `bindings/python/fixpp.i` module docstring.)*

### 055 — Python lifetime / ownership OO layer (PY-004)

**B-055-1 — `Engine.close()` arms the engine's Python liveness sentinel before the GIL-releasing native teardown, so concurrent public engine entry fails fast with `fixpp.ObjectLifetime` (1202) instead of entering the C ABI during close.** While `Engine.close()` is parked in child-close/native-destroy work, a second Python thread attempting `engine.open_session(...)` or `engine.start()` sees `_dead == True` and raises `ObjectLifetime` without crossing the binding/native boundary. This is a characterization of the documented concurrent-close behavior, not a new synchronization primitive: the OO layer is still GIL-governed, and the guarantee is "no stale-handle C-ABI entry" rather than multi-threaded engine mutation support. *(055 FR-007/008/016, SC-001 close-race characterization; `bindings/python/tests/test_close_flow.py`.)*

**B-055-2 — the PY-004 OO callback path actively rejects ALL THREE blocking reentrant operations (`session.send`, `session.close`, `engine.close`) with `fixpp.CallbackReentrantClose` (1204).** This is the shipped `[2m]` Article XX inversion: the GIL-protected `session._in_callback` marker is set on callback entry and cleared on every exit path, and the OO wrapper now checks it before entering the C ABI for send/close/engine-destroy. The older 054 documentary rule remains relevant only to the flat-function callback path; on the OO path the as-built send-from-callback deadlock is upgraded to a fail-fast typed exception, matching close/engine-destroy. *(055 FR-017, SC-007; `.specify/2m-pybind.md` §1.3/§6.5/§6.7/§9; `bindings/python/tests/test_reentrancy.py`.)*

**L-055-1 — on CPython 3.12, the `_fixpp` extension refuses subinterpreter import outright, so the typed `fixpp.SubInterpreterRejected` (1201) constructor guard is shadowed by a stronger import-time barrier on this build.** FR-018 still holds operationally: the binding cannot be used from a PEP 554 sub-interpreter. The limitation is that the typed 1201 path is unwitnessed here because `import fixpp` itself fails first with `ImportError: module _fixpp does not support loading in subinterpreters`. **Status: documented platform/runtime limitation of the current build, not a fixpp correctness failure.** *(055 FR-018, SC-007 note; `bindings/python/tests/test_subinterpreter.py`; quickstart verified 2026-06-27.)*

### 056 — Python wheel packaging (PY-005)

**B-056-1 — v1.0 ships ONE self-contained stable-ABI wheel `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` that `pip install`s and runs on stock CPython 3.10/3.11/3.12/3.13 with no compiler / SWIG / Conan / system fixpp present.** The single `cp310-abi3` wheel (Py_LIMITED_API floor `0x030A0000`) replaces the earlier per-version `cp310-cp310 … cp313-cp313` plan (the abi3 single-wheel pivot — USER decision at Gate A r4). The `_fixpp.so` statically links the engine + `fixpp_capi` + `-static-libstdc++/-libgcc` and builds with `with_otel=False` + static OpenSSL, so `auditwheel show`'s external-library list is EMPTY (self-contained, FR-002). The four bundled FIX dictionaries (`FIX42/FIX44/FIX50SP2/FIXT11`) are resolved through `fixpp.dictionary_path(...)` / `fixpp.dictionary_bytes(...)` over the `_fixpp_data` package — never a repo-relative path — so the round-trip works with no repo present. The C-ABI surface is byte-frozen (the `0→1` GA freeze HELD; FR-012). **Status: shipped v1.0 deliverable; install-validated 3.10–3.13 against the built SWIG-4.4.1 wheel.** *(056 FR-001..009, SC-001..007; `bindings/python/{pyproject.toml,build-wheel.sh,cibw-before-all.sh,fixpp_dict_data.py,tests/wheel/}`; tier1.yml `python-wheel-*` jobs.)*

**B-056-2 — the typed `fixpp.SubInterpreterRejected` (1201) runtime guard is now WITNESSED on CPython 3.10/3.11 (no import barrier), where it is the SOLE guard, and the is-main check was corrected to `interp id == 0`.** On 3.12+ the single-phase import barrier refuses `import fixpp` in a sub-interpreter before the runtime check runs (L-055-1); on 3.10/3.11 there is no barrier, so the constructor-time check is the only line of defence. 056 T013 found that defence broken — the main-interpreter id captured at module `%init` is a process-global overwritten by the sub-interp's re-import, so `Engine()` was NOT rejected — and fixed it to `PyInterpreterState_GetID(PyInterpreterState_Get()) == 0` (rejects every non-main interpreter regardless of import order). **Status: FR-018 now witnessed end-to-end on 3.10 AND 3.11 (the no-barrier band), not merely inferred from the 3.12 barrier.** *(056 T013; `bindings/python/fixpp.i`; `bindings/python/tests/wheel/test_subinterpreter.py`; cf. L-055-1.)*

**L-056-1 — the Windows wheel is DEFERRED / best-effort for v1.0; only the Linux `manylinux_2_28_x86_64` wheel is mandatory.** A separable, on-demand lane (`.github/workflows/wheel-windows.yml`, `workflow_dispatch` / `windows-wheel` label, `continue-on-error`) scaffolds the port but is NEVER a Linux merge-gate dependency (FR-011 / WIN-1). Finishing it is not trivially cheap — it needs an MSVC-flavoured before-all (Conan `compiler=msvc` + static OpenSSL, no gcc-toolset), `delvewheel` (not auditwheel) for runtime-DLL repair, and SABI/`-DPy_LIMITED_API` link validation under MSVC. **Mitigation: build from source on Windows via the existing MSVC C-ABI build, or use WSL2 + the Linux wheel. Status: documented v1.0 scope boundary; lane present but best-effort.** *(056 FR-011, WIN-1; `.github/workflows/wheel-windows.yml`.)*

**L-056-2 — the per-version `cp3XX-cp3XX` wheel fallback (FR-010) was NOT triggered; only the single abi3 wheel ships.** The abi3 feasibility gate (T004: the SWIG wrapper compiles `-fsyntax-only -DPy_LIMITED_API=0x030A0000` against 3.10 headers with zero limited-API violations) and the cross-version runtime witness (T013: clean install + locator round-trip on 3.10/3.11/3.12/3.13; `abi3audit --strict` clean) both passed, so the FR-010 contingency that would emit per-version wheels stays closed. **Status: contingency documented as not-triggered (abi3 shipped); retained as the recovery path should a future interpreter expose an abi3 import flake.** *(056 FR-010, T004/T013/T015.)*

**L-056-3 — CPython 3.14+ is covered by the abi3 wheel's forward-compatibility but is UNTESTED in v1.0.** `requires-python = ">=3.10"` carries no upper cap and the `cp310-abi3` stable-ABI tag is forward-compatible by construction, so a 3.14 interpreter will install and import the same wheel; v1.0 CI only exercises 3.10–3.13 (the install-test matrix has no 3.14 leg yet). **Mitigation: add a 3.14 matrix leg when 3.14 reaches GA. Status: documented forward-compat-but-untested boundary.** *(056 PKG-3, FR-001; tier1.yml `python-wheel-test` matrix.)*

## 058-async-mutex-hardening (Cluster-4 async_mutex hardening, 2026-07-02)

Closes seven Phase-0-verified concurrency defects (AM-P1..AM-P3) plus the test-validity gaps
(T-1..T-7) in the coroutine `async_mutex` primitive (`include/fixpp/core/sync/async_mutex.hpp`),
embedded in `MemoryStore`, `FileStore`, `SeqnumManager`, and `Session write_gate_`. No public API
signature change. Spec: `specs/058-async-mutex-hardening/`; contract delta:
`specs/058-async-mutex-hardening/contracts/async_mutex-contract-delta.md`.

### Behaviors

**B-058-1 — the free-list pool pop/push is now ABA-safe (generation-tagged packed head) and the bump-allocator exhaustion counter is bounded (cannot wrap and reissue a live slot).** AM-P1: the tagless Treiber free-list head is replaced by a generation-tagged packed head (`{generation:54, slot_index:10}` in the existing 8-byte `waiter_pool_free_` atom — no new member, `sizeof(async_mutex)==131120` layout golden held) plus a per-slot persistent `free_link` atomic (never the destroyed record), closing both the ABA window and the plain-`next_` data race on slot reuse. AM-P2-3: the `waiter_pool_next_` bump allocator moved from an unconditional `fetch_add` (which incremented even on capacity-check-FAILING attempts, eventually wrapping the u32 counter and re-issuing an already-live slot) to a bounded CAS that checks capacity BEFORE incrementing and can never advance past `waiter_pool_capacity_` (512 slots). Both witnessed by deterministic forced-interleaving harnesses (`test_async_mutex_aba_interleave`, `test_pool_exhaustion_reuse`), mutation-tested. *(058 FR-001/FR-009, research.md D-1/D-4; `async_mutex.hpp`.)*

**B-058-2 — the destructor precondition now catches the in-flight-resumer teardown race, and the safe-destruction happens-before is documented and TSan-modeled.** AM-P2-1/AM-P2-2: the destructor's `std::terminate()` guard is widened to trip on `in_flight_resumers_ != 0` (not just `state_`/residual-waiter checks), converting a silent cancel-delivered-then-destroy write-after-free into a loud precondition failure. The barrier decrement (resume runner, last statement) is `release`; the drain-terminal and destructor reads are `acquire` — establishing a happens-before so a caller that destroys the mutex immediately after a completed `cancel_and_drain()` cannot race a cross-executor resumer's pool writes, for the parked-then-reaped case (see L-058-1 for the excluded case). Witnessed by death tests (`test_destructor_release_death`), a cross-executor MT teardown witness (`test_drain_destroy_inflight_mt`), and a dedicated TSan-modeled mutation-proof (`test_arm64_weak_memory` D-2 epochs — relaxing either ordering produces a genuine TSan data-race report 10/10 runs). *(058 FR-002/FR-003, research.md D-2/D-3; `async_mutex.hpp`.)*

**B-058-3 — chain-walk and null-awaiter impossible states now terminate loudly instead of silently corrupting or misbehaving.** AM-P3-1/AM-P3-2: `unlock()`'s residual-list and fresh-LIFO-walk `else` arms (structurally reachable only via a corrupted invariant — a `granted` record observed mid-walk) now `assert(false, ...)` + `std::terminate()` instead of silently stepping past the corruption; the resume runner's null-awaiter arm gets the same trap, with any defensive `result_` disarm required to neutralize via `guard.release()` (never a phantom unlock). AM-P3-3 (OOM-terminate on the post-grant resume `asio::post`) is settled as the primitive's final, documented fail-stop disposition — a deliberately different posture from the pre-grant slot-assign OOM path, which fails closed with `sync_lock_alloc_failed` (see contract-delta "OOM disposition on the resume path"). All three trap arms witnessed by real fault-injection `EXPECT_DEATH` tests (`test_am_p3_impossible_state_traps`), each independently mutation-tested. *(058 FR-004/FR-005/FR-007, research.md D-5/D-6/D-8; `async_mutex.hpp`.)*

### Limitations

**L-058-1 — a waiter GRANTED (not merely parked-and-reaped) on a different executor than the drainer forfeits the safe-destruction guarantee for that window; this cross-executor granted-holder-vs-drain overlap is an explicit EXCLUSION from the supported envelope.** B-058-2's happens-before closes AM-P2-1 for a waiter that was still PARKED at drain start and got reaped (reaped → cancelled → counted runner). It does NOT cover a cross-executor waiter that was instead GRANTED before/during the drain and becomes a cross-executor *holder*: that holder decrements `active_holders_count_` (relaxed) BEFORE its own `state_` CAS, so the drain can observe `active_holders_count_==0`, finalize, and the caller can destroy the mutex while the holder's pending CAS still targets freed memory — and the destructor guard cannot catch it (both counters read 0 at that point). This is the pre-existing `:189`/`:190`-documented UNDEFINED drain-overlap case (L-048-2), now precisely scoped by the AM-P2-1 fix rather than newly introduced. **Requirement: any cross-executor waiter that was granted MUST complete its `unlock()` before the drain begins (or unlock strand-locally).** Callers that keep `cancel_and_drain()` strand-local, co-located with all acquire/cancel/unlock of the mutex (the documented contract, L-048-2), never reach this exclusion. `async_mutex` stores no executor, so there is no cheap production assertion seam for this case (same Gate-A P2-4 constraint as L-048-2) — the contract stays documentation-enforced. **Status: documented unsupported topology (exclusion, not a defect).** *(058 research.md D-2/D-3; `specs/058-async-mutex-hardening/contracts/async_mutex-contract-delta.md` "Drain / teardown contract"; `async_mutex.hpp` destructor + `cancel_and_drain` doc-comments; cf. L-048-2.)*

**L-058-2 — two `unlock()` terminal-CAS-fail → recursive-unlock arms (F4/F6) and one `push_residual` CAS-retry arm (F7) carry lane-scoped or structural coverage waivers, not correctness gaps.** F4 (fast-path terminal-CAS-fail) and F6 (post-FIFO-walk terminal-CAS-fail) are reachable in-contract but only organically hit the seam-OFF coverage lane rarely (F4, ~0.3–1.5% of hammer opportunities) or never (F6, requires two mutually-anti-correlated narrow races to coincide); both are WITNESSED by dedicated forced-interleaving seam tests (`test_async_mutex_terminal_cas_recursive_unlock`, mutation-tested RED-on-neuter) — the production lcov line is waived per the T036 lane caveat, correctness is carried by the seam witness. F7 (`push_residual`'s weak-CAS back-edge) is structurally unhittable under the supported (non-drain-overlap) contract: `push_residual` runs only inside holder-serialized `unlock()`, so `next_drain_head_` has a single writer at any instant outside the L-058-1 drain-overlap exclusion; empirically call-count == loop-iteration-count with zero variance across every trial including the highest-volume hammer run. **Status: documented coverage-lane waivers with landed correctness witnesses (not unreachable-by-design, not untested).** *(058 T036/T040/T046, `.specify/decisions/058-async-mutex-hardening-coverage-design.md`; `## Coverage` disposition to be finalized at T036/`/speckit-verify`.)*

**L-058-3 — the three blind `phase_.store(...)` transitions in `async_lock`'s initiation body (direct-grant → `granted`, cancellation-slot-`assign`-throws → `cancelled`, and `draining_` → `cancelled`) are safe only under asio's contract that a waiter's `cancellation_signal::emit()` is serialized onto that waiter's associated executor; they are NOT CAS-guarded against a concurrent `on_cancel`.** Unlike `on_cancel()`, which transitions `queued → cancelled` via a `compare_exchange` (`async_mutex.hpp:1087`) so it can never clobber a concurrent grant, the three sites at `async_mutex.hpp:1336` (direct-grant), `:1286` (inherited-slot `assign` threw → fail-closed), and `:1297` (mutex already draining) perform a plain `store()` that blindly overwrites `phase_`. This is race-free only because all three execute **synchronously inside `async_lock`'s initiation body, before the coroutine suspends**, on the waiter's associated executor — and asio guarantees `cancellation_signal::emit()` runs on that same executor and only once the operation is genuinely in flight (suspended); therefore `on_cancel` cannot interleave these stores. A caller that wires a cancellation slot whose `emit()` is driven from a *different* executor than the one the mutex operation runs on (violating asio's cancellation-association contract) would break the assumption: `on_cancel`'s `queued → cancelled` CAS could then race a blind `store(granted)`/`store(cancelled)` and tear the phase state. This is pre-existing 048 code (the E-3 / research.md D-3 fail-closed arms and the direct-grant fast path); the AM-P2-1 release-ordering fix leaves these sites unchanged. Callers that use `use_awaitable` / `co_spawn` cancellation in the standard way (the slot is associated with the coroutine's own executor) always satisfy the contract. **Status: documented contract-reliance on asio's cancellation-executor serialization (not a defect); sibling to L-058-1's cross-executor exclusion.** *(058 Gate-B Codex/Fable MINOR; `async_mutex.hpp` on_cancel + async_lock initiation; cf. L-058-1, L-048-2.)*

## 060-int128-decimal-compare (Cluster-2 residual C1 — exact wide-integer cross-exponent decimal compare, 2026-07-04)

Reverses the 001/2a Gate-A "no `__int128`" decision: `decimal_traits<pod_decimal>::compare`'s
different-exponent slow path is replaced by a branch-free `k≥19` order-of-magnitude dominance guard
+ one `mul_u64_wide` 64×64→128 widening multiply (default `__int128` path; `#else` portable fallback;
MSVC `#elif` intrinsic path). Bit-identical `strong_ordering` vs the prior branchy comparison,
default-path swap, no runtime mode flag, no public/C-ABI/wire/error/layout change (`decimal.hpp`
byte-identical). Own spec + own Gate A, catalogued as **NFR-018** — NOT part of 001-core-decimal.
Spec: `specs/060-int128-decimal-compare/`.

### Limitations

**L-060-1 — the MSVC `#elif` branch of `mul_u64_wide` (`_umul128` x64 / `__umulh` ARM64) is Tier-2-only; no Linux CI lane compiles it.** The default Linux build (GCC/Clang) takes the native `__int128` branch; the portable `#else` limb path is covered locally via a forced build (`FIXPP_DECIMAL_FORCE_PORTABLE_MUL=ON`, T013) run against the differential oracle + witness matrix. Neither Linux configuration ever compiles the `#elif _MSC_VER` arm — analogous to the L-049-3 Windows-witness-deferred shape (same class: a per-PR-invisible platform-only branch). The `_umul128`/`__umulh` signatures + arch availability were confirmed against current Microsoft `<intrin.h>` documentation (MS Learn, msvc-170) at T012, and the `#elif` conditions were kept conservative by design — a wrong guess degrades to the portable `#else` (a **perf** bug, not a correctness bug). **MSVC x64 discharged locally (T014):** the differential oracle + witness matrix ran on `windows-msvc-debug` — 11/11 oracle cells, 4/4 mul-primitive cells, 16/16 `DecimalCompare.*` witnesses, bit-identical to the reference. **Status: MSVC x64 empirically discharged locally AND CI-confirmed on the merged head** — the full 3-lane `run-tier2` CI (debug/release/asan on `windows-msvc-*`) ran GREEN on PR #165 (`windows-msvc-debug` success; all three tiers success). *(060 T012/T013/T014; `src/core/decimal.cpp` `mul_u64_wide` [`:253`]; 049 L-049-3 precedent.)*

## 062-grouped-typed-read-fix (typed reads of repeating-group ENTRIES — mechanism + single-entry-per-occurrence nested, 2026-07-05)

062 removes the compile blocker where typed reads of repeating-group ENTRIES did not compile (`group_view::operator[]` span-ctor vs the generated `G_<n>` `MessageView`-ctor). It delivers the entry-read MECHANISM: span-scan scalar accessors on a generated entry flyweight (zero per-access heap alloc, no per-entry sub-index — FR-004a) plus a lazy dict-aware nested sub-view (one bounded arena build per stable outer occurrence, cached collision-free by outer-slice `data` identity — FR-004b). NO typed builders, NO writer, NO C-ABI / error-enum / wire-framing / top-level-read change (FR-007). Spec: `specs/062-grouped-typed-read-fix/`. The nested-read mechanism is unit-proven for **single-entry-per-occurrence** nesting; the two limitations below bound the MULTI-ENTRY nested case, deferred to prerequisite **063 "nested group-parse correctness"** (both defects are PRE-EXISTING and UPSTREAM of 062's RC1-frozen surface — 062 holds the whole-frame `OffsetTable::build()` guard and `group_slice.len` UNCHANGED). Full analysis: `research/G19-fix-fpml-iso20022/research/findings/dict-group-tag-collision-2026-07-05.md`.

### Limitations

**L-062-3 — the entry `field_value(tag)` escape hatch span-scans the WHOLE entry slice (including a nested group's bytes) and returns the FIRST occurrence.** A tag that lives ONLY inside a nested group is returned by an outer entry's `field_value(tag)` as if it were an outer-entry field. This mirrors the whole-message `field_value` first-occurrence semantics (N3) and is a property of the untyped escape hatch only — the codegen-scoped TYPED accessors (`emit_scalar` / nested `group<c,G_c>()`) are membership-scoped **as a set** and unaffected on shipped dictionaries. **Note (Fable audit 2026-07-08):** each typed accessor still resolves its tag via the same flat first-occurrence `wire::get` scan over the entry slice (incl. nested bytes), so typed-path correctness rests on parent/child scalar-member tag **disjointness** — mechanically verified for all 6 vendored group-bearing dicts, but NOT enforced for a user/dialect dictionary that shares a scalar tag between a parent group and its nested child (hardening tracked in issue #180). Prefer the typed accessors when nested/outer disambiguation matters. *(062 N3; spec `field_value` note; whole-message first-occurrence precedent.)*

**L-063-1 — FIX40/41/42 declare group-count fields with legacy XML type `INT` (not `NUMINGROUP`), so table_view-driven and codegen group registration is INERT for them (pre-existing on `main`; discovered by the 063 census, deferred to a follow-up).** `Dictionary::as_table_view()` (both the legacy bare-`no_tag` and the 063 context-scoped registration loops) and the codegen emitter (`tools/codegen/fixpp-codegen/emit_messages.cpp`) both gate group detection on `fr.type == field_data_type::NumInGroup`. FIX 4.0/4.1/4.2's XML types every `<group>` count field as `INT` (0 matches for `type='NUMINGROUP'` across all three), so `as_table_view()` registers **zero** groups for them and the generated `v42` flyweight has **zero** `groups::`/`struct G_` repeating-group accessors — table_view-driven group parsing/validation (`wire::Validator`, `OffsetTable::group()` via `group_member_fn`) and typed group reads are inert for these three dictionaries. The raw structural accessors `Dictionary::group()`/`group_fields()` are unaffected (e.g. `group_fields(382)` on FIX42 returns its 4 members). This is **orthogonal to Defect A/B** (not a wrong-variant or extent bug — no registration at all) and **pre-existing** (present before 063). 063's census/guards (FR-002/SC-002) therefore cover the six group-bearing dictionaries (FIX43/44/50/50SP1/50SP2 + FIXT.1.1); the FIX40/41/42 gap is carved out. **Fix (deferred to a follow-up feature — 064-class legacy-vocab precedent):** relax group detection to structural (`FieldRef.group_no_tag` / the `<group>` element, which the loader already tracks independent of field type), which would additionally materialize v42's group codegen surface (a large golden regen) and FIX40/41/42 group validation — feature-sized, out of 063's Defect-A/B scope. *(063 census `tests/dictionary/reused_tag_census_test.cpp`; decided 2026-07-07; spec.md FR-002 carve-out / SC-002.)*

**L-063-2 — [RESOLVED by 065-cabi-nested-group-membership, 2026-07-10 — issue #179 nested-read defect FIXED] the C-ABI `fixpp_group_get_nested_group` positional read includes a trailing outer-level member in the LAST nested instance's field lookups — a **reachable GA C-ABI silent-wrong-value defect on any group-bearing dictionary** (exposed, not merely edge-activated, by 063's outer-slice correction; correct fix = a membership-aware C-ABI follow-up, tracked as **issue #179**).** **RESOLUTION (065):** the hand-rolled positional scanner was deleted and the nested read now delegates to the membership-aware `OffsetTable::nested_group_slices` (062) + `consume_group_extent` (063), so the last nested instance is bounded by dictionary membership and a trailing outer member reads `TAG_NOT_FOUND` (not `OK`+wrong-value). No exported-symbol/header/enum/version change (C-ABI 1.5.0 freeze held). Un-skipped witness `MessageReadGroup.NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember` (mutation-proven RED on the pre-fix scanner) + dual FR-011 witnesses (direct `as_table_view()` + engine-loopback). Scope: **depth-1** (issue #179); depth-≥2 and the same-value membership-collision case remain bounded (see L-065-1, L-062-3/L-063-4/#180). Historical analysis retained below. The C-ABI nested-group read (`src/capi/message_read.cpp:462-609`) slices a nested repeating group by a **positional, dictionary-membership-free** delimiter scan and closes the last nested instance at the **end of the outer occurrence's slice** (`:587-590`). Before 063, Defect B truncated the outer slice before a nested group's 2nd entry, so this multi-instance branch was unreachable (a multi-entry nested C-ABI read returned `nc=0`). 063's nesting-aware `OffsetTable::group()` makes the outer slice correct (spans all nested entries) — a **strict improvement** for the common case — but if a well-formed message carries an outer-group member **after** a multi-entry nested group (e.g. a FIX44 ExecutionReport whose `NoLegs(555)` entry carries a multi-entry `NoLegSecurityAltID(604)` nested group followed by declared trailing leg members such as `LegQty(687)`), that trailing member's bytes fall inside the last nested instance's span, so `fixpp_group_get_field_*(nested, last_index, trailing_tag, …)` returns it (`FIXPP_ERR_OK`) instead of `FIXPP_ERR_TAG_NOT_FOUND`. **Reachability (Fable audit 2026-07-08): NOT an edge case** — the trailing-member-after-nested-group layout is ubiquitous in real dicts (scan: 222 such layouts in FIX44, ~20k in FIX50SP2), so any conforming counterparty can trigger the wrong-value `ERR_OK` via the public C-ABI; the prior "edge activated" framing understated it (issue #179). ~~**The C++ typed read path (`Dictionary::as_table_view()` → generated flyweights) is UNAFFECTED — it is membership-bounded and correct.**~~ **AMENDED by 066-dict-backed-inbound-parse (2026-07-09) — this claim was FALSE on the shipped path until 066 landed.** At the time this row was written (063, 2026-07-07), the claim was correct only for the unit-tier `Parser<access_mode::Index>{dict}` construction used by 062/063's own tests; it was never true for the SHIPPED dispatch path, because `Session::parse_and_dispatch_` (`session.cpp:316`) built its `Parser` with the DEFAULT (dictionary-free) constructor — a fact discovered by the Gate-A investigation for issue #179/065 (2026-07-09) that produced 066. So the C++ typed read delivered to a real application callback was, like the C-ABI path this row documents, membership-free/positional on every inbound-dispatched message: a group's last instance ran to end-of-message on the typed path too, for the identical reason as the C-ABI defect above. **066 fixes this for the typed path (and the C-ABI TOP-LEVEL group read + scalar-as-group contract) by dict-backing `parse_and_dispatch_` itself** — see B-066-1 / contract C1 (`specs/066-dict-backed-inbound-parse/contracts/inbound-parse.md`), proven directly on shipped dispatch by `tests/session/test_066_group_membership_red_test.cpp`. **This row's original C-ABI NESTED-group defect (the trailing member absorbed into the last nested instance via `fixpp_group_get_nested_group`'s positional scan) is UNCHANGED by 066** — 066 dict-backs the top-level parse only; the nested C-ABI cursor still does its own membership-free scan. That remains the tracked **#179 / 065** follow-up (a membership-aware C-ABI nested-read fix), which per SC-005 depends on 066 having landed first. A correct nested C-ABI fix needs nested-group membership, which this positional path deliberately lacks; plan.md Round-2 explicitly rejected plumbing dict/`group_context` through the GA-frozen C-ABI cursor (a gratuitous rewrite), so the fix is a **membership-aware C-ABI follow-up feature**, out of 063's (and 066's) scope. Pinned by the `GTEST_SKIP`'d witness `tests/capi/message_read_test.cpp::MessageReadGroup.NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember` (un-skipped when 065 lands, mirroring the `:353` lifecycle). No exported C symbol / freeze change (SC-005 holds). Cross-ref the FIX4x scope carve-out at L-066-1 (tied to L-063-1). *(063 T025 / Round-2 tasks-pin #5; decided 2026-07-07; `message_read.cpp:554-562` LCOV_EXCL rationale corrected; amended by 066 FR-010 2026-07-09.)*

**L-063-3 — strict `dictionary_driven_validator` group validation does not yet share the parser's nesting-aware, context-threaded repeating-group walk, and its per-context group DELIMITER for a reused NumInGroup tag is the dictionary's single (global, first-seen) `GroupRef.first_field_tag`.** Two residuals in the opt-in strict validator's Step-3 group check (`include/fixpp/wire/validator.hpp`), both confined to validation/reject behaviour — the C++ typed-read/parser path (`OffsetTable::group()` → generated flyweights) is UNAFFECTED. **(a) Flat walk:** `validate()` scans `msg.offsets().entries()` non-recursively and queries every group at ROOT context (`{msg_type, path=[]}`); a genuinely NESTED reused tag misses the context store and degrades to the legacy bare-`no_tag` (Defect-A-affected, `main`-parity) resolution, and the flat instance counter cannot span a multi-entry nested group (same shape as the pre-063 parser `seen_in_instance` truncation). **(b) Global delimiter:** `Dictionary::as_table_view()` sets the context store's `group_first` from `group_first_field(no_tag)` (= `GroupRef.first_field_tag`, the declaration first field), which is keyed by `no_tag` globally (one `GroupRef` per `no_tag`, first-seen for reused tags) because `message_fields()` is tag-sorted and so preserves no per-message declaration order to derive a context-exact delimiter from. So a reused NumInGroup tag whose delimiter genuinely differs across contexts gets the first-seen variant's delimiter in the non-first context (and, because the delimiter is also registered as a member, that one delimiter may be a spurious extra member of that context's set when it is not otherwise present — a lenient membership false-positive within this same residual). The per-context MEMBER SET is otherwise context-exact (the Defect-A fix). **This replaced a worse bug:** `group_first` was previously `members.front()` (the LOWEST-TAG member of the tag-sorted per-message set, not the delimiter), which made the validator falsely reject valid real-dictionary groups whose delimiter is not their lowest-tag member — e.g. FIX44 `NoPartyIDs(453)` (delimiter `PartyID(448)`, lowest member `PartyIDSource(447)`). That false-rejection is FIXED and pinned by the mutation-proven witness `tests/wire/validator_production_table_view_test.cpp::ValidatorProductionTableView.GroupDelimiterFromWireNotTagSortedMember`. **Fix (deferred follow-up):** give the validator the same context-threaded, nesting-aware descent as `OffsetTable::consume_group_extent` (resolving membership by real context and consuming nested extents recursively), which subsumes both residuals. *(discovered in 063 Gate-cleanup review 2026-07-07; delimiter-source fix landed in `dictionary.cpp` `as_table_view()`; nesting/ reused-delimiter residual deferred.)*

**L-063-4 — `OffsetTable::group_slices()`'s slice splitter (and its redundant flat cap loop) re-walk the group's extent FLAT, not nesting-aware — a defense-in-depth gap deferred as real-dictionary-unreachable.** `consume_group_extent()` (`src/wire/offset_table.cpp`) correctly computes the nesting-aware `group_end` for the outer group, but `group_slices()`'s instance splitter (`:596-599`) and the redundant post-extent cap loop (`:548-558`) then re-walk that extent with a **flat** "does `entries_[k].tag == delim` mark a new outer instance" test, with no notion of nesting depth. If a nested group's own delimiter tag ever equalled its enclosing group's delimiter tag, the nested group's repeated delimiter fields would be mistaken for new outer-instance boundaries and the outer slice would be split incorrectly (the extent walk would still be correct; only the splitter would err). **This configuration does not occur in any shipped FIX dictionary** (Fable audit 2026-07-08: 0 nested/parent delimiter collisions across all 6 group-bearing vendored dicts). NOTE the earlier rationale that "the wire itself would be ambiguous" is **overstated** — `consume_group_extent` decodes such a collision *correctly* via declared counts, so the wire is decidable; only the flat splitter errs. The "impossible" is therefore a **convention of the shipped XMLs**, not a structural guarantee (confirmed: every real-dict nested/parent delimiter pair censused by 063 is distinct; the real-dict guard `NestedGroupExtent.MultiEntryNestedExtentGuard` passes for exactly this reason, not by accident). Reproducing the gap requires a **hand-built, non-representative** membership forcing outer/nested delimiter collision (`tests/wire/nested_group_extent_test.cpp:511-519`'s documented synthetic scoping trick). **Unenforced for user/dialect dictionaries:** nothing in the loader, `as_table_view()`, or the public `table_view` mutators rejects a nested==parent delimiter collision, so a user-supplied dialect XML or hand-built `table_view` (both public APIs) CAN construct it and reach the splitter bug — hardening (pin + optional load-time guard) tracked in **issue #180**. **Status: genuine internal inconsistency (extent walk is nesting-aware, the splitter is not); unreachable via any SHIPPED dictionary but unenforced for user/dialect dicts — deferred as defense-in-depth, non-blocking.** **Fix (deferred follow-up):** make the splitter nesting-aware too (advance `k` via the same `consume_group_extent` recursion on a nested count before testing outer-delimiter boundaries) and fold the redundant flat cap loop into the same traversal. *(Gate B PR#176 r1, Codex finding #2, downgraded and waived at P3 by orchestrator triage (real-dict-unreachable); `src/wire/offset_table.cpp:548-558,596-599`.)*

## Membership-aware C-ABI nested repeating-group read (065-cabi-nested-group-membership / #179)

### Limitations

**L-065-1 — a pre-existing typed-path context-threading gap at nesting depth ≥ 2, and the consequent depth-2 C-ABI-vs-typed divergence, are NOT fixed by 065 (which is depth-1 scoped, issue #179).** The generated typed nested accessor threads the parent group's context **unpushed** into `nested_group_slices` on a nested descent (`tools/codegen/fixpp-codegen/emit_messages.cpp` nested-descent site; `group_view::operator[]` copies `base_ctx_` verbatim without a `.pushed(nested_tag)`), so a **depth-2** typed read of a member `X` from a `539`-entry queries membership under the too-short path `[453]` for a group registered under `[453,539]` — a miss that degrades to the legacy bare-`no_tag` store (Defect-A-prone). This is **pre-existing** (062/063), independent of 065, and reachable only for genuinely doubly-nested reads. 065 deliberately stores the **arithmetically-correct pushed path** on its C-ABI nested cursor (`nested->group_ctx = parent->group_ctx.pushed(nested_tag)`) rather than mirror the typed path's bug (project "don't enshrine bugs" discipline), so at depth-2 the C-ABI resolves membership under the correct `[453,539]` while the typed path resolves under `[453]` — a genuine **C-ABI-vs-typed divergence with the C-ABI being the more-correct side**. This divergence is **INERT at depth-1** (065's scope; verified: the nested cursor's `group_ctx` is read only on a *further* descent — depth-1 field reads go through `scan_slice_for_tag` and never touch it) and SC-005 equivalence is explicitly bounded to depth-1. **Fix (deferred follow-up):** push the typed context on nested descent in the emitter (`.pushed(nested_tag)`) + a forced golden regen + a validator counterpart — feature-sized, out of #179's depth-1 scope. When that lands it must push the typed context (not un-push the C-ABI's), reconciling the two paths onto the correct full path. *(065 research Decision 7; decided 2026-07-10; `emit_messages.cpp` nested-descent site, `group_view.hpp` `operator[]`.)*

## FIX 4.0/4.1 dictionary loader legacy-type support (064-fix4041-legacy-types / D-004)

### Behaviors

- **B-064-1 — The XML loader accepts the two pre-canonical legacy field-type names `TIME` and `DATE` (FIX 4.0/4.1), resolving them via the same collapse table as the post-canonical FIX-5.0 aliases; the `[FIX50SP2 §3.3]` `field_data_type` enum is unchanged.** `TIME → field_data_type::UtcTimestamp`, `DATE → field_data_type::LocalMktDate` (`src/dictionary/xml_loader.cpp` `kFieldTypeTable`, the collapse block after the `TAGNUM`/`LOCALMKTTIME`/`XID`/`XIDREF` rows). This completes `[const §I.1]`'s all-nine-versions runtime-XML commitment (FIX 4.0/4.1 were the last two un-loadable versions). The relaxation is **global, not version-scoped** — consistent with the existing global collapse rows, a `FIX44.xml` using `type="DATE"` would now load; no vendored FIX 4.2+ file actually uses these names, so no vendored file's resolved typing changes. Every other field type (`INT`, `LENGTH`, `DATE`-adjacent `MONTHYEAR`/`DAYOFMONTH`, …) was already in-vocabulary. *(FR-001/002/003/007; research R2/R3/R5; witnesses `xml_loader_test.cpp::Fix40/Fix41LoadsLegacyTypes`, `lookup_test.cpp` FIX40/FIX41 rows.)*

### Limitations

- **L-064-1 — `DATE` is typed `LocalMktDate` — a deliberate stronger-typing DIVERGENCE from QuickFIX, which resolves `DATE` to `TYPE::Unknown` (no validation).** QuickFIX's `DataDictionary::XMLTypeToType` (SHA `19ef6a4c`, `src/C++/DataDictionary.cpp:678`) has **no** `DATE` branch → falls through to `TYPE::Unknown`, i.e. QuickFIX performs no type validation on `DATE` fields. fixpp instead maps `DATE → LocalMktDate` because the two `DATE`-typed fields (`TradeDate`, `FutSettDate`) are typed `LOCALMKTDATE` in every FIX 4.2+ canonical dict, giving our typed reads real date semantics. **This divergence is metadata-only and carries zero interop-rejection risk:** `field_type_from_data_type` (`include/fixpp/dict/field_type.hpp:98-114`) collapses **both** `LocalMktDate` and `UtcTimestamp` (and `UtcDateOnly`, `DialectExtension`, `default`) to the same coarse `field_type::String` that the Phase-1 validator consumes, so no date/timestamp value-format check is keyed on the fine-grained enum — fixpp rejects no message value that QuickFIX's `TYPE::Unknown` path accepts. The divergence shows only in what `field_ref::type()` *reports*. The `TIME → UtcTimestamp` mapping AGREES with QuickFIX (`XMLTypeToType:675`) and needs no divergence row. **Status: intentional (recorded design choice), not a defect.** *(FR-009; SC-005; research R3/R4; QuickFIX `DataDictionary::XMLTypeToType`.)*

## 061-typed-app-messages (typed application-message write shape-oracle — 5 exemplar builders + `wire::body_builder`, 2026-07-08)

061-slim delivers a **write shape-oracle**: 5 hand-written exemplar builders (NewOrderSingle 35=D / ExecutionReport 35=8 / OrderCancelReject 35=9 / NewOrderList 35=E / AllocationReport 35=AS, all `fixpp::v44`) on a shared `wire::body_builder`, each anchored to a checked-in QuickFIX-authored body-only golden + a dict-aware round-trip witness. It is the prerequisite (shape-oracle + `body_builder`) for the follow-on FR-015a codegen writer-emitter, NOT its implementation. Spec: `specs/061-typed-app-messages/`.

### Behaviors

- **B-061-1 — `wire::body_builder` emits the application BODY ONLY (leading `35=<MsgType>\x01` then business fields; NEVER framing tags `8/9/34/49/52/56/10` — INV-2); the engine stamps the session header + checksum trailer at frame time.** Repeating groups are emitted count-precedence (`No<Group>=<N>` then N instances) with LIFO nesting; group grammar is fail-closed at `commit()` (INV-5) against an **author-supplied `delimiter_tag`** (no dictionary lookup — `body_builder` is a `wire→core` primitive, no `wire→dictionary` edge), rejecting an empty instance or a non-delimiter-first instance, mirroring the C-ABI `validate_group_grammar`. Decimals canonical via `decimal_t::format` (INV-3). *(FR-001/002/004/005; INV-2/3/5; `include/fixpp/wire/body_builder.hpp`; witnesses `tests/wire/test_body_builder.cpp`, `tests/session/test_exemplar_roundtrip.cpp`.)*
- **B-061-2 — `body_builder` accumulates its intermediate entry tree with ZERO GLOBAL HEAP: an internal fixed member buffer (`kArenaCap=16384 B`) + a `std::pmr::monotonic_buffer_resource` with a NULL upstream, so arena exhaustion throws → caught → fail-closed typed error (INV-4, `out` untouched); the serialized body stays capped at `kBodyCap=3800 B`.** This mirrors the C-ABI `OutboundAccumulator` arena model and preserves the 020 builders' self-imposed no-heap guarantee (`Builder_NoHeap_CountingResource`, `[const §VIII.5]` — which binds only the inbound parse→fromApp path, not outbound builders). Pinned by the global-`operator new` counter test `tests/wire/test_body_builder.cpp::BodyBuilder.NoGlobalHeap_CountingNew`. *(Decision 4, amended implement-time 2026-07-08 by user ruling — data-model §1 / research Decision 4; [[feedback_monotonic_arena_percall_pmr_vector_leaks]].)*
- **B-061-3 — the 5 exemplar builders emit business fields in QuickFIX / FIX44-dictionary order to byte-match their external goldens under `shape_oracle_profile()` (which excludes only framing `{8,9,10,34,52}`, matching every business field incl. `TransactTime(60)` verbatim).** For the two refactored builders (D/8), this CHANGES the field order from their legacy 020 non-ascending emission to ascending (SC-001 golden-match overrides byte-identity to 020; the 020 read tests are order-insensitive and stay green — value-preserving). Enum/char domains (e.g. `Side(54)`=`'1'/'2'`) are hand-validated per-exemplar across all builders that carry the field (D/8/E/AS); generic enum-range tables are out of scope (→ FR-015a). *(FR-002/003; SC-001; `src/session/business_messages.cpp`; tasks.md T013/T014 CORRECTION.)*
- **B-061-4 — `wire::MessageView` is MOVE-ONLY (copy ctor/assignment `= delete`d); constructing a `MessageView` binds its `OffsetTable` to a per-message arena and copying it would silently LEAK on nested reads.** Surfaced by the 061 read scaffold (originally `return *mv` = a copy): a `MessageView` copy runs `std::pmr`'s `select_on_container_copy_construction`, which returns a DEFAULT-constructed `polymorphic_allocator` (→ `new_delete_resource`), re-rooting `table_`'s allocator OFF the arena. Its lazily-built nested sub-`OffsetTable`s (placement-new'd, reclaimed wholesale with the arena, never individually destructed — `offset_table.cpp build_nested_subview`) then allocate from the global heap and are never freed — an ASan-LeakSanitizer leak on every nested typed read. **Move-CONSTRUCTION preserves the source arena allocator (pmr move-construction adopts it), so parse results (`expected_t<MessageView>`) still flow by move.** Move-**assignment** is also `= delete`d (gate-b/r1 RC#1): `std::pmr::polymorphic_allocator` does NOT propagate on container move-assignment, so `mv = std::move(parsed)` would keep the target's (possibly default-rooted) allocator and reopen the identical leak class via a different path — a compile-time-enforced `static_assert` regression pin (`tests/wire/parser_index_test.cpp`) confirms `MessageView` is move-constructible but not move-assignable. The C++ typed-read/parser/C-ABI production paths never copied or move-assigned a `MessageView` (compiler-verified caller census: only 6 pre-existing wire *tests* copied, now moved; zero sites move-assign). *(061 verify L1; ASan; `include/fixpp/wire/parser.hpp` MessageView special members; witnesses `tests/session/test_exemplar_{read,roundtrip}.cpp` under ASan.)*

### Limitations

- **L-061-1 — no v42 (market-data) grouped/nested write exemplar is expressible: v42 codegen emits ZERO typed repeating-group accessors (same root cause as L-063-1 — FIX40/41/42 type their group-count fields as legacy XML `INT`, not `NUMINGROUP`, so `Dictionary::as_table_view()` + the codegen emitter register zero groups).** All 5 exemplars are therefore forced to `fixpp::v44`. Expressing a grouped market-data write (e.g. a `NoQuoteSets`-bearing message in v42) is blocked until v42 group codegen exists, which is an **FR-015a prerequisite**, not 061 work. *(spec Out-of-Scope; Clarifications 2026-07-08; [[project_061_typed_app_messages]]; cross-ref L-063-1.)*
- **L-061-2 — the 5 exemplar builders are REPRESENTATIVE shape-oracles, not full-field: each sets all message-level required fields + the identifying first field of each required component + enough optionals to exercise every field TYPE and the full group/nesting SHAPE, NOT every optional field.** Full-field coverage, the remaining ~28 OFFICIAL A/M/P builders, and all-version coverage are the follow-on FR-015a / FR-015b features; the 5 catalogue rows (A-001/A-002/A-006/A-007/P-003) therefore carry 061 evidence but remain `backlog` for full coverage until FR-015a closes them. *(FR-002/FR-010; data-model §2 "Representative shape-oracle rule"; the 020 "minimal fields" precedent.)*

## 066-dict-backed-inbound-parse (dictionary-backed inbound receive parse, 2026-07-09)

066 threads the session's configured dictionary into the inbound receive parse (`Session::parse_and_dispatch_`, the single parse site shared by admin and app dispatch), which had been dictionary-FREE: every inbound-dispatched `MessageView` (C-ABI and C++ typed) carried no group membership, so a repeating group's last instance absorbed every subsequent body field on the real dispatch path — the root cause behind issue #179, broader than #179's C-ABI-nested framing (it also affected top-level groups and the C++ typed path, not only the nested C-ABI cursor). Clone (`fixpp_msg_clone`) and `reify` propagate the same membership (FR-007), so a clone/reify handle reads identically to its dict-backed source. Spec: `specs/066-dict-backed-inbound-parse/`.

### Behaviors

- **B-066-1 — inbound repeating-group reads are now membership-bounded on the SHIPPED dispatch path (both C-ABI and C++ typed), matching QuickFIX/J's strict in-group behavior; this is an INTENDED behavior change (permissive → strict), not merely a bug fix.** A counterparty field inside a group instance that is not a declared member of that group in its context now terminates the instance at that field (`OffsetTable::consume_group_extent` breaks on the first non-member) — both at the TRAILING end (a field after the group is no longer absorbed by the last instance) and, more subtly, INTERIOR to an instance (an undeclared tag between two declared members truncates the instance right there, so a declared member appearing AFTER the undeclared tag is now absent even though it was present on the wire). Before 066 this was permissive: an unknown in-group field was silently tolerated and the last instance's extent ran to end-of-message, returning `FIXPP_ERR_OK` + a wrong value instead of `FIXPP_ERR_TAG_NOT_FOUND`/absent. **Extension story**: the presently-shipped path for a superset counterparty (e.g. a FIX-Latest / Orchestra EP addition) is to keep the loaded dictionary CURRENT — Orchestra/EP additions declared with group membership are strict-bounding-inclusive by construction ([[project_orchestra_fix_latest_direction]]); the `dialect_overlay` config knob (D-009, `spec/feature-catalogue.md:126`) is the PLANNED membership-extension path but remains **`backlog`/unshipped** — it is NOT a currently-functional escape hatch for an undeclared in-group field. Top-level unknown-tag tolerance is unaffected (still indexed + `get(tag)`-readable) — only group extents became strict. Proven directly on the shipped path (not inferred from a `Parser<Index>{dict}` unit-tier test) by `tests/session/test_066_group_membership_red_test.cpp::GroupMembershipRed.TrailingFieldAbsentFromLastInstance` (trailing) and `::InteriorUndeclaredTagTruncatesInstance` (interior) — both real `Session::parse_and_dispatch_` witnesses, RED before 066 and GREEN after — plus the C-ABI mirror `tests/capi/dict066_group_membership_red_test.cpp::GroupMembershipCapiRed.TrailingFieldAbsentFromLastInstance`. *(FR-001/003/008; contract C1/C3 `specs/066-dict-backed-inbound-parse/contracts/inbound-parse.md`; Edge Cases; Clarifications 2026-07-09.)*

### Limitations

- **L-066-1 — FIX 4.0/4.1/4.2 sessions become strict-but-GROUP-BLIND under dict-backing (inherits L-063-1): their inbound group reads flip from present-but-positionally-wrong to `TYPE_MISMATCH`/absent, not to membership-correct.** These three dictionaries type their group-count fields with the legacy XML `INT` (not `NUMINGROUP`), so `Dictionary::as_table_view()` (`dictionary.cpp:335`'s NumInGroup gate) registers ZERO groups for them — the same root cause as L-063-1/L-061-1. Once the inbound parse is dict-backed (066), a FIX40/41/42 session's `fixpp_msg_get_group`/typed group query on what IS a real wire-level repeating group now returns `FIXPP_ERR_TYPE_MISMATCH`/absent rather than a membership-bounded read, because `as_table_view()` never registered it as a group at all. 066's membership-correctness claims (SC-001/FR-001/FR-003) are therefore SCOPED to the six group-registering dictionaries: FIX43 / FIX44 / FIX50 / FIX50SP1 / FIX50SP2 / FIXT.1.1. Structural INT-count group registration (the L-063-1-deferred fix) remains out of scope for 066. **Status: documented scope carve-out (tied to L-063-1), not a 066 defect.** *(FR-001/003/008; spec.md Edge Cases; contract C1 Scope note; cf. L-063-1, L-061-1.)*

**Release note (066):** Inbound repeating-group reads (C-ABI `fixpp_group_*`/`fixpp_msg_get_group` and the C++ typed flyweights) are now dictionary-membership-bounded on the SHIPPED receive path — a group instance containing an undeclared field (trailing or interior) is correctly truncated at that field instead of silently absorbing subsequent bytes, matching QuickFIX/J's strict in-group semantics. This is an **interop-visible, intended behavior change** (permissive → strict); keep the loaded dictionary current for any counterparty extension (see B-066-1's extension story — `dialect_overlay`/D-009 is planned, not yet shipped). Scoped to group-registering dictionaries (FIX43/44/50/50SP1/50SP2/FIXT.1.1); FIX 4.0/4.1/4.2 sessions become strict-but-group-blind (`TYPE_MISMATCH`) for group queries — a pre-existing scope carve-out (L-063-1/L-066-1).
