# Research — Credential redaction at the message-store boundary (034)

Phase 0 design decisions. All file:line references are into the `034` working tree (submodule HEAD).

## R1 — Where to mask: the single store-entry boundary

**Decision**: Apply masking inside `Session::store_then_emit(seqnum_t, std::span<const std::byte> frame)`
(`src/session/session.cpp:4399`), in **Step 1**, before `co_await store_->store(stamped_seq, masked, outbound)`,
and transmit the **original** `frame` in Step 2 unchanged.

**Rationale**: `store_then_emit` is the **only** site that calls `store_->store(...)` for outbound frames
(verified — Fable 5.2 §1b; it is reached by the initiator Logon emit `:835` and the acceptor reply Logon
emit `:2248`, and every other outbound frame). Masking here satisfies FR-009 (single boundary → every store
backend and every caller inherit it, no per-backend or per-call-site duplication) and keeps the wire path
(Step 2) untouched (FR-002), because Step 2 transmits the caller's original `frame`, not the masked copy.

**Alternatives considered**:
- *Mask at the two Logon emit call sites*: rejected — duplicates logic, violates FR-009, and each site
  would have to manage two buffers.
- *Mask inside each `MessageStore` impl (`FileStore::store`, `MemoryStore::store`)*: rejected — duplicates
  across backends, pushes a session-protocol concern (FIX tag semantics) into the storage layer, and would
  need re-doing for any future store. The store boundary in the **session** is the correct single seam.

## R2 — Same-length, zero-alloc masker (NOT `redact_tag554`)

**Decision**: Add a new sibling utility `mask_tag554_same_length_inplace(std::span<std::byte> frame)` in
`include/fixpp/session/logon_credentials.hpp`. It scans for a genuine SOH-delimited `554=` field (the same
boundary anchoring as `redact_tag554`: `\x01554=` mid-frame, or `554=` at offset 0) and overwrites each
value byte (from past `554=` up to the next `\x01` or end) **in place** with `'*'` — **preserving the byte
count** (no length change). Operates on a mutable span; returns nothing (or a bool "did-mask"). Zero heap.

**Rationale**: The existing `redact_tag554` (`logon_credentials.hpp:74`) **allocates a `std::string`** and
**collapses the value to the 3-byte literal `"***"`** — both disqualifying here: it violates FR-008
(zero-alloc — self-imposed persist-path discipline aligned with `[const §XV.1]` per-message heap
avoidance, not a §VIII.5 parse→fromApp mandate) and FR-003 (same length → `9=` BodyLength, store offsets, and store record
CRC stay valid). A separate same-length in-place masker is required; the two share the *field-detection rule*
(documented as a shared invariant) but not the rewrite mechanics.

**Alternatives considered**:
- *Reuse `redact_tag554` and re-frame (`recompute 9=`/`10=`)*: rejected — allocates, mutates length, and
  would need a full re-frame; far more complex than a same-length overwrite.
- *Generalize to a multi-tag redactor*: rejected (YAGNI) — only `554` is in scope (FR-005).

## R3 — Buffer sizing and the over-bound case

**Decision**: Copy the frame into a **coroutine-frame-resident `std::array<std::byte,
kMaxMaskableLogonBytes>`** only when masking is needed (R4), mask the copy, and pass
`std::span(buf.data(), frame.size())` to `store_`. The buffer lives across `co_await store_->store(...)`,
so it is part of the existing `store_then_emit` coroutine frame — it enlarges that frame by
≤`kMaxMaskableLogonBytes`, adding **zero new allocations** (SC-004 count basis unchanged; precedent: the
resend path already places `std::array<std::byte, kRpBufSize> rp_buf` in the same coroutine frame,
`session.cpp:4758`, with no PMR witness). `kMaxMaskableLogonBytes` is bound to the **`build_logon`
builder's actual maximum output capacity** (not a hand-picked round number), so it is exact rather than
generous. Add an **`open()`-time credential-length guard** (extending the 033 FQ-1 FIXT-config validation)
that the configured `username`+`password` lengths cannot produce a Logon exceeding that capacity — combined
with the bound being the builder's own ceiling, the over-bound branch becomes **provably dead defensive
code** for any frame that survives the MsgType=A gate. To keep that dead branch covered without a §IX.1
waiver, `kMaxMaskableLogonBytes` is a **test-only / internal compile-time bound** — overridable solely
through the existing `FIXPP_TEST_HOOKS` compile-gated seam (`[const §XV.9]`; precedent: the
`static constexpr kRpBufSize` in the same `store_then_emit` function), **NOT** a public `SessionConfig`
knob, public constructor parameter, or template parameter on `Session` (which is a non-template class) —
so a fault-injection cell can drive a small-bound case with **zero production ABI/config surface**
(Art. X preserved). See the contracts witness table. If, defensively, a
to-be-masked frame still exceeds the bound at runtime, **fail closed**: do not persist the cleartext —
skip the store write for that frame (logged-then-proceed, I-07, consistent with the existing admin
store-error model at `session.cpp:4417-4420`) rather than leak; the wire frame is still transmitted.

This skip-store-but-transmit fall-through is **wire-safe**: admin frames are folded into a
`SequenceReset-GapFill` on resend whether or not they were stored (`session.cpp:4744-4770`), so for a
`35=A` Logon a skipped store is **wire-identical** to a stored one — both emit GapFill on resend, never a
verbatim replay. (The earlier "the seqnum was already stamped" reasoning is wrong and dropped; wire-safety
rides on the admin→GapFill fold, not on the seqnum stamp.) The durable outbound **counter** is a separate
cell — advanced via `persist_outbound_advance_` → `store_->next_seqnum` (`session.cpp:691`), untouched by
skipping a frame **store** — so there is no durable counter hole either.

> **Implementation correction (2026-06-13):** the `FIXPP_TEST_LOGON_MASK_BOUND` compile-override
> seam described above could **not** be realized — `kMaxMaskableLogonBytes` is consumed inside
> `store_then_emit`, which is compiled into `libfixpp_session` *without* the test define, so a define on
> a test target cannot reach it. The dead `#if` arm was collapsed to a plain `static constexpr 256`, and
> the over-bound branch instead earns its BRDA via a `FIXPP_TEST_HOOKS` **frame-injection** accessor
> (`store_then_emit_test_access`) that feeds a hand-crafted >256-byte `35=A` frame to the real branch
> against the real bound. Same `FIXPP_TEST_HOOKS` gate, same zero-production-surface outcome,
> mutation-proven. See `plan.md ## Gate A` deviation #1.

**Rationale**: The store's own `max_frame_bytes` is 256 KiB (`file_store.hpp:109`) — far too large to copy
into the coroutine frame unconditionally. But only **Logon** frames carry `554`, and Logons are small;
bounding the *maskable copy* (not all frames) to the `build_logon` ceiling keeps it in the coroutine frame
with zero new alloc, and makes the over-bound branch dead-but-safe defensive code.

**Alternatives considered**:
- *Mutate the original buffer in place, store, then restore before transmit*: rejected — `frame` is a
  `const` span owned by the caller; const-cast + restore is fragile across the `co_await store` suspension
  and the noexcept/cancellation paths (a throw/cancel mid-store could leave the buffer masked → masked wire
  frame). A coroutine-frame copy is simpler and has no restore-ordering hazard.
- *Heap-allocate the masked copy*: rejected — violates FR-008.
- *Fatal-no-transmit on over-bound (transition to `Disconnected`)*: rejected — it would fracture the
  existing admin store-error model (`store_then_emit` absorbs non-abort store errors and still transmits,
  I-07 at `session.cpp:4417-4420`) for no wire-safety gain, since skip-store-but-transmit is already
  wire-identical to store-then-transmit for an admin Logon (admin→GapFill, above).

## R4 — Gate: only a genuine `554`-bearing Logon (`35=A`) is masked

**Decision**: Mask only when **both**: (a) the frame's `MsgType(35)` value is `"A"`, and (b) the frame
contains a genuine SOH-delimited `554=` field. Cheap pre-check order: first `memchr`/find for `\x01554=`
(common frames have none → O(n) reject, no copy); if found, confirm `35=A` via a small header scan
(mirrors the existing `interpret_logon` MsgType scan, `admin_messages.cpp:237-286/:344`).

**Rationale**: FR-006 scopes masking to outbound `35=A`. The `35=A` gate is the **load-bearing safety
boundary**, not belt-and-suspenders: it confines masking to the **never-replayed-verbatim** class. Admin
frames are folded into a `SequenceReset-GapFill` on resend (`session.cpp:4744-4770`), so a masked stored
Logon never reaches the wire — whereas **app frames ARE replayed verbatim** from stored bytes
(`build_replay_frame` → `transmit_async`, `session.cpp:4758-4763`). If masking ever applied to a non-admin
frame carrying a genuine `554`, a resend of that app frame would put the **mask on the wire** → peer-observable
desync. The 554-absent fast path (the overwhelming majority of outbound frames) does **no copy and no
allocation** — FR-007 / SC-003 byte-identical no-op falls straight out.

**Alternative considered and rejected — "mask any genuine 554 on any outbound frame"**: rejected as
**unsafe**, not "simpler defense-in-depth". Masking app frames (which are replayed verbatim) would mask
the on-the-wire copy on resend → peer-observable mismatch. The MsgType=A gate is precisely what excludes
the verbatim-replayed class; FR-006 stays Logon-only.

## R5 — Store CRC and the FIX `10=` checksum

**Decision**: The `FileStore` computes its per-record CRC32 over exactly the bytes passed to `store()`
(`file_store.cpp:503-543`). Since we pass the **masked** span, the record CRC is over the masked frame and
is self-consistent — `retrieve()` validates and returns the masked frame normally (FR-003 / SC-005). The
embedded FIX `10=` checksum of the stored frame is **not** recomputed and therefore no longer matches the
masked body; this is **accepted and documented**: stored frames are never re-validated as FIX and never
replayed (admin → `SequenceReset-GapFill`, `session.cpp:4744-4770`), so a stale `10=` in the at-rest copy is
inert. `9=` BodyLength
**does** stay valid (same-length mask), so the record remains structurally well-formed.

**Rationale**: Recomputing `10=` would (a) require extra work and (b) gain nothing — nobody checksums the
stored frame. Keeping the mask strictly same-length-value preserves every property the store actually relies
on (length, offsets, record CRC).

## R6 — `retrieve()` returns the masked frame

**Decision**: Because only the masked copy is ever written, `retrieve()`/the resend store-walk return the
**masked** bytes. The P1 witness asserts redaction by **reading the store file bytes from disk directly**
(not via `retrieve()`), per the spec's "assert the named post-condition, not a proxy" requirement and
[[feedback_witness_asserts_named_postcondition_not_proxy]].

**Rationale**: An operator (or any in-process caller) using `retrieve()` should also never get the cleartext;
masking at store-time gives that for free. The disk-byte witness is the strongest, most direct assertion.

## R7 — Wire/store divergence is unobservable (safety proof)

**Decision / fact relied upon**: Stored Logon bytes are **never retransmitted verbatim** — the resend
store-walk classifies admin messages (incl. `35=A`) via `is_admin_type` and folds them into a
`SequenceReset-GapFill`; `build_replay_frame` is reached only for app frames (Fable 5.2 §1b;
`session.cpp:4735-4771`). Therefore the masked stored copy can never reach the wire, and there is no
peer-observable wire/store mismatch. **Forward constraint** (recorded as a new limitation, FR-010): if a
future feature ever introduces verbatim admin-frame replay, it MUST re-derive credentials from configuration,
not from the (now-masked) store.

## R8 — Documentation reconciliation (FR-010)

**Decision**: On merge, flip B&L **L-033-6** from a limitation to a mitigation ("password is masked at the
store boundary; …"), add the forward-constraint limitation from R7, and append a **dated correction note**
to `specs/033-fixt-fix50sp2-session/tasks.md` T024/T020 acknowledging the "no production frame persistence
exists" claim was incomplete (it overlooked the 008 message store) — **without** rewriting the merged text.
Add the 034 traceability row to `feature-catalogue.md` + `coverage-index.md`.

**Rationale**: Closes the stale-claim that anchored the original sweep past this store, per the
release-gate doc-hygiene discipline, without falsifying shipped history.
