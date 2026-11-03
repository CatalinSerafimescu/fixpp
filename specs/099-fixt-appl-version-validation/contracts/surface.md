# Contract: public surface of 099

## C++

| Symbol | Contract |
|---|---|
| `version_registry::transport()` | data-model E-1a |
| `dict::session_to_application`, `dict::is_fixt_admin_msgtype`, `dict::is_fixt_standard_header_tag` | data-model E-2 |
| `Dictionary::as_table_view(fixt_framing)` | data-model E-3 |
| `dictionary_driven_validator::validate(…, envelope)`, `table()` | data-model E-4 |
| `MessageView::resolved_application_version()` | data-model E-5 |
| `Session` constructor, fourth defaulted parameter | a non-owning route set; Engine routes pass theirs, a direct-constructed session omits it (research R-3) |
| `Engine::register_session` | also returns `invalid_session_config` for a validating FIXT session failing the check (data-model E-9) |
| `Engine::start()` | also returns `out_of_memory` on a route-set build failure; blocks for the walks on an engine with a FIXT session |
| `SessionConfig`, `Session::open()` comments | state that the dictionary has no engine anchor and is required on the session (#651); the phrase `value_or(engine_anchor)` stays only for the axes that have one |

## C ABI 1.14 (additive)

```c
fixpp_error_t fixpp_session_config_set_default_appl_ver_id(fixpp_session_config_t* cfg, const char* wire_value);
fixpp_error_t fixpp_session_config_set_validate_inbound_messages(fixpp_session_config_t* cfg, bool validate);
fixpp_error_t fixpp_engine_config_add_dictionary(fixpp_engine_config_t* cfg, fixpp_dict_t* dict);
fixpp_error_t fixpp_msg_get_resolved_appl_ver_id(const fixpp_msg_t* msg, const char** value_out, size_t* len_out);
```

| Symbol | Errors | Lifetime | Reentrancy (`[const §X.5]`) |
|---|---|---|---|
| `set_default_appl_ver_id` | NULL cfg → `FIXPP_ERR_NULL_HANDLE`. NULL, empty or a value other than `"2"`…`"9"`, `"10"` → `FIXPP_ERR_CAPI_CONFIG_INVALID` | copied | single-thread |
| `set_validate_inbound_messages` | NULL cfg → `FIXPP_ERR_NULL_HANDLE` | copied | single-thread |
| `add_dictionary` | As `fixpp_session_config_set_dictionary`: NULL cfg → `FIXPP_ERR_NULL_HANDLE`; NULL dict or an empty wrapper → `FIXPP_ERR_CAPI_CONFIG_INVALID`; destroyed or wrong-type handle → `FIXPP_ERR_INVALID_HANDLE` | copies the shared pointer; several calls accumulate, in order; `fixpp_engine_create` copies the list | single-thread |
| `get_resolved_appl_ver_id` | NULL out-pointer → `FIXPP_ERR_NULL_HANDLE`; `FIXPP_ERR_TAG_NOT_FOUND` when the message has no resolved version | static storage, valid for the process | requires-session-lock, as `fixpp_msg_version`; callable from the receive callback, takes no lock |

`fixpp_msg_version` is unchanged, including that its `appl_ver_id` reads tag 1137. The binding documents gain the notes in research R-11.

**A refused config.** The refusal reaches the caller at registration, not from `fixpp_engine_start`: `fixpp_session_open` returns `FIXPP_ERR_THREAD_CONFIG` (the frozen mapping of `invalid_session_config`), and the builder is not consumed. `fixpp_session_open`'s and the validation setter's header comments say so. Nothing returns this cause from `fixpp_engine_start`.

## Python

The flat SWIG functions for all four, with GIL table rows. `Dictionary` in `fixpp_oo.py` is passed through. `EXPECTED_PUBLIC_SURFACE` updated. A refused config raises `fixpp.SessionError` from `session_open`, with `.name` `FIXPP_ERR_THREAD_CONFIG`. `engine_start` holds the GIL across the walks of a FIXT engine; its GIL table row says so.

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

Non-FIXT session: none.

| FIXT session | Validating | Not validating |
|---|---|---|
| admin frame | transport dictionary, S as envelope when it is not S | transport dictionary if any, else S |
| application frame, 1128 resolves, dictionary answers | that dictionary, S as envelope when it is not S | that dictionary's view |
| application frame, no 1128 | negotiated slot | negotiated slot, else S |
| 1128 empty | Reject 373=4, 371=1128 | delivered, S's view |
| 1128 unknown value, or no dictionary | Reject 373=5, 371=1128 | delivered, S's view |
| first 1128 past the header, application frame | Reject 373=14, 371=1128 | delivered, negotiated view; `reify` may read the body's value |
| last MsgType differs from the third field's | Reject 371=35, 373=13 in the header, 373=14 past it | as today: routed by the third field, dispatched by the last |
| application frame before Logon completes | disconnect | disconnect (today) |
| Logon, either role, 1137 absent | Reject 373=1, 371=1137, disconnect | acceptor: same; initiator: recorded, as today |
| Logon, either role, 1137 empty, unparseable or unserviceable | Reject 373=5, 371=1137, disconnect | acceptor: same; initiator: recorded, as today |
| no transport dictionary | `register_session` and `open()` refuse | opens |

## Disclosed, not changed

- A tag an overlay adds to S's trailer is not honoured on an application message answered by another table (L-row).
- A Length+Data pair an overlay adds to S's header is not split by another table. The frame scan and the validator read inside the Data; measured, a `34=7` there is read as MsgSeqNum (L-row, owner sign-off).
- `toApp` views and resend replay use S's table (L-row).
- FIXT 1.1's Message Type Default level is not implemented, as in all three QuickFIX engines (L-row).
- A non-validating FIXT session whose config fails the FIXT check is refused only at `open()` inside the role loop: logged when a logger is set, not returned (L-row, owner sign-off).
- `FIXT11.xml`'s 1128 enum stops at 9, so an admin frame carrying `1128=10` draws 373=5 from a FIXT11 transport dictionary (B-row). An
  application frame with `1128=10` routes to FIX Latest, as QuickFIX/J; QuickFIX C++ rejects `1128=10` on every message.
- Header Required-ness is not enforced on an application message (B-081-1 stays).
