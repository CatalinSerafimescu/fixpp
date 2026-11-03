# Data model: 099

## E-1 `session::detail::fixt_route_set` (private to the session library)

`src/session/fixt_route_set.hpp`. Forward-declared in `session.hpp` and `engine.hpp`, which hold it through a `shared_ptr`, so neither public header gains `validator.hpp`.

```text
class fixt_route_set                   // not copyable, not movable
  struct entry {
    shared_ptr<const wire::dictionary_driven_validator> validator;  // holds the one table by value
    shared_ptr<const dict::table_view> owner;                       // aliases validator->table(); the parse route's owner object
  };
  static expected_t<shared_ptr<const fixt_route_set>>
      build(const dict::version_registry&, span<const dict::Dictionary* const> session_dictionaries) noexcept;
  const entry* find(const dict::Dictionary*) const noexcept;        // null if absent
```

One entry per distinct `Dictionary` object: every registry slot, the transport dictionary, and each listed session dictionary, skipping null (a non-validating entry may have none, and its `open()` refuses it). The tables come from `as_table_view(fixt_framing)` (E-3), moved into the validator.

Invariants:

- **Written once, in `build`; read-only after.** No lock, no atomic. The set is handed out only after `build` returns, so readers on other strands see a fully built object.
- **Owner-object rule** (`Parser`'s owned-route constructor). Entries are heap-held and never moved or reassigned, so `&entry.owner` is stable.
- **Lifetime.** One `shared_ptr` per holder: the Engine, and each session through `routes_` (E-6). The set dies with the last holder, so no declaration order on the Engine matters. `route_entry` holds raw pointers and has no destructor, so a coroutine frame or a session member destroyed after `routes_` dereferences nothing. A view's clone or reify copies the aliasing `owner`, which keeps the table alive on its own.
- **Whole or nothing.** `build` catches `std::bad_alloc` and returns `out_of_memory`, publishing nothing.
- **The fallback.** A FIX50SP2 lookup answered by the registry's one-way fallback gets FIX Latest's dictionary pointer, and so shares that entry.

**Who builds it.** `Engine::start()`, after `validate_engine_config` and before its first allocation, when at least one registered entry is FIXT, passing each FIXT entry's `config.dictionary`. A failure returns from `start()` with nothing spawned. The role loops hand it to each session through `session_engine_access::adopt_routes`, right after `make_shared<Session>` and before `open()`. A FIXT session with no adopted set, which is a direct-constructed one, builds a private one in `open()` (E-6) over every registry slot.

## E-1a `dict::version_registry` (public, additive)

| Addition | Meaning |
|---|---|
| `transport() const noexcept` → `core::expected_t<Dictionary const*>`, as `get()` | the FIXT.1.1 dictionary among the engine dictionaries, last writer wins as for every slot; `dict_no_dictionary_for_application_version` if none |

Registering a `vt11` dictionary no longer drops it. `get()` is unchanged: a `vt11` dictionary is not an application version. No mutable state, no cache, and its move operations and `noexcept` constructors stay as they are.

## E-2 Public `dict` helpers

| Symbol | From | Meaning |
|---|---|---|
| `session_to_application(session_version)` | moved from `version_registry.cpp`'s anonymous namespace to `version_profile.hpp` | `vt11` and `Unknown` map to `Unknown` |
| `is_fixt_admin_msgtype(string_view) noexcept` | made public from `reify.cpp`'s `is_fixt_admin` | the seven-member set of the spec's Terminology; `reify.cpp` calls it |
| `is_fixt_standard_header_tag(uint16_t) noexcept` | new, `dictionary.cpp`, over `kFixtFramingTable` minus 10, 89, 93, plus 370 | the peek's standard header set (research R-5 says why 370); `kFixtFramingTable` stays private to `src/dictionary/` |

## E-3 `Dictionary::as_table_view(fixt_framing)` (public overload)

Adds the framing surface for v40–v44. `v50`, `v50sp1` and `v50sp2` have it already. `vt11` and FIX Latest declare those tags themselves, and stay as `as_table_view()` builds them. `as_table_view()` is unchanged, so a non-FIXT session is unchanged.

**Preservation contract.** The result equals `as_table_view()`'s on every store but `fixt_framing_tags_` and `fixt_framing_types_`. Both forms bump `as_table_view_call_count`.

## E-4 `wire::dictionary_driven_validator`

Still holds its table by value, with the same constructor and `noexcept` specification. Adds no data member, so its layout does not move. `Validator`'s five pure virtuals are untouched (`[const §XIV.2]`): the two additions are non-virtual and on this `final` class only.

| Addition | Meaning |
|---|---|
| `validate(msg, scratch_mr, ref_tag_out, const table_view* envelope) const noexcept` | the virtual `validate` forwards here with a null envelope |
| `const table_view& table() const noexcept` | what the route set's aliasing owner points at |

`envelope` is read only on the failing branch of Step 1's tag check: a tag failing `valid_tags.contains` and `is_fixt_framing_tag`, with the envelope's `is_header_tag` true, passes, and **its enum and type are checked against the envelope**. The accept path is unchanged.

## E-5 The resolved application version: one contract

**Owner: the view.** `wire::MessageView::resolved_application_version()` returns a `dict::application_version`, default `Unknown`. It is one byte on `MessageView`.

**Original writer: the session only.** `parse_and_dispatch_` writes it from the frame's route (E-7), after the parse and before the callback, through `wire::detail::message_view_resolved_access`, a detail key shaped like `message_view_membership_access`. It is `Unknown` on a non-FIXT session, an admin message, an unresolved value, and any view that was not an inbound dispatch.

**Copy writers: exactly two, each from its source view, through the same key.** Both re-parse into a fresh view, so without this they would read `Unknown`:

- `fixpp_msg_clone` (`src/capi/message_write.cpp`), on the dict-backed and the dict-free branch.
- `dict::detail::owning_message_handle_from_frame` (`src/dictionary/reify.cpp`), on both branches. So `dict::reify(...)`'s handle's `view()` answers as its source did. A bridge file under `[arch §2.4]`, which already reads `message_view_membership_access`.

**Readers.**

- C: `fixpp_msg_get_resolved_appl_ver_id`, for an inbound view and a clone. `FIXPP_ERR_TAG_NOT_FOUND` when `Unknown`.
- Python: the flat SWIG function.
- `fixpp_msg_version` reads no part of this: it returns BeginString and raw tag 1137, unchanged (`Updated (099-…)` notes in research R-11).

**Against `dict::reify`'s own answer.** `owning_message_handle::version().application` is computed by `reify` from the frame's first 1128, else the profile, with the same resolver (FR-001). The two agree on every message a validating session delivers, because FR-026 keeps a delivered frame's first 1128 in its header and refuses an empty one (W-10). On a non-validating session they can differ, three ways, each with a W-6 cell and a B-row:

- a 1128 past the header: routing ignores it, `reify` reads it (research R-5);
- an empty `1128=`: the accessor is `Unknown` with S's view, and `reify` passes the empty value to the resolver, whose rule means "use the default";
- an initiator's reconnect whose reply has no 1137 it can resolve: `route_negotiated_` is `Unknown`, while `negotiated_version_profile()` keeps the previous value, so a `reify` with that profile labels the frame with the previous connection's version. The split follows from ruling 1, which keeps that accessor's writes.

## E-6 Session state (internal)

```text
route_entry {
  shared_ptr<const table_view> const* owner;
  dictionary_driven_validator const*  validator;   // null only when not validating, non-FIXT
  table_view const*                   envelope;    // S's table when this route's table is not S's, else null
}

fixt_routing_     bool: begin_string is FIXT.1.1 (the FIXT check has guaranteed the registry and default_appl_ver_id)
route_negotiated_ application_version: selection's read of the negotiated version, outside the establishment arms only; written on
                  every Logon, Unknown when an initiator's reply has nothing it can resolve. negotiated_appl_version_ keeps today's writes (FR-027)
routes_           shared_ptr<const fixt_route_set>: adopted from the Engine, or built in open(); null on a non-FIXT session
app_routes_       array<route_entry, slot count>, indexed by application_version
transport_route_  route_entry
session_route_    route_entry
```

Non-FIXT: `session_route_ = {&inbound_tv_, validator_.get(), nullptr}`, and nothing else is used. FIXT: `session_route_` is S's entry from the set, `inbound_tv_` is a copy of that owner unless `cfg_.dict_snapshot` supplies it (the C-ABI path, as today), and `validator_` stays null.

The gate's guard reads `session_route_.validator`, not `validator_`. `session_test_access::has_validator` becomes `cfg_.validate_inbound_messages && session_route_.validator`, which W-18's `hello` record and the run-thread rig read.

**Written in `open()`, before the first observable mutation**, on the same side of the commit as the carry and parse blocks. Read on the session's strand with no lock. A failed `open()` leaves `state_` at `never_opened`, and a retry may reassign a privately built `routes_`, under the same owner-object carve-out as `inbound_tv_`: no view exists then.

**Slot rule (FR-002).** For each version `v`: `registry.get(v)` is a dictionary `d`, or `v` is S's own version, or neither. S's entry if `v` is S's own version or `d`'s `session_version` is S's. Otherwise `routes_->find(d)`, with the envelope. Otherwise a null owner, which is a miss.

**Transport route.** S's entry if `defines_fixt_admin(S)`. Otherwise `registry.transport()`'s entry, if it passes the check, with the envelope. Otherwise, reachable only when not validating, S's entry.

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

**The peek's oracle** (one clause, research R-5). Walks from the frame's start with S's hooks. MsgType is the third field's. Stops at the first tag for which `inbound_tv_->is_header_tag(tag) || dict::is_fixt_standard_header_tag(tag)` is false; 10, 89 and 93 satisfy neither. Admin is `dict::is_fixt_admin_msgtype`. Whether a second 35 is in the header is read only on a mismatch: the cold path re-walks with the same stop rule.

**Selection,** once at arm entry, in every arm:

1. Not `fixt_routing_` → `session_route_`.
2. Peek unreadable → `session_route_`; the arm's fault and garble dispositions run as today. Admin → `transport_route_`.
3. An establishment arm (NotConnected, LogonSent) → `session_route_`, no refusal. The negotiated slot is not read.
4. 1128 present and empty → refusal `empty_value`, `session_route_`.
5. 1128 present → resolve. A failure → refusal `unknown_value`, `session_route_`. A miss on the slot → refusal `no_dictionary`, `session_route_`, `resolved` set.
6. 1128 absent → the slot of `route_negotiated_`; `Unknown` or a miss, reachable only when not validating → `session_route_`.

A refusal is acted on only by a validating session. A non-validating session uses the entry it carries.

**Steps a validating FIXT session adds.** Each runs after the arm's garble, supersede and fault checks.

| Step | Arm | What it does |
|---|---|---|
| **S1** | NotConnected, LogonSent | `route.application \|\| hdr.msg_type != hdr.fault_ref_msg_type` → Disconnected, nothing sent. It reads the third field: `interpret_logon` reads the last 35, so it could not refuse `D…A` carrying Logon fields |
| **S2** | same | Today's 3/5 exemption, then the gate on `transport_route_`. A Reject whose `ref_tag_id` is 1137 → emit it, then `disconnect_unless_superseded_`. Any other Reject stays in the arm, as today |
| **S3** | same | Today's `interpret_logon` with the transport route's hooks, then the arm's guards in today's order. The 1137 block: acceptor as today, after `check_inbound` and mTLS. **The initiator gains it right after `check_inbound`, ahead of the 141=Y reset unit and mTLS**: absent 373=1, empty, unparseable or no slot 373=5, Reject 371=1137, disconnect. At today's record site a `141=Y` reply with a bad 1137 would durably reset both stores and then be refused. Today's record stays where it is. A non-validating initiator records as today |
| **A1** | LogonReceived, Active | `hdr.msg_type != hdr.fault_ref_msg_type` → Reject 371=35, 373=13 if the second 35 is in the header, else 14. Ahead of the 3/5 exemption |
| **A2** | same | `route.refusal` → Reject 371=1128, 373=4 for `empty_value`, else 5 |
| **A3** | same | Today's 3/5 exemption, then the gate with the route: parse with its table; `application && !header_1128 && mv.get(1128)` has a value → Reject 373=14, 371=1128; then validate with its envelope |

A1, A2 and A3's probe fill `RejectDecision`'s new RefMsgType and RefSeqNum: the third field's value and the first 34 (`fault_ref_seq_num`). The third field's value also keys `consume_rejected_seqnum_`. A non-FIXT session leaves both empty, and the arm reads `hdr` as today. LogoutSent gains no step: it drains every frame as today, scanned with the route's hooks.

### The per-arm table

**Cell ids** are row-column: `NC`, `LS`, `AC`, `LO`, then a letter. W-4, W-5, W-9 and the mutant table cite them.

**Columns.** Frames named as `D…0`: third field 35=D, last 35=0, the later one past the header. `0,D`: adjacent, in the header. `+L`: also carrying 98, 108 and `1137=9`.

| | Column | Inputs |
|---|---|---|
| a | repeated 35 | `D…0`, `D…3`, `D…4`, `D…5`, `D…A`, `0…D`, `3…D`, `4…D`, `5…D`, `A…0`, `0,D`, `D…A+L`, `0…A+L`. Control: `D,D`, no split, as column d |
| b | route refusal | header `1128=99`, `abc`, `4` (no dictionary), empty, on a NewOrderSingle (NOS) |
| c | past-header 1128 | NOS with a body `1128=6`, `1128=abc` |
| d | ordinary application frame | NOS valid; `1128=9`; `1128=10`; missing 11, at the expected number and five ahead |
| e | Logon 1137 fault | absent, empty, `99`, `abc`, `4`. Controls `9`, `10`. Other fault: an undeclared tag 5999 with `9`, `4`, `99`, absent. `141=Y` with `4` |
| f | wrong BeginString | `8=FIX.4.4`: Logon with `9`, `99`, `4`, absent; NOS |
| g | stale SendingTime | Logon with `9`, `99`, `4`, absent; NOS; Heartbeat |
| h | off-list mTLS identity | Logon with `9`, `99`, `4`, absent |
| i | posture mismatch | production posture, `464=Y`: Logon with `9`, `99`, `4`, absent |

**Notation.** `R a/b x`: Reject 373=a, 371=b, 372=x. `Disc`: Disconnected. `stays`: state unchanged. `k`: NextNumIn kept. `c`: NextNumIn consumed. `—`: nothing sent. `app`, `adm`: delivered to `fromApp`, `fromAdmin`. `auth`: `session_event_compid_authorization_failed` raised.

#### Today: measured through a real `Session` at `c0a828ef`, validating (research R-18)

A "not reached" cell is not a measurement. **A** cannot establish, so its AC and LO rows are not reached and every A Logon below measures the tag 98 fault, not the column's input: `R 2/98 A`, stays, k.

| Row | a | b | c | d | e | f | g | h | i |
|---|---|---|---|---|---|---|---|---|---|
| **NC** T | third `D`: `R 2/8 <last>` stays k, but `D…3`, `D…5`: `Disc` — k. Third admin: `Disc` — k. `D…A+L`: `R 2/8 A` stays k. `0…A+L`: `R 2/98 A` stays k | `R 2/8 D` stays k | `R 2/8 D` stays k | `R 2/8 D` stays k | absent `R 1/1137 A` stays k. Others `R 5/1137 A` `Disc` c. 5999, whatever 1137 says: `R 2/5999 A` stays k | Logon `9`, `99`, `4`: `Disc` — k. Absent `R 1/1137 A` stays k. NOS `R 2/8 D` stays k | Logon `9`, `99`, `4`: `R 10/52 A` `Disc` k. Absent `R 1/1137 A` stays k. NOS `R 2/8 D` stays k. Heartbeat `Disc` — k | `9`, `99`, `4`: auth — `Disc` c. Absent `R 1/1137 A` stays k | `9`, `99`, `4`: Logout `Disc` k. Absent `R 1/1137 A` stays k |
| **NC** L | `Disc` — k. `D…A+L`, `0…A+L`: `R 2/98 A` stays k | `99`, `abc`: `R 5/1128 D` stays k. `4`, empty: `Disc` — k | `6`: `Disc` — k. `abc`: `R 5/1128 D` stays k | valid: `Disc` — k. Missing 11: `R 1/11 D` stays k | absent `R 1/1137 A` stays k. `99`, `abc`: `R 5/1137 A` stays k. Empty, `4`: `R 5/1137 A` `Disc` c. 5999: `R 2/5999 A` stays k, but with `99`: `R 5/1137 A` | `9`, `4`: `Disc` — k. `99`: `R 5/1137 A` stays k. Absent `R 1/1137 A` stays k. NOS `Disc` — k | `9`, `4`: `R 10/52 A` `Disc` k. `99`: `R 5/1137 A` stays k. Absent `R 1/1137 A` stays k. NOS, Heartbeat `Disc` — k | `9`, `4`: auth — `Disc` c. `99`: `R 5/1137 A` stays k, no auth. Absent likewise `R 1/1137 A` | `9`, `4`: Logout `Disc` k. `99`: `R 5/1137 A` stays k. Absent `R 1/1137 A` stays k |
| **NC** A | third `D`: `Disc` — k. Third admin with a body tag: `R 2/<tag> <last>` stays k. `0,D`: `Disc` — k. `+L`: `R 2/98 A` stays k | as NC L | as NC L | as NC L | the 98 fault | the 98 fault; NOS `Disc` — k | the 98 fault; NOS, Heartbeat `Disc` — k | the 98 fault | the 98 fault |
| **LS** T | as NC T | as NC T | as NC T | as NC T | absent `R 1/1137 A` stays k. Others, `141=Y` with `4` too: Active — c. 5999 as NC T | as NC T | as NC T, with Logout (`SendingTime(52) accuracy`) `Disc` k for `R 10/52 A` `Disc` k | as NC T | as NC T |
| **LS** L | as NC L | as NC L | as NC L | as NC L | absent `R 1/1137 A` stays k. `99`, `abc`: `R 5/1137 A` stays k. Empty, `4`: Active — c, `141=Y` with `4` too. 5999 as NC L | as NC L | as NC L, with the same Logout for `R 10/52 A` | as NC L | as NC L |
| **LS** A | as NC A | as NC A | as NC A | as NC A | the 98 fault | the 98 fault; NOS `Disc` — k | the 98 fault; NOS, Heartbeat `Disc` — k | the 98 fault | the 98 fault |
| **AC** T | `D…0`: `R 2/8 0` stays c. `D…3`: — stays c, in silence. `D…4`, `D…A`: `R 2/8 <last>` stays k. `D…5`: adm, Logout, `Disc` c. `0…D`, `3…D`, `4…D`, `5…D`, `0,D`: app stays c. `A…0`: adm stays c. `+L`: `R 2/8 A`, `R 2/98 A`, stays k | `R 2/8 D` stays c | `R 2/8 D` stays c | `R 2/8 D` stays c; five ahead: k | absent `R 1/1137 A` stays k. Others: app stays c | `9`, `99`, `4`: `Disc` — k. Absent `R 1/1137 A` stays k. NOS `R 2/8 D` stays c | `9`, `99`, `4`: `R 10/52 A`, Logout, `Disc` k. Absent `R 1/1137 A` stays k. NOS `R 2/8 D` stays c. Heartbeat `R 10/52 0`, Logout, `Disc` c | not reached: this arm has no such check (source) | not reached (source) |
| **AC** L | `D…0`: adm stays c. `D…3`: — stays c. `D…4`: adm stays k. `D…A`: app stays c. `D…5`: as AC T. `0…D`, `3…D`, `4…D`, `5…D`, `0,D`, `D,D`: app stays c. `A…0`: adm stays c. `+L`: `R 2/98 A` stays k | `99`, `abc`: `R 5/1128 D` stays c. `4`, empty: app stays c | `6`: app stays c. `abc`: `R 5/1128 D` stays c | valid: app stays c. Missing 11: `R 1/11 D` stays c; five ahead: k | absent `R 1/1137 A` stays k. `99`, `abc`: `R 5/1137 A` stays k. Others: app stays c | `9`, `4`: `Disc` — k. `99`: `R 5/1137 A` stays k. Absent `R 1/1137 A` stays k. NOS `Disc` — k | `9`, `4`: `R 10/52 A`, Logout, `Disc` k. `99`: `R 5/1137 A` stays k. Absent `R 1/1137 A` stays k. NOS `R 10/52 D`, Logout, `Disc` c. Heartbeat as AC T | not reached (source) | not reached (source) |
| **LO** T, L | — stays k. `D…5`: — `Disc` k | — stays k | — stays k | — stays k | — stays k | — stays k | — stays k | not reached (source) | not reached (source) |

LogonReceived is the Active arm's code and is not separately reachable with a frame (research R-18).

**Validation off, measured, the same on T, A and L.** Establishment arms: every a to d and f cell `Disc` — k, except `D…A+L` and `0…A+L`, which **establish**: Logon reply from an acceptor, Active, c. `NC-e`: `R 1/1137 A` for absent, `R 5/1137 A` otherwise, `Disc` c. `LS-e`: Active — c. Column g: `R 10/52 A` `Disc` k in NC, the Logout in LS, whatever 1137 says. Column h: auth — `Disc` c. Column i: Logout `Disc` k. Active: `D…0`, `A…0`: adm c; `D…3`: — c; `D…4`: adm k; `D…5`: adm, Logout, `Disc` c; every other a to e frame: app c, or a ResendRequest and k when five ahead. Column f: `Disc` — k. Column g: `R 10/52 <third>`, Logout, `Disc`: k on a Logon, c otherwise. LO as validating.

#### After: design, not measured, read from each arm against the table above

Written to the recommendations of `plan.md` items 2 and 3. **A answers as T** in every row, since FIXT11 is its transport dictionary. Validation off: the paragraph above stands, apart from what the route's hooks change (below).

| Row | a | b | c | d | e | f | g | h | i |
|---|---|---|---|---|---|---|---|---|---|
| **NC** T, A | `Disc` — k (S1). `D,D` as d | `Disc` — k (S1) | `Disc` — k (S1) | `Disc` — k (S1) | absent `R 1/1137 A` `Disc` k (S2). Others `R 5/1137 A` `Disc` c (S3). **5999, whatever 1137 says: `R 2/5999 A` stays k**: the gate names 5999, so S2 does not fire | `9`, `99`, `4`: `Disc` — k. Absent `R 1/1137 A` `Disc` k (S2). NOS `Disc` — k (S1) | `9`, `99`, `4`: `R 10/52 A` `Disc` k. Absent `R 1/1137 A` `Disc` k (S2). NOS, Heartbeat `Disc` — k | `9`, `99`, `4`: auth — `Disc` c. Absent `R 1/1137 A` `Disc` k, no auth | `9`, `99`, `4`: Logout `Disc` k. Absent `R 1/1137 A` `Disc` k |
| **NC** L | as T, A | as T, A | as T, A | as T, A | absent, `99`, `abc`: `R 1/1137 A` or `R 5/1137 A` `Disc` k (S2). Empty, `4`: `R 5/1137 A` `Disc` c (S3). 5999: `R 2/5999 A` stays k, but with `99`: `R 5/1137 A` `Disc` k | `9`, `4`: `Disc` — k. `99`: `R 5/1137 A` `Disc` k. Absent `R 1/1137 A` `Disc` k. NOS `Disc` — k | `9`, `4`: `R 10/52 A` `Disc` k. `99`: `R 5/1137 A` `Disc` k. Absent `R 1/1137 A` `Disc` k. NOS, Heartbeat `Disc` — k | `9`, `4`: auth — `Disc` c. `99`, absent: their Reject, `Disc` k, no auth | `9`, `4`: Logout `Disc` k. `99`, absent: their Reject, `Disc` k |
| **LS** T, A | as NC | as NC | as NC | as NC | absent `R 1/1137 A` `Disc` k (S2). Others `R 5/1137 A` `Disc` c (S3, new), and with `141=Y` no reset unit runs. 5999 as NC | as NC | as NC, with the Logout for `R 10/52 A` | **`9`: auth — `Disc` c. `99`, `4`: `R 5/1137 A` `Disc` c, no auth, because S3's block is ahead of mTLS here.** Absent as NC | as NC |
| **LS** L | as NC | as NC | as NC | as NC | as NC L, and `141=Y` as LS T, A | as NC L | as NC L, with the Logout | `9`: auth — `Disc` c. `4`: `R 5/1137 A` `Disc` c, no auth. `99`, absent as NC L | as NC L |
| **AC** T, A, L | `R 14/35 <third>` stays, c. `4…D`, `A…0`: k. `0,D`: `R 13/35 0` stays c. Five ahead: k. `D,D` as d (A1) | `99`, `abc`, `4`: `R 5/1128 D` stays c. Empty: `R 4/1128 D` stays c (A2) | `R 14/1128 D` stays c (A3) | valid: app stays c, on the negotiated slot or 1128's; five ahead: ResendRequest, k. Missing 11: `R 1/11 D` stays c; five ahead: k | **T, A:** absent `R 1/1137 A` stays k. Others: app stays c, accessor `Unknown`. **L:** as AC L today. FR-010 does not reach this arm | NOS `Disc` — k. Logon: as today on the row's shape | NOS `R 10/52 D`, Logout, `Disc` c. Heartbeat `R 10/52 0`, Logout, `Disc` c. Logon: as today on the row's shape | not a check of this arm (source) | not a check of this arm (source) |
| **LO** T, A, L | — stays k. `D…5`: — `Disc` k, as today: FR-025 does not reach this arm | — stays k | — stays k | — stays k | — stays k | — stays k | — stays k | not a check of this arm (source) | not a check of this arm (source) |

What moved, from the table above: T's and A's Reject-and-stay on an application frame in an establishment arm (S1). `R 1/1137` and L's `R 5/1137` stay to Disc (S2). An initiator's Active on a bad 1137 to a Reject and Disc, ahead of its reset unit and mTLS (S3). Every delivered or silently consumed repeated-35 frame in Active to a Reject (A1). T's `R 2/8` on an application frame to its route's answer, which moves column f's and g's NOS to `Disc` and to the SendingTime ending, as L does today.

**Split left in place: the consume column of `NC-e` and `LS-e`.** Under the recommendation, `99` and `abc` keep the number on L, through the gate, and consume it on T and A, through the block. Observable only on an initiator's reconnect. Owner item 3 states it; W-5 pins both literals.

**A validation-off frame's new ending.** The route's hooks scan a frame whose pair only that table declares. A malformed such pair on a non-validating session then takes `dispose_unparseable_`'s Reject in Active, where at `c0a828ef` S's hooks read past it and delivered the frame. B-row; W-6's `off-pair` cell.

## E-8 C ABI

`fixpp_engine_config` gains `std::vector<std::shared_ptr<const Dictionary>> dictionaries`, **copied** into `EngineConfig` in `fixpp_engine_create`, not moved. `engine.h` promises the builder is untouched on failure, and the only failure after that step is an allocation one, so W-20's C arm is what can tell a copy from a move.

## E-9 Check helper

`fixt_session_config_valid(const SessionConfig&, const dict::version_registry*) noexcept`, in `src/session/`, with `defines_fixt_admin`. Clauses: `begin_string == "FIXT.1.1"` with no `default_appl_ver_id`; a null registry; a registry with no slot for the default; and, when validating, no transport dictionary. Called from `open()` when `begin_string == "FIXT.1.1"`, and from `Engine::register_session` when `cfg.validate_inbound_messages && cfg.begin_string == "FIXT.1.1"`, not `cfg.is_fixt()`, which is false for the first clause's config (research R-2). The Engine's registry is never null, so the null clause is reachable only from a direct `open()`.

## State transitions

No new FSM states. The endings are E-7's.
