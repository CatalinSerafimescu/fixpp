# Data model: 099

## E-1 `session::detail::fixt_route_set` (private to the session library)

`src/session/fixt_route_set.hpp`. Forward-declared in `session.hpp` and `engine.hpp`, which hold it through a `shared_ptr`, so neither public header gains `validator.hpp`.

```text
class fixt_route_set                   // not copyable, not movable
  struct entry {
    shared_ptr<const wire::dictionary_driven_validator> validator;  // holds the one table by value
    shared_ptr<const dict::table_view> owner;                       // aliases validator->table(); the parse route's owner object
  };
  template <std::ranges::forward_range Dictionaries>   // of const dict::Dictionary*
  static expected_t<shared_ptr<const fixt_route_set>>
      build(const dict::version_registry&, Dictionaries&& session_dictionaries) noexcept;
  const entry* find(const dict::Dictionary* d, const dict::Dictionary* pair_source) const noexcept;  // null if absent
```

One entry per distinct **route table**: `{d, null}` for every registry slot, the transport dictionary and each listed session dictionary, skipping null (a non-validating entry may have none, and its `open()` refuses it); and, for each listed S that declares a **header pair**, `{d, S}` for every other `d`, including a `d` whose version S answers, which the slot rule then never returns and NFR-003's M still counts. A header pair is a non-standard Length+Data pair with either tag in S's header (`table_view::is_header_tag`). No vendored dictionary declares one, so only an overlay pays. The tables come from `as_table_view(fixt_framing)`, or `as_table_view(fixt_framing, S's header pairs)` for `{d, S}` (E-3), moved into the validator. So a route is one table and one `dict_hooks::for_table_view`, and a view, its clone and its reify split alike (fixpp#426).

Invariants:

- **Written once, in `build`; read-only after.** No lock, no atomic. The set is handed out only after `build` returns, so readers on other strands see a fully built object. The edge is the role loop's `co_spawn` in `start()`, which synchronizes with the loop's first step on any thread, and on a C engine also the worker's thread creation after `start()` returns. `adopt_routes` and `open()`'s writes run on the session strand, one after the other in the role loop's coroutine, ahead of the first inbound frame.
- **Owner-object rule** (`Parser`'s owned-route constructor). Entries are heap-held and never moved or reassigned, so `&entry.owner` is stable.
- **Lifetime.** One `shared_ptr` per holder: the Engine, and each FIXT session through `routes_` (E-6). The set dies with the last holder, so no declaration order on the Engine matters. `route_entry` holds raw pointers and has no destructor, so a coroutine frame or a session member destroyed after `routes_` dereferences nothing; so does a view in a destroyed frame, since `MessageView` and `OffsetTable` have implicit destructors that read no table. A view's clone or reify copies the aliasing `owner`, which keeps the table alive on its own, so it may be used on another thread after the Engine and session are gone. The Engine writes its pointer once, in `start()`, reads it only when a role loop hands it out, and `stop()` does not clear it.
- **Whole or nothing.** `build` catches `std::bad_alloc` and returns `out_of_memory`, publishing nothing.
- **Keys.** `find` runs only in `open()`, while `cfg_` and the registry hold the dictionaries it is keyed on, so a freed and reused address cannot match.
- **The fallback.** A FIX50SP2 lookup answered by the registry's one-way fallback gets FIX Latest's dictionary pointer, and so shares that entry.

**Who builds it.** `Engine::start()`, after `validate_engine_config` and before its first allocation, when at least one registered entry has `config.is_fixt()`, passing a view over `registry_` filtered by that predicate, yielding each one's `config.dictionary.get()`, so no list is collected. An entry with `begin_string` FIXT.1.1 and no default takes no part: `open()` refuses it (E-9) before it would read a set. A failure returns from `start()` with nothing spawned. The role loops hand it to each FIXT session through `session_engine_access::adopt_routes`, right after `make_shared<Session>` and before `open()`. A FIXT session with no adopted set, which is a direct-constructed one, builds a private one in `open()` (E-6) over every registry slot, passing `std::views::single` of its own dictionary.

## E-1a `dict::version_registry` (public, additive)

| Addition | Meaning |
|---|---|
| `transport() const noexcept` → `core::expected_t<Dictionary const*>`, as `get()` | the FIXT.1.1 dictionary among the engine dictionaries, last writer wins as for every slot, so a later one failing FR-003's check hides an earlier one that passes, and the check refuses; `dict_no_dictionary_for_application_version` if none |

Registering a `vt11` dictionary no longer drops it. `get()` is unchanged: a `vt11` dictionary is not an application version. No mutable state, no cache, and its move operations and `noexcept` constructors stay as they are.

## E-2 Public `dict` helpers

| Symbol | From | Meaning |
|---|---|---|
| `session_to_application(session_version)` | moved from `version_registry.cpp`'s anonymous namespace to `version_profile.hpp` | `vt11` and `Unknown` map to `Unknown` |
| `is_fixt_admin_msgtype(string_view) noexcept` | made public from `reify.cpp`'s `is_fixt_admin` | the seven-member set of the spec's Terminology; `reify.cpp` calls it |
| `is_fixt_standard_header_tag(uint16_t) noexcept` | new, `dictionary.cpp`, over `kFixtFramingTable` minus 10, 89, 93, plus 370 | the peek's standard header set (research R-5 says why 370); `kFixtFramingTable` stays private to `src/dictionary/` |
| `is_route_answered_header_tag(uint16_t) noexcept` | new, `dictionary.cpp` | 35, 1128, 1129, 1156: the header tags S does not judge on another route (FR-008) |

## E-3 `Dictionary::as_table_view(fixt_framing)` (public overload)

Adds the framing surface for v40–v44. `v50`, `v50sp1` and `v50sp2` have it already. `vt11` and FIX Latest declare those tags themselves, and get no framing surface. `as_table_view()` is unchanged, so a non-FIXT session is unchanged.

**NoHops's structure (FR-007).** On a table whose own header does not declare NoHops (`is_header_tag(627)` false), the overload also registers its group metadata in the bare store: `set_group_first(627, 628)` and members 629 and 630, none required. No validator branch and no envelope: `validate_group_level` and `consume_group` ask `group_first_field`, `group_member_tags` and `group_required_members`, each of which falls to the bare store on a context miss, and `set_group_first` sets the bit their pre-filter tests (read from `table_view.hpp` and `validator.hpp`). So one ending on every route and shape: count high or missing delimiter `R 1/628 <third>` stays, c, and k on a Logon, what T and L answer today where a table declares it (research R-4). `dict_hooks::for_table_view`'s group callbacks read the same accessors, so the delivered view's `group(627)` answers there too, with or without validation. Measured on real tables with these calls applied, not through a `Session` (the parent's `research/probes/099_fixt_session_today/gate_a_l2r2/nh.log`): the validator's `1/628`, and one answer from the view, `fixpp_msg_get_group`, a clone and a `reify` handle. On shape A that is new with validation off (FR-011, B-row). `get(628)` reads the offset table and does not change. A header group an overlay adds to S is not composed (L-row): on another table its count and members pass as S's header tags, enum and type judged, and a wrong count passes.

**Preservation contract.** The result equals `as_table_view()`'s on every store but `fixt_framing_tags_`, `fixt_framing_types_` and, where the paragraph above applies, 627's entries in `group_first_` and `group_members_` and its bit in `group_bits_`. Every form bumps `as_table_view_call_count`.

**`as_table_view(fixt_framing, span<const length_data_pair> extra)`** also registers `extra` through `table_view_builder::set_length_pair_data_tag`, after the dictionary's own pairs and before the table is sealed. So S's pair wins a collision on that table, and its result differs from the plain overload only in the two pair stores and `has_nonstandard_pair()`. `dict::header_length_pairs(const Dictionary&)` answers S's header pairs, at configuration time only.

## E-4 `wire::dictionary_driven_validator`

Still holds its table by value, with the same constructor and `noexcept` specification. Adds no data member, so its layout does not move. `Validator`'s five pure virtuals are untouched (`[const §XIV.2]`): the two additions are non-virtual and on this `final` class only.

| Addition | Meaning |
|---|---|
| `validate(msg, scratch_mr, ref_tag_out, const table_view* envelope) const noexcept` | the virtual `validate` forwards here with a null envelope |
| `const table_view& table() const noexcept` | what the route set's aliasing owner points at |

**With a null envelope the walk is today's**, and the branch that chooses is outside the field loop, so a non-FIXT validation pays nothing per field. **With an envelope,** Step 1 asks first, for each field: `envelope->is_header_tag(tag) && !dict::is_route_answered_header_tag(tag)`. If so, the tag passes membership and **its enum and type are checked against the envelope**, whatever the routed table declares for it, wherever in the frame it appears. Otherwise today's checks run on the routed table. FR-007's framing surface answers 1128, 1129 and 1156 there. `scratch_mr` and `ref_tag_out` are the caller's, per call, as the gate's stack buffer is today, so a validator shared across strands writes nothing of its own.

## E-5 The resolved application version: one contract

**Owner: the view.** `wire::MessageView::resolved_application_version()` returns a `dict::application_version`, default `Unknown`. It is one byte on `MessageView`.

**Original writer: the session only.** `parse_and_dispatch_` writes it from the frame's route (E-7), after the parse and before the callback, through `wire::detail::message_view_resolved_access`, a detail key shaped like `message_view_membership_access`. It is `Unknown` on a non-FIXT session, an admin message, an unresolved value, and any view that was not an inbound dispatch.

**Copy writers: exactly two, each from its source view, through the same key.** Both re-parse into a fresh view, so without this they would read `Unknown`:

- `fixpp_msg_clone` (`src/capi/message_write.cpp`), on the dict-backed and the dict-free branch.
- `dict::detail::owning_message_handle_from_frame` (`src/dictionary/reify.cpp`), on both branches. So `dict::reify(...)`'s handle's `view()` answers as its source did. A bridge file under `[arch §2.4]`, which already reads `message_view_membership_access`.

`dict::reify_as`'s typed owner is not one of them: its `view()` parses dict-free today (each generated `<version>/Reify.hpp`, read, not run), so `group(627)` declines there and `resolved_application_version()` is `Unknown`. It carries `version_v` instead.

**Readers.**

- `dict::reify`: when the byte is set, returns `{application, vt11, byte}` and reads neither 1128 nor the profile. Otherwise it reads 1128 and the profile, as today. A view no session wrote is `Unknown`, so a non-session caller and a non-FIXT session are unchanged. `version_profile` stays 4 bytes and carries no per-MsgType default.
- C: `fixpp_msg_version`, E-8. Python: `msg_version`, returning the three values.

**Against `dict::reify`'s own answer.** Equal in all three bytes whenever the byte is set, by the rule above, whatever profile the caller passes. On a non-validating session they split when the byte is `Unknown` and `reify`, falling through, finds a 1128 or a profile that answers otherwise (B-row; W-6 names the cases it pins):

- an empty `1128=`: `reify` passes the empty value to the resolver, whose rule means "use the default";
- an initiator's reconnect whose reply has no 1137 it can resolve: `route_negotiated_` is `Unknown`, while `negotiated_version_profile()` keeps the previous value, so a `reify` with that profile labels the frame with the previous connection's version. The split follows from ruling 1, which keeps that accessor's writes;
- an unknown 1128, such as `1128=99`: the accessor is `Unknown`, and `reify` returns `dict_unknown_appl_ver_id` and no handle;
- a past-header 1128, such as a body `1128=6`, on an initiator whose reply had no 1137: step 6 leaves the byte `Unknown`, and `reify` reads the first occurrence with no position test, so it answers that version (read, not run). No requirement rests on it: FR-014's equality is for a byte the session wrote.

## E-6 Session state (internal)

```text
route_entry {
  shared_ptr<const table_view> const* owner;
  dictionary_driven_validator const*  validator;   // null only when not validating, non-FIXT
  table_view const*                   envelope;    // S's table when this route's table is not S's, else null
}

fixt_routing_     bool: begin_string is FIXT.1.1. Behind a successful open() only, the FIXT check has guaranteed the registry and default_appl_ver_id
route_negotiated_ application_version: selection's read of the negotiated version, outside the establishment arms only; written as
                  FR-027 says. negotiated_appl_version_ keeps today's writes (FR-027)
msg_type_defaults_ E-10; written and read as route_negotiated_ is
pair_source_      Dictionary const*: S when the set holds a {d, S} entry, else null, read from the set so that open() has no fallible step after its first route write; the second argument of every find() but S's own entry's, which is find(S, nullptr), since E-1 builds {d, S} only for every other d
routes_           shared_ptr<const fixt_route_set>: adopted from the Engine, or built in open(); null on a non-FIXT session
app_routes_       array<route_entry, slot count>, indexed by application_version
transport_route_  route_entry
session_route_    route_entry
```

Non-FIXT: `session_route_ = {&inbound_tv_, validator_.get(), nullptr}`, and nothing else is used. FIXT: `session_route_` is S's entry from the set, `routes_->find(S, nullptr)`, `inbound_tv_` is a copy of that owner unless `cfg_.dict_snapshot` supplies it (the C-ABI path, as today), and `validator_` stays null. So a C session holds two tables for S: the peek and the outbound path read `inbound_tv_`, and the scan, gate and dispatch read the route. E-3's preservation contract makes them equal on `is_header_tag` and the pair stores, so they split a frame alike.

The gate's guard reads `session_route_.validator`, not `validator_`. `session_test_access::has_validator` becomes `cfg_.validate_inbound_messages && session_route_.validator`, which W-18's `hello` record and the run-thread rig read.

**Written in `open()`, before the first observable mutation**, on the same side of the commit as the carry and parse blocks. Read on the session's strand with no lock; `route_negotiated_` and `msg_type_defaults_` are also written there, at Logon, and a test accessor reads them only while nothing pumps. A failed `open()` leaves `state_` at `never_opened`, and a retry may reassign a privately built `routes_`, under the same owner-object carve-out as `inbound_tv_`: no view exists then. An adopted `routes_` stays as adopted on a retry, and `open()` after a success returns `session_already_open` ahead of all this, as today. An adopted set with no entry for S is refused: `open()` returns `invalid_session_config` rather than read a null entry.

**Slot rule (FR-002).** For each version `v`: `registry.get(v)` is a dictionary `d`, or `v` is S's own version, or neither. S's entry if `v` is S's own version or `d`'s `session_version` is S's. Otherwise `routes_->find(d, pair_source_)`, with the envelope. Otherwise a null owner, which is a miss.

**Transport route.** S's entry if `defines_fixt_admin(S)`. Otherwise `routes_->find(registry.transport(), pair_source_)`, if that dictionary passes the check, with the envelope. Otherwise, reachable only when not validating, S's entry.

## E-7 Per-frame route and what each arm does with it (internal)

```text
inbound_route {
  route_entry entry;
  application_version resolved;     // Unknown: admin, non-FIXT, establishment arm, or not resolved
  route_refusal refusal;            // none | empty_value | unknown_value | no_dictionary
  bool application;                 // the third field's MsgType is not admin
  bool header_1128;                 // the peek saw 1128 before stopping
}
```

**The peek's oracle** (one clause, research R-5). Walks from the frame's start with S's hooks. MsgType is the third field's. It is unreadable when the third field is not 35, or when `read_value` faults before the stop, whether or not 35 was read, or when a tag before the stop is malformed (a non-digit, above 0xFFFF, empty, no `=`), as `scan_frame_header` reports `malformed_tag` on the same frames. Stops at the first tag for which `inbound_tv_->is_header_tag(tag) || dict::is_fixt_standard_header_tag(tag)` is false; 10, 89 and 93 satisfy neither. The stop is latched: the first three fields are still read, so the peek's MsgType equals the scan's `fault_ref_msg_type`, and no 1128 is taken after the stop. Admin is `dict::is_fixt_admin_msgtype`. Whether a second 35 is in the header is read only on a mismatch: the cold path re-walks with the same stop rule.

**Selection,** once at arm entry, in every arm:

1. Not `fixt_routing_` → `{&inbound_tv_, validator_, null}`, what such a session has read since before this feature, whatever its `open()` did: the owner pointer is never null. A FIXT session with `session_route_.owner` null takes the same step: its `open()` refused at or ahead of the route block, whose first route write is that member and which has no fallible step after it. Every arm disregards that session's frame ahead of selection, so no route is read, and nothing runs on a session whose `open()` stopped ahead of its store and its strand. A later failure in `open()` leaves the routes bound, and this step is not taken.
2. Peek unreadable → `session_route_`; the arm's fault and garble dispositions run as today. Admin → `transport_route_`.
3. An establishment arm (NotConnected, LogonSent) → `session_route_`, no refusal. The negotiated slot is not read.
4. 1128 present and empty → refusal `empty_value`, `session_route_`.
5. 1128 present → resolve, by `dict::resolve_application_version` with the profile `{vt11, route_negotiated_}`, not `negotiated_version_profile()`, which carries `negotiated_appl_version_`. The value is not empty here, so the profile's default goes unread. A failure → refusal `unknown_value`, `session_route_`. A miss on the slot → refusal `no_dictionary`, `session_route_`, `resolved` set.
6. 1128 absent → the version is the third field's MsgType's entry in `msg_type_defaults_`, else `route_negotiated_`. `resolved` set to it. `Unknown` or a slot miss → `session_route_`. Reachable only when not validating: a validating session reaches Active only through S3, whose 1137 test and E-10's kept entries passed `registry.get`, and E-6's slot rule gives every such answer an entry.

A refusal is acted on only by a validating session. A non-validating session uses the entry it carries. It has no S1 to S3, and takes the same hooks: an establishment arm scans with the selected route, and `interpret_logon` reads with the transport route's, so `D…A+L` is scanned with S and interpreted with the transport's table.

**Steps a validating FIXT session adds.** Each runs after the arm's garble, supersede and fault checks.

| Step | Arm | What it does |
|---|---|---|
| **S1** | NotConnected, LogonSent | `route.application \|\| hdr.msg_type != hdr.fault_ref_msg_type` → Disconnected, nothing sent. It reads the third field: `interpret_logon` reads the last 35, so it could not refuse `D…A` carrying Logon fields |
| **S2** | same | Today's 3/5 exemption, then the gate on `transport_route_`. On a Logon (`hdr.msg_type == "A"`), a Reject whose `ref_tag_id` is 1137, 1130 or 372 → emit it, then `disconnect_unless_superseded_`. Any other Reject stays in the arm, as today |
| **S3** | same | Today's `interpret_logon` with the transport route's hooks, then the arm's guards in today's order. The 1137 block: acceptor as today, after `check_inbound` and mTLS. **The initiator gains it right after `check_inbound`, ahead of the 141=Y reset unit and mTLS**: absent 373=1, empty, unparseable or `app_version_registry_->get` failing 373=5, Reject 371=1137, disconnect. The test is the acceptor's, the registry and not the slot, so one input has one ending by role. At today's record site a `141=Y` reply with a bad 1137 would durably reset both stores and then be refused. **Both blocks then read E-10's entries** and refuse as it says. Today's record stays where it is, and also writes `msg_type_defaults_`. A non-validating initiator records as today |
| **A1** | LogonReceived, Active | `hdr.msg_type != hdr.fault_ref_msg_type` → Reject 371=35, 373=13 if the second 35 is in the header, else 14. Ahead of the 3/5 exemption |
| **A2** | same | `route.refusal` → Reject 371=1128, 373=4 for `empty_value`, else 5 |
| **A3** | same | Today's 3/5 exemption, then the gate with the route: parse with its table; `application && !header_1128 && mv.get(1128)` has a value → Reject 373=14, 371=1128; then validate with its envelope |

A1, A2 and A3's probe fill `RejectDecision`'s new RefMsgType and RefSeqNum: the third field's value and the first 34 (`fault_ref_seq_num`). The third field's value also keys `consume_rejected_seqnum_`. A non-FIXT session leaves both empty, and the arm reads `hdr` as today. A1, A2 and A3 run in that order at the gate's place, ahead of the arm's BeginString, CompID and SendingTime guards (read from source), so of two faults in Active the earlier step answers. LogoutSent gains no step: it drains every frame as today, scanned with the route's hooks.

### The per-arm table

**Cell ids** are row-column: `NC`, `LS`, `AC`, `LO`, then a letter. W-4, W-5, W-9 and the mutant table cite them.

**Columns.** Frames named as `D…0`: third field 35=D, last 35=0, the later one past the header. `0,D`: adjacent, in the header. `+L`: also carrying 98, 108 and `1137=9`.

| | Column | Inputs |
|---|---|---|
| a | repeated 35 | `D…0`, `D…3`, `D…4`, `D…5`, `D…A`, `0…D`, `3…D`, `4…D`, `5…D`, `A…0`, `0,D`, `D…A+L`, `0…A+L`. Control: `D,D`, no split, as column d |
| b | route refusal | header `1128=99`, `abc`, `0`, `1`, `4` (no dictionary), empty, on a NewOrderSingle (NOS) |
| c | past-header 1128 | NOS with a body `1128=6`, `1128=abc` |
| d | ordinary application frame | NOS valid; `1128=9`; `1128=10`; missing 11, at the expected number and five ahead |
| e | Logon 1137 fault | absent, empty, `99`, `abc`, `4`. Controls `9`, `10`. Other fault: an undeclared tag 5999 with `9`, `4`, `99`, absent. `141=Y` with `4` |
| f | wrong BeginString | `8=FIX.4.4`: Logon with `9`, `99`, `4`, absent; NOS |
| g | stale SendingTime | Logon with `9`, `99`, `4`, absent; NOS; Heartbeat |
| h | off-list mTLS identity | Logon with `9`, `99`, `4`, absent |
| i | posture mismatch | production posture, `464=Y`: Logon with `9`, `99`, `4`, absent |

**Notation.** `R a/b x`: Reject 373=a, 371=b, 372=x. `Disc`: Disconnected. `stays`: state unchanged. `k`: NextNumIn kept. `c`: NextNumIn consumed. `—`: nothing sent. `app`, `adm`: delivered to `fromApp`, `fromAdmin`. `auth`: `session_event_compid_authorization_failed` raised.

#### Today: the keyed logs are the oracle

`{T,A,L}_{on,off}.log` and `mtd.log`, under the parent's `research/probes/099_fixt_session_today/`, keyed by shape, on or off, arm and input, measured through a real `Session` at `c0a828ef` (research R-18). W-4, W-5 and W-9 read their RED-first expectation from those lines, not from prose, so no compression stands between a cell and its measurement. `compact.py` renders them for reading.

The cells that decide the design:

- T rejects every application frame in every arm on tag 8, 373=2. A cannot establish: every Logon measures tag 98.
- Validating, absent 1137 is `R 1/1137 A`, stays, kept, whatever else the Logon carries but an undeclared tag, which the validator names first.
- Validation off, on every shape: `NC-e` absent `R 1/1137 A` `Disc` c; `99`, `abc`, `4`, empty `R 5/1137 A` `Disc` c; `9`, `10` Active c, an undeclared 5999 ignored either way. `LS-e` Active c on every input. `D…A+L` and `0…A+L` establish.
- `D…3` in Active is consumed in silence. `D…5` logs the session out.
- Other admin frames in an establishment arm: T's gate Rejects and stays on a fault (`TestRequest` missing 112, `Heartbeat 1128=99`) and otherwise disconnects with nothing sent. A answers TestRequest today with its own fault.

LogonReceived is the Active arm's code and is not separately reachable with a frame (research R-18).

#### After: design, not measured, read from each arm against the table above

As the owner ruled, `plan.md` items 2 and 3. **A answers as T** in every row and on every other admin frame, since FIXT11 is its transport dictionary. Validation off: the logs stand, apart from what the route's hooks change (below). A Heartbeat carrying 1137 is judged by the gate and stays: S2 reads a Logon only.

| Row | a | b | c | d | e | f | g | h | i |
|---|---|---|---|---|---|---|---|---|---|
| **NC** T, A | `Disc` — k (S1). `D,D` as d | `Disc` — k (S1) | `Disc` — k (S1) | `Disc` — k (S1) | absent `R 1/1137 A` `Disc` k (S2). Others `R 5/1137 A` `Disc` c (S3). **5999, whatever 1137 says: `R 2/5999 A` stays k**: the gate names 5999, so S2 does not fire | `9`, `99`, `4`: `Disc` — k. Absent `R 1/1137 A` `Disc` k (S2). NOS `Disc` — k (S1) | `9`, `99`, `4`: `R 10/52 A` `Disc` k. Absent `R 1/1137 A` `Disc` k (S2). NOS, Heartbeat `Disc` — k | `9`, `99`, `4`: auth — `Disc` c. Absent `R 1/1137 A` `Disc` k, no auth | `9`, `99`, `4`: Logout `Disc` k. Absent `R 1/1137 A` `Disc` k |
| **NC** L | as T, A | as T, A | as T, A | as T, A | absent, `99`, `abc`: `R 1/1137 A` or `R 5/1137 A` `Disc` k (S2). Empty, `4`: `R 5/1137 A` `Disc` c (S3). 5999: `R 2/5999 A` stays k, but with `99`: `R 5/1137 A` `Disc` k | `9`, `4`: `Disc` — k. `99`: `R 5/1137 A` `Disc` k. Absent `R 1/1137 A` `Disc` k. NOS `Disc` — k | `9`, `4`: `R 10/52 A` `Disc` k. `99`: `R 5/1137 A` `Disc` k. Absent `R 1/1137 A` `Disc` k. NOS, Heartbeat `Disc` — k | `9`, `4`: auth — `Disc` c. `99`, absent: their Reject, `Disc` k, no auth | `9`, `4`: Logout `Disc` k. `99`, absent: their Reject, `Disc` k |
| **LS** T, A | as NC | as NC | as NC | as NC | absent `R 1/1137 A` `Disc` k (S2). Others `R 5/1137 A` `Disc` c (S3, new), and with `141=Y` no reset unit runs. 5999 as NC | as NC | as NC, with the Logout for `R 10/52 A` | **Direct-constructed session only:** an Engine's reconnect step refuses the identity before LogonSent. `9`: auth — `Disc` c. `99`, `4`: `R 5/1137 A` `Disc` c, no auth, because S3's block is ahead of mTLS here. Absent as NC | as NC |
| **LS** L | as NC | as NC | as NC | as NC | as NC L, and `141=Y` as LS T, A | as NC L | as NC L, with the Logout | Direct-constructed only, as above. `9`: auth — `Disc` c. `4`: `R 5/1137 A` `Disc` c, no auth. `99`, absent as NC L | as NC L |
| **AC** T, A, L | `R 14/35 <third>` stays, c. `4…D`, `A…0`: k. `0,D`: `R 13/35 0` stays c. Five ahead: k. `D,D` as d (A1) | `99`, `abc`, `0`, `1`, `4`: `R 5/1128 D` stays c. Empty: `R 4/1128 D` stays c (A2) | `R 14/1128 D` stays c (A3) | valid: app stays c, on the negotiated slot or 1128's; five ahead: ResendRequest, k. Missing 11: `R 1/11 D` stays c; five ahead: k | **T, A:** absent `R 1/1137 A` stays k. 5999: `R 2/5999 A` stays k. Others: app stays c, accessor `Unknown`. **L:** as AC L today. FR-010 does not reach this arm | NOS `Disc` — k. Logon: as today on the row's shape | NOS `R 10/52 D`, Logout, `Disc` c. Heartbeat `R 10/52 0`, Logout, `Disc` c. Logon: as today on the row's shape | not a check of this arm (source) | not a check of this arm (source) |
| **LO** T, A, L | — stays k. `D…5`: — `Disc` k, as today: FR-025 does not reach this arm | — stays k | — stays k | — stays k | — stays k | — stays k | — stays k | not a check of this arm (source) | not a check of this arm (source) |

What moved, from the table above: T's and A's Reject-and-stay on an application frame in an establishment arm (S1). `R 1/1137` and L's `R 5/1137` stay to Disc (S2). An initiator's Active on a bad 1137 to a Reject and Disc, ahead of its reset unit and mTLS (S3). Every delivered or silently consumed repeated-35 frame in Active to a Reject (A1). T's `R 2/8` on an application frame to its route's answer, which moves column f's and g's NOS to `Disc` and to the SendingTime ending, as L does today.

**Split left in place: the consume column of `NC-e` and `LS-e`.** `99` and `abc` keep the number on L, through the gate, and consume it on T and A, through the block. Observable only on an initiator's reconnect. W-5 pins both literals.

**A validation-off frame's new ending.** The route's hooks scan a frame whose pair only that table declares. A malformed such pair on a non-validating session then takes `dispose_unparseable_`'s Reject in Active, where at `c0a828ef` S's hooks read past it and delivered the frame. B-row; W-6's `off-pair` cell.

## E-8 C ABI

`fixpp_engine_config` gains `std::vector<std::shared_ptr<const Dictionary>> dictionaries`, **copied** into `EngineConfig` in `fixpp_engine_create`, not moved. `engine.h` promises the builder is untouched on failure, and the only failure after that step is an allocation one, so W-20's C arm is what can tell a copy from a move.

`fixpp_resolved_msg_version_t` becomes `[2i §4.6]`'s four bytes, `{kind, session, application, _reserved}`, mirroring `dict::resolved_message_version`. Only `application` is stored, in E-5's byte. `kind` and `session` are derived in the getter, `kind` from the view's 35 by `dict::is_fixt_admin_msgtype` on every row, a non-FIXT `35=A` included, and `session` from its 8, so no session path gains a write.

| Handle | `kind` | `session` | `application` |
|---|---|---|---|
| FIXT application message, resolved, with a dictionary or not | 1 | `vt11` | E-5's byte |
| FIXT application message, unresolved: validation off with an unknown or empty 1128, or a reconnect with nothing resolved | 1 | `vt11` | `FIXPP_APPL_VER_UNKNOWN` |
| FIXT admin message | 0 | `vt11` | `FIXPP_APPL_VER_UNKNOWN` |
| Non-FIXT application message | 1 | from BeginString | `session_to_application` of it |
| Non-FIXT admin message | 0 | from BeginString | `FIXPP_APPL_VER_UNKNOWN` |
| A BeginString no enumerator names | by 35 | `FIXPP_SESSION_VER_UNKNOWN` | `FIXPP_APPL_VER_UNKNOWN` |
| A clone, a reify view | as its source | as its source | as its source |
| A `toApp` view on FIXT | by 35 | `vt11` | `FIXPP_APPL_VER_UNKNOWN`: no session wrote it |
| Tag 8 absent | all zero, `FIXPP_ERR_TAG_NOT_FOUND`, as today | | |

**Against `dict::reify(...).version()`,** on the rows a delivered frame can reach, each with a W-10 cell. A FIXT application message with the byte set, and a clone or reify view of one: all three bytes equal, whatever the profile, because `reify` reads none there (E-5). A FIXT admin message and both non-FIXT rows, where the byte is `Unknown`: `session` from `reify` is still the caller's profile's, as today, so W-10 passes a profile naming the frame's own version. Given `{Unknown, Unknown}`, what `negotiated_version_profile()` returns before a version is negotiated, `reify` says `session` `Unknown` on an admin message where the getter says `vt11` (read from source). That differs in `session` alone, and no research R-16 condition rests on it. No comparison on the other rows: the unresolved row is E-5's splits, and W-10 asserts the literal on the rest. Constant values are the enums' own, so the conversion is identity; static assertions pin it. Wire values do not coincide with them.

## E-9 Check helper

`fixt_session_config_valid(const SessionConfig&, const dict::version_registry*) noexcept`, in `src/session/`, with `defines_fixt_admin`. Clauses: `begin_string == "FIXT.1.1"` with no `default_appl_ver_id`; a null registry; a registry whose `get` finds no dictionary for the default, so the one-way fallback counts; and, when validating, no transport dictionary. Called from `open()` when `begin_string == "FIXT.1.1"`, and from `Engine::register_session` when `cfg.validate_inbound_messages && cfg.begin_string == "FIXT.1.1"`, not `cfg.is_fixt()`, which is false for the first clause's config (research R-2). The Engine's registry is never null, so the null clause is reachable only from a direct `open()`. Behind that clause `open()` reads a null registry as an empty one (`registry_or_empty` in `session.cpp`), so that M-7b, which removes it, is a failed assertion and not a crash. `open()` runs the check first, then the private build, then E-10's reservation, so a refused config walks nothing and a config failing both ways is `invalid_session_config`. At `register_session` it sits with the other guards, ahead of the duplicate-id test, so a config failing both is `invalid_session_config` there too.

## E-10 Message Type Defaults (FR-030, FR-031)

```text
msg_type_default { char msg_type[8]; uint8_t len; application_version version; }
msg_type_defaults_  std::vector<msg_type_default>, sorted by MsgType, at most 256, reserved in open()
```

Empty on a non-FIXT session and on most FIXT ones, so step 6 costs a size test. A lookup is a binary search over bytes, no heap. **Storage:** `open()`, on the FIXT branch beside the route adoption, calls `reserve(256)`, caught as the build is, so a failure is FR-028's `out_of_memory`. One block per FIXT session, the shape of B-544-3, and `clear()` keeps it, so no Logon grows it. On an Engine route `open()` runs in the role loop, whose failure branch logs it (research R-2). Written at today's record site, in each role, on every Logon that reaches it, cleared first. S3's block reads the group and stores nothing, so a Logon it refuses leaves the entries as they were: selection reads them only outside the establishment arms, and the next Logon that reaches the record site clears them. 256 exceeds every vendored dictionary's message count, which W-25 recomputes.

**`scan_logon_msg_type_defaults(frame, hooks, out)`** in `src/session/logon_msg_type_defaults.hpp`, with the transport route's hooks, so a Data field is not read as a group member. `interpret_logon` is not changed. `out` carries every instance the scan consumed, counted or not, with its 372, 385, 1130 and 1410 as read, because S3's block refuses on uncounted entries too (`MT-i`, `MT-k`); the record site keeps the subset this paragraph names. `out` is a local of the Logon arm, bounded by 384's value and the frame's length, and a Logon is outside NFR-002. **One rule, validating or not.** It reads 384. Absent, not all ASCII digits, or 0, there are no entries. Otherwise its value is `parse_bounded_u32`'s, saturating, as `consume_group` reads a count, so the scan and the gate read one value. Otherwise it consumes at most that many contiguous instances, each starting at a 372 and ending at the next 372 or at the first tag outside 385, 1130, 1406, 1131, 1410, and it stops at the first non-member. A later 372 is not an entry. An instance's other fields may come in any order; a member repeated within one instance reads first-wins, as `MessageView::get` does, and a second 384 is a non-member. The gate passes an empty value (measured on 1128), so an empty 372, 385 or 1410 can reach the scan: an empty 385 or 1410 does not count, and the record site drops an empty 372. An entry **counts** when 385 is `S` and 1410 is `Y`. 1406 and 1131 are ignored. 1130 absent: the entry sets no default. **The record site keeps** each counted entry whose 1130 parses and whose 372 fits and is an application message, within the first 256 counted; a later entry for one MsgType wins.

**Endings, validating FIXT, either role.** S3's block, after 1137's, refuses a Logon with any of these among the entries the scan consumed, `R x/tag A` `Disc` c; with two, either Reject may answer, as FR-010's double faults:

- no 385: `1/385`;
- a non-empty 1130 the resolver cannot parse, whatever its 385 and 1410: `5/1130`;
- a counted entry with an empty 1130, or one `app_version_registry_->get` fails, the test 1137 takes: `5/1130`;
- the entry is the 257th counted, before de-duplication: `5/384`;
- a counted 372 over eight bytes: `5/372`.

The gate runs first, in dictionary order, as owner item 3 has it for a double fault. A gate Reject naming 1130 or 372 on a Logon disconnects through S2, with NextNumIn kept, and any other tag it names stays (owner item 13). So `5/1130` and `5/372` are k on a gate path and c on a block path, observable only on an initiator's reconnect: item 3's ruled split, which FR-031 reaches. W-25 pins both.

Cells are `MT-` plus a letter; W-25 walks them on T, A and L.

| Cell | Entry, with 385=S and 1410=Y unless it says otherwise | T, A | L, whose ApplVerID and MsgType code sets are closed |
|---|---|---|---|
| `MT-a` | `372=D\|1130=10` | Active; a NOS with no 1128 takes FIX Latest's slot | same |
| `MT-b` | `1130=99`, `abc` | `R 5/1130 A` `Disc` c | gate: `R 5/1130 A` `Disc` k (S2) |
| `MT-c` | `1130=4`, no dictionary; or empty | `R 5/1130 A` `Disc` c | same |
| `MT-d` | no 1130 | Active; 1137 answers | same |
| `MT-e` | 257 entries: 257 copies of `372=D`, and on T and A also 257 distinct MsgTypes | `R 5/384 A` `Disc` c | same |
| `MT-f` | a 372 over 8 bytes. Boundary: `372=ABCDEFGH\|1130=10` | `R 5/372 A` `Disc` c. Boundary: Active; that MsgType with no 1128 takes FIX Latest's slot, which does not declare it (`**The eight-byte boundary**`) | gate: `R 5/372 A` `Disc` k (S2), the boundary too, by the domain |
| `MT-g` | `372=0` with a good 1130; `372=0\|1130=99`; `372=0\|1130=4` | Good 1130: Active; not kept. `1130=99` as `MT-b`, `1130=4` as `MT-c` | same, by this column's `MT-b` and `MT-c` |
| `MT-h` | two entries for `D` | Active; the later wins | same |
| `MT-i` | 385=R or 1410=N, or no 1410, `1130=99` | `R 5/1130 A` `Disc` c | gate: `R 5/1130 A` `Disc` k (S2) |
| `MT-j` | 385=R or 1410=N, or no 1410; `1130=10`, `4` or empty | Active; ignored | same |
| `MT-k` | no 385 | `R 1/385 A` `Disc` c | same |
| `MT-l` | `384=0`, `384=-1` or no 384, then an entry with `1130=10` | Active; no entries, 1137 answers | same |
| `MT-m` | `384=1`, an entry for `D` with `1130=9`, `553=u`, an entry for `D` with `1130=10` | Active; the first answers | same |
| `MT-n` | `384=2` with one entry; `384=1` with two | gate: `R 1/372 A` `Disc` k (S2) | same |
| `MT-o` | 312 entries with no 1130 and no 1410 | Active; none counts | same |
| `MT-p` | `384=abc`; `385=X`; `1410=X` | `384=abc`, `385=X`: `R 5/that tag A` stays k. `1410=X`: Active; not counted | same |

**The eight-byte boundary (`MT-f`).** FIX Latest does not declare `ABCDEFGH`, so that entry cannot show a delivery on a validating session: its frame draws a Reject whose tag differs by shape, because T's envelope answers 8 and FIX Latest alone answers 373=2, 371=8, so the cell asserts `fromApp` 0 and no tag. The delivered form needs a dictionary that declares the MsgType:

- **Validating, T and A.** Registered for the default's slot, FIX44 plus an overlay built from strings in the test, with a `<message msgtype='ABCDEFGH'>` and a `<message msgtype='ABCDEFGX'>`, each with member Text(58), and both values added to field 35, which FIX44 closes. Logon `1137=9|384=1|372=ABCDEFGH|385=S|1130=6|1410=Y`: Active. `35=ABCDEFGH|58=x`, no 1128: delivered, `fromApp` 1, accessor v44. The same with `1128=9`: Reject 373=5, 371=35, stays, c. The sibling `35=ABCDEFGX|58=x`, no 1128: 1137 answers, Reject 373=5, 371=35, stays, c. The overlay declares the sibling so that a lookup taking it for `ABCDEFGH` delivers it, which is the only way the two routes differ. A Reject's 372 is not asserted here: no run has shown an eight-byte 372 on this path, and `kMaxShippedMsgTypeLength` bounds it on the fault path.
- **Not validating, T, A and L, a named boundary cell.** `372=ABCDEFGH|385=S|1130=10|1410=Y`, then `35=ABCDEFGH`, no 1128: delivered, accessor FIX Latest.

Measured 2026-11-03 against this tree's `linux-clang-debug` archives, real `Parser` and validator on real tables with the framing surface emulated through `table_view_test_access`, not through a `Session`, so the endings above stay design and W-25 runs them first. The tree's `src` and `include` equal `c0a828ef`'s and `load_any_with_overlay` ran, so 098's code is linked; the archives' freshness is not otherwise shown. FIX44 with an overlay adding only the message: 373=5, 371=35. With the value on 35 as well: `ABCDEFGH|58` passes, and an undeclared `ABCDEFGX` draws 373=5, 371=35, as it does on FIX50SP2, so an undeclared sibling cannot tell the routes apart. With both declared, `ABCDEFGX|58` passes. FIX50SP2 answers `ABCDEFGH` 373=5, 371=35. Re-derive with `load_any_with_overlay`, `as_table_view()` plus the framing surface, and `dictionary_driven_validator` over those frames.

**What is measured.** The gate's answers, through a real `Session`, 2026-11-03 (the parent's `research/probes/099_fixt_session_today/gate_a_l2r2/mtz2.log`), with S the final file of the paragraph below and on L, both establishment arms: it passes `MT-a`, `MT-c`, `MT-d`, `MT-e`, `MT-g`'s good 1130 and `1130=4`, `MT-h`, `MT-j`, `MT-k`, `MT-l`, `MT-m`, `MT-o`, `MT-p`'s `1410=X`, and on T `MT-b`, `MT-f` and its boundary, `MT-g`'s `1130=99`, `MT-i`; it answers `MT-n` `R 1/372 A`, `MT-p`'s other two `R 5/that tag A`, and on L `MT-b`, `MT-g`'s `1130=99` and `MT-i` `R 5/1130 A`, and `MT-f` and its boundary `R 5/372 A`, each staying today. So `MT-p`'s stays are measured and nothing new acts on them. A is T by its transport dictionary, not run. S2's disconnect on 1130 and 372, and every ending of the block, are design: no code exists. W-25 runs them first.

**Not validating:** the same scan, and nothing refuses, so the session establishes. An entry with no 385 does not count. Step 6's slot miss uses S.

**`FIXT11.xml`** gains, on Logon, `<group name='NoMsgTypes'>` with 372, 385, 1130, 1406, 1131 and 1410, every member `required='N'` as Orchestra's are, as Logon's last child, after EncodedText, leaving the existing empty `MsgTypeGrp` component and Logon's reference to it as they are (the parent's `research/probes/099_fixt_session_today/gate_a_l2r2/edited/FIXT11.xml`, the file `mtz2.log` ran on), and field definitions for 384, 385 and 1410, 385 closed at `R` and `S` and 1410 `BOOLEAN`, as QuickFIX/J's copy: 384 and 385 after 383, and 1410 directly before 1409, so that 1409 stays the last field (research R-17). Nothing else. A dictionary an operator supplies without the group keeps refusing such a Logon on 384: `R 2/384 A` stays k, `mtd.log`'s T line (B-row). FR-003's check does not require it.

## State transitions

No new FSM states. The endings are E-7's.
