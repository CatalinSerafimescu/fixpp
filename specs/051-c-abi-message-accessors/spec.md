# Feature Specification: C ABI message surface — Feature C (field/group accessors, outbound construct + commit) + the [2i §4.3] session/app error-block amendment

**Feature Branch**: `051-c-abi-message-accessors`
**Created**: 2026-06-24
**Status**: Draft
**Input**: User description: "051 C-ABI Feature C — message field/group accessors + outbound construct/commit, PLUS a folded-in [2i §4.3] session/app error-block amendment. Last C-ABI feature before the 0→1 GA freeze; unblocks Python PY-001..005."

> **Workstream context.** This is the **third and last** of the three C-ABI features in the v1.0 tracker (A.2), following Feature A (049 — handle catalogue, error surface, version accessors; merged PR #146) and Feature B (050 — engine/session lifecycle, send, receive callback; merged PR #147). Feature B made the opaque handles *operable* but left the **message representation** behind a wall: the receive callback hands the consumer an inbound `fixpp_msg_t` it cannot yet read, and `fixpp_session_send` takes a raw committed byte span the consumer has no C-ABI way to *produce*. Feature C closes both gaps — it publishes the field/group **read** accessors (CA-008, CA-010-read), the outbound **construct/populate/commit** surface (CA-009, CA-010-write), and the `fixpp_msg_commit` bridge that turns a constructed outbound message into the byte span Feature B's `fixpp_session_send` already accepts. With Feature C merged, the **Python bindings (PY-001..005) unblock** — they were gated on a complete, pure-C message surface. **Folded in per Article XX:** an amendment to the signed-off `[2i §4.3]` master error enum that publishes a **session/app error block** and re-points the five reachable `session_*`/`app_*` arms off `FIXPP_ERR_UNKNOWN` — discharging **L-050-4** and (partially) **L-049-2**, and closing the **050 FR-015/SC-005** that were descoped at Feature B implement-time. Authoritative shape contract: `.specify/2i-capi.md` ([2i]) §4.6 / §4.7 / §4.8 (message surface) + §4.3 (error enum); ABI policy: `.specify/constitution.md` Article X + amendment process Article XX.

## Clarifications

### Session 2026-06-24 (kickoff, source-verified against the anchor + as-built Features A/B)

> These record the design points resolved at kickoff by reading `[2i]` §4.6/§4.7/§4.8/§4.3 against the as-built 049/050 surface. They are pre-`/speckit-clarify`; `/speckit-clarify` may surface further targeted questions (the open item is flagged in **Assumptions**).

- Q: `[2i §4.7]` prose says `fixpp_session_send(session, msg)` "serialises + commits + sends", but Feature B (050, merged) shipped `fixpp_session_send(session, const bytes, len)` taking a **committed application-payload byte span**, not a `fixpp_msg_t`. Which model does Feature C build? → A: **The §10/§1.2 span model, reconciling the stale §4.7 prose.** Feature C authors **`fixpp_msg_commit(msg) → (bytes, len)`** which serialises a constructed outbound message into the committed app-payload span; the consumer ships that span via the **existing, unchanged** `fixpp_session_send`. The §4.7 inline `fixpp_session_send(session, msg)` example is flagged **stale** (it predates Feature B locking the byte-span send) — a recorded deviation analogous to 050's stale `[2i §4.10]` send example; **do NOT reopen `[2i]` for the prose**, but the §4.3 amendment (FR group 2) does reopen `[2i]` and must note it. `fixpp_msg_commit`'s signature is **net-new and authored here** (the anchor references the symbol in §1.2/§10 but never declares it). (Resolves the send-surface model.)
- Q: Can the consumer mutate a parsed **inbound** `fixpp_msg_t` with the `fixpp_msg_set_*` family? → A: **No — inbound messages are immutable** (`[2i §10] Q5`, DECIDED v0.2). `fixpp_msg_set_*` on an inbound flyweight returns `FIXPP_ERR_INVALID_HANDLE`; a consumer that wants to mutate calls `fixpp_msg_clone(inbound, &copy)` first (the clone is a fresh, owner-controlled outbound-shaped message). `fixpp_msg_clone` is part of the CA-009 surface and is the v1.0 cross-strand-handoff escape hatch.
- Q: Which numeric range does the new session/app error block occupy? `[2i §4.3]` shows blocks `[0,99]` cross-cutting … `[1200,1299]` bindings; `0..999` are all Phase-2-doc-owned and the only gaps are `[11,99]` (cross-cutting reserved), `[1300,1399]` (post-v1.x growth), `[1400+]`. → A: **Deferred to `/speckit-plan`** as an explicit, justified artifact with the actual `[2i §4.3]` diff for Gate A. The choice **freezes permanently** at the `0→1` GA cut, so it is a design decision Gate A must review on the real diff, not a spec-time guess. (Spec records the constraint; plan records the slot. See **Assumptions**.)
- Q: The five session/app arms in the plan note are written `session_invalid_argument=119`, `=77`, `=129`, `=130`, `=131`. Are those the proposed C-ABI codes? → A: **No — they are the C++ `fixpp::core::error` enum ORDINALS** that identify *which* arms currently fall through to `FIXPP_ERR_UNKNOWN` (source-verified: `core/error.hpp:325/628/740/…`; the enum comments literally read "slot 119"). The amendment mints **fresh** `FIXPP_ERR_SESSION_*` / `FIXPP_ERR_APP_*` C-ABI codes in the new block and maps each C++ ordinal to its new code. Taken as C-ABI codes the ordinals would collide (119/129/130/131 sit inside the `[100,199]` 2b-wire block; 77 inside cross-cutting-reserved `[11,99]`) — confirming they cannot be the codes.
- Q: Does outbound repeating-group **construction** (FR-012 / US4 — the `[2i §4.8]` builder family) ship in Feature C, or defer to a thin follow-up? → A: **Include in Feature C (user decision 2026-06-24).** The full `[2i §4.8]` surface ships — inbound group *read* (US3) and outbound group *write* (US4) — so a pure-C / Python consumer can both parse and originate multileg/list/market-data messages day one. FR-012 is **MUST** (not deferred); no L-051-x group limitation.
- Q: An outbound `fixpp_msg_t` is bound to its session's per-message arena. If the session closes/destroys while an outbound handle is still alive, what is the contract? → A: **Tombstone on session close (user decision 2026-06-24).** Live outbound message handles are invalidated when their owning session closes/destroys; subsequent `fixpp_msg_set_*` / `fixpp_msg_commit` / group-builder calls on them return `FIXPP_ERR_INVALID_HANDLE`, and `fixpp_msg_destroy` stays a NULL-safe idempotent no-op. This applies the 050 handle-tombstone discipline (`[[feedback_cabi_handle_destroy_needs_tombstone]]`) and prevents post-close arena UAF. (See FR-009a, Edge Cases.)
- Q: The anchor annotates read accessors `FIXPP_REQUIRES_SESSION_LOCK` ("on the owning session strand"), yet seam #13 reads a `fixpp_msg_clone` on a different strand after the source dispatch window closed. For a detached clone, what is the read-reentrancy contract? → A: **Clone reads are `FIXPP_THREAD_SAFE` (user decision 2026-06-24).** A clone is owner-controlled and owns its arena, so reads on it are callable from any thread (the caller serializes concurrent access to the *same* handle) and need no session strand; only **inbound-flyweight** reads carry `FIXPP_REQUIRES_SESSION_LOCK`. This resolves the seam-#13 tension. (See FR-018.)
- Q: Source check found the C-ABI exposes only `fromApp` (receive); `app_do_not_send` (toApp veto) and `app_callback_threw` (toApp/toAdmin throw) are not triggerable from pure C (only 3 of the 5 arms are). Publish-and-defer, or add a toApp hook? → A: **Add a C-ABI `toApp` (send-side) callback hook now (user decision 2026-06-24)**, so all 5 arms are end-to-end-witnessable. This adds an outbound-path callback registration + a verdict contract (send / veto→`app_do_not_send` / error→`app_callback_threw`) mirroring Feature B's `fromApp` trampoline. (Adds US6 + FR-022..FR-024; SC-004 now witnesses all 5 arms live; no L-051-2 deferred witness.)
- Q (design, source-verified — not an open choice): `Session::send_impl` (`session.cpp:4100`) **splices** the app-payload between a stamped header/trailer (validate → excise `43`/`122` PossDup → frame); it does **not** re-parse/reorder. → A: The outbound `fixpp_msg_commit` accumulator MUST therefore emit a valid **wire-order** app-payload itself — `35=<type>` first, each field `digit-tag=non-empty-value\x01`, SOH-terminated, **no** framing tags (`8/9/34/49/52/56/10`), repeating groups in dictionary-grammar order (`NoXXX=count` then per-entry delimiter-first). `set_*` MUST reject framing-tag tags at set-time (fail-fast, not at commit). `[2i §4.3]` numeric range for the new block = **cross-cutting `[11,99]`** (plan decision; Gate A reviews the diff).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Read scalar fields from an inbound message (Priority: P1)

A C consumer (or a binding author building on the C ABI) receives an inbound application message via the Feature-B receive callback and needs to read its fields — strings, integers, doubles, decimals, and the message type — by FIX tag, through `extern "C"` calls, with every fallible read reporting a stable numeric code and never letting a C++ exception cross the boundary.

**Why this priority**: Reading inbound fields is the precondition for *any* useful inbound processing; without it the receive callback hands back an opaque, unreadable handle. It is the half of the message surface the Python bindings most immediately need (parsing inbound order/quote/exec messages).

**Independent Test**: Inside a registered receive callback, a pure-C program reads `35` (msg type), a string tag, an int tag, a double tag, and a decimal tag from the inbound `fixpp_msg_t`; confirms the returned string pointer aliases the wire buffer (zero global-heap), that an absent tag returns `FIXPP_ERR_TAG_NOT_FOUND`, a wrong-flavour read returns `FIXPP_ERR_TYPE_MISMATCH`, and an int read over non-numeric wire bytes returns `FIXPP_ERR_WIRE_INVALID_FRAME` — all without linking any C++ header.

**Acceptance Scenarios**:

1. **Given** an inbound `fixpp_msg_t` inside the dispatch window, **When** the consumer reads a present STRING tag, **Then** it receives `FIXPP_ERR_OK`, a pointer that aliases the underlying wire buffer (no allocation), and the byte length — valid until the dispatch window closes or a `fixpp_msg_set_*` is called on the same handle.
2. **Given** an inbound message, **When** the consumer reads an absent tag, **Then** `FIXPP_ERR_TAG_NOT_FOUND`; **When** it reads a tag with the wrong accessor flavour (e.g. `get_int` on a string-typed field), **Then** `FIXPP_ERR_TYPE_MISMATCH`; **When** it reads an int/double whose wire bytes are non-numeric, **Then** `FIXPP_ERR_WIRE_INVALID_FRAME`.
3. **Given** a NULL handle, **Then** `FIXPP_ERR_NULL_HANDLE`; **Given** a destroyed/expired inbound handle (read after the dispatch window), **Then** `FIXPP_ERR_INVALID_HANDLE` — never undefined behaviour, never a crash.
4. **Given** an inbound message, **When** the consumer calls the `fixpp_msg_get_msg_type` convenience accessor, **Then** it gets the same result as reading tag `35` directly.

---

### User Story 2 - Construct, populate, commit, and send an outbound message (Priority: P1)

A C consumer needs to build an outbound application message from nothing — create it against a session+message-type, set its scalar fields, commit it into the wire-byte payload, and hand that payload to the Feature-B send — entirely through `extern "C"` calls, then release it.

**Why this priority**: This is the outbound half of the round-trip and the second thing the Python bindings need. Together with US1 it forms the minimum viable pure-C message path. It also makes Feature B's byte-span `fixpp_session_send` *producible* from C for the first time (today a C consumer has no way to build the span).

**Independent Test**: A pure-C program calls `fixpp_msg_create_outbound(session, "D", 1, &msg)`, sets a handful of string/int/double/decimal fields, calls `fixpp_msg_commit(msg, …)` to obtain the committed app-payload span, ships it via `fixpp_session_send`, and `fixpp_msg_destroy(msg)`; confirms the peer receives a well-formed message, that committing a message whose body would exceed the frame cap surfaces `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`, and that destroy is idempotent/NULL-safe.

**Acceptance Scenarios**:

1. **Given** an open session whose dictionary defines msg type "D", **When** the consumer creates an outbound message, sets fields, and commits, **Then** `fixpp_msg_commit` returns `FIXPP_ERR_OK` and a committed app-payload span (leading `35=D` + the set application fields, SOH-terminated) that contains **no** session-framing tags (8/9/34/49/52/56/10 — the session stamps those).
2. **Given** a created outbound message, **When** the consumer sets a STRING/INT/DOUBLE/DECIMAL field, **Then** the bytes are deep-copied into the per-message arena (the caller may free its buffer immediately) and any prior `fixpp_msg_get_*` pointer on the same handle is invalidated; **When** it sets a tag the dictionary marks a different type, **Then** `FIXPP_ERR_TYPE_MISMATCH`; **When** it sets a tag absent from the dictionary, **Then** `FIXPP_ERR_DICT_CONFIG`.
3. **Given** `fixpp_msg_create_outbound` with a msg type not in the session's dictionary, **Then** `FIXPP_ERR_DICT_CONFIG`; **Given** a NULL session/msg_type/out-pointer, **Then** `FIXPP_ERR_NULL_HANDLE`; **Given** a destroyed session, **Then** `FIXPP_ERR_INVALID_HANDLE`.
4. **Given** a committed message whose serialised body would exceed the per-session frame size cap, **When** the consumer commits, **Then** `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` — never a silent truncation.
5. **Given** any outbound message, **When** the consumer destroys it (including twice, or with NULL), **Then** the destroy is idempotent, null-safe, releases the per-message arena slot, and never throws.

---

### User Story 3 - Read repeating groups, including nested groups, from an inbound message (Priority: P1)

A C consumer needs to read the repeating groups of an inbound message — obtain a group cursor by its `NoXxx` tag, learn the entry count, read fields from a given entry by index, and descend into nested groups.

**Why this priority**: Repeating groups are pervasive in real FIX traffic (MarketDataIncrementalRefresh, MultilegOrder, ListOrder, ExecutionReport allocations). An inbound reader that cannot walk groups cannot parse a large fraction of the conformance corpus; the Python bindings need it to expose list/market-data messages.

**Independent Test**: Inside a receive callback for a message carrying a repeating group, a pure-C program obtains the group cursor + count via `fixpp_msg_get_group`, reads a field from entry `[0]` and from entry `[count-1]`, confirms `entry_index == count` returns `FIXPP_ERR_INDEX_OUT_OF_RANGE`, descends into a nested group via `fixpp_group_get_nested_group`, and confirms cursor lifetime is bounded by the parent message.

**Acceptance Scenarios**:

1. **Given** an inbound message carrying a repeating group, **When** the consumer calls `fixpp_msg_get_group(msg, NoXxx, …)`, **Then** `FIXPP_ERR_OK`, a group cursor (aliasing the message; lifetime bounded by the parent `fixpp_msg_t`), and the entry count.
2. **Given** a group cursor with count N, **When** the consumer reads a field from entry index `i ∈ [0,N)`, **Then** the typed value (string/int/double/decimal); **When** `i >= N`, **Then** `FIXPP_ERR_INDEX_OUT_OF_RANGE`; **When** the field is absent from that entry, **Then** `FIXPP_ERR_TAG_NOT_FOUND`; **When** the flavour mismatches, **Then** `FIXPP_ERR_TYPE_MISMATCH`.
3. **Given** an entry containing a nested group, **When** the consumer calls `fixpp_group_get_nested_group(group, entry_index, nested_tag, …)`, **Then** a nested cursor with its own count, bounded by the parent cursor's lifetime.
4. **Given** a `group_tag` that the dictionary does not mark as a `NumInGroup` tag, **Then** `FIXPP_ERR_TYPE_MISMATCH`; **Given** an absent group, **Then** `FIXPP_ERR_TAG_NOT_FOUND`.

---

### User Story 4 - Construct outbound messages containing repeating groups (Priority: P2)

A C consumer building an outbound message needs to emit repeating groups — begin a group by its `NoXxx` tag, add entries, set fields on each entry (including nested groups), and end the group so it is serialised into the committed payload.

**Why this priority**: Required to *originate* (not just parse) list/multil-leg/market-data messages from pure C. It is P2 rather than P1 because the majority of the day-one conformance corpus and the initial Python unblock exercise flat single-instrument messages; outbound group origination is the next increment. **In scope for Feature C** (user decision 2026-06-24 — ships the full `[2i §4.8]` builder surface).

**Independent Test**: A pure-C program creates an outbound message, calls `fixpp_msg_group_begin(msg, NoXxx, &builder)`, adds two entries via `fixpp_group_builder_add_entry`, sets fields on each with `fixpp_entry_set_*`, calls `fixpp_msg_group_end`, commits, and confirms the serialised payload contains the group with the correct `NoXxx` count and per-entry fields; confirms the builder handle is invalidated after `group_end`.

**Acceptance Scenarios**:

1. **Given** a created outbound message, **When** the consumer begins a group, adds entries, sets per-entry fields, and ends the group, **Then** the committed payload carries the repeating group with the correct count and field order per the dictionary grammar.
2. **Given** a group builder, **When** `fixpp_msg_group_end` returns, **Then** the builder (and any entry handles from it) are invalidated; reuse returns `FIXPP_ERR_INVALID_HANDLE`.
3. **Given** a `group_tag` the dictionary does not mark as a group, **When** the consumer calls `fixpp_msg_group_begin`, **Then** `FIXPP_ERR_TYPE_MISMATCH`.

---

### User Story 5 - Session/app failures surface as published, stable C-ABI codes (Priority: P2)

A C consumer that hits a session-layer or application-callback failure (an invalid argument, a send in an invalid state, a `toApp` veto, a callback that threw, a malformed app payload) needs a **stable, named** numeric code it can branch on — not the opaque `FIXPP_ERR_UNKNOWN` sentinel that Features A/B returned for these arms.

**Why this priority**: It closes the named limitations L-050-4 and L-049-2 (for the session/app arms; log/otel stay deferred-by-design), and restores the 050-descoped FR-015/SC-005, before the `0→1` GA freeze makes the numeric layout permanent. It is P2 because the message surface (US1–US4) is byte-functional without it — these failures are *already* reported, just under the catch-all `FIXPP_ERR_UNKNOWN`; this story upgrades their *legibility*. The two `toApp`-originated arms (`app_do_not_send`, `app_callback_threw`) are made end-to-end-reachable by the US6 send-callback hook.

**Independent Test**: A pure-C program drives all five arms — `session_invalid_argument` (operate on an unregistered/invalid session id), `session_invalid_state_for_send` (send before established), `app_payload_malformed` (send a payload carrying a framing tag), `app_do_not_send` (a registered toApp callback returns the veto verdict, US6), `app_callback_threw` (a registered toApp callback returns the error verdict, US6) — and confirms each returns its **published** `FIXPP_ERR_SESSION_*` / `FIXPP_ERR_APP_*` code (not `FIXPP_ERR_UNKNOWN`), that `fixpp_strerror` returns a non-empty static string for each, and that an engine created by a consumer whose recorded minor is **below** the new codes' introducing minor still sees `FIXPP_ERR_UNKNOWN` for them (forward-compat downgrade).

**Acceptance Scenarios**:

1. **Given** a session/app failure on one of the five reachable arms, **When** the consumer receives the result code, **Then** it is the published session/app-block code for that arm, not `FIXPP_ERR_UNKNOWN`.
2. **Given** any published session/app code, **When** the consumer calls `fixpp_strerror(code)`, **Then** a non-empty, static, English description with the same zero-allocation contract as every other code.
3. **Given** an engine created with a recorded consumer minor **below** the new codes' introducing minor, **When** a session/app arm fires, **Then** the return is downgraded to `FIXPP_ERR_UNKNOWN`; **Given** a consumer minor at/above it, **Then** the real session/app code.
4. **Given** the `[2i §4.3]` amendment, **When** the occupancy-drift gate runs in CI, **Then** it passes with the new block accounted for (no drift across the magnitude-domain table, final-layout block, prose, inline comments, prior-doc total, and Appendix D.2).

---

### User Story 6 - Inspect or veto outbound messages via a registered send (toApp) callback (Priority: P2)

A C consumer needs to register a send-side (`toApp`) callback that the engine invokes on the originate path before an application message is transmitted, so it can inspect the outbound message and optionally veto it (suppress the send) or signal a callback failure.

**Why this priority**: It is the outbound mirror of the Feature-B `fromApp` receive callback and the only way a pure-C consumer can exercise the `app_do_not_send` (veto) and `app_callback_threw` (error) arms (US5). It also gives bindings the standard `toApp` tap QuickFIX consumers expect. It is P2 because the core message round-trip (US1–US3) does not depend on it.

**Independent Test**: A pure-C program registers a toApp callback, sends a message, and confirms: (a) with a "send" verdict the message is transmitted and the peer receives it; (b) with a "veto" verdict the send returns `FIXPP_ERR_APP_DO_NOT_SEND` and nothing is transmitted; (c) with an "error" verdict the send returns `FIXPP_ERR_APP_CALLBACK_THREW`; (d) inside the callback the outbound message is readable via the US1 accessors.

**Acceptance Scenarios**:

1. **Given** a registered toApp callback returning the **send** verdict, **When** the consumer sends an app message, **Then** the message is transmitted and `fixpp_session_send` returns `FIXPP_ERR_OK`.
2. **Given** a registered toApp callback returning the **veto** verdict, **When** the consumer sends, **Then** nothing is transmitted and the send surfaces `FIXPP_ERR_APP_DO_NOT_SEND` (the engine's `app_do_not_send` originate-path semantics — DoNotSend, per `[const]`/L-019-4 originate-path-tap scope).
3. **Given** a registered toApp callback returning the **error** verdict, **When** the consumer sends, **Then** the send surfaces `FIXPP_ERR_APP_CALLBACK_THREW` and the engine applies its callback-threw handling (terminal close per the C++ session contract).
4. **Given** a toApp callback invocation, **When** the callback runs, **Then** the outbound message is exposed as a readable `fixpp_msg_t` (US1 accessors apply) whose lifetime is bounded by the callback dispatch window.

---

### Edge Cases

- **Mutate an inbound message** → `fixpp_msg_set_*` / group-builder on an inbound flyweight returns `FIXPP_ERR_INVALID_HANDLE` (inbound is immutable per `[2i §10] Q5`); the consumer must `fixpp_msg_clone` first.
- **Read after dispatch window / after destroy** → `FIXPP_ERR_INVALID_HANDLE`; a returned string pointer used after the window or after a mutating set on the same handle is a documented use-after-invalidation the consumer must avoid (the contract is stated, not enforced on the hot path).
- **Outbound message outliving its session** → tombstoned on session close (FR-009a): `set_*` / `commit` / group-builder → `FIXPP_ERR_INVALID_HANDLE`, `destroy` → NULL-safe no-op; never a post-close arena UAF. A `fixpp_msg_clone` is session-independent and survives.
- **Cross-strand handoff of an inbound handle** → only via `fixpp_msg_clone` (the clone is owner-controlled and independent); reads on the clone are `FIXPP_THREAD_SAFE` (FR-018); passing the raw inbound handle to another strand is outside the contract.
- **Handle-type mismatch** → passing a `fixpp_session_t*` where a `fixpp_msg_t*` is expected is caught by the type-tag check and returns `FIXPP_ERR_INVALID_HANDLE` before any struct read.
- **Commit of an empty / header-only outbound message** → still produces a valid app-payload span (`35=<type>` + trailing SOH); the session adds framing.
- **`get_double` precision** → IEEE-754 ↔ ASCII round-trip (~17 significant digits); PRICE/QTY/AMT consumers use `get_decimal`/`set_decimal` for exactness (documented, not an error).
- **Forward-compat downgrade of a session/app code** to an older-minor consumer → `FIXPP_ERR_UNKNOWN`, never a wrong-meaning code.
- **Exception escaping a steady-state accessor/setter thunk** → invariant violation → fatal log + `std::abort`, **not** translated (per `[arch §5.3]` / `[2i §5.2]` steady-state rule); construction-time thunks (`create_outbound`) translate.

## Requirements *(mandatory)*

> **Two distinct FR groups**, kept separate so Gate A can review the design-doc amendment (Group 2) on its own merits, independent of the message surface (Group 1).

### Functional Requirements — Group 1: CA-008/009/010 message surface

**CA-008 — field read accessors (`[2i §4.6]`)**

- **FR-001**: The C ABI MUST expose tag-keyed field read accessors over a `fixpp_msg_t` — `fixpp_msg_get_string`, `fixpp_msg_get_bytes` (type-agnostic raw view), `fixpp_msg_get_int`, `fixpp_msg_get_double`, `fixpp_msg_get_decimal` — plus the presence/metadata helpers `fixpp_msg_has_tag` and `fixpp_msg_version` (resolved message version). All take a `uint16_t` tag (NOT a per-field-name symbol), return a `fixpp_error_t`, and write the typed result out-parameter, each thunking into `wire::MessageView::get<...>`-equivalent without re-implementing parsing.
- **FR-002**: The string/bytes accessors MUST return a pointer that **aliases the underlying wire buffer** (flyweight rule `[2b §6.4]`) with its byte length — there is **no caller-buffer copy variant** on the read path. The read path MUST be **zero global-heap** (no `malloc`/`new`; any internal offset-table caching is from the per-message arena, not the global heap). The returned pointer's lifetime is bounded by the `fixpp_msg_t` lifetime and is invalidated by (a) the next `fixpp_msg_set_*` on the same handle, (b) for inbound, the receive-callback return, or (c) for outbound, `fixpp_msg_destroy`.
- **FR-003**: The accessors MUST return distinct, documented codes: `FIXPP_ERR_NULL_HANDLE` (NULL), `FIXPP_ERR_INVALID_HANDLE` (destroyed/expired/type-mismatched handle), `FIXPP_ERR_TAG_NOT_FOUND` (absent tag), `FIXPP_ERR_TYPE_MISMATCH` (accessor flavour ≠ dictionary type), `FIXPP_ERR_WIRE_INVALID_FRAME` (int/double wire bytes don't parse as a number), and `FIXPP_ERR_DECIMAL_INVALID` / `FIXPP_ERR_DECIMAL_PRECISION_LOSS` (decimal accessor on bad bytes / a precision-losing non-default trait conversion). No C++ exception may cross the boundary.
- **FR-004**: The C ABI MUST expose `fixpp_msg_get_msg_type` (resolved `35`, 1–3 ASCII chars, aliasing view) as a convenience over `fixpp_msg_get_string(msg, 35, …)`.

**CA-009 — outbound construct, populate, commit (`[2i §4.7]`, §10 commit bridge)**

- **FR-005**: The C ABI MUST expose `fixpp_msg_create_outbound(session, msg_type, msg_type_len, msg_out)` constructing a mutable outbound message bound to the session's per-message arena and dictionary; it MUST return `FIXPP_ERR_DICT_CONFIG` when `msg_type` is absent from the session's dictionary, `FIXPP_ERR_NULL_HANDLE` on NULL inputs, `FIXPP_ERR_INVALID_HANDLE` on a destroyed session.
- **FR-006**: The C ABI MUST expose the setter family over an **outbound** `fixpp_msg_t` — `fixpp_msg_set_string`, `fixpp_msg_set_bytes`, `fixpp_msg_set_int`, `fixpp_msg_set_double`, `fixpp_msg_set_decimal`, and `fixpp_msg_remove_tag`. Setters MUST deep-copy borrowed buffers into the per-message arena (the caller may free immediately), MUST be **zero global-heap** (arena-allocated, not the consumer's allocator), and MUST return `FIXPP_ERR_DICT_CONFIG` (tag absent from dictionary) / `FIXPP_ERR_TYPE_MISMATCH` (dictionary type ≠ flavour) / `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (obvious frame-cap overrun).
- **FR-007**: `fixpp_msg_set_*` and the group-builder MUST return `FIXPP_ERR_INVALID_HANDLE` when applied to an **inbound** (immutable) message; the consumer mutates only after `fixpp_msg_clone` (per `[2i §10] Q5`).
- **FR-008**: The C ABI MUST expose **`fixpp_msg_commit`** (signature authored here — the anchor references but never declares it) that serialises a constructed outbound message into the **committed application-message-payload byte span** — leading `35=<msgtype>` + application fields, SOH-terminated, carrying **no** session-framing tags (8/9/34/49/52/56/10, which the session stamps). The consumer ships the returned span via the **existing, unchanged** Feature-B `fixpp_session_send`. Commit MUST return `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` when the serialised body exceeds the per-session frame cap. v1.0 is **single-message commit only**; a streaming `Writer` C-ABI is post-v1 (`[2i §2]` non-goal #8 / §10 Q6).
- **FR-008a**: The stale `[2i §4.7]` inline prose `fixpp_session_send(session, msg)` MUST be reconciled to the as-built byte-span model (`fixpp_msg_commit` → span → `fixpp_session_send(session, bytes, len)`); this is a **recorded deviation** (analogous to 050's stale `[2i §4.10]` send example) and MUST be noted in the `[2i]` amendment that FR group 2 already authorises — the deviation itself does not separately reopen `[2i]`.
- **FR-009**: The C ABI MUST expose `fixpp_msg_destroy(msg)` — idempotent, NULL-safe, releasing the per-message arena slot, never throwing (per `[2i §4.2.1]` destroy discipline) — and `fixpp_msg_clone(src, clone_out)` producing an independent, owner-controlled outbound-shaped copy (the v1.0 cross-strand-handoff escape hatch), returning `FIXPP_ERR_VERSION_MISMATCH` when the source's resolved version is not in the engine's loaded dictionaries.
- **FR-009a**: An outbound `fixpp_msg_t` bound to a session's per-message arena MUST be **tombstoned when its owning session closes/destroys** (user decision 2026-06-24): once the session is gone, `fixpp_msg_set_*` / `fixpp_msg_commit` / the group-builder on that handle return `FIXPP_ERR_INVALID_HANDLE`, and `fixpp_msg_destroy` stays a NULL-safe idempotent no-op — never a post-close arena use-after-free. This applies the 050 handle-tombstone discipline (`[2i §4.2.2]` `FIXPP_HANDLE_TAG_DEAD`; `[[feedback_cabi_handle_destroy_needs_tombstone]]`). A `fixpp_msg_clone` is independent of any session and is **not** tombstoned by session close (it is owner-controlled, FR-009).

**CA-010 — repeating-group read (`[2i §4.8]`)**

- **FR-010**: The C ABI MUST expose `fixpp_msg_get_group(msg, group_tag, group_out, count_out)` returning a group cursor (aliasing the message; lifetime bounded by the parent `fixpp_msg_t`) and entry count, with `FIXPP_ERR_TAG_NOT_FOUND` (absent group) / `FIXPP_ERR_TYPE_MISMATCH` (`group_tag` not a `NumInGroup` tag).
- **FR-011**: The C ABI MUST expose per-entry field readers `fixpp_group_get_field_{string,int,double,decimal}(group, entry_index, tag, …)` with `FIXPP_ERR_INDEX_OUT_OF_RANGE` (`entry_index >= count`) / `FIXPP_ERR_TAG_NOT_FOUND` / `FIXPP_ERR_TYPE_MISMATCH`, and `fixpp_group_get_nested_group(...)` returning a nested cursor bounded by the parent cursor's lifetime (per `[2c §4.7]` / W-007).

**CA-010 — repeating-group construction (`[2i §4.8]` builder; in scope per user decision 2026-06-24)**

- **FR-012**: The C ABI MUST expose the outbound group-builder family — `fixpp_msg_group_begin(msg, group_tag, builder_out)`, `fixpp_group_builder_add_entry(builder, entry_out)`, `fixpp_entry_set_{string,int,double,decimal}(entry, tag, value…)`, `fixpp_msg_group_end(msg, builder)` — emitting a repeating group into the committed payload per the dictionary grammar, invalidating the builder + entry handles at `group_end`, with `FIXPP_ERR_TYPE_MISMATCH` when `group_tag` is not a group tag, `FIXPP_ERR_INVALID_HANDLE` on an inbound (immutable) message (FR-007), and reuse of an ended builder. Entries support nested groups (a builder may itself begin a nested group within an entry per `[2c §4.7]` / W-007).

### Functional Requirements — Group 2: the `[2i §4.3]` session/app error-block amendment (Article XX)

- **FR-013**: The feature MUST amend the signed-off `[2i §4.3]` master error enum to allocate a **new session/app numeric block** in a currently-unused range (the exact range chosen at `/speckit-plan` with explicit justification and the actual `[2i §4.3]` diff for Gate A; the slot **freezes permanently** at the `0→1` GA cut). The amendment is processed **per Article XX, folded into this feature's Gate A** (043 constitution-amendment precedent) — not a separate `gate-a-ph2` pass unless Gate A objects.
- **FR-014**: The amendment MUST mint published `FIXPP_ERR_SESSION_*` / `FIXPP_ERR_APP_*` codes mapping the **five reachable** C++ `core::error` arms — identified by C++ ordinal: `session_invalid_argument` (119), `session_invalid_state_for_send` (77), `app_do_not_send` (129), `app_callback_threw` (130), `app_payload_malformed` (131) — to fresh C-ABI numeric slots in the new block.
- **FR-015**: The amendment MUST update the **full co-update set in one pass** (or one site lags): `include/fix/c_api/error.h` (`#define` constants), the no-`default` `translate()` in `src/capi/error.cpp` (re-point the five arms off `FIXPP_ERR_UNKNOWN`), the append-only `tools/abi_history/error_codes_v1.txt` audit, the `fixpp_strerror` static lookup table, and `kIntroducingMinor` in `error.cpp` (the new codes' introducing-minor = the 0.4.0 minor).
- **FR-016**: The amendment MUST keep the occupancy-drift gate (`tools/check_capi_occupancy.sh`) green: update the `[2i §1.1]` magnitude-domain table (single source of truth) and sweep **all** derived sites in `[2i]` in one pass — `[2i §1.1]` final-layout block, `[2i §3.11]` prose, `[2i §4.3]` inline comments, `[2i §6.5]` prior-doc total, and Appendix D.2 — and decide+document whether the Phase-4-owned session/app block enters the gate's accounting term or the gate logic gains a new term.
- **FR-017**: With the new codes' introducing-minor = the 0.4.0 minor, the feature MUST make the **SC-004 minor-specific downgrade sub-witness** (deferred by 050) live: on the send-path composition `translate_for_consumer(translate(e), consumer_minor)`, a recorded consumer minor **below** the introducing minor downgrades a session/app code to `FIXPP_ERR_UNKNOWN`, and at/above sees the real code.

### Functional Requirements — Cross-cutting (versioning, reentrancy, allocation, trap)

- **FR-018**: Each new public C-ABI symbol MUST carry exactly one reentrancy class per `[2i §4.10]` — 0 unannotated symbols (the 049 reentrancy CI gate extends to cover them). Assignments: **inbound-flyweight** read accessors + setters + group read/build cursors → `FIXPP_REQUIRES_SESSION_LOCK`; `fixpp_msg_version` → `FIXPP_THREAD_SAFE` (set at parse time, never mutated); `fixpp_msg_destroy` → `FIXPP_THREAD_SAFE`; `fixpp_msg_clone` → `FIXPP_REQUIRES_SESSION_LOCK` on the **source's** session (the clone is constructed on the source strand); **reads on a detached clone → `FIXPP_THREAD_SAFE`** (user decision 2026-06-24 — the clone owns its arena, callable from any thread, caller serializes concurrent same-handle access). The reentrancy gate MUST distinguish the inbound-flyweight-read class from the detached-clone-read class so it does not over-claim `FIXPP_REQUIRES_SESSION_LOCK` for the seam-#13 cross-strand path.
- **FR-019**: The published C-ABI version MUST receive an additive **MINOR** bump `0.3.0 → 0.4.0` (new exported message-surface symbols + new additive session/app error codes). The `0 → 1` major freeze stays deferred to GA per `remaining-work/release-engineering.md` Task 2.
- **FR-020**: Construction-time thunks (`fixpp_msg_create_outbound`) MUST translate caught C++ errors to codes (`[arch §5.3]` carve-out); steady-state thunks (accessors, setters, group cursors, commit) MUST treat an escaping exception as an invariant violation → fatal log + `std::abort` (`[2i §5.2]`), never a translated code.
- **FR-021**: The feature MUST add the `[2i §9]` message-surface seams to the C-ABI test corpus (including the named `fixpp_msg_clone` cross-strand-handoff seam #13, the zero-global-heap allocation guard on both read and set paths, and an **outbound-message-tombstone-on-session-close** seam witnessing FR-009a — `set_*`/`commit` → `FIXPP_ERR_INVALID_HANDLE` and `destroy` no-op after the session is destroyed, with a sanitizer run proving no UAF), and a per-symbol entry in the ABI golden symbol list (`tests/abi/golden/fixpp_capi_symbols.txt`).

### Functional Requirements — Group 3: send (toApp) callback hook (user decision 2026-06-24)

- **FR-022**: The C ABI MUST expose a send-side (`toApp`) callback registration — `fixpp_session_register_send_callback(session, cb, userdata)` (pre-start, mirroring the Feature-B `fixpp_session_register_callback` recv pattern) — routed through `CapiApplication::toApp` (a new override) to the registered C callback on the originate path, before transmit.
- **FR-023**: The toApp callback MUST return a **verdict** (a C callback cannot throw a C++ exception, so the throw-equivalent is an explicit return): **send** (proceed → transmit), **veto** (suppress → `fixpp_session_send` returns `FIXPP_ERR_APP_DO_NOT_SEND`, mapping `app_do_not_send`), or **error** (`fixpp_session_send` returns `FIXPP_ERR_APP_CALLBACK_THREW`, mapping `app_callback_threw`, and the engine applies its callback-threw handling). The exact verdict encoding (return code vs out-param) is authored at `/speckit-plan`/contracts.
- **FR-024**: Inside the toApp callback the outbound message MUST be exposed as a **readable** `fixpp_msg_t` (the US1 accessors apply), lifetime bounded by the callback dispatch window. Scope is the **originate-path tap** only (per L-019-4 — ResendRequest retransmissions are not surfaced to toApp); the callback's reentrancy class is `FIXPP_REQUIRES_SESSION_LOCK` (runs on the session strand).

### Key Entities

- **`fixpp_msg_t`** — opaque message handle. **Inbound** flavour: a non-owning, immutable observer of a wire flyweight, lifetime bounded by the receive-callback dispatch window; reads are `FIXPP_REQUIRES_SESSION_LOCK`. **Outbound** flavour: a mutable, owner-controlled message bound to the session's per-message arena, lifetime ended by `fixpp_msg_destroy` **or tombstoned when its session closes** (FR-009a). A **clone** is a session-independent outbound-shaped copy that survives session close and whose reads are `FIXPP_THREAD_SAFE`. All flavours share the handle type + type-tag/tombstone plumbing from Feature A.
- **`fixpp_group_t`** — opaque, read-only repeating-group cursor aliasing a parent `fixpp_msg_t`; carries an entry count; supports nested descent.
- **`fixpp_group_builder_t` / `fixpp_entry_t`** — opaque outbound group-construction handles (FR-012, in scope); invalidated at `fixpp_msg_group_end`.
- **Committed app-payload span** — the `(const bytes, len)` output of `fixpp_msg_commit`: `35=<type>` + application fields, SOH-terminated, **no** session-framing tags; the unit `fixpp_session_send` already accepts.
- **`fixpp_error_t` session/app block** — the new published numeric block (FR-013/FR-014) mapping the five reachable C++ session/app arms to stable C-ABI codes.

## Out of Scope

- **Streaming/multi-frame outbound serialise API** — v1.0 is single-message commit (`[2i §2]` non-goal #8 / §10 Q6); post-v1.
- **Per-field-name typed accessors** (`fixpp_NewOrderSingle_get_cl_ord_id`) — the v1.0 surface is tag-keyed (`[2i §3.10]` commitment 3).
- **`fixpp_msg_get_dict` reflection / dictionary handle on the message** — dictionary-agnostic in v1.0 (`[2i §10] Q7`).
- **log/otel C-ABI error arms** — they STAY `FIXPP_ERR_UNKNOWN` (their C-ABI functions do not exist yet; `[1000,1099]` reserved, post-v1). L-049-2 is discharged **only** for the reachable session/app arms; the log/otel leg is deferred-by-design, not an open gap.
- **049's FR-015 (version accessor)** — already shipped in Feature A; "closes FR-015/SC-005" in this spec refers to the **050** deferred error-block FR-015/SC-005, not 049's version accessor.
- **Mutating inbound messages in place** — inbound is immutable (`[2i §10] Q5`); clone-first.
- **Transport/TLS/endpoint C-ABI handles** — deferred to v1.x (`[2i §7.7]` / §1.2 non-goal #7).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A pure-C program (no C++ headers linked) reads every scalar type (string/int/double/decimal) and the msg type from an inbound message, and constructs + commits + sends an outbound message that a peer receives well-formed — the full read+write round-trip exercised end-to-end.
- **SC-002**: A pure-C program walks a repeating group (count, per-entry field reads, one nested descent) from an inbound message and reads correct values for every entry.
- **SC-003**: Every read accessor and every setter is verified **zero global-heap** under the allocation guard (counting-resource + mallocnesia LD_PRELOAD dual gate); the read string pointer is confirmed to alias the wire buffer.
- **SC-004**: All **five** session/app arms — `session_invalid_argument`, `session_invalid_state_for_send`, `app_payload_malformed` (direct send path) and `app_do_not_send`, `app_callback_threw` (via the US6 toApp callback) — return their **published** `FIXPP_ERR_SESSION_*` / `FIXPP_ERR_APP_*` code (not `FIXPP_ERR_UNKNOWN`) from a pure-C end-to-end stimulus, each with a non-empty `fixpp_strerror` string and the live minor-gated downgrade witnessed (below-minor → `FIXPP_ERR_UNKNOWN`, at/above → real code).
- **SC-005**: The occupancy-drift gate and the ABI-golden symbol gate both pass with the amended `[2i §4.3]` and the new message-surface symbols; `abidiff` reports the change as **additive** (no breaking re-definition).
- **SC-006**: `fixpp_msg_clone` enables a verified cross-strand handoff — a clone read on strand B matches the source byte-for-byte after the source's dispatch window closed on strand A (`[2i §9]` seam #13), within the ≤ 1 µs warm-cache clone budget for a ~200-byte message.

## Assumptions

- **Outbound group construction (FR-012 / US4) is in scope** (user decision 2026-06-24): the feature ships the full `[2i §4.8]` builder surface (read + write groups). No group limitation is carried.
- **The new session/app block's numeric range is chosen at `/speckit-plan`** as an explicit, justified artifact with the real `[2i §4.3]` diff; Gate A reviews the diff. The spec fixes the *constraint* (fresh unused range; permanent at GA) not the *value*.
- **`fixpp_msg_commit`'s exact signature is authored at `/speckit-plan`/contracts** against the real `wire`/`Session` serialise surface (source-verified, like 050's send), returning a span the existing `fixpp_session_send` accepts byte-for-byte.
- **The 049/050 C-ABI infrastructure is reused**: the opaque-handle type-tag plumbing + tombstone discipline, the per-symbol reentrancy CI gate, the ABI-golden symbol gate, the occupancy gate, and the pure `translate_for_consumer(code, consumer_minor)` (now fed the live recorded minor).
- **The build/verify caps hold** (`[[feedback_build_resource_cap_oom]]`): max `-j2`, sanitizer presets and the verify matrix run strictly ONE AT A TIME (WSL2 OOM).
- **After merge, all three CI tiers run** (`run-tier1` Linux + `run-tier2` Windows/MSVC + `run-tier3` libc++) to re-validate the full cross-platform C-ABI surface at the last-C-ABI-feature milestone (Tier-4 macOS still TBD). This is a post-merge action, not an acceptance criterion of the spec.

## Behaviors & Limitations (proposed — finalised at Polish per `[[project_behaviors_limitations_catalogue]]`)

- **B-051-1** — A C consumer can read any inbound field/group and construct/commit/send any outbound message entirely through `extern "C"`; the Python bindings unblock on this surface.
- **B-051-2** — Inbound messages are immutable; mutation requires `fixpp_msg_clone` first. Clone is also the only sanctioned cross-strand-handoff path for an inbound message.
- **B-051-3** — Five previously-opaque session/app failure arms now surface published, stable C-ABI codes; older-minor consumers transparently see `FIXPP_ERR_UNKNOWN` for them (forward-compat downgrade).
- **B-051-4** — A C consumer can register a send-side (toApp) callback to inspect/veto outbound messages on the originate path (verdict: send / veto→`app_do_not_send` / error→`app_callback_threw`); this is the outbound mirror of the Feature-B receive callback. Scope is the originate-path tap (ResendRequest retransmissions are not surfaced — L-019-4).
- **L-051-1 (proposed)** — log/otel C-ABI error arms remain `FIXPP_ERR_UNKNOWN` (no C-ABI functions yet; post-v1). L-049-2 discharged only for the session/app arms.

## Dependencies & References

- **C-ABI shape contract `[2i]`** (`.specify/2i-capi.md`): **§4.6** field read accessors (CA-008) + flyweight lifetime; **§4.7** setters + `create_outbound` / `destroy` / `clone` (CA-009) + the §10 commit bridge; **§4.8** group read + builder (CA-010); **§4.3** the master `fixpp_error_t` layout + occupancy gate (**amended here**, FR group 2); **§4.9** uniform cancellation translation; **§4.10** reentrancy taxonomy; **§5.2** construction-vs-steady thunk split; **§10** Q5 (immutable inbound) / Q6 (single-frame commit) dispositions; **§9** seams (incl. #13 clone cross-strand).
- **Feature A artifacts (049)**: `tools/abi_history/error_codes_v1.txt`, `tools/check_capi_occupancy.sh`, `tools/check_capi_reentrancy.sh`, `tests/abi/golden/fixpp_capi_symbols.txt`, the pure `translate_for_consumer`. Discharges **L-050-4** and **L-049-2** (reachable session/app arms only).
- **Feature B artifacts (050)**: the as-built `fixpp_session_send(session, const bytes, len)` byte-span send (Feature C produces the span via `fixpp_msg_commit`); the receive callback that hands back the inbound `fixpp_msg_t` Feature C makes readable; the engine-owned io_context + worker model.
- **C++ surface**: `wire::MessageView` (inbound read), the outbound serialise/`Writer` surface (`[2c §4.7]`), `core/error.hpp` (the five session/app arms — ordinals 119/77/129/130/131), `Session`/`Engine` per-message arena (`[arch §5.2]`).
- **Constitution**: Article X (ABI policy), Article XX (amendment process — folds the `[2i §4.3]` amendment into Gate A), Article XVII §8 (verification-gate / completeness-audit rules).
