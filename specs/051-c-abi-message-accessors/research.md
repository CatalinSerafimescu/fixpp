# Phase 0 Research — 051 C-ABI Feature C

All decisions below are **source-verified at plan time** against real headers (per [[feedback_planning_explore_existence_claims_unreliable]]); each cites the file:line it was confirmed against. Format: Decision / Rationale / Alternatives.

## D-1 — The C++ surface map: what is a thin thunk vs net-new

**Decision.** Split Feature C by how much C++ already exists:
- **Inbound read (CA-008, CA-010-read) = thin thunks** over existing runtime surface.
- **Outbound construct/commit (CA-009, CA-010-write) = NET-NEW** in-arena accumulator + serialiser (no existing higher-level outbound builder; the low-level `wire::Writer` is unsuitable, D-2).
- **toApp hook (Group 3) = NEW trampoline override** over the existing `Application::toApp` virtual that `send_impl` already fires.

**Rationale (verified):**
- `wire::MessageView<access_mode::Index>::get(uint16_t tag) → expected_t<field_view>` is runtime, tag-keyed (`parser.hpp:200`); `field_view::bytes()`/`as_string()` (`parser.hpp:28`); `get_decimal(tag, mr)` (`parser.hpp:215`); `msg_type()` (`parser.hpp:143`); `has_tag` ≈ a `get(tag)` presence check; `offsets()` exposes the `OffsetTable` (`parser.hpp:193`). The inbound `fixpp_msg_t` already wraps `const MessageView<Index>*` (`capi_internal.hpp:114`).
- The outbound side has **no** higher-level builder; `wire::Writer` exists but frames (D-2).
- `Application::toApp(const MessageView<Index>&, SessionId) → expected_t<void>` exists (`application.hpp:98`) and `Session::send_impl` fires it on the originate path (`session.cpp:278`, `:3523`).

**Alternatives.** Treat outbound as a thin `Writer` wrapper — rejected (D-2). Defer outbound groups — rejected (user decision 2026-06-24, FR-012 in scope).

## D-2 — Outbound = in-arena accumulator, NOT a `wire::Writer` wrapper

**Decision.** The outbound `fixpp_msg_t` is a **mutable, ordered, in-arena field/group accumulator** bound to `Session::session_arena()`. `fixpp_msg_commit` serialises the accumulated state into a valid **app-payload** (`35=<type>` first; each field `digit-tag=non-empty-value\x01`; SOH-terminated; **no** framing tags `8/9/34/49/52/56/10`; repeating groups in dictionary-grammar order — `NoXXX=count` then per-entry delimiter-first). The committed span aliases the arena (D-3).

**Rationale (verified).** `wire::Writer::commit()` is move-qualified and **backpatches digit-only `9=<BodyLength>` + appends `10=<CheckSum>`** with `body_start_` *after* the `9=` field (`writer.hpp:106,132–140`) — i.e. it emits a **full wire frame**. But `Session::send_impl` (the only consumer of the committed bytes via `Engine::send`) **splices** the app-payload between a session-stamped header/trailer and **rejects** any payload that carries a boundary `8=/9=/34=/49=/52=/56=/10=` token as `app_payload_malformed` (`session.cpp:4100,4152–4162`); it does **not** re-parse/reorder. So a `Writer`-framed output would be rejected. The accumulator therefore emits the unframed app-payload directly; `set_*` rejects framing tags at set-time (fail-fast, clearer than a deferred commit/send rejection).

**Alternatives.** (a) Wrap `Writer` and slice `dst_[body_start_, pos_)` — rejected: `body_start_` is private, fragile, and still requires an 8=/9= prefix dance. (b) Reuse `Writer` and strip framing post-hoc — rejected: same fragility + a needless checksum pass. (c) Add a "no-frame mode" to `Writer` — rejected: widens a 2b-owned shared type for a C-ABI-only need; the accumulator is ~50 lines isolated in `src/capi`.

## D-3 — `fixpp_msg_commit` signature + committed-span lifetime

**Decision.** `fixpp_error_t fixpp_msg_commit(fixpp_msg_t* msg, const uint8_t** payload_out, size_t* len_out)`. The returned span **aliases the message arena**, valid until the next mutating call on the same `fixpp_msg_t` (`set_*`/`remove_tag`/group-build) or `fixpp_msg_destroy` — the same flyweight lifetime rule as `fixpp_msg_get_string` (`[2i §4.6]`). The consumer ships it via the existing `fixpp_session_send(session, payload_out, len_out)`, or copies it first. Commit returns `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` if the serialised body would exceed the frame cap.

**Rationale.** The anchor references `fixpp_msg_commit` (`[2i §1.2]`/`§10`) but never declares it — the signature is authored here (like 050's send). Aliasing-arena lifetime is the consistent choice (matches `get_string`) and zero-copy. The frame cap aligns with `send_impl`'s 4096-byte stack buffer (`session.cpp:4021` "larger than ~3800 bytes returns wire_frame_too_large") — commit should reject early with the same magnitude so the consumer gets a clear C-ABI code rather than a deferred send failure.

**Alternatives.** Owned-snapshot span (deep-copy, stable until destroy) — rejected: extra copy, inconsistent with the read flyweight rule. Caller-provided buffer + `BUFFER_TOO_SMALL` — rejected: forces a two-call size/fill dance the read path deliberately avoids.

**NORMATIVE inherited ordering invariant (Codex #4 — pin the dependency).** The quickstart pattern `commit → fixpp_session_send → fixpp_msg_destroy` (the committed span destroyed immediately after send returns) is **safe today only because** Feature B's `fixpp_session_send` blocks on `fut.get()` (`src/capi/session.cpp:216–218` — the borrowed span outlives the call precisely because `.get()` blocks) AND `Engine::send` deep-copies the payload at send entry (`src/session/engine.cpp:1490` — `std::vector<std::byte> payload_copy(app_payload.begin(), app_payload.end())`, before any async hop). 051 **depends** on this ordering: if a future refactor made `fixpp_session_send` non-blocking OR `Engine::send` zero-copy, the immediate-destroy pattern becomes a UAF. This is recorded as an inherited invariant 051 relies on (not re-proven here), and FR-021 adds an ASan regression seam: commit-from-arena → send → immediately destroy the message → assert no UAF.

## D-4 — Inbound repeating-group read = runtime `OffsetTable::group_slices(NoTag)`

**Decision.** `fixpp_msg_get_group(msg, group_tag, …)` thunks into `msg.view->offsets().group_slices(group_tag)` (the runtime primitive under the templated `MessageView::group<NoTag,GroupT>()`, `parser.hpp:233–242`), yielding the per-instance arena slices + count. `fixpp_group_get_field_*(group, i, tag, …)` walks instance slice `[i]` for `tag` (an `Iter`-mode field walk or a per-instance offset lookup). `fixpp_group_get_nested_group` recurses on the instance slice.

**Rationale.** The templated `group<NoTag,GroupT>()` needs a compile-time codegen flyweight type — unavailable at a runtime tag-keyed C-ABI boundary — but it is built on the runtime `group_slices(NoTag)` which only needs the dict's `group_member_fn` threaded at parse (the engine constructs the inbound view with it, `parser.hpp:106`). So the runtime walk is available; the C-ABI does NOT need the codegen types.

**Alternatives.** Expose only codegen-typed group reads — rejected: incompatible with a dictionary-agnostic tag-keyed C ABI. Re-parse each instance with a fresh `MessageView` — viable fallback if `group_slices` per-instance field lookup proves awkward; deferred to implement.

## D-5 — Dictionary access for `TYPE_MISMATCH` / `DICT_CONFIG`

**Decision.** Read-path `TYPE_MISMATCH` (`get_int` on a dictionary-known-non-INT tag) uses the `opaque_dict` + `classify_fn` already threaded onto the inbound `MessageView` (`parser.hpp:103,113`). Outbound `create_outbound`/`set_*` `DICT_CONFIG` (msg-type/tag absent from the dictionary) + `TYPE_MISMATCH` use the **session's** `Dictionary` (the outbound msg is created against a session, which owns its resolved dictionary). When the inbound view was built dict-free (no classify_fn), the read accessors skip the dictionary type check and report only parse-level failures (`WIRE_INVALID_FRAME` for a non-numeric int) — documented degradation, not an error.

**Rationale.** Matches the as-built threading model; avoids a new dictionary handle on `fixpp_msg_t`. `[2i §10] Q7` keeps the message dictionary-agnostic (no `fixpp_msg_get_dict`).

**Alternatives.** Attach a dict handle to every `fixpp_msg_t` — rejected (Q7 dictionary-agnostic). Skip all type checks — rejected: the anchor §4.6/§4.7 specify `TYPE_MISMATCH`/`DICT_CONFIG`.

## D-6 — `[2i §4.3]` session/app block placement = dedicated Phase-4 block `[1400,1499]` (RULED at Gate A round 1)

**Decision.** Mint the codes in a **NEW dedicated Phase-4-owned block `[1400,1499]`** — **6** codes at 1400–1405: `FIXPP_ERR_SESSION_INVALID_ARGUMENT`(1400), `FIXPP_ERR_SESSION_INVALID_STATE`(1401), `FIXPP_ERR_APP_DO_NOT_SEND`(1402), `FIXPP_ERR_APP_CALLBACK_THREW`(1403), `FIXPP_ERR_APP_PAYLOAD_MALFORMED`(1404), and the message-construction reject `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN`(1405). 1400–1404 map the C++ `core::error` ordinals 119/77/129/130/131 in `translate()`; 1405 has no C++ ordinal (raised only by the `set_*` framing-tag reject path, D-2/INV-3).

**Rationale (the placement decision, ruled at Gate A round 1).** Session/app are C-ABI-boundary **domain** failures (bad arg / bad state-for-send / business veto / callback failure / malformed payload), NOT cross-cutting **sentinels**. The `[0,99]` block is the boundary-sentinel range (`NULL_HANDLE`/`INVALID_HANDLE`/`TYPE_MISMATCH`/`TAG_NOT_FOUND`/…) — placing domain codes at `[11,99]` would permanently **relabel** that block to "sentinels + selected session/app domain failures." On the **last** GA-permanent C-ABI feature the relabel is forever, and every future consumer reads `[0,99]` as "C-ABI boundary sentinel"; a dedicated domain is the correct taxonomy. `[1400,1499]` is genuinely free (verified `[2i §1.1]`: it was the `[1400+]` "future expansion" reservation) and does **not** collide with the `[1300,1399]` post-v1.x wire-format growth block (SOFH/SBE/… per `[const §XVIII.2]`). The cost — `[1400,1499]` is the **15th and last** block in the stated `≤1500/15-block` budget (`[2i §1.1]`), and it plants a Phase-4-owned block in a Phase-2-keyed layout requiring a genuinely new occupancy accounting term — is bounded and acceptable: spending the final reserved block on a clean session/app domain beats permanently contaminating the sentinel range. The relabel is forever; the last-reserved-block cost is one-time.

**6-distinct-codes defense (for Gate A, vs §4.3's "coalesce, don't expose per-variant" rationale):** 5 distinct **consumer-remediation classes** — bad argument / bad state-for-send / business veto / callback failure / malformed payload — + 1 message-construction reject class, each warranting a different consumer reaction, not per-variant implementation noise. This is the same "remediation class" test §4.3 applies to the per-doc coalescing groups.

**Occupancy-gate mechanic (verified, `tools/check_capi_occupancy.sh`).** Check A asserts a per-symbol `EXPECTED` map (`[SYMBOL]=numeric`) equals the parsed `error.h` `#define`s AND the `error_codes_v1.txt` audit; Check B asserts the prior-doc `[2X §6.X]` source-domain coalescing counts (the 8 domains DECIMAL/WIRE/DICT/THREAD/STORE/SYNC/TLS/TRANSPORT). The new Phase-4 block is **published surface, not a prior-doc coalescing** — so the co-update touches **Check A only**: +6 `EXPECTED` entries (1400–1405) — the new Phase-4-owned accounting term — + 6 `error.h` defines + 6 audit rows + the §1.1 layout/magnitude rows. It does **NOT** enter Check B's `EXPECT_COUNT`, does **NOT** change the prior-doc `97` total, and does **NOT** change the `[0,99]` cross-cutting count (which stays 11 occupied / 8 introduced — the whole point of a dedicated block). Mechanically clean and contamination-free.

**Alternatives (rejected).** `[11,99]` cross-cutting — rejected: permanently relabels the sentinel block, the GA-forever cost outweighs its mechanical cheapness. Extend 2d THREAD `[300,399]` — rejected (splits the family; app-callback codes in a *threading* block — domain mismatch). `[1300,1399]` — rejected (earmarked post-v1.x wire-format growth, `[const §XVIII.2]`; do not repurpose).

## D-7 — `translate()` re-point + audit + strerror + introducing-minor

**Decision.** In `src/capi/error.cpp`: re-point the **5** mapped arms (119/77/129/130/131, currently `→ FIXPP_ERR_UNKNOWN`) to 1400–1404; add **6** `fixpp_strerror` table entries (static, zero-alloc — including one for 1405 `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN`, which has no `translate()` arm); append 6 rows to `tools/abi_history/error_codes_v1.txt` with **introducing-minor = 4** (the 0.4.0 minor). **Replace the scalar `kIntroducingMinor = 2` with a PER-CODE introducing-minor lookup seeded from `error_codes_v1.txt`'s introducing column** — existing codes keep minor 2 (every currently-published code is minor 2: 050 added symbols but no codes, so there is no minor-3 code), the 6 new codes get minor 4 — so `translate_for_consumer(code, consumer_minor)` computes `introducing_minor(code) > consumer_minor → FIXPP_ERR_UNKNOWN` per code.

**Rationale.** The as-built `translate_for_consumer` (`error.cpp:229–235`) uses a single **scalar** `constexpr kIntroducingMinor = 2`; its in-source comment already mandates "when a later minor adds codes this becomes a per-code lookup seeded from `error_codes_v1.txt`." A literal scalar-bump to 4 would downgrade **every** existing 0.2/0.3 code to `UNKNOWN` for a `consumer_minor=3` engine — a silent regression of the whole published surface (Codex #5). The per-code lookup is therefore mandatory, not optional. Single co-update set per FR-015; missing one site lags ([[feedback_half_restructure_symmetric_api]]). The `translate()` is the 049 no-`default` switch — adding the 5 mapped arms keeps it exhaustive; 1405 is NOT a `translate()` arm.

**Alternatives.** None — this is the mechanical co-update; the per-code table is forced by the existing-code-survival requirement.

## D-8 — toApp trampoline + verdict contract

**Decision.** `CapiApplication` adds a `toApp(const MessageView<Index>&, SessionId) → expected_t<void>` override that, if a C send-callback is registered for the session, wraps the outbound `MessageView` in a (read-only) `fixpp_msg_t` and invokes the callback. The C callback returns a **verdict** from the closed `fixpp_toapp_verdict` enum: `FIXPP_TOAPP_SEND` (=0) → `toApp` returns `{}` (send); `FIXPP_TOAPP_VETO` (=1) → `unexpected(app_do_not_send)` (veto/DoNotSend); `FIXPP_TOAPP_ERROR` (=2), and any out-of-range value, → `unexpected(app_callback_threw)` (terminal-close). Registration: `fixpp_session_register_send_callback(session, cb, userdata)` (pre-start, mirroring `register_callback`). Reentrancy: `FIXPP_REQUIRES_SESSION_LOCK` (runs on `exec_`, `application.hpp:8`).

**Rationale.** A C callback cannot throw a C++ exception, so the "threw" outcome is an explicit verdict value. Mapping it to `unexpected(app_callback_threw)` is critical: `application.hpp:102` says `unexpected(other_error) ⇒ abort`, but `app_callback_threw` specifically triggers terminal-close (`session.hpp:455–464`), which is the desired surfaced-not-aborted behaviour. So the trampoline must map "error verdict" to exactly `app_callback_threw`, never a generic error.

**Alternatives.** An out-param verdict + a void callback — viable; the return-code form is simpler and matches the `expected_t<void>` shape. A bool veto only — rejected: cannot express `app_callback_threw`.

## D-9 — Outbound `fixpp_msg_t` tombstone on session close (FR-009a)

**Decision (single LAZY mechanism — reconciled with E-9).** The outbound `fixpp_msg_t` carries (a) the 049/050 handle type-tag (flipped to `FIXPP_HANDLE_TAG_DEAD` on its **own** `fixpp_msg_destroy`) AND (b) a **self-contained session-validity token: a `std::weak_ptr<SessionLiveness>`** aimed at a per-session liveness control block. The strong `shared_ptr<SessionLiveness>` lives in the **engine-retained `fixpp_session` shell** (which outlives `Session::session_arena()`); `fixpp_session_close` resets it, expiring the token. Every `set_*`/`fixpp_entry_set_*`/`commit`/group-build/`fixpp_msg_destroy` checks BOTH `tag_ != DEAD` AND `weak.lock() != nullptr` **before any arena dereference**; either failing → `FIXPP_ERR_INVALID_HANDLE` (destroy → no-op `OK`). A `fixpp_msg_clone` (session-independent, owner-controlled arena) carries no session token and is **not** tombstoned.

**This is a LAZY check, NOT an eager tag-flip.** Source-verified there is no infrastructure for an eager flip: `capi_internal.hpp` has no registry of issued message handles (`SessionSlot`=`{cb,userdata,established}`; `fixpp_session`=`{engine,id,slot,valid}`), no session generation counter, and the bare `fixpp_msg` struct has no validity field today. The lazy weak-ptr is the only mechanism that (i) needs no per-handle enumeration at close, (ii) lives **outside** the reclaimed `session_arena()` so the check is safe even after the arena is genuinely freed, and (iii) has its expiry happen-before arena teardown. The prior E-9 "eager tag-flip to DEAD on session close" wording is **removed** — it was unimplementable (no enumerator) and inconsistent with this D-9.

**Expiry MUST cover all arena-reclamation paths (source-verified ordering).** `fixpp_session_close` (`src/capi/session.cpp:135–182`) calls `Session::close(graceful)` and flips `session->valid=false` but does **NOT** destroy the C++ `Session`; the `Session` + `session_arena_` (`session.hpp:588`, owned by the C++ `Session`) are reclaimed only at `fixpp_engine_destroy` (`state_.reset()` → `EngineState::engine_` dtor → its Sessions), and the `fixpp_session` shells are retained even past engine-destroy (`sessions_`). So `create outbound msg → fixpp_engine_destroy` **without** a prior `fixpp_session_close` reclaims the arena while a close-only token still `.lock()`s — the same 050 UAF on a different path. The strong `SessionLiveness` ref MUST therefore be reset on **every** path: `fixpp_session_close`, `fixpp_engine_destroy` (reset each retained session's ref before/as `state_.reset()`), and any internal session removal. Invariant: token expiry happens-before arena teardown on all paths.

**Rationale.** Directly applies the 050 root-cause lesson ([[feedback_cabi_handle_destroy_needs_tombstone]]; `[2i §4.2.2]`). Prevents post-close arena UAF — the exact class Gate A + verify missed in 050 — by keeping the validity token outside the arena and checking it before any dereference.

**Alternatives.** Independent-arena outbound (survives session close) — user rejected at clarify (chose tombstone). Documented-precondition-only — rejected (reintroduces the UAF class). `(fixpp_session*, uint64_t generation)` pair with the generation in the engine-retained shell — viable equivalent, but it needs a net-new generation counter; the `weak_ptr<SessionLiveness>` is self-contained and the lighter pin.

## D-10 — Reentrancy class per symbol

**Decision.** The shared `fixpp_msg_get_*` read accessors carry the **single conservative class `FIXPP_REQUIRES_SESSION_LOCK`** (matching the inherited `[2i §4.6]` annotation — NOT edited here). All `set_*`/`fixpp_entry_set_*`, group read/build cursors, the toApp callback → `FIXPP_REQUIRES_SESSION_LOCK`. `fixpp_msg_version` → `FIXPP_THREAD_SAFE` (`parser.hpp:759` set-at-parse, never mutated). `fixpp_msg_destroy` → `FIXPP_THREAD_SAFE`. `fixpp_msg_clone` → `FIXPP_REQUIRES_SESSION_LOCK` on the source. `fixpp_session_register_send_callback` → SINGLE_THREAD (pre-start). The detached-clone-read `FIXPP_THREAD_SAFE` property is a **documented runtime/handle-state guarantee, OUTSIDE the per-symbol gate** — not a second annotation on the shared symbols.

**Rationale.** The per-symbol static gate (`check_capi_reentrancy.sh`) asserts exactly **one** annotation per declaration (`[2i §4.10]`); it has no runtime handle-flavour dimension and therefore **cannot** distinguish an inbound-flyweight read from a detached-clone read on the same `fixpp_msg_get_*` symbol. Claiming the gate enforces the clone exemption is impossible AND contradicts the inherited `[2i §4.6]` `FIXPP_REQUIRES_SESSION_LOCK` annotation (New-P3-a — inheritance integrity; do NOT edit §4.6). So the shared symbols take the single conservative class and the clone-read thread-safety becomes a documented runtime property (a clone owns its arena; reads callable from any thread; caller serialises same-handle access) — the seam-#13 cross-strand contract is a documented/runtime guarantee, not a gate-enforced one. Zero new symbols; the gate is unchanged. (Codex's option A — splitting the ABI into `fixpp_msg_get_*` + `fixpp_msg_clone_get_*` — would statically enforce the THREAD_SAFE clone-read but doubles the read surface and re-opens the §4.8 group-read symbols; rejected in favour of the conservative single class.)

**Alternatives.** Dual-class the shared symbols + claim the gate distinguishes them — rejected (unimplementable; contradicts §4.6). Split the ABI (option A) — rejected (symbol-doubling).

## D-11 — Test strategy + allocation guard

**Decision.** Per-symbol GoogleTest in `tests/capi/`; a pure-C read+write round-trip (SC-001) over loopback with a test-supplied dictionary (inherited L-050-1); group read+write (SC-002); the §9 zero-global-heap allocation guard on read AND set paths via the **dual gate** counting-resource + mallocnesia LD_PRELOAD ([[feedback_tracking_pmr_resource_false_pass]]); the 5-arm error witness incl. the toApp arms (SC-004); the clone cross-strand seam #13 (SC-006); the outbound-tombstone-on-session-close seam under ASan/TSan (FR-009a). Multi-threaded harness for the toApp/tombstone threading surface ([[feedback_single_threaded_harness_masks_strand_races]]).

**Rationale.** Mirrors 049/050's test shape; the allocation guard must be the dual gate or a non-PMR escape via global `new` is false-green.

## D-12 — Version bump 0.3.0 → 0.4.0 (additive MINOR)

**Decision.** `FIXPP_C_ABI_VERSION_MINOR` 3 → 4 (`version.h:33`). New symbols + new additive error codes justify the MINOR bump; `0→1` major freeze stays deferred to GA.

**Rationale.** Additive surface (FR-019); same discipline as 049 (0.1→0.2) / 050 (0.2→0.3).

## D-13 — Reachability of the 5 arms (the 050 SC-005 trap, resolved)

**Decision.** Pre-toApp-hook, only 3 of the 5 arms were C-reachable (`session_invalid_argument`, `session_invalid_state_for_send`, `app_payload_malformed`); `app_do_not_send`/`app_callback_threw` are toApp-originated and the C-ABI had no toApp hook (verified `capi_internal.hpp:70–84` — `CapiApplication` overrides only `onLogon`/`onLogout`/`fromApp`). **User decision (2026-06-24): add the toApp hook (D-8)** so all 5 are end-to-end-witnessable from pure C. SC-004 now asserts all 5 live.

**Rationale.** Avoids the 050 SC-005 trap (publishing a code with no live stimulus) — enumerated the concrete C trigger for each arm before tasks ([[feedback_strand_local_drain_witness_stimulus_must_reach_codepath]]).

**Alternatives.** Publish-numerically + witness-3-live + defer-2 (L-051-2) — rejected by user (chose the toApp hook).
