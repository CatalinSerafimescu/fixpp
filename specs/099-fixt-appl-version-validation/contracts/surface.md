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
| `Engine::start()` | also returns `out_of_memory` on a route-set build failure. Blocks for the walks on an engine with a FIXT session, so a host calling it from a handler on `exec` stalls that thread; its two `engine.hpp` comments change to say so (research R-3) |
| `SessionConfig`, `Session::open()` comments | state that the dictionary has no engine anchor and is required on the session (#651); the phrase `value_or(engine_anchor)` stays only for the axes that have one |

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
    uint8_t _reserved;
} fixpp_resolved_msg_version_t;
fixpp_error_t fixpp_msg_version(const fixpp_msg_t* msg, fixpp_resolved_msg_version_t* version_out);  /* same signature, new meaning */
```

| Symbol | Errors | Lifetime | Reentrancy (`[const §X.5]`) |
|---|---|---|---|
| `set_default_appl_ver_id` | NULL cfg → `FIXPP_ERR_NULL_HANDLE`. NULL, empty or a value other than `"2"`…`"9"`, `"10"` → `FIXPP_ERR_CAPI_CONFIG_INVALID` | copied | single-thread |
| `set_validate_inbound_messages` | NULL cfg → `FIXPP_ERR_NULL_HANDLE` | copied | single-thread |
| `add_dictionary` | As `fixpp_session_config_set_dictionary`: NULL cfg → `FIXPP_ERR_NULL_HANDLE`; NULL dict or an empty wrapper → `FIXPP_ERR_CAPI_CONFIG_INVALID`; destroyed or wrong-type handle → `FIXPP_ERR_INVALID_HANDLE` | copies the shared pointer; several calls accumulate, in order; `fixpp_engine_create` copies the list | single-thread |
| `fixpp_msg_version` | Unchanged: NULL out-pointer → `FIXPP_ERR_NULL_HANDLE`; then `check_inbound_msg`; tag 8 absent → `FIXPP_ERR_TAG_NOT_FOUND`, zeros. An unresolved version is `FIXPP_ERR_OK` with `FIXPP_APPL_VER_UNKNOWN` | by value, nothing aliases the wire | requires-session-lock, as today; callable from the receive callback, takes no lock |

Values per handle: data-model E-8. Each changed or new declaration carries one reentrancy token (`tools/check_capi_reentrancy.sh`). The five headers' hashes go into `tools/capi_freeze.sha256` (research R-11; W-12 shows the gate RED first). Constants are additive, in `c_api/dict.h`. **Migration:** a caller that read BeginString or raw 1137 reads `fixpp_msg_get_string(msg, 8, …)` or `(msg, 1137, …)`.

**A refused config.** The refusal reaches the caller at registration, not from `fixpp_engine_start`: `fixpp_session_open` returns `FIXPP_ERR_THREAD_CONFIG` (the frozen mapping of `invalid_session_config`), and the builder is not consumed. `fixpp_session_open`'s and the validation setter's header comments say so. Nothing returns this cause from `fixpp_engine_start`.

## Python

The flat SWIG functions for the three setters and `msg_version`, which returns `(kind, session, application)`, with GIL table rows; the constants arrive through `dict.h`'s `%include`. `Dictionary` in `fixpp_oo.py` is passed through. `EXPECTED_PUBLIC_SURFACE` updated. A refused config raises `fixpp.SessionError` from `session_open`, with `.name` `FIXPP_ERR_THREAD_CONFIG`. `engine_start` holds the GIL across the walks of a FIXT engine; its GIL table row says so, with the reason `dict_load_from_xml`'s row uses (research R-11).

## TOML

```toml
[dictionary]
kind = "path"
path = "FIX50SP2.xml"
extra_paths = ["FIXT11.xml", "orchestra/OrchestraFIXLatest.xml"]   # optional
```

Each entry is resolved and loaded as `path` is. A failure, a non-array or a non-string entry is a diagnostic at
`dictionary.extra_paths`. `kind` other than `path` is refused as today. A session's `dictionary` default is the `path` file. No overlay
applies to an extra. `load_toml_config` builds no engine and runs no FIXT transport check: a validating FIXT session with no transport dictionary loads, and the host's `Engine::register_session` returns `invalid_session_config`.

## Observable behaviour

Non-FIXT session: none. FIXT session: **data-model E-7 is the one source**, per state and per input, with and without validation. Beyond it:

| FIXT session | Validating | Not validating |
|---|---|---|
| which dictionary answers an admin frame | transport dictionary, S as envelope when it is not S | transport dictionary if any, else S |
| which answers an application frame in LogonReceived or Active | 1128's dictionary, else the Message Type Default's (data-model E-10), else the negotiated slot, S as envelope when it is not S | the same, else S |
| no transport dictionary | `register_session` and `open()` refuse | opens |

## Disclosed, not changed

- A tag an overlay adds to S's trailer is not honoured on an application message answered by another table (L-row; narrows FR-008).
- A body Length+Data pair of a routed dictionary sharing a tag with a pair S declares in its header is not honoured on that session: S's wins (B-row, research R-20).
- A header tag S declares, past the body, is judged by S and passes. QuickFIX answers 373=14 (B-row, research R-6).
- With validation off, `35=D … 98=0|108=30|1137=9|35=A` establishes a session of either role, because `interpret_logon` reads the last 35; the other repeated-35 splits of E-7's validation-off paragraph stay too (B-row, FR-025). LogoutSent takes a last MsgType of 5 as the reply, whatever the third field says, on a validating session too.
- `35=A` once Active is outside FR-010: on T it reaches `fromApp`, on L `1137=99` draws a Reject (B-row).
- A malformed pair only the route's table declares takes a Reject on a non-validating session, where `c0a828ef` delivered the frame (B-row, FR-011).
- 370 is a header tag on a FIXT session whatever the route, judged by the route's table. QuickFIX C++ refuses it (B-row).
- A non-FIXT validating session keeps the first-wins, last-wins MsgType split (B-row; byte-identical; to file).
- `toApp` views and resend replay use S's table (L-row).
- Message Type Default goes beyond all three QuickFIX engines, which ignore the group (B-row). Not implemented: declaring such defaults in fixpp's own Logon, acting on a peer's `385=R` entries for what fixpp sends, and 1406 and 1131 (L-row, FR-024). On L, whose dictionary closes 1130, a bad 1130 on any entry disconnects; on T and A only an entry that counts does (data-model E-10, B-row).
- `dictionaries/FIXT11.xml` is the QuickFIX C++ file plus MsgTypeGrp on Logon, not verbatim (FR-032).
- A non-validating FIXT session whose config fails the FIXT check is refused only at `open()` inside the role loop: logged when a logger is set, not returned (L-row, owner item 5). The line's format string is in `src/log/format_registry.cpp`.
- `FIXT11.xml`'s 1128 enum stops at 9, so an admin frame carrying `1128=10` draws 373=5 from a FIXT11 transport dictionary (B-row). An
  application frame with `1128=10` routes to FIX Latest, as QuickFIX/J; QuickFIX C++ rejects `1128=10` on every message.
- Header Required-ness is not enforced on an application message (B-081-1 stays).
