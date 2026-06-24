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

## D-4 — Inbound repeating-group read = runtime `OffsetTable::group_slices(NoTag)`

**Decision.** `fixpp_msg_get_group(msg, group_tag, …)` thunks into `msg.view->offsets().group_slices(group_tag)` (the runtime primitive under the templated `MessageView::group<NoTag,GroupT>()`, `parser.hpp:233–242`), yielding the per-instance arena slices + count. `fixpp_group_get_field_*(group, i, tag, …)` walks instance slice `[i]` for `tag` (an `Iter`-mode field walk or a per-instance offset lookup). `fixpp_group_get_nested_group` recurses on the instance slice.

**Rationale.** The templated `group<NoTag,GroupT>()` needs a compile-time codegen flyweight type — unavailable at a runtime tag-keyed C-ABI boundary — but it is built on the runtime `group_slices(NoTag)` which only needs the dict's `group_member_fn` threaded at parse (the engine constructs the inbound view with it, `parser.hpp:106`). So the runtime walk is available; the C-ABI does NOT need the codegen types.

**Alternatives.** Expose only codegen-typed group reads — rejected: incompatible with a dictionary-agnostic tag-keyed C ABI. Re-parse each instance with a fresh `MessageView` — viable fallback if `group_slices` per-instance field lookup proves awkward; deferred to implement.

## D-5 — Dictionary access for `TYPE_MISMATCH` / `DICT_CONFIG`

**Decision.** Read-path `TYPE_MISMATCH` (`get_int` on a dictionary-known-non-INT tag) uses the `opaque_dict` + `classify_fn` already threaded onto the inbound `MessageView` (`parser.hpp:103,113`). Outbound `create_outbound`/`set_*` `DICT_CONFIG` (msg-type/tag absent from the dictionary) + `TYPE_MISMATCH` use the **session's** `Dictionary` (the outbound msg is created against a session, which owns its resolved dictionary). When the inbound view was built dict-free (no classify_fn), the read accessors skip the dictionary type check and report only parse-level failures (`WIRE_INVALID_FRAME` for a non-numeric int) — documented degradation, not an error.

**Rationale.** Matches the as-built threading model; avoids a new dictionary handle on `fixpp_msg_t`. `[2i §10] Q7` keeps the message dictionary-agnostic (no `fixpp_msg_get_dict`).

**Alternatives.** Attach a dict handle to every `fixpp_msg_t` — rejected (Q7 dictionary-agnostic). Skip all type checks — rejected: the anchor §4.6/§4.7 specify `TYPE_MISMATCH`/`DICT_CONFIG`.

## D-6 — `[2i §4.3]` session/app block placement = cross-cutting `[11,99]`

**Decision.** Mint the 5 codes in the **cross-cutting block `[0,99]`** at the next free slots (11–15): `FIXPP_ERR_SESSION_INVALID_ARGUMENT`, `FIXPP_ERR_SESSION_INVALID_STATE`, `FIXPP_ERR_APP_DO_NOT_SEND`, `FIXPP_ERR_APP_CALLBACK_THREW`, `FIXPP_ERR_APP_PAYLOAD_MALFORMED` (exact symbol names finalised in contracts). Map from the C++ `core::error` ordinals 119/77/129/130/131 in `translate()`.

**Rationale.** `0..999` are all Phase-2-doc/2j-owned; the only free ranges are `[11,99]` (cross-cutting growth), `[1300,1399]` (post-v1.x wire-format growth, earmarked SOFH/SBE/… per `[const §XVIII.2]` — do not repurpose), `[1400+]`. Session/app are **C-ABI-boundary** errors with no wire/dict/transport home — the same category as the existing `[0,99]` residents `NULL_HANDLE`/`INVALID_HANDLE`/`TYPE_MISMATCH`/`TAG_NOT_FOUND` (handle/boundary sentinels). Placing them in `[11,99]`: (a) lowest occupancy-gate disruption — the block is already 2i-owned, so the gate gains **no new ownership term**, only a count bump (11→16); (b) 5 into 89 free slots is ample; (c) precedent: session-lifecycle already maps into the 2d THREAD block (`session_already_closed → FIXPP_ERR_THREAD_SESSION_LIFECYCLE=301`), but extending *that* block would scatter the family and put app-callback codes in a *threading* block (domain mismatch). The actual `[2i §4.3]` diff is the Gate A review artifact (Article XX).

**5-distinct-codes defense (for Gate A, vs §4.3's "coalesce, don't expose per-variant" rationale):** these are 5 distinct **consumer-remediation classes** — bad argument / bad state-for-send / business veto / callback failure / malformed payload — each warranting a different consumer reaction, not per-variant implementation noise. This is the same "remediation class" test §4.3 applies to the per-doc coalescing groups.

**Alternatives.** New dedicated Phase-4 block `[1400,1499]` — cleaner domain separation + headroom, but it is the last block in the stated ≤1500/15-block budget, plants a Phase-4 block in a Phase-2-keyed layout, and needs a genuinely new gate accounting term. Extend 2d THREAD `[300,399]` — rejected (splits the family; app codes in a threading block). Both recorded as considered; `[11,99]` is the lower-risk GA-permanent default.

## D-7 — `translate()` re-point + audit + strerror + introducing-minor

**Decision.** In `src/capi/error.cpp`: re-point the 5 arms (currently `→ FIXPP_ERR_UNKNOWN`) to the new codes; add 5 `fixpp_strerror` table entries (static, zero-alloc); append 5 rows to `tools/abi_history/error_codes_v1.txt` with **introducing-minor = 4** (the 0.4.0 minor); set `kIntroducingMinor` = 4 for the 5 new codes so the live `translate_for_consumer` downgrade fires `consumer_minor < 4 → FIXPP_ERR_UNKNOWN`, `≥ 4 → real`.

**Rationale.** Single co-update set per FR-015; missing one site lags ([[feedback_half_restructure_symmetric_api]]). The `translate()` is the 049 no-`default` switch — adding the 5 arms keeps it exhaustive.

**Alternatives.** None — this is the mechanical co-update.

## D-8 — toApp trampoline + verdict contract

**Decision.** `CapiApplication` adds a `toApp(const MessageView<Index>&, SessionId) → expected_t<void>` override that, if a C send-callback is registered for the session, wraps the outbound `MessageView` in a (read-only) `fixpp_msg_t` and invokes the callback. The C callback returns a **verdict** `fixpp_error_t`: `FIXPP_ERR_OK` → `toApp` returns `{}` (send); `FIXPP_ERR_APP_DO_NOT_SEND` → `unexpected(app_do_not_send)` (veto/DoNotSend); any other value → `unexpected(app_callback_threw)` (terminal-close). Registration: `fixpp_session_register_send_callback(session, cb, userdata)` (pre-start, mirroring `register_callback`). Reentrancy: `FIXPP_REQUIRES_SESSION_LOCK` (runs on `exec_`, `application.hpp:8`).

**Rationale.** A C callback cannot throw a C++ exception, so the "threw" outcome is an explicit verdict value. Mapping it to `unexpected(app_callback_threw)` is critical: `application.hpp:102` says `unexpected(other_error) ⇒ abort`, but `app_callback_threw` specifically triggers terminal-close (`session.hpp:455–464`), which is the desired surfaced-not-aborted behaviour. So the trampoline must map "error verdict" to exactly `app_callback_threw`, never a generic error.

**Alternatives.** An out-param verdict + a void callback — viable; the return-code form is simpler and matches the `expected_t<void>` shape. A bool veto only — rejected: cannot express `app_callback_threw`.

## D-9 — Outbound `fixpp_msg_t` tombstone on session close (FR-009a)

**Decision.** The outbound `fixpp_msg_t` carries the 049/050 handle type-tag + a validity/generation tie to its session. On session close/destroy (the session's arena is reclaimed), the handle is tombstoned: `set_*`/`commit`/group-build → `FIXPP_ERR_INVALID_HANDLE`; `fixpp_msg_destroy` → NULL-safe idempotent no-op. A `fixpp_msg_clone` (session-independent, owner-controlled arena) is **not** tombstoned.

**Rationale.** Directly applies the 050 root-cause lesson ([[feedback_cabi_handle_destroy_needs_tombstone]]; `[2i §4.2.2]` `FIXPP_HANDLE_TAG_DEAD`). Prevents post-close arena UAF — the exact class Gate A + verify missed in 050.

**Alternatives.** Independent-arena outbound (survives session close) — user rejected at clarify (chose tombstone). Documented-precondition-only — rejected (reintroduces the UAF class).

## D-10 — Reentrancy class per symbol

**Decision.** Inbound-flyweight read accessors, all `set_*`, group read/build cursors, the toApp callback → `FIXPP_REQUIRES_SESSION_LOCK`. Detached-clone reads → `FIXPP_THREAD_SAFE` (user decision; the reentrancy gate must distinguish clone-read from inbound-flyweight-read). `fixpp_msg_version` → `FIXPP_THREAD_SAFE` (`parser.hpp:759` set-at-parse, never mutated). `fixpp_msg_destroy` → `FIXPP_THREAD_SAFE`. `fixpp_msg_clone` → `FIXPP_REQUIRES_SESSION_LOCK` on the source. `fixpp_session_register_send_callback` → SINGLE_THREAD (pre-start).

**Rationale.** Matches `[2i §4.6/§4.7/§4.8/§4.10]` + the clarify decision. The clone/inbound split is the one nuance the gate must encode (D-1 seam #13).

**Alternatives.** Uniform `REQUIRES_SESSION_LOCK` for all reads — rejected at clarify (breaks the seam-#13 cross-strand contract).

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
