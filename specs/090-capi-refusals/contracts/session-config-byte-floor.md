# Contract: the session-config byte floor (fixpp#452)

**Track**: C-ABI (§1–§7) **and** `[C++ track]` (§8, labelled).
**Marking**: **BREAKING**, under `[const §X.7]`, on the two C-ABI setters.
**Header**: `include/fix/c_api/session.h`. **Definition**: `src/capi/config.cpp`.
**Requirements**: FR-011 (C-ABI); FR-012, FR-013 (`[C++ track]`).
**Criteria**: SC-006, SC-007, SC-008. **Ledger row**: B-452-1.
**Model**: [`../data-model.md`](../data-model.md) §2.3 (S-3), §3.2 (T-2), §4 EC-6/EC-7.

```c
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_comp_ids(
    fixpp_session_config_t* cfg, const char* sender, const char* target);

FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_begin_string(
    fixpp_session_config_t* cfg, const char* begin_string);
```

**Neither signature changes.**

---

## 1. Why this is the breaking change

> **Both setters return `FIXPP_ERR_OK` today for a value containing SOH (`\x01`) or `'='`. After
> this change they return `FIXPP_ERR_CAPI_CONFIG_INVALID` and store nothing.**

The bytes reach the wire **verbatim**. `include/fixpp/wire/writer.hpp` states the mechanism in one
line — *"Raw bytes append: writes `tag=value\x01` into dst"* / *"Returns
`wire_field_value_truncated` if dst is too small (no OOB write)"*. `Writer::append_raw` does
**length checking only: no escaping, no charset validation**.

⚠️ **`begin_string` is genuinely unvalidated today, and that is worth stating because a reader
assumes a BeginString allow-list exists.** It does not. **Re-derive:** `grep -n "begin_string"
src/session/session.cpp` finds only an equality test against the literal `"FIXT.1.1"` (the FIXT
registry gate for `default_appl_ver_id`) and the inbound comparison `hdr.begin_string !=
cfg_.begin_string`. Any byte string is accepted and emitted.

⚠️ **The refusal at the SETTERS — rather than only at `Session::open` — is an owner decision.** The
issue filed that half as *optional* because it is a declared breaking change; the decision to bump
1.6 → 1.7 marked BREAKING removes the objection.

---

## 2. The rule — one definition, stated as a POLICY FLOOR

> **A configured FIX field value MUST NOT contain any byte `< 0x20` (SOH `\x01` included) or `'='`
> (0x3D).**

**The charset is the existing floor, unchanged, and that is deliberate.** It is inherited verbatim
from the shipped credential guard in `Session::open`, written under the comment *"Floor: reject any
byte < 0x20 (incl. SOH \x01) or '=' (0x3D) in username/password when set. Fail-closed at
`open()`-time before any emission."* Widening it — rejecting all C0 bytes, or demanding
ASCII-printable — would refuse values legal today and is a **separate decision with a separate blast
radius**. This change **propagates** the floor; it does not raise it.

⚠️ **The rule must be named and documented as a POLICY FLOOR, not as the FIX grammar.** `=` **can**
appear in a FIX field value as fixpp parses it: the scanner does `++i;  // skip '='` before taking
the value start, so it splits at the **first** `=` only and `372=A=B` is one field whose value is
`A=B`. A name such as `contains_forbidden_config_byte` states what is true;
`is_valid_fix_field_value` does not. It is a conservative compatibility/security floor.

### 2.1 Why the rule needs exactly one definition with external linkage (FR-013)

**The existing guard is a function-local lambda inside `Session::open`'s body** —
`auto is_invalid_cred_byte = [](unsigned char c) noexcept -> bool { … };` — and **a lambda has no
linkage**. Nothing outside that body can call it.

`src/capi/config.cpp` must validate a config **before any `Session` exists**. So once the setters
refuse, one rule with one definition is **not tidiness — it is the only way both surfaces can
enforce the same rule.**

⚠️ **A comp-id validator already exists, in the wrong layer with the wrong charset.**
`src/session/file_store_factory.cpp` validates sender/target comp IDs — *"reject: empty; path
separator '/' (Linux) or '\\' (Windows); NUL byte '\0';"* plus a filename length bound. **SOH and `=`
pass it cleanly.** It is per-store-factory, and `MemoryStoreFactory` — the C-ABI default, installed
by `fixpp_session_config_create` — gets **no comp-id validation at all**. That is the strongest
argument for one universal rule rather than a second point fix. ⚠️ **Unifying the two validators is
explicitly out of scope**: the store-factory one checks a different thing for a different reason.

---

## 3. Pre-conditions

Unchanged, read at source. For `fixpp_session_config_set_comp_ids`:

| # | check | on failure |
|---|---|---|
| 1 | `cfg != nullptr` | `FIXPP_ERR_NULL_HANDLE` |
| 2 | `sender != nullptr && target != nullptr && sender[0] != '\0' && target[0] != '\0'` — *"both required (eager validation)"* | `FIXPP_ERR_CAPI_CONFIG_INVALID` |
| **3 — NEW** | neither `sender` nor `target` contains a byte `< 0x20` or `'='` | `FIXPP_ERR_CAPI_CONFIG_INVALID` |

`fixpp_session_config_set_begin_string` is the same shape with one argument.

⚠️ **The refusal is keyed on BYTE CONTENT.** It is not keyed on lifecycle, not on the config's
prior state, and not on which fields are already set.

---

## 4. Post-conditions

### 4.1 On refusal

| observable | value |
|---|---|
| return code | **exactly** `FIXPP_ERR_CAPI_CONFIG_INVALID` (10) |
| the config | **nothing stored** — the offending value does not reach `cfg->cfg`, and a previously-set value is not overwritten |
| the wire | **no emission** — the refusal is at config time, before any session exists |

⚠️ **"Nothing stored" is half the contract and the half the issue is about.** A cell that checks only
the return code misses the point: the shipped naming precedent for this family is
`FixtCredentials.FQ3{a..e}_…_ReturnsInvalidConfig_NoWireEmit`, and **the suffix is the
requirement**.

⚠️ **For `set_comp_ids`, the refusal is ATOMIC across both arguments.** Today both assignments run
inside one `try`; a bad `target` must not leave a new `sender` stored.

### 4.2 On acceptance

Unchanged: the value is stored, `FIXPP_ERR_OK`.

---

## 5. ⚠️ The code does NOT identify the state — every witness must pin the state

`FIXPP_ERR_CAPI_CONFIG_INVALID` is **already produced by conditions that have nothing to do with
this change**, so a test asserting only the code measures nothing:

| confusable producer | condition |
|---|---|
| the setters' own **pre-existing** guard | a `NULL` or empty argument (§3 check 2) |
| `fixpp_session_open` | called **after** `fixpp_engine_start` — *"Register-before-start (FR-004): a session_open after engine_start is a C-ABI-enforced config error"* |
| a **dead** engine handle | a destroyed/tombstoned handle on several session-surface calls |

⇒ **Assert the exact code from the call under test, in a state where no confusable producer is
live, and pair it with a control showing the injected byte is what produced it.** A non-empty,
non-NULL value that differs from an accepted one **only** by the injected byte is that control.

⚠️ **The lifecycle ordering itself is UNCHANGED by this feature.** It is documented here only as a
confusable state.

---

## 6. What is explicitly UNCHANGED

- **Both signatures**, their export macro and their reentrancy class (`single-thread` per handle).
- **`FIXPP_ERR_NULL_HANDLE` on a null `cfg`**, and `FIXPP_ERR_CAPI_CONFIG_INVALID` on a null or
  empty argument. **The delta is the byte floor alone.**
- **The config-builder's consumption semantics** — *"CONSUMED by `fixpp_session_open` on success …
  on failure untouched (caller still owns it)"*.
- **The register-before-start ordering** (§5).
- **`error::invalid_session_config`'s `translate()` arm is NOT re-pointed.** It is **grouped with
  `error::clock_not_set`** — `case error::invalid_session_config: case error::clock_not_set: return
  FIXPP_ERR_THREAD_CONFIG;` — so re-pointing it would silently move an unrelated error and would be
  a broader C-ABI behaviour change than the three refusals this feature is scoped to.
- **Every other `fixpp_session_config_set_*`** — only these two gain the floor at the C ABI.
- **`include/fix/c_api/error.h`** and the symbol golden — nothing minted, nothing exported.

---

## 7. ⚠️ No cross-surface note, and no limitation row

**The declarations state their own refusal and its code, and say NOTHING about the other surface.**
A claim that a C consumer sees one code where a C++ caller's error maps to another, for the same
byte in the same field, was **DELETED — not narrowed, not hedged, not re-worded** — and the
limitation row `L-452-1` with it.

**Why deletion and not a narrower claim:** a false claim replaced by a narrower claim reproduces the
defect; only deletion closes it. **And the claim is unobservable:** both engine `co_await
…open()` sites **discard** the error (*"Rejecting a pre-session connection: nothing consumes a close
error."* on the accept arm; a bare `co_return;` on the initiator arm), and **`fixpp_session_open`
never calls `Session::open()` at all** — it calls `Engine::register_session`, whose
`FIXPP_ERR_THREAD_CONFIG` comes from a **different condition on a different field**.

⚠️ **Re-derivation recipe, not a result:** re-run `grep -rn "open()" src/ | grep -i "co_await"` and
`grep -rn "\->open()\|\.open()" src/`, and read what each site does with the result. **Do not
re-introduce the note without exhibiting a caller that both propagates that error to a boundary AND
puts it through `translate()`.**

**A false limitation in the live ledger is durable and citable.** B-452-1 is a **behaviour** row;
there is no limitation row **for this refusal** in this delta. (The delta's only limitation rows are the
two out-of-scope residuals — see [`version-and-freeze.md`](./version-and-freeze.md) §4.3.)

---

## 8. `[C++ track]` — the same floor, three more fields, and the predicate's home

⚠️ **Governed by `[const §XVII.1]`. `[const §X.7]` is recorded here as NOT ENGAGED** — none of this
is a C-ABI symbol, none consumes an error-code slot, none moves a version macro, and none is in
`tools/capi_freeze.sha256`'s manifest. Precedent: `.specify/456-table-view-seal.md`.

### 8.1 The population of injectable configured strings, and how it was closed

**The condition:** *a `SessionConfig` member of string type whose bytes reach `Writer::append_raw`
without passing a validator.* Derived by enumerating **every** member of `struct SessionConfig` in
`include/fixpp/session/session_config.hpp` and tracing each to its emission sites in
`src/session/admin_messages.cpp` — **complete rather than sampled**, which is why the sweep can
state its negatives.

| member | emitted verbatim | guarded today |
|---|---|---|
| `sender_comp_id` — tag 49 | yes, every admin builder | **no** |
| `target_comp_id` — tag 56 | yes, every admin builder | **no** |
| `begin_string` — tag 8 | yes, every admin builder | **no** |
| `supported_msg_types[].msg_type` — tag 372 inside the 384 group | yes, at `build_logon` | **no** (§8.2) |
| `username` — tag 553, `password` — tag 554 | yes | **yes**, at `Session::open` |

**Two members the sweep returns as clean, and structurally so:** `default_appl_ver_id` is a
`std::optional<fixpp::dict::application_version>` over an `enum class : std::uint8_t`, reaching the
wire only through a fixed ordinal→literal mapping — **non-injectable by construction**, not by
validation; and the sub-IDs (`SenderSubID`, `TargetSubID`, `OnBehalfOfCompID`, `DeliverToCompID`,
`SenderLocationID`, `TargetLocationID`) **do not exist as config members at all**.

### 8.2 RefMsgType(372) — IN SCOPE at one site, a CHECKED NEGATIVE at the others

**In scope:** the `build_logon` site inside the NoMsgTypes(384) group —
`w.append_raw(372, sv_to_bytes(entry.msg_type))`, where `supported_msg_type::msg_type` is a
`std::string` carrying the comment `// RefMsgType(372)`. Config-fed, verbatim, unvalidated.

⭐ **Its sibling in the same loop already fails closed:** MsgDirection(385) renders through an
exhaustive switch whose comment says an off-enum value *"is runtime-checked and fails closed
(`std::unexpected`) rather than being unrepresentable by construction"*. **The precedent for this
pattern already lives in that file** — applied to the enum and not to the string beside it.

**Checked negative:** the `build_reject` and `build_business_message_reject` sites take
`ref_msg_type` as a **`std::string_view` parameter fed from the inbound frame**, not from config.
**The condition they rest on:** *the value at those sites is produced by a scanner that terminates a
non-Data field at SOH.* ⚠️ An `=` **can** survive into such a value and forges no field **in
fixpp** — what a **counterparty** parser does with a second `=` is **not measured and not claimed**;
it is a recorded residual.

⚠️ **Re-derivation recipe, not a result:** `grep -n "append_raw(372" src/session/admin_messages.cpp`,
then for each hit follow its enclosing builder's parameter back to its call site in
`src/session/session.cpp`. **A new 372 site fed by config would show up as a builder whose argument
is a `cfg_.` member.**

**372 has no C-ABI setter**, so it rides the C++ track only and touches neither the version bump nor
the freeze.

### 8.3 The changed C++ declarations

| declaration | what changes |
|---|---|
| **NEW** — the floor predicate, in a `session`-owned config-validation **leaf header** under `include/fixpp/session/` | A new public declaration: a `[[nodiscard]] constexpr bool` over a `std::string_view`, depending on nothing but `<string_view>`. **Documented as a policy floor, not as the FIX grammar** (§2) |
| `SessionConfig::sender_comp_id`, `::target_comp_id`, `::begin_string` | each gains the floor as a **stated precondition** |
| `supported_msg_type::msg_type` (`include/fixpp/session/session_types.hpp`) | today the declaration carries the bare comment `// RefMsgType(372)`; gains the same floor |
| `Session::open()` | its documented failure conditions gain the three CompID/BeginString fields **and** SupportedMsgTypes' RefMsgType, under `error::invalid_session_config` |

**The existing `Session::open` lambda is REPLACED by a call to the predicate**, so the rule has
exactly one definition (FR-013).

### 8.4 Why the predicate's home is `session` and not `core`

⚠️ **The layering argument that once pointed at `core` is RETRACTED, because it was false against
the clause it cited.** `[arch §2.3]`'s allowed-edge whitelist reads `capi | session, wire,
dictionary, transport, tls, log, otel, tap, core (read-only)` — **`capi → session` is explicitly
permitted** — and `src/capi/config.cpp` already includes `fixpp/session/memory_store.hpp`,
`fixpp/session/memory_store_factory.hpp` and `fixpp/session/security_profile.hpp`, and stores a
`SessionConfig`.

**The real reason is OWNERSHIP.** The rule is not a byte-level primitive: it is a policy about what
may appear in a **configured FIX field value**, whose authority is `SessionConfig` and whose existing
definition lives in `Session::open`. `core` is the protocol-independent leaf; putting a
FIX-configuration policy there would make `core` the owner of a rule it cannot justify. A leaf header
under `include/fixpp/session/` is reachable from `src/capi/config.cpp` by the permitted edge and from
`src/session/session.cpp` directly.

### 8.5 A cost obligation, recorded rather than assumed

The per-call cost of the byte scan on the commit path is **not measured**. The instrument is
`[const §VIII.2]`'s **paired base-vs-candidate run on one runner**, A-B-A-B, min-per-tree. A
config-time scan over a CompID **should** be unmeasurable — *"should be" is not a measurement*.
