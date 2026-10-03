# Data model: 093-inbound-frame-dispositions

Each entity names its visibility: public (an installed header), private (`src/`), engine seam (private
state reached through `session_engine_access`, E-11), or C ABI. Shapes are design intent. The types
the implementation lands are authoritative, and Gate A may revise these.

## E-1: `Framer::Config::resync_on_garble` and `garble_summary`

Public and additive, in `include/fixpp/wire/framer.hpp`. Contract C-1 has the rules.

- `bool resync_on_garble = false;` is a new `Config` member. With it `false`, `feed` is unchanged byte
  for byte, including the order of the `frame_len > max_frame_bytes` check.
- `std::size_t max_begin_string_bytes` is a new `Config` member, read only in resync mode. Its default
  is the length of the longest supported profile identifier. The session's two Framers set it to the
  larger of that default and `cfg.begin_string.size()` (plan.md OD-16).
- `kBodyLengthDigitCap` is a named Framer constant, read only in resync mode: the longest BodyLength
  digit run, leading zeros included, that a candidate may carry (C-1 W-2). Its condition and the recipe
  that derives it are in research R-2; its value is recorded there at implementation.
- `struct garble_summary { std::uint32_t regions; core::error first_kind; std::size_t discarded; };`
  describes what one `feed` call disregarded.
  - `regions` is the number of garbled regions the call opened. A region continued from an earlier
    call is not counted again.
  - `first_kind` is the first opened region's kind: `wire_framing_resync`, `wire_invalid_body_length`
    or `wire_checksum_mismatch`. With `regions == 0` it is `core::error{}` (slot 0, never a failure
    kind) and is not read, because E-4 emits no event for such a summary.
  - `discarded` is every byte the call dropped, including those of a continued region.
- `garble_summary last_garbles() const noexcept;` returns the summary, which every `feed` resets.
- New private state: `bool searching_`, true while a garbled region is open across feeds. The carry
  holds at most the four trailing bytes that are a proper prefix of `8=FIX`. There is no
  "previous byte was SOH" state, because the post-garble start rule is `8=FIX` anywhere (C-1).
- Test access and counted work (FR-053, quickstart §2): `friend struct framer_test_access;`,
  unconditional and never gated on `FIXPP_TEST_HOOKS`, defined once in
  `tests/support/framer_test_access.hpp`. It reads private counters of the bytes the Framer reads, sums
  and moves. The counters are always compiled, so they enter `sizeof(Framer)` (the size pins are
  re-derived by tasks.md T012's command). With the flag off they change no `feed` result, error kind
  or carry state. Their shape (members, types, increment sites) is fixed at implementation (tasks.md
  T017); their cost is in the paired `framer_bench` run.
- With the flag on, and only then:
  - `frame_len > max_frame_bytes` is tested as soon as `body_off` is known, before the CheckSum
    (FR-013, OD-4);
  - the BeginString and BodyLength scans are bounded by those two caps, over encoded bytes (C-1 W-2);
  - the carry is compacted only when the incoming bytes would not fit after what it holds, so a
    carry-only feed never compacts (C-1 W-1).
- The session path adds a fourth kind of its own, `core::error::wire_header_out_of_order` (an existing
  code), for criterion 3 (C-2 step 1). The Framer never produces it.

## E-2: The inbound limit L, the carry and the parse buffer

Engine seam and private, `Session`.

- `std::uint32_t inbound_limit_for(SessionConfig const&) noexcept` is one free function, used by
  `register_session`'s gate, by the accept loop's first-frame read and by `Session::open()`. It returns
  the advertised 383 if set, else 65536. `register_session` and `open()` refuse a value under 4096 or
  over 262144 (OD-2). The accept loop runs only for a registered entry, so it never sees one.
- `std::uint32_t inbound_limit_` is set in `open()` from that function.
- **The read size R** is the size of the pump's per-read buffer, `read_buf` in `run_read_pump`
  (`src/session/engine.cpp`); re-derive it there. FR-002's R, the plan's "L + 4 KiB", this entity's
  "the read size" and research R-3's worked totals all mean that one value.
- The carry is allocated in `open()` with capacity L plus the pump's read size, and needs no public
  change to `pmr_carry_buffer`:
  1. inside a `try`, allocate one block of L + the read size + `kContainerSlack` from
     `cfg_.framer_carry_arena` (else `new_delete_resource()`). A `bad_alloc` is an `open()` error;
  2. build a Session-owned `monotonic_buffer_resource` over exactly that block, whose upstream is the
     same spill witness as the parse buffer's (null on every lane except MSVC debug, where it forwards
     and records);
  3. build the `pmr_carry_buffer` over that resource. Its `noexcept` constructor reserves L + the read
     size, which the block serves, so the reserve cannot fail.
  - The `kContainerSlack` term is needed because MSVC's debug STL allocates a container proxy from the
    allocator when the vector is constructed (`include/fixpp/core/pmr_arena_upstream.hpp` says why). A
    block of exactly L + the read size would then spill, and a null upstream would terminate inside the
    constructor. Quickstart Q-14 runs on the MSVC sandbox for that reason.
  - The pump borrows the carry through the seam. The block, the resource and the carry are released at
    destruction, in reverse order.
- `std::span<std::byte> inbound_parse_buf_` holds B(L) bytes. It is allocated once in `open()` from
  `session_arena_` (research R-3) and released at destruction. It is `mutable`, because
  `validate_inbound_` is `const`.
- With N(L) = ⌊L/3⌋ + 1, the most fields a frame of L bytes can hold plus one:
  B(L) = 12·N(L) + 4·`overlay_cap_for`(N(L)) + `kAlignPad` + `kCallbackReadHeadroom` +
  `kContainerSlack`.
  - `kAlignPad` covers aligning the entry and overlay blocks inside one monotonic resource.
  - The three constants are named, and their values are a measurement recorded in `research.md`, not
    written into a comment.
  - `kContainerSlack` covers MSVC-debug container proxies. A per-lane cell measures the peak with
    `pmr_allocation_tracking_resource` and asserts peak ≤ B(L).
  - **`kCallbackReadHeadroom`'s sizing condition.** Each parse reserves its entries up front (below),
    where the base grows them inside its stack parse arena (the `monotonic_buffer_resource` in
    `parse_and_dispatch_`, `src/session/session.cpp` at the merge base). So at a small L a sparse frame
    can leave callbacks less room than the base leaves. The condition: for every frame the base
    delivers, B(L) minus that frame's up-front reserve and its parse leaves at least the room the
    base's stack arena leaves after the same parse. Check it at implementation on the derived
    constants, at L = 64 KiB (the C-ABI population, FR-010) and at the OD-2 floor. Where it fails,
    contract L-17 says so rather than claiming the C-ABI exhaustion is not worsened.
- `OffsetTable::Config::max_offset_entries = N(L)` for every inbound parse. Each parse reserves
  `min(N(L), frame.size()/3 + 1)` entries, which never exceeds what B(L) budgets.
- Per-session cost: the carry plus B(L). Research R-3 has the worked totals.

## E-3: The parse reserve argument

Public and additive. `OffsetTable::build` is private, and `Parser::parse` builds the table through
`MessageView`'s constructors, so the reserve is threaded through three declarations:
- a new public overload `Parser::parse(frame, mr, OffsetTable::Config cfg, std::size_t
  reserve_entries)`;
- a new private, tag-dispatched `MessageView` constructor that carries the reserve. `Parser` is
  already a friend of `MessageView`, so no new public `MessageView` overload is added;
- a new public `OffsetTable` constructor overload, `(frame, mr, cfg, hooks, reserve_entries)`, which
  passes the reserve to the private `build`. `MessageView` is not a friend of `OffsetTable`, so this
  overload is public.

The reserve is a per-call hint. The table does not store it, and clones and reifies do not copy it.
Every existing overload is unchanged and reserves nothing, which is today's behaviour. A reserve above
`cfg.max_offset_entries` reserves at most that many entries, since the table never holds more. A
reserve the resource cannot serve fails like any allocation in `build`: `build` catches the
`bad_alloc` and reports `out_of_memory` (`src/wire/offset_table.cpp`).

## E-4: Garbled-frame accounting

Private `Session` state, with a public reader.

- `std::atomic<std::uint64_t> garbled_frames_` is monotonic. It is incremented with relaxed ordering on
  the strand and may be read from any thread.
- **Placement.** It lives on the `Session`. The engine builds one `Session` per `SessionEntry` per
  `Engine::start()`: both role loops `co_return` after one connection, and `start()` runs once.
  `lookup()` keeps returning that `Session` after its connection ends. So the counter is monotonic per
  SessionId for the engine's life, and the C getter reads it through the reader snapshot. If the engine
  ever serves a second connection per entry, this placement must be re-derived (plan.md, Gate A
  departures). To re-derive, read what follows the `run_read_pump` call in `run_accept_loop` and in
  `run_connect_loop` (`src/session/engine.cpp`), and the lifecycle note above `class Engine` in
  `include/fixpp/session/engine.hpp`.
- `void note_garbles_(wire::garble_summary const&) noexcept` (seam) adds `regions` to the counter. When
  `regions > 0` it emits one `session_event_garbled_frame` and logs, rate-bounded (E-12). The pump calls
  it after every feed whose summary is non-empty. The accept loop calls it once, after `open()`, with
  the first-frame read's summary. C-2 step 1 calls it with `{1, wire_header_out_of_order,
  frame.size()}`.
  - **The first-frame summary** sums the summaries of every feed the read made (regions and
    discarded added, the first kind kept). The read stops at its first frame, whose end is a frame
    boundary, and the bytes after it reach the pump as `initial_bytes`, so the pump's Framer starts
    at a frame boundary, not searching (C-1 Ordering). No region straddles the first frame's surplus
    and the pump's first feed, so no garble is counted twice or missed across the hand-over.
  - **A region a later feed continues** (`regions == 0`, `discarded > 0`) adds nothing to the counter,
    emits no event and logs nothing. It is counted once, when it opens, and the event for that feed
    carries only the bytes that feed discarded. Its later bytes are not observed, by design.
- `std::uint64_t garbled_frame_count() const noexcept` is a public C++ accessor on `Session`: a relaxed
  load. Any thread may call it, its successive reads never decrease, and it orders no other session
  state, so a caller that needs the count to reflect a given frame must synchronise by other means.

## E-5: New `SessionEvent` alternatives

Public, in `include/fixpp/session/session_event.hpp`: a C++ source change, not C-ABI (contract C-7
row 11). All are appended to the variant, so a `std::visit` over `SessionEvent` with no default arm
stops compiling until it handles the three; the precedent is #424's
`session_event_resend_slot_gap_filled`.

- `session_event_garbled_frame { core::error first_kind; std::uint32_t frames; std::uint32_t
  discarded_bytes; }`. There is one per summary with `regions > 0` (E-4: a summary that only
  continues a region emits none). `first_kind` is one of E-1's four kinds.
  `discarded_bytes` saturates at `UINT32_MAX`.
- `session_event_establishment_timeout { }`
- `session_event_close_reset_wait_expired { }` (FR-041's bounded wait)
- The 16-slot ring can evict older events under a garble flood. That is disclosed (L-9), and the
  counter (E-4) is the durable signal.

## E-6: Establishment deadline

Private, the engine pump and the accept loop.

- `run_read_pump(..., std::optional<steady_time_point> establish_deadline)`.
  - Acceptor: accept time + `logon_timeout`. The same deadline bounds the first-frame read
    (C-4 phase a), whose return becomes `{offset, len, garble_summary}`. The engine then slices the
    first frame and the surplus at `offset`, not at 0.
  - Initiator: the time `drive_reconnect` returns + `logon_timeout`.
- While `!has_reached_active()`, the pump tests `steady_now() >= deadline` on `engine_cfg.clock` before
  each read and before each frame's delivery, in both drains (C-4). A read that blocks races
  `await_deadline` on `engine_cfg.clock`, only so that it wakes. After the first Active, reads are
  plain and nothing is tested.
- The deadline ignores `cfg.clock_override` (C-4).
- On the acceptor, the accept loop computes `deadline − now` after the TLS handshake. When it is not
  positive it closes the transport without calling `read_first_frame_bounded`. Otherwise it passes
  `min(5 s, deadline − now)` as that function's relative deadline, whose conversion to an absolute time
  is unchanged.
- `bool has_reached_active() const noexcept` (seam) reads a **dedicated** latch, `reached_active_`. It
  is set unconditionally in `record_state_transition_` on the first entry to Active, before its
  `engine_.application == nullptr` early return.
  - `onLogon_fired_` cannot serve: it latches only when an application is attached, so a session with
    none would never disarm the deadline and would be closed while Active. Quickstart Q-36 witnesses
    that by behaviour.
  - E-4's placement condition applies here too: a `Session` reused for a second connection would need
    the latch reset when the transport is installed.
  - It is a plain `bool`, written in `record_state_transition_` and read by the pump through the
    seam, both on the session strand (the pump is co_awaited inline on it, research R-2).

## E-7: `SessionConfig::logon_timeout_ms`

Public and additive.

- `std::uint32_t logon_timeout_ms{10000};` Zero is refused by `register_session`, by `open()`, by the
  TOML loader and by the C setter.
- The C ABI setter is `fixpp_session_config_set_logon_timeout_ms(fixpp_session_config_t*, uint32_t ms)`.
- The TOML key is `logon_timeout_ms`, a bare integer of milliseconds, as `logout_disconnect_timeout_ms`
  is. Its mapper requires an integer with 0 < v ≤ `UINT32_MAX`. Each refusal is a `LoadDiagnostic` on
  the key (`include/fixpp/config/load_diagnostic.hpp`): a non-integer with
  `reason_class::malformed_value` (present but wrong type, as `src/config/logger_resolver.cpp`'s
  integer keys refuse one; there is no type-mismatch class), and zero, a negative value or a value
  above `UINT32_MAX` with `reason_class::out_of_range`. `logout_disconnect_timeout_ms`'s mapper is not
  the shape for the non-integer case: it ignores one without a diagnostic.

## E-8: C-ABI garbled-frame getter

- `fixpp_error_t fixpp_session_garbled_frame_count(const fixpp_session_t*, uint64_t* out)`.
- **Refusals**, in `fixpp_session_is_established`'s order (`src/capi/session.cpp`): a null `out`
  returns `FIXPP_ERR_NULL_HANDLE` before anything else is read; then `*out` is written 0, and
  `check_session` refuses a null handle with `FIXPP_ERR_NULL_HANDLE` and a destroyed one with
  `FIXPP_ERR_INVALID_HANDLE`.
- **Read path.** A scoped `engine_->lookup(id)`, which reads the atomic reader snapshot, then a
  relaxed load of the counter through the returned `shared_ptr<Session>`, released before return, as
  `fixpp_session_close` does (`src/capi/session.cpp`). `fixpp_session_is_established` is the pattern
  for the refusals only: it reads the handle's slot and takes no lookup. The `shared_ptr` keeps the Session alive for the read even if
  `Engine::stop()`'s `registry_.clear()` runs at the same time, which is what backs the
  "thread-safe" token.
- **Lifecycle points.** `lookup` returns null until the entry's role loop publishes its Session
  (E-13): before `fixpp_engine_start`, and after it until that publish. The getter then writes 0 and
  returns `FIXPP_ERR_OK`. Once published, the count is monotonic, and `lookup` keeps returning the
  Session after its connection ends. The C ABI reaches `Engine::stop()` only through
  `fixpp_engine_destroy`; after that returns, `check_session` refuses the handle with
  `FIXPP_ERR_INVALID_HANDLE`.
- Python exposes it through `%apply` OUTPUT, with a GIL-table row.

## E-9: `MessageStore::reset_to`

Public, additive and non-pure.

- `virtual asio::awaitable<core::expected_t<void>> reset_to(seqnum_t next_in, seqnum_t next_out) noexcept;`
- Precondition: `next_in, next_out ∈ {1, 2}`. Any other value returns `session_invalid_argument` with
  no effect. The only caller, the 141=Y unit, passes values in that set (research R-6).
- The default body runs `co_await reset()`, then `next_seqnum(dir, true)` once for each target that is
  2. The first error is returned.
- The overrides in `MemoryStore` and `FileStore` are one operation under the store's writer lock: no
  reader, and no restart after a crash, sees an intermediate state.
- The four pure virtuals stay four, against Article XIV §2's limit of five.

## E-10: Reset-unit-in-flight flag and completion signal

Private, `Session`.

- `bool reset_unit_in_flight_` is set across the unit's single `reset_to` await and cleared after it,
  error paths included. It is a plain `bool` because the unit and `close()` both run on the session
  strand, which `teardown_reset_done_`'s existing use already relies on.
- A completion signal, which the unit raises when it clears the flag. One shape is an
  `asio::steady_timer` armed at `time_point::max()` and cancelled by the unit. The implementation picks
  the shape, but it must not poll.
- `close()` awaits that signal raced against `await_deadline(*effective_clock_, now + logon_timeout_ms)`,
  and only when it is about to issue its teardown reset with the flag set (C-6). On expiry it records
  E-5's `session_event_close_reset_wait_expired` and proceeds.
- The unit runs under C-6's cancellation shield, so no cancellation interrupts the await. The flag's
  exit paths are the store's own results.

## E-11: `session_engine_access`

Engine seam. `friend struct session_engine_access;` is declared in `include/fixpp/session/session.hpp`
and defined in a header under `src/session/` that is not installed. It is B21's shape, but for the
engine rather than for tests, so it is not gated on `FIXPP_TEST_HOOKS`.

- It exposes to `run_read_pump` and the accept loop:
  - `inbound_limit()`;
  - `has_reached_active()`;
  - `note_garbles_()`;
  - `note_establishment_timeout_()`;
  - `note_engine_stop_()`, the engine-stop flag's setter (E-13);
  - the borrowed carry.
- Nothing in the installed `Session` surface gains an underscore-suffixed engine hook.

## E-12: Garble log records

Private, `Session`; the session's first production log site (OD-5).

- `FIXPP_SLOG` with the session's `trace_context`, through the logger resolved once at `open()`:
  `cfg_.logger_override`, else the engine's. It may be null, which `FIXPP_SLOG` tolerates.
- Every format string is registered in `src/log/format_registry.cpp`.
- Rate bound: at most one record per `max(HeartBtInt, 1 s)`, where HeartBtInt is the session's
  configured `heartbeat_interval` with the default its liveness loop applies when unset: the value
  its Logon advertises (re-derive in `run_liveness_loop`, `src/session/session.cpp`). It is known from
  `open()`, so the same bound applies in every phase, before the Logon exchange and to the
  first-frame summary handed over after `open()` included. A HeartBtInt of 0 is legal and disables
  liveness, and the 1 s floor still bounds the rate. A record carries the kind and bytes of the garble that triggered it, and the number
  of garbles counted since the previous record but not logged.
- If the logger's bounded queue is full, its default `drop_newest` policy drops the record without
  blocking the strand. The counter (E-4) stays exact.
- **The establishment-timeout and over-L close records** (FR-006, FR-013) take the same path: the
  same logger, `FIXPP_SLOG` with the session's `trace_context`, and format strings registered beside
  the garble's. The timeout record carries T; the over-L record carries the failure kind and L. Each
  fires at most once per connection, because each ends it with `close(terminal)`, so neither is
  rate-bounded.

## E-13: The engine-stop flag

Private `Session` state, set through the engine seam.

- `bool engine_stop_requested_`, false at construction. It is written and read only on the session
  strand.
- `Engine::stop()`'s step 1 reads `entry.session` on the control strand, captures it, and inside the
  `co_spawn` that already runs on the session strand sets the flag through
  `session_engine_access::note_engine_stop_()` before the `emit`. A null session has nothing to set.
- `logon_arm_superseded` returns true when `state_ == closing`, when this flag is set, or when the FSM
  has left the arm's state. Every site of the predicate therefore stops a Logon arm after
  `Engine::stop()`'s step 1 has run on the session's strand (C-6). The flag is written and read on one
  strand, so a non-atomic `bool` is correct, and the ordering is strand order, not real time. It holds
  on the condition that every suspension in a Logon arm is followed by the predicate before the next
  effect. Re-derive the sites with `grep -n logon_arm_superseded src/session/session.cpp`, against the
  arm's `co_await` sites.
- E-4's placement condition applies: a `Session` reused for a second connection would need the flag
  reset when the transport is installed.
- **Why a null session is safe to skip** (a condition, read at `00c1f720`). Both role loops publish
  `entry.session` through `publish_entry` on the control strand **before** the first
  `on_inbound_frame`: the acceptor delivers its first frame only after a successful publish, and the
  initiator's frames are delivered only by the pump, which starts after it. `publish_entry` checks
  `stopped_` first and, when it is set, publishes nothing, and the loop closes the session and returns
  without delivering a frame. Step 1 and `publish_entry` both run on the control strand, so they are
  serialised. So no Logon arm runs while `entry.session` is null. Re-derive by reading what follows the
  `publish_entry` calls in `run_accept_loop` and `run_connect_loop` (`src/session/engine.cpp`). If a
  change ever delivers a frame before the publish, this fails, and the predicate must read the engine's
  `stopped_` through the seam instead.

## State and disposition changes

`contracts/inbound-frame-dispositions.md` C-2 has the per-state table. It is not reproduced here.
