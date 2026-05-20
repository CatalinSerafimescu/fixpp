# Design Doc 2e — `MessageStore` Async API + QuickFIX-Compat Shim Feasibility

> **Status:** Draft v0.5 — post-sign-off targeted gap-closure pass (2026-05-20) added Appendix D §D.4 / §D.5 / §D.6 + cross-references; spine of v0.4 (4-pure-virtual `MessageStore`, awaitable visitor, single-log-per-session on-disk shape, atomic-rename `reset()`, exclusive `async_mutex`, `commit_per_message` default, the §3.1 inherited-primitives table, the §6.3.5 platform-portability table, the 21-seam list with seam 2 extended, the 10-variant §6.7 errors table with operator-doc tightening, the Appendix D §D.1 / §D.2 / §D.3 drop-ins) survives unchanged.
> **Date:** 2026-05-20 (v0.5; v0.4 dated 2026-05-08)
> **Owner:** Opus drafts; user approves.
> **Headers:** `fixpp::session::MessageStore` (`include/fixpp/session/message_store.hpp`); default impls `fixpp::session::MemoryStore` (`include/fixpp/session/memory_store.hpp`), `fixpp::session::FileStore` (`include/fixpp/session/file_store.hpp`); `fixpp::session::MessageStoreFactory` (`include/fixpp/session/message_store_factory.hpp`); `fixpp::session::retrieve_visitor` (`include/fixpp/session/retrieve_visitor.hpp`); `fixpp::session::seqnum_t` consumed via `<fixpp/session/seqnum.hpp>` (placeholder; ownership re-pointed to the Phase-4 session-module spec per §3.1 / §10 Q9 — see N1 below); `<fixpp/session/quickfix_compat/cfg_loader.hpp>` (config-translation only — **no runtime adapter**, no `<…/quickfix_compat/message_store_adapter.hpp>`, **no `<…/quickfix_compat/sync_message_store_adapter.hpp>`** — round 2 retired the templated sync-store adapter per Codex C-R2-P2-1 escalation, see §4.8.B).
> **Inherits:** `[arch §4.4]` (`session` module surface — `MessageStore` lives here), `[arch §5.1]` (executor model — session-serialisation-domain-bound async ops, per `[2d §4.8]` round-3 wrapper-class shape), `[arch §5.3]` (error model — `expected_t<T>` hot path, no exceptions parse→`fromApp`), `[arch §5.4]` (storage / lifetime classes), `[arch §5.5]` (lifetime contract), `[arch §5.6]` (mid-session reconfiguration ban — store factory frozen at session open; **store ownership is `unique_ptr` per N1**), `[arch §6]` (plugin pattern — ≤5 pure-virtual cap), `[arch §10]` row 2e (handoff), `[arch §11]` Q3 (this doc OWNS the QuickFIX-compat-shim answer for v1.0), `[2a §10] Q3` (raw-frame storage confirmation — answered here), `[2b §4.2]` (frame-byte producer; `frame_view::bytes()`), `[2b §4.5]` (`Writer::commit` finalises BodyLength + CheckSum — load-bearing for outbound store ordering, round-1 root cause #1), `[2b §6.4]` (lifetime contract — flyweights MUST NOT bleed into the store API), `[2b §6.6]` (allocation, exceptions, threading; three-arena pinning; view-escape rule), `[2b §7.4]` (MessageStore consumes raw frames — *not* typed payloads), `[2c §1.1]` / `[2c §7.2]` (typed messages are not the persistence shape; store keeps no `Dictionary&`), `[2d §4.4]` (`EngineConfig::clock`; `EngineConfig::executor`), `[2d §4.5]` (`SessionConfig::store_factory` field — typed `std::shared_ptr<MessageStoreFactory>` in 2d v0.4; **N1 flags this as a sibling-doc inconsistency**: factory ownership should be `unique_ptr` per `[arch §5.6]`. Cross-doc amendment owed — declared in Appendix D §D.1 and applied at 2e sign-off per §10 Q11), `[2d §4.6]` (`fixpp::current_trace_context` — session-domain awaitable + `session_local<T>` storage; not consumed by v1.0 default impls but available to audit-pipeline impls), `[2d §4.7]` (cancellation propagation API — two-phase close; per-mode effect table at 2d §4.7 includes `MessageStore::write` row at line 808 — round-2 RC#2 fixed: 2e's engine-internal `FileStore::flush_for_session_close()` hook is owed to `[2d §4.7]` as Appendix D §D.2 drop-in, applied at 2e sign-off), `[2d §4.8]` (`session_executor` — project-owned wrapper class holding either `asio::strand` (`per_session_strand`) or attested `any_io_executor` (`direct_executor`); store callbacks rebind to this wrapper per `[2d §7.3]`), `[2d §6.5]` (`cancellable_dispatch` — cancellation primitive that returns `awaitable<expected_t<void>>` with `dispatch_aborted` — the model 2e adopts for cancellation surfacing), `[2d §6.7]` (`dispatch_aborted` and `clock_sleeps_cancelled` cancellation variants in the `FIXPP_ERR_CANCELLED` group), `[2d §7.3]` (MessageStore strand-binding handoff), `[2d §7.4]` (executor-compat surface for `async_mutex`), `[2d §7.9]` (`effective_clock` for any persisted timestamps).
> **Cites:** `[FIX-SL §4.1]` (Sequence numbers — wire `MsgSeqNum(34)` semantics; **does not bound the type**), `[FIX-SL §4.4]` (Sequence reset — S-017 path; reset is operator-driven, not autonomous), `[FIX-SL §4.4.2]` (Using ResetSeqNumFlag(141) for 24-hour connectivity — `ResetSeqNumFlag(141)=Y` Logon recovery path; round-3 post-cap re-anchor per Codex C-R3-P2-1), `[FIX-SL §4.4.3]` (Using ResetSeqNumFlag(141) during connection establishment — `ResetSeqNumFlag(141)=Y` Logon recovery path; round-3 post-cap re-anchor per Codex C-R3-P2-1), `[FIX-SL §4.5.4]` (Rejecting invalid messages — `reset()` truncation semantics tie back here), `[FIX-SL §4.8]` (Message recovery — the wire-side oracle for what the store must persist and replay), `[FIX-SL §4.8.3]` (Responding to ResendRequest — the consumer of `retrieve(begin,end)`), `[FIX-SL §4.8.5]` (Gap-fill process), `[FIX-SL §4.8.6]` (Sequence reset (hard reset, GapFillFlag=N) — `SequenceReset-Reset` (35=4, 123=N) wire-message semantics; round-3 post-cap retitle to coverage-index row per Codex C-R3-P2-1; v0.3's "ResetSeqNumFlag(141)=Y Logon" labelling was a section-mismapping), `[FIX-SL §4.8.8]` (Processing gaps for session layer messages — admin-msg gap-fill rule), `[const §I.1]` (v1.0 version surface), `[const §II.3]` (Tier 2 platform support — Windows/MSVC), `[const §VI.5]` (exact-coverage citations), `[const §VII]` (testing — ≥10 seams), `[const §VIII.5]` (zero global-heap new/delete on the hot path; arena allocations expected — **the correct citation; v0.1 mis-cited `[const §VIII.4]` per N13**), `[const §X.4]` (forwards-compat reserved range), `[const §X.5]` (C-ABI handle invalidation rules), `[const §XI.1]` (coroutines), `[const §XI.2]` (ASIO native cancellation), `[const §XI.3]` (awaitable mutex required in coroutine context), `[const §XI.4]` (callbacks dispatch on session strand), `[const §XI.5]` (store-write path always uses mutex regardless of policy), `[const §XIV.2]` (≤5 pure-virtual cap on plugin interfaces), `[const §XV.1]` (no heap-alloc per message on hot path), `[const §XV.4]` (no synchronous disk-I/O on every send — banned QuickFIX `FileStore` pattern), `[const §XV.9]` (`std::mutex` in coroutine context — banned, **no transitional carve-out** per root cause #3), `[const §XV.15]` (no `drop-oldest` on app/session message path; store path is on the session message path), `[const §XVII.1]` (Codex Gate A required for design docs), `[const §XVIII.5]` (no early shipping of post-v1 protocols — replicated MessageStore stays out), `[SYN §3.2 Q6b]` (`async_shared_mutex` is **out** of v1.0 — root cause #3 dropped 2e's RW-mutex optimisation), `[SYN §3.2 Q7]` (DECIDED async-API shape — `asio::awaitable<...>` writes; QuickFIX-compat shim if feasible, otherwise documented incompatibility), `[SYN §3.2 Q8]` (DECIDED store-write path always uses mutex regardless of policy). Sibling docs at point of use: `[2a §4.2]` (`trap_throw` pattern — the pattern 2e's PMR-throw-to-`expected_t` follows; **the correct citation per N13**), `[2a §6.7]`, `[2a §7.1]` (raw-frame replay decision; this doc confirms), `[2b §4.2]` (`frame_view::bytes()`), `[2b §4.5]` (`Writer::commit` finalises BodyLength + CheckSum), `[2b §6.4]`, `[2b §6.6]`, `[2b §6.7]`, `[2b §7.4]` (MessageStore raw-frame contract), `[2c §6.7]`, `[2c §7.2]` (no `Dictionary&` held by store), `[2d §4.5]`, `[2d §4.7]`, `[2d §4.8]`, `[2d §6.5]`, `[2d §6.7]`, `[2d §7.3]`.
> **Catalogue rows owned (in part):** **S-011** (Message store interface — `[FIX-SL §4.8]`); **S-012** (In-memory message store implementation — `[FIX-SL §4.8]`); **S-013** (File-based message store implementation — `[FIX-SL §4.8]`); **S-014** (Session recovery — resend flow, GapFill (123=Y) for admin messages — `[FIX-SL §4.8]`) — **store-side contract only**: the `retrieve(begin,end)` query shape and the raw-frame discipline that lets the FSM emit GapFill for admin frames; the FSM itself (`ResendRequest` issue, `SequenceReset-GapFill` emit) is owned by the **Phase-4 session-module spec**. **OSS-002** (QuickFIX `MessageStore` interface — the *interface shape* this doc inherits and re-skins as async; v1.0 ships Path B-only — documented incompatibility + migration recipe per §4.8.A; v0.2's "Path A subset wrapper" was retired in round 2 per Codex C-R2-P2-1 escalation, see §4.8.B). **COM-009** (Replicable MessageStore — **post-v1.0**, P3; this doc must show the async surface does not foreclose a future replicated impl, but does NOT design it; tracked in §10 as a forward-compat invariant only).
> **Convergence log:** see end-of-doc Appendix C. v0.1 → v0.2 (round 1) addressed Codex review (7 P1 / 4 P2 / 3 P3) and Opus adversarial review (post-judging 11 P1 / 7 P2 / 5 P3, 4 root causes — outbound-store-call-sequence, recovery-visitor-async, supporting-primitive-ownership, FileStore-disk-algorithm). v0.2 → v0.3 (round 2) addresses Codex round 2 (5 P1 / 3 P2 / 1 P3) and Opus adversarial round 2 (post-judging 5 P1 / 5 P2 / 3 P3, 2 root causes — on-disk atomicity extends to §6.3.1 file-pair shape; sibling-doc cross-edits applied/declared). 1 Codex P1 disagreed by Opus (round-2 finding C-R2-P1-1 on outbound cancellation taxonomy — the durable-before-transmit shape was endorsed by Opus round 1 and protects `[const §XV.15]`). v0.3 → v0.4 (round 3, post-cap line-edit pass per 2c v1.3 / 2d v0.4 precedent) addresses Codex round 3 (3 P1 / 1 P2 / 1 P3) and Opus adversarial round 3 (post-judging 3 P1 / 2 P2 / 2 P3, 1 root cause — collateral surfaces of the §6.3 single-log-per-session rewrite were not swept in §4.3 / §1.1 / §6.2.1 / §6.3.5). Round cap hit at round 3; user authorized post-cap line-edit pass to produce v0.4 — text-pinning + 5-site `[FIX-SL §4.8.6]` citation re-anchor + engine→FileStore concept-mechanism specification + 2 editorial nits. **v0.4 → v0.5 (2026-05-20, post-sign-off targeted gap-closure pass — NOT a Gate A round; same shape as `[2e §D.3]` self-amendment by `008-message-store` Phase-4 Gate A)** addresses 3 gaps surfaced by adversarial pressure-test of `008-message-store`'s pipeline-step-9 checklist audit waivers (Codex targeted review: CONFIRM P1 / CONFIRM P1 / CONFIRM P2 at `research/reviews/codex_2e_targeted_msgstore_review.md`; Opus adversarial post-judging tally P1 = 2, P2 = 3, P3 = 4, 2 root causes — "scope & trust" primitives in §4.3 / §6.3 (Gap 1 + Gap 2 + N-1 + N-2); store-object-allocation contract silence in §4.4 / §8 (Gap 3 + N-4) — at `research/reviews/opus_2e_targeted_msgstore_adversarial_review.md`). Closes by adding Appendix D §D.4 (CompID filesystem-safety at `FileStoreFactory::make()`), §D.5 (advisory-lock filesystem-type scope-pin), §D.6 (`std::default_delete<MessageStore>` deleter pin + post-v1.0 custom-deleter reservation) plus the 5 propagation actions (N-1 FR-013/I-16/contract docstrings; N-2 §6.7 operator-doc; N-3 seam 2 CompID-validation sub-cases; N-4 forward-compat reservation in §D.6; N-5 `noexcept` impl note in §D.4). No Gate A reset; no spine changes. See Appendix C.

---

## 1. Goals

1. Pin the **public C++ surface** of `fixpp::session::MessageStore`: a 4-method (down from 5; see §4.1.1, N2) pure-virtual interface, every method `awaitable<expected_t<...>>` per `[SYN §3.2 Q7]`, accepting raw FIX frames as opaque byte ranges (not typed payloads). The **virtual-vs-concept** decision is argued explicitly in §4.1 (N3): virtual stays for 2i C-ABI compatibility; ~15 ns of vtable dispatch is named in the §6.6 budget.
2. Confirm `[2a §10] Q3` — **MessageStore stores raw FIX frames**, byte-identical to what `wire::Writer::commit` produced (outbound) or `wire::Framer::feed` accepted (inbound). The store is decimal-trait-agnostic, dictionary-agnostic, and version-agnostic.
3. Specify two default impls — **`MemoryStore`** (in-memory; default for tests, embedded use) and **`FileStore`** (local-fs; default for production deployments) — both PMR-aware and constitutional-compliant on the hot path.
4. Lock the **factory shape** (`MessageStoreFactory`): `make()` returns `expected_t<std::unique_ptr<MessageStore>>` per `[arch §5.6]` (N1). The minted store is owned by the Session and frozen at session open.
5. Lock the **store-write mutex contract** per `[SYN §3.2 Q8]` and `[const §XI.5]`: every mutating method serialises against the per-store-instance writer mutex regardless of `SessionConfig::lock_policy`. The mutex is `fixpp::sync::async_mutex` (per `[const §XI.3]`); the dependency on 2f's contract is named in §3.1 + §7.4. **No RW-mutex / `async_shared_mutex` and no `std::recursive_mutex` transitional adapter** — per `[SYN §3.2 Q6b]` and `[const §XV.9]` (no carve-out).
6. Lock the **outbound store-call sequence** (root cause #1): outbound dispatch is `toApp → wire::Writer::commit() → store(seq, committed_span, outbound) → transport.async_write`. The store sees the byte-identical post-commit frame (BodyLength + CheckSum finalised per `[2b §4.5]`); cancellation between `commit` and `store` is benign (no persistence, no transmission); cancellation between `store` and `async_write` is the documented ResendRequest case. Inbound dispatch is `Framer::feed → Parser → store(seq, frame, inbound) → fromApp/fromAdmin`: store happens **after** parse succeeds and **before** the application callback so a callback crash does not lose the persisted frame. Documented in §6.1, §7.6.
7. Resolve `[arch §11] Q3` — **QuickFIX-compat shim feasibility**. **Verdict: Path B only** (§4.8.A) — documented incompatibility + migration recipe + optional config-file translator. v0.2's §4.8.B "curated Path A subset wrapper" was retired in v0.3 (Codex C-R2-P2-1 escalated to P1: user-attested traits cannot deliver compile-time safety). A thin `cfg_loader` (config-file translation only — no runtime adapter) ships at `<fixpp/session/quickfix_compat/cfg_loader.hpp>`.
8. Stay **zero global-heap allocation on the `store()` hot path** per `[const §VIII.5]` (the hot path is "between parse and `fromApp`" on inbound and "between `Writer::commit` and `transport.async_write`" on outbound — both rewritten in goal 6). PMR-arena allocations from `session_arena` are expected; **`MemoryStore::store` performs zero allocator calls after construction** (fixed slot + slab layout; see §4.2 / N9). The `retrieve()` recovery path is allowed to allocate (visitor-side caller-PMR).
9. Stay exception-free on the hot path per `[arch §5.3]`. Every `store()` / `retrieve()` / `next_seqnum()` / `reset()` returns `expected_t<...>`; PMR-allocation throw paths route through `fixpp::core::detail::trap_throw` per `[2a §4.2]` (the no-terminate-on-PMR-throw mechanism) and surface as documented `error::store_*` variants. The no-terminate behaviour is rooted in `[arch §5.3]`'s `expected_t<T>` error model + the `[2a §4.2]` `trap_throw` operationalisation; allocator policy on the hot path (zero global `new`/`delete`) follows `[const §VIII.5]`. (Round-2 N2-P2-1 refinement on round-1 N13: `[const §VIII.4]` was the wrong citation — corrected in round 1; v0.3 further tightens the no-terminate citation to `[arch §5.3]` + `[2a §4.2]` rather than overloading `[const §VIII.5]`.)
10. Lock the **cancellation result contract** per method (root cause #1, escalated C-P2-8 → P1): cancellation that wins **before** the store's linearisation point completes the awaitable as `expected_t::unexpected{store_cancelled}` (joining `[2d §6.5]`'s `dispatch_aborted` discipline); cancellation that loses to the linearisation point completes normally with the operation's value. Naming is per-method in §6.1 cancellation subsection.

### 1.1 Scope boundary — what 2e owns vs what it doesn't

2e owns:

- The `MessageStore` 4-pure-virtual interface, its `MessageStoreFactory`, its `direction_t` / `seqnum_t` (consumed-here, owner = Phase-4 session spine per §3.1) / `retrieve_visitor` supporting types.
- The `MemoryStore` and `FileStore` default impls.
- The `FileStore` durability/fsync policy knobs (`FileStorePolicy`) AND the on-disk algorithm (round-1 root cause #4 + round-2 root cause #1: a single-log-per-session append-only scheme with per-record direction tags + per-record CRC32 + atomic-rename `reset()` whose durability primitive is **mandatory** Linux parent-directory `fsync` after the rename + **mandatory** Windows `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` — round-3 post-cap pin per Codex C-R3-P1-2; see §6.3 / §6.3.5).
- The store-write mutex contract (exclusive `async_mutex` only).
- The outbound and inbound store-call sequence (root cause #1).
- The default-Path-B verdict (the round-2 close on N4 Path A: the curated subset wrapper is RETIRED — see §4.8.B / Appendix C round 2).
- The cancellation result contract per method (root cause #1 / C-P2-8 escalation).

2e does **not** own:

- The **session FSM** that *consumes* `MessageStore` — `ResendRequest` issuance, `SequenceReset-GapFill` emission, the `[FIX-SL §4.8.5]` gap-fill state machine, the per-counterparty resend buffer policy. These belong to the **Phase-4 session-module spec**. 2e defines the seam (the awaitable-visitor `retrieve` + the four other methods); the session-module spec walks the seam.
- The concrete declaration of `seqnum_t` (owner = Phase-4 session-module spec per §3.1; until that spec lands, 2e consumes the placeholder alias `using seqnum_t = std::uint32_t` — **with** an explicit cross-doc handoff in §10 Q9).
- The **C ABI** for `MessageStore`. C ABI is owned by **2i**; this doc only fixes the C++ surface (§5).
- **Replicated / cross-process / cloud** message stores. Per `[const §XVIII.5]`, COM-009 is out of v1.0 scope.
- **Typed-payload persistence sinks** (SBE/Avro snapshots, control-plane decoded records). Owned by `2j` (control plane) per `[2b §7.4]`.
- **Log/metric/tap** sinks; those are 2k / 2l.

### 1.2 Magnitude domain — what `MemoryStore` / `FileStore` are sized for

These caps bound v1.0 default-impl resource budgets; they are not constitutional invariants and are not parser-DoS bounds (those live in `[2b §1.2]`).

- **Per-session frame count.** A FIX session typically retains ≤ 10⁵ outbound frames and ≤ 10⁵ inbound frames over its lifetime before counter reset. **`MemoryStore` is positioned for tests + embedded use only** (round-2 Codex C-R2-P1-3 / Opus N9 close): default ring is **`bounded(default = 10_000)` entries per direction** (round-2 fix: v0.2's 200_000 default × 256 KiB max_frame_bytes worst-case sat at ~97.7 GiB, vastly above the 1 GiB DoS cap and tripping the engine's own construction guard). Production deployments **MUST** use `FileStore` (no count limit beyond fs free space) or a custom impl; if a production deployment must use `MemoryStore`, the user explicitly configures `inbound_capacity` / `outbound_capacity` and accepts the storage-DoS arithmetic in §4.2. The capacity policy enum is **`bounded(N)`** (the default; rejects with `store_capacity_exhausted` when full per `[const §XV.15]`) or **`unbounded`** (opt-in for embedded test rigs that bound memory by the lifetime of a single test run); `evict_oldest` is **NOT** offered — that policy violates `[const §XV.15]` and is banned at the doc level. See §4.2.
- **Per-frame size.** Bounded by `Framer::Config::max_frame_bytes` (default 256 KiB per `[2b §4.2]`). `MemoryStore` reserves up to `entries × max_frame_bytes` in its slab — at the round-2 default of 10_000 entries per direction × 256 KiB, worst-case ≈ 5 GiB across both directions; **the engine rejects construction unless the operator explicitly raised `EngineConfig::max_store_memory_per_session` above that worst-case** (round-2 close — defaults that fail their own DoS cap are not shippable). The "most production frames are < 1 KiB" observed-typical is unchanged; the worst-case bound is what the `[const §XV.15]` ban requires.
- **Storage-DoS bound (round-1 N9 + round-2 C-R2-P1-3 close).** Per-session memory ceiling = `(inbound_capacity + outbound_capacity) * Framer::Config::max_frame_bytes`. The engine refuses to construct a `MemoryStore::Config` whose product exceeds `EngineConfig::max_store_memory_per_session` (default: 1 GiB; see §4.2). With the round-2 default capacity at 10_000 per direction × 256 KiB, the worst-case is ≈ 5 GiB — explicitly above the default 1 GiB cap, by design: the user MUST opt into a higher cap (proving they sized for the worst-case) OR reduce `max_frame_bytes` (reducing the slab footprint linearly). A control-plane caller (or misbehaving Phase-4 FSM) issuing `retrieve(1, 0, …)` walks no more than this ceiling; the awaitable visitor (round-1 root cause #2) lets the visitor early-stop per frame, bounding any *per-call* cost.
- **`FileStore` steady-state write rate (round-2 C-R2-P2-2 close — single number).** **Per-session: ≈ 10⁴ frames/s under default `FileStorePolicy::commit_per_message`** on commodity NVMe (per-record append + `fdatasync`/`FlushFileBuffers` per record; the ~150 µs flush floor dominates per `[2d §6.3]` precedent). Under `commit_batched(N)` the cadence amortises to ≈ N × 10⁴ frames/s with a documented data-loss window (§4.3.1); under `commit_interval(ms)` the cadence is driven by the timer and the data-loss window is up to `ms`. The §6.6 ≤ 250 µs (soft) per-call latency ceiling is consistent with the per-session 10⁴ frames/s number (1 / 10⁴ = 100 µs steady-state, with the 250 µs ceiling absorbing measurement noise and `fdatasync` jitter). The v0.2 §1.2 "≤ 10⁵ frames/s" line was an engine-wide aggregate ceiling across multiple sessions sharing one `file_io_executor` — that framing was inconsistent with the per-call latency budget; v0.3 drops the engine-wide aggregate from §1.2 (the engine-aggregate scaling argument lives in `[2d §6.4]`'s backpressure narrative, not here).
- **Recovery `retrieve(begin, end)` range.** Bounded by the session's seqnum window — at most one trading-day's worth of frames (≤ 10⁵). The awaitable-visitor pattern (§4.5) makes the cost streaming, not bulk-allocating, and lets the visitor stop early per frame.
- **Counter type (placeholder).** `seqnum_t = std::uint32_t` (matches FIX wire `MsgSeqNum(34)` — `[FIX-SL §4.1]` does not bound it explicitly). **Type ownership is the Phase-4 session-module spec** (§3.1 / §10 Q9); 2e consumes a placeholder alias until that spec lands. Overflow is **session-fatal** (`store_seqnum_overflow`; see §6.7 / N6) and requires operator/user intervention — the store does not autonomously reset.

These are caller-relevant scale boundaries; the seam works at any scale below the cap, and §10 records the trigger for revisiting if a concrete consumer hits the ceiling.

## 2. Non-goals

- **No typed-payload persistence.** The store stores opaque byte ranges; it does *not* parse, does *not* hold a `Dictionary&`, does *not* know about `decimal<T>`, does *not* know about per-version namespaces. Replay is byte-for-byte. This is the explicit confirmation of `[2a §10] Q3`. Typed-payload sinks (SBE/Avro snapshots, control-plane decoded streams, audit pipelines) are a *different* sink and live at `2j` (control plane) per `[2b §7.4]`.
- **No replicated / multi-node / cross-process MessageStore.** COM-009 is post-v1.0. The async surface is designed to admit such an impl in v1.x without breaking change (§10), but no design is provided here.
- **No SQL / columnar / object-store back-ends.** v1.0 ships local-fs and in-memory only.
- **No cross-session shared store.** Each `Session` owns a `MessageStore`; per N1 the factory returns `unique_ptr` so the type itself disallows `shared_ptr`-mediated cross-session aliasing. The supported pattern for impl change is close-and-reopen.
- **No QuickFIX synchronous `MessageStore` v-table adapter at any tier (round-2 close per Codex C-R2-P2-1).** Per the verdict in §1 goal 7 / §4.8.A. v0.2's §4.8.B "curated Path A subset" — a templated wrapper behind a four-condition `static_assert` chain over user-asserted traits — was retired in v0.3 because the four conditions are *runtime safety* properties (no Session re-entry, no sync disk I/O, no spawned threads, cooperative cancellation) that compile-time `static_assert` cannot enforce against user attestation; shipping it labelled as "compile-time-checked safe subset" misled the migration path. v1.0 ships Path B only.
- **No `drop-oldest` policy on the store path.** The store sits on the session message path per `[const §XV.15]`; `drop-oldest` is constitutionally banned. `MemoryStore` capacity-exhaustion returns `error::store_capacity_exhausted` (caller's session backpressure policy then decides between `block` and `disconnect_and_recover` per `[2d §4.5]`).
- **No mid-session `MessageStore` swap.** The factory is frozen at session open per `[arch §5.6]` and `[2d §4.5]`. The **factory-return type is `unique_ptr` per N1**; per `[arch §5.6]` no mid-session reconfiguration of any store-shape field. The supported pattern for impl change is close-and-reopen.
- **No store-side encryption.** Disk-level encryption is the operating system's responsibility (LUKS/dm-crypt on Linux, BitLocker on Windows).
- **No `flush()` as a public-interface method (N2).** `flush()` is no-op on `MemoryStore` and the only meaningful caller is graceful Session-close on `FileStore`. v0.1's 5-method interface dropped to **4** in v0.2; Session-close durability is delegated to `~FileStore` + an internal mechanism (see §4.1.1, §7.6).
- **No `async_shared_mutex` / RW-mutex (root cause #3).** Per `[SYN §3.2 Q6b]`, this primitive is post-v1.0; v1.0 stores use the exclusive `fixpp::sync::async_mutex` everywhere. **No `std::recursive_mutex` transitional adapter** — `[const §XV.9]` admits no carve-out.

## 3. Inherited surface

From `[arch §4.4]` (session module surface):

> `fixpp::session::MessageStore` — interface; writes return `asio::awaitable<...>` `[SYN §3.2 Q7]`. `fixpp::session::MemoryStore`, `fixpp::session::FileStore` — default impls.

From `[arch §6]` plugin pattern row:

> `fixpp::session::MessageStore` — Default impl `MemoryStore`, `FileStore` — Design doc **2e** — Async API; QuickFIX-compat shim on best-effort.

From `[arch §10]` row 2e:

> 2e — MessageStore async API — `asio::awaitable<...>` writes, QuickFIX-compat shim feasibility — Cross-cutting hooks: §6 plugin pattern; §4.4.

From `[arch §11]` Q3 (this doc OWNS the answer):

> QuickFIX-compat shim for synchronous `MessageStore` impls — feasible or document as known incompatibility — Owner **2e** — Disposition: Phase 2 validates `[SYN §3.2 Q7]`.

**Answer (locked here, v0.3 final):** Path B only (documented incompatibility — §4.8.A). v0.2's §4.8.B "Path A subset" wrapper was retired in v0.3 per Codex C-R2-P2-1 (escalated to P1 by Opus). Justification in §4.8.

From `[2a §10] Q3` (raw-frame storage confirmation — this doc OWNS the answer):

> Confirm with **2e** that (a) MessageStore records **raw FIX frames** for replay (not typed payloads), so replay is decimal-trait-agnostic, and (b) the typed-payload-persistence `static_assert(is_lossless_for_fix_float)` lives at the persistence-write site (§7.1 framing).

**Answer (locked here):** (a) confirmed — `MessageStore` accepts and emits `std::span<const std::byte>` byte ranges, byte-identical to the `wire::Writer` / `wire::Framer` output (§4.1, §6.2). (b) confirmed — `MessageStore` carries no `static_assert(is_lossless_for_fix_float)`; it doesn't parse and doesn't care.

From `[2b §4.5]` (`Writer::commit` finalises BodyLength + CheckSum) — load-bearing for root cause #1:

> `Writer::commit()` … fills in BodyLength (tag 9) and CheckSum (tag 10) … After commit, the Writer is consumed.

**Implication for 2e:** the only valid outbound `store()` input is the **post-commit** frame span. Storing pre-commit bytes would persist a frame with placeholder `9=` and missing/incorrect `10=` — the framer would reject it on replay (W-004 / W-005). v0.1's call ordering (`toApp → store(outbound) → Writer::commit`) is **wrong**; v0.2 fixes it to `toApp → Writer::commit → store(outbound, committed_span) → transport.async_write` (§6.1, §7.6).

From `[2b §6.4]` (lifetime contract on flyweights) and `[2b §6.6]` (allocation, exceptions, threading; three-arena pinning; view-escape rule):

**Implication for 2e:** the input span to `store(...)` is borrowed from the wire-side per-message arena, which the FSM resets after `fromApp` returns. The store therefore MUST take a *deep copy* of the bytes inside `store(...)` — the input span is borrowed for the duration of the awaitable's first suspension point at most. The store's persisted-byte buffers live in a separate `store_arena` (§8) whose lifetime is the store's, not the session's per-message arena's.

From `[2c §1.1]` and `[2c §7.2]`:

**Implication for 2e:** no `Dictionary&`, no `dict::reify`, no version-tag carried alongside the bytes. Stored frames are opaque.

From `[2d §4.5]` (`SessionConfig::store_factory`), `[2d §4.7]` (cancellation propagation API — two-phase close; per-mode effect table at lines 798–809 enumerates the 9 affected ops including `MessageStore::write` (in-flight) at line 808 — round-2 RC#2 close: 2e's engine-internal `FileStore::flush_for_session_close()` hook is owed to `[2d §4.7]` as Appendix D §D.2 drop-in, not a phantom citation in 2d as v0.2 implied), `[2d §4.8]` (`session_executor` — project-owned wrapper class), `[2d §6.5]` (`cancellable_dispatch` — typed `awaitable<expected_t<void>>`; `dispatch_aborted` is the cancellation outcome that must be observable in `expected_t`), `[2d §6.7]` (error model — `dispatch_aborted` and `clock_sleeps_cancelled` are the cancellation peers `store_cancelled` joins in the `FIXPP_ERR_CANCELLED` group):

**Implication for 2e:**
- Every `MessageStore` async method's invocation runs **inside the session serialisation domain** (per `[2d §4.8]` round-3 root cause #1: the wrapper holds either `asio::strand` under `per_session_strand` or the user-attested `any_io_executor` under `direct_executor`); the awaitable's completion handler rebinds to that same wrapper for caller-side observability per `[2d §7.3]`.
- Cancellation flows through ASIO native slots per `[const §XI.2]`; cancellation **outcome** is observable in `expected_t::unexpected{store_cancelled}` (mirroring `[2d §6.5]`'s `dispatch_aborted` shape) — see §6.1 cancellation subsection (round-1 root cause #1).
- Per-call PMR on the hot path comes from `SessionConfig::session_arena` per `[2d §4.5]`.
- The `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` per `[2d §7.9]` is the timing source if the store wants to stamp persistence times. v1.0 default impls do **not** persist timestamps alongside frames.
- `[2d §4.5]` v0.4 line 534 declares `SessionConfig::store_factory` as `std::shared_ptr<MessageStoreFactory>`. **N1 flags this as an inherited inconsistency**: `[arch §5.6]` mid-session-swap ban implies unique ownership; `unique_ptr` is the type-correct choice. 2e holds `unique_ptr<MessageStoreFactory>` on `SessionConfig`. **Round 2 close**: the cross-doc amendment is declared as Appendix D §D.1 drop-in (round-2 root cause #2 fix per Codex C-R2-P1-4); the orchestrator applies the 2d-side edit at 2e sign-off, exactly as 2d v0.4 / 2c v1.3 precedent. Until that commit lands, 2e's factory CONSTRUCTION is `std::unique_ptr<MessageStore>`-returning regardless; the `SessionConfig`-side field type is the gating sibling-doc edit.

From `[const §XIV.2]` (≤5 pure-virtual cap):

**Implication for 2e:** the `MessageStore` interface in §4.1 has **4** pure-virtual methods (after dropping `flush()` per N2). Below the cap; no Gate-A-eligible justification needed. The visitor at §4.5 has **1 pure + 1 overridable** virtual method (per Codex P3-12 / N11; the cap doesn't bind the visitor — it's a per-call callback shape, not a plugin interface).

From `[SYN §3.2 Q7]` (DECIDED):

> `MessageStore` writes return `asio::awaitable<...>`. Synchronous impls satisfy the API trivially (`co_return`); async / journal / replicated impls suspend.

From `[SYN §3.2 Q8]` (DECIDED — store-write mutex):

> Store-write path always uses mutex regardless of policy.

From `[SYN §3.2 Q6b]` (DECIDED — `async_shared_mutex` is post-v1.0):

**Implication for 2e:** v1.0 uses the exclusive `fixpp::sync::async_mutex` for every store-state mutation. **No RW-mutex / `async_shared_mutex` and no `std::recursive_mutex` adapter** (root cause #3).

From `[const §XV.4]` (banned QuickFIX-`FileStore` pattern):

**Implication for 2e:** the default `FileStorePolicy` is **NOT** `commit_per_message` blocking-fsync-per-write in the QuickFIX sense; it is an append-only log with `commit_per_message` (a single `fdatasync` per record) by default. Per `[const §XV.4]`'s "async journal with background flush" framing, opt-in `commit_batched(N)` and `commit_interval(ms)` policies amortise the fsync (§4.3, §6.3).

From `[const §II.3]` (Tier 2 — Windows/MSVC):

**Implication for 2e (round-1 root cause #4 + round-2 root cause #1):** the FileStore on-disk algorithm MUST be portable to Windows. v0.1's directory-fsync scheme is Linux-specific; v0.2 shipped an append-only-log + truncate-on-startup scheme without directory-fsync; **v0.3 collapses the v0.2 two-files-per-direction shape down to a single log file per session** with per-record direction tags, so the truncate-then-flush atomicity claim (§6.3.4) actually applies to the algorithm as written (§6.3 round-2 root cause #1 fix per Codex C-R2-P1-2).

This document refines the surface above; it does **not** diverge from any sibling-doc contract except where explicitly flagged: `[2d §4.5]` factory-ownership shape per N1 — round-2 amendment in Appendix D §D.1 (orchestrator applies at 2e sign-off); `[2d §4.7]` graceful-close store-flush hook — round-2 amendment in Appendix D §D.2 (orchestrator applies at 2e sign-off).

### 3.1 Inherited primitives — exhaustive list (root cause #3)

Per `[const §VI.5]`, every primitive 2e leans on must trace to its owning section. v0.1 invented or extended primitives without owner-doc citations; v0.2 enumerates them.

| Primitive | Type | Owner doc / section | 2e's use |
|---|---|---|---|
| `seqnum_t` | type alias | **Phase-4 session-module spec** (not yet drafted) — see §10 Q9. **Until that spec lands**, 2e consumes the placeholder `using seqnum_t = std::uint32_t` from `<fixpp/session/seqnum.hpp>` per `[FIX-SL §4.1]`'s wire `MsgSeqNum(34)` semantics. | §4.1 method 1/2/3, §4.7. |
| `direction_t` | enum | 2e (declared here; not invented elsewhere) | §4.1, §4.6. |
| `expected_t<T>` | template alias | `[arch §5.3]` | every method return. |
| `awaitable<T>` | ASIO type | `[arch §5.1]` / `[const §XI.1]` | every method return. |
| `fixpp::sync::async_mutex` | mutex class | **2f** (not yet drafted) — contract via `[2d §7.4]` executor-compat surface. Hand-off gate: 2f sign-off required before 2e implementation. | §6.4. |
| `cancellable_dispatch(session_executor, slot, handler)` | function | `[2d §6.5]` — returns `awaitable<expected_t<void>>` with `dispatch_aborted` as the cancellation outcome. 2e's `store_cancelled` mirrors the shape. | §6.1 cancellation, §7.6. |
| `session_executor` (project-typed wrapper class) | class | `[2d §4.8]` v0.4 | §6.1, §7.3. |
| `effective_clock` | resolution | `[2d §7.9]` (`= clock_override ?: engine.clock`) | §6.5 (not consumed by v1.0 default impls; reserved for audit/replication impls). |
| `session_arena` | PMR resource | `[2d §4.5]` — per-session lifetime | §6.1 hot-path scratch + awaitable promise frame fallback. |
| `store_arena` | PMR resource | **2e** (declared here) — per-store-instance lifetime, peer of `session_arena`, **NOT** a sub-resource | §4.2, §4.3, §8. |
| `MessageStoreFactory` ownership shape | C++ pointer type | `[arch §5.6]` (frozen at session open) + `[2d §4.5]` (held by `SessionConfig`). 2e returns `std::unique_ptr<MessageStore>` from `make()`; the factory itself is `std::unique_ptr<MessageStoreFactory>` on `SessionConfig`. **`[2d §4.5]` v0.4 line 534 holds it as `std::shared_ptr<MessageStoreFactory>` — round-2 root cause #2 close: orchestrator applies Appendix D §D.1 drop-in at 2e sign-off (per 2d v0.4 / 2c v1.3 sibling-doc-edit precedent).** | §4.4, Appendix D §D.1. |
| `frame_view::bytes()` | accessor | `[2b §4.2]` | inbound `store()` source. |
| `Writer::commit() && -> expected_t<std::size_t>` | finaliser | `[2b §4.5]` | outbound `store()` precondition (root cause #1). |

**Hand-off gates:**
- 2f sign-off is required before 2e implementation lands (`async_mutex` shape + executor-compat surface).
- The Phase-4 session-module spec MUST publish `seqnum_t` (or 2e's `<fixpp/session/seqnum.hpp>` placeholder is promoted to that spec) before sign-off; see §10 Q9.
- **Two sibling-doc Appendix D drop-ins are applied at 2e sign-off** (round-2 root cause #2 close — orchestrator coordination, not separate Gate A re-runs of 2d v0.4):
  - **Appendix D §D.1** — `[2d §4.5]` `SessionConfig::store_factory` field type `shared_ptr → unique_ptr` (round 1, refined in round 2 with byte-exact diff form per Opus N2-P3-1).
  - **Appendix D §D.2** — `[2d §4.7]` per-mode effect table gains a `FileStore::flush_for_session_close()` row + a one-paragraph contract on the hook's cancellation/error semantics (NEW in round 2 per Codex C-R2-P1-5).

## 4. Public API — C++

### 4.1 `MessageStore` interface

```cpp
// include/fixpp/session/message_store.hpp
namespace fixpp::session {

enum class direction_t : std::uint8_t {
    inbound  = 0,    // frames received from the peer (post-Framer, pre-Parser).
    outbound = 1,    // frames emitted to the peer  (post-Writer::commit per [2b §4.5]).
};

using seqnum_t = std::uint32_t;    // PLACEHOLDER — owner = Phase-4 session-module spec; see §3.1 / §10 Q9.

// retrieve_visitor — awaitable per-frame callback. See §4.5.
class retrieve_visitor;

// MessageStore — 4 pure-virtual methods per [const §XIV.2] (well below the
// ≤5 cap; v0.2 dropped flush() per N2 — see §4.1.1).
//
// VIRTUAL-vs-CONCEPT decision (N3): the surface stays virtual to satisfy
// 2i's C-ABI (callable handle / vtable shape) and to admit the dynamic
// shape `std::unique_ptr<MessageStore>` Session holds. The vtable cost
// (~5–15 ns warm-cache per dispatched call) is named in the §6.6 ceiling
// budget. A concept-bounded alternative (see §4.1.1 "concept vs virtual")
// would eliminate the dispatch but require Session to be templated on the
// store, breaking 2i's runtime-handle contract.
//
// Lifetime: the implementation is owned by the Session. Construction:
// `MessageStoreFactory::make()` returns `expected_t<unique_ptr<MessageStore>>`
// per [arch §5.6] (N1). The Session moves the unique_ptr into its private
// store slot at Session::open and destroys it at Session teardown. No
// shared_ptr; no shared store across two Sessions; no mid-session swap.
//
// Threading: every async method is invoked on the session strand
// (the session_executor wrapper per [2d §4.8]); completion handlers
// rebind to the same strand for caller-side observability per [2d §7.3].
// Implementations MAY post work onto an internal I/O strand and return
// to the session strand on completion; doing so does NOT relax the
// store-write mutex contract (§6.4 / [SYN §3.2 Q8]).
//
// Allocation: store() hot path is bound by [const §VIII.5] (zero global
// new/delete). MemoryStore::store performs zero allocator calls (fixed-slot
// layout per §4.2; N9). FileStore::store allocates only from store_arena
// for the framed-record scratch (§6.3). retrieve()'s recovery path is
// allowed to allocate from caller-supplied mr (visitor-side).
//
// Exceptions: every method is noexcept on the hot path; PMR allocation
// failures route through trap_throw per [2a §4.2] and surface as
// expected_t::unexpected per [const §VIII.5] (N13: cite [2a §4.2] +
// [const §VIII.5], NOT [const §VIII.4]).
//
// Cancellation result contract (§6.1 / root cause #1, C-P2-8 escalated):
// every method's awaitable completes with expected_t::unexpected{store_cancelled}
// when cancellation wins before the linearisation point; mirrors
// [2d §6.5]'s dispatch_aborted shape so the FSM can distinguish recovery
// errors from benign cancellation.
class MessageStore {
public:
    virtual ~MessageStore() = default;

    // 1. Persist a single raw frame at sequence number `seq` in direction `dir`.
    //
    //    OUTBOUND CALL ORDERING (root cause #1): the FSM MUST call this
    //    AFTER wire::Writer::commit() returns success (per [2b §4.5]) and
    //    BEFORE transport::async_write. The committed_span is byte-identical
    //    to what transport::async_write will emit. If async_write is
    //    cancelled mid-flight the frame remains persisted (peer ResendRequest
    //    will replay it on reconnect — the documented recovery case).
    //
    //    INBOUND CALL ORDERING (root cause #1): the FSM calls this AFTER
    //    Parser succeeds and BEFORE the application callback fromApp/fromAdmin
    //    so a callback crash does not lose the persisted frame.
    //
    //    `frame` aliases caller-owned bytes; the implementation MUST copy
    //    into store-owned storage before the awaitable's first suspension
    //    point — [2b §6.4]'s view-escape contract makes the input span
    //    unsafe to retain past suspension.
    //
    //    Concurrency: invocations on the SAME store instance serialise
    //    against the per-instance writer mutex (§6.4). Under the v1.0
    //    single-session-serialisation-domain discipline (per [2d §4.8]
    //    round-3 root cause #1), contention is zero. The second arrival
    //    under deliberate violation **suspends FIFO-fairly on async_mutex**
    //    (per Codex P1-5 / [SYN §3.2 Q8] — store does NOT return
    //    store_concurrent_writer; debug-build assert at the Session layer
    //    catches the invariant violation per [2d §6.1]). The
    //    `store_concurrent_writer` v0.1 error variant is REMOVED.
    //
    //    Sequence-number verification (Opus round-2 N2-P2-3 close): the
    //    store verifies `seq == next_seqnum(dir, false)` inside its
    //    mutexed critical section (after mutex acquire, before slab
    //    memcpy / pwrite). On mismatch, returns store_seqnum_out_of_order
    //    without state mutation. The verification cost (~1 ns atomic
    //    compare under v1.0 mutex-always serialisation per Opus N2-P2-2)
    //    is included in the §6.6 ceiling for MemoryStore::store.
    //
    //    Cancellation: see §6.1 cancellation subsection — completes with
    //    store_cancelled if cancelled before the linearisation point.
    [[nodiscard]] virtual asio::awaitable<expected_t<void>>
    store(seqnum_t seq,
          std::span<const std::byte> frame [[clang::lifetimebound]],
          direction_t dir) noexcept = 0;

    // 2. Retrieve the persisted frames in [begin, end] inclusive (FIX-style;
    //    end == 0 is interpreted as "infinity" per [FIX-SL §4.8.3]).
    //
    //    INVALID INPUT (N9): begin == 0 is REJECTED with store_seqnum_invalid
    //    (FIX wire seqnums start at 1 per [FIX-SL §4.1]). end != 0 && end < begin
    //    is REJECTED with store_invalid_range. These checks happen before any
    //    visitor invocation.
    //
    //    The visitor receives one awaitable call per persisted frame in
    //    seqnum order (root cause #2; see §4.5). The visitor's awaitable
    //    return drives async retransmit / GapFill decisions cleanly.
    //
    //    Mutex hold rule (root cause #2): the store does NOT hold the
    //    per-instance writer mutex across visitor co_await. The visitor
    //    sees a snapshot or a mutation-detecting iterator; if the store
    //    detects mid-traversal mutation, the next visitor call observes
    //    the new state without UB (frames already visited are not re-visited).
    //
    //    Span lifetime (root cause #2): the frame_view passed to
    //    on_frame is valid only until the visitor's awaitable resumes.
    //    If the visitor needs to keep bytes, it copies into its own arena.
    //
    //    Returns success if iteration completed without store-side error
    //    (visitor stop is success; visitor abort surfaces the visitor's
    //    error). Returns store_seqnum_gap if the requested range overlaps
    //    a seqnum that was never persisted (unless the gap is at the
    //    trailing edge of `end == 0` — that's a normal end-of-store).
    //
    //    Allocation: visitor-side; the store does NOT bulk-materialise.
    //
    //    Direction: the FSM dispatches retrieve() against direction::outbound
    //    on its own (replaying its own sent frames in response to peer's
    //    ResendRequest); inbound retrieval is for diagnostic / audit use.
    [[nodiscard]] virtual asio::awaitable<expected_t<void>>
    retrieve(seqnum_t begin,
             seqnum_t end,
             direction_t dir,
             retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept = 0;

    // 3. Read the next-expected sequence number for `dir`, OPTIONALLY
    //    incrementing it (post-increment semantics: returns the value
    //    before the increment). Implements OSS-002's
    //    `getNextSenderMsgSeqNum`/`incrNext...`/`getNextTargetMsgSeqNum`
    //    as a single method (option (a) per §4.1.1).
    //
    //    Concurrency (Opus round-2 N2-P2-2 close): per §6.4, ALL FOUR
    //    methods acquire the writer mutex — including this one. The
    //    counter read+increment is serialised by the mutex; v0.2's
    //    "atomic fetch-add" wording is dropped because the mutex is the
    //    serialisation primitive (an atomic on top of a mutexed critical
    //    section is redundant). The §6.6 ceiling for next_seqnum is
    //    sized to mutex acquire + mutexed counter access + release
    //    (≤ 50 ns uncontended).
    //
    //    On overflow (current value == seqnum_max and increment requested)
    //    returns store_seqnum_overflow without incrementing. SESSION-FATAL
    //    per N6: the FSM MUST surface this to user code (typically via
    //    onLogout with a reason or a session-level error callback); the
    //    store does NOT autonomously reset.
    [[nodiscard]] virtual asio::awaitable<expected_t<seqnum_t>>
    next_seqnum(direction_t dir, bool increment) noexcept = 0;

    // 4. Reset both counters to 1 and truncate persisted frames per
    //    [FIX-SL §4.5.4] / [FIX-SL §4.8.6] hard-reset semantics ([FIX-SL
    //    §4.8.6] per coverage-index row 83 covers `SequenceReset-Reset`
    //    (35=4, 123=N) wire-message hard-reset semantics, which is the
    //    operation reset() performs). The session FSM calls this on
    //    explicit reset paths (ResetOnLogon=Y, etc., per [FIX-SL §4.4] /
    //    S-017); 2e provides the contract. Note: the user-facing
    //    `ResetSeqNumFlag(141)=Y` Logon recovery flow lives at
    //    [FIX-SL §4.4.2] / [FIX-SL §4.4.3]; reset() is the storage-side
    //    primitive the FSM composes with that Logon flow per §6.7
    //    `store_seqnum_overflow` row.
    //
    //    Atomic: either both counters are 1 AND no frames are persisted,
    //    OR the operation reports failure and the prior state is intact.
    //    Success-return implies durability — Linux mandatory parent-dir
    //    fsync per POSIX `rename(2)`; Windows mandatory MOVEFILE_WRITE_THROUGH
    //    per Win32 `MoveFileExW` semantics (round-3 post-cap pin per
    //    Codex C-R3-P1-2; see §6.3 / §6.3.5). FileStore implements this
    //    via atomic-rename of a freshly-prepared replacement log
    //    (§6.3.4 / round-2 RC#1 fix); MemoryStore implements via
    //    index-and-slab-state reset.
    //
    //    NOTE: there is no public flush() (N2). Graceful Session-close
    //    durability for FileStore is delegated to ~FileStore + an internal
    //    Session→FileStore signal — see §4.1.1 + §7.6.
    [[nodiscard]] virtual asio::awaitable<expected_t<void>>
    reset() noexcept = 0;
};

}  // namespace fixpp::session
```

#### 4.1.1 4-pure-virtual count justification + concept-vs-virtual + `flush()` removal

**Final method count: 4 pure-virtual** (`store`, `retrieve`, `next_seqnum`, `reset`). Well below the `[const §XIV.2]` ≤5 cap. No Gate-A-eligible justification needed.

**Why `flush()` was removed (N2).** v0.1 listed `flush()` as the 5th method. Three reasons to drop it:

1. **No-op on `MemoryStore`** — `flush()` was a `co_return {};` shell. Burning one of the ≤5 plugin slots on a method that does nothing on the default test impl is poor surface-area economics.
2. **Single caller on `FileStore`** — Session-close graceful path. That caller can equivalently invoke a non-virtual mechanism (a private `flush_now()` hook on the concrete `FileStore`, signalled by `Session` over `cancellable_dispatch` per `[2d §6.5]`, or `~FileStore`'s destructor flush on graceful close).
3. **Higher-utility methods exist** that aren't on the surface — `health()` / `is_durable()` for FSM disconnect-and-recover decisions, `truncate_before(seqnum)` for operational journal trimming. v0.2 reserves the freed slot for the right method when a real consumer surfaces.

The removal is captured in §7.6 (Session-close path uses `~FileStore` + an internal callable, not a public `flush()`).

**Why virtual stays over concept (N3).** A concept-bounded `template <MessageStore S> class Session<S>` would be zero-overhead but template-spread Session across the whole engine. Virtual stays because:

- **2i C-ABI requires runtime polymorphism.** The C-side handle `fixpp_store_t` wraps a `MessageStore*` whose vtable is reachable at runtime; a concept impl would need a manual vtable mock at the C-ABI boundary. (This decision is 2i's; 2e records the constraint.)
- **`std::unique_ptr<MessageStore>` is the type-erased ownership shape Session holds.** Per N1, Session owns the store via `unique_ptr<MessageStore>`; that pointer is virtual-by-construction.
- **Plugin discoverability.** `MessageStore` participates in `[arch §6]`'s plugin pattern; users implementing a custom store inherit + override.

**Cost of choice.** A virtual call on the warm-cache hot path costs ~5–15 ns per dispatch (branch-predicted indirect call). The §6.6 budget for `MemoryStore::store` 200-byte frame names this explicitly: ~80–150 ns total budget, of which up to ~15 ns is vtable dispatch (~7.5% of the 200 ns ceiling). The ceiling is honest, not loose.

**Why `direction_t` collapse over interface split.** Same argument as v0.1: the asymmetry is on the FSM caller side, not the store side. Splitting into `SeqnumStore` would force two atomicity contracts in every impl. Collapsed: one atomicity invariant per impl. OSS-002's QuickFIX shape colocates all four operations on one interface; v1.0 follows that precedent.

#### 4.1.2 `[[clang::lifetimebound]]` / `[[nodiscard]]` discipline

- Every awaitable-returning method is `[[nodiscard]]`.
- The `frame` parameter on `store(...)` carries `[[clang::lifetimebound]]` per `[2b §6.4]`'s precedent; the implementation contract (§6.2) requires deep-copy before suspension, but the annotation flags caller-side misuse (passing a temporary).
- The `visitor` parameter on `retrieve(...)` carries `[[clang::lifetimebound]]`.
- `expected_t<T>` returns are `[[nodiscard]]` per `[2a §4.2]` precedent.

### 4.2 `MemoryStore` — default in-memory impl

```cpp
// include/fixpp/session/memory_store.hpp
namespace fixpp::session {

// MemoryStore — TEST / EMBEDDED USE ONLY (round-2 close on Codex C-R2-P1-3
// + Opus N9 escalation). Production deployments MUST use FileStore or a
// custom impl; sessions running 24/7 with the default capacity will hit
// store_capacity_exhausted long before they hit a counter reset.
//
// Capacity policy: bounded(N) (default) rejects with store_capacity_exhausted
// when full; unbounded grows without limit (opt-in for embedded test rigs);
// drop-oldest is BANNED per [const §XV.15] and is not exposed at the API.
class MemoryStore final : public MessageStore {
public:
    enum class capacity_policy : std::uint8_t {
        bounded   = 0,    // default; rejects with store_capacity_exhausted when full per [const §XV.15].
        unbounded = 1,    // opt-in; grows without limit. Embedded test rigs only — sized by the
                          // lifetime of a single test run; production deployments MUST NOT use
                          // this on a long-running session.
    };

    struct Config {
        // Capacity policy (round-2 fix per Codex C-R2-P1-3).
        capacity_policy policy = capacity_policy::bounded;

        // Per-direction ring capacity (used only when policy == bounded).
        // Round-2 default: 10_000 (down from v0.2's 200_000, which at the
        // 256 KiB max_frame_bytes cap defaulted into a worst-case ~97.7 GiB
        // slab — vastly above the 1 GiB EngineConfig::max_store_memory_per_session
        // default cap, tripping the construction guard). MemoryStore is
        // sized for tests / embedded use only; long-running production
        // sessions hit the cap before the counter resets. The user MAY
        // raise these values, but MUST also raise
        // EngineConfig::max_store_memory_per_session to match the new
        // worst-case (capacity * max_frame_bytes), or reduce
        // max_frame_bytes below 256 KiB.
        std::size_t inbound_capacity  = 10'000;
        std::size_t outbound_capacity = 10'000;

        // Maximum frame size accepted on store() — sized from
        // Framer::Config::max_frame_bytes per [2b §1.2] / [2b §4.2].
        // Default 256 KiB. Drives the fixed slab layout (round-1 N9 / Codex P2-9).
        std::size_t max_frame_bytes   = 256 * 1024;

        // PMR resource for the store's persisted-byte buffers (the
        // store_arena in §8). Independent of the session arena. Must
        // outlive the MemoryStore instance. Null = engine-default
        // monotonic resource sized to (inbound_capacity + outbound_capacity)
        // * max_frame_bytes at construction. The slab is allocated in ONE
        // call at construction (round-1 N9); store() performs zero
        // allocator calls.
        std::pmr::memory_resource* store_resource = nullptr;
    };

    // Construction validates Config against EngineConfig::max_store_memory_per_session
    // (default 1 GiB, round-1 N9 storage-DoS bound). With round-2 default
    // capacities, worst-case is ~5 GiB and construction fails with
    // store_factory_failed unless the operator explicitly raised the cap
    // (see §1.2 close on Codex C-R2-P1-3). Failure surfaces through the
    // factory's expected_t per §4.4.
    explicit MemoryStore(Config c) noexcept;

    // MessageStore overrides — see §4.1 for contract.
    asio::awaitable<expected_t<void>>
        store(seqnum_t, std::span<const std::byte>, direction_t) noexcept override;
    asio::awaitable<expected_t<void>>
        retrieve(seqnum_t, seqnum_t, direction_t, retrieve_visitor&) noexcept override;
    asio::awaitable<expected_t<seqnum_t>>
        next_seqnum(direction_t, bool) noexcept override;
    asio::awaitable<expected_t<void>>
        reset() noexcept override;
};

}  // namespace fixpp::session
```

Notes:

- **Synchronous semantics; awaitable return is satisfied trivially.** Each method's implementation does its work inside the session serialisation domain (per `[2d §4.8]`) and `co_return`s; no suspension point. HALO targets these awaitables for elision per `[const §XI.6]`.
- **Fixed-slot + fixed-slab layout under `capacity_policy::bounded`** (round-1 N9 / Codex P2-9 — unchanged structurally; round-2 only changed the defaults). At construction:
  - `entry[inbound_capacity + outbound_capacity]` ring (each entry = `{ seq, len, slab_offset }`, 16 B);
  - `slab[(inbound_capacity + outbound_capacity) * max_frame_bytes]` byte slab.
  Both arrays allocated in one PMR call from `store_resource` at construction. **`store()` performs zero allocator calls** — it computes the per-direction ring slot, memcpy's `frame` into the slot's pre-reserved slab range, writes the entry's `(seq, len)`, advances the per-direction tail. The reset path zeroes the entry array (lazy slab reuse — no zero pass on the slab).
- **`capacity_policy::unbounded`** (opt-in, embedded test rigs only): each direction is a `std::pmr::deque<entry>` over `store_arena`; the slab is grown on demand from the same arena. `store()` MAY allocate; the §6.6 latency ceilings DO NOT apply under unbounded mode (the "zero-allocator-calls" §9 seam #16 is bounded-only). This is the only deviation from `[const §VIII.5]` zero-`new`/`delete` discipline 2e admits, and only under explicit user opt-in.
- **PMR posture: store-owned `store_arena`** per §8. `MemoryStore` does **not** alias `SessionConfig::session_arena`. Default if `Config::store_resource == nullptr`: an engine-provided dedicated `monotonic_buffer_resource` whose total reservation is the slot+slab footprint above.
- **`drop-oldest` is BANNED.** Per `[const §XV.15]`, the policy is not exposed; if `policy == bounded` and the ring fills, `store()` returns `store_capacity_exhausted`, the session's backpressure policy (`block` / `disconnect_and_recover` per `[2d §4.5]`) takes over.
- **Concurrency.** A single per-instance `fixpp::sync::async_mutex` per `[const §XI.3]` (the writer mutex per §6.4). Under the v1.0 single-session-serialisation-domain discipline (per `[2d §4.8]` round-3 root cause #1: covers BOTH `per_session_strand` and `direct_executor` modes), contention is zero; the mutex is defence-in-depth against session-serialisation-domain violations (whether the user's `direct_executor` attestation is incorrect, or the FSM's `per_session_strand` contract is violated — Opus round-2 N2-P3-2 vocabulary alignment with 2d v0.4). **No RW-mutex / no shared lock** (round-1 root cause #3).
- **No timestamp persisted alongside frames.** The bytes are exactly what the wire produced.

### 4.3 `FileStore` — default file-based impl

```cpp
// include/fixpp/session/file_store.hpp
namespace fixpp::session {

// Append-only-log durability policies. The log is a single file per
// session; every store() appends a record carrying its `dir`
// discriminator (§6.3.1 round-2 single-log shape; round-3 post-cap
// pin per Codex C-R3-P1-1 — v0.3's "per direction per session"
// wording was a stale collateral from v0.2's two-files-per-direction
// shape). The policies differ on WHEN the OS-level flush happens.
struct FileStorePolicy {
    enum class kind : std::uint8_t {
        // Default per [const §XV.4]'s "async journal with background flush"
        // framing: every successful store() returns AFTER fdatasync (Linux)
        // / FlushFileBuffers (Windows). Per-session throughput: ≈ 10⁴
        // frames/s on commodity NVMe (single number aligned across §1.2,
        // §4.3.1, §6.6 per Codex C-R2-P2-2 close — the §6.6 ≤ 250 µs
        // soft per-call ceiling is consistent with this rate; the v0.2
        // §1.2 "≤ 10⁵ frames/s" line was an engine-wide aggregate
        // mis-read as per-session and is dropped from §1.2). Crash-loss
        // window: zero. Use this in regulated / financial-grade
        // deployments where every persisted record is a recovery contract.
        commit_per_message = 0,

        // Opt-in. Flush after every N records. Throughput: amortises across
        // the batch. Crash-loss window: up to N records. The session FSM
        // SHOULD invoke a graceful Session-close flush before Logout exchange
        // completes (see §7.6) so the regulator-mandated tail records make
        // it to durable storage.
        commit_batched     = 1,

        // Opt-in. Flush on a periodic timer. Throughput: max. Crash-loss
        // window: bounded by interval. NOT recommended for production
        // financial deployments without an external durability hook.
        commit_interval    = 2,
    };

    kind                       which = kind::commit_per_message;
    std::size_t                batch_size = 1;            // commit_batched only.
    std::chrono::milliseconds  interval = std::chrono::milliseconds{100};  // commit_interval only.
};

class FileStore final : public MessageStore {
public:
    struct Config {
        // Directory holding session-local store files. Created if missing.
        // Path encoding is std::filesystem::path native (UTF-8 on Linux;
        // UTF-16 on Windows; the engine canonicalises at Engine::open).
        std::filesystem::path directory;

        // Session identity; encoded into filename so two sessions in the
        // same directory don't collide (sender__target.log — single log
        // per session per §6.3.1; round-3 post-cap pin per Codex C-R3-P1-1
        // — v0.2's `sender__target.<dir>.log` two-files-per-direction
        // shape was retired in v0.3 round-2 RC#1).
        //
        // FILESYSTEM-SAFETY VALIDATION (v0.5 per §D.4 — Gap 1 close).
        // FileStoreFactory::make() MUST validate these CompID values
        // before composing the filename and before opening any file or
        // taking the advisory lock: each MUST be non-empty, MUST NOT
        // contain a path separator ('/' on Linux; '/' or '\\' on
        // Windows), a NUL byte, a `.` or `..` path segment, a control
        // character in [0x00, 0x1F] or 0x7F, and the composed path
        // component MUST NOT exceed NAME_MAX (pathconf(_PC_NAME_MAX) on
        // Linux; MAX_PATH minus directory prefix on Windows). On
        // violation, make() returns store_factory_failed before any
        // file is opened. Validation uses primitive
        // string_view::find_first_of / find calls; std::filesystem::path
        // constructors are NOT invoked until validation has passed
        // (preserves the noexcept contract on make()). The same
        // validation is mirrored at quickfix_compat::cfg_loader as
        // defense in depth. FIX-SL §4.3 admits ASCII printables in
        // Tag 49 / Tag 56 — the protocol does NOT itself constrain
        // CompIDs to alphanumeric, so the validation is genuinely
        // load-bearing. See Appendix D §D.4 for the normative bind.
        std::string sender_comp_id;
        std::string target_comp_id;

        // Durability knob.
        FileStorePolicy policy = {};

        // Maximum frame size accepted on store(). Per-record cap; the log
        // file itself has no size limit other than fs free space.
        std::size_t max_frame_bytes = 256 * 1024;

        // Executor for the file-I/O work (§4.3.2).
        asio::any_io_executor file_io_executor;

        // PMR resource for store-owned scratch (per-write framing buffers,
        // index entry growth). Independent of session arena per §8.
        std::pmr::memory_resource* store_resource = nullptr;
    };

    explicit FileStore(Config c) noexcept;

    asio::awaitable<expected_t<void>>
        store(seqnum_t, std::span<const std::byte>, direction_t) noexcept override;
    asio::awaitable<expected_t<void>>
        retrieve(seqnum_t, seqnum_t, direction_t, retrieve_visitor&) noexcept override;
    asio::awaitable<expected_t<seqnum_t>>
        next_seqnum(direction_t, bool) noexcept override;
    asio::awaitable<expected_t<void>>
        reset() noexcept override;

    // Engine-internal: signalled by Session at graceful close per §7.6.
    // NOT virtual, NOT on MessageStore (replaces v0.1's public `flush()`;
    // see N2 + §4.1.1). Idempotent. The engine reaches the concrete
    // FileStore through the session's stored unique_ptr<MessageStore>
    // via concept-shaped dispatch (round-3 post-cap pin per Opus
    // N3-P2-1):
    //
    //     namespace fixpp::session::detail {
    //     template <class S>
    //     concept has_flush_for_session_close = requires(S& s) {
    //         { s.flush_for_session_close() }
    //             -> std::same_as<asio::awaitable<expected_t<void>>>;
    //     };
    //     }  // namespace fixpp::session::detail
    //
    // The Session-close sequencer attempts a `static_cast<FileStore*>` on
    // the unique_ptr's stored pointer ONLY when the session was opened
    // with FileStoreFactory (the engine knows the concrete type from the
    // factory it called); the cast is therefore a known-safe down-cast
    // gated on the factory-type tag the engine retains, NOT a
    // `dynamic_cast` and NOT RTTI. Custom MessageStore impls (any
    // user-supplied factory other than FileStoreFactory) are excluded
    // from the dispatch path at the engine layer — the concept above is
    // satisfied only by `FileStore`, so user impls do NOT need to define
    // `flush_for_session_close()` and a no-op default is the implicit
    // contract. MemoryStore explicitly does NOT define
    // flush_for_session_close() (no durability work to drain; the
    // concept fails the requires-expression and the engine skips the
    // call at compile time when the factory is MemoryStoreFactory).
    //
    // The mechanism keeps the [const §XIV.2] ≤5 pure-virtual cap on
    // MessageStore at 4 — this hook is non-virtual on FileStore only
    // and not declared on the MessageStore base; users who write a
    // custom MessageStore impl are not asked to supply a `flush_for_session_close()`.
    //
    // CROSS-DOC HOOK (round-2 root cause #2 close per Codex C-R2-P1-5):
    // the callsite contract — that the engine invokes this hook on the
    // graceful-close path BEFORE phase 1 ends, AFTER any in-flight
    // store() awaitables drain — is owed to [2d §4.7] as the Appendix D
    // §D.2 drop-in. v0.2 inlined a phantom citation in §7.6 / status
    // block; v0.3 declares the cross-doc edit explicitly so 2d's
    // per-mode effect table at lines 798–809 lands the new row at 2e
    // sign-off. Cancellation/error semantics: under graceful close the
    // hook RUNS TO COMPLETION (it is the durability seam honouring
    // [FIX-SL §4.5] graceful-Logout durability — drains commit_batched
    // / commit_interval pending records to durable storage); it is
    // NOT under phase-1's child cancellation timeout slot. Under
    // terminal close (cancellation_type::total per [2d §6.5]) the hook
    // is NOT invoked (root cancellation skips phase 1, the user accepts
    // data loss; recovery on next session open truncates the last
    // incomplete record per §6.3.4 startup contract); store_cancelled
    // surfaces ONLY in the forced-cancellation path, NOT under graceful
    // Logout. A mid-hook fdatasync error surfaces as
    // expected_t::unexpected{ store_io_failure } and the engine logs
    // it before completing the close. See §6.2.1 + Appendix D §D.2 for
    // matching wording. (Round-3 post-cap pin per Codex C-R3-P1-3 +
    // Opus N3-P2-1.)
    asio::awaitable<expected_t<void>>
        flush_for_session_close() noexcept;
};

}  // namespace fixpp::session
```

#### 4.3.1 fsync policy decision (Codex P1-6 fix, root cause #4)

**Default: `commit_per_message`.**

Argument:

- The v0.1 default `group_commit_with_timer` claimed the peer's `ResendRequest` could "re-deliver" lost outbound frames if the peer received and acked them. That is **wrong** (Codex P1-6): peer ResendRequest can only ask *us* to resend, never re-deliver to us. If we transmitted, crashed, and lost the journal entry, we cannot satisfy the peer's resend ask. Default-loose durability with that justification misstated FIX recovery.
- v0.2 makes the **honest, conservative choice the default**: `commit_per_message` flushes on every record. Per-session throughput is ≈ 10⁴ frames/s on commodity NVMe (round-2 Codex C-R2-P2-2 close — single number aligned across §1.2, §4.3.1, §6.6) — adequate for production FIX deployments. Users who explicitly trade throughput for crash-loss accept that trade through `commit_batched(N)` or `commit_interval(ms)`.
- `[const §XV.4]` bans the QuickFIX "synchronous I/O on every send" pattern. v0.2's `commit_per_message` is NOT that pattern: the file I/O happens on `file_io_executor` (a dedicated thread pool — see §4.3.2), the session strand `co_await`s the completion. The session strand never blocks on `pwrite`/`fdatasync`. The QuickFIX banned pattern is "synchronous I/O on the session-thread"; this is asynchronous I/O with a forced flush after each write.

**Crash-survival semantics (§6.3 elaborates).** On unclean shutdown:

- `commit_per_message`: every successful `store()` return is durable. No data loss.
- `commit_batched(N)`: up to `N - 1` records may be lost.
- `commit_interval(ms)`: up to `ms` of records may be lost.

In all cases, on session restart, `FileStore` scans the append-only log and **truncates** the trailing partial record (CRC mismatch or short read). This makes torn-write detection automatic. The session FSM, on observing a `next_seqnum` lower than the peer expects on resume, may issue `SequenceReset-GapFill` (admitting the loss) or, if the operator authorises, a `ResetSeqNumFlag(141)=Y` Logon.

#### 4.3.2 Threading: file-I/O executor

**Decision: `FileStore::Config::file_io_executor` is required at construction.** The session strand `co_await`s a completion that runs on `file_io_executor`.

Argument:
- The session strand cannot block on `pwrite()` / `fdatasync()`; doing so freezes the FSM (heartbeat, parser, app callbacks).
- `EngineConfig` exposes a default `file_io_executor` (typically a 4-thread `asio::thread_pool` shared across all `FileStore`s in the engine).
- Cancellation per `[2d §6.5]`: total cancellation aborts the pending I/O at the next `file_io_executor` scheduling point.

**Saturation policy.** If `file_io_executor`'s queue saturates:
- `store()` awaitable suspends (block semantics on the session strand) — backpressure propagates to the wire receive.
- Drop is banned per `[const §XV.15]`.

This matches the `block` mode in `[2d §6.4]`.

### 4.4 `MessageStoreFactory` (N1: `unique_ptr` ownership)

```cpp
// include/fixpp/session/message_store_factory.hpp
namespace fixpp::session {

// Frozen-at-open factory consumed by SessionConfig::store_factory.
//
// Ownership shape (N1):
//   - Factory is held by SessionConfig as std::unique_ptr<MessageStoreFactory>.
//     [2d §4.5] v0.4 uses std::shared_ptr — flagged as a sibling-doc
//     inconsistency in §10 Q11 / Appendix D for 2d to amend at 2e sign-off.
//   - make() returns expected_t<std::unique_ptr<MessageStore>>. The Session
//     moves the unique_ptr into its private slot at Session::open per
//     [arch §5.6]. No shared_ptr; no sharing across two Sessions; no
//     re-binding mid-session.
//   - The C ABI's fixpp_store_t (owned by 2i) is a NON-OWNING observer of
//     the Session-owned store; closing the session destroys the store and
//     any held fixpp_store_t becomes invalid per [const §X.5].
class MessageStoreFactory {
public:
    virtual ~MessageStoreFactory() = default;

    // Construct a MessageStore for one session. Called once at Session::open
    // per [arch §5.6] / [2d §4.5]; called from the engine's open path
    // (NOT a hot-path method).
    //
    // Allocation: caller-supplied PMR resource for any factory-side scratch.
    // Persisted-frame storage in the minted MessageStore is the impl's own
    // resource (passed to the impl's Config).
    //
    // Errors: store_factory_failed (impl reports inability to construct,
    // e.g., FileStore directory unwritable; MemoryStore::Config exceeds
    // EngineConfig::max_store_memory_per_session per N9; CompID
    // filesystem-safety validation failure per §D.4 for FileStoreFactory;
    // both Config-supplied AND engine-threaded file_io_executor empty for
    // FileStoreFactory per §4.3.2).
    //
    // STORE-OBJECT ALLOCATION CONTRACT (v0.5 per §D.6 — Gap 3 close).
    // make()'s return type expected_t<std::unique_ptr<MessageStore>>
    // commits the v1.0 contract to std::default_delete<MessageStore>
    // destruction (the default unique_ptr deleter): the concrete store
    // object MUST be destructible via `delete static_cast<MessageStore*>(p)`.
    // Factory implementations that wish to use a PMR allocator for the
    // store object itself MUST wrap the deallocation into a
    // std::default_delete-compatible path (typical pattern: a static
    // operator delete overload on the concrete store class that routes
    // back to the PMR resource, paired with
    // std::pmr::polymorphic_allocator::new_object for the matching
    // allocation). A std::unique_ptr<MessageStore, CustomDeleter> return
    // type is NOT supported in v1.0 and is reserved for a possible
    // post-v1.0 evolution per [const §X.4]. The PMR resources discussed
    // in §6.1.1 / §8 / FR-026 / FR-027 govern the store's INTERNAL
    // storage (slab, ring, framing scratch, index, persisted-frame
    // copy) — they do NOT govern the deleter shape of the store object
    // itself. See Appendix D §D.6 for the normative bind.
    [[nodiscard]] virtual expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender_comp_id,
         std::string_view target_comp_id,
         std::pmr::memory_resource* mr,
         std::size_t max_store_memory_bytes,                 // engine-resolved EngineConfig::max_store_memory_per_session per N9 / [2e §1.2]
         asio::any_io_executor file_io_executor) noexcept = 0;  // engine-resolved EngineConfig::file_io_executor per §4.3.2:665 / :669
};

// Default factories shipped with v1.0 — thin wrappers over the
// MemoryStore and FileStore impls' Configs.
class MemoryStoreFactory final : public MessageStoreFactory {
public:
    explicit MemoryStoreFactory(MemoryStore::Config c = {}) noexcept;
    expected_t<std::unique_ptr<MessageStore>>
        make(std::string_view, std::string_view, std::pmr::memory_resource*,
             std::size_t, asio::any_io_executor) noexcept override;
};

class FileStoreFactory final : public MessageStoreFactory {
public:
    explicit FileStoreFactory(FileStore::Config c) noexcept;
    expected_t<std::unique_ptr<MessageStore>>
        make(std::string_view, std::string_view, std::pmr::memory_resource*,
             std::size_t, asio::any_io_executor) noexcept override;
};

}  // namespace fixpp::session
```

The factory's pure-virtual surface is one method (`make`), well under the `[const §XIV.2]` cap.

### 4.5 `retrieve_visitor` — awaitable per-frame visitor (root cause #2)

```cpp
// include/fixpp/session/retrieve_visitor.hpp
namespace fixpp::session {

enum class visit_result : std::uint8_t {
    cont  = 0,    // continue iteration to the next persisted frame.
    stop  = 1,    // stop iteration cleanly; retrieve() returns success.
    abort = 2,    // stop iteration with the visitor's error attached.
};

// Per-call callback shape — NOT a plugin interface; the [const §XIV.2]
// pure-virtual cap does not bind here (N11 / Codex P3-12). Defines
// EXACTLY ONE pure-virtual method (`on_frame`) plus ONE overridable
// virtual error hook (`abort_error`) — counts named precisely (Codex P3-12
// fix). The on_frame parameter list does NOT carry direction_t (N12 fix —
// dead parameter dropped; the visitor knows its own direction from the
// retrieve() call that constructed it).
//
// AWAITABLE return (root cause #2): on_frame returns
// awaitable<expected_t<visit_result>> so the visitor's per-frame work
// can suspend on its own async I/O (e.g., the FSM's resend transport
// write, or a sidecar audit-tee's downstream stream). The store keeps
// the frame_view stable across the visitor's awaitable suspension and
// resumption; cancellation flows through the visitor's awaitable
// per [2d §6.5].
class retrieve_visitor {
public:
    virtual ~retrieve_visitor() = default;

    // Called once per persisted frame in seqnum order.
    //
    // `frame` aliases store-internal storage stable across the visitor's
    // awaitable's suspension/resumption; the visitor MUST NOT retain the
    // span past the awaitable's completion. To keep bytes beyond
    // completion, copy into the visitor's own arena.
    //
    // Threading: invoked on the same strand the retrieve() awaitable was
    // awaited on (the session strand under default discipline); the store
    // rebinds completion to that strand if work was posted to a foreign
    // executor (FileStore's reads from file_io_executor).
    //
    // Mutex hold rule (root cause #2): the store does NOT hold its
    // per-instance writer mutex across the visitor's co_await. Any
    // recursive store-mutating call from on_frame's awaitable (e.g., a
    // tee chain that posts to another store) is guaranteed not to deadlock.
    [[nodiscard]] virtual asio::awaitable<expected_t<visit_result>>
    on_frame(seqnum_t seq,
             std::span<const std::byte> frame [[clang::lifetimebound]]) noexcept = 0;

    // Called once if the visitor's on_frame returned visit_result::abort;
    // lets the visitor surface its error code through retrieve()'s
    // expected_t<void>. The default impl returns store_visitor_aborted
    // (Codex P3-13 fix: the body is now valid C++; v0.1's
    // `return /* error::store_visitor_aborted */;` was a comment).
    // Visitors override to surface their own error.
    [[nodiscard]] virtual fixpp::core::error
    abort_error() noexcept {
        return fixpp::core::error{fixpp::core::error_code::store_visitor_aborted};
    }
};

}  // namespace fixpp::session
```

**Why awaitable visitor over `awaitable<retrieved_frame>` pull (root cause #2 choice).** Two viable shapes were considered:
- (a) `async_generator<frame_view>` — coroutine-pull pattern; consumer iterates with `for co_await`. Modern but couples to coroutine machinery the C-ABI / non-coroutine consumer (2i, SWIG/Python) can't reach.
- (b) **awaitable visitor** — `awaitable<visit_result> on_frame(seqnum_t, frame_view)`. The visitor's suspension is type-visible at the call site; survives translation to non-coroutine consumers (2i exposes the same awaitable shape over its callable-handle vtable; SWIG translates to Python's `async def`).

**Default: (b)** — smaller surface change from v0.1, survives C-ABI / SWIG translation. The visitor pattern stays "streaming, no bulk allocation" (the v0.1 vector-return concern); the per-frame awaitable is the right shape for the FSM's per-frame retransmit-vs-GapFill decision per `[FIX-SL §4.8.5]`.

### 4.6 `direction_t`

Defined in §4.1: `enum class direction_t : std::uint8_t { inbound = 0, outbound = 1 };`. Stable enum; values frozen in v1.0; future v-values reserve the open range per `[const §X.4]`.

### 4.7 `seqnum_t` — placeholder (root cause #3 / Codex P1-3 fix)

```cpp
// include/fixpp/session/seqnum.hpp
namespace fixpp::session {

// PLACEHOLDER ALIAS — owner = Phase-4 session-module spec (not yet drafted).
//
// The Phase-4 spec will publish the canonical type. Until that spec lands,
// 2e consumes this placeholder per [FIX-SL §4.1] (wire MsgSeqNum(34); 32-bit
// unsigned by observed-implementation convention — QuickFIX, fix8, OnixS).
//
// The Phase-4 spec MAY pick uint32 (matching this placeholder; observed
// convention) or uint64 (for never-reset use cases — see §10 Q9 +
// the N6 escalation). Until then, 2e binds at uint32 and surfaces
// store_seqnum_overflow on wrap (§6.7).
//
// CROSS-DOC HANDOFF: when the Phase-4 spec lands, this header either
// (a) re-exports the Phase-4 spec's typedef, or (b) is deleted and 2e's
// includes are repointed. Either is a single-line edit per [const §VI.5].
//
// v0.1 falsely cited [2d §4.5] as the owner — that section declares
// SessionConfig fields, not seqnum_t. Codex P1-3 / Opus root cause #3
// fix.
using seqnum_t = std::uint32_t;

inline constexpr seqnum_t seqnum_min = 1;            // FIX seqnums start at 1 per [FIX-SL §4.1].
inline constexpr seqnum_t seqnum_max = std::numeric_limits<seqnum_t>::max();

}  // namespace fixpp::session
```

### 4.8 QuickFIX-compat shim

#### 4.8.A Default — Path B (documented incompatibility)

**Verdict (v0.3 final): Path B only.** v1.0 ships **no** generic synchronous-`MessageStore`-to-async runtime adapter. The deliverable is the failure-mode list (§4.8.A.1) and the migration recipe (§4.8.A.3). The user explicitly authorised Path B as a v1.0 outcome in round 1; round-2 review (Codex C-R2-P2-1 escalated to P1 by Opus) caught that v0.2's §4.8.B Path A subset framed user-attested traits as compile-time-checked safety, which is misleading on a migration path. v0.3 retires the §4.8.B subset wrapper; see §4.8.B below for the rationale.

##### 4.8.A.1 Why an unconditional Path A is not feasible

Five hazards on the unconditional adapter (v0.1's table):

1. **User callback re-entry deadlock.** User's sync `set(...)` calls back into `Session` → session strand awaits worker thread → worker thread waits on session strand. Classic deadlock.
2. **Hidden `std::mutex`.** User's impl locks; relocates to worker thread; under load the mutex contends with itself if shared.
3. **Unbounded blocking.** User's sync `set()` may call `pwrite` / `fsync`. On a slow disk this blocks the worker thread for milliseconds-to-minutes.
4. **Mismatched threading model.** User's impl spawns its own threads; our adapter cannot join them at engine teardown.
5. **Cancellation token mismatch.** Sync impl has no cancellation slot; we either ignore cancellation (engine teardown hangs) or abandon the worker (resource leak).

These five compose. A "well-behaved" sync impl that satisfies **none** of (1)..(5) would be safe to wrap *if the four absences were enforceable at compile time against the wrapped type*. They are not (Codex C-R2-P2-1 / round 2): three of the four are user-asserted boolean traits (`reenters_session`, `performs_sync_disk_io`, `spawns_threads`), and the fourth (`polls_cancel_token_within`) is a behavioural deadline that no `static_assert` can verify on a synchronous implementation. v0.2 §4.8.B's framing as "compile-time diagnostic pointing at the violating clause" was misleading on a migration path; v0.3 retires the §4.8.B wrapper and the five hazards stand as the final answer for why v1.0 does not ship a generic sync-store adapter.

##### 4.8.A.2 What ships at default

- **No `<fixpp/session/quickfix_compat/message_store_adapter.hpp>` header.** Confirmed in the status block (N5 fix).
- **Migration documentation.** The book chapter `book/migration_from_quickfix.md` gains a "MessageStore migration" section (§4.8.A.3).
- **Optional config-file loader.** `<fixpp/session/quickfix_compat/cfg_loader.hpp>` ships a `cfg_to_file_store_factory(path)` reader — config translation only, NOT a runtime adapter.

##### 4.8.A.3 1-page migration recipe

For users currently on QuickFIX with a custom `MessageStore`:

| QuickFIX call | fixpp equivalent |
|---|---|
| `quickfix::MessageStore::set(seq, body)` | `co_await store->store(seq, std::as_bytes(std::span{body}), direction_t::outbound)` (or `inbound`) |
| `quickfix::MessageStore::get(begin, end, vec)` | `co_await store->retrieve(begin, end, dir, my_visitor)` where `my_visitor::on_frame` is `co_await`-able |
| `quickfix::MessageStore::getNextSenderMsgSeqNum()` | `co_await store->next_seqnum(direction_t::outbound, /*increment=*/false)` |
| `quickfix::MessageStore::incrNextSenderMsgSeqNum()` | `co_await store->next_seqnum(direction_t::outbound, /*increment=*/true)` |
| `quickfix::MessageStore::reset()` | `co_await store->reset()` |
| `quickfix::FileStore::flush()` | (no equivalent on the public `MessageStore`; `FileStore` exposes `flush_for_session_close()` consumed only by Session — see §4.1.1, §7.6) |

Migration steps:
1. Inherit from `fixpp::session::MessageStore` instead of `quickfix::MessageStore`.
2. Convert each method body to an `awaitable<expected_t<...>>` returning `co_return`.
3. If the original impl held an `std::mutex`, replace with `fixpp::sync::async_mutex` per `[const §XI.3]`.
4. If the original impl performed sync `pwrite` / `fsync`, post that work to a dedicated executor per the §4.3.2 pattern.
5. Construct a `MessageStoreFactory` subclass that returns the new impl from `make(...)` (returning `unique_ptr` per N1); pass to `SessionConfig::store_factory`.

#### 4.8.B Path A subset — RETIRED in v0.3 (round-2 close per Codex C-R2-P2-1 escalated to P1)

**v0.3 retires the v0.2 `quickfix_compat::sync_message_store_adapter<UserSync, AdapterPolicy>` wrapper entirely.** The header `<fixpp/session/quickfix_compat/sync_message_store_adapter.hpp>` is **NOT** shipped in v1.0; the `static_assert` / concept chain, the `adapter_traits<Sync>` user-asserted boolean trio, the `store_shim_timeout` error variant, and the §9 seam #12 ("Path A subset acceptance test") are all retired.

**Why retired (round-2 rationale).** Codex round 2 P2-1 (escalated by Opus to P1 in the round-2 review) caught a real overclaim in v0.2's §4.8.B framing: the four "compile-time-checked" conditions — `reenters_session`, `performs_sync_disk_io`, `spawns_threads`, `polls_cancel_token_within` — are *user-attested traits*, not statically checkable properties of the wrapped sync impl. A user who writes

```cpp
template<> struct adapter_traits<MyStore> {
    static constexpr bool reenters_session     = false;
    static constexpr bool performs_sync_disk_io = false;
    static constexpr bool spawns_threads        = false;
};
```

and then has `MyStore::set(...)` call `Session::send(...)`, perform `pwrite`/`fsync`, or spawn a thread, **compiles cleanly** and hits the runtime deadlock / blocking-I/O / leaked-thread the four-condition contract was supposed to prevent. The v0.2 §4.8.B claim "compile-time diagnostic pointing at the violating clause" fires only when the user *correctly* declares their store unsafe — i.e., it catches the cases that don't matter. The v0.2 supported-impl list was also wrong: the QuickFIX/J JDBC-stub `MessageStore` performs synchronous JDBC blocking calls (Hazard 3 by Codex's own §4.8.A.1 list), so a user attesting `performs_sync_disk_io = false` against the JDBC stub produces a compiling-but-runtime-unsafe adapter shipped under a "curated supported impls" label.

**The user explicitly authorised Path B as a v1.0 outcome in round 1.** The round-1 N4 finding ("Path A subset is shippable") was Opus-introduced; round 2 rolls it back. v1.0 ships **Path B only**: documented incompatibility (§4.8.A above) + the migration recipe (§4.8.A.3) + the optional config-file translator (`<fixpp/session/quickfix_compat/cfg_loader.hpp>` per §4.8.A.2). The five hazards in §4.8.A.1 stand as the "why we don't ship a generic sync-store adapter" justification — they are exactly the runtime-safety gaps that `static_assert`/concepts cannot close on user-attested traits, and the round-2 review caught that v0.2's §4.8.B framing inverted the burden of proof by labelling user-attestation as compile-time enforcement.

**Migration path for users who wrote the v0.2 §4.8.B adapter.** None — v0.2 was a draft; no v0.2 user surface was published. Internal callers fall back to §4.8.A.3's migration recipe (port the sync impl to `awaitable<expected_t<...>>` and inherit `MessageStore` directly).

**What v0.3 ships at default (unchanged from v0.2 §4.8.A).** No `<fixpp/session/quickfix_compat/message_store_adapter.hpp>`. No `<fixpp/session/quickfix_compat/sync_message_store_adapter.hpp>`. Migration documentation in `book/migration_from_quickfix.md` (per §4.8.A.3). Optional config-file loader at `<fixpp/session/quickfix_compat/cfg_loader.hpp>` (config translation only, NOT a runtime adapter).

The §9 seams retire seam #12 ("Path A subset acceptance test"). §6.7 retires `store_shim_timeout`. §11 hand-off retires the `quickfix_compat::sync_message_store_adapter` artefact reference.

## 5. Public C ABI — none (delegated to 2i)

Per `[arch §4.10]` and the brief: C ABI for `MessageStore` (and the `fixpp_store_t` opaque handle if 2i pursues a callable handle) is owned by **2i**; this doc only fixes the C++ surface. The `fixpp_store_t` opaque handle is a **non-owning observer** of the Session-owned store per N1; on `Session::close` the handle becomes invalid per `[const §X.5]` reentrancy rules. The `direction_t`, `seqnum_t`, and per-method error-code mapping (per §6.7) are 2i's call against the per-doc-prefix discipline `FIXPP_ERR_STORE_*`.

## 6. Behavioural contract

### 6.1 Allocation, exceptions, threading, cancellation

#### 6.1.1 Allocation on the dispatch hot path

- **Hot path (corrected per root cause #1).** Inbound: between Parser success and the application callback `fromApp`/`fromAdmin` — the FSM stores the inbound frame after validate, before dispatch. Outbound: between `wire::Writer::commit()` success and `transport::async_write` — the FSM stores the committed frame after `Writer::commit` produces the post-commit span, before transmit. Both windows are `[const §VIII.5]` zero-global-`new`/`delete` discipline.
- `store(...)`'s per-call scratch comes from `SessionConfig::session_arena` per `[2d §4.5]`; the persisted-byte buffers are pre-allocated in the store-owned `store_arena` per §8.
- **`MemoryStore::store` performs zero allocator calls (N9 / Codex P2-9).** Fixed-slot + fixed-slab layout per §4.2; the slab is allocated in one PMR call at construction. `store()` is memcpy + index increment.
- **§6.1.1 governs INTERNAL-storage allocation on the dispatch hot path (v0.5 per §D.6 clarification — Gap 3 close).** The store object's **own** allocation/deallocation is governed by Appendix D §D.6 (`std::default_delete<MessageStore>`-compatible per the `unique_ptr<MessageStore>` return type of `MessageStoreFactory::make()`), NOT by the PMR resources discussed here. The PMR threading discussed in this section governs only the slab / ring / framing-scratch / index allocations *inside* the minted store, not the store object's own allocation.

#### 6.1.2 Exceptions (round-1 N13; round-2 Opus N2-P2-1 refinement)

All public surface is `noexcept`. PMR allocation throw paths route through `fixpp::core::detail::trap_throw` per `[2a §4.2]` (the `trap_throw` pattern at `[2a §4.2]` is **the** no-terminate-on-PMR-throw mechanism; allocator policy on the hot path follows `[const §VIII.5]`'s zero-global-heap rule, but the no-terminate behaviour itself is owned by `[arch §5.3]`'s `expected_t<T>` error model + the `[2a §4.2]` operationalisation, not by `[const §VIII.5]` directly — Opus round-2 N2-P2-1 close on a citation overshoot). The converted error surfaces as `expected_t::unexpected{store_io_failure}` (FileStore disk error) or `expected_t::unexpected{store_capacity_exhausted}` (MemoryStore ring full under `bounded` policy; under `unbounded` policy the throw is the only failure path and surfaces as `store_io_failure`). v0.1's `[const §VIII.4]` citation was wrong (`[const §VIII.4]` is "v1.0 perf targets" — N13's round-1 correction stands; round 2 only refines the no-terminate citation to `[2a §4.2]` + `[arch §5.3]` rather than overloading `[const §VIII.5]` with a meaning it does not carry).

#### 6.1.3 Threading

Per `[2d §7.3]`: store ops are invoked on the session strand (the `session_executor` wrapper per `[2d §4.8]`); completion handlers rebind to the same strand. `MemoryStore` does its work on the session strand and returns synchronously; `FileStore` posts to `file_io_executor` and rebinds to the session strand on completion (per `[2d §6.5]`'s `cancellable_dispatch` handoff shape).

#### 6.1.4 Cancellation result contract per method (root cause #1 / Codex C-P2-8 escalated to P1)

Cancellation flows through ASIO native slots per `[const §XI.2]`. v0.1 left the **completion shape ambiguous** — exception or `expected_t::unexpected`. v0.2 makes it explicit, per method, mirroring `[2d §6.5]`'s `cancellable_dispatch → awaitable<expected_t<void>>` precedent (which already routes the cancellation outcome `dispatch_aborted` into `expected_t`).

**Why the post-store-pre-write taxonomy (round-3 post-cap pin per Codex C-R3-P3-1).** The post-store-pre-write cancellation that persists is the durably-stored shape that protects `[const §XV.15]` (no silent loss of session messages): if `store()` linearises before `transport::async_write` and the latter is then cancelled, the frame is durably journaled and the peer's later `ResendRequest` is honourable. A `durable_after_transmit` mode where `store(outbound)` runs *after* `transport::async_write` success is **out-of-scope** for v1.0 — it would create a worse partial-write hole (a partial wire write whose tail bytes are dropped before `async_write` completes leaves the peer with a frame for which we have no journal entry; on reconnect the peer can `ResendRequest` a seqnum we cannot honour). See Appendix C round-2 entry, finding C-R2-P1-1, for the disagreement reasoning.

| Method | Cancellation wins **before** linearisation point | Cancellation wins **after** linearisation point | Durability implication of the after-linearisation branch |
|---|---|---|---|
| `store(seq, frame, dir)` | `expected_t::unexpected{store_cancelled}`. **No persistence**. **Outbound:** the FSM has not yet called `transport::async_write`, so no transmission happened — the cancellation is benign per root cause #1. **Inbound:** the FSM has not yet called `fromApp/fromAdmin`, frame is dropped. | Frame persisted. Awaitable resumes normally with `expected_t<void>{}`. The outbound `transport::async_write` path proceeds; if it is in turn cancelled, the standard ResendRequest recovery applies. | Frame **DURABLE** on disk per §6.3 algorithm; in-memory state **STABLE**; recovery contract **HONOURED**. `store_cancelled` is **NOT** surfaced — the frame did persist. |
| `retrieve(begin, end, dir, visitor)` | `expected_t::unexpected{store_cancelled}` before any visitor invocation. | Visitor invocation in progress: cancellation propagates through the visitor's awaitable per root cause #2; the next visitor frame is not invoked; `retrieve()` completes with `store_cancelled`. | n/a — read path; no durability transition. |
| `next_seqnum(dir, increment)` | `expected_t::unexpected{store_cancelled}`. **Counter not advanced**. | Counter advanced (if `increment == true`); awaitable resumes with the value. | Counter **DURABLE** on disk under `commit_per_message` per §6.3 algorithm (FileStore: counter-record `pwrite` + flush); recovery on next open observes the advanced counter. |
| `reset()` | `expected_t::unexpected{store_cancelled}`. **No state change**. | Reset already linearised; awaitable resumes with `expected_t<void>{}`. | Reset state **DURABLE** (FileStore: linearisation point IS the `rename` per §6.3.4; durability primitive per §6.3.5 — Linux mandatory parent-dir fsync, Windows mandatory `MOVEFILE_WRITE_THROUGH`); recovery on next open observes the new live log. |

The linearisation point for each method is precisely defined:
- `MemoryStore::store`: the entry-array index increment after the slab memcpy (under the writer mutex).
- `FileStore::store`: the successful return of the OS-level flush call (`fdatasync` / `FlushFileBuffers`) under `commit_per_message`; the append's successful `pwrite` under batched policies (both under the writer mutex).
- `MemoryStore::next_seqnum(_, true)`: the counter increment under the writer mutex (Opus N2-P2-2 close: the v0.2 "atomic fetch-add" wording is dropped — the mutex is the serialisation primitive per §6.4).
- `FileStore::next_seqnum(_, true)`: the counter-record `pwrite` + flush under the writer mutex (matches `store`'s linearisation on the counter record).
- `MemoryStore::reset`: the entry-array zero pass under the writer mutex.
- `FileStore::reset`: the **`rename`** of `<...>.log.reset.tmp` over `<...>.log` under the writer mutex (§6.3.4 round-2 atomic-rename algorithm; v0.2's "truncate-then-flush sentinel write" is replaced).
- `retrieve`: per-frame; each visitor invocation has its own linearisation boundary (the visitor's awaitable resumes).

**Linearisation-as-durability footnote (round-3 post-cap pin per Opus N3-P3-1).** "Linearisation point" for `FileStore` methods means "the point at which the on-disk state per §6.3 transitions to the new value AND the durability primitive (`fdatasync` / `FlushFileBuffers` for write paths; the `rename` plus the §6.3.5 durability primitive for `reset()`) has returned success"; cancellation that wins **after** that point cannot un-do durability — the new state is on disk and survives a host crash. The fourth column on the cancellation table above surfaces this implication explicitly so a Phase-4 FSM author writing the recovery path does not have to compose the linearisation-point list with the §6.3 durability contract by hand.

**`store_cancelled` is added to §6.7.** Per `[const §XI.2]`, the C-ABI mapping reuses `FIXPP_ERR_CANCELLED` (joining `[2d §6.7]`'s `dispatch_aborted` and `clock_sleeps_cancelled` in the cancellation group).

**Idempotency under cancellation (round-1 root cause #4 + round-2 root cause #1 ties).** `FileStore` treats partial writes at the tail as torn writes and truncates on next open per §6.3.2; `FileStore::reset()` is atomic at the `rename` linearisation point per §6.3.4 (a crash before the rename is recoverable as the old log; a crash after the rename is the new sentinel-only log; nothing in between is observable); `MemoryStore` does not insert into its index until the slab memcpy and entry write are complete (atomicity by construction).

### 6.2 Lifetime contract — the input span (incl. shutdown ordering, N7)

The `frame` parameter to `store(...)` aliases caller-owned bytes. Per `[2b §6.4]`, those bytes are part of the wire-side per-message arena, which is reset by the session FSM after `fromApp` returns. The store's awaitable may suspend; during that suspension the per-message arena is still alive — but the contract of the wire-side arena does not guarantee that, so:

- the implementation MUST take a deep copy of `frame` into `store_arena` **before the awaitable's first suspension point**.
- the input span lifetime is bounded by the *immediate* (un-suspended) part of the call.

Visitor span (root cause #2 / §4.5): the `frame` parameter to `retrieve_visitor::on_frame` aliases store-internal storage stable across the visitor's awaitable's suspension/resumption; the visitor MUST NOT retain it past the awaitable's completion. The store does NOT hold the writer mutex across the visitor's `co_await`, so a recursive store-mutating call from the visitor's awaitable is guaranteed not to deadlock (mid-traversal mutation handling is described in §4.1 method 2).

#### 6.2.1 Session shutdown ordering (N7)

Lifetime ordering at session destruction (graceful or terminal):

1. **`Session::close(graceful)`** — phase 1 awaits in-flight ops including pending `store()` / `retrieve()` / `next_seqnum()` / `reset()`. Phase 1's child cancellation state per `[2d §4.7]` fires `cancellation_type::total` if the timeout expires; in-flight ops complete with `store_cancelled`. **The engine-internal `FileStore::flush_for_session_close()` hook is NOT in the cancellable in-flight set** (round-3 post-cap pin per Codex C-R3-P1-3): it is the durability seam honouring `[FIX-SL §4.5]` graceful-Logout durability and **runs to completion** outside phase 1's child timeout — it is awaited after ordinary store ops drain and before the Logout `async_write` is issued; it completes with `expected_t<void>{}` on success or `expected_t::unexpected{store_io_failure}` on a mid-flush `fdatasync`/`FlushFileBuffers` error; it does **NOT** surface `store_cancelled` under graceful Logout. The cancellation case is forced shutdown (`Session::close(terminal)` per step 2 / `cancellation_type::total` per `[2d §6.5]`), under which the hook is short-circuited and may leave the on-disk log in an unflushed state — recovery on next session open truncates the last incomplete record per the §6.3.4 startup contract; the user accepts that data-loss window. (See §4.3 `flush_for_session_close()` block-comment + Appendix D §D.2 for matching wording.)
2. **`Session::close(terminal)`** — phase 1 is skipped; root cancellation fires immediately. The Session **joins** all in-flight store awaitables before destroying the store: cancel → drain → destroy `unique_ptr<MessageStore>` → release `session_arena` → `~Session`.
3. **Cancellation completion handlers run before `session_arena` is released.** This is enforced by step 2's drain ordering: no completion handler observes a destroyed `session_arena`. (UAF risk under TSan + ASan validated by §9 seam **"Session shutdown ordering test"**.)
4. **`MessageStore` MUST NOT outlive its owning `Session`** (type-level fix from N1: `unique_ptr` ownership; no `shared_ptr` admits the v0.1 lifetime hole).

The visitor span on the retrieve path is bounded by the visitor call's awaitable, not the `retrieve()`'s awaitable — even if the session is closing mid-replay, the visitor's awaitable is the inner one and resolves first.

### 6.3 FileStore on-disk algorithm — single append-only log per session (round-1 root cause #4 + round-2 root cause #1)

**v0.3 collapses v0.2's two-file-per-direction shape down to a single log file per session** (round-2 root cause #1 close per Codex C-R2-P1-2). The atomicity arguments in §6.3.4 truncate-then-flush + the cross-direction reset are now real algorithms operating on a single file; v0.2's "two log files but truncate-the-log singular" pathology — where a crash after `ftruncate(outbound.log)` + flush but before `ftruncate(inbound.log)` would land a mixed-state restart — is no longer reachable because there is only one log to truncate. The new scheme:

- **One log file per session, both directions interleaved** (round-2 fix).
- Detects torn writes automatically via per-record CRC32.
- Makes `reset()` atomic via **rename** of a freshly-prepared replacement log, with **mandatory** durability primitives on both platforms (round-3 post-cap pin per Codex C-R3-P1-2): the truncate-in-place + sentinel-write sequence v0.2 used had a non-atomic window between truncate-success and sentinel-write-success; v0.3 prepares the new log under a `.reset.tmp` name, fsyncs it, then atomically renames over the live log. **Linux: `rename(2)` is atomic on the same filesystem per POSIX; durability of the rename requires a parent-directory `fsync` after the `rename` call returns success — MANDATORY (not optional, not documentation) per the POSIX `rename(2)` man page. Windows: `MoveFileExW(old, new, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` — MANDATORY for durable rename; NTFS metadata journaling alone does NOT durably commit the rename until the user-mode write-through flag is set, so plain `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` without `MOVEFILE_WRITE_THROUGH` is insufficient.** `reset()` may complete successfully only after the platform durability primitive has returned success; v0.3's "no directory-fsync primitive" / "optional Linux dir-fsync" / "Windows rename is durable on success" wording is retired.
- Makes `commit_per_message` durable via `fdatasync`/`FlushFileBuffers` on the live log file handle.

**Cross-filesystem note.** `<...>.log.reset.tmp` is in the same directory as the live log so `rename` cannot fail with cross-device `EXDEV`; a future implementation that admits an external temp directory MUST reject cross-filesystem paths instead of falling back to copy.

#### 6.3.1 On-disk format

**One file per session** (round-2 fix per Codex C-R2-P1-2 — collapsed from v0.2's two files per direction):

- `<sender>__<target>.log` — append-only log of records. Each record is **`[record_kind(1) | dir(1) | reserved(2) | seq(4) | len(4) | crc32(4) | bytes(len) | padding-to-8-byte-align]`** (record header is 16 bytes; `crc32` is over `record_kind + dir + reserved + seq + len + bytes`).
  - `record_kind ∈ { frame = 0, counters = 1, sentinel = 2 }`. Frame records carry the persisted FIX bytes; counters records carry `{ next_inbound, next_outbound }` advance markers; the sentinel record is the first record in any well-formed log and carries `{ magic | version | session_triple_hash | crc32 }` per §10 Q7.
  - `dir ∈ { inbound = 0, outbound = 1, n_a = 2 }`. Frame records use `inbound`/`outbound`; counters and sentinel records use `n_a`.
- A small in-memory index built at startup by scanning the log; not persisted as a separate file (rebuilt on every restart, ≤ ms for typical FIX session sizes). The index has two seqnum→offset maps, one per direction, populated by walking the frame records and discriminating on the per-record `dir` field.
- The counters live as records of kind `counters` interleaved into the same log, written under the same writer mutex, durable under the same per-policy flush. There is **no separate counters file**; v0.2's "documentation aid" framing was load-bearing for the round-1 truncate-then-flush atomicity claim because the v0.2 §6.3.4 algorithm assumed there was exactly one file to truncate. Round 2 makes that assumption real.

#### 6.3.2 Open / restart algorithm

1. **Stale-tmp cleanup.** If `<sender>__<target>.log.reset.tmp` exists from a crashed reset (see §6.3.4), unlink it. The live log is the source of truth; a partial `.reset.tmp` is discarded.
2. Open the live log file (create with the sentinel record if absent).
3. Verify the sentinel record; if invalid → return `store_factory_failed` (the directory holds a foreign file or a corrupted earlier session; recovery is operator's choice).
4. **Scan from after the sentinel.** For each record: check `len` against `max_frame_bytes`; check `crc32`; on the first failure (CRC mismatch, short read, or `len > max_frame_bytes`) → **truncate** the log to that offset (`ftruncate` on Linux, `SetEndOfFile` on Windows) and `fdatasync`/`FlushFileBuffers`. The torn record is the first observation point of the crash.
5. Rebuild the per-direction in-memory index from the valid `frame` records.
6. The counters' last durable values are read from the most-recent `counters` record in the log (or `next_inbound = next_outbound = 1` if no `counters` record has been written since the sentinel).

**Property:** at any restart point, the log is either intact (every CRC valid) or it ends in exactly one torn record that the restart truncates. There is no "outbound truncated + inbound intact + counters mixed" pathology that v0.2's two-file shape allowed (round-2 fix per Codex C-R2-P1-2).

#### 6.3.3 `store(seq, frame, dir)` algorithm

1. Acquire writer mutex (per §6.4).
2. Verify `seq == next_seqnum(dir, false)` (per §4.1 method 1 docstring + Opus N2-P2-3); on mismatch, release mutex and return `store_seqnum_out_of_order`.
3. Compute CRC32; assemble record header (`record_kind = frame`, `dir = inbound|outbound`) + body in a `store_arena`-allocated framing buffer (≤ `record_header_size + max_frame_bytes` bytes; one buffer per concurrent in-flight store, capped at 1 since the writer mutex serialises).
4. `pwrite` the record at the log's tail offset.
5. Per policy:
   - `commit_per_message`: `fdatasync` (Linux) / `FlushFileBuffers` (Windows) — record now durable; awaitable completes with success.
   - `commit_batched(N)`: increment batch counter; if reached N, flush; record durable on flush success.
   - `commit_interval(ms)`: leave the flush to the timer-fired worker; `store()` completes with success on `pwrite` success only (so the durability semantics here are "durable within `ms` of the awaitable resumption").
6. Update in-memory index entry for `(dir, seq) → offset`.
7. Release writer mutex.

#### 6.3.4 `reset()` algorithm — atomic rename (round-2 close on Codex C-R2-P1-2)

1. Acquire writer mutex.
2. **Prepare a replacement log under a temporary name.** Open `<sender>__<target>.log.reset.tmp` for write (create-or-truncate). Write a fresh sentinel record (matching the live log's `session_triple_hash` and `magic`) followed by a fresh `counters` record with `next_inbound = next_outbound = 1`.
3. `fdatasync` (Linux) / `FlushFileBuffers` (Windows) on the temp file's handle.
4. **Atomically rename `<...>.log.reset.tmp` → `<...>.log`** with the platform's durable-rename primitive. Linux: `rename(old, new)` (POSIX-atomic on the same filesystem). Windows: **`MoveFileExW(old, new, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`** — `MOVEFILE_WRITE_THROUGH` is **MANDATORY** for durable rename per Win32 `MoveFileExW` semantics (round-3 post-cap pin per Codex C-R3-P1-2; NTFS metadata journaling alone does NOT durably commit the rename without the user-mode write-through flag). This is the linearisation point.
5. **Linux only: `fsync` the parent directory** (`fsync(open(dirname(path), O_RDONLY | O_DIRECTORY))`) — **MANDATORY** for durability of the rename per the POSIX `rename(2)` man page (round-3 post-cap pin per Codex C-R3-P1-2; without the parent-directory `fsync`, a host crash can drop the directory entry update even though the data file's contents are durable, leaving the next open seeing the *old* log despite `reset()` having returned success). On Windows the durable-rename primitive is `MOVEFILE_WRITE_THROUGH` on the `MoveFileExW` call in step 4; no separate dir-fsync is needed because Windows' filesystem stack does not have a separate directory file.
6. Replace the engine-held file handle with one opened on the new live log; close the old handle.
7. Reset the in-memory index to empty.
8. Release writer mutex.

**Property (round-2 fix; round-3 post-cap durability pin per Codex C-R3-P1-2):** the linearisation point is the `rename` call PLUS the platform durability primitive (Linux: parent-dir `fsync` mandatory; Windows: `MOVEFILE_WRITE_THROUGH` mandatory on the `MoveFileExW` call). `reset()` returns success only after the durability primitive has returned success. At any restart point:
- Crash before step 4 → `<...>.log.reset.tmp` may exist (will be cleaned by §6.3.2 step 1) but the live log is unchanged → restart sees the old intact log → `reset()` reports failure to the FSM (the awaitable did not complete normally) → the FSM retries on next session open.
- Crash between step 4 and step 5 (Linux only — Windows pins durability in step 4 via `MOVEFILE_WRITE_THROUGH`) → `rename` was atomic, but the parent-dir `fsync` did not complete → on a worst-case crash the directory entry update is lost and the next open sees the old log → `reset()` did NOT return success to the FSM (the awaitable was not yet resumed) → the FSM retries on next session open. There is **no observable** "reset returned success but the next open sees the old log" pathology, because reset cannot return success until step 5 has completed.
- Crash after step 5 → fully reset state.

There is no observable "old frames + new counters" or "outbound-truncated + inbound-intact" intermediate state from the restart's perspective.

#### 6.3.5 Platform-portability subsection

| Primitive | Linux | Windows |
|---|---|---|
| Append `pwrite` | `pwrite(fd, buf, len, offset)` | `WriteFile(handle, buf, len, &n, &overlapped)` with `overlapped.Offset` |
| Force durability | `fdatasync(fd)` | `FlushFileBuffers(handle)` |
| Truncate at offset (used at restart for torn-record cleanup; NOT used by `reset()` per §6.3.4) | `ftruncate(fd, off)` | `SetFilePointerEx(handle, off, …, FILE_BEGIN)` + `SetEndOfFile(handle)` |
| Atomic rename — atomicity primitive (load-bearing for `reset()` per §6.3.4 step 4) | `rename(old, new)` (POSIX-atomic on the same filesystem; `renameat2(old, new, RENAME_EXCHANGE)` is an alternative for atomic exchange but not used here — rename-over-existing is the v0.3 contract) | `MoveFileExW(old, new, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` (the `MOVEFILE_WRITE_THROUGH` flag is the Windows durability primitive; see next row) |
| Atomic rename — durability primitive (load-bearing for **durability** of the rename per §6.3.4 step 5; **MANDATORY** on both platforms — round-3 post-cap pin per Codex C-R3-P1-2) | `fsync(open(dirname(path), O_RDONLY \| O_DIRECTORY))` after the `rename` call returns success — **MANDATORY** per POSIX `rename(2)` man page; `reset()` may not return success until this fsync returns | covered by `MOVEFILE_WRITE_THROUGH` on the `MoveFileExW` call in step 4 — **MANDATORY** per Win32 `MoveFileExW` semantics; NTFS metadata journaling alone does NOT durably commit the rename without the user-mode write-through flag |
| Advisory lock | `flock(fd, LOCK_EX)` | `LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK, …)` |

**Atomicity vs durability of the rename (round-3 post-cap rewrite per Codex C-R3-P1-2).** The `rename` syscall is atomic on the same filesystem on both Linux and Windows: at any crash point during/after `rename`, either the old name is the live log or the new name is the live log; never a half-renamed state. **Durability** of the rename — i.e., "after a crash, will the rename still be visible?" — is **not** automatic on either platform: on Linux, the parent-directory `fsync` after the rename is **mandatory** per the POSIX `rename(2)` man page; on Windows, the `MOVEFILE_WRITE_THROUGH` flag on the `MoveFileExW` call is **mandatory** per Win32 semantics (NTFS metadata journaling commits the rename to the journal but the journal itself is buffered, so the rename's durability boundary moves to whichever forcing function user-mode supplies — `MOVEFILE_WRITE_THROUGH` is that function). The §6.3.4 algorithm above is correct for both atomicity and durability on both platforms; the v0.3 "no directory-fsync primitive" / "optional Linux dir-fsync" / "Windows rename is durable on NTFS journaling alone" wording was a documentation defect and is retired. The `reset()` durability contract per §4.1 method 4 ("either both counters are 1 AND no frames are persisted, OR the operation reports failure and the prior state is intact") binds: success-return implies durability, which requires the platform durability primitive to have returned success first.

§9 seam **"FileStore Windows crash-survival"** runs the §9 #2 / #3 / #10 corpus on Windows / MSVC.

**Scope & trust — filesystem-type contract for advisory-lock honoring (NEW v0.5 per Appendix D §D.5 — Gap 2 close; resolves the §10 Q4 "directory contention" closure's reliance on cross-host lock semantics).** `FileStore` is supported only on filesystems where the platform advisory-lock primitive above (Linux: `flock(2)`; Windows: `LockFileEx`) provides effective cross-process exclusive-lock semantics **for every host that may open the live log path**. **Behaviour on filesystems that do not honour those semantics — including but not limited to NFS (any version without an active and correctly-configured lock manager — `rpc.statd` + `rpc.lockd` on Linux; equivalent on other Unixes), SMB / CIFS, FUSE-mounted network filesystems, and cluster filesystems (GPFS, Lustre, GFS2, OCFS2) — is unsupported and outside the v1.0 correctness contract.** `FileStoreFactory::make()` does NOT detect or warn on such deployments (the probe-is-worse-than-nothing argument: a `flock`-then-`flock`-on-a-second-fd same-host probe does not prove multi-host correctness; a `statfs(2)` / `GetVolumeInformationW` filesystem-type probe is non-portable and incomplete — NFSv4 over Kerberos may be fine; NFSv3 over UDP without `lockd` is broken; same `f_type = NFS_SUPER_MAGIC`); operators who deploy on shared storage MUST verify cross-host lock semantics out of band and attest correctness as a deployment precondition. The §10 Q4 single-writer contract holds **only** under this scope restriction. A future v1.x feature may add an opt-in `EngineConfig::store_filesystem_attested = true` flag, but that is post-v1.0 (`[const §X.4]` reserved range). See Appendix D §D.5 for the normative bind and `[2e §6.7]` `store_io_failure` row for the observability propagation (operator note on shared-FS CRC mismatches under N-2).

**Scope & trust — CompID filesystem-safety on `<sender>__<target>.log` (NEW v0.5 per Appendix D §D.4 — Gap 1 close).** `FileStoreFactory::make()` MUST validate the effective `sender` / `target` CompID values before composing `<directory>/<sender>__<target>.log` and before any file is opened or advisory lock taken — rejecting empty CompIDs, path separators, NUL bytes, `.` / `..` segments, control characters `[0x00, 0x1F]` / `0x7F`, and CompIDs that would produce a path component exceeding `NAME_MAX` — with `expected_t::unexpected{store_factory_failed}`. FIX-SL §4.3 admits ASCII printables in Tag 49 / Tag 56; the FIX protocol does NOT itself constrain CompIDs to alphanumeric, so the validation closes a real directory-traversal hazard rather than a hypothetical one. The same validation is mirrored at `fixpp::session::quickfix_compat::cfg_loader::cfg_to_file_store_factory()` as defense in depth. See Appendix D §D.4 for the normative bind and the `noexcept`-safe validation primitive list (`std::string_view::find_first_of` / `find`; `std::filesystem::path` constructors NOT invoked until validation passes).

### 6.4 Store-write mutex contract per `[SYN §3.2 Q8]` (round-1 root cause #3; round-2 vocabulary alignment per Opus N2-P3-2)

Every `MessageStore` impl carries one per-instance `fixpp::sync::async_mutex` (per `[const §XI.3]`) — the **writer mutex**. **All four methods** (`store`, `retrieve`, `next_seqnum`, `reset`) acquire it (Opus round-2 N2-P2-2: `next_seqnum` is mutexed, the v0.2 "atomic fetch-add" wording is dropped — the mutex is the serialisation primitive). There is **no read lock**, **no `async_shared_mutex`**, **no `std::recursive_mutex` adapter**:

- `[SYN §3.2 Q6b]` puts `async_shared_mutex` out of v1.0.
- `[const §XV.9]` bans `std::mutex` in coroutine context — no transitional carve-out.
- Under v1.0 single-session-serialisation-domain discipline (per `[2d §4.8]` round-3 root cause #1: covers BOTH `per_session_strand` and `direct_executor` modes), contention on the exclusive mutex is zero. The RW optimisation buys nothing while leaning on a primitive 2f does not own.

The mutex stays a `fixpp::sync::async_mutex` regardless of `SessionConfig::lock_policy` per `[const §XI.5]`.

**Concurrent-arrival shape (Codex P1-5 fix; Opus round-2 N2-P3-2 vocabulary alignment).** If two callers attempt a mutating method on the same instance concurrently from outside the session serialisation domain — a v1.0-invariant violation under both `per_session_strand` and `direct_executor` modes — the second arrival **suspends FIFO-fairly on the async_mutex** and proceeds when the first releases. The store **does NOT return `store_concurrent_writer`**. The Session-layer debug-build assert per `[2d §6.1]` is the front-line check; the store mutex is defence in depth (the §4.2 line "defence-in-depth against session-serialisation-domain violations" matches this wording — Opus N2-P3-2 close). The v0.1 `store_concurrent_writer` error variant is REMOVED from §6.7.

Special note on `retrieve` (round-1 root cause #2): the writer mutex IS acquired at entry to validate `begin`/`end` and to take a snapshot of the index, but it is **NOT held across the visitor's `co_await`**. If the writer mutex were held across the visitor, a recursive store-mutating call from the visitor's awaitable (e.g., a tee-chain audit visitor that posts to another store on the same session) would deadlock. The store releases the mutex before invoking `on_frame` and re-acquires for any per-frame state read; if mid-traversal mutation is detected (the index advanced past the tracked iterator), the next visitor call sees the new state without UB (frames already visited are not re-visited; the iteration stops at the original `end` even if new frames appeared during traversal).

### 6.5 Clock and timestamps

Per `[2d §7.9]`'s effective-clock rule: if a store impl wants to stamp persistence times alongside frames, it reads `effective_clock` (resolved per-session) via the `Session*` recovered from the project-owned `session_executor` wrapper class's `session_ptr()` member-function accessor (per `[2d §4.8]` round-3 root cause #1; not via ASIO's property-query pipeline because `asio::any_io_executor`'s supported property set is fixed and closed). **v1.0 default impls do NOT persist timestamps alongside frames.** The session FSM stamps `SendingTime(52)` on outbound frames *before* calling `store()`, so the wire bytes already carry the timestamp. Inbound frames carry the peer's `SendingTime(52)`.

A future audit-pipeline-style store impl that needs receive-time stamping would read `effective_clock.now()` once per `store()` call and persist it as a sidecar entry; the v1.0 default impls do not need this.

### 6.6 Latency Tier 1 ceilings (N8 fix)

Per `[2a §6.5]` / `[2b §6.6]` v0.2 / `[2d §6.3]` precedent: Linux/Clang/x86_64 warm-cache, named workload. CI fails on >5% regression vs the previous tagged release (or, where noted, the FileStore I/O-bound rows: >2× regression).

Each row has a per-component arithmetic justification matching `[2b §6.6]` v0.2's pattern. The vtable dispatch cost (N3 / §4.1 decision) is included where it applies — ~5–15 ns warm-cache.

| Operation | Workload | Ceiling | Component breakdown |
|---|---|---|---|
| `MemoryStore::store` | 200-byte frame, in-strand HALO-fired | ≤ 200 ns | vtable dispatch ~10 ns + awaitable creation ~5–10 ns (HALO target) + `async_mutex::async_lock` uncontended ~10–20 ns + seq-verification atomic compare ~1 ns (Opus N2-P2-3 close) + memcpy 200 B at 3 GiB/s ~10–15 ns + entry-array write ~1 ns + mutex release ~5–10 ns + awaitable completion ~5–10 ns ≈ ~55–75 ns; ceiling at 200 ns leaves 2–3× headroom for cold-line probabilistic cases. |
| `MemoryStore::store` | 1 KiB frame | ≤ 800 ns | Dominated by memcpy at 3 GiB/s ≈ 333 ns; rest as above ≈ 60 ns; ceiling 800 ns is loose for noisy measurement. |
| `MemoryStore::next_seqnum(dir, false)` | read-only, mutexed (Opus N2-P2-2 close) | ≤ 50 ns | vtable dispatch ~10 ns + `async_mutex` acquire/release uncontended ~15–25 ns + counter load ~1 ns + awaitable plumbing ~5–10 ns. v0.2's ≤ 30 ns target assumed an unmutexed atomic-load path which §6.4 forbade; v0.3 lifts the ceiling to match the all-four-methods-take-the-mutex contract. |
| `MemoryStore::next_seqnum(dir, true)` | post-increment, mutexed | ≤ 50 ns | vtable + mutex + counter increment + awaitable ≈ 30–40 ns. |
| `MemoryStore::retrieve` | 100-frame range, visitor `cont` per call | ≤ 8 µs | ~70 ns/frame (vtable dispatch on visitor call ~10 ns + span construction ~5 ns + visitor awaitable plumbing ~50 ns under HALO; per-frame total dominated by visitor work). |
| `MemoryStore::reset` | full reset, capacity 200_000 entries | ≤ 500 µs | Per N8: an entry-array zero pass at 10 GiB/s of `memset` over 200_000 × 16 B = 3.2 MiB ≈ 320 µs. Slab is **not** zeroed (lazy reuse); only the entry array is touched. v0.1's ≤ 1 µs claim was off by ~100× (entry array alone is 3.2 MiB; `memset` at 10 GiB/s is ~320 µs); v0.2 lifts the ceiling to a defensible value. |
| `FileStore::store` | 200-byte frame, `commit_per_message` | ≤ 250 µs (soft) | Dominated by `fdatasync`/`FlushFileBuffers` floor (~150 µs commodity NVMe). Consistent with the §1.2 / §4.3.1 per-session 10⁴ frames/s target (1 / 10⁴ s = 100 µs steady-state; the 250 µs ceiling absorbs measurement noise and `fdatasync` jitter). CI flags >2× regression. |
| `FileStore::store` | 200-byte frame, `commit_batched(N=64)` | ≤ 50 µs (soft) | I/O-bound; amortised flush cost at one per N records. |
| `FileStore::retrieve` | 100-frame range, warm cache | ≤ 50 µs | Index lookup + `pread` per record. |
| `FileStore::reset` | full reset (round-2 atomic-rename algorithm per §6.3.4; round-3 post-cap durability pin per §6.3.5) | ≤ 5 ms (soft) | tmp-file open + sentinel-write + counters-write + tmp `fdatasync`/`FlushFileBuffers` + atomic-rename + platform durability primitive (Linux: parent-dir `fsync` mandatory; Windows: `MOVEFILE_WRITE_THROUGH` on the `MoveFileExW` call — mandatory and folded into the rename's syscall cost, no separate flush). Bounded by two flush calls on Linux (tmp-file flush + parent-dir fsync); one flush call plus the WRITE_THROUGH-tagged rename on Windows. |

`store()` ceilings on `MemoryStore` are nanosecond-tier (the constitutional hot-path discipline of `[const §VIII.5]` applies). `FileStore` ceilings are I/O-bound soft targets — CI flags >2× regression rather than >5%.

### 6.7 Errors introduced by this design (after root-cause/N1–N9 changes)

Per the per-doc-prefix discipline established by `[2a §6.7]` (`FIXPP_ERR_DECIMAL_*`), `[2b §6.7]` (`FIXPP_ERR_WIRE_*`), `[2c §6.7]` (`FIXPP_ERR_DICT_*`), `[2d §6.7]` (`FIXPP_ERR_THREAD_*`): 2e adopts the prefix **`FIXPP_ERR_STORE_*`** for its C-ABI mapping target, owned by 2i.

**Variant changes from v0.1 (rolled forward through v0.3):** `store_concurrent_writer` REMOVED in v0.2 (Codex P1-5 / round-1 root cause #3 — FIFO-fair mutex, not error). `store_cancelled` ADDED in v0.2 (round-1 root cause #1 / Codex C-P2-8 escalation). `store_seqnum_invalid` ADDED in v0.2 (round-1 N9 — `begin == 0`). `store_invalid_range` ADDED in v0.2 (round-1 N9 — `end < begin && end != 0`). `store_shim_timeout` was ADDED in v0.2 for the §4.8.B Path A subset (N4) and is **REMOVED in v0.3** (Codex C-R2-P2-1 escalation: §4.8.B retired). `store_seqnum_overflow` REWORDED to "session-fatal" per round-1 N6. **`store_visitor_aborted` C-ABI mapping changes in v0.3** (Codex C-R2-P2-3 close): no longer coalesced with `store_cancelled` under `FIXPP_ERR_CANCELLED` — `FIXPP_ERR_CANCELLED` reserves cancellation outcome, while `store_visitor_aborted` joins a new visitor/recovery group `FIXPP_ERR_STORE_VISITOR` (visitor abort during replay can represent retransmit/audit/consumer failure, not cancellation).

| `fixpp::core::error` variant | Source section | Remediation class |
|---|---|---|
| `store_io_failure` | §4.3 / §6.3 — FileStore disk I/O error (`pwrite`/`fdatasync`/`FlushFileBuffers`/`SetEndOfFile` returned a system error) | Runtime error — disk full, hardware fault, fs unmounted. Caller's session backpressure policy decides between `block` (retry — NOT recommended for disk faults) and `disconnect_and_recover`. **Operator note (v0.5 per Appendix D §D.5 / N-2 — Gap 2 propagation):** if the deployment is on a shared filesystem (NFS / SMB / FUSE / cluster FS) and `store_io_failure` surfaces during the restart scan as a CRC mismatch on otherwise-healthy hardware, the most likely cause is two-host concurrent write under §D.5's unsupported-filesystem scope restriction (the advisory lock per §6.3.5 was taken but not honored cross-host); the operator MUST verify the deployment topology before treating this as a hardware fault. No new error variant is added (the 10-variant freeze per FR-021 holds; adding `store_unsupported_filesystem` is a probe-coupled error that §D.5 explicitly rejects). |
| `store_seqnum_gap` | §4.1 method 2 — `retrieve()` overlaps a never-persisted seqnum (excluding trailing-edge `end == 0`) | Recovery error — typically `reset()` happened mid-session or seqnum bookkeeping desynced. FSM-side caller decides; common response is Logout. |
| `store_seqnum_out_of_order` | §4.1 method 1 — `store()` called with a `seq` ≠ `next_seqnum(dir, false)`. Detected inside the writer-mutex critical section (after mutex acquire, before slab memcpy / `pwrite`); on mismatch, the store releases the mutex with no state mutation (Opus round-2 N2-P2-3 close on the v0.2 contract hole) | Programmer error — bug in FSM or user code that bypassed FSM. |
| `store_capacity_exhausted` | §4.2 — `MemoryStore` ring is full; `drop-oldest` BANNED per `[const §XV.15]` | Runtime error — caller's session backpressure policy applies. |
| `store_seqnum_overflow` | §4.1 method 3 — `next_seqnum(dir, increment=true)` would produce > `seqnum_max` | **Session-fatal** (N6) — once observed, the session cannot send any further outbound messages until `reset()` is called; the FSM MUST surface this to user code (typically via `onLogout` with reason or a session-level error callback) so the user can decide between (a) `reset()` + `ResetSeqNumFlag(141)=Y` Logon (full session-history loss) per `[FIX-SL §4.4.2]` (24-hour connectivity) / `[FIX-SL §4.4.3]` (connection establishment) — round-3 post-cap re-anchor per Codex C-R3-P2-1 from the v0.3 mismapped `[FIX-SL §4.8.6]` citation — or (b) sticky session-fatal abort (manual operator reconciliation). The store does NOT autonomously reset. |
| `store_factory_failed` | §4.4 — `MessageStoreFactory::make(...)` reported failure (e.g., `FileStoreFactory` directory unwritable; `MemoryStore::Config` exceeds `EngineConfig::max_store_memory_per_session` per N9) | Configuration error — caller fixes the directory or swaps factory before re-opening. |
| `store_visitor_aborted` | §4.5 — `retrieve_visitor::on_frame`'s awaitable returned `visit_result::abort` and visitor did not override `abort_error()` | Caller's choice — visitor surfaces its own error or this default. |
| `store_seqnum_invalid` | §4.1 method 2 — `retrieve(begin == 0, …)` (FIX wire seqnums start at 1 per `[FIX-SL §4.1]`) (N9) | Programmer error — caller passed an invalid seqnum. |
| `store_invalid_range` | §4.1 method 2 — `end != 0 && end < begin` (N9) | Programmer error — caller's range is reversed. |
| `store_cancelled` | §6.1.4 — cancellation won before linearisation point (round-1 root cause #1 / Codex C-P2-8 escalation) | Cancellation outcome — joins `[2d §6.7] dispatch_aborted` and `[2d §6.7] clock_sleeps_cancelled` in the cancellation group at the C ABI. The FSM treats this distinct from `store_io_failure`: cancellation = no state change, no recovery action; `store_io_failure` = real fault, may need disconnect_and_recover. |

(10 variants. v0.1 had 8; +4 added in v0.2 (`store_cancelled`, `store_seqnum_invalid`, `store_invalid_range`, `store_shim_timeout`); -1 removed in v0.2 (`store_concurrent_writer`); -1 removed in v0.3 (`store_shim_timeout` — §4.8.B Path A subset retired per Codex C-R2-P2-1 escalation); v0.2 → v0.3 net is -1; v0.1 → v0.3 net is +2.)

C-ABI mapping (delegated to **2i**) per the per-doc-prefix discipline:

- runtime I/O / capacity / overflow → **`FIXPP_ERR_STORE_RUNTIME`**: `store_io_failure`, `store_capacity_exhausted`, `store_seqnum_overflow`.
- consistency / programmer / hostile-input → **`FIXPP_ERR_STORE_CONSISTENCY`**: `store_seqnum_gap`, `store_seqnum_out_of_order`, `store_seqnum_invalid`, `store_invalid_range`.
- configuration → **`FIXPP_ERR_STORE_CONFIG`**: `store_factory_failed` (in v0.3; `store_shim_timeout` was retired with §4.8.B per Codex C-R2-P2-1).
- visitor / recovery — **NEW group in v0.3 per Codex C-R2-P2-3** → **`FIXPP_ERR_STORE_VISITOR`**: `store_visitor_aborted` (the `retrieve_visitor::abort_error()` virtual lets the visitor surface a typed underlying error code; the wrapper-default is `store_visitor_aborted` when the visitor does not override). Visitor abort during replay can represent retransmit/audit/consumer failure, NOT cancellation; coalescing this with `FIXPP_ERR_CANCELLED` (v0.2) caused C consumers to read transport-write failures during ResendRequest as benign close-cancel.
- cancellation → **`FIXPP_ERR_CANCELLED`** per `[const §XI.2]` (joining `[2d §6.7] dispatch_aborted` and `clock_sleeps_cancelled`): `store_cancelled` only (Codex C-R2-P2-3 close: `store_visitor_aborted` is split out per the new `FIXPP_ERR_STORE_VISITOR` group above).

Final coalescing is 2i's call. The §4.5 `abort_error()` virtual return is the source-of-truth for the visitor's typed error code (mapped through 2i's per-doc-prefix discipline); `store_visitor_aborted` is the wrapper-default surfaced when the visitor does not override.

## 7. Integration with adjacent modules

### 7.1 Wire (`[arch §4.3]`, owner **2b**)

- The bytes 2e persists are produced by `wire::Writer` (outbound, via `Writer::commit()`'s span output per `[2b §4.5]`) and consumed by `wire::Framer::feed` (inbound, via `frame_view::bytes()` per `[2b §4.2]`). 2e accepts both as `std::span<const std::byte>`.
- **OUTBOUND CALL ORDERING (root cause #1).** `toApp` mutates the outbound message → `wire::Writer::commit()` finalises BodyLength + CheckSum per `[2b §4.5]` → 2e's `store(seq, committed_span, outbound)` copies the finalised bytes → transport's `async_write` consumes the same span. **Storing pre-commit bytes would persist a frame the framer rejects on replay** (placeholder `9=`, missing `10=`).
- **The store MUST copy** bytes into `store_arena` before the awaitable's first suspension per §6.2.
- Per `[2b §6.4]`: `MessageView` lifetime contracts MUST NOT bleed into the store API. Stored frames are flat byte ranges, not views. The store does not instantiate `wire::frame_view`, does not track generation tokens, does not hold a `wire::View`. Per `[2b §6.6]`: view-escape contract makes the input span unsafe past suspension. Per `[2b §7.4]`: MessageStore is the raw-frame-only sink.

### 7.2 Codegen / typed messages (`[arch §4.2]`, owner **2c**)

- Stored frames are version-agnostic. Per `[2c §7.2]` and §1.1 — no `Dictionary&`, no `dict::reify`, no `application_version` per stored frame.

### 7.3 Threading + Clock (`[arch §5.1]`, owner **2d**)

- **Strand binding.** Every `MessageStore` async method is invoked on the session strand via `session_executor` per `[2d §4.8]`; completions rebind to the same strand per `[2d §7.3]`. `FileStore`'s posting to `file_io_executor` is internal.
- **Cancellation result contract.** `store_cancelled` mirrors `[2d §6.5]`'s `dispatch_aborted` shape; the FSM-side caller observes cancellation in `expected_t` (see §6.1.4 root cause #1 fix).
- **PMR.** Per-call scratch on `store()` comes from `SessionConfig::session_arena` per `[2d §4.5]`; the store-owned `store_arena` (§8) is independent. **`MemoryStore::store` performs zero allocator calls** (N9).
- **Effective clock.** v1.0 default impls do not consume the clock (per §6.5).
- **Factory.** `SessionConfig::store_factory` carries the factory per `[2d §4.5]`; **flagged as a sibling-doc inconsistency** (`shared_ptr` in 2d v0.4 vs `unique_ptr` per N1 here) — Appendix D drop-in for 2d.

### 7.4 Awaitable mutex (2f) — root cause #3

- 2e takes a hard dependency on `fixpp::sync::async_mutex` for the writer-mutex contract per §6.4 and `[const §XI.3]`.
- The contract 2e needs from 2f matches `[2d §7.4]`'s executor-compat surface: completion on the awaiter's bound executor (the session strand), `cancellation_type::total` honoured, `dispatch` policy on completion. 2f's signature work is owned by 2f; 2e does not refine it.
- **2f sign-off is a hard hand-off gate before 2e implementation.** The v0.1 "transitional `std::recursive_mutex` adapter" is REMOVED per `[const §XV.9]` (no carve-out).
- **No `async_shared_mutex` / RW-mutex** per `[SYN §3.2 Q6b]`.

### 7.5 Log + OTel (`[arch §4.7]`, `[arch §4.8]`, owner **2k**)

- The store emits structured-log events on:
  - `store_io_failure` — error level, fields `{store_kind, direction, seq, file_path, errno}`.
  - `retrieve` calls — info level on entry/exit.
  - `commit_per_message` flush stalls (FileStore) — warn level if a flush exceeds 100× the per-platform expected baseline.
- OTel span shape: one `fixpp.session.message_store.store` span per call. The `flush` work is a child span.
- All observability is implemented through 2k's `Logger` / `Sink` / `TracerProvider` interfaces.

### 7.6 Phase-4 session-module spec — recovery FSM consumer

The Phase-4 session-module spec (not yet drafted) consumes 2e's surface:

- **`store(...)` callsites — root cause #1 ordering.** Inbound: post-Parser, pre-`fromApp/fromAdmin`, `co_await store->store(seq, frame, inbound)`. Outbound: post-`toApp`, **post-`Writer::commit`**, pre-`transport::async_write`: `co_await store->store(seq, committed_span, outbound)`.
- **`retrieve(...)` callsite.** On peer's `ResendRequest(begin, end)`: the FSM walks `co_await retrieve(begin, end, outbound, gap_fill_visitor)`; the awaitable visitor's `on_frame` performs the per-frame retransmit (`co_await transport.async_write(...)`) or the `SequenceReset-GapFill` emit decision per `[FIX-SL §4.8.5]` and `[FIX-SL §4.8.8]`. The visitor's per-frame logic is owned by the session-module spec.
- **`next_seqnum(...)` callsites.** Outbound: read-without-increment to populate `MsgSeqNum(34)` on the about-to-emit frame; post-`store()` increment. Inbound: read-with-increment after validate to advance the expected counter.
- **`reset()` callsite.** On `ResetOnLogon=Y` / `ResetOnDisconnect` per `[FIX-SL §4.4]` (S-017). Per N6, `store_seqnum_overflow` is session-fatal — Phase-4 surfaces it through a session-level error callback.
- **Graceful Session-close path (replaces v0.1's `flush()` callsite, N2; round-2 RC#2 close per Codex C-R2-P1-5; round-3 post-cap concept-mechanism specification per Opus N3-P2-1).** On `Session::close(graceful)` per `[2d §4.7]` (the cancellation propagation API + two-phase close — the per-mode effect table at lines 798–809 enumerates the 9 affected ops including `MessageStore::write` (in-flight) at line 808): before phase 1 completes, the engine's Session-close sequencer calls `FileStore::flush_for_session_close()` (an engine-internal, non-virtual, non-public method on the concrete `FileStore`) to drain any pending `commit_batched` / `commit_interval` records to durable storage. **The hook RUNS TO COMPLETION outside phase 1's child timeout** — it is the durability seam honouring `[FIX-SL §4.5]` graceful-Logout durability (round-3 post-cap pin per Codex C-R3-P1-3; see also §6.2.1). **Engine→`FileStore` dispatch mechanism (round-3 post-cap pin per Opus N3-P2-1):** the engine's Session-close sequencer dispatches via concept-shaped `requires { store.flush_for_session_close(); }` (named `fixpp::session::detail::has_flush_for_session_close` per §4.3 block-comment) — a non-virtual customization point that lives ONLY on the concrete `FileStore` type, **NOT** on the `MessageStore` pure-virtual interface (so the `[const §XIV.2]` ≤5 cap stays at 4) and **NOT** dispatched via RTTI (`dynamic_cast`) on the hot session-close path. The engine retains the factory-type tag at session open (it called `FileStoreFactory::make(...)` itself) and uses that tag to gate a known-safe `static_cast<FileStore*>` on the `unique_ptr`'s stored pointer when the factory was `FileStoreFactory`; for any other factory (including user-supplied custom factories and `MemoryStoreFactory`), the concept is not satisfied at the call site (compile-time no-op when the factory's mint type lacks the member) and the dispatch is elided. **`MemoryStore` does NOT define `flush_for_session_close()`** — the concept's `requires` clause fails and the engine skips the call; user-supplied custom `MessageStore` impls similarly inherit the no-op default for free without having to define the method. **No public `flush()` on `MessageStore`.** **Cross-doc edit owed.** v0.2 cited `[2d §4.7]` for this hook as if the contract already existed; the actual `[2d §4.7]` v0.4 effect table at lines 798–809 has no row for `flush_for_session_close()`. Round 2 declares the cross-doc amendment as **Appendix D §D.2 drop-in** (orchestrator applies at 2e sign-off, per 2d v0.4 / 2c v1.3 sibling-doc-edit precedent): one new row in the §4.7 effect table — `FileStore::flush_for_session_close() | runs (graceful pre-phase-1 store-durability flush) | n/a (already drained) | not invoked (terminal skips)` — plus a one-paragraph contract on the hook's cancellation/error semantics. See Appendix D §D.2.

The seam between 2e and the Phase-4 spec is the awaitable visitor type plus the **four** method calls + the engine-internal `flush_for_session_close()` path on `FileStore`.

## 8. PMR — recap

Two storage classes for 2e-owned data, both rooted in distinct PMR resources from the session arenas (per `[2d §8]` and `[2b §6.6]`):

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| `SessionConfig::session_arena` (per `[2d §4.5]` / `[2d §8]`) | session lifetime | per-call scratch on `store()` / `next_seqnum()` / `reset()` (PMR fallback for the awaitable's promise frame when HALO does not fire); `cancellable_dispatch` node storage for completion handoff back to session strand | session destruction |
| `store_arena` (owned by `MessageStore` impl; in v1.0 default impls, a `monotonic_buffer_resource` constructed from `MemoryStore::Config::store_resource` or `FileStore::Config::store_resource`) | store-instance lifetime (= session lifetime per N1 / `unique_ptr` ownership) | `MemoryStore`: the fixed `entry[]` ring + fixed slab (one allocation at construction; **no per-`store()` allocation** per N9); the writer-mutex internal state. `FileStore`: the in-memory log index + per-write framing scratch (≤ 1 buffer) | store destruction (= session destruction) |

- **`MemoryStore` MUST own its `store_arena`** and never alias `SessionConfig::session_arena` — frames persisted in `MemoryStore` outlive any per-session-arena reset cadence. The default if `Config::store_resource == nullptr` is an engine-provided dedicated `monotonic_buffer_resource`; the store's `store_arena` is therefore a peer of `session_arena`, not a sub-resource.
- **`FileStore`'s `store_arena`** holds the in-memory log index and per-write framing buffers; persisted bytes go to disk via `pwrite` and are not retained in memory.
- **Per `[const §VIII.5]`:** zero `new`/`delete` between parse and `fromApp` on the inbound path applies to `store(...)` — the only allocations are PMR-arena allocations from `session_arena` (HALO fallback) and `store_arena` (fixed at construction). Both are not `new`/`delete`. The `tools/check_alloc.py` post-link symbol scan catches regressions; §9 seam **"MemoryStore::store performs zero allocator calls"** is the per-impl tightening (N9).
- **`retrieve(...)`** is allowed PMR allocation per the brief's recovery-path carve-out. The visitor itself may allocate from caller-supplied mr.
- **§8 governs INTERNAL-storage PMR layout (v0.5 per Appendix D §D.6 clarification — Gap 3 close).** The store object's **own** allocation/deallocation is governed by Appendix D §D.6 — `std::default_delete<MessageStore>`-compatible per the `unique_ptr<MessageStore>` return type of `MessageStoreFactory::make()`. The store_arena PMR resource (and the `mr` threaded into `make()`) govern the slab / ring / framing-scratch / index allocations *inside* the minted store; they do NOT replace or override the deleter shape of the store object itself. Custom factory authors who want PMR-allocated store objects MUST wrap deallocation into a `std::default_delete`-compatible path (typical: static `operator delete` overload on the concrete store class) — `unique_ptr<MessageStore, CustomDeleter>` is NOT supported in v1.0 per §D.6.

## 9. Test seams

Per `[arch §10]` requirement (4) and `[const §VII]`. v0.3 ships **21 seams** (v0.2 had 20; round 2 retires seam #12 "Path A subset acceptance test" with the §4.8.B retirement per Codex C-R2-P2-1 escalation; round 2 adds two new seams — #19 "`FileStore::flush_for_session_close()` graceful-close drain" (round-2 RC#2 per Codex C-R2-P1-5) and #20 "`store_seqnum_out_of_order` detection" (Opus N2-P2-3); seam #10 "Reset semantics + atomicity" is rewritten in place to cover round-2 RC#1's atomic-rename algorithm (per Codex C-R2-P1-2) but keeps its slot; net +1, but the slot count is +2 from the renumbering after retiring #12 + #13 fuzzer-promotion and adding #19 / #20 + the closing #21 fuzzer; total = 21). Seams are referenced by name; ordinals may shift across review rounds.

(Round-2 numbering note: with seam #12 retired, v0.2's #13 "`quickfix_compat::cfg_to_file_store_factory` sanity" became #12; v0.2's #14–#20 became #13–#19 (with #19 v0.2 "Session shutdown ordering" → #18 v0.3 by reorder); the two NEW seams are #19 "`flush_for_session_close()`" + #20 "`store_seqnum_out_of_order`"; v0.2's #20 "Fuzzer" became #21.)

1. **`MemoryStore` round-trip.** `store(1, frame_a, outbound)` × 3; `retrieve(1, 3, outbound, byte_collecting_visitor)`; verify bytes-for-bytes. Same for `inbound`. `tests/session/test_memory_store_round_trip.cpp`.
2. **`FileStore` crash-survival.** Spawn child; in child open `FileStore` with `commit_per_message`; `store()` 100 frames; `SIGKILL`; parent re-opens; `retrieve(1, 0, outbound, visitor)`; verify all 100 frames byte-identical. Variant for `commit_batched` and `commit_interval` per the §4.3.1 data-loss-window. **Sub-cases NEW v0.5 per N-3 (Appendix D §D.4 — CompID filesystem-safety validation, Gap 1 close):** verify `FileStoreFactory::make()` returns `store_factory_failed` *before* any file is opened or advisory lock taken when (a) `sender_comp_id` or `target_comp_id` contains a path separator (`/` on Linux; `/` or `\\` on Windows); (b) contains `..`; (c) contains a NUL byte; (d) contains a control character in `[0x00, 0x1F]` or `0x7F`; (e) is empty (`""`); (f) would produce a path component exceeding `NAME_MAX`. FR-033's 21-seam count is preserved (this is a sub-case extension, not a new seam). `tests/session/test_file_store_crash_survival.cpp` and `tests/session/test_file_store_compid_validation.cpp` (split for clarity; both count under seam #2).
3. **`FileStore` portable torn-write protection.** Open `FileStore`; `store()` N frames (mixed inbound + outbound, exercising the round-2 single-log per-record-direction-tag shape per §6.3.1); programmatically truncate the log mid-record; re-open; verify the truncated record is detected via CRC32 and the log is truncated to last whole record per §6.3.2. **Runs on Linux/Clang Tier 1 AND Windows/MSVC Tier 2** per `[const §II.3]`. `tests/session/test_file_store_torn_write.cpp`.
4. **`MemoryStore` capacity exhaustion.** Open with `policy = bounded`, `outbound_capacity = 100`; `store()` 100 frames; 101st returns `expected_t::unexpected{store_capacity_exhausted}`; verify state unchanged. Variant for `policy = unbounded`: 10⁵ frames stored without capacity error (no hard cap); the `store_capacity_exhausted` path is unreachable under `unbounded`. `tests/session/test_memory_store_capacity.cpp`.
5. **FIFO-fair concurrent-writer test.** Open `MemoryStore`; from two `asio::thread_pool`-backed coroutines, invoke `store()` concurrently. Verify the second arrival **suspends and proceeds in FIFO order** (Codex P1-5 fix); verify NEITHER arrival receives `store_concurrent_writer` (variant removed). Run under TSan + ASan. Lives in `tests/session/test_store_fifo_fair.cpp`.
6. **Cancellation-result-contract test (round-1 root cause #1 / Codex C-P2-8 escalation).** For each method, fire cancellation slot before linearisation point; verify the awaitable completes with `expected_t::unexpected{store_cancelled}`. Fire cancellation after linearisation; verify normal completion with the operation's value. Validates the §6.1.4 cancellation table per method. `tests/session/test_store_cancellation_contract.cpp`.
7. **Outbound store-after-commit byte-equality test (round-1 root cause #1 / Codex P1-1).** Construct a Writer for a small frame whose BodyLength digit count would be 2 → 3 at commit (forcing the `[2b §4.5]` `memmove` backpatch). `Writer::commit()` produces the post-commit span; `store(seq, span, outbound)`; the store's persisted bytes MUST carry the final BodyLength digits AND the CheckSum byte. Variant: order check that `store()` is NEVER reached before `Writer::commit()` (compile-time assertion that the call sequence in the test FSM stub follows §6.1 / round-1 root cause #1). `tests/session/test_outbound_store_post_commit.cpp`.
8. **Replay over arbitrary `[begin, end]` including gaps + invalid input (round-1 N9).** `store()` seqs 1, 2, 4, 5; `retrieve(1, 5, outbound, visitor)` returns `store_seqnum_gap`. `retrieve(1, 2, outbound, visitor)` succeeds. `retrieve(0, 5, outbound, visitor)` returns `store_seqnum_invalid`. `retrieve(5, 1, outbound, visitor)` returns `store_invalid_range`. `retrieve(4, 0, outbound, visitor)` (infinity) succeeds. `tests/session/test_retrieve_with_gaps.cpp`.
9. **Awaitable visitor + span lifetime (round-1 root cause #2).** `store()` 10 frames; `retrieve(1, 10, outbound, visitor)` where each `on_frame` `co_await`s a 100-µs delay before returning `cont`. Verify span content stable across the visitor's suspension; verify `[[asan]]`-instrumented access to the span returns the right bytes. Variant: visitor `co_return abort` after frame 3 → awaitable returns `expected_t::unexpected{<visitor::abort_error()>}`. `tests/session/test_retrieve_visitor.cpp`.
10. **`FileStore::reset()` atomic-rename test (round-2 RC#1 per Codex C-R2-P1-2 — replaces v0.2's "truncate + sentinel" crash test).** Open `FileStore`; `store()` 5 frames mixed inbound + outbound (exercising the round-2 single-log shape); call `reset()`. Verify the post-reset live log contains exactly: sentinel record + counters record (`next_inbound = next_outbound = 1`); verify `<...>.log.reset.tmp` does NOT exist post-reset. Same for `MemoryStore::reset()`. **Crash test cuts (round-2 §6.3.4 algorithm):** SIGKILL between (a) tmp-file open and tmp-file `fdatasync` → restart cleans `.reset.tmp`, sees old log; (b) tmp-file `fdatasync` and `rename` → restart cleans `.reset.tmp`, sees old log; (c) `rename` and parent-dir `fsync` (Linux) → restart sees new log on most filesystems (or old log if rename was lost — log warning). Verify each path lands a coherent restart state. `tests/session/test_store_reset.cpp`.
11. **QuickFIX-compat default Path B compile-time guard.** `static_assert(!std::is_constructible_v<fixpp::session::MessageStore, FakeQuickFixStore*>)` — verifies no implicit construction path from a sync-shaped object into the async interface. **No Path A subset test ships in v0.3** (round-2 close on Codex C-R2-P2-1 escalation: §4.8.B retired, no `sync_message_store_adapter` to test). `tests/session/test_quickfix_compat_path_b_guard.cpp`.
12. **`quickfix_compat::cfg_to_file_store_factory` sanity.** Feed sample CFG; verify resulting `FileStoreFactory` matches `FileStorePath`; mint session; round-trip frame; verify byte-equality. `tests/session/test_quickfix_compat_cfg_loader.cpp`.
13. **Latency regression — `MemoryStore::store` warm cache.** Google Benchmark on `MemoryStore::store(seq, 200-byte-frame, outbound)` ≤ 200 ns. CI fails on >5% regression. Variant for 1 KiB frame ≤ 800 ns. Variant for `MemoryStore::reset` capacity at the round-2 default 10_000 entries per direction ≤ 25 µs (scaling from the round-1 N8 ≤ 500 µs ceiling at 200_000: entry-array zero pass at 10 GiB/s of 10_000 × 16 B × 2 = 320 KiB ≈ 32 µs; ceiling 25 µs is tight but defensible since the reset zero is well within L2 cache; CI flags >5% regression). Variant for `MemoryStore::next_seqnum(dir, false)` ≤ 50 ns (Opus N2-P2-2 lift to mutex-always model). `bench/session/bench_memory_store.cpp`.
14. **Allocation guard — global heap on hot path.** `tools/check_alloc.py` + `mallocnesia` (Linux/Clang Tier 1 per `[2a §9]` / `[2b §9]` / `[2d §9]`). 10⁴-message session run; zero global-heap `new`/`delete`/`malloc` between parse and `fromApp`; PMR-arena allocations are expected. `tests/perf/test_store_alloc_guard.cpp`.
15. **`MemoryStore::store` performs zero allocator calls (round-1 N9 — replaces v0.1 seam #15 wishful test per N10).** Open with `policy = bounded` (the zero-allocator-calls property does NOT hold under `unbounded`). Inject a tracking PMR resource into `MemoryStore::Config::store_resource` that increments a counter on every `allocate(...)` call. Construct the `MemoryStore` (which consumes one large `allocate` for the slot+slab combined). Run 10⁴ `store()` calls; verify the counter is unchanged from the post-construction baseline. **`MemoryStore::store` MUST do zero allocator calls under `bounded` policy.** `tests/session/test_memory_store_zero_allocator_calls.cpp`.
16. **PMR poison on retrieve-recovery path (round-1 N10 fix — replaces v0.1 wishful seam #15).** Inject a poison PMR resource into the visitor's mr; the resource throws on the N-th allocation. `retrieve()` walks; the visitor's per-frame allocation triggers the throw; verify the retrieve awaitable surfaces `expected_t::unexpected{store_visitor_aborted}` (mapped from `trap_throw` per §6.1.2 — round-2 N2-P2-1 refinement: cite `[2a §4.2]` + `[arch §5.3]` for the no-terminate behaviour, not `[const §VIII.5]`) without termination. `tests/session/test_store_pmr_poison_retrieve.cpp`.
17. **Conformance corpus replay.** Load recorded FIX session; for each frame, `MemoryStore::store(seq, frame_bytes, dir)`; `retrieve(1, 0, dir, byte_compare_visitor)`; assert byte-identical replay. Variant for `FileStore`. `tests/conformance/test_store_corpus_replay.cpp`.
18. **Session shutdown ordering test (round-1 N7).** Spin a `Session` with `MemoryStore` + `FileStore`; issue 100 in-flight `store()` calls; trigger `Session::close(terminal)`; verify under TSan + ASan: no UAF on `session_arena`; all `store_cancelled` outcomes route correctly; `~MessageStore` runs before `session_arena` release. `tests/session/test_store_shutdown_ordering.cpp`.
19. **`FileStore::flush_for_session_close()` graceful-close drain (round-2 RC#2 per Codex C-R2-P1-5 — NEW seam).** Open `FileStore` with `policy = commit_batched(N=64)`; `store()` 32 frames (half a batch — under v0.2's no-flush-hook model these would be silently lost on a host crash before the batch boundary). Call `Session::close(graceful)` — engine reaches the concrete `FileStore` and invokes `flush_for_session_close()`. Re-open; `retrieve(1, 32, outbound, visitor)` returns all 32 frames byte-identical. Variant: under `Session::close(terminal)`, verify `flush_for_session_close()` is **NOT** invoked (per Appendix D §D.2 contract); the 32-frame data-loss is the documented `commit_batched` window. `tests/session/test_file_store_flush_for_session_close.cpp`.
20. **`store_seqnum_out_of_order` detection (Opus N2-P2-3 — NEW seam).** Open `MemoryStore`; via a test-only friend hook, drive `store(seq=5, frame, outbound)` while `next_seqnum(outbound, false) == 1`. Verify the awaitable returns `expected_t::unexpected{store_seqnum_out_of_order}`; verify the entry-array index is unchanged (no slab memcpy, no entry write); verify the writer mutex was acquired and released across the verification. Same for `FileStore`. `tests/session/test_store_seqnum_out_of_order.cpp`.
21. **Fuzzer.** `tests/fuzz/fuzz_message_store.cpp` — libFuzzer-driven random interleavings of `store/retrieve/reset/next_seqnum` against `MemoryStore` and `FileStore`. ASan + UBSan + TSan invariants. Required by `[const §VII.7]` (the store is on the session message path; fuzzing the surface catches torn-state regressions across the round-2 single-log on-disk algorithm).

## 10. Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | **`FileStore` on Windows — durability semantics.** `FlushFileBuffers(handle)` per §6.3.5; advisory locks via `LockFileEx`. The portable append-only-log + truncate-on-restart scheme (§6.3) needs **no** directory-fsync primitive on either platform. | **CLOSED** in v0.2 — algorithm rewritten per root cause #4 to be portable; §9 seam **"FileStore portable torn-write protection"** runs Linux + Windows. | 2e (closed) |
| 2 | **Replicated MessageStore (COM-009) — post-v1.** Async surface admits a future replicated impl; visitor pattern handles streaming reads from a remote source; writer mutex contract becomes an `async_mutex` over a distributed lock. | Tracked as a forward-compat invariant; no design work in v1.0. | post-v1 follow-up |
| 3 | **Sidecar audit log.** v1.0 default impls do not carry an optional `audit_sink`; users compose by chaining `MessageStore` instances (a `tee_store` factory at construction time per N1's composition note). | Defer; user-side composition is acceptable for v1.0. | post-v1 follow-up |
| 4 | **`FileStore` directory contention.** Two engines pointed at the same directory both trying to write the same `<sender>__<target>.log` — recipe for corruption. v1.0 does an `flock`-style advisory lock at open per §6.3.5; second opener gets `store_factory_failed`. (Round-2 update: file path is now `<sender>__<target>.log` — single file per session per §6.3.1, not `<...>.<dir>.log`.) | Specified inline in §6.3.5 (Linux: `flock`; Windows: `LockFileEx`); cross-platform shim is straightforward. | 2e |
| 5 | **`MessageStore::flush()` semantics for `MemoryStore`.** **DROPPED** — `flush()` removed from public interface entirely (N2). Session-close path uses engine-internal `FileStore::flush_for_session_close()` whose callsite contract is owed to `[2d §4.7]` as Appendix D §D.2 drop-in (round-2 RC#2 close per Codex C-R2-P1-5). | **CLOSED** in v0.2 (N2); `[2d §4.7]` cross-doc edit declared in Appendix D §D.2 (round 2). | — |
| 6 | **2k OTel span attribute set.** §7.5 lists names; precise attribute set is 2k's call. | Defer to **2k**. | 2e + 2k |
| 7 | **`FileStore` header sentinel — should it carry `BeginString` / `SenderCompID` / `TargetCompID`?** Yes — header carries `magic | version | hash(session_triple) | crc32`; re-open verifies, returns `store_factory_failed` on mismatch. | DECIDED yes; documented in §6.3.1. | 2e |
| 8 | **2f `async_mutex` signature.** Locked at `[2d §7.4]` and §6.4. | Defer to **2f** signature; **2f sign-off is hard hand-off gate** for 2e implementation per §3.1. | 2f |
| 9 | **`seqnum_t` ownership and width — should it be `uint32_t` (current placeholder) or `uint64_t` (N6)?** The 32-bit choice matches observed convention; `uint64_t` would eliminate `store_seqnum_overflow` for never-reset use cases (audit-trail sessions running 24/7 for years). Tradeoff: 2× memory in counters file + index entries. **Owner per §3.1: Phase-4 session-module spec.** | Phase-4 picks; until then, 2e consumes `uint32_t` placeholder per `<fixpp/session/seqnum.hpp>`. | Phase-4 session-module spec |
| 10 | **`FileStore::Config::policy.commit_interval` worker — does it run on `file_io_executor` or its own timer thread?** Same `file_io_executor` (single execution context per `FileStore` instance avoids cross-thread races on the log file handle). | DECIDED — `file_io_executor` only. | 2e |
| 11 | **`SessionConfig::store_factory` ownership — `unique_ptr` (2e §4.4 / N1) or `shared_ptr` (`[2d §4.5]` v0.4 line 534)?** `unique_ptr` is the type-correct choice per `[arch §5.6]` (no mid-session swap, no shared store). 2d v0.4 has `shared_ptr` — sibling-doc inconsistency. | **Round-2 close (per Codex C-R2-P1-4):** orchestrator applies Appendix D §D.1 + §D.2 at 2e sign-off as part of the commit — same precedent as 2d v0.4 / 2c v1.3 sibling-doc-edit pattern. 2e ships `unique_ptr<MessageStore>` from `make()` regardless. | 2d (orchestrator-applied at 2e sign-off) |

## 11. Hand-off

**Docs unblocked by 2e sign-off (downstream):**

- **Phase-4 session-module spec** — knows the store contract (4 methods + visitor; raw-frame discipline; awaitable visitor; reset semantics; mutex contract; outbound call ordering per root cause #1; cancellation result contract per §6.1.4); the recovery FSM is now drafftable. The Phase-4 spec ALSO publishes `seqnum_t` per §3.1 / §10 Q9.
- **2i** (C ABI message rep + error enum) — knows `fixpp_store_t` opaque handle is a non-owning observer of a `unique_ptr`-held store (N1); knows the new `FIXPP_ERR_STORE_*` family for §6.7's variants; knows the `direction_t` and `seqnum_t` C-ABI shapes.
- **2j** (control plane) — knows mid-session `MessageStore` swap is rejected; the gRPC `OpenSession` request carries optional `MessageStoreFactory` selector.
- **2k** (log + OTel) — knows the structured-log event shape and OTel span shape (§7.5).
- **2m** (SWIG / Python) — knows the per-method async shape; Python-side custom impls inherit `fixpp.session.MessageStore`.

**Catalogue + coverage-index amendments owed at sign-off:**

- No new catalogue rows. S-011..S-014, OSS-002, COM-009 are already OFFICIAL.
- `library/spec/coverage-index.md` line 76 (`§4.8 | Message recovery | Y | S-014 | —`) is amended to **`S-011, S-012, S-013, S-014`** (Codex P2-10 / per Opus confirm) with a note that 2e discharges the **store-side** API + default impls and Phase-4 owns the FSM.
- `[arch §11]` Q3 disposition is updated from `Phase 2 validates [SYN §3.2 Q7]` to the live FR-039 wording at `architecture.md:598` (Path B verdict, no runtime adapter; documented incompatibility + migration recipe + `quickfix_compat::cfg_loader` config-translation surface; disposition applied by `008-message-store` Phase-4 Gate A convergence). v0.2's Path A subset wrapper retired in round 2 per Codex C-R2-P2-1 escalation; v0.3 verdict is Path B only.
- **Two `[2d]` sibling-doc amendments via Appendix D drop-ins (round-2 root cause #2 close):**
  - `[2d §4.5]` `SessionConfig::store_factory` field type is amended from `std::shared_ptr<MessageStoreFactory>` to `std::unique_ptr<MessageStoreFactory>` (N1 sibling-doc edit) — **Appendix D §D.1**.
  - `[2d §4.7]` per-mode effect table gains a `FileStore::flush_for_session_close()` row + a one-paragraph contract on the hook's cancellation/error semantics (Codex C-R2-P1-5 / round-2 RC#2) — **Appendix D §D.2**.
- The `quickfix_compat::sync_message_store_adapter` wrapper artefact published in v0.2 §4.8.B is retired from this hand-off (Codex C-R2-P2-1).

`feature-catalogue.md` is **not edited from this rewrite.**

---

## Appendix A — Catalogue row coverage

| Row | Catalogue text | Where satisfied in this doc | Test seam(s) (by name) |
|---|---|---|---|
| **S-011** | OFFICIAL — Message store interface — `[FIX-SL §4.8]` | §4.1 (4-method interface); §4.4 (factory); §4.6 (`direction_t`); §4.7 (`seqnum_t` placeholder) | "MemoryStore round-trip", "Conformance corpus replay" |
| **S-012** | OFFICIAL — In-memory message store impl — `[FIX-SL §4.8]` | §4.2 (`MemoryStore`); §4.4 (`MemoryStoreFactory`) | "MemoryStore round-trip", "MemoryStore capacity exhaustion", "Reset semantics + atomicity", "Latency regression — MemoryStore::store warm cache", "MemoryStore::store performs zero allocator calls" |
| **S-013** | OFFICIAL — File-based message store impl — `[FIX-SL §4.8]` | §4.3 (`FileStore`, `FileStorePolicy`); §4.4 (`FileStoreFactory`); §6.3 (portable on-disk algorithm) | "FileStore crash-survival", "FileStore portable torn-write protection", "Cancellation-result-contract test", "Conformance corpus replay" |
| **S-014** (store-side only; FSM owned by Phase-4) | OFFICIAL — Session recovery — resend flow, GapFill (123=Y) — `[FIX-SL §4.8]` | §4.1 method 2; §4.5 (awaitable `retrieve_visitor`); §6.3 (admin frames are byte-identical raw); §7.6 (FSM seam description) | "Replay over arbitrary [begin, end] including gaps + invalid input", "Awaitable visitor + span lifetime" |
| **OSS-002** | OSS:QuickFIX `MessageStore` interface | §4.1.1 (interface-shape inheritance argument); §4.8.A.3 (migration recipe table); §4.8.B (round-2 retirement rationale per Codex C-R2-P2-1) | "QuickFIX-compat default Path B compile-time guard", "quickfix_compat::cfg_to_file_store_factory sanity" |
| **COM-009** | COMMERCIAL:CameronTec — Replicable MessageStore (warm-standby failover) — **post-v1.0** | §1.1 non-goal; §10 Q2 (forward-compat invariant) | none in v1.0 |

## Appendix B — Normative References

Per `[const §VI.5]`, exact-coverage. Pure list, no commentary.

- `[FIX-SL §4.1]` Sequence numbers
- `[FIX-SL §4.4]` Sequence reset
- `[FIX-SL §4.4.2]` Using ResetSeqNumFlag(141) for 24-hour connectivity (round-3 post-cap addition per Codex C-R3-P2-1; matches `library/spec/coverage-index.md` row 60 verbatim)
- `[FIX-SL §4.4.3]` Using ResetSeqNumFlag(141) during connection establishment (round-3 post-cap addition per Codex C-R3-P2-1; matches `library/spec/coverage-index.md` row 61 verbatim)
- `[FIX-SL §4.5]` Message exchange during a FIX connection (graceful-Logout durability — load-bearing for `flush_for_session_close()` per §7.6; matches `library/spec/coverage-index.md` row 63 verbatim)
- `[FIX-SL §4.5.4]` Rejecting invalid messages (Reject 35=3)
- `[FIX-SL §4.8]` Message recovery
- `[FIX-SL §4.8.3]` Responding to ResendRequest(35=2)
- `[FIX-SL §4.8.5]` Gap fill process (SequenceReset-GapFill)
- `[FIX-SL §4.8.6]` Sequence reset (hard reset, GapFillFlag=N) (round-3 post-cap retitle per Codex C-R3-P2-1; matches `library/spec/coverage-index.md` row 83 verbatim — covers `SequenceReset-Reset` (35=4, 123=N) wire-message semantics, NOT the `ResetSeqNumFlag(141)=Y` Logon recovery path which lives at §4.4.2 / §4.4.3 above)
- `[FIX-SL §4.8.8]` Processing gaps for session layer messages
- `[arch §1.1]` Goals
- `[arch §4.4]` `session` module
- `[arch §4.10]` `capi` module surface
- `[arch §5.1]` Executor model
- `[arch §5.3]` Error model
- `[arch §5.4]` Trace context
- `[arch §5.5]` Lifetime model
- `[arch §5.6]` Configuration shape (frozen at session open)
- `[arch §6]` Plugin pattern
- `[arch §10]` Hand-off to design docs 2a–2m, row 2e
- `[arch §11]` Open Architectural Questions, row Q3
- `[const §I.1]` v1.0 version surface
- `[const §II.3]` Tier 2 platform support — Windows/MSVC
- `[const §VI.5]` Spec coverage discipline (exact-coverage citations)
- `[const §VII]` Testing requirements
- `[const §VIII.5]` Allocator policy on hot path (zero new/delete between parse and fromApp)
- `[const §X.4]` Forwards-compat reserved range
- `[const §X.5]` Reentrancy contract on C ABI handles
- `[const §XI.1]` Coroutines as the session/transport composition primitive
- `[const §XI.2]` ASIO native cancellation slots end-to-end
- `[const §XI.3]` Awaitable mutex required in coroutine context
- `[const §XI.4]` Application threading default — per-session strand
- `[const §XI.5]` Hot-path lock policy — store-write always uses mutex
- `[const §XIV.2]` Pluggable interface ≤5 pure-virtual cap
- `[const §XV.1]` Banned: heap-alloc per message on hot path
- `[const §XV.4]` Banned: synchronous disk I/O on every send
- `[const §XV.9]` Banned: `std::mutex` in coroutine context
- `[const §XV.15]` Banned: application-layer message drops on slow consumer
- `[const §XVII.1]` Codex Gate A required for design docs
- `[const §XVIII.5]` No early shipping of post-v1 protocols
- `[SYN §3.2 Q6b]` `async_shared_mutex` post-v1.0
- `[SYN §3.2 Q7]` MessageStore async API + QuickFIX-compat shim feasibility (round-2 C-R2-P3-1 close on title drift; v0.2 used non-canonical "MessageStore replication shape")
- `[SYN §3.2 Q8]` Hot-path lock policy — store-write always uses mutex
- `[2a §4.2]` `decimal_traits<T>` customization point (PMR + `trap_throw` pattern)
- `[2a §6.7]` Errors introduced by 2a
- `[2a §7.1]` Decimal — wire integration (raw-frame replay decision; this doc confirms)
- `[2a §10] Q3` Confirm with 2e raw-frame storage
- `[2b §4.2]` `Framer` + `frame_view::bytes()`
- `[2b §4.5]` `Writer::commit` finalises BodyLength + CheckSum
- `[2b §6.4]` Lifetime contract on flyweights
- `[2b §6.6]` Allocation, exceptions, threading; three-arena pinning; view-escape
- `[2b §6.7]` Errors introduced by 2b
- `[2b §7.4]` MessageStore + typed-payload persistence (raw-frame contract)
- `[2c §1.1]` Codegen scope boundary
- `[2c §6.7]` Errors introduced by 2c
- `[2c §7.2]` Session integration (no `Dictionary&` held by store)
- `[2d §4.4]` `EngineConfig` (clock, executor, store factory anchors)
- `[2d §4.5]` `SessionConfig` (`store_factory` field; `session_arena`)
- `[2d §4.6]` `current_trace_context` + `session_local<T>`
- `[2d §4.7]` Two-phase close (graceful flush before phase 1 completes)
- `[2d §4.8]` `session_executor` + executor resolution path
- `[2d §6.1]` Strand semantics
- `[2d §6.3]` Latency Tier 1 ceilings
- `[2d §6.4]` Backpressure on the strand
- `[2d §6.5]` Cancellation semantics + `cancellable_dispatch`
- `[2d §6.7]` Errors introduced by 2d
- `[2d §7.3]` MessageStore strand-binding handoff
- `[2d §7.4]` Awaitable mutex executor-compat surface
- `[2d §7.9]` Single effective_clock per session
- `[2d §8]` Threading PMR recap

Engineering-judgment decisions whose primary driver is design judgment rather than a specific spec section (the awaitable-visitor over pull-API choice, `direction_t` parameterisation over interface split, `FileStorePolicy` default selection, the v1.0-`uint32_t` `seqnum_t` placeholder while the Phase-4 spec is unstarted, the v0.3 verdict on Path B-only after Path A subset retirement, the virtual-vs-concept decision for the `MessageStore` interface, the round-2 single-log-per-session shape over a generation-manifest scheme, the round-2 atomic-rename `reset()` algorithm) cite `[SYN §3.2 Q6b]`, `[SYN §3.2 Q7]`, `[SYN §3.2 Q8]`, and the relevant `[const §X.y]` clauses inline at point of use; they are not spec normatives and are intentionally omitted from this appendix.

## Appendix C — Convergence log

Records the v0.1 → v0.2 Gate A round 1 convergence pass, the v0.2 → v0.3 Gate A round 2 convergence pass, the v0.3 → v0.4 round-3 post-cap line-edit pass, and the v0.4 → v0.5 post-sign-off targeted gap-closure pass. Per the cumulative-log discipline used by 2c v1.3 / 2d v0.4: round-1 entry is preserved verbatim; round-2 entry is preserved verbatim; round-3 (post-cap) entry is preserved verbatim; v0.4 → v0.5 (post-sign-off targeted) entry is appended at the top of the chronological listing below (newest-first).

---

### Post-sign-off targeted gap-closure pass: v0.4 → v0.5 (2026-05-20)

**Reviews input (TARGETED post-sign-off pass — NOT a Gate A round):**
- Codex targeted review (Gap 1 CONFIRM P1 / Gap 2 CONFIRM P1 / Gap 3 CONFIRM P2): `research/reviews/codex_2e_targeted_msgstore_review.md`
- Opus adversarial targeted review (post-judging tally `P1 = 2, P2 = 3, P3 = 4`; 5 NEW findings N-1..N-5; 2 root causes — "scope & trust" primitives in §4.3 / §6.3; store-object-allocation contract silence in §4.4 / §8): `research/reviews/opus_2e_targeted_msgstore_adversarial_review.md`

**Origin:** `008-message-store` Phase-4 pipeline-step-9 checklist audit waivers (CHK004 / CHK008 / CHK030 in `specs/008-message-store/checklists/implementation-readiness.md`) had fabricated scope-pinning rationales when adversarially pressure-tested. The 3 gaps trace to design-doc-level silence that v0.4's Gate A round 1–3 convergence missed:

| Gap | Codex | Opus | Resolution |
|---|---|---|---|
| Gap 1 — CompID filesystem safety on `<sender>__<target>.log` filename | CONFIRM P1 | CONFIRM P1 (Opus tightens with empty-CompID + control-char + NAME_MAX rejects) | **Appendix D §D.4 NEW** — normative MUST-reject at `FileStoreFactory::make()` before file open / lock take; mirrored at `cfg_loader` as defense-in-depth |
| Gap 2 — `flock` / `LockFileEx` semantics scope on NFS / SMB / cluster FS | CONFIRM P1 (Codex escalates from initial-framing P2) | CONFIRM P1 (Opus tightens with explicit unsupported-FS enumeration + "no detection or warning" pin) | **Appendix D §D.5 NEW** — scope-pin in §6.3.5; FR-013 / I-16 / `file_store_factory.hpp` docstring cross-reference (N-1); `store_io_failure` operator-doc tightening in §6.7 (N-2) |
| Gap 3 — `std::unique_ptr<MessageStore>` deleter / store-object allocation contract | CONFIRM P2 | CONFIRM P2 (Opus considered P1 escalation; rejected because type signature is genuinely load-bearing) | **Appendix D §D.6 NEW** — `std::default_delete<MessageStore>` pin in §4.4 with cross-reference in §6.1.1 / §8; forward-compat reservation (`unique_ptr<MessageStore, CustomDeleter>` NOT supported in v1.0; reserved per `[const §X.4]`) per N-4 |

**Disagreement: NONE.** Opus confirms all 3 Codex verdicts at the severities Codex assigned. Per memory `feedback_gate_a_codex_dual_pass`: Codex's review here is the rescue pass only (no second `/codex:adversarial-review`); Opus carries sole adversarial responsibility against Codex's findings — this is a post-sign-off targeted single-round dual-pass per user direction (option A in the orchestrator's path-choice), NOT the full /gate-a-ph2 fresh-design loop.

**Sections edited (v0.4 → v0.5):**
- §4.3 `FileStore::Config` block-comment — line-edit referencing §D.4 (CompID validation at make() time).
- §4.4 `MessageStoreFactory::make()` block-comment — line-edit referencing §D.6 (default-deleter pin + post-v1.0 reservation).
- §6.1.1 — line-edit clarifying object-allocation vs internal-storage-allocation scope (cross-reference §D.6).
- §6.3.5 platform-portability table area — NEW "scope & trust" paragraph after the platform table referencing §D.5 (FS-type scope) and §D.4 (CompID validation); resolves root cause #1.
- §6.7 `store_io_failure` row — operator-doc tightening per N-2 (note shared-filesystem CRC mismatch as a likely operator misconfiguration signal under §D.5 scope restriction). FR-021's 10-variant freeze preserved (no new variant).
- §8 PMR recap — cross-reference §D.6 for object-allocation scope split.
- §9 seam #2 — extended with CompID-validation reject sub-cases per N-3 (CompIDs containing `/`, `\`, `..`, NUL, control char, empty, NAME_MAX excess MUST surface `store_factory_failed` from `FileStoreFactory::make()` before file open / lock take). FR-033 21-seam count preserved.
- Appendix D — NEW §D.4 + §D.5 + §D.6 (self-amendments by `008-message-store` Phase-4 step-9 audit, same shape as §D.3 self-amendment by `008-message-store` Phase-4 Gate A).

**Not edited (verified unchanged):** the 4-pure-virtual `MessageStore` interface; the awaitable visitor; the single-log-per-session on-disk shape; atomic-rename `reset()`; exclusive `async_mutex`; `commit_per_message` default; §3.1 inherited-primitives table; §6.1.4 cancellation result-contract; §6.3.4 atomic-rename algorithm; §6.3.5 platform-portability primitives table (the table itself — the new prose is *appended* after it, not rewriting it); the 10-variant §6.7 errors table (variants + groups unchanged); Appendix D §D.1 / §D.2 / §D.3 (drop-ins unchanged).

**Cross-doc propagation owed in this same pass** (the `008-message-store` bundle inherits the v0.5 amendments):
- `specs/008-message-store/spec.md` — FR-008 tightened with CompID validation per §D.4; FR-013 cross-references §D.5; FR-005 / FR-025 cross-reference §D.6.
- `specs/008-message-store/data-model.md` — E4 (FileStore) cross-references §D.4 / §D.5; E5 (MessageStoreFactory) cross-references §D.6; I-16 cross-references §D.5.
- `specs/008-message-store/contracts/` — `file_store.hpp` Config docstring cites §D.4; `file_store_factory.hpp` make() docstring cites §D.5 + §D.4; `message_store_factory.hpp` make() docstring cites §D.6; `cfg_loader.hpp` cites §D.4 defense-in-depth mirror.
- `specs/008-message-store/checklists/implementation-readiness.md` — CHK004 / CHK008 / CHK030 reclassify to **SPEC-FIXED** citing §D.4 / §D.5 / §D.6; CHK006 / CHK018 reclassify from WAIVED to **DD-DECIDED** with corrected anchors; verdict restored to GREEN; `/speckit-analyze` re-run required per skill step 7.

**Verdict:** v0.5 closes the 3 gaps without re-litigating any Appendix C round 1–3 disposition or touching the v0.4 spine. The post-sign-off targeted pass is sufficient; no Gate A reset; no Phase B Codex re-design; no Phase C comparator.

---

### Round 3: v0.3 → v0.4 (2026-05-08, post-cap line-edit pass per 2c v1.3 / 2d v0.4 precedent)

**Reviews input:**
- Codex Gate A round 3 (3 P1 / 1 P2 / 1 P3): `research/reviews/codex_2e_3_msgstore_review.md`
- Opus adversarial round 3 (post-judging 3 P1 / 2 P2 / 2 P3, 1 root cause): `research/reviews/opus_2e_3_msgstore_adversarial_review.md`

**Closing recommendation followed:** "v0.4 can ship after a single convergence pass." Round cap hit at round 3 / max 3; user authorized post-cap line-edit pass to produce v0.4 — text-pinning across §1.1 / §4.3 / §6.2.1 / §6.3 / §6.3.4 / §6.3.5 + 5-site `[FIX-SL §4.8.6]` citation re-anchor + engine→FileStore concept-mechanism specification (§4.3 + §7.6) + 2 editorial nits (§6.1.4 preamble + cancellation-table durability column). No algorithm changes; no contract additions; no test seam restructures.

**Round 1 + round 2 verification:** all 4 round-1 root causes and both round-2 root causes verified against v0.3 prose by Opus round-3 baseline-check. **Zero full regressions.** Three of six root causes carried *partial* local issues (RC#4 durability axis; RC2#1 §4.3 collateral; RC2#2 §6.2.1 consumer wording) — all line-edit-class, all addressed by this v0.4 pass.

**Round 3 root cause addressed:**
- **#1 — The round-2 §6.3 single-log rewrite landed correctly inside §6.3 but the convergence pass did not sweep the surrounding sections (§4.3 public-header sketch, §6.2.1 phase-1 in-flight list, §6.3 / §6.3.5 / §1.1 durability-of-rename wording) for collateral, leaving v0.2-shape text in places that quote or consume the v0.3 contract.** Same defect class as 2c v1.2's `get_string<Tag>` regression (round-2 fix replaced one wrong sketch in one site, missed the same shape elsewhere). **Fix:** v0.4 sweeps four sections to align with the §6.3 / §6.3.4 v0.3 algorithm. (1) §4.3 public-header sketch (line 491 + line 534): "single file per session" wording; `sender__target.log` filename example. (2) §6.3 / §6.3.5 / §1.1 durability-of-rename wording: drop "uses no directory-fsync primitive" / "optional Linux dir-fsync" / "Windows rename is durable on success"; pin Linux dir-fsync after rename as **mandatory** per POSIX `rename(2)`; pin Windows `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` as **mandatory**. (3) §6.2.1 phase-1 in-flight list: drop `flush_for_session_close()` from the cancellable list; mention it separately as runs-to-completion outside the phase-1 child timeout. (4) §6.3 (`Cross-filesystem note`): explicit statement that `<...>.log.reset.tmp` is in the same directory as the live log so `rename` cannot fail with cross-device `EXDEV`.

**Per-finding resolution:**

| Finding | Severity | Verdict | Resolution | Section(s) edited |
|---|---|---|---|---|
| C-R3-P1-1 (stale per-direction log prose survives the single-log rewrite — §4.3 line 491 "single file per direction per session"; §4.3 line 534 `sender__target.<dir>.log`) | P1 | **CONFIRM @ P1** — round-3 root cause #1 | §4.3 `FileStorePolicy` block-comment rewritten to "single file per session; every store() appends a record carrying its `dir` discriminator (§6.3.1)"; §4.3 `FileStore::Config` filename-comment example changed from `sender__target.<dir>.log` to `sender__target.log`. The grep-style convergence check (`<dir>.log`, "per direction per session", `<direction>.log` outside Appendix C historical text) returns zero hits in the active sections of v0.4. | §4.3 (`FileStorePolicy` enum block-comment, `FileStore::Config::sender_comp_id`/`target_comp_id` field comment) |
| C-R3-P1-2 (`reset()` durability primitives contradict the reset contract on both Linux and Windows — §6.3 line 932 + §1.1 line 33 + §6.3.5 line 1000 say "no/optional dir-fsync"; §6.3.4 step 5 + §6.3.5 line 997 mandate it; §6.3.5 claims NTFS journaling makes `MoveFileExW` durable without `MOVEFILE_WRITE_THROUGH`) | P1 | **CONFIRM @ P1** — round-3 root cause #1 | Pin one canonical durability contract across all sites. **Linux**: `rename(2)` is atomic on the same FS, but durability requires `fsync` on the parent directory (mandatory, not optional, per POSIX `rename(2)` man page). **Windows**: `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` is required for durability — NTFS journaling alone does NOT durably commit the rename until the user-mode write-through flag is set. Sites pinned: §1.1 (root-cause-#4 framing), §6.3 (preamble bullet list + new Cross-filesystem note), §6.3.4 step 4 (Windows mandatory `MOVEFILE_WRITE_THROUGH`) + step 5 (Linux mandatory parent-dir `fsync`), §6.3.5 platform-portability table (split rename row into atomicity-primitive + durability-primitive rows; rewrite trailing Atomicity-vs-durability paragraph), §6.3.4 Property paragraph (success-return implies durability). The v0.3 "no directory-fsync primitive" / "optional Linux dir-fsync" / "Windows rename is durable on NTFS journaling alone" wording is retired. | §1.1, §6.3, §6.3.4 (steps 4 + 5; Property paragraph), §6.3.5 (table + trailing paragraph) |
| C-R3-P1-3 (`flush_for_session_close()` cancellation semantics are contradictory — §4.3 / Appendix D §D.2: "runs to completion"; §6.2.1: "in-flight ops complete with `store_cancelled`" includes the hook) | P1 | **CONFIRM @ P1** — round-3 root cause #1 | Picked option A (Codex's counter-proposal): the hook is part of graceful phase 1 but is **NOT** under the child timeout cancellation slot — it runs to completion as the durability seam honouring `[FIX-SL §4.5]`. Sites pinned with matching wording: §4.3 `flush_for_session_close()` block-comment (extended cancellation/error paragraph stating runs-to-completion under graceful Logout; cancellation_type::total under `Session::close(terminal)` short-circuits and may leave on-disk log unflushed; `store_cancelled` ONLY in forced-cancellation path); §6.2.1 step 1 (drop `flush_for_session_close()` from cancellable in-flight list; mention separately as runs-to-completion outside phase-1 child timeout); cross-references between §4.3 ↔ §6.2.1 ↔ Appendix D §D.2. Appendix D §D.2 contract paragraph already carries the "runs to completion / store_io_failure" wording — unchanged. | §4.3 (`flush_for_session_close()` block-comment), §6.2.1 (step 1) |
| C-R3-P2-1 (`[FIX-SL §4.8.6]` is still used for the wrong reset mechanism — coverage-index says §4.8.6 is "Sequence reset (hard reset, GapFillFlag=N)"; `ResetSeqNumFlag(141)` lives at §4.4.2 / §4.4.3) | P2 | **CONFIRM @ P2** | 5-site re-anchor: (1) status block `Cites:` line — added `[FIX-SL §4.4.2]` + `[FIX-SL §4.4.3]` + retitled `[FIX-SL §4.8.6]` to the coverage-index form ("Sequence reset (hard reset, GapFillFlag=N)"); (2) §6.7 `store_seqnum_overflow` row — re-anchored from `[FIX-SL §4.8.6]` to `[FIX-SL §4.4.2]` / `[FIX-SL §4.4.3]` for the `ResetSeqNumFlag(141)=Y` Logon recovery path; (3) Appendix B — added `[FIX-SL §4.4.2]` / `[FIX-SL §4.4.3]` entries (matching coverage-index rows 60 / 61 verbatim) and retitled `[FIX-SL §4.8.6]` (matching coverage-index row 83 verbatim); (4) §4.1 method 4 docstring — `[FIX-SL §4.8.6]` citation kept (the `reset()` operation is a hard reset; `[FIX-SL §4.8.6]` per the coverage-index retitle now correctly points at `SequenceReset-Reset` (35=4, 123=N) wire-message hard-reset semantics, which is exactly what `reset()` does); (5) round-1 Appendix C entry's `[FIX-SL §4.8.6]` citation list-row is preserved verbatim (round-1 entries kept verbatim per cumulative-log discipline; the §6.7 row's substantive citation is what governs implementer reading). | status block (`Cites:`), §6.7 (`store_seqnum_overflow` row), Appendix B |
| C-R3-P3-1 (Appendix C row C-R2-P1-1 claims §6.1.4's preamble has a durable-before-transmit explanation that isn't there) | P3 | **CONFIRM @ P3** | Picked option (a) from the round-3 review — added the promised one-sentence note to §6.1.4's preamble, citing the round-1 Opus disagreement reasoning + naming `durable_after_transmit` as out-of-scope for v1.0. The Appendix C row C-R2-P1-1 prose at line 1315 / 1333 now matches what §6.1.4 actually carries. (Option (b) — re-anchoring Appendix C — was rejected per the round-3 review; the explanation belongs in §6.1.4 anyway.) | §6.1.4 (preamble note added) |
| N3-P2-1 (Opus new — `flush_for_session_close()` "friend mechanism" is unspecified between RTTI / virtual-on-base / friend-decl-on-FileStore / concept-shaped) | P2 (NEW) | Independent line-edit | Picked the recommended option: **non-virtual customization point on `FileStore` only, dispatched via concept-shaped `requires { store.flush_for_session_close(); }` from `Session`** (concept name `fixpp::session::detail::has_flush_for_session_close`). The engine retains the factory-type tag at session open and uses that to gate a known-safe `static_cast<FileStore*>` on the unique_ptr's stored pointer when the factory was `FileStoreFactory`; for any other factory, the concept's `requires` clause is not satisfied and the dispatch is a compile-time no-op. `MemoryStore` does NOT define `flush_for_session_close()` — the concept fails the requires-expression. The `[const §XIV.2]` ≤5 cap on `MessageStore` stays at 4 (the customization point lives only on `FileStore`, not on the base), no RTTI overhead on the close path, no `dynamic_cast`, and custom `MessageStore` impls inherit the no-op default for free without having to define the method. Pinned at §4.3 (block-comment for `flush_for_session_close()` extended with the concept declaration + dispatch shape) and §7.6 (Phase-4 hand-off rewritten to spell out the concept-shaped dispatch). Appendix D §D.2 contract paragraph already correctly says the engine reaches the concrete `FileStore` "via the session's stored `unique_ptr<MessageStore>` through a friend mechanism" — the §4.3 / §7.6 specification of that mechanism is owned by 2e itself, so no third Appendix D drop-in is needed (the Phase-4 session-module spec consumes the §7.6 specification at its sign-off; that spec doesn't exist yet). | §4.3 (`flush_for_session_close()` block-comment extended), §7.6 (Phase-4 hand-off rewritten) |
| N3-P3-1 (Opus new — §6.1.4 cancellation table doesn't surface durability implication of the after-linearization branch) | P3 (NEW) | Editorial line-edit | Added a fourth column ("Durability implication of the after-linearisation branch") to the §6.1.4 cancellation table: `store` row — frame DURABLE on disk, `store_cancelled` NOT surfaced because the frame did persist; `next_seqnum` row — counter DURABLE under `commit_per_message`; `reset` row — reset state DURABLE (FileStore: linearisation point IS the rename; durability primitive per §6.3.5); `retrieve` row — n/a (read path). Plus a "Linearisation-as-durability footnote" added immediately after the linearisation-point list explicitly stating that "linearisation point" for `FileStore` methods means "the on-disk state has transitioned to the new value AND the durability primitive has returned success" — cancellation that wins after the linearisation point cannot un-do durability. | §6.1.4 (cancellation table 3-column → 4-column; new footnote after linearisation-point list) |

**Disagreements:** None. All 5 Codex round-3 findings confirmed at rated severity (zero escalations, zero disagreements per Opus round-3 judgement).

**New sibling-doc tensions surfaced by the round-3 rewrite:** None. The engine→FileStore concept mechanism per N3-P2-1 is owned by 2e itself (the concept lives in `fixpp::session::detail`); `Session` is owned by the Phase-4 session-module spec which doesn't exist yet, so no third Appendix D drop-in is needed (Phase-4 will consume §7.6's specification at its own sign-off). `[2d §4.7]` Appendix D §D.2 contract paragraph is unchanged — it already correctly describes the engine's reach mechanism in `[arch §5.6]`-compatible language.

**Net effect summary:**

Sections **line-edited** (no structural rewrites): status block (status + convergence-log pointer + `Cites:` line for FIX-SL re-anchor); §1.1 (durability-primitive wording on RC#4 framing); §4.3 (`FileStorePolicy` block-comment + `FileStore::Config` filename comment + `flush_for_session_close()` block-comment with concept-mechanism specification); §6.1.4 (preamble note + cancellation table 3-col → 4-col + linearisation-as-durability footnote); §6.2.1 step 1 (drop `flush_for_session_close()` from cancellable list; mention as runs-to-completion); §6.3 (preamble bullet list + new Cross-filesystem note); §6.3.4 steps 4 + 5 + Property paragraph (durability primitives mandatory on both platforms); §6.3.5 (platform-portability table split rename row into atomicity + durability rows; trailing paragraph rewritten); §6.7 (`store_seqnum_overflow` row FIX-SL re-anchor); §7.6 Phase-4 hand-off (engine→FileStore concept-mechanism specified); Appendix B (`[FIX-SL §4.4.2]` + `[FIX-SL §4.4.3]` + `[FIX-SL §4.5]` added; `[FIX-SL §4.8.6]` retitled to coverage-index form). Sections **rewritten** (vs line-edited): none — every change is text-pinning class.

**Citation re-anchor count: 5 sites.** (1) status block `Cites:` line; (2) §6.7 `store_seqnum_overflow` row; (3) Appendix B (`[FIX-SL §4.8.6]` retitle + `[FIX-SL §4.4.2]` + `[FIX-SL §4.4.3]` additions); (4) §4.1 method 4 docstring `[FIX-SL §4.8.6]` retained (the `reset()` operation IS a hard reset; the coverage-index retitle propagates by reference); (5) round-1 Appendix C entry `[FIX-SL §4.8.6]` annotation preserved verbatim per cumulative-log discipline.

**§9 test seam count: unchanged at 21.** No seam additions or retirements. Seam #19 (`flush_for_session_close()` graceful-close drain) pass criteria are now consistent with the §6.2.1 / §4.3 contract pin (option A: hook runs to completion; the seam's "verify all 32 frames byte-identical" assertion lands cleanly under the v0.4 contract). Seams #2 / #3 / #10 (FileStore crash-survival / portable torn-write / atomic-rename `reset()`) verify against the mandatory-fsync contract and the Windows mandatory `MOVEFILE_WRITE_THROUGH` contract per the v0.4 §6.3.5 platform table.

**§6.7 error variant count: unchanged at 10.** No additions or retirements.

**Appendix D drop-ins: unchanged.** §D.1 (factory `unique_ptr` byte-exact diff form per Opus N2-P3-1) and §D.2 (graceful-close flush hook on `[2d §4.7]`) are byte-identical to v0.3 — both already correct in v0.3; the §6.2.1 / §4.3 contract-pin in v0.4 makes §D.2's runs-to-completion paragraph internally consistent with its consumer.

**Round-1 + round-2 root-cause regression check:** zero full regressions; three partial local issues (RC#4 durability axis; RC2#1 §4.3 collateral; RC2#2 §6.2.1 consumer wording) all addressed by the v0.4 line-edits; the v1.0 spine — 4-pure-virtual `MessageStore`, awaitable visitor, single-log-per-session on-disk shape, atomic-rename `reset()`, exclusive `async_mutex`, `commit_per_message` default, the §3.1 inherited-primitives table, the §6.3.5 platform-portability table (durability-row updated), the 21-seam list, the 10-variant §6.7 errors table, the two Appendix D drop-ins — survives unchanged.

---

### v0.2 → v0.3 (Gate A round 2 converged)

Date: 2026-05-08. Source reviews: `research/reviews/codex_2e_2_msgstore_review.md` (5 P1 / 3 P2 / 1 P3) and `research/reviews/opus_2e_2_msgstore_adversarial_review.md` (judges Codex round 2, escalates one Codex P2 to P1 and disagrees on one Codex P1; adds 0 new P1 + 3 new P2 + 2 new P3; combined post-judging tally **5 P1 / 5 P2 / 3 P3, 2 root causes**; concludes "v0.3 can ship after a single convergence pass" — line-edits + 2 root-cause-fixes class). Opus's judgements on Codex findings were binding for this pass.

**Closing recommendation followed:** "v0.3 can ship after a single convergence pass — the spine is right, this is line-edits + 2 root-cause-fixes class."

#### Round 2 — root causes addressed

**Round-2 root cause #1 — On-disk atomicity is asserted but the algorithm doesn't extend to the §6.3.1 file-pair shape (Codex C-R2-P1-2).** v0.2 §6.3.1 declared **two log files per session** (one per direction: `<sender>__<target>.<direction>.log`); §6.3.4 described truncating "the log" singular and claimed truncate-then-flush atomicity. The atomicity argument held for one file but did not extend to two: a crash after `ftruncate(outbound.log)` + `fdatasync` but before `ftruncate(inbound.log)` lands a restart with empty outbound + intact inbound + ambiguous counters state. **Round-2 fix:** v0.3 collapses the on-disk shape to **one log file per session** (`<sender>__<target>.log`) with per-record `record_kind ∈ { frame, counters, sentinel }` and per-record `dir ∈ { inbound, outbound, n_a }` discriminator fields in the record header. The single-file shape is simpler than the alternative two-files-plus-manifest shape (rejected — more cut points to crash-test, larger code surface). Additionally, `reset()` is rewritten from "truncate-in-place + sentinel-write + flush" to **atomic-rename**: prepare `<...>.log.reset.tmp` with sentinel + counters records, `fdatasync` the tmp, atomically `rename` over the live log (POSIX-atomic on the same filesystem; Windows `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`), then (Linux) `fsync` the parent directory for rename-durability. The rename is the linearisation point; v0.2's truncate-then-sentinel-write window is eliminated. Sections edited: §1.1 (root-cause #4 framing), §3 (Tier-2 implication wording), §6.3 / §6.3.1 / §6.3.2 / §6.3.3 / §6.3.4 / §6.3.5 (entire §6.3 rewritten), §6.1.4 (FileStore::reset linearisation point updated to "the rename"), §6.6 (FileStore::reset row reworded), §10 Q4 (file path updated). New §9 seam #10 ("`FileStore::reset()` atomic-rename test") replaces v0.2's truncate-then-sentinel crash test.

**Round-2 root cause #2 — Sibling-doc cross-edits owed by 2e are declared but not applied; the 2d-side hooks 2e cites do not exist in 2d v0.4 (Codex C-R2-P1-4 + C-R2-P1-5).** v0.2 leaned on two sibling-doc edits not present in signed-off 2d v0.4: (i) `SessionConfig::store_factory` field type change (`shared_ptr → unique_ptr`), declared in v0.2 Appendix D but not yet applied to 2d (C-R2-P1-4); (ii) `[2d §4.7]` graceful pre-phase-1 store-flush hook, **not declared anywhere**, just cited inline at v0.2 §7.6 and the status block as if 2d already carried the contract — actual `[2d §4.7]` v0.4 lines 798–809 has no such row in the per-mode effect table (C-R2-P1-5). **Round-2 fix:** v0.3 lands two Appendix D drop-ins, both queued for orchestrator-apply at 2e sign-off (per 2d v0.4 / 2c v1.3 sibling-doc-edit precedent, where the orchestrator amends the sibling at THIS doc's sign-off commit so the parent commit picks up both the new doc + the amended sibling): **Appendix D §D.1** — refined factory-ownership amendment with byte-exact diff form per Opus N2-P3-1 (replaces v0.2's approximate post-edit form); **Appendix D §D.2** — NEW, graceful pre-phase-1 store-flush hook adds one row to `[2d §4.7]`'s effect table + a one-paragraph contract on the hook's cancellation/error semantics. v0.2's phantom `[2d §4.7]` citation in §7.6 is replaced with the explicit cross-doc-edit declaration. §10 Q5 closure strengthened ("the engine-internal `FileStore::flush_for_session_close()` hook lives at `[2d §4.7]` per Appendix D §D.2"); §10 Q11 disposition strengthened from "sibling-doc amendment owed by 2d at 2e sign-off" to "orchestrator applies Appendix D §D.1 + §D.2 at 2e sign-off as part of the commit." Sections edited: status block, §3 (`[2d §4.7]` annotation + closing paragraph), §3.1 hand-off-gates list, §4.3 (`flush_for_session_close()` block-comment expanded with cross-doc hook contract reference), §4.4 (factory ownership), §7.6 (Phase-4 hand-off rewritten), §10 Q5 + Q11, §11 hand-off (catalogue + amendments-owed list), Appendix D §D.1 (refined to byte-exact diff form per Opus N2-P3-1) + Appendix D §D.2 (NEW). New §9 seam #19 ("`FileStore::flush_for_session_close()` graceful-close drain") tests the round-2 RC#2 hook contract.

#### Per-finding resolution

| Finding | Severity | Verdict | Resolution | Section(s) edited |
|---|---|---|---|---|
| C-R2-P1-1 (outbound cancellation persists frame that never reached the wire — Codex's revised proposal: `store(outbound)` after `async_write` success) | P1 | **DISAGREE** | Opus round 1 explicitly endorsed the `toApp → Writer::commit → store(seq, committed_span, outbound) → transport.async_write` ordering as the durable-before-transmit shape that protects `[const §XV.15]` (no silent loss of session messages). Codex's revised counter-proposal (store after async_write success) creates a worse partial-write hole: a partial wire write whose tail bytes were dropped before `async_write` completed leaves the peer with a frame for which we have no journal entry; on reconnect, the peer can `ResendRequest` a seqnum we cannot honour. v0.3's ordering is the durably-storable shape vendor stores (fix8 with sync-mode, OnixS) ship for compliance environments; cancelled `async_write` after `store()` linearisation leaves a persisted frame at seq N + counter at N+1 + peer at expected-in N, recoverable via `ResendRequest` (peer ask, we honour) or `SequenceReset-GapFill` (we admit) — both inside `[FIX-SL §4.8]`. Peer-resend is a **protocol** primitive, not a transport one; v0.3's contract delivers durably-storable bytes to the protocol layer regardless of whether the transport flushes them. **No change needed.** v0.3 §6.1.4's preamble note documents why post-store-pre-write cancellation is the durably-stored shape and names `durable_after_transmit` mode as out-of-scope. | §6.1.4 (preamble note clarifying the taxonomy choice) |
| C-R2-P1-2 (FileStore reset not atomic across directions and counters) | P1 | **CONFIRM @ P1** — root-2 root cause #1 | v0.3 collapses §6.3.1 to one log file per session with per-record direction tags; rewrites §6.3.4 reset as atomic-rename; updates §6.1.4 FileStore::reset linearisation point to "the rename"; updates §6.6 reset row component breakdown; new §9 seam #10 covers the round-2 algorithm crash-test cuts. | §1.1, §3, §6.3 (entirely rewritten), §6.1.4, §6.6, §9 seam #10 |
| C-R2-P1-3 (MemoryStore default sizing contradicts its own DoS cap by ~100×) | P1 | **CONFIRM @ P1** | v0.2's defaults (200_000 entries × 256 KiB max_frame_bytes worst-case = ~97.7 GiB ≫ 1 GiB cap) reduced to 10_000 entries per direction; explicit "test/embedded use only" labelling at §1.2 + §4.2; new `capacity_policy` enum (`bounded(default = 10_000)` / `unbounded`); `drop_oldest` explicitly NOT exposed (banned per `[const §XV.15]`). Worst-case at the new defaults is ~5 GiB — explicitly above the default 1 GiB cap, by design (operator must opt into a higher cap or reduce `max_frame_bytes`). Production deployments MUST use FileStore. | §1.2, §4.2, §9 seam #4 (capacity-exhaustion test extended for `unbounded`), §9 seam #15 (zero-allocator-calls test scoped to `bounded`), §9 seam #13 (latency test capacity scaled from 200_000 to 10_000) |
| C-R2-P1-4 (factory ownership cross-doc collision) | P1 | **CONFIRM @ P1** — round-2 root cause #2 | Verified: 2d v0.4 §4.5 line 534 reads `std::shared_ptr<MessageStoreFactory> store_factory;` while 2e §4.4 / §3.1 / §7.3 cite `unique_ptr`. Resolution: keep 2e at `unique_ptr` (matches `[arch §5.6]`'s frozen-at-open + move-only semantics; `unique_ptr<Factory>` is a configuration record, not a runtime live store) and apply Appendix D §D.1 drop-in at 2e sign-off via the orchestrator (per 2d v0.4 / 2c v1.3 precedent). v0.3 strengthens §10 Q11 disposition from "sibling-doc amendment owed by 2d" to "orchestrator applies Appendix D §D.1 + §D.2 at 2e sign-off." Appendix D §D.1 refined to byte-exact diff form per Opus N2-P3-1. | §3 (`[2d §4.5]` annotation), §3.1 (factory-ownership row + hand-off-gates list), §10 Q11, §11 hand-off, Appendix D §D.1 |
| C-R2-P1-5 (FileStore close-flush hook cites a 2d phase that does not exist) | P1 | **CONFIRM @ P1** — round-2 root cause #2 | Verified: `[2d §4.7]` v0.4 per-mode effect table (lines 798–809) has 9 affected ops; `MessageStore::write` (line 808) is the only MessageStore-related row; **there is no `FileStore::flush_for_session_close()` row**. v0.2 §7.6 + status block paraphrased a phantom contract. Resolution: declare Appendix D §D.2 drop-in NEW (one new row + one-paragraph contract on cancellation/error semantics — per graceful: runs as graceful pre-phase-1 store-durability flush; per graceful phase 2: n/a (already drained); per terminal: not invoked). v0.3 §7.6 rewritten to point at the cross-doc edit; status block updated. Picked option (1) from the round-2 review (Phase-4 / drop-the-hook were rejected — the hook is needed for `commit_batched`/`commit_interval` durability completeness on `Session::close(graceful)`). | Status block, §3 (`[2d §4.7]` annotation), §4.3 (`flush_for_session_close()` block-comment expanded), §7.6 (rewritten), §10 Q5 (closure strengthened), Appendix D §D.2 (NEW), §9 seam #19 (NEW) |
| C-R2-P2-1 → P1 (Path A overclaims compile-time enforcement — escalated by Opus to P1) | P1 (escalated) | **CONFIRM @ P1 + ESCALATE** | The four "compile-time-checked" Path A conditions are user-attested traits (`reenters_session`, `performs_sync_disk_io`, `spawns_threads`) and a behavioural deadline (`polls_cancel_token_within`), none statically checkable. A user who attests `reenters_session = false` and then re-enters Session compiles cleanly and hits runtime deadlock. Worse, v0.2's "curated supported impls" list named the QuickFIX/J JDBC stub, which performs synchronous JDBC blocking calls (Hazard 3 by §4.8.A.1's own list). Picked option (a) from the round-2 review: **drop the templated `quickfix_thread_pool_adapter` / `sync_message_store_adapter` from v1.0 and ship Path B only** — the user authorized Path B as a v1.0 outcome in round 1, and the round-1 N4 finding (Path A subset is shippable) was Opus-introduced; round 2 rolls it back. v0.3 retires §4.8.B's wrapper, the `static_assert` chain, the `adapter_traits<Sync>` user-asserted concept, the `store_shim_timeout` error variant, and §9 seam #12 ("Path A subset acceptance test"). The five hazards in §4.8.A.1 stand as the "why we don't ship a generic sync-store adapter" justification. | Status block (Headers list trimmed), §1 goal 7, §1.1, §2 (non-goals), §3 (catalogue rows owned), §4.8.A (verdict reworded), §4.8.A.1 (closing sentence reworded), §4.8.B (entirely retired), §6.7 (`store_shim_timeout` removed; tally updated 11 → 10), §9 (seam #12 retired; seam-count preamble updated), §11 hand-off, Appendix A (OSS-002 row updated) |
| C-R2-P2-2 (FileStore throughput and latency targets inconsistent) | P2 | **CONFIRM @ P2** | v0.2 §1.2 said "≤ 10⁵ frames/s", §4.3.1 said "~10⁴ frames/s", §6.6 implied ~4_000/s from the 250 µs per-call ceiling — three different numbers for the same operation. v0.3 picks **≈ 10⁴ frames/s per-session under `commit_per_message`** as the realistic published number (consistent with `[2d §6.3]`'s 150 µs `fdatasync` floor on commodity NVMe); §6.6's 250 µs ceiling becomes the per-call latency budget consistent with that throughput; §1.2's 10⁵ frames/s line is dropped (the engine-aggregate scaling argument lives in `[2d §6.4]`'s backpressure narrative, not here). | §1.2, §4.3 (`commit_per_message` enum docstring), §4.3.1, §6.6 |
| C-R2-P2-3 (`store_visitor_aborted` mapped as cancellation) | P2 | **CONFIRM @ P2** | v0.2 §6.7 coalesced `store_cancelled` and `store_visitor_aborted` to `FIXPP_ERR_CANCELLED`, but visitor abort during replay can represent retransmit/audit/consumer failure, not cancellation (the §4.5 contract makes visitor errors the way replay consumers stop asynchronous recovery). v0.3 splits the cancellation group: `FIXPP_ERR_CANCELLED` reserves cancellation outcome (`store_cancelled` only); new `FIXPP_ERR_STORE_VISITOR` group for `store_visitor_aborted` (and any future visitor-side error variants). The §4.5 `abort_error()` virtual return is documented as the source-of-truth for the visitor's typed error code; `store_visitor_aborted` is the wrapper-default when the visitor doesn't override. | §6.7 (variant changes paragraph + C-ABI mapping + `store_visitor_aborted` row scope clarified) |
| C-R2-P3-1 (Appendix B has non-exact reference labels) | P3 | **CONFIRM @ P3** | Per `[const §VI.5]` exact-title rule. `[SYN §3.2 Q7]` retitled from "MessageStore replication shape" → "MessageStore async API + QuickFIX-compat shim feasibility" (matches the actual SYN row). `[FIX-SL §4.8.6]` retitled from "Hard reset semantics" → "ResetSeqNumFlag(141)=Y Logon — hard-reset Logon flow" (matches the actual FIX-SL §4.8.6 title). | Appendix B |
| N2-P1-1 (Opus new — none) | — | — | None at P1; Opus added only P2 + P3 findings in round 2. | — |
| N2-P2-1 (Opus new — `[const §VIII.5]` no-terminate citation overshoots) | P2 (NEW) | Independent line-edit | The actual no-terminate behaviour is implied by `[arch §5.3]`'s `expected_t<T>` error model + operationalised by `[2a §4.2]`'s `trap_throw` pattern; `[const §VIII.5]` is allocator policy on the hot path. v0.3 §6.1.2 reworded: PMR throw paths route through `trap_throw` per `[2a §4.2]` (the no-terminate-on-PMR-throw mechanism); allocator policy follows `[const §VIII.5]`. §9 seam #16 (PMR poison) prose updated to cite `[2a §4.2]` + `[arch §5.3]` for the no-terminate property, not `[const §VIII.5]`. | §6.1.2, §9 seam #16 |
| N2-P2-2 (Opus new — `next_seqnum`'s "atomicity" claim collides with §6.4 mutex serialisation) | P2 (NEW) | Independent line-edit | Three v0.2 statements were mutually inconsistent: §4.1 method 3 docstring "OPTIONALLY incrementing it atomically", §6.1.4 "linearisation point: the atomic fetch-add", and §6.4 "all four methods take the mutex". v0.3 picks "mutex always" (option (1) in the round-2 review): §4.1 method 3 docstring drops "atomic"; §6.1.4 changes the MemoryStore::next_seqnum linearisation point from "the atomic fetch-add" to "the counter increment under the writer mutex"; §6.6 row 3 (next_seqnum read-only) ceiling lifts from ≤ 30 ns to ≤ 50 ns reflecting `async_mutex` acquire/release on the uncontended path. The §6.4 "all four take the mutex" rule simplifies impl-side reasoning; counter is read on the session strand (uncontended); the 50 ns ceiling is well within `[const §VIII.5]` hot-path budget. | §4.1 method 3 docstring, §6.1.4 (linearisation point list), §6.4 preamble, §6.6 (next_seqnum row) |
| N2-P2-3 (Opus new — `store_seqnum_out_of_order` error variant has no detection mechanism specified) | P2 (NEW) | Independent line-edit | v0.2 declared the variant in §6.7 but didn't say where the check happens. v0.3 §4.1 method 1 docstring adds: "the store verifies `seq == next_seqnum(dir, false)` inside its mutexed critical section before performing the slab memcpy + entry write; on mismatch, returns `store_seqnum_out_of_order` without state mutation." §6.6 row 1 latency budget adds "atomic compare ~1 ns" (negligible vs the existing ~50–75 ns sum). §6.1.4 linearisation-point list extended with the verification-failure case. §6.7 row source-section description extended with "Detected inside the writer-mutex critical section". New §9 seam #20 ("`store_seqnum_out_of_order` detection") covers the contract. | §4.1 method 1 docstring, §6.1.4 (linearisation point list — verification step), §6.6 (MemoryStore::store row), §6.7 (`store_seqnum_out_of_order` row), §9 seam #20 (NEW) |
| N2-P3-1 (Opus new — Appendix D §D.1 drop-in not byte-exact) | P3 (NEW) | Editorial line-edit | v0.2 Appendix D §D.1 used a post-edit-form drop-in that didn't exactly match 2d v0.4 §4.5's line 534–535 alignment. v0.3 Appendix D §D.1 rewrites the drop-in as a `diff -u`-style "before / after" form per the 2d v0.4 Appendix D §D.1 / 2c v1.3 Appendix D §3 precedent — quotes the actual 2d v0.4 lines verbatim, then shows the post-edit form, so the orchestrator's apply step is mechanical. | Appendix D §D.1 |
| N2-P3-2 (Opus new — `direct_executor` mode mentioned at §4.2 but never wired through to §6.4 mutex contract) | P3 (NEW) | Editorial line-edit | v0.2 §6.4's "If two strands attempt..." wording was a hold-over from the per-session-strand mode picture; under `direct_executor` mode there are no strands, only attested-already-serialised executors. v0.3 §6.4 adopts 2d v0.4's vocabulary: "If two callers attempt a mutating method on the same instance concurrently from outside the session serialisation domain — a v1.0-invariant violation under both `per_session_strand` and `direct_executor` modes — the second arrival suspends FIFO-fairly..." §4.2 line "defence-in-depth against `direct_executor` mode mis-use" reworded to "defence-in-depth against session-serialisation-domain violations (whether the user's `direct_executor` attestation is incorrect, or the FSM's `per_session_strand` contract is violated)" — aligns 2e's language with 2d v0.4 throughout. | §4.2 (Notes), §6.4 (concurrent-arrival paragraph) |

#### Disagreements

**1 disagreement.** Codex C-R2-P1-1 ("outbound cancellation persists a frame that never reached the wire") — judged **DISAGREE** by Opus. Reasoning above in the per-finding table; net: the v0.2 ordering is the durable-before-transmit shape Opus round 1 explicitly endorsed; Codex's revised counter-proposal would create a worse partial-write hole that violates `[const §XV.15]`. v0.3 §6.1.4 keeps the v0.2 taxonomy and adds one preamble sentence explaining the choice + naming `durable_after_transmit` as out-of-scope.

#### New sibling-doc tensions surfaced by the round-2 rewrite

1. **`[2d §4.7]` per-mode effect table — `FileStore::flush_for_session_close()` row.** v0.2 cited `[2d §4.7]` for this hook as if 2d already carried the contract; the actual `[2d §4.7]` v0.4 has no such row. Round 2 declares the cross-doc edit as Appendix D §D.2 drop-in, applied by the orchestrator at 2e sign-off (per 2d v0.4 / 2c v1.3 precedent). NEW relative to v0.2's Appendix D scope.
2. **Phase-4 session-module spec — `seqnum_t` ownership.** Unchanged from round 1. Forward hand-off; not a tension.

#### Net effect summary

Sections **rewritten** (vs line-edited): §1.2 magnitude domain (capacity-policy + DoS arithmetic + per-session FileStore throughput single number); §4.2 Config + Notes (`capacity_policy` enum + round-2 default sizing + vocabulary alignment with 2d v0.4); §4.8.A.1 closing rationale (§4.8.B retirement justification); §4.8.B (entirely retired in v0.3 — replaced with retirement rationale prose); §6.3 / §6.3.1 / §6.3.2 / §6.3.3 / §6.3.4 / §6.3.5 (entire §6.3 rewritten — single log per session + atomic-rename `reset()`); §6.1.4 (linearisation point list updated for FileStore::reset = "the rename" and for the seq-verification failure case); §6.4 vocabulary alignment (Opus N2-P3-2); §6.7 (variant changes paragraph + `store_visitor_aborted` C-ABI mapping split + `store_shim_timeout` retired + tally 11 → 10); §7.6 Phase-4 hand-off (graceful-close path declares cross-doc edit explicitly); §9 (seam #12 retired; seam #10 rewritten for atomic-rename; seam #19 NEW; seam #20 NEW); Appendix B (`[SYN §3.2 Q7]` and `[FIX-SL §4.8.6]` retitled); Appendix C (this round-2 entry appended); Appendix D §D.1 (refined to byte-exact diff form per N2-P3-1) + Appendix D §D.2 (NEW). Sections **line-edited** (no structural change): status block (status + headers list + convergence-log pointer); §1 goal 7 (Path B-only verdict); §1.1 (root-cause #4 framing for round-2 RC#1); §2 non-goals (Path A wrapper rationale); §3 (`[2d §4.5]` / `[2d §4.7]` annotations + closing paragraph + cross-doc edit declarations); §3.1 (factory-ownership row + hand-off-gates list); §4.1 method 1 + method 3 docstrings (seq-verification mechanism + mutex-always wording); §4.3 (`commit_per_message` docstring single-number throughput; `flush_for_session_close()` block-comment expanded with cross-doc hook contract reference); §4.3.1 (10⁴ frames/s alignment); §6.1.2 (no-terminate citation refined); §6.5 (session_executor wrapper class wording); §6.6 (MemoryStore::store row + next_seqnum row + FileStore::reset row); §10 Q4 + Q5 + Q11; §11 hand-off (catalogue + amendments-owed list); Appendix A (OSS-002 row).

**Codex round-2 P1 escalations / disagreements:** 1 escalation (C-R2-P2-1 → P1 by Opus); 1 disagreement (C-R2-P1-1, Opus DISAGREE — durable-before-transmit shape stands); 4 P1 confirmations (C-R2-P1-2 / C-R2-P1-3 / C-R2-P1-4 / C-R2-P1-5).

**§9 test seam count: v0.2 had 20 → v0.3 has 21** (-1 retired: seam #12 "Path A subset acceptance test" with the §4.8.B retirement; +2 NEW: seam #19 "`FileStore::flush_for_session_close()` graceful-close drain" for round-2 RC#2 + seam #20 "`store_seqnum_out_of_order` detection" for Opus N2-P2-3; seam #10 "Reset semantics + atomicity" rewritten in place for round-2 RC#1's atomic-rename algorithm but keeps its slot; seam #3 "FileStore portable torn-write protection" extended to exercise the round-2 single-log shape; seam #4 "MemoryStore capacity exhaustion" extended for `unbounded` policy; seam #13 "latency regression" capacity scaled from 200_000 to 10_000; seam #15 "zero-allocator-calls" scoped to `bounded` policy; seam #16 "PMR poison" cite refined per N2-P2-1; net seam-count delta 20 → 21, i.e., +1).

**§6.7 error variant count: v0.2 had 11 → v0.3 has 10** (-1: `store_shim_timeout` retired with §4.8.B per Codex C-R2-P2-1 escalation; net -1).

**Sibling-doc cross-references added in v0.3:**
- `[2d §4.4]` (extended to cite `EngineConfig::executor` alongside `clock`).
- `[2d §6.7]` `dispatch_aborted` and `clock_sleeps_cancelled` (named explicitly in §6.7 cancellation-group description, joining `store_cancelled` at the C ABI).
- `[arch §5.3]` (named explicitly in §6.1.2 as the source of the no-terminate-on-PMR-throw model — Opus N2-P2-1 refinement).

**Appendix D drop-ins declared at v0.3 (orchestrator applies at 2e sign-off):**
- §D.1 — `[2d §4.5]` `SessionConfig::store_factory` field type `shared_ptr → unique_ptr` (refined to byte-exact diff form in round 2 per Opus N2-P3-1).
- §D.2 — `[2d §4.7]` per-mode effect table gains `FileStore::flush_for_session_close()` row + a one-paragraph contract on the hook's cancellation/error semantics (NEW in round 2).

---

### v0.1 → v0.2 (Gate A round 1 converged)

Date: 2026-05-08.

**Reviews input:**
- Codex Gate A (7 P1 / 4 P2 / 3 P3): `research/reviews/codex_2e_msgstore_review.md`
- Opus adversarial (post-judging 11 P1 / 7 P2 / 5 P3, 4 root causes — outbound-store-call-sequence, recovery-visitor-async, supporting-primitive-ownership, FileStore-disk-algorithm): `research/reviews/opus_2e_msgstore_adversarial_review.md`

**Closing recommendation followed:** "Needs significant rewrite — 4 overlapping root causes; v0.2 should restart from a corrected mental model."

**Round 1 root causes addressed:**

- **#1 — Outbound store-call sequence.** v0.1 placed `store(outbound)` between `toApp` and `Writer::commit`, persisting bytes that are NOT the post-commit frame (BodyLength + CheckSum unfinished per `[2b §4.5]`). v0.2 rewrites the call ordering across the doc: outbound dispatch is `toApp → Writer::commit() → store(seq, committed_span, outbound) → transport.async_write`; inbound dispatch is `Framer::feed → Parser → store → fromApp/fromAdmin`. Stated in §1 goal 6, §6.1 (allocation/threading subsections + the cancellation result-contract subsection §6.1.4), §7.1 (the wire integration paragraph), §7.6 (the Phase-4 hand-off). New §6.1.4 cancellation-result-contract per method (collapses Codex C-P2-8 escalated to P1 — same root). Cancellation taxonomy: cancellation before linearisation = `expected_t::unexpected{store_cancelled}` (mirrors `[2d §6.5]`'s `dispatch_aborted`); cancellation after linearisation = normal completion. New error variant `store_cancelled` in §6.7. New seam **"Outbound store-after-commit byte-equality test"** (#7) covers a BodyLength digit-count growth at commit + replay byte-equality. New seam **"Cancellation-result-contract test"** (#6) covers per-method cancellation taxonomy. Ties N7 (lifetime/shutdown ordering): §6.2.1 makes shutdown ordering explicit (cancel → drain → destroy store → release session_arena → ~Session).

- **#2 — Recovery-visitor sync over async surface.** v0.1's `retrieve_visitor::on_frame` returned a synchronous `visit_result`, making async retransmit (`co_await transport.async_write` per `[FIX-SL §4.8.3]`) impossible inside the visitor without span escape, blocking I/O, or hidden copies. v0.2 makes the visitor **awaitable** (`asio::awaitable<expected_t<visit_result>> on_frame(seqnum_t, std::span<const std::byte>)`). The store keeps the span stable across the visitor's `co_await`; the store does NOT hold the writer mutex across visitor `co_await` (so a tee-chain visitor can recursively call store ops without deadlock); per-frame allocation is the visitor's caller-PMR responsibility; cancellation flows through the visitor's awaitable per `[2d §6.5]`. The chosen shape is the awaitable visitor (option (b) from the round-1 guidance) over the `async_generator` pull (option (a)) because the awaitable visitor survives translation to non-coroutine consumers (2i C-ABI; SWIG/Python `async def`). Sections §4.1 method 2, §4.5, §6.2 (visitor span lifetime row), §6.4 (mutex hold rule), §7.6, §6.7. Two new seams: **"Awaitable visitor + span lifetime"** (#9) replaces v0.1's static visitor #8 and covers root cause #2's stable-span / visitor-suspension property; **"PMR poison on retrieve-recovery path"** (#17) replaces v0.1's wishful seam #15 per N10. N12 (dead `direction_t` param on `on_frame`) folded into the rewrite.

- **#3 — Supporting-primitive ownership.** v0.1 leaned on `seqnum_t`, `async_shared_mutex` (RW-mutex), `std::recursive_mutex` transitional adapter, and the `MessageStoreFactory`'s `shared_ptr<MessageStore>` return shape — each without an owner-doc citation, and at least three contradicting binding documents. v0.2 publishes a **§3.1 inherited-primitives exhaustive list** that names every primitive 2e leans on with its owning section + verbatim citation: `seqnum_t` (owner = Phase-4 session-module spec; placeholder per `[FIX-SL §4.1]`; cross-doc handoff in §10 Q9 — Codex P1-3 fix); `async_mutex` only (no `async_shared_mutex` per `[SYN §3.2 Q6b]`; no `std::recursive_mutex` per `[const §XV.9]`; 2f sign-off is hard hand-off gate — Codex P1-7 fix); the FIFO-fair concurrent-writer behaviour per `[SYN §3.2 Q8]` (Codex P1-5 fix; `store_concurrent_writer` removed from §6.7); factory ownership = `unique_ptr` per `[arch §5.6]` mid-session-swap ban (N1; cross-doc edit owed by `[2d §4.5]` flagged in §10 Q11 + Appendix D).

- **#4 — FileStore on-disk algorithm.** v0.1 used `fsync` + directory `fsync` (Linux-specific; Windows has no portable directory-fsync primitive — `[const §II.3]` Tier 2 needs to work). v0.2 rewrites §6.3 as an **append-only log + truncate-on-startup** scheme: every `store()` appends a record (`[seq | dir | reserved | len | crc32 | bytes | pad]`); restart scans the log, truncates the trailing torn record (CRC mismatch / short read / oversized `len`); `reset()` truncates to header sentinel + flushes. **No directory-fsync primitive is needed.** Default `FileStorePolicy` = `commit_per_message` (slow but correct: `fdatasync`/`FlushFileBuffers` per record); opt-in `commit_batched(N)` and `commit_interval(ms)` document their data-loss windows honestly. The v0.1 "peer ResendRequest re-delivers" claim is removed (Codex P1-6 fix — peer can only request, never re-deliver). New §6.3.5 platform-portability sub-section names every primitive (`pwrite` / `fdatasync` / `ftruncate` / `flock` on Linux; `WriteFile` / `FlushFileBuffers` / `SetEndOfFile` / `LockFileEx` on Windows) and asserts no directory-fsync is required. New seam **"FileStore portable torn-write protection"** (#3) runs on Linux/Clang Tier 1 AND Windows/MSVC Tier 2.

**Per-finding resolution:**

| Finding | Severity | Fix shape | Section(s) touched |
|---|---|---|---|
| Codex P1-1 (outbound store before commit) | P1 | Subsumed under root cause #1. Outbound call ordering rewritten: `toApp → Writer::commit → store(seq, committed_span, outbound) → transport.async_write` per `[2b §4.5]`. New §9 seam **"Outbound store-after-commit byte-equality test"** (#7) covers a BodyLength digit-count growth at commit + replay byte-equality. | §1 goal 6, §4.1 method 1 docstring, §6.1, §7.1, §7.6 |
| Codex P1-2 (`retrieve_visitor` cannot drive async resend; dangling spans) | P1 | Subsumed under root cause #2. Visitor made awaitable (`awaitable<expected_t<visit_result>> on_frame(seqnum_t, span<const byte>)`); span lifetime is the visitor's awaitable, not the call; mutex not held across visitor `co_await`; recursive store ops from the visitor cannot deadlock. | §4.1 method 2, §4.5, §6.2, §6.4, §6.7 (`store_cancelled` ties), §7.6 |
| Codex P1-3 (`seqnum_t` invented under false `[2d §4.5]` citation) | P1 | Subsumed under root cause #3. §4.7 marked PLACEHOLDER; §3.1 inherited-primitives table names Phase-4 session-module spec as owner; §10 Q9 captures the cross-doc handoff. | §1 goal 1, §3.1, §4.7, §10 Q9 |
| Codex P1-4 (FileStore reset not atomic across both directions and counters) | P1 | Subsumed under root cause #4. §6.3.4 reset algorithm is truncate-then-flush + counters-record write + flush; the truncate-flush pair is atomic in the crash-recovery sense; no "old frames + new counters" pathology is reachable. New §9 seam **"Reset semantics + atomicity"** (#10) crash-tests every cut point. | §6.3.1, §6.3.4 |
| Codex P1-5 (concurrent writer 3 different specs) | P1 | Subsumed under root cause #3. v0.1's `store_concurrent_writer` removed from §6.7; mutex semantics are FIFO-fair `async_mutex` per `[SYN §3.2 Q8]`. §9 seam #5 renamed **"FIFO-fair concurrent-writer test"** to assert FIFO serialisation under TSan. | §4.1 method 1, §6.4, §6.7, §9 seam **"FIFO-fair concurrent-writer test"** |
| Codex P1-6 (default FileStore durability text misstates FIX recovery) | P1 | Subsumed under root cause #4. §4.3.1 rewritten: peer ResendRequest cannot re-deliver our outbound; default flipped to `commit_per_message`; data-loss windows on opt-in policies stated explicitly. | §4.3, §4.3.1, §6.3 |
| Codex P1-7 (RW-mutex / `std::recursive_mutex` adapter — primitives 2f does not own; `[const §XV.9]` carve-out attempted) | P1 | Subsumed under root cause #3. RW-mutex / `async_shared_mutex` dropped (`[SYN §3.2 Q6b]`); `std::recursive_mutex` adapter dropped (`[const §XV.9]` no carve-out); 2f sign-off as hard hand-off gate stated in §3.1. | §1 goal 5, §2 (non-goal), §3.1, §6.4, §7.4 |
| Codex P2-8 → P1 (cancellation result contract for `awaitable<expected_t<T>>`) | P1 (escalated) | Subsumed under root cause #1. New §6.1.4 cancellation result-contract subsection: per-method linearisation point + before/after taxonomy; `expected_t::unexpected{store_cancelled}` mirrors `[2d §6.5]`'s `dispatch_aborted`. New `store_cancelled` variant in §6.7. New §9 seam **"Cancellation-result-contract test"** (#6). | §6.1.4, §6.7, §9 seam **"Cancellation-result-contract test"** |
| Codex P2-9 (zero-allocation contradicted by per-message PMR) | P2 | Subsumed under N9. `MemoryStore::Config` adds explicit `max_frame_bytes`; the slot+slab layout is documented in §4.2; `store()` performs zero allocator calls (memcpy + index increment). New §9 seam **"MemoryStore::store performs zero allocator calls"** (#16) replaces v0.1 wishful seam #15. | §1 goal 8, §4.2, §6.1.1, §8 (PMR table), §9 seam **"MemoryStore::store performs zero allocator calls"** |
| Codex P2-10 (coverage handoff misses S-011/S-012/S-013) | P2 | Coverage-index amendment captured in §11 hand-off: line 76 changes from `S-014` to `S-011, S-012, S-013, S-014`. | §11 hand-off |
| Codex P2-11 → P1 (Windows/FileStore platform semantics deferred) | P1 (escalated) | Subsumed under root cause #4. §6.3.5 platform-portability subsection names every primitive (Linux + Windows); algorithm proven not to require directory-fsync. §9 seam **"FileStore portable torn-write protection"** (#3) runs both platforms. | §6.3, §6.3.5, §9 seam **"FileStore portable torn-write protection"**, §10 Q1 closed |
| Codex P3-12 (visitor pure-virtual count wrong) | P3 | §4.5 prose corrected: visitor defines exactly **one** pure-virtual + **one** overridable virtual (matches `on_frame` `= 0` + `abort_error()` with default body). | §4.5 |
| Codex P3-13 (`abort_error()` body not valid C++) | P3 | §4.5 default body rewritten: `return fixpp::core::error{fixpp::core::error_code::store_visitor_aborted};` (compiles per `[2c §6.7]` / `[2d §6.7]` precedent). | §4.5 |
| Codex P3-14 (status block citation `[2b §5]` wrong) | P3 | Status block + §3 cite `[2b §6.4]` (Lifetime contract), `[2b §6.6]` (allocation + view-escape), `[2b §7.4]` (MessageStore + typed-payload persistence raw-frame contract); `[2b §5]` removed (it's "C ABI surface — none"; not load-bearing here). | status block, §3, Appendix B |
| Opus N1 P1 (factory `shared_ptr` vs `[arch §5.6]` `unique_ptr`) | P1 (NEW) | §4.4 `MessageStoreFactory::make()` returns `expected_t<std::unique_ptr<MessageStore>>`; §1 goal 4 + §3.1 inherited-primitives table state the ownership shape; §10 Q11 + Appendix D drop-in flag the sibling-doc edit owed by 2d (`[2d §4.5]` v0.4's `shared_ptr` field type). | §1 goal 4, §3.1, §4.1 (Lifetime block-comment), §4.4, §10 Q11, Appendix D |
| Opus N2 P1 (`flush()` cap trick — no-op on `MemoryStore`, only Session-close on `FileStore`) | P1 (NEW) | `flush()` REMOVED from `MessageStore` interface; v0.2 has 4 pure-virtual methods. Engine-internal `FileStore::flush_for_session_close()` is a non-virtual, non-public method that the engine reaches via friend; signalled from `Session::close(graceful)` per `[2d §4.7]`. | §1 goal 1, §2 (non-goal), §4.1 (no `flush` row), §4.1.1 (rationale), §4.3 (`flush_for_session_close`), §6.6 (no flush ceilings row), §7.6, §10 Q5 closed |
| Opus N3 P1 (concept vs virtual — virtual eats 7.5% of ≤200 ns budget) | P1 (NEW) | §4.1.1 argues virtual stays for 2i C-ABI runtime polymorphism + `unique_ptr<MessageStore>` ownership shape + `[arch §6]` plugin discoverability; §6.6 names ~5–15 ns vtable dispatch in every per-row arithmetic justification. | §1 goal 1, §4.1 (block comment), §4.1.1, §6.6 |
| Opus N4 P1 (Path A subset shippable) | P1 (NEW) | §4.8.B publishes `quickfix_compat::sync_message_store_adapter<UserSync, AdapterPolicy>` behind a four-condition `static_assert` + `adapter_traits<Sync>` user-asserted concept. Conditions: (i) no Session re-entry, (ii) no sync disk I/O, (iii) no spawned threads, (iv) cooperative cancellation token. New `store_shim_timeout` variant in §6.7. New §9 seam **"Path A subset acceptance test"** (#12). The user authorised Path B explicitly as a v1.0 outcome — Path A subset is an enhancement, not a contradiction. | §1 goal 7, §2 (non-goal modified), §4.8 (split A / B), §6.7, §9 seam **"Path A subset acceptance test"** |
| Opus N5 P1 (status block over-claims `quickfix_compat/` shim path) | P1 (NEW) | Status block rewritten: separates `Owner: Opus drafts; user approves.` from a `Headers:` list; `<fixpp/session/quickfix_compat/cfg_loader.hpp>` listed (config-translation only, no runtime adapter); the new opt-in `<fixpp/session/quickfix_compat/sync_message_store_adapter.hpp>` is listed and tied to its four-condition `static_assert`; no implication of a default `message_store_adapter.hpp`. | status block |
| Opus N6 P2 (`store_seqnum_overflow` is session-fatal, not routine) | P2 (NEW) | §6.7 row rewritten: session-fatal; FSM MUST surface via `onLogout` reason / session-level error callback; recovery is operator-driven (`reset()` + `ResetSeqNumFlag(141)=Y` Logon per `[FIX-SL §4.8.6]` OR sticky abort). Store does NOT autonomously reset. §10 Q9 captures the `uint64_t` follow-up. | §1.2 (counter type bullet), §4.1 method 3 docstring, §6.7, §10 Q9 |
| Opus N7 P2 (`session_arena` vs `store_arena` lifetime — shutdown UAF latent) | P2 (NEW) | §6.2.1 spells out shutdown ordering: graceful-close phase 1 awaits in-flight ops; terminal-close cancels root and JOINS in-flight awaitables; ~Session ordering: cancel → drain → destroy `unique_ptr<MessageStore>` → release `session_arena` → ~Session. Visitor span is bounded by visitor awaitable (inner), not retrieve awaitable (outer). N1's `unique_ptr` ownership makes "store outlives session" type-impossible. New §9 seam **"Session shutdown ordering test"** (#19). | §6.2.1, §9 seam **"Session shutdown ordering test"** |
| Opus N8 P2 (Tier 1 ceilings don't add up; reset off by 100×) | P2 (NEW) | §6.6 rewritten with per-row arithmetic (vtable + awaitable + mutex + memcpy + ring write + completion) per `[2b §6.6]` v0.2 pattern. Vtable cost named per N3 (~5–15 ns). `MemoryStore::reset` ceiling lifted from ≤ 1 µs to ≤ 500 µs (matches `memset` at 10 GiB/s of 3.2 MiB entry array; slab is lazy-reuse, not zeroed). FileStore `commit_per_message` row tightened to ≤ 250 µs reflecting `fdatasync` floor. | §6.6 |
| Opus N9 P2 (DoS — `retrieve(0, 0)`; ring exhaustion) | P2 (NEW) | §4.1 method 2 rejects `begin == 0` (`store_seqnum_invalid`) and `end < begin && end != 0` (`store_invalid_range`). §1.2 documents storage-DoS bound = `(inbound_capacity + outbound_capacity) * max_frame_bytes`; engine refuses Configs whose product exceeds `EngineConfig::max_store_memory_per_session` (default 1 GiB). New error variants in §6.7. | §1.2, §4.1 method 2, §4.2, §4.4 (factory error), §6.7 |
| Opus N10 P2 (seam #15 PMR poison wishful) | P2 (NEW) | v0.1 seam #15 deleted (the surface no longer admits the injection — `MemoryStore::store` does zero allocator calls per N9). Replaced by §9 seam **"PMR poison on retrieve-recovery path"** (#17) that injects poison into the visitor's mr; verifies `expected_t::unexpected{store_visitor_aborted}` surfaces from `trap_throw` without termination. | §9 seam **"PMR poison on retrieve-recovery path"** |
| Opus N11 P3 (visitor `[const §XIV.2]` cap reference misleading) | P3 (NEW) | §4.5 prose drops the `[const §XIV.2]` reference (the visitor is per-call callback shape, not a plugin interface — the cap doesn't bind here). | §4.5 |
| Opus N12 P3 (`direction_t` parameter on `on_frame` is dead) | P3 (NEW) | `direction_t` dropped from `on_frame` signature in §4.5 (the visitor knows its own direction from the `retrieve()` call that constructed it). | §4.1 method 2 (call-site type), §4.5 |
| Opus N13 P3 (`[const §VIII.4]` citation overreaches) | P3 (NEW) | Status block + §1 goal 9 + §6.1.2 cite `[2a §4.2]` (`trap_throw` pattern) + `[const §VIII.5]` (zero-global-heap on hot path) — NOT `[const §VIII.4]` (which is "v1.0 perf targets"). | status block, §1 goal 9, §6.1.2, Appendix B |

**Disagreements:** None — every Codex finding was confirmed at its rated severity (with two escalations Codex P2-8 → P1 cancellation contract and Codex P2-11 → P1 Windows platform shim, both folded into root causes #1 and #4 respectively). 0 Opus "Disagree" verdicts in round 1.

**Sibling-doc tensions surfaced:**

1. **`[2d §4.5]` `SessionConfig::store_factory` field type** is `std::shared_ptr<MessageStoreFactory>` in 2d v0.4; per N1, `[arch §5.6]`'s mid-session-swap ban requires `unique_ptr` ownership. 2e ships `unique_ptr<MessageStore>` from `make()` regardless; the `SessionConfig` field-type edit is the sibling-doc amendment owed by 2d at 2e sign-off — Appendix D drop-in below.
2. **Phase-4 session-module spec — `seqnum_t` ownership.** 2e consumes a `<fixpp/session/seqnum.hpp>` placeholder (`uint32_t`) until the Phase-4 spec lands and publishes the canonical type. The Phase-4 spec MAY pick `uint64_t` per N6 / §10 Q9. This is a forward hand-off, not a tension — recorded for the Phase-4 spec author.

**Net effect summary:**

Sections **rewritten** (vs line-edited): the entire status block (header rewrite separating Owner from Headers list per N5; convergence-log pointer added; citation fixes per N13 + Codex P3-14); §1 goals (the call-ordering goal + cancellation-result goal added; goal count went from 8 → 10; goal 6 reworded for Path B + Path A subset); §3.1 (NEW — inherited-primitives exhaustive list); §4.1 (block comment fully rewritten; method count down to 4; `store_concurrent_writer` line dropped; `flush()` removed); §4.1.1 (rewritten — concept-vs-virtual + flush-removal rationale); §4.2 (`MemoryStore` Config + slot-and-slab layout); §4.3 (`FileStore` Config + `FileStorePolicy` + `flush_for_session_close()` + `commit_per_message` default); §4.3.1 (durability ladder rewritten honestly); §4.4 (`make()` returns `unique_ptr`); §4.5 (visitor made awaitable; `direction_t` parameter dropped; `abort_error` body fixed; `[const §XIV.2]` reference dropped); §4.7 (PLACEHOLDER + cross-doc handoff); §4.8 (split into §4.8.A default Path B + §4.8.B Path A subset); §6.1 (subsections reorganised; new §6.1.4 cancellation result-contract); §6.2 (input span + visitor span); §6.2.1 (NEW — Session shutdown ordering); §6.3 (entirely rewritten — append-only log + truncate-on-startup + per-record CRC + counters-as-piggyback-record + portable platform shim); §6.3.5 (NEW — platform portability subsection); §6.4 (mutex contract — exclusive only; FIFO-fair concurrent-arrival; mutex hold rule on retrieve); §6.6 (per-row arithmetic justification; reset ceiling lifted; vtable cost named); §6.7 (variant changes — `store_concurrent_writer` removed; `store_cancelled`, `store_seqnum_invalid`, `store_invalid_range`, `store_shim_timeout` added; `store_seqnum_overflow` reworded session-fatal; `store_factory_failed` extended for N9 storage-DoS); §7.1 (outbound call ordering ties); §7.4 (mutex hand-off gate); §7.6 (Phase-4 hand-off — graceful-close path uses `flush_for_session_close`); §10 (Q1 closed; Q5 closed; Q9 added — Phase-4 `seqnum_t` ownership; Q11 added — `[2d §4.5]` factory ownership amendment owed); §11 (catalogue + coverage-index amendments). Sections **line-edited** (no structural change): §2 (non-goals — added `flush()`-dropped, `async_shared_mutex`-dropped, Path A subset opt-in clarification); §6.5 (effective_clock); §7.2 (codegen integration); §7.3 (factory ownership flag); §7.5 (logging fields).

**§9 test seam count: v0.1 had 16 → v0.2 has 20** (+5 new: "Cancellation-result-contract test" #6, "Outbound store-after-commit byte-equality test" #7, "Awaitable visitor + span lifetime" #9 (replaces v0.1 #8), "Path A subset acceptance test" #12, "MemoryStore::store performs zero allocator calls" #16 (replaces v0.1 wishful #15), "PMR poison on retrieve-recovery path" #17 (replaces v0.1 #15), "Session shutdown ordering test" #19; v0.1 #5 renamed/sharpened to "FIFO-fair concurrent-writer test"; v0.1 #3 renamed/extended to "FileStore portable torn-write protection" running on Tier 1 + Tier 2; net seam count 20).

**§6.7 error variant count: v0.1 had 8 → v0.2 has 11** (+4 added: `store_cancelled`, `store_seqnum_invalid`, `store_invalid_range`, `store_shim_timeout`; -1 removed: `store_concurrent_writer`; net +3).

**Sibling-doc cross-references added:**
- `[2b §4.5]` (Writer commit finalises BodyLength + CheckSum — load-bearing for root cause #1).
- `[FIX-SL §4.8.6]` (hard reset semantics for the N6 `store_seqnum_overflow` rewording).
- `[FIX-SL §4.4]` (sequence reset path — S-017 ties).
- `[const §II.3]` (Tier 2 — Windows/MSVC for root cause #4).
- `[const §X.5]` (C-ABI handle invalidation rule for `fixpp_store_t` per N1).
- `[2a §4.2]` and `[const §VIII.5]` (replacing v0.1's `[const §VIII.4]` per N13).
- `[2d §4.7]` (two-phase close — graceful flush before phase 1 — for Session-close `flush_for_session_close` callsite per N2).
- `[SYN §3.2 Q6b]` (`async_shared_mutex` post-v1.0 — root cause #3).

---

## Appendix D — Drop-in amendments for sibling-doc text touched by this rewrite

Per convergence rule 6 + the 2d v0.4 / 2c v1.3 sibling-doc-edit precedent, sibling-doc text touched by this rewrite is surfaced as drop-in amendment language for the orchestrator to apply at sign-off. The 2e rewrite agent does not edit `2d-threading.md` directly. Per `[const §VI.5]`, every reference uses the exact `[DocAbbrev §X.Y.Z] Title` form; review-internal IDs (e.g., "C-R2-P1-4 close", "round-2 root cause #2") are not carried into the sibling text.

### D.1 `[2d §4.5] fixpp::session::SessionConfig — session-level frozen-at-open knobs` — `store_factory` field type (round 1, refined in round 2 per Opus N2-P3-1)

**Tension:** `[2d §4.5]` v0.4 line 534 declares `std::shared_ptr<MessageStoreFactory> store_factory;` as a `SessionConfig` plugin-override field. Per N1 (round 1), `[arch §5.6]`'s mid-session-swap ban implies unique ownership of the factory (no shared-store-across-sessions, no second `shared_ptr` alive past `~Session`). 2e §4.4 ships `make()` returning `std::unique_ptr<MessageStore>` regardless; the field-type edit is the sibling-doc amendment owed by 2d at 2e sign-off.

**Before** (current `2d-threading.md` v0.4 text, `[2d §4.5]` lines 533–535):

```cpp
    // ── Plugin overrides (each null → inherit from EngineConfig) ────────
    std::shared_ptr<MessageStoreFactory>           store_factory;
    std::shared_ptr<fixpp::tls::cert_source>       cert_source;
```

**After** (drop-in replacement):

```cpp
    // ── Plugin overrides (each null → inherit from EngineConfig) ────────
    std::unique_ptr<MessageStoreFactory>           store_factory;   // unique ownership per [arch §5.6] / [2e §4.4]
    std::shared_ptr<fixpp::tls::cert_source>       cert_source;
```

The diff is a single-token swap on line 534 (`std::shared_ptr` → `std::unique_ptr`) plus a one-line trailing comment on the same line. Column alignment matches `[2d §4.5]` v0.4's existing two-space gutter; the orchestrator's apply step is mechanical.

Note: `cert_source` stays `shared_ptr` (its ownership is owned by 2g; not affected by this amendment).

The orchestrator applies this edit at 2e sign-off; the amendment is recorded in `[2d-threading.md App C]` as a cross-doc edit driven by 2e's N1 finding (round 1) + Codex C-R2-P1-4 (round 2 — refinement to byte-exact diff form per Opus N2-P3-1).

### D.2 `[2d §4.7] Cancellation propagation API — two-phase close` — `FileStore::flush_for_session_close()` row in the per-mode effect table (round 2 — NEW per Codex C-R2-P1-5 / round-2 root cause #2)

**Tension (NEW in round 2):** v0.2 §7.6 + status block cited `[2d §4.7]` for an engine-internal `FileStore::flush_for_session_close()` graceful-pre-phase-1 store-flush hook; the actual `[2d §4.7]` v0.4 per-mode effect table at lines 798–809 has **no such row**. Implementers reading `[2d §4.7]` alone do not see the hook; implementers reading `[2e §7.6]` alone read a phantom citation; the resulting Phase-4 session-module spec author cannot pick one side without re-running Gate A on 2d. Round 2 closes this by declaring the cross-doc edit explicitly: 2d v0.4 §4.7's per-mode effect table gains one row + a one-paragraph contract.

**Before** (current `2d-threading.md` v0.4 text, `[2d §4.7]` per-mode effect table at lines 798–809; the existing 9 rows are not modified — the new row is appended after the `MessageStore::write` row at line 808):

```
| `MessageStore::write` (in-flight) | runs to completion | cancelled (`operation_aborted`) | cancelled |
```

**After** (drop-in addition — append one row to the existing table; do not modify the existing rows):

```
| `MessageStore::write` (in-flight) | runs to completion | cancelled (`operation_aborted`) | cancelled |
| `FileStore::flush_for_session_close()` (engine-internal hook) | runs (graceful pre-phase-1 store-durability flush) | n/a (already drained) | not invoked (`terminal` skips phase 1 entirely per §4.7) |
```

**One-paragraph contract on the hook's cancellation/error semantics (drop-in addition — append immediately after the per-mode effect table at line 809, before the existing Notes section that begins at line 810):**

> **`FileStore::flush_for_session_close()` hook contract (driven by `[2e §7.6]`).** The engine-internal `FileStore::flush_for_session_close()` is a non-virtual, non-public method on the concrete `FileStore` (NOT on `MessageStore`'s pure-virtual interface — see `[2e §4.1.1]`); the engine reaches it via the session's stored `unique_ptr<MessageStore>` through a friend mechanism. Under `close_mode::graceful` it is invoked once during phase 1, after the FSM's last in-flight `store(...)` awaitable has resumed and before the Logout `async_write` is issued; it drains any pending `commit_batched` / `commit_interval` records to durable storage so the regulator-mandated tail records make it past a host crash that follows close. Cancellation: the hook completes either with success (`expected_t<void>{}`) or with `expected_t::unexpected{store_io_failure}` on a mid-flush `fdatasync`/`FlushFileBuffers` error; the engine logs the failure and proceeds with phase 1's Logout exchange (the durability gap is documented as a `commit_batched` / `commit_interval` data-loss window per `[2e §4.3.1]`). Under `close_mode::terminal` the hook is **not** invoked — terminal close fires root cancellation immediately, and the in-flight `MessageStore::write` row above governs the in-flight state. The hook is **idempotent**: a second invocation is a no-op (returns `expected_t<void>{}` immediately).

The orchestrator applies this edit at 2e sign-off; the amendment is recorded in `[2d-threading.md App C]` as a cross-doc edit driven by 2e's round-2 root cause #2 (Codex C-R2-P1-5).

### D.3 `[2e §4.4] MessageStoreFactory — public-surface factory shape` — `make()` virtual signature extended from 3-param to 5-param (added post-design-doc-sign-off by `008-message-store` Phase-4 Gate A — round-1 RC#1 + fresh-loop round-2 RC#1)

**Tension (NEW post-sign-off):** `[2e §4.4]` lines 712–715 declare the pure-virtual `MessageStoreFactory::make()` with 3 parameters — `(sender_comp_id, target_comp_id, mr)` — and the §4.4 closing paragraph constrains the factory CTOR to be Config-only (no `EngineConfig&` back-channel). The `008-message-store` Phase-4 bundle Gate A surfaced two composition gaps that the 3-param shape cannot satisfy without violating that Config-only CTOR rule:

1. **Cap-construction guard (round-1 RC#1, Codex P1-3 / N9 cluster).** `[2e §1.2]:54` and `§4.4 store_factory_failed` (line 1117) require `MemoryStoreFactory::make()` to reject Configs whose product `(inbound_capacity + outbound_capacity) * max_frame_bytes` exceeds `EngineConfig::max_store_memory_per_session`. Under the 3-param signature the factory has no path to read the engine-resolved cap value at call time, and reading it through an `EngineConfig&` CTOR back-channel breaks `§4.4`'s Config-only CTOR rule. Resolution: thread the engine-resolved value at call time as `make()`'s 4th parameter `std::size_t max_store_memory_bytes` (the engine reads `EngineConfig::max_store_memory_per_session` at session-open and passes it to each `make()` call). The factory CTOR stays Config-only.
2. **`file_io_executor` injection for path-only `cfg_loader` (fresh-loop round-2 RC#1).** `[2e §4.3.2]:665` requires `FileStore::Config::file_io_executor` at `FileStore` construction; `[2e §4.8.A.2]:869` prescribes the path-only `cfg_loader(const std::filesystem::path&)` reader, which has no `EngineConfig` access. Composing these two contracts on the 3-param `make()` is impossible without either (a) breaking §4.4's Config-only CTOR rule (passing `EngineConfig&` through the factory constructor) or (b) breaking `§4.3.2:665` (constructing `FileStore` without a required field). Resolution: thread the engine-resolved value at call time as `make()`'s 5th parameter `asio::any_io_executor file_io_executor` (the engine reads `EngineConfig::file_io_executor` at session-open and passes it to each `make()` call). `FileStore` itself is constructed inside `make()` after the executor is resolved, preserving `§4.3.2:665`'s required-at-construction contract. **Precedence rule:** Config-supplied wins — if the factory's stored `Config.file_io_executor` is non-empty (caller passed their own at factory construction), that wins; otherwise the engine-threaded 5th-parameter value populates the minted `FileStore::Config::file_io_executor`; both empty → `make()` returns `store_factory_failed` (no "no executor" operating mode for `FileStore`). `MemoryStoreFactory::make()` accepts the 5th parameter and silently discards it (no-op; non-empty values are accepted, not a misuse — `MemoryStore` has no file-I/O work).

Both additions use the same threading pattern: engine-resolved Config-supplied parameters at call time, factory CTOR stays Config-only, design-doc §4.4 frozen-public-surface rule preserved.

**Before** (current `2e-msgstore.md` v0.4 text, `[2e §4.4]` lines 712–715 within the `MessageStoreFactory` block — the existing comment block above the method is not modified):

```cpp
    [[nodiscard]] virtual expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender_comp_id,
         std::string_view target_comp_id,
         std::pmr::memory_resource* mr) noexcept = 0;
```

…and the matching default-factory declarations at lines 723–724 (`MemoryStoreFactory`) and 730–731 (`FileStoreFactory`):

```cpp
    expected_t<std::unique_ptr<MessageStore>>
        make(std::string_view, std::string_view, std::pmr::memory_resource*) noexcept override;
```

**After** (drop-in replacement — the pure-virtual on `MessageStoreFactory` and the `override` declarations on both default factories):

```cpp
    [[nodiscard]] virtual expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender_comp_id,
         std::string_view target_comp_id,
         std::pmr::memory_resource* mr,
         std::size_t max_store_memory_bytes,                 // engine-resolved EngineConfig::max_store_memory_per_session per N9 / [2e §1.2]
         asio::any_io_executor file_io_executor) noexcept = 0;  // engine-resolved EngineConfig::file_io_executor per §4.3.2:665 / :669
```

```cpp
    expected_t<std::unique_ptr<MessageStore>>
        make(std::string_view, std::string_view, std::pmr::memory_resource*,
             std::size_t, asio::any_io_executor) noexcept override;
```

**Effective:** as of `008-message-store` Phase-4 Gate A convergence (2026-05-20). **Pre-applied at this rewrite** — the live `[2e §4.4]` block at lines 712–732 has been replaced in-place with the 5-param "After" form above, shipping inside the `008-message-store` PR bundle (parallel to the FR-037/038/039 hybrid-ownership pattern recorded in `008-message-store` spec.md Clarifications Q3 + research.md D-8). This contrasts with D.1/D.2 which were pre-applied at 2e v0.4 sign-off and shipped through `007-threading-clock`'s merge. This Appendix D §D.3 entry is retained as the byte-exact diff record of the amendment.

### D.4 `[2e §4.3] FileStore — public-surface knobs (Config + FileStorePolicy + flush_for_session_close())` — `FileStoreFactory::make()` MUST validate CompID filesystem-safety (added post-design-doc-sign-off by `008-message-store` Phase-4 pipeline-step-9 audit — Gap 1, Codex CONFIRM P1 / Opus CONFIRM P1 with empty-CompID + control-char + NAME_MAX tightening)

**Tension (NEW post-sign-off):** `[2e §4.3]` lines 541–568 declare `FileStore::Config::sender_comp_id` / `target_comp_id` as raw `std::string` with no character-set or path-component constraint; `[2e §6.3.1]` lines 1002–1012 write `<sender>__<target>.log` (and the §6.3.4 sibling `<sender>__<target>.log.reset.tmp`) by direct string interpolation. A `cfg_loader`-provisioned (FR-030) or operator-misconfigured CompID containing `/`, `\`, `..`, NUL, control characters, or exceeding `NAME_MAX` produces a path component that **escapes `Config::directory`** — directory traversal outside the configured store directory. The composed filename is **also** the advisory-lock target (FR-013 / I-16); a CompID with a path separator causes `flock` to be taken on an unintended file, defeating the §10 Q4 single-writer contract. This is a real correctness defect, not caller hygiene only — `FileStoreFactory::make()` is the choke point all factory paths pass through (direct Path-B users construct `FileStoreFactory` from a `Config{}` literal and bypass `cfg_loader` entirely), so the bind site is the factory `make()`.

FIX-SL §4.3 (Tag 49 SenderCompID / Tag 56 TargetCompID) admits ASCII printables; it does NOT actually constrain to alphanumeric. So spec-side silence on CompID filesystem-safety is genuinely under-constrained relative to FIX-protocol-legal inputs.

**Before** (current `2e-msgstore.md` v0.4 text, no validation contract — `[2e §4.3]` `FileStore::Config` block at lines 547–553 and `[2e §6.3.1]` filename composition at lines 1002–1012 carry no MUST-reject clause on CompID values; `[2e §4.4]` `FileStoreFactory::make()` at lines 712–732 (post-§D.3) carries no validation hook before file open / lock take):

```cpp
struct Config {
    std::filesystem::path directory;
    std::string           sender_comp_id;       // ← no validation
    std::string           target_comp_id;       // ← no validation
    // ...
};
```

```text
File path composition (§6.3.1): "<directory>/<sender>__<target>.log"
                                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                                                no constraint on these strings
```

**After** (drop-in normative addition — pre-applied at this rewrite as `[2e §4.3]` block-comment expansion + `[2e §4.4]` `make()` validation paragraph):

> **CompID filesystem-safety validation (NEW v0.5 per §D.4 — Gap 1 close).** `FileStoreFactory::make(sender, target, mr, max_store_memory_bytes, file_io_executor)` MUST validate the effective `sender` / `target` CompID values **before** composing `<directory>/<sender>__<target>.log` and **before** any file is opened or advisory lock taken. The composed filename stem MUST be exactly one filesystem path component: if either CompID is empty, contains a path separator (`'/'` on Linux; `'/'` or `'\\'` on Windows), a NUL byte, a `.` or `..` path segment, a control character in the range `[0x00, 0x1F]` or `0x7F`, or would cause the composed path component to exceed `NAME_MAX` (Linux: `pathconf(_PC_NAME_MAX)` on the parent directory; Windows: `MAX_PATH` minus the directory prefix), `make()` MUST fail with `expected_t::unexpected{store_factory_failed}` before any file is opened and before any advisory lock is taken. The same validation MUST be mirrored at `fixpp::session::quickfix_compat::cfg_loader::cfg_to_file_store_factory()` as defense in depth so a malformed CFG fails fast at config-load rather than at session-open. The validation is **`noexcept`-safe**: implementations use primitive `std::string_view::find_first_of` / `find` calls and MUST NOT invoke `std::filesystem::path` constructors (which can throw on Windows on long paths) until validation has passed — preserves the `noexcept` contract on `FileStoreFactory::make()` per `[2e §4.4]` and the `contracts/message_store_factory.hpp:76` signature.
>
> **Rationale.** FIX-SL §4.3 admits ASCII printables in Tag 49 / Tag 56 — the FIX protocol does NOT constrain CompIDs to alphanumeric. A `cfg_loader`-provisioned (FR-030) `<sender>__<target>.log` composed from an unvalidated CompID can escape `Config::directory` (directory traversal) and/or cause the advisory lock to be taken on an unintended file (defeating the §10 Q4 single-writer contract). The bind site is `FileStoreFactory::make()` (not `cfg_loader`-only) because direct Path-B users construct `FileStoreFactory` from `Config{}` literals; `cfg_loader`-side validation is defense in depth. The empty-CompID reject closes a separate hazard: `__.log` is a valid filename but defeats the `session_triple_hash` discrimination (sentinel record per §6.3.1 / §10 Q7), silently aliasing all empty-CompID sessions to one file.

**Cross-doc propagation (the `008-message-store` PR bundle inherits this amendment):**
- `specs/008-message-store/spec.md` FR-008 — tightened with the validation requirement and `make()` bind point.
- `specs/008-message-store/data-model.md` E4 (`FileStore`) — cross-references §D.4 for the CompID validation contract.
- `specs/008-message-store/contracts/file_store.hpp` (Config block-comment) and `contracts/file_store_factory.hpp` (make() docstring) — cross-reference §D.4.
- `specs/008-message-store/contracts/cfg_loader.hpp` (docstring) — cross-references §D.4 defense-in-depth mirror.
- `specs/008-message-store/checklists/implementation-readiness.md` CHK004 — reclassified from WAIVED-with-fabricated-rationale to **SPEC-FIXED §D.4**.
- `[2e §9 seam #2]` ("FileStore crash-survival") — extended with the CompID-validation reject sub-cases (CompIDs containing `/`, `\`, `..`, NUL, control char, empty, NAME_MAX excess MUST surface `store_factory_failed` from `make()` before file open / lock take). FR-033 21-seam count preserved (this is a sub-case extension, not a new seam).

**Effective:** as of `008-message-store` Phase-4 pipeline-step-9 audit (2026-05-20). **Pre-applied at this rewrite** — the live `[2e §4.3]` block and `[2e §4.4]` `make()` validation paragraph carry the post-amendment shape, shipping inside the `008-message-store` PR bundle (same Path-A precedent as §D.3). This Appendix D §D.4 entry is retained as the normative record of the amendment.

### D.5 `[2e §6.3.5] On-disk algorithm — platform portability + advisory locks` — `FileStore` supported only on filesystems where `flock` / `LockFileEx` semantics are honored (added post-design-doc-sign-off by `008-message-store` Phase-4 pipeline-step-9 audit — Gap 2, Codex CONFIRM P1 / Opus CONFIRM P1 with unsupported-FS enumeration + "no detection or warning" tightening)

**Tension (NEW post-sign-off):** `[2e §6.3.5]` lines 1052–1063 (the platform-portability table) state `flock(fd, LOCK_EX)` / `LockFileEx(...)` as the advisory-lock primitives with **no filesystem-type qualifier**. `[2e §10 Q4]` line 1237 ("`FileStore` directory contention") asserts the advisory lock closes the two-engine race — but that claim only holds on filesystems that honor `flock`/`LockFileEx` cross-process. On NFSv3 without a correctly-configured `rpc.statd`/`rpc.lockd`, `flock` is silently downgraded to a no-op (`man 2 flock` BUGS section: *"flock() does not lock files over NFS"*); on legacy SMB/CIFS mounts and on cluster filesystems (GPFS, Lustre, GFS2, OCFS2), advisory locks are not reliably propagated across hosts. Under those topologies the §10 Q4 contract **silently fails** — two engines on two hosts both acquire "the lock" and proceed to interleave writes into the same log, producing torn records that surface as `store_io_failure` on the next restart's CRC scan (operator's actual fault — deployed on unsupported FS — surfaces as what looks like a hardware fault).

The "local-fs" framing at `[2e §1.1]` line 33 and goal 3 (line 18) reads as descriptive operating-mode framing, NOT as a normative MUST excluding shared mounts. v0.4's `[2e §6.3.5]` / FR-013 / I-16 carry no filesystem-type qualifier and the contract reads as making an unconditional cross-host correctness promise.

**Scope-pin vs runtime probe.** A `flock`-then-`flock`-on-a-second-fd same-host probe does NOT prove multi-host correctness (it tests local kernel support, not multi-host lock-manager propagation). A `statfs(2)` / `GetVolumeInformationW` filesystem-type probe is non-portable, incomplete (NFSv4 over Kerberos may be fine; NFSv3 over UDP without lockd is broken; same `f_type = NFS_SUPER_MAGIC`), and cluster-FS magic numbers are not exhaustively enumerable forwards-compatibly. **The probe is worse than nothing** — it would either give false reassurance on misconfigured shared FS or refuse valid deployments. The cheaper and more honest fix is to **narrow the supported deployment surface in the design contract** and let operators attest correctness out of band.

**Before** (current `2e-msgstore.md` v0.4 text — `[2e §6.3.5]` lines 1052–1063 platform-portability table carries `flock` / `LockFileEx` as the advisory-lock primitive with no filesystem-type qualifier; `[2e §10 Q4]` line 1237 asserts the advisory lock closes the two-engine race unconditionally; FR-013 / I-16 / `contracts/file_store_factory.hpp:34-36` repeat the unconditional shape):

```text
Advisory exclusive lock at open: flock(fd, LOCK_EX) [Linux]
                                 LockFileEx(LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY) [Windows]
                                 ← no filesystem-type qualifier; assumed to work everywhere
```

**After** (drop-in normative addition — appended to the §6.3.5 platform-portability table closing paragraph as a "Scope & trust" subsection per root cause #1):

> **Scope & trust: filesystem-type contract for advisory-lock honoring (NEW v0.5 per §D.5 — Gap 2 close).** `FileStore` is supported only on filesystems where the platform advisory-lock primitive in §6.3.5 (Linux: `flock(2)`; Windows: `LockFileEx`) provides effective cross-process exclusive-lock semantics for every host that may open the live log path. **Behaviour on filesystems that do not honour those semantics — including but not limited to NFS (any version without an active and correctly-configured lock manager — `rpc.statd` + `rpc.lockd` on Linux; equivalent on other Unixes), SMB/CIFS, FUSE-mounted network filesystems, and cluster filesystems (GPFS, Lustre, GFS2, OCFS2) — is unsupported and outside the v1.0 correctness contract.** `FileStoreFactory::make()` does NOT detect or warn on such deployments (per the probe-is-worse-than-nothing argument above); operators who deploy on shared storage MUST verify cross-host lock semantics out of band and attest correctness as a deployment precondition. The `[2e §10 Q4]` single-writer contract holds **only** under this scope restriction.
>
> A future v1.x feature may add an opt-in `EngineConfig::store_filesystem_attested = true` flag for operators who have verified their shared-mount topology, but that is post-v1.0 (`[const §X.4]` reserved range). For v1.0, the default-and-only mode is "local filesystem with kernel-honored advisory locks."

**Observability propagation (N-2 — propagation finding from the Opus targeted review).** Under §D.5, an operator who deploys on an unsupported filesystem and triggers two-host concurrent write sees the corruption surface later as a torn record / CRC mismatch on the restart scan, surfacing through §6.7 as either `store_factory_failed` (sentinel-hash mismatch on re-open) or `store_io_failure` (mid-scan CRC mismatch). That observability path conflates operator-fault (unsupported FS topology) with hardware-fault (legitimate disk error). No new error variant is added — adding `store_unsupported_filesystem` would be a probe-coupled error that §D.5 explicitly rejects, and would touch the 10-variant freeze (FR-021). Instead, `[2e §6.7]`'s `store_io_failure` row remediation column is extended with one sentence:

> *Operator note (per §D.5):* If the deployment is on a shared filesystem (NFS / SMB / FUSE / cluster FS) and `store_io_failure` surfaces during the restart scan as a CRC mismatch, the most likely cause is two-host concurrent write under §D.5's unsupported-filesystem scope restriction; the operator MUST verify the deployment topology before treating this as a hardware fault.

**Cross-doc propagation (the `008-message-store` PR bundle inherits this amendment):**
- `specs/008-message-store/spec.md` FR-013 — cross-references §D.5 scope restriction.
- `specs/008-message-store/data-model.md` I-16 — cross-references §D.5.
- `specs/008-message-store/contracts/file_store_factory.hpp` (make() docstring at lines 34–36) — cross-references §D.5.
- `specs/008-message-store/checklists/implementation-readiness.md` CHK008 — reclassified from WAIVED-with-fabricated-rationale to **SPEC-FIXED §D.5**.

**Effective:** as of `008-message-store` Phase-4 pipeline-step-9 audit (2026-05-20). **Pre-applied at this rewrite** — the live `[2e §6.3.5]` "Scope & trust" subsection and `[2e §6.7]` `store_io_failure` row operator-note carry the post-amendment shape, shipping inside the `008-message-store` PR bundle. This Appendix D §D.5 entry is retained as the normative record of the amendment.

### D.6 `[2e §4.4] MessageStoreFactory — public-surface factory shape` — `std::unique_ptr<MessageStore>` deleter contract pinned to `std::default_delete` (added post-design-doc-sign-off by `008-message-store` Phase-4 pipeline-step-9 audit — Gap 3, Codex CONFIRM P2 / Opus CONFIRM P2 with forward-compat reservation per N-4)

**Tension (NEW post-sign-off):** `[2e §4.4]` lines 686–717 (post-§D.3) declare `MessageStoreFactory::make()` returning `expected_t<std::unique_ptr<MessageStore>>` — the bare (default-deleter) `unique_ptr` shape. v0.4 is **silent on how the store object itself is allocated/deallocated**. `[2e §6.1.1]` lines 927–931 + `[2e §8]` lines 1192–1199 + FR-026 / FR-027 address the store's **internal storage** (the `store_arena` / `MemoryStore` slab) — they do NOT govern the store object's own allocation. A user-supplied `MessageStoreFactory::make()` could legally execute `auto* p = static_cast<MemoryStore*>(mr->allocate(sizeof(MemoryStore), alignof(MemoryStore))); new (p) MemoryStore(cfg); return std::unique_ptr<MessageStore>(p);` — on destruction, `std::default_delete<MessageStore>` calls global `operator delete`, which does NOT match the PMR allocator → **UB / UAF**.

The type system implicitly implies `std::default_delete<MessageStore>` (the default deduction for `std::unique_ptr<MessageStore>` is exactly that — `delete static_cast<MessageStore*>(p)` invokes the virtual destructor per `contracts/message_store.hpp:36`). But the doc's heavy PMR emphasis everywhere ELSE (FR-026 / FR-027 / §6.1.1 / §8 / FR-007 zero-allocator-calls) creates real ambiguity for custom-factory authors who reach for `std::pmr::polymorphic_allocator<MyStore>::allocate(1)` + placement-new and return `unique_ptr<MessageStore>(p)` expecting it to compose. It does not. The Opus targeted review records that `[const §VIII.5]` (zero global-heap `new`/`delete` on hot path) does NOT subsume this — `[const §VIII.5]` governs the hot path ("between parse and `fromApp`"); `make()` is the cold open path and is exempt. So §D.6 is genuinely needed and is not implied by any upstream constitutional rule.

**Why this is P2 (not P1):** the type signature is genuinely load-bearing — anyone reaching `make()`'s declaration sees `unique_ptr<MessageStore>` without a deleter, and any C++ author who has used `unique_ptr` once knows that means `std::default_delete`. The documentation gap is "one sentence missing," not "wrong type." The shipping default impls (`MemoryStoreFactory::make()` and `FileStoreFactory::make()`) use ordinary `std::make_unique`, which is `default_delete`-compatible — no v1.0 correctness defect; only a custom-impl-author footgun.

**Why this is not P3:** §6.1.1 / §8 / FR-026 / FR-027 / FR-007 emphasize PMR everywhere. A custom-factory author reading those sections WILL reasonably ask: "is the store object itself PMR-allocated, and if so, with what deallocation hook?" The doc never answers. The answer "you can use a custom deleter via `std::unique_ptr<MessageStore, MyDeleter>`" is **false** (the return type fixes the deleter to `std::default_delete`); the answer "you can PMR-allocate the object if you wrap the dealloc into a `default_delete`-compatible path" is **true but undocumented**.

**Before** (current `2e-msgstore.md` v0.4 text, no deleter contract — `[2e §4.4]` lines 686–717 declares `make()` returning `expected_t<std::unique_ptr<MessageStore>>` with no statement on object-allocation discipline; `[2e §6.1.1]` lines 927–931 + `[2e §8]` lines 1192–1199 + FR-026/FR-027 talk only about internal-storage allocation):

```cpp
[[nodiscard]] virtual expected_t<std::unique_ptr<MessageStore>>
make(std::string_view sender_comp_id,
     std::string_view target_comp_id,
     std::pmr::memory_resource* mr,
     std::size_t max_store_memory_bytes,
     asio::any_io_executor file_io_executor) noexcept = 0;
// ← no statement on how the concrete store object is allocated/deallocated
```

**After** (drop-in normative addition — pre-applied at this rewrite as `[2e §4.4]` paragraph after the `make()` block, with cross-references in `[2e §6.1.1]` and `[2e §8]`):

> **Store-object allocation contract (NEW v0.5 per §D.6 — Gap 3 close).** `MessageStoreFactory::make()`'s return type `expected_t<std::unique_ptr<MessageStore>>` commits the v1.0 contract to **`std::default_delete<MessageStore>`** destruction (the default `unique_ptr` deleter): the concrete store object MUST be destructible via `delete static_cast<MessageStore*>(p)`. Factory implementations that wish to use a PMR allocator for the store object itself MUST wrap the deallocation into a `std::default_delete`-compatible path — the typical pattern is a static `operator delete` overload on the concrete store class that routes back to the PMR resource (e.g., paired with `std::pmr::polymorphic_allocator::new_object` for the matching allocation). **A `std::unique_ptr<MessageStore, CustomDeleter>` return type is NOT supported in v1.0** and is reserved for a possible post-v1.0 evolution per `[const §X.4]`. The PMR resources discussed in `[2e §6.1.1]` / `[2e §8]` / FR-026 / FR-027 govern the store's **internal storage** (slab, ring, framing scratch, index, persisted-frame copy) — they do NOT govern the deleter shape of the store object itself.

**Forward-compat reservation (N-4 — propagation finding from the Opus targeted review).** A post-v1.0 evolution to `std::unique_ptr<MessageStore, CustomDeleter>` (e.g., to support a future PMR-on-the-object impl, or to enable the `2j` control-plane spec's gRPC `OpenSession`-side store-handle ownership) would be an ABI / API break on the `make()` return type. §D.6 explicitly reserves this evolution per `[const §X.4]` so a v1.x designer can revisit the deleter shape without misreading §D.6 as a permanent lock.

**Cross-references (separating object allocation from internal-storage allocation):**
- `[2e §6.1.1]` (allocation on the dispatch hot path) gains a line-edit immediately after the `MemoryStore::store` zero-allocator-calls statement: *"§6.1.1 governs **internal-storage** allocation on the dispatch hot path; the store object's **own** allocation/deallocation is governed by §D.6 — `std::default_delete<MessageStore>`-compatible per the `unique_ptr<MessageStore>` return type of `make()`."*
- `[2e §8]` (PMR recap) gains a line-edit immediately after the store_arena description: *"§8 governs **internal-storage** PMR layout; the store object's **own** allocation/deallocation is governed by §D.6 — `std::default_delete<MessageStore>`-compatible, NOT replaced by the deleter shape."*

**Cross-doc propagation (the `008-message-store` PR bundle inherits this amendment):**
- `specs/008-message-store/spec.md` FR-005 / FR-025 — cross-reference §D.6.
- `specs/008-message-store/data-model.md` E5 (`MessageStoreFactory`) — cross-references §D.6.
- `specs/008-message-store/contracts/message_store_factory.hpp` (make() docstring) — cross-references §D.6.
- `specs/008-message-store/checklists/implementation-readiness.md` CHK030 — reclassified from WAIVED-with-fabricated-rationale to **SPEC-FIXED §D.6**.

**Effective:** as of `008-message-store` Phase-4 pipeline-step-9 audit (2026-05-20). **Pre-applied at this rewrite** — the live `[2e §4.4]` paragraph + `[2e §6.1.1]` line-edit + `[2e §8]` line-edit carry the post-amendment shape, shipping inside the `008-message-store` PR bundle. This Appendix D §D.6 entry is retained as the normative record of the amendment.
