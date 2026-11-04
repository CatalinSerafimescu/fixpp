# Contract: public surface of 099

## C++

| Symbol | Contract |
|---|---|
| `version_registry::transport()` | data-model E-1a |
| `dict::session_to_application`, `dict::is_fixt_admin_msgtype`, `dict::is_fixt_standard_header_tag` | data-model E-2 |
| `Dictionary::as_table_view(fixt_framing)`, and the form taking extra pairs; `dict::header_length_pairs`; `dict::is_route_answered_header_tag` | data-model E-2, E-3 |
| `dictionary_driven_validator::validate(…, envelope)`, `table()` | data-model E-4 |
| `MessageView::resolved_application_version()`; `dict::reify` takes it when set | data-model E-5 |
| `Engine::register_session` | also returns `invalid_session_config` for a validating FIXT session failing the check (data-model E-9) |
| `Engine::start()` | also returns `out_of_memory` on a route-set build failure, which leaves the Engine as constructed: nothing spawned, no set held, and `start()` may be called again, so "once" means once successfully (W-20's Engine arm). Blocks for the walks on an engine with a FIXT session: legal from a handler on `exec`, where it stalls that thread, so a host that cannot afford that calls it elsewhere. Its `engine.hpp` comments change to say so (research R-3) |
| `Session::open()`, direct-constructed | a FIXT session with no adopted set walks in `open()` (data-model E-1), and stalls the thread running it for as long |
| `SessionConfig`, `Session::open()` comments | state that the dictionary has no engine anchor and is required on the session (#651); the phrase `value_or(engine_anchor)` stays only for the axes that have one |

`session.hpp` is installed and gains no public name: data-model E-6's, E-7's and E-10's members and nested types, and `RejectDecision`'s flag, are private. `sizeof(Session)` moves (B-row).

## C ABI 1.14

```c
fixpp_error_t fixpp_session_config_set_default_appl_ver_id(fixpp_session_config_t* cfg, const char* wire_value);
fixpp_error_t fixpp_session_config_set_validate_inbound_messages(fixpp_session_config_t* cfg, bool validate);
fixpp_error_t fixpp_engine_config_add_dictionary(fixpp_engine_config_t* cfg, fixpp_dict_t* dict);

/* BREAKING ([const §X.7]): was {begin_string, begin_string_len, appl_ver_id, appl_ver_id_len}. */
typedef struct fixpp_resolved_msg_version {
    uint8_t kind;         /* FIXPP_MSG_KIND_SESSION_ADMIN = 0, FIXPP_MSG_KIND_APPLICATION = 1 */
    uint8_t session;      /* FIXPP_SESSION_VER_*, values of dict::session_version */
    uint8_t application;  /* FIXPP_APPL_VER_*, values of dict::application_version; UNKNOWN = 0 */
    uint8_t _reserved;    /* 0 */
} fixpp_resolved_msg_version_t;
fixpp_error_t fixpp_msg_version(const fixpp_msg_t* msg, fixpp_resolved_msg_version_t* version_out);  /* same signature, new meaning */
```

| Symbol | Errors | Lifetime | Reentrancy (`[const §X.5]`) |
|---|---|---|---|
| `set_default_appl_ver_id` | NULL cfg → `FIXPP_ERR_NULL_HANDLE`. NULL, empty or a value other than `"2"`…`"9"`, `"10"` → `FIXPP_ERR_CAPI_CONFIG_INVALID` | copied | single-thread |
| `set_validate_inbound_messages` | NULL cfg → `FIXPP_ERR_NULL_HANDLE` | copied | single-thread |
| `add_dictionary` | As `fixpp_session_config_set_dictionary`: NULL cfg → `FIXPP_ERR_NULL_HANDLE`; NULL dict or an empty wrapper → `FIXPP_ERR_CAPI_CONFIG_INVALID`; destroyed or wrong-type handle → `FIXPP_ERR_INVALID_HANDLE` | copies the shared pointer; several calls accumulate, in order, so data-model E-1a's last writer wins holds for two of one version; `fixpp_engine_create` copies the list | single-thread |
| `fixpp_msg_version` | Unchanged: NULL out-pointer → `FIXPP_ERR_NULL_HANDLE`; then `check_inbound_msg`; tag 8 absent → `FIXPP_ERR_TAG_NOT_FOUND`, zeros. An unresolved version is `FIXPP_ERR_OK` with `FIXPP_APPL_VER_UNKNOWN` | by value, nothing aliases the wire | thread-safe, `[2i §4.6]`'s token: on a const message it reads the view and writes only `version_out` (read from `check_inbound_msg` and `MessageView::get`); callable from the receive callback, takes no lock. The view's `mutable` members are per view, and only group and unknown-field reads fill them; this getter makes neither. The session writes E-5's byte before the callback, so no reader sees it change. An inbound handle stays valid only until the callback returns: the token does not extend that |

A later valid setter call replaces the earlier value, a rejected call leaves it, and a default cannot be cleared: destroy the builder. The validation setter applies to every session the config opens, a non-FIXT one included, where it is C++'s existing validation; W-12 runs T and A only, so that case has no C witness.

Values per handle: data-model E-8. Each changed or new declaration carries one reentrancy token (`tools/check_capi_reentrancy.sh`). The five headers' hashes go into `tools/capi_freeze.sha256` (research R-11; W-12 shows the gate RED first). Constants are additive, in `c_api/dict.h`, a closed list with the enums' values: `FIXPP_APPL_VER_{UNKNOWN,FIX40,FIX41,FIX42,FIX43,FIX44,FIX50,FIX50SP1,FIX50SP2,FIXLATEST}`, `FIXPP_SESSION_VER_{UNKNOWN,FIX40,FIX41,FIX42,FIX43,FIX44,FIX50,FIX50SP1,FIX50SP2,FIXT11,FIXLATEST}`, `FIXPP_MSG_KIND_{SESSION_ADMIN,APPLICATION}`. **Migration:** a caller that read BeginString or raw 1137 reads `fixpp_msg_get_string(msg, 8, …)` or `(msg, 1137, …)`. Those are wire values and the struct's bytes are the constants, which do not coincide (`1137=9` is `FIXPP_APPL_VER_FIX50SP2`). No mapping call is added, and `dict.h`'s comment on the constants says so.

**A refused config.** The refusal reaches the caller at registration, not from `fixpp_engine_start`: `fixpp_session_open` returns `FIXPP_ERR_THREAD_CONFIG` (the frozen mapping of `invalid_session_config`), and the builder is not consumed. `fixpp_session_open`'s and the validation setter's header comments say so. Nothing returns this cause from `fixpp_engine_start`. The code does not say which clause refused: every `invalid_session_config` cause shares it, in C++ and in C, E-9's four among them, and `fixpp_session_open`'s comment says so. No finer report is added.

**`fixpp_engine_start`.** A table build failure returns `FIXPP_ERR_UNKNOWN` (research R-11) and leaves the engine not started, so a later call may retry; a second call after a success is the domain error `engine.h` documents. On an engine with a FIXT session it blocks the calling thread for the walks, before any worker runs. `engine.h`'s comment says all of this. Its token and `fixpp_session_open`'s stay `single-thread`.

## Python

The flat SWIG functions for the three setters and `msg_version`, which returns `(kind, session, application)`, with GIL table rows, each `hold` because none makes an engine round trip, so `msg_version` may run on the receive callback's thread; the constants arrive through `dict.h`'s `%include`. `Dictionary` in `fixpp_oo.py` is passed through, and the OO layer takes the same flat config handles (`Engine(config)`, `open_session(config)`), so a host on it calls the flat setters: no OO method is added. A non-OK code raises through the error bridge, as from every flat function: `FIXPP_ERR_TAG_NOT_FOUND`'s class from `msg_version` with tag 8 absent, `fixpp.Unknown` from `engine_start` on a table build failure. `EXPECTED_PUBLIC_SURFACE` updated. A refused config raises `fixpp.SessionError` from `session_open`, with `.name` `FIXPP_ERR_THREAD_CONFIG`. `engine_start` holds the GIL across the walks of a FIXT engine; its GIL table row says so, with the reason `dict_load_from_xml`'s row uses (research R-11).

## TOML

```toml
[dictionary]
kind = "path"
path = "FIX50SP2.xml"
extra_paths = ["FIXT11.xml", "orchestra/OrchestraFIXLatest.xml"]   # optional
```

Each entry is resolved and loaded as `path` is: `resolve_path`, then `dict::load_any` under `trap_throw_to_expected`. A failure, a non-array, or a non-string or empty entry is a diagnostic at
`dictionary.extra_paths`, whose message names the entry by index, and each bad entry adds its own. An empty array is an absent key. A repeated file is loaded again, as another dictionary and another walk. `kind` other than `path` is refused as today, at `dictionary.kind`, and this key is not read. A session's `dictionary` default is the `path` file, filled in by the loader: no engine fallback (FR-019). An extra of `path`'s version takes its registry slot (data-model E-1a), and a session whose S is the `path` file still answers that version with S (FR-002). No overlay
applies to an extra. `load_toml_config` builds no engine and runs no FIXT transport check: a validating FIXT session with no transport dictionary loads, and the host's `Engine::register_session` returns `invalid_session_config`.

## Observable behaviour

Non-FIXT session: as SC-005 states. FIXT session: **data-model E-7 is the one source**, per state and per input, with and without validation. Beyond it:

| FIXT session | Validating | Not validating |
|---|---|---|
| which dictionary answers an admin frame | transport dictionary, S as envelope when it is not S | transport dictionary if any, else S |
| which answers an application frame in LogonReceived or Active | 1128's dictionary, else the Message Type Default's (data-model E-10), else the negotiated slot, S as envelope when it is not S | the same, else S |
| no transport dictionary | `register_session` and `open()` refuse | opens |

## Disclosed, not changed

- A tag an overlay adds to S's trailer is not honoured on an application message answered by another table, and the structure of a header group an overlay adds to S is not judged there (L-row; narrows FR-008).
- A malformed NoHops draws 373=1, 371=628 on every route and shape, the validator's existing mapping, which B85 owns the wording of. QuickFIX C++ does not count header groups, and J and Go answer 373=16 (B-row, data-model E-3).
- On shape A, whose S declares no header, the routed table judges a standard header field: with FIX44 registered, `1128=6|347=KOI8-R` draws 373=5, 371=347 on A and passes on T. All three QuickFIX engines judge it with the transport dictionary, so A departs and T does not (B-row, research R-6).
- A body Length+Data pair of a routed dictionary sharing a tag with a pair S declares in its header is not honoured on that session: S's wins (B-row, research R-20).
- A header tag S declares, past the body, is judged by S and passes. QuickFIX answers 373=14 (B-row, research R-6).
- With validation off, `35=D … 98=0|108=30|1137=9|35=A` establishes a session of either role, because `interpret_logon` reads the last 35; the other repeated-35 splits of E-7's validation-off bullet stay too (B-row, FR-025). LogoutSent takes a last MsgType of 5 as the reply, whatever the third field says, on a validating session too.
- `35=A` once Active is outside FR-010: on T it reaches `fromApp`, on L `1137=99` draws a Reject (B-row).
- A malformed pair only the route's table declares takes a Reject on a non-validating session, where `c0a828ef` delivered the frame (B-row, FR-011).
- On a non-validating shape A session the view's `group(627)` and `fixpp_msg_get_group` answer NoHops, where `c0a828ef`'s did not; `get(628)` does not change (B-row, FR-011).
- 370 is a header tag on a FIXT session whatever the route, judged by the route's table. QuickFIX C++ refuses it (B-row).
- A non-FIXT validating session keeps the first-wins, last-wins MsgType split (B-row; byte-identical; to file).
- `toApp` views and resend replay use S's table (L-row).
- Message Type Default goes beyond all three QuickFIX engines, which ignore the group (B-row). Not implemented: declaring such defaults in fixpp's own Logon, acting on a peer's `385=R` entries for what fixpp sends, and 1406 and 1131; the eight-byte RefMsgType and 256 counted entries are limits (L-row, FR-024; data-model E-10).
- A 372 outside FIX Latest's closed MsgType domain, on any entry, counted or not, and a short custom `372=U1` among them, draws `R 5/372 A` `Disc` on L, because L's dictionary closes it. T's and A's domain is open by their own dictionary, so data-model E-10's rule decides there (B-row).
- `dictionaries/FIXT11.xml` is the QuickFIX C++ file plus MsgTypeGrp on Logon, not verbatim (FR-032). The regenerated `vt11` headers gain the group on the typed Logon, additive. A dictionary supplied without the group answers such a Logon `R 2/384 A`, stays (B-row, data-model E-10). An overlay that already declares those fields meets a base declaring them: 098's merge rules decide, as for any field the base declares, and a conflict is refused at load.
- `Engine::start()` blocks on an engine with a FIXT session, which holds one full table per route table for its lifetime, and Python's `engine_start` holds the GIL across the walks, so a second engine's worker waits in `PyGILState_Ensure` (B-row, research R-3, R-11).
- Two 1128s in the header read first-wins. QuickFIX C++ and Go answer 373=13 (B-row, research R-5).
- `dict::reify` ignores the profile its caller passes when the byte is set (B-row, data-model E-5).
- A peer's 1128 can select any registered dictionary, whatever the session default: registering one is what permits it (B-row).
- A non-validating FIXT session whose config fails the FIXT check is refused only at `open()` inside the role loop: logged when a logger is set, not returned (L-row, owner item 5). `open()`'s reservation failing on an Engine route takes the same branch (FR-028). The line's format string is in `src/log/format_registry.cpp`; it is error level, through `SessionConfig::logger_override` ahead of `EngineConfig::logger`, and carries `begin_string`, `sender_comp_id`, `target_comp_id` and the error, so the two causes read apart by the error's value. A CompID past 15 bytes is cut in the line (`ArgValue`'s inline string), so two sessions whose CompIDs share their first 15 bytes read alike. The loop ends, the other sessions run, and `lookup()` answers null for that one.
- `FIXT11.xml`'s 1128 enum stops at 9, so an admin frame carrying `1128=10` draws 373=5 from a FIXT11 transport dictionary (B-row). An
  application frame with `1128=10` routes to FIX Latest, as QuickFIX/J; QuickFIX C++ rejects `1128=10` on every message.
- Header Required-ness is not enforced on an application message (B-081-1 stays).
