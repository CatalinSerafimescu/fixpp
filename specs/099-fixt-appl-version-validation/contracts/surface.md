# Contract: public surface of 099

## C++

| Symbol | Contract |
|---|---|
| `version_registry::transport()`, `fixt_table()`, `transport_table()` | data-model E-1 |
| `dict::session_to_application` | data-model E-2 |
| `Dictionary::as_table_view(fixt_framing)` | data-model E-3 |
| `dictionary_driven_validator(shared_ptr, shared_ptr)` | data-model E-4 |
| `MessageView::resolved_application_version()` | data-model E-5 |
| `SessionConfig`, `Session::open()` comments | state that the dictionary has no engine anchor and is required on the session (#651); the phrase `value_or(engine_anchor)` stays only for the axes that have one |

## C ABI 1.14 (additive)

```c
fixpp_error_t fixpp_session_config_set_default_appl_ver_id(fixpp_session_config_t* cfg, const char* wire_value);
fixpp_error_t fixpp_session_config_set_validate_inbound_messages(fixpp_session_config_t* cfg, int enable);
fixpp_error_t fixpp_engine_config_add_dictionary(fixpp_engine_config_t* cfg, fixpp_dict_t* dict);
fixpp_error_t fixpp_msg_get_resolved_appl_ver_id(const fixpp_msg_t* msg, const char** value_out, size_t* len_out);
```

- `wire_value`: `"2"`…`"9"`, `"10"`. NULL cfg → `FIXPP_ERR_NULL_HANDLE`. NULL, empty or another value → `FIXPP_ERR_CAPI_CONFIG_INVALID`.
- `add_dictionary`: copies the shared pointer; wrong or dead handle → `FIXPP_ERR_INVALID_HANDLE`. Several calls accumulate. Valid until
  `fixpp_engine_create` consumes the builder.
- `get_resolved_appl_ver_id`: static storage, valid for the process. `FIXPP_ERR_TAG_NOT_FOUND` when the message has no resolved version.
  Callable from the receive callback; takes no lock.
- `fixpp_msg_version` is unchanged, including that its `appl_ver_id` reads tag 1137.
- A refused FIXT `open()` surfaces as today's mapping of `invalid_session_config`. The header says so.

## Python

The flat SWIG functions for all four, with GIL table rows. `Dictionary` in `fixpp_oo.py` is passed through. `EXPECTED_PUBLIC_SURFACE` updated.

## TOML

```toml
[dictionary]
kind = "path"
path = "FIX50SP2.xml"
extra_paths = ["FIXT11.xml", "orchestra/OrchestraFIXLatest.xml"]   # optional
```

Each entry is resolved and loaded as `path` is. A failure, a non-array or a non-string entry is a diagnostic at
`dictionary.extra_paths`. `kind` other than `path` is refused as today. A session's `dictionary` default is the `path` file. No overlay
applies to an extra.

## Observable behaviour

Non-FIXT session: none.

| FIXT session | Validating | Not validating |
|---|---|---|
| admin frame | transport dictionary | transport dictionary if any, else S |
| application frame, 1128 resolves, dictionary answers | that dictionary | that dictionary's view |
| application frame, no 1128 | negotiated slot | negotiated slot, else S |
| 1128 empty | Reject 373=4, 371=1128 | delivered, S's view |
| 1128 unknown value, or no dictionary | Reject 373=5, 371=1128 | delivered, S's view |
| first 1128 past the header | Reject 373=14, 371=1128 | delivered, negotiated view; `reify` may read the body's value |
| application frame before Logon completes | disconnect | disconnect (today) |
| initiator Logon reply, 1137 absent, unparseable or unserviceable | Reject 371=1137, disconnect | recorded, as today |
| no transport dictionary | `open()` refuses | opens |

## Disclosed, not changed

- A tag an overlay adds to S's trailer, and a Length+Data pair an overlay adds to S's header, are not honoured on an application message
  answered by another table (L-row).
- `toApp` views and resend replay use S's table (L-row).
- FIXT 1.1's Message Type Default level is not implemented, as in all three QuickFIX engines (L-row).
- `FIXT11.xml`'s 1128 enum stops at 9, so an admin frame carrying `1128=10` draws 373=5 (B-row).
- Header Required-ness is not enforced on an application message (B-081-1 stays).
