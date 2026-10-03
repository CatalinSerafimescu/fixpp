# Research: 093-inbound-frame-dispositions

`/speckit-plan` extends this file. R-1 was done at specify time because the owner made the FR-004
ruling conditional on it.

## R-1: a frame whose third field is not MsgType(35)

**Question.** A correctly framed frame (8= first, 9= second with a correct count, 10= last and correct)
whose third field is not 35, e.g. `8=FIX.4.2|9=..|34=2|35=0|…|10=..|`. Must it be disregarded as
garbled, Rejected with 373=14, accepted, or met with a disconnect? Does the answer differ before and
after Logon?

**Decision.** Disregard it as garbled: no Reject, NextNumIn unchanged, continue. This holds in every
state and in both validation modes (FR-004). It confirms the owner's ruling of 2026-10-02.

### Primary documents

The local copies are under the parent's `research/G19-fix-fpml-iso20022/research/`. Text was extracted
with pypdf; page numbers are the PDF's own.

- **FIX Session Layer 2020 §4.5.2 (pp. 26–27).**
  - The definition: "A message shall be considered garbled if … MsgType(35) is not the third tag in a
    message."
  - The rule: "The receiving FIX session processor must disregard the garbled message and not
    increment NextNumIn."
  - §4.5.4 scopes Reject to a message that is "not garbled".
  - §8.5 StandardHeader (p. 62): "MsgType(35) must be the third field in the message."
  - Code 14 ("Tag specified out of required order") appears only in the code list (p. 80).
- **FIX Session Test Cases 2020.**
  - Scenario 2 row t (p. 11): "BeginString(8), BodyLength(9), and MsgType(35) are not the first three
    fields of message. 1. Consider garbled and ignore message. 2. Do not increment NextNumIn.
    3. Continue accepting messages." It carries no state qualifier.
  - The only 373=14 row is Scenario 14 row g: header, then body, then trailer interleaving. That is a
    different defect.
- **FIX 4.4 with 2003-06-18 errata.**
  - Vol 1, p. 18: "The first three fields in the standard header are BeginString … BodyLength …
    MsgType".
  - Vol 2, p. 30, "What constitutes a garbled message": "MsgType (tag #35) is not the third tag in a
    message."
  - Vol 2, p. 43, rows 2s and 2t: "Consider garbled and ignore message (do not increment inbound
    MsgSeqNum)".
- **FIX 4.2:** not obtained (fixtrading.org did not serve the document). The 2020 Session Layer's
  BeginString row lists FIX.4.2.

### Engines

Shallow clones of each default branch, 2026-10-02: QuickFIX/C++ `386ce46e`, QuickFIX/J `db43e4d4`,
QuickFIX/Go `eff945dd`, QuickFIX/n `03081287`.

| Engine | After Logon | Before Logon | Notes |
|---|---|---|---|
| QuickFIX/C++ | ignored (logged) | **disconnect** (`SocketConnection.cpp` drops the socket when not logged on) | The check is `Message.cpp` `headerOrder[]` and throws `InvalidMessage("Header fields out of order")`. It is skipped when `ValidateLengthAndChecksum=N`. 373=14 comes from `DataDictionary` `hasValidStructure`, which never covers the first three fields |
| QuickFIX/J | ignored (logged) | ignored, unless the frame is itself a Logon (disconnect) | `Message.java` preamble check, guarded by having a data dictionary. `RejectGarbledMessage=Y` (not the default) sends a text-only Reject with no 373 |
| QuickFIX/Go | ignored (`OnEventf`, no state change) | **disconnect** on the acceptor's first frame | `message.go` extracts 8, 9 and 35 in order. There is no toggle |
| QuickFIX/n | ignored | ignored, unless the frame is a Logon (disconnect) | `Message.cs` mirrors C++, and so does its toggle |

The shared acceptance test is `test/definitions/server/fix42/2t_FirstThreeFieldsOutOfOrder.def`. It sends
fixpp#514's exact shape after Logon (`34=3^35=0`), then resends 34=3 as a TestRequest, and expects the
Heartbeat reply at outbound 34=3. So the engine sent nothing in response and did not consume the
number. No repo excludes the test.

### Consequences for the spec

- No source supports 373=14 for this shape. B-005-7's description of a QuickFIX divergence, "QF emits
  373=14", is wrong for field 3. QuickFIX emits 373=14 for header, body and trailer interleaving.
- **Before Logon the engines split.** C++ and Go disconnect, while J and n ignore the frame unless it is
  a Logon. No spec text requires a disconnect. FR-004 and FR-005 disregard the frame, and FR-006's
  establishment timeout bounds the wait. Gate A may revisit this. A disconnect is defensible under TC
  Scenario 2S ("first message received is not a Logon → disconnect"), but it is not mandated.
- **Validation off.** C++, J and n skip the check when validation is off and then accept the frame.
  fixpp has no equivalent toggle for framing checks, and FR-004 applies in both of fixpp's validation
  modes.

---

The sections below were written at `/speckit-plan`, 2026-10-02, from three read-only code surveys at
`00c1f720`. Line numbers are leads at that commit. Re-derive them before citing.

## R-2: Framer resync and garbled-frame accounting (#514; FR-001 to FR-003, FR-008)

**Facts.**
- `parse_frame` (`src/wire/framer.cpp`) checks the shape `8=`…SOH and never reads the BeginString value.
  Both header scans use an unbounded `find_soh`: the BeginString value is scanned to its SOH, and the
  BodyLength value to its SOH, before its digits are checked.
- `parse_frame` sums the CheckSum over `[0, checksum_off)` once `10=` sits at the counted offset with
  three digits and a trailing SOH.
- `feed` begins every call by erasing the bytes the previous call consumed (`consume_front`, a
  `pmr::vector` front erase). The pump's drain loop calls `feed` with an empty `incoming` once per
  produced frame, so every drain call moves the carry's remainder.
  - Error kinds: `wire_framing_resync` (leading bytes are not `8=`), `wire_invalid_body_length`,
    `wire_checksum_mismatch` and `wire_frame_too_large`.
  - `wire_invalid_body_length` covers a missing, empty or non-digit 9, and also `10=` not being at
    `body_off + BodyLength`. So "CheckSum not last" (TC 3e) reports as a BodyLength failure.
- Every error clears the whole carry (`feed`'s `fail` path). With an `out` span longer than one, an error
  also discards frames already produced in that call. The pump's `out` has length 1.
- `wire_frame_too_large` comes from three places:
  - BodyLength digits that overflow, or a value over `max_frame_bytes`;
  - `frame_len > max`, tested **after** the checksum;
  - carry overflow in `feed`.

  The pump's Framer is default-constructed, with a 256 KiB limit, but its carry is 64 KiB. So the carry
  is what bounds frames today.
- **Callers of `Framer::feed`** (the header is public):
  - the pump (`engine.cpp`, twice);
  - `read_first_frame_bounded`;
  - `parse_and_dispatch_` and `validate_inbound_`, each re-framing one exact frame;
  - `reify.cpp` and the generated reify code;
  - three fuzz harnesses and six benches;
  - about 60 test files (`git grep -n -E "\bFramer\b|\.feed\(" -- tests`).

  No C-ABI or Python caller.
- The pump is co_awaited inline on the session strand, so a synchronous `Session` member called from it
  is race-free.
- `SessionEvent` (`include/fixpp/session/session_event.hpp`) is a variant to which alternatives are only
  appended. No `std::visit` runs over it, and it is not in the C ABI. The ring holds 16 entries.
- **No production log call exists in `src/`.** `SessionConfig::logger_override` is written by the
  resolver and never read by the session.

- **One `Session` per `SessionEntry` per `Engine::start()`.** Both role loops `co_return` after the
  pump of their first matching connection, `start()` runs once, and `entry.session` is retained for
  `lookup()` after the connection ends. The accept loop is per registry entry, with its own listener.

**Decision (re-derived at Gate A round 1; contract C-1).**
- **Resync lives inside the Framer, opt-in**, behind `Framer::Config::resync_on_garble` (default `false`).
  Every other caller keeps its behaviour byte for byte.
- With the flag on, any error other than `wire_frame_too_large` opens or continues a garbled region,
  and the scan continues **in the same call**. There is no re-feed per garble.
  - At a frame boundary, framing is as today. After a garble, the next frame start is the next
    `8=FIX`, whatever precedes it (plan.md OD-11).
  - A structurally complete candidate whose CheckSum value is wrong is consumed through its own end
    as one garble. Every other garble is searched past from the byte after its first byte.
  - If the call has already produced a frame, it stops before resolving a later garble.
- **Bounded work** (C-1 W-1 to W-4):
  - compact only when the incoming bytes would not fit after what the carry holds. A pending
    candidate is at most L bytes, and the carry is L plus one read, so each compaction moves at most L
    bytes per read's worth of appended bytes, whatever the segmentation. Compacting on every
    non-empty feed would move up to L bytes per one-byte read;
  - in resync mode, at every candidate including a boundary, cap the BeginString scan at the
    BeginString cap and the BodyLength digit run at `kBodyLengthDigitCap`, both over encoded bytes, and
    read BodyLength digit by digit against L (revised at Gate A round 2, G93-A-06, G93-O2-02);
  - sum a CheckSum only over a structurally complete candidate, which is then consumed whole.
- **State:** one `searching_` flag, plus at most four held bytes that are a proper prefix of `8=FIX`.
- **Reporting:** one `garble_summary{regions, first_kind, discarded}` per call, read with
  `last_garbles()`.
- **`frame_len > L` is checked as soon as `body_off` is known, before the checksum, in resync mode
  only.** An over-L frame with a bad checksum then closes (FR-013) rather than being disregarded. Strict
  callers keep today's order.
- The pump calls `note_garbles_(summary)` through `session_engine_access`. It is synchronous and
  `noexcept`. It adds to the counter, emits one `session_event_garbled_frame` per summary with
  `regions > 0` (a summary that only continues a region emits none, data-model E-4),
  and logs rate-bounded through the logger resolved at `open()` (data-model E-12).
- `read_first_frame_bounded` runs its Framer as `{max = L, resync on}` and returns `{offset, len,
  summary}`. After `open()`, the engine hands the summary to the Session once.

**The two header caps (added at Gate A round 2).**
- **Why the BodyLength cap counts digits, not the value.** A value cap alone leaves
  `8=FIX.4.4␁9=000…`, fed one byte per read, rescanned through every zero on every feed: Θ(k) per byte,
  up to the carry. The current scan shape locates the BodyLength terminator from the start and then
  parses every digit (`src/wire/framer.cpp`), so without a cursor each feed repeats it. FIX `int`
  permits leading zeros, so the cap must leave room for legitimate padding.
- **Recipe for `kBodyLengthDigitCap`, run at implementation.** The cap is the larger of:
  1. the decimal width of the largest L (262144), with an allowance for zero padding;
  2. the longest BodyLength digit run, leading zeros included, in the repo's FIX-TC fixtures and interop
     goldens. Search for a `9=` that follows each SOH spelling the fixtures use (the C escape `\x01`,
     `^A`, `|`, and a literal 0x01 byte), because a bare `9=[0-9]+` also matches tags such as 49 and
     109, and a `[^0-9]9=` filter misses the `\x019=` spelling. Seed one padded run in a scratch copy
     and show the search finds it before its maximum is believed.

  The chosen value must also meet the ceiling that contract C-1 W-2 states, so that the cap's term stays small
  next to the bound's other terms: a large cap would reopen the one-byte-read rescan while the bound
  stayed technically true. If the search finds a run above the ceiling, stop and record it here rather
  than raising the ceiling.

  Record the chosen value, the date, the search's output, the allowance's reason, and that the value
  meets both the floor and the ceiling, here. Never write it into a comment.
- **Why the BeginString cap takes the configured length.** A fixed cap at the longest supported
  identifier garbles every inbound frame of a session configured with a longer BeginString, which today
  works: the C setter checks only non-empty and forbidden bytes, TOML copies the string, and `open()`
  checks only the FIXT.1.1 combinations. Taking `max(longest supported, cfg.begin_string.size())`
  keeps that session framing its own frames with no new refusal (plan.md OD-16).
- **What the search prefix still assumes.** After a garble the search looks for `8=FIX`. A configured
  BeginString that does not begin with `FIX` gets no resync after a garble (contract L-16).

**The counted-work bound's constant (contract C-1 "The bound"; tasks.md T017a).** Derived at
implementation, once T017 has fixed the counters' shape, from W-1 to W-3: one unit per byte read,
summed or moved; W-1's compaction contributes per L ÷ R, W-2's per-feed header rescan the two caps, and
W-3 a constant, since each byte is summed at most once. Record here the value, its derivation step by
step from each W clause, the date and the head. Q-4's cells and `fuzz_wire_framer`'s resync arm assert
against it, and quickstart §2's work-bound mutants must exceed it. Never write the value into a
comment.

**Alternatives rejected.**
- **A persisted BodyLength scan cursor across feeds** (Codex, G93-A-06). It breaks C-1's rule that the
  Framer keeps one flag and nothing else besides the carry.
- **Refusing a configured BeginString longer than the cap** at `register_session`, `open()` and the C
  setter (Opus G93-O2-02's first shape). It adds a C++, C and TOML refusal, and the C one would be a new
  BREAKING row. The `max(...)` form needs no refusal.
- **Resync in the pump.** It cannot recover frames the Framer has already discarded, and it would
  duplicate the scan in two callers.
- **Stepping one byte per `feed`.** `consume_front` is a front erase, so stepping byte by byte is
  quadratic over a 64 KiB junk carry.
- **One garble per feed, with the caller re-feeding** (the pre-Gate-A design). Each re-feed front-erases
  the remainder. With `8=␁` triples behind a failed large candidate, that is about L/3 garbles, each
  moving the tail: Σ(L − 3k) ≈ L²/6 bytes (derived, Gate A round 1, G93-A-04). The same holds for small
  frames drained one per feed behind a failed large candidate, and for nested candidates resolved one
  per one-byte read. That is why W-1 compacts only when the incoming bytes do not fit.
- **A frame start of `8=` after an SOH only** (the pre-Gate-A rule). It loses the good frame after any
  garbled region that does not end in SOH. That covers leading junk `XYZ8=FIX…` and a truncated frame,
  whose last byte is a value byte (G93-O-01).
- **`␁8=` as an additional start.** Every supported profile begins with `FIX`, so it recovers no
  supported frame that `8=FIX` misses, and it needs the "previous byte was SOH" state.
- **`8=` anywhere.** It matches tag suffixes (`38=`, `58=`) in every payload.
- **A running prefix sum for CheckSums.** It keeps nested candidates inside a wrong-CheckSum frame
  eligible, but it costs a second carry's worth of memory per session. Consuming the wrong-CheckSum
  frame through its own end costs nothing, and it is what QuickFIX/C++ does. Its
  `Parser::readFixMessage` (at `386ce46e`, read 2026-10-02) extracts a message through the `␁10=…␁`
  after the counted BodyLength, and only then validates the CheckSum, so a message with a wrong
  CheckSum is dropped whole.
- **Reporting the §4.5.2 criterion.** The Framer cannot tell criterion 2 from criterion 4. The event
  carries the failure kind, and B&L says so.
- **Making the flag the default.** That changes reify, the re-framing parse helpers and the fuzz
  harnesses, which want strict framing.
- **A per-region record list for the first-frame read.** At 3 bytes per region, the 4096-byte budget
  allows about 1365 records per accept (derived, G93-A-08). A summary carries what the counter needs.
- **The counter on `SessionEntry`** (G93-O-02's fix shape). Under today's one-Session-per-entry
  lifecycle it adds no observable value. A reader from any thread would also need a new path that
  survives `stop()`'s `registry_.clear()`, which the reader snapshot already gives the `Session`.

## R-3: Inbound limit L and parse capacity (#515; FR-010 to FR-014)

**What a parse allocates from its resource.**
- `OffsetTable::build` allocates `entries_` (12 B per field, with every field including 8, 9 and 10) by
  `push_back` with no reserve.
- It allocates `overlay_` (4 B per slot) as one exact `assign` of `overlay_cap_for(n)`: the next power of
  two at or above 1.25n+1, with a minimum of 8.
- Nothing else is allocated during the parse.
- The validator and the header scan allocate nothing per field.
- Lazy reads inside a callback allocate on demand: `group_index_`, group slices, nested tables,
  `unk_items_` and C-ABI cursors. Their failure on exhaustion is per API, and R-10 has the facts.

**Densest field.** `1=<SOH>` is 3 bytes. An empty value is accepted by `build` and by the scan's
`length_data_carry::read_value`, so Nmax(L) = ⌊L/3⌋.

**Ceilings today.**
- `default_max_offset_entries` (`offset_table.hpp`) fails a frame with more fields than the cap on every
  lane. 092's LateSite cells use exactly that trigger (`kLateFillerFields`). So the earlier claim that
  "MSVC debug never fails" is false.
- The arena runs out first on 2× growth, at roughly 512 fields in 16 KiB. That figure is derived, not
  measured. MSVC's 1.5× growth fails at a different count.

**Decision.**
- One function, `inbound_limit_for(cfg)`, computes L: the advertised 383 if set, else 64 KiB. It is
  called by `register_session`'s gate, by the accept loop before the first-frame read, and by
  `open()`. The first and third of those refuse a value under 4096 or over 256 KiB with
  `invalid_session_config` (OD-2).
- L is stored on the Session. The pump reads it through `session_engine_access` and builds
  `Framer{.max_frame_bytes = L, .resync_on_garble = true}` over the carry that `open()` allocated,
  sized L + the read size. The first-frame read builds the same Framer config over its own carry, so
  a first frame over L is refused there too (G93-O-06).
- With N(L) = ⌊L/3⌋ + 1, the parse buffer is B(L) = 12·N(L) + 4·`overlay_cap_for`(N(L)) + `kAlignPad`
  + `kCallbackReadHeadroom` + `kContainerSlack`. The last three cover alignment, callback reads and the
  MSVC-debug container proxies. It is allocated once in `open()` from `session_arena_`.
  - N(L) is the count the reserve can reach at `frame.size() = L`. The pre-Gate-A formula over ⌊L/3⌋
    was one entry short (G93-A-09).
  - `bad_alloc`, for the buffer and for the carry, is mapped to an `open()` error.
- `parse_and_dispatch_` and `validate_inbound_` build a fresh `monotonic_buffer_resource` over that span
  on each call, which is how they reset today. `validate_inbound_` is `const`, so the span is `mutable`.
- Each parse passes `reserve = min(N(L), frame.size()/3 + 1)` as a **call argument**, not a `Config`
  field. `OffsetTable::build` is private and `Parser::parse` builds the table through `MessageView`'s
  constructors, so the argument is threaded through a new `Parser::parse` overload, a private tagged
  `MessageView` constructor and a new `OffsetTable` constructor overload (data-model E-3; revised at
  Gate A round 2, G93-A-05).
- The carry's `pmr_carry_buffer` constructor is `noexcept` and reserves its capacity. `open()` therefore
  allocates the block itself, inside a `try`, and builds the carry over a `monotonic_buffer_resource`
  covering that block, so the constructor's reserve cannot fail. The block includes `kContainerSlack`,
  because MSVC's debug STL allocates a container proxy from the allocator at construction (data-model
  E-2).
- The session's `OffsetTable::Config::max_offset_entries = N(L)`.
- Admin and outbound parses stay on their stack arrays, and 093 does not change them. Their size is
  **not** derived from what they parse (G93-O-11). Derived, Gate A round 1: `Session::send` builds the
  body in a 4096-byte stack buffer. A dense 4 KiB body is about 1365 fields, which needs about
  1366 × 12 B of entries plus 2048 × 4 B of overlay, roughly 24 KiB. That is more than
  `kInboundParseArena`. Those sites ignore `parse_failed`, so the send callback is skipped silently.
  This is pre-existing and out of #515's inbound scope. It is disclosed as contract L-14 and filed as
  a follow-up.
- Upstream: a spill witness, which records any allocation past B(L). It is `null_memory_resource` on
  every lane except MSVC debug, where it forwards and records. Tests assert that nothing spilled.
- **One buffer is safe.** Validate and dispatch run in sequence and are synchronous. Callbacks are
  synchronous, `send()` and `close()` are awaitables, and the callback scope asserts against nesting.

**Alternatives rejected.**
- **Reserving `field_no` from the scan.** There is no size gain, because the buffer must cover Nmax
  anyway. It also couples two scanners that must agree forever (`offset_table.hpp`'s #389 note).
- **A reserve held in `Config`.** Clones and reifies copy `Config` into a `frame_len + 4096` arena, so the
  reserve would push large clones onto the heap. `Config` is also a public-header type.
- **A per-thread buffer, or lazy growth.** The owner ruled per session (Clarifications). Lazy growth
  allocates on the inbound path.
- **Carry = L.** A frame of exactly L would then depend on how the stream is segmented.

**Memory.**
- Today: about 68 KiB live per connection, plus two transient 16 KiB stack arrays per validated frame.
- After: the carry (L + 4096) plus B(L). Worked totals, **derived** at Gate A round 1 (2026-10-02),
  before the three measured constants:

  | L | carry | entries 12·N(L) | overlay | total |
  |---|---|---|---|---|
  | 65536 | 69,632 B | 262,152 B | 131,072 B | 462,856 B, about 452 KiB |
  | 262144 | 266,240 B | 1,048,584 B | 524,288 B | 1,839,112 B, about 1.75 MiB |

  - `overlay_cap_for` rounds up to a power of two, so the ratio to L is piecewise. No constant
    "k·L" is quoted anywhere.
  - B&L states the formula and these two totals, re-derived with the measured constants (SC-007).
  - The pre-Gate-A "about 6·L" left out the carry and was wrong (G93-A-10).
- **Admission bound.** The acceptor's loop serves one registry entry and builds a Session only after a
  first frame whose CompIDs match. The reservation is therefore at most one carry plus B(L) per
  registered session with a live or establishing connection, which is an operator-sized quantity
  (G93-A-10).
- Throughput should not get worse: each frame loses a 16 KiB memset (two when validated), and the
  reserve is a pointer bump. `on_inbound_frame_bench` is not paired in CI, so the plan runs a manual
  paired base-vs-branch, with the base worktree path padded to the same length.

**Tests that flip or rot.**
- The ten `LateSite_*_Closes` cells lose their trigger. FR-014's defence cell replaces them: it shrinks
  the buffer through `session_test_access` after `open()`.
- `test_066_arena_fit_test` keeps private copies of 8192 and 16384. Delete it or re-base it.
- `test_070_max_message_size_test`'s pre-establishment exemption reverses (FR-013).
- `engine_readpump_test`'s 128 KiB oversize body still exceeds L.

## R-4: Establishment timeout (#514; FR-006)

**Facts.**
- Initiator: `drive_reconnect` installs the transport, enters LogonSent and emits the Logon. The engine's
  connect loop runs one cycle per call, and connect retries happen inside `drive_reconnect_attempt`
  before install.
- Acceptor: accept, then the TLS handshake (bounded by `tls_handshake_timeout`), then
  `read_first_frame_bounded` (5 s and 4096 B), then `open()`, attach, deliver, pump.
- `close(terminal)`:
  - writes Disconnected;
  - cancels sleeps and emits total on `root_cancel_`;
  - calls `close_async()` on the transport, which **closes the socket**.
- `record_state_transition_` closes nothing (fixpp#534).
- `Clock::cancel_sleeps()` is clock-wide. `await_deadline` (`read_first_frame_bounded.hpp`) already
  re-arms on a spurious sweep. The liveness loop does not (fixpp#536, unconfirmed).

- **Before the first matching frame, the acceptor has no Session.** Every failure there (the TLS
  handshake bound, the first-frame deadline or byte budget, a CompID mismatch) closes the raw
  transport with nothing recorded. `read_first_frame_bounded` appends every byte it reads to `buf`,
  discarded or not, and rejects once `buf.size()` exceeds its budget. No production log call exists
  in `src/`, so there is no out-of-session log site either.
- After a non-Logon or refused first frame whose CompIDs match, the accept loop still delivers it,
  ignores the result, and enters the pump. So a pre-Active Session with an open transport is reachable
  on the acceptor.

**Decision (re-derived at Gate A round 1; contract C-4).** The deadline is one absolute steady time
per connection, in two phases.
- **Phase (a), acceptor before a Session exists.** From accept, the first-frame read is bounded by
  `min(5 s, deadline − now)` and by its unchanged byte budget, which still counts discarded bytes (the
  spec's Assumption). Expiry or over-budget closes the raw transport, and no event is possible
  (contract L-6).
- **Phase (b), a Session that has not yet reached Active.** It covers the acceptor after the first
  frame's delivery, against the same deadline, and the initiator from the moment `drive_reconnect`
  returns.
  - **Expiry is a check at each loop head** (revised at Gate A round 2, G93-A-01). Before each read and
    before each frame's delivery, in both drains, the pump tests `steady_now() >= abs_deadline` on
    `engine_cfg.clock`. A read that blocks races `async_read_some || await_deadline(clock,
    abs_deadline)` only so that it wakes.
  - Why: `await_deadline` (`src/session/read_first_frame_bounded.hpp`) sleeps to the absolute instant,
    and nothing orders its completion against a read that is also ready. A peer that keeps the socket
    readable past T could otherwise win the race on every iteration, which would make the owner-ruled
    bound depend on asio's completion order. Phase (a) is bounded regardless, because the first-frame
    byte budget bounds its iterations.
  - The deadline runs on `engine_cfg.clock` and ignores `SessionConfig::clock_override`. `Session::open()`
    resolves `effective_clock_` to the override when one is set, so the two can differ. Mock-clock cells
    drive `engine_cfg.clock`. `close()`'s wait runs on `effective_clock_`, which is a separate bound.
  - On TLS, the handshake bound is fixed per listener (`lcfg.accepted_transport_config
    .tls_handshake_timeout` in `src/session/engine.cpp`) and runs before the first-frame read. 093 does
    not shorten it, so phase (a) lasts at most `max(T, that bound)`. When no time remains after the
    handshake, the accept loop closes without a read. `read_first_frame_bounded` takes a relative
    deadline, and its conversion to an absolute time is left alone, because 088's B6 re-arm mutant is
    pinned on that line.
  - On expiry: `note_establishment_timeout_()` (event + log), then `stop_pump()`, which calls
    `close(terminal)`.
- After Active, the plain read.
- The race covers waits for peer bytes only. The local suspensions inside `on_inbound_frame` are not
  raced (G93-A-01, the part Opus judged). The condition: a peer cannot hold them open. Before Active the
  outbound volume is the Logon reply plus at most a refusal, far below a socket send buffer. A store
  operation that never completes hangs the session in any state, as it does today.

**Alternatives rejected.**
- **A detached session timer.** It would need a join counter, and a timer that calls `close()` while
  counted in `liveness_counter_` deadlocks `close()`'s join loop.
- **Arming on every reconnect attempt.** There is no per-attempt Logon to arm for.
- **One connection-scoped watchdog spanning TLS, hydration, the store and the Logon write** (Codex,
  G93-A-01, rounds 1 and 2). A peer cannot hold those suspensions open, so a watchdog over them would
  test the store and the TLS stack, not establishment. A store that never completes hangs every state,
  not only establishment.
- **Uncharging discarded bytes from the first-frame budget** (Codex, G93-A-02). It contradicts the
  Assumption that the bounded first read is unchanged. Phase (a)'s stricter bounds already satisfy
  the ruling's "bounded by the timeout".

**Cost.** One parallel group per blocked read, before the first Active only. FR-052's zero-allocation
scope starts at the first Active. Whether the group allocates is not assumed: asio draws its state from
a per-thread recycling allocator (`asio::detail::recycling_allocator` with the parallel-group tag, in
the Conan-cached asio's `experimental/impl/parallel_group.hpp`), which can serve a block without
calling `operator new`. The verify record measures it (L-13).

## R-5: Liveness refresh placement (#516; FR-020, FR-021)

- In the shared LogonReceived/Active arm, the order is:
  1. the 383 check (above the switch, Active only);
  2. the scan;
  3. the fault check;
  4. the validate gate;
  5. the BeginString/CompID guard;
  6. the SendingTime guard;
  7. Reset-mode SequenceReset;
  8. Guard 4 (seq 0, too-high, the PossDup Rejects, `check_inbound`, the too-low cases);
  9. GapFill;
  10. Logout;
  11. Reject(35=3);
  12. the only writer, guarded by `fsm_state_ == Active`.
- **Decision.** One unconditional `last_inbound_steady_ = effective_clock_->steady_now()` goes after the
  fault check and after the new 35-not-third check, before the validate gate. The old writer is deleted.
  - TestRequest matching is independent of the refresh. `pending_test_req_id_` is set by the loop and
    cleared only by an inbound Heartbeat, and the grace deadline uses the `inbound_deadline` computed
    before the sleep.
- **Tests.** `run_liveness_cell` in `tests/session/unparseable_frame_disposition_test.cpp`, on the mock
  clock, gets inverted twins. The 092 FR-018 cells stay green and become real witnesses: reverting the
  disposer would now refresh. `heartbeat_testrequest_test.cpp` is a second home.
  - Grep found no cell pinning "too-high does not refresh". So the move is followed by an unfiltered run
    of the whole session suite.

## R-6: A closing session, and the atomic reset unit (#523, #524; FR-030, FR-040 to FR-042)

**#523.**
- `close()` sets `state_ = closing`.
- A terminal close writes Disconnected before it first suspends. A graceful close from NotConnected or
  LogonSent yields only in its flush hook.
- So the window exists only with a store whose flush yields.
- **Decision.** One check in each of those two arms, right after the header scan and its 35-not-third
  disregard (contract C-2 steps 1 and 2):
  `if (state_ == lifecycle::closing && (fsm_state_ == NotConnected || fsm_state_ == LogonSent)) return
  success;`.
  - Garbled-frame accounting runs before it, because it is not an arm effect (G93-O-05; FR-030's
    carve-out).
- **Tests.** Extend `LogonCloseDuringSuspension` in `tests/session/test_session_plaintext_roundtrip.cpp`
  (`HookedStore`, `CaseRig`).
  - Coalesce by appending the second frame to the single write of the Logon, or of the Logon reply.
  - The RED observable: `validate_inbound_messages = true` plus a dictionary-invalid second frame, whose
    Reject reaches `toAdmin` after close began (`expect_no_admin_after_close`).
  - The flush's 8 posts are replaced by a bounded hold.

**#524, the unit today.**
- Manager `reset_to_one` (inline), then `store_->reset()` (**yields**), then a check, then manager
  `set_next_inbound(2)` (inline), then `persist_inbound_advance_` (**yields**), then a check.
- The initiator adds the outbound restore and one more yield.
- The restore is lost when `close()`'s `seqnum_mgr_.drain()` has run: `set_next_inbound` then returns
  `session_already_closed` (`L-518-1`).

**Decision (FR-041; owner ruling "close() waits, with a timeout"; re-derived at Gate A round 1,
contract C-6).**
1. Compute the targets:
   - `in = advanced ? 2 : 1`;
   - `out = (initiator && own_reset && n_pre_outbound == 2) ? 2 : 1`.
2. Disable cancellation on the awaitable thread (R-9).
3. Set the manager, capturing the first error without returning on it. Each operation grants inline
   under L-518-1's uncontended-fast-path condition, so nothing suspends between steps 2 and 4.
4. If step 3 succeeded: set `reset_unit_in_flight_`, `co_await store_->reset_to(in, out)`, clear the
   flag and signal completion.
5. Restore `enable_total_cancellation()`. No return path precedes it.
6. The existing dispositions on the captured results, whose early returns now follow the restore.
7. Run the existing superseded check, which also tests the engine-stop flag (R-9, added at Gate A
   round 2).

`close()` waits only when it is about to issue its teardown reset and a unit is in flight. The wait
is event-driven: the unit's completion signal raced against `await_deadline` on `effective_clock_`,
bounded per plan.md OD-1. On expiry it proceeds and records an event.
- With a teardown reset and no expiry, the result is 1/1 for every store. Without a teardown reset, the
  result is the unit's values for every store, because the manager was set before the store await and
  the drain can no longer strand the restore.
- After expiry, the result is still 1/1 for an overriding store whose `reset_to` holds a FIFO writer
  lock, but not for a default-body store (contract L-4).
- The pre-Gate-A design polled with `co_await asio::post` in a loop, the shape `close()` uses for the
  liveness counter. Bounded by a clock and waiting on an `fdatasync` plus a rename, that spins the
  strand (G93-O-07). It is replaced.
- The in-unit `teardown_reset_done_` stops become dead and go. The flag remains as `close()`'s latch.
- The true targets are passed to volatile stores too. That may fix fixpp#538 (unconfirmed), so its
  reproduction runs on this branch and on its base.

**The operation.**
`virtual asio::awaitable<expected_t<void>> reset_to(seqnum_t next_in, seqnum_t next_out) noexcept;` with
a default body of `reset()` and then one `next_seqnum(dir, true)` for each target that is 2.
- Precondition `next_in, next_out ∈ {1, 2}`, refused otherwise with `session_invalid_argument`
  (G93-A-16). `next_seqnum` advances by one, so a general target would need a loop that is O(n).
- `MemoryStore`: one critical section that clears and sets both counters, keeping `++generation_`.
- `FileStore`:
  - parametrise `initialise_fresh` (it hard-codes `seqnum_min`), or append a counter record to the temp
    file before the rename;
  - change both the POSIX and the Windows temp branches, Region 3 and the `operation_aborted` catch;
  - restart takes the last counter record, so the rename is the single commit point.

**Subclasses.**
- About 29: two in `include/` and about 27 test-local ones.
- No `reset_to` name exists today. `-Wsuggest-override` is off, and `-Werror` is on with `-Wall -Wextra
  -Wpedantic`.
- A non-pure virtual needs no subclass change. Re-derive the population with `git grep -n "public
  .*MessageStore"`.
- The vtable changes, so code built against the old header must be rebuilt. That is C++ only; the C ABI
  is unaffected.
- `HookedStore` must forward `reset_to` to its inner store and fire its hooks there. Otherwise the
  existing `*StoreEndsAtTeardownReset` cells silently measure the default body.

## R-7: Configuration and C-ABI surface (FR-007, FR-051)

- `SessionConfig::logon_timeout_ms{10000}` sits next to `logout_disconnect_timeout_ms`. That field's
  comment claims a validation nothing performs, so copy neither the pattern nor the comment.
- `open()`'s validation block refuses a zero timeout, and an advertised 383 below 4096 or above 256 KiB.
- **`Engine::register_session`** refuses the same two, beside its existing 041 fail-closed gate
  (G93-O-03). Engine-managed sessions, every C-ABI session among them, run `open()` lazily inside the
  role loops. On the acceptor, an `open()` failure closes that transport and `co_return`s, which ends
  the accept loop for good with nothing reported. The initiator `co_return`s silently too. `open()`
  keeps its checks for sessions used directly. The general silence of a lazy `open()` failure, which
  already applies to a null dictionary, is a follow-up to file.
- **TOML:** add the key to `kRecognized`, with a mapper for a bare integer of milliseconds, as
  `logout_disconnect_timeout_ms`'s is (`src/config/scalar_mappers.cpp`). It requires an integer with
  0 < v ≤ `UINT32_MAX`, and refuses a non-integer or an out-of-range value with a diagnostic
  (G93-A-18).
- **The C setter** follows `fixpp_session_config_set_heartbeat_seconds`: null handle → `NULL_HANDLE`, and
  zero → `FIXPP_ERR_CAPI_CONFIG_INVALID`, as `set_begin_string` does on an empty value.
- **The C getter** follows `fixpp_session_is_established`: an out-parameter checked for null, then
  `check_session`. It reads through `engine_->lookup(id)` and reports 0 before the session exists.
- **Gates:**
  - the symbol golden, sorted;
  - `tools/capi_freeze.sha256` for `session.h` and `version.h`;
  - `tools/check_capi_reentrancy.sh`, one token per doc block: "single-thread" on the setter,
    "thread-safe" on the getter.
- **Version:** MINOR +1 (re-derive the current value from `include/fix/c_api/version.h`). No error code
  is added. BREAKING is placed per declaration, as `[const §X.7]` requires and as 092's 1.10 clauses do.
  `version.h`'s history entry, headed BREAKING with a one-line pointer per row, details an effect only
  where no declaration carries it, and the 1.10 sentences 093 falsifies are
  amended in place (contract C-7, G93-A-07). #523 and #524 are not BREAKING, following B-518-1's owner
  ruling.
- **Python:**
  - the setter is picked up automatically (`%include "fix/c_api/session.h"`);
  - the getter needs `%apply … *OUTPUT { uint64_t* … }`, tested on both wheel lanes because `uint64_t`
    differs between LP64 and Windows;
  - the getter also needs a row in the GIL table, which states it is exhaustive.

## R-8: Tests that pin the old behaviour (re-derive at implementation)

- `engine_readpump_test.cpp` `FramerFailureClosesEstablishedSession_*`: flips (L-004-4).
  `OverCapacityFrameClosesSession` stays.
- `fix_tc_coverage_gaps_test.cpp` `HeaderFieldsOutOfOrder_MsgTypeNotFirst_Accepted_*`: flips (B-005-7).
- `test_validate_gate_inbound.cpp` W1 (373=14 for 35-not-third): flips to disregard.
- `unparseable_frame_disposition_test.cpp`:
  - `PreActive_D8Logon_*_Refused` and the D-8 cells: flip (FR-005);
  - the `LateSite_*_Closes` cells: re-based (R-3);
  - the liveness pin comment: rewritten as a condition.
- `coverage_adversarial_test.cpp`'s 35-position cells.
- `test_070_max_message_size_test.cpp`: the pre-establishment exemption reverses.
- `test_066_arena_fit_test.cpp`: its private constants go.
- `test_session_plaintext_roundtrip.cpp` `*StoreEndsAtTeardownReset` and the
  `close_from_*_persist` cells: re-derived (R-6).
- **The population no grep can find:** test frame builders that emit non-canonical field order. Recipe:
  1. In a scratch copy, add a temporary `std::abort()` on a fault-free `!msg_type_is_third` in every arm.
  2. Run the whole suite unfiltered, including the loopback, C-ABI and Python round-trips.
  3. Collect every failure.

## R-9: Items Gate A round 1 settled, and what P3 and P6 measure

- **Total cancellation and the reset unit.** Settled by design (contract C-6); cells measure it.
  - **What reaches the unit first.** `Engine::start` spawns both role loops bound to
    `entry.session_cancel`'s slot, and each resets to `enable_total_cancellation`. The pump and
    `on_inbound_frame` are co_awaited inline. `Engine::stop()` emits total on that slot in its step 1,
    closes each transport in step 2, joins the loops in step 3, and calls `close(terminal)` only in
    step 4. So the common shutdown path reaches the unit's store await before any `close()`
    (G93-A-05). `close()`'s own `root_cancel_` is not the first signal.
  - **The pre-unit state is not benign.** The peer has reset and we have not, so its next Logon at
    34=2 without 141=Y meets our old NextNumIn, which is #524's own symptom. A `FileStore` "old or new"
    outcome admits exactly that, and `FileStore`'s writer lock is a cancellable `async_mutex`.
  - **asio scoping fact** (read 2026-10-02 in the Conan-cached asio's `impl/awaitable.hpp`).
    `this_coro::reset_cancellation_state` writes the cancellation state of the awaitable thread's
    bottom frame, not the current frame's. Both `await_transform` overloads, for a child awaitable and
    for an async operation, throw `operation_aborted` when that state is cancelled and
    `throw_if_cancelled` is on, which is the default. So a disable inside a helper coroutine outlives
    the helper. `close()`'s own disable is harmless only because the pump `co_return`s after it.
  - **Decision.** The unit disables cancellation before it sets the manager, and restores
    `enable_total_cancellation()` explicitly after the store await, before any disposition (C-6 steps 2
    and 5). No throw point lies between the manager set and the store operation, and no return path
    skips the restore. The cost is L-12: `Engine::stop()`'s join
    waits for an in-flight unit's store operation.
  - **What the shield swallows, and the flag that replaces it** (added at Gate A round 2, G93-A-04).
    After the restore the state is fresh, so stop's emission is not replayed. `logon_arm_superseded`
    tests only `state_ == closing` and the FSM state, and `Engine::stop()` calls `close()` only after
    its join, so the predicate cannot see stop. Read at `00c1f720`:
    - the acceptor, after the unit, emits the reset event, assigns the outbound number, fires
      `toAdmin` and writes the reply, and then reaches Active and `onLogon`, unless stop's step 2 has
      already closed the socket and the write fails;
    - the initiator, after the unit, emits the reset event and reaches Active and `onLogon`. Unless it
      honours a NextExpectedMsgSeqNum(789), nothing in between writes, so it does so whatever step 2's
      timing.

    On the base, total cancellation throws at the arm's next `co_await`, and no callback runs.
    `Engine::stop()`'s step 1 already `co_spawn`s its emit onto each session strand. It now also sets a
    Session-side engine-stop flag inside that lambda, before the emit, through `session_engine_access`,
    with `entry.session` read on the control strand first. The predicate tests the flag, so once stop's
    step 1 has run on the session's strand, step 7's check stops the arm before any of those effects.
  - **The `co_spawn` alternative, re-judged.** It spawns the store await on the session strand with a
    token bound to an empty cancellation slot. The pending cancellation then throws at the arm's next
    `co_await`, which is the behaviour stop needs. But the reset event is emitted synchronously between
    the unit and that next suspension, so the alternative still needs the flag, and it allocates a
    frame per unit besides. The in-place shield plus the flag is chosen (plan.md OD-14, OD-15).
  - **Cells** (quickstart §1): `Engine::stop()` during the unit for `MemoryStore`, `FileStore` and a
    default-body `HookedStore`, with and without a teardown reset. A mutant that drops the shield is
    RED on the default-body and `FileStore` cells.
- **The pre-Active deadline race may allocate** (revised at Gate A round 2, G93-O2-01).
  - It runs only until the first Active, and FR-052's zero-allocation scope starts there. The read-path
    allocation guard cannot see the pump, because it measures `async_read_some` + `feed` directly.
  - **No pmr counter can see it.** asio's parallel-group state goes through
    `asio::detail::recycling_allocator` with the parallel-group tag, and its arms' frames through the
    awaitable-frame recycler. Neither draws on a `std::pmr` resource, so a pmr counting resource reads 0
    with or without the race. The round-1 design's instrument was blind.
  - **Measure:** a global `operator new` counter around the pump's pre-Active reads, shown first to
    count a known allocation (the replacement `operator new` must be linked, or the counter stays at
    0). Record the count in the verify evidence. A lead, not measured: the recycling cache may serve
    each read's block from the previous read's, so the count after warm-up may be 0 even with the race.
  - **Cells.** The disarm is witnessed by behaviour: a session with no application reaches Active
    before T, idles past T, and stays Active (quickstart Q-36), which a never-disarm mutant turns RED.
    Q-19 drives the real pump past Active under the global counter and asserts zero per Active read
    after a warm-up read, as a regression witness only.
  - The shape is the one `read_first_frame_bounded` already ships (088's `operator||` join, which
    replaced a timer handler that outlived its frame).

## R-10: Lazy reads at headroom exhaustion, and fixpp#540 (FR-011, FR-015; added at Gate A round 2)

Read at `00c1f720` on 2026-10-02. Line numbers are not cited; re-derive from the named declarations.

- **`MessageView::unknown_fields()`** (`include/fixpp/wire/parser.hpp`) is `noexcept`. On its first
  call it marks the list built and then `push_back`s each unknown tag into `unk_items_`, a pmr vector
  over the view's parse resource. Over the inbound parse arena that resource's upstream is
  `arena_upstream()`, which is `null_memory_resource` on every lane except MSVC debug
  (`include/fixpp/core/pmr_arena_upstream.hpp`). Exhaustion therefore throws `bad_alloc` out of a
  `noexcept` function, which is `std::terminate`. That is filed as fixpp#540, unconfirmed. `src/` never
  calls `unknown_fields()`, and the C ABI does not expose it, so only a C++ application calling it in a
  callback reaches it. 093 widens what can reach it, because frames of up to N(L) fields now parse and
  are delivered.
- **The catch (FR-015).** Inside the body, keeping `noexcept`: on `bad_alloc`, clear `unk_items_`, keep
  the built flag set so later calls return the same empty view, and return an empty view. An empty view
  after exhaustion cannot be told from a frame with no unknown fields (contract L-5).
- **`OffsetTable::group_slices()`** (`include/fixpp/wire/offset_table.hpp`) is a one-line wrapper over
  the internal status-bearing `group_slices_status()`, and drops the status, so exhaustion gives an
  empty span.
- **`nested_group_slices()`** carries `alloc_failed`, which `fixpp_group_get_nested_group`
  (`src/capi/message_read.cpp`) returns as `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`.
- **`fixpp_msg_get_group`** calls the degrading `group_slices()`. An empty span on a tag that is present
  returns `FIXPP_ERR_TYPE_MISMATCH`, so exhaustion is misreported.
- **The C cursor shells.** Both group getters allocate their `fixpp_group` shell with
  `polymorphic_allocator<fixpp_group>(arena).new_object<fixpp_group>()` from the parse arena, with no
  catch, so exhaustion lets a `bad_alloc` escape the C function, and the session ends or the process
  terminates. This is pre-existing on main, unconfirmed and outside 093 (contract L-17; plan.md
  OD-19). Filed as fixpp#541 (batch B28), with a reproduce-first item.
- **Why no numeric headroom can make exhaustion impossible** (Codex G93-A-02's option 2). Each C
  cursor shell is allocated per call, so a callback that calls a group getter repeatedly allocates
  without bound. A stated headroom bounds how much a callback gets, not how much it asks for.
