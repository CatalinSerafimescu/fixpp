# Data Model — 089 live QuickFIX interop fidelity

**Date**: 2026-09-10 · Derived from [spec.md](./spec.md) + [research.md](./research.md).

The numbered sections below carry this feature's entities; some are new artifacts, the rest extend
existing ones. No count is recorded here — a count in a document is a result nothing re-runs.

```
Conversation script (declarative, digest-pinned)
        │ drives
        ▼
   Cell (8 logical)  ×  Config (4)  ──▶  Run (run_id)  ──contains──▶  Witness
        │                                    ▲                          ▲   ▲
        │ paired by arm                      │ hello carries run_id     │   │
        ▼                                    │ + script digest          │   │
  Validation pair                     Hello record (first line)         │   │
                                                                        │   │
   ┌────────── each side emits BOTH ──────────┐                         │   │
   │  Sent record  (what I handed my builder) ├── compared per direction ┘   │
   │  Readback record (what I parsed)         ├─────────────────────────────┘
   └──────────────────────────────────────────┘
                                                    Witness evidence record
                                                    (accumulates every run;
                                                     gate per (config, arm)
                                                     AND over the union)

   Disk preflight reading ── gates every build, per configuration
```

**The symmetry is the point.** Two comparisons run per business message, one per direction:
`(fixpp sent → peer readback)` and `(peer sent → fixpp readback)`. The second pair did not exist before
Gate A round 1: the model carried *what fixpp built* and *what the peer parsed*, and nothing carried what
the **peer** built — so for the peer→fixpp direction the comparison had no left-hand side.

---

## 1. Hello record — the capability handshake (FR-016a)

Emitted **once**, as the **first line** of **each** stream, before any message is processed. ⚠️ **Both
sides emit one** — the counterparty and fixpp — because both streams must be corroborable and because
fixpp's hello is where the arm attestation lives (§1a).

⚠️ **A hello alone cannot corroborate a `pass`**, and this is the shape of the record's limitation rather
than a defect in it: it is written *before any message is processed*, so a counterparty that starts, writes
its hello and conversates not at all supplies every field a hello-only check inspects. The **terminal
record (§11)** is the other half; FR-014's corroboration requires both.

| Field | Type | Rules |
|---|---|---|
| `type` | string | literal `"hello"` |
| `engine` | string | which counterparty — the two flavours must be distinguishable |
| `engine_version` | string | the QuickFIX version actually running (pinned: cpp `1.16.0`, J `3.0.1`) |
| `readback_protocol` | integer | the record-format version this build emits |
| `dictionary_enabled` | boolean | whether this cell's session has a data dictionary — see *Validation rules* |
| `run_id` | string | the unique run this stream belongs to (FR-013b) |
| `cell_id` | string | the logical cell being executed |
| `config` | string | `normal` · `asan` · `ubsan` · `tsan` |
| `script_digest` | string | content digest of the conversation script driving this run (FR-008c) |
| `counterparty_digest` | string | the image digest this build was pulled from (FR-016b) |

**Where these values come from — the channel, named.** The shim sets them in the counterparty's
environment before launch, extending the `cp_env` dict `launch_counterparty` already builds
(`phase-9-harness/tools/run_interop_cell.py` — it already carries ten `INTEROP_CP_*` knobs and is passed as
`env=cp_env` to both the C++ and the Java branch):

| env key | consumed as | copied verbatim, or recomputed? |
|---|---|---|
| `INTEROP_CP_RUN_ID` | `run_id` | verbatim |
| `INTEROP_CP_CELL_ID` | `cell_id` | verbatim |
| `INTEROP_CP_CONFIG` | `config` | verbatim |
| `INTEROP_CP_IMAGE_DIGEST` | `counterparty_digest` | verbatim |
| `INTEROP_CP_SCRIPT_PATH` | the conversation script the counterparty drives from | — |
| `INTEROP_CP_SCRIPT_DIGEST` | the shim's own digest of that file | **not copied** — see below |

⚠️ **`script_digest` in the hello is RECOMPUTED by the counterparty over the file it actually opened**, and
the shim compares it to `INTEROP_CP_SCRIPT_DIGEST` before launching the gtest. A verbatim copy would prove
only that the counterparty can echo a string. The algorithm is **lowercase-hex SHA-256 over the script
file's bytes**, computed identically by the shim (`hashlib`), the C++ counterparty (OpenSSL, already
linked) and the Java counterparty (`MessageDigest`) — no new dependency on any side, which is what keeps
R-3's Article III/V `PASS` intact. The **shim is the single computer of record**; every other party
recomputes and is checked against it.

#### ⛔ The gtest needs the SAME four values, and `INTEROP_CP_*` does not reach it

The `INTEROP_CP_*` block above is **counterparty-only** — it is `env=cp_env` on the counterparty launch.
But fixpp's own `hello` (above) and its `terminal` (§12) require `run_id`, `cell_id`, `config` and
`script_digest` too, and **`run_id` is minted by the shim** (§11): there is no other source for it inside
the gtest. Without a second channel, fixpp emits a stream that promotion cannot structurally join.

The shim therefore sets a **parallel block on the gtest's environment**, at the point it invokes the
binary named by the cell:

| env key | consumed as | copied verbatim, or recomputed? |
|---|---|---|
| `INTEROP_FIXPP_RUN_ID` | `run_id` | verbatim |
| `INTEROP_FIXPP_CELL_ID` | `cell_id` | verbatim |
| `INTEROP_FIXPP_CONFIG` | `config` | verbatim |
| `INTEROP_FIXPP_ARM` | `arm` (`validation-off` \| `validation-on`) | verbatim |
| `INTEROP_FIXPP_SCRIPT_PATH` | the conversation script fixpp drives from | — |
| `INTEROP_FIXPP_SCRIPT_DIGEST` | the shim's digest of that file | **not copied** — recomputed, as below |
| `INTEROP_FIXPP_READBACK_PATH` | where fixpp writes its own stream | — |

⚠️ **Same rule as the counterparty**: `script_digest` in fixpp's hello is **recomputed by fixpp over the
file it actually opened** and compared against `INTEROP_FIXPP_SCRIPT_DIGEST`. Both sides recomputing
against one shim-held value is what makes a *disagreement about which script ran* detectable at all; a
verbatim echo on either side would prove only that a string can be copied.

⛔ **An ABSENT value is a hard failure, not a defaulted one.** A gtest that cannot read
`INTEROP_FIXPP_RUN_ID` MUST abort before the conversation starts rather than emit a record without it, and
promotion MUST reject a stream whose `hello` or `terminal` omits any join key (`witness-evidence.md` E-1b).
**This is the direction that fails toward green**: an omitted key leaves the comparator with nothing to
disagree with, so the join silently succeeds against nothing — which is why the forced arm for it is an
*omission*, not a mismatch.

⚠️ **R-4's rejection of an env var is thereby reversed and must be recorded as reversed.** R-4 declined it
because it *"needs a new `cp_env` key in `launch_counterparty`"*; that block already exists and already
carries ten keys, so the cost it was priced at was never real. Leaving the rejection on file would leave
`research.md` contradicting FR-013b.

**Validation rules**

- **Absent hello ⇒ the cell FAILS.** Not a skip. A peer that cannot announce itself is a stale peer.
- `readback_protocol` **older than the cell requires ⇒ FAIL.** Newer is permitted only if the format is
  backward-compatible; the contract states which changes are.
- ⚠️ **This is distinct from the existing availability probe.** `probe_counterparty()`
  (`tests/interop/support/counterparty_probe.hpp:178`) answers *is a peer listening* and yields a
  **skip**. The hello answers *is it the right peer* and yields a **failure**. Collapsing the two
  reintroduces exactly the silence this feature exists to remove.
- ⚠️ `dictionary_enabled == false` on a cell whose witnesses include repeating groups ⇒ **FAIL**. Under
  `UseDataDictionary=N` both engines flatten groups (R-5), so such a cell would report a plausible,
  group-free field set and compare clean against a group-free intent — a false green. **Which cells those
  are is now decidable**: FR-008b requires the declarative script to carry at least one multi-instance
  nested group, so the rule has a determinate population. It did not before — a script declaring no field
  content left this rule undecidable.
- **The hello is the join key.** `run_id` + `script_digest` + `config` + `counterparty_digest` are read
  from here, and an evidence row's copies must match them. A row pointing at a **stale** run directory
  otherwise satisfies every evidence field — R-4 closes that hazard for records inside one stream (truncate
  mode) and leaves it open one level up, at the pointer.
- ⚠️ **The gate that reads this record runs shim-side, before the conversation** (FR-024): the refusal is
  *"do not run this cell"*, and at that moment only the shim is running. Its failure text MUST NOT use the
  word `unavailable:` — `parse_gtest_status` greps that token out of gtest stdout and converts it to
  `skip:`, which would turn a stale-peer **failure** into a skip.

---

## 1a. fixpp's hello — the arm attestation (FR-011a)

fixpp's stream carries a `hello` with the fields of §1 that apply to it (`type`, `run_id`, `cell_id`,
`config`, `script_digest`, `arm`) plus the two that exist only here:

| Field | Type | Rules |
|---|---|---|
| `has_validator` | boolean | **`Session::has_validator_for_test()`**, read from the live session after `open()` |
| `dictionary_digest` | string | lowercase-hex SHA-256 of the dictionary XML the session loaded |

**Validation rules**

- ⚠️ **`has_validator` MUST come from the live session, never from the `SessionConfig` the driver filled
  in.** The accessor exists today (`include/fixpp/session/session.hpp`, `has_validator_for_test()`) and
  returns true iff the inbound validator was constructed at `open()` — i.e. iff
  `validate_inbound_messages == true` **and** the dictionary is non-null. A value read off the config
  struct is the harness asserting its own intent, which is precisely the vacuity FR-011a closes.
- **`has_validator` MUST equal `arm == "validation-on"`.** A mismatch FAILS the run. Without this the whole
  of US4 can go vacuously green: a validation-on arm launched with the flag false produces trivially
  identical accepted sets and satisfies SC-004 and US4 AC-1/AC-3 with nothing able to see it.
- ⚠️ **This is what FR-011 cannot do.** Per FR-001/R-5a the production dictionary is loaded on **both**
  arms, so *"a production dictionary is loaded"* is true in the validation-off arm too — a predicate whose
  population equals its complement. `has_validator` is false in the off arm by construction.

---

## 2. Readback record — what an engine parsed (FR-003)

One record per application message an engine receives. **Emitted by both sides**: the counterparty for
what it received from fixpp, and fixpp for what its **typed read tier decoded** from the peer. Symmetric
with §3.

| Field | Type | Rules |
|---|---|---|
| `type` | string | literal `"readback"` |
| `msg_type` | string | FIX `MsgType(35)` as received |
| `seq_num` | integer | `MsgSeqNum(34)` — part of the correlation key (FR-005) |
| `direction` | enum | which way the message travelled |
| `occurrence` | integer | 0-based ordinal within `(seq_num, direction)` on this stream — completes the key |
| `poss_dup` | boolean | `PossDupFlag(43)` as observed. **Diagnostic only — not a key component** |
| `fields` | list of *field entries* | **every** body field parsed, each carrying the **raw** parsed spelling; header/trailer excluded |
| `typed_reads` | list of *typed entries* | the subset read through the engine's **typed accessors** (FR-003b) |

⚠️ **A readback record carries NO `script_step_id`.** No step identifier is on the wire and the receiver
does not hold the sender's script, so it cannot stamp one; it is **inherited from the paired `sent` record
after correlation** on `(seq_num, direction, occurrence)`. Requiring the receiver to write it was a
requirement no side could satisfy.

**Typed entry** — a field entry plus the two columns that make the typed tier observable:

| Field | Type | Rules |
|---|---|---|
| `path` | string | as for a field entry |
| `fix_type` | string | the field's FIX datatype **as the emitter resolved it from the dictionary loaded for that cell** (`PRICE`, `QTY`, `UTCTIMESTAMP`, `CHAR`, …). ⚠️ **The script does NOT declare it** — see below |
| `value` | string | the accessor-resolved value rendered in **the contract's canonical form for that `fix_type`** — for `PRICE`/`QTY`, the shortest decimal spelling with no trailing zeros; for `UTCTIMESTAMP`, `YYYYMMDD-HH:MM:SS.sss` |

⚠️ **`fix_type` is the observable that makes the FR-003b bypass arm discriminating**, and it works because
of where the value has to come from: a generic enumeration walks the message's own field map and performs
**no dictionary lookup at all**, so a bypass produces entries with `fix_type` absent or empty. The script
deliberately does not carry `fix_type` and the comparator resolves the expected value from its **own** copy
of `FIX44.xml`, so a bypass cannot fabricate it either — it has no source to copy from.

⚠️ **A decimal-spelling observable was tried and REJECTED on verified evidence, and the rejection is
recorded so it is not re-proposed.** The idea was to seed `Price(44)` with the wire spelling `100.1000` and
discriminate the raw entry (`100.1000`) from the typed one (`100.1`). It requires the trailing zeros to
reach the wire — and fixpp's decimal writer **strips trailing fractional zeros**
(`src/core/decimal.cpp`, `decimal_traits<pod_decimal>::to_chars`, step *AC-S4: strip trailing fractional
zeros*). Both paths would have read `100.1`, and the arm would have been **inert while looking green**.

The canonical rendering of `value` remains a contract obligation on the emitter — it is what keeps the two
emitters byte-identical for C-7 — it is simply not what the bypass arm discriminates on.

**Field entry**

| Field | Type | Rules |
|---|---|---|
| `path` | string | `"40"` for a top-level field; `"453[0].448"` for a group member |
| `value` | string | the value as decoded, escaped per the contract |
| `value_b64` | string | **alternative to `value`** — base64 of the raw bytes, when they are not valid UTF-8 (FR-025). Exactly one of `value` / `value_b64` is present |

**Validation rules**

- **Body only, structurally determined.** A field is excluded iff the receiving engine classifies it as
  header or trailer **under the dictionary loaded for that cell**; that membership is
  *built-in list ∪ dictionary-declared header* on both engines. `8, 9, 35, 34, 49, 56, 52, 10` are an
  illustrative subset, **not the rule**.
- ⚠️ **The body set SHRINKS when US1 turns the dictionary on**, so FR-006's exact-set equality ranges over
  a *different set* before and after the flip this feature mandates. `FIX44.xml`'s `<header>` block
  declares fields that are **not** in QuickFIX-cpp's built-in list — `SecureData(91)` and the `NoHops`
  members among them — so under `UseDataDictionary=N` they are **body** and appear in `fields`, and under
  `=Y` they are **header** and are excluded. Any golden or expected set fixed against `=N` behaviour is
  wrong after US1, and FR-019's re-capture obligation must be read as covering set **membership**, not
  only serialization and ordering.
- ⚠️ **`occurrence`, not `poss_dup`, disambiguates a repeat.** `PossDupFlag(43)` separates an original from
  a replay but not replay *n* from replay *n+1*, and `L-021-3` records that QuickFIX-cpp **strips** it from
  the wire — so on those combos it carries no information at all. It stays in the record as a diagnostic.
- **Completeness is the point.** The record carries every body field the peer parsed, not a selected
  subset; FR-006's set equality is meaningless over a filtered set.
- ⚠️ **Group instances must be present, not merely their count.** Trap 1 (C++): the count field lives in
  `m_fields` while instances live in `m_groups`, so a naive scalar-only walk emits `453=2` and drops the
  instances silently. A record containing a `NoXxx` count but no member at that path is **malformed**,
  and the contract's witness must assert that.
- ⚠️ **Enumeration must not mutate the message.** Trap 2 (Java): `getGroups(int)` is `computeIfAbsent`,
  so probing it per tag inserts empty lists into the message under test. Drive from `groupKeyIterator()`.
- `path` uniqueness: two entries may not share a `path` within one record.

---

## 3. Sent record — what an engine meant to send (FR-003a)

Produced by **each** side when a message is built, from the values handed to **its own** builder. This is
the comparison's left-hand side in whichever direction the message travels. fixpp's sent record is what
this document previously called the *intent record*; the counterparty's sent record is new, and it is what
makes the peer→fixpp direction comparable at all.

| Field | Type | Rules |
|---|---|---|
| `type` | string | literal `"sent"` |
| `msg_type` | string | must equal the paired readback's |
| `seq_num` | integer | correlation key with `direction` and `occurrence` |
| `direction` | enum | as above |
| `occurrence` | integer | as above |
| `script_step_id` | string | the conversation-script step this message realises |
| `fields` | list of *field entries* | the declared field set, same `path` grammar and same escaping rule |

**Validation rules**

- Same `path` grammar, same **format**, same correlation key and same escaping as the readback record — or
  set equality is comparing incomparable things, and one comparator cannot serve both.
- ⚠️ **Same format, NOT the same file.** Each process writes its own stream
  (`counterparty-readback.jsonl`, `fixpp-readback.jsonl`), because both are opened in **truncate** mode and
  two processes truncating one path means whichever opens second wipes the other's records — the
  stale/absent-record hazard truncate mode exists to close, reproduced one level up. The comparator reads
  both and pairs on `(seq_num, direction, occurrence)`; see `contracts/readback-jsonl.md` § Transport.
- **`fields` are derived from the builder inputs, never re-read from the serialized frame.** Re-reading
  would compare an engine's writer to its own reader and prove nothing about the other side. FR-018's first
  spurious-hit arm forces exactly that mis-derivation and requires RED — with a post-capture frame mutation
  behind it, because without one the two derivations are indistinguishable.
- ⚠️ **TWO STAGES, and this replaces *"emitted before transmission … from builder inputs"*.** That single
  emission point does not exist on either side: no application holds both the body intent and
  `MsgSeqNum(34)` at one moment.

  | Stage | Captured | The seam — **named per side** |
  |---|---|---|
  | 1 — body intent | the `fields` set, from the builder inputs, immutable thereafter | the call site that builds the message, before it is handed to the session. Keyed by an emitter-local token |
  | 2 — header identity | `seq_num` and `direction` only | **counterparties**: `Application::toApp` / `toAdmin`. Both engines assign 34 *before* the callback and serialize *after* it — QuickFIX-cpp's `Session::sendRaw` calls `fill(header)` (which sets `MsgSeqNum`) then `m_application.toApp(message, m_sessionID)` then `message.toString(...)`; QuickFIX-J's `Session.sendRaw` calls `initializeHeader(header)` then the admin/app branch's `application.toApp`. So the header object carries 34 with the frame not yet serialized. **fixpp**: the `Application::toApp` callback (`src/session/session.cpp` — invoked after the complete frame is built and before `store_then_emit`), reading **tag 34 only** from its `MessageView` |

  The record is written when stage 2 supplies the key. ⚠️ **Stage 2 MUST NOT re-read any body field.**
  fixpp's `MessageView` at that seam *is* frame-derived, which is exactly why the restriction is on **what**
  is read there, not on **where**: header identity is permitted, body intent is not. C-8 says so.
- ⚠️ **This is the emission point that moved, and nothing else did.** The user's recorded correlation-key
  clarification — *"`MsgSeqNum(34)` + direction, using data already on the wire; nothing is injected"* — is
  untouched and remains correct: 34 is on the wire, both engines expose it before serialization, and no
  injection is required. What was wrong was this document's own *"emitted before transmission"* design
  statement.
- A message that fails to send still leaves its left-hand side: the stage-1 capture is written with the key
  it has, or recorded as unsent, never silently dropped.
- ⚠️ **The peer's sent record cannot be replaced by a script-declared expectation.** The counterparty
  generates its outbound identifiers at run time — `interop_counterparty_main.cpp` builds the
  ExecutionReport with `const int seq = ++id_counter_;` and `"ORD" + std::to_string(seq)` /
  `"EXC" + std::to_string(seq)`, echoing `Symbol`/`Side`/`CumQty`/`AvgPx` from the inbound NOS — so
  `OrderID`/`ExecID` are knowable only inside the peer. FR-008a's declarative script closes the
  fixpp→peer direction; only this record closes the other one.

---

## 4. Witness — one (message × direction × occurrence) fidelity result

The unit a catalogue row cites (FR-015a).

| Field | Type | Rules |
|---|---|---|
| `witness_id` | string | stable and derivable from the conversation script |
| `run_id` | string | the **run** it belongs to — the join to §1's stream |
| `authoritative` | boolean | true for the one designated run of this `(cell_id, config)` slot; retries are `false` and are excluded from every gate |
| `combo_id` | string | `C1` · `C2` · `C3` · `C4` — the role × flavour axis (spec § *Conversation census*) |
| `cell_id` | string | the **logical cell**, `≡ (combo_id, arm)`; 8-valued |
| `config` | string | `normal` · `asan` · `ubsan` · `tsan` — **required**, see below |
| `arm` | enum | `validation-off` · `validation-on` |
| `script_step_id` | string | the conversation-script step this witness covers |
| `msg_type` | string | |
| `direction` | enum | |
| `occurrence` | integer | ordinal within `(seq_num, direction)` |
| `verdict` | enum | `pass` · `fail` · `skip` — and `skip` may not be produced by a missing record |
| `mismatch` | list | on failure: the offending paths, each classified |

**Three identities, kept distinct (FR-015c).** Conflating them is what made the previous single "key"
both unsatisfiable as an equality and blind on the role × flavour axis:

| # | Identity | Value |
|---|---|---|
| 1 | observed-row uniqueness | `(run_id, cell_id, config, arm, script_step_id, direction, occurrence)` |
| 2 | **stable completeness key** | **`(cell_id, script_step_id, direction, occurrence)`** |
| 3 | cross-config projection `π` | an observed row reduced to identity 2 |

⚠️ **Identity 2 retains the role × flavour axis, and that is the point.** `cell_id ≡ (combo_id, arm)`, so
dropping `run_id` and `config` still leaves the combo visible. The obvious alternative remainder —
`(arm, script_step_id, direction, occurrence)` — **collapses all four combos into one set**, under which a
configuration that ran one of its four combos projects to the same set as one that ran all four. That is
the config-blindness this document already warns about, reproduced one axis over, inside the key written to
close it.

⚠️ **The equality runs across `config` at fixed everything else, never across `arm`.** `arm` lives inside
`cell_id`, so a cross-arm equality is unsatisfiable by construction; cross-arm agreement is §10's property
over a different entity.

⚠️ **Exactly one authoritative run per `(cell_id, config)` slot.** `(cell_id, config, run_id)` uniqueness
alone permits unbounded rows per slot — every retry mints a new `run_id` — and because `π` drops `run_id`
those duplicates *collapse*, so a retry is **invisible** rather than loud, and a failed run and a retried
pass can both sit in the record with no rule saying which governs. That fails toward green.

⚠️ **`config` is load-bearing, not decoration.** Without it the completeness gate is structurally blind to
the axis FR-021/SC-010/SC-011 exist to protect: the gate runs exact-set equality over the **union**, so a
configuration producing **zero** witnesses leaves the union unchanged and the gate passes over a matrix
that ran a fraction of itself. And `cell_id` cannot carry the config in its place — the id space is
8-valued by FR-015a and is asserted globally unique by the shipped schema check.

**Validation rules — the comparison (FR-006)**

Exact set equality between the **sent** record and the paired **readback** record — in *both* directions,
using the same comparator — failing on **any** of three, each named distinctly:

| Class | Meaning |
|---|---|
| `value_mismatch` | both declare the path; values differ |
| `missing` | intent declares the path; the peer did not report it |
| `spurious` | the peer reported a path the intent does not declare |

- ⚠️ **A subset comparison is prohibited.** It cannot see `spurious`, which is the class that catches a
  builder emitting a field it never intended.
- ⚠️ **No readback record ⇒ `fail`, never `pass` and never `skip`** (FR-016c). The comparator asserts a
  record was received *before* comparing contents; an empty field set must not compare equal to an empty
  intent by accident.
- Value comparison is on **decoded values**, not rendered text — decimals compare numerically.

---

## 5. Cell row — one (logical cell × config × run) result (FR-015a, FR-013a)

Extends the existing `cell_results.yaml` row.

**Existing required fields** (`tests/interop/cell_results_schema_check_test.py`, `REQUIRED_FIELDS`): `id`,
`config`, `kind`, `status`, `matrix_disposition`, `spec_ref`. Conditionals: `deferred_reason`, `priority`,
`tracking_issue_state`.

**New — identity and run evidence (FR-013, FR-013a)**

| Field | Type | Rules |
|---|---|---|
| `cell_id` | string | the **logical** cell — 8-valued, stable across configs and runs |
| `run_id` | string | uniquely identifies the run that produced this row |
| `run_timestamp` | timestamp | when it ran |
| `script_digest` | string | the conversation script that drove it (FR-008c) |
| `counterparty_flavour` | string | which engine |
| `counterparty_version` | string | from the hello record, **not** from a config file |
| `counterparty_digest` | string | the image digest actually used (FR-016b) |
| `ledger_ref` | `(cell_id, config)` | names the run-ledger entry (§11) that corroborates this row |

⚠️ **There is no `artifact_path` on a committed row.** An absolute path into a run directory is
machine-local, and the check that reads this manifest is a **ctest** three CI tiers run on hosted runners
that hold no run artifacts. See §11 and FR-014.

**Validation rules**

- **Row identity is `(cell_id, config)`** — the 32-slot inventory, one committed row per slot, retries
  never committed. The shipped `id` field is **retained** and derived as `"<cell_id>@<config>"`, so
  `test_ids_unique` keeps a well-defined subject and **does not need to be replaced**. ⚠️ An earlier
  reading of this section claimed it did, on the assumption that 8 ids had to serve 32 rows; separating the
  ledger from the manifest removes that assumption.
- **New fields are required CONDITIONALLY, on `kind: conversation`.** Extending the global
  `REQUIRED_FIELDS` would break the **59** `status: pass` rows already committed — none of which has an 089
  run artifact — colliding head-on with FR-020.
- **`status: pass` requires a ledger entry** (§11) whose `terminal_state` is `completed` and whose
  `witness_count` equals the census figure for that slot. The check **opens nothing**: corroboration
  against the stream happens at promotion time, on the machine that ran the cell (FR-014b), and what CI
  re-checks is the machine-independent record of it.
- `counterparty_version` comes from the **hello record** — a value read from a config file describes what
  was *asked for*, not what *ran*, and this feature exists because those diverge.
- Cardinality: **8 logical cells** = 4 role×flavour × 2 validation arms, executed **per config**; 8 × 4 =
  **32 rows**. FR-021 forbids folding a config into another's row.
- ⚠️ A run that died on `ENOSPC` is recorded as **`error:enospc`** — never `pass`, `skip`, `n/a` or `fail`
  (FR-014a). The vocabulary is closed by a shipped assertion (`{pass, fail, skip, known-limitation, n/a}`,
  with `n/a` bound to a `deferred:*` disposition and `known-limitation:*` bound to a tracking issue), so
  with no new state the implementer reaches for `fail` and an infrastructure abort becomes
  indistinguishable from a fidelity defect. Adding the state is a **required edit** to that check.
- Artifact finalisation is **atomic**, and a terminal run record is written whatever the outcome.

---

## 6. Witness evidence record — the second level (FR-015b)

A separate artifact accumulating witness rows across all cells and all configs.

**Validation rules**

- The expected set per configuration is the **census's 100 completeness keys** (spec § *Conversation
  census*), which the expansion rule derives from `business steps × applicable combos × declared
  occurrences × 2 arms`. The **`config` axis multiplies the runs, not the key** — `config` is deliberately
  *not* in the completeness key, because it is the axis the equality is taken **over**.
- The gate compares **π(authoritative rows of config `c`)** against **π(authoritative rows of config `c'`)**
  for every ordered pair of the four configs, and against the census set — **exact set equality**, not
  containment. Only rows with `authoritative: true` participate. It *also* runs over the union after the
  last configuration. ⚠️ The union pass alone is not a check: with
  no `config` on the row, a configuration producing **zero** witnesses leaves the union unchanged and
  exact-set equality passes. SC-011's *"the same witness set as the `normal` arm"* is unimplementable
  without partitioning by config.
- ⚠️ **The expected witness set is DERIVED from the conversation script, never hand-listed.** A
  hand-maintained list drifts toward whatever is currently produced, which makes the gate agree with
  reality by construction — a check that cannot fail.
- ⚠️ **…and script-derivation alone is self-consistent under step deletion**, which is the other horn of
  the same problem: one source drives both production and expectation, so deleting a business step removes
  the witness *and* its expectation and the gate stays green over a conversation that no longer covers
  that message. The script-derived set is therefore asserted **exactly equal to an independent declarative
  census pinned to the spec** (FR-015d) — the table in spec § *Conversation census*, enumerated over
  **business steps × applicable combos × declared occurrences × 2 arms**, ⚠️ in exactly the unit of §4's
  **completeness key**. A census
  counted per *message type* × *both directions* is a different unit and cannot be compared: it undercounts
  repeated steps and replays, and overcounts directions that do not apply in a given combo. Two sources
  that must agree cannot both drift silently. This is **not** a return to a
  hand-maintained expected list: the per-run projection stays script-derived; the census is the
  disagreeing second opinion.
- ⚠️ **Accumulated, not rewritten per config.** The four configs run in sequence with reclaim between them
  (plan.md § Matrix sequencing); a later config overwriting the record erases the earlier ones' rows, and
  the completeness gate would then pass over what survived.

---

## 7. Conversation script step — declarative (FR-008a)

| Field | Type | Rules |
|---|---|---|
| `step_id` | string | stable identifier; the `script_step_id` every **`sent`** record and witness carries (a readback inherits it after correlation — §2) |
| `step` | integer | order within the conversation |
| `phase` | enum | `logon` · `admin` · `business` · `logout` |
| `msg_type` | string | |
| `originator` | enum | which side sends |
| `expects_witness` | boolean | business messages yes; admin exchanges assert session behaviour |
| `intent_fields` | list of *field entries* | **concrete declared values**, same `path` grammar as §2/§3 — including discriminating decimal and timestamp values, and at least one multi-instance **nested** group across the script as a whole (FR-008b) |
| `typed_reads` | list of paths | which fields the receiver reads via **typed accessors** (FR-003b); the rest are covered by generic enumeration |
| `depends_on` | list of `step_id` | explicit request/reply dependency, e.g. the ExecutionReport that answers a NewOrderSingle |
| `runtime_generated` | list of paths | fields whose values the **originator** produces at run time (e.g. the peer's `OrderID`/`ExecID`). Compared against the originator's **sent** record (§3), never against a declared value |

The script is the **source of the expected witness set**. One script, parameterised by role×flavour, so
the four combinations cannot drift apart. It is content-addressed and its digest is pinned into every run
(FR-008c) and into every hello record (§1).

⚠️ **Declaring only `{step, phase, msg_type, originator, expects_witness}` is not enough**, and the
consequences are concrete rather than stylistic: a conforming implementation would ship scalar-only
minimal messages that satisfy the five-message census, which makes `readback-jsonl.md` C-4 and C-5
(both *group* traps, both explicitly required to carry their own witness) **unsatisfiable**, makes §1's
`dictionary_enabled` rule undecidable, and leaves US1's dictionary flip changing nothing observable —
since a conversation with no groups is parsed identically with and without a dictionary.

⚠️ **`runtime_generated` is the seam that keeps the script honest.** A field the peer generates cannot be
declared, and pretending otherwise would either force a fake expectation into the script or quietly drop
the field from the comparison. Naming it explicitly routes it to §3 instead.

---

## 8. Disk preflight reading

| Field | Type | Rules |
|---|---|---|
| `configuration` | string | which configuration is about to be built — the readings are **per configuration** |
| `build_mount_free` | bytes | `df` on the build mount |
| `host_mount_free` | bytes | `df` on the Windows host mount; **N/A off WSL** |
| `is_wsl` | boolean | `microsoft` in `/proc/version` |
| `required_internal_free` | bytes | how much must be free **inside the VHD** for this configuration — bounds *total data resident at once*. Measured, with the date |
| `required_host_growth` | bytes | how much the VHD may still need to **grow** for this configuration — bounds *net new* host allocation. Measured, with the date |
| `failing_predicate` | enum | `none` · `internal` · `host` · `both` — which predicate refused |
| `verdict` | enum | `proceed` · `reclaim-first` · `stop` |

**Validation rules**

- ⚠️ **Two predicates, two ceilings, evaluated INDEPENDENTLY.** `required_internal_free` is compared to
  `build_mount_free`; `required_host_growth` is compared to `host_mount_free`. Neither threshold may be
  applied to the other reading.

  This replaces the previous *"gate on the minimum of the two readings against one threshold"* rule, whose
  fail-open was not where it looked. `min(a, b) ≥ T` is **stricter** than either alone, so it fails toward
  a false RED — not the dangerous direction. The dangerous direction is that the single threshold was
  **derived from the host delta** (R-1) and then **applied to the build-mount reading**, which bounds total
  resident footprint — 34 GiB for the ASan configuration. A 4 GiB host-delta threshold would authorise a
  34 GiB build whenever the VHD had 4 GiB free: the exact ENOSPC the gate exists to prevent, reproduced by
  the gate's own arithmetic. **One threshold applied to two ceilings under-checks whichever ceiling is
  larger.**
- **`failing_predicate` is named in the output.** A verdict that does not say which ceiling refused cannot
  route the operator correctly.
- ⚠️ **`reclaim-first` is offered ONLY for an internal-space failure.** Deleting inside WSL frees blocks
  for reuse within the VHD and returns nothing to the host (plan.md § *The mechanism*), so prescribing
  reclaim for a **host** failure prescribes a remedy the plan's own mechanism section proves cannot work —
  and having done it, the readings are unchanged and the operator loops.
- ⚠️ **On WSL, an unreadable or absent host mount ⇒ `stop`.** "Could not read the binding constraint"
  must never resolve to `proceed`. This is the arm most likely to be written backwards, and it needs its
  own witness.
- Off WSL (a CI runner, where the host mount does not exist), `host_mount_free` is **not applicable**, the
  `required_host_growth` predicate is skipped, and `required_internal_free` alone governs — a distinct case
  from *unreadable*, and the two must not share a code path.
- ⚠️ **An unset or unparseable threshold must not default to 0.** A zero threshold makes `proceed` true for
  a reason unrelated to free space, which is a spurious hit; FR-018 requires an arm forcing it.
- ⚠️ **The host reading must not fall back to the build reading.** Resolving the same mount twice makes the
  host predicate vacuously true; FR-018 requires an arm forcing it.
- Both readings, both required values with their measurement dates, `failing_predicate` and the verdict are
  printed on **every** invocation, pass or fail.
- `verdict: stop` exits non-zero. It must not warn-and-continue and must not exit 0.

---

## 9. Run — one execution of one cell under one configuration

| Field | Type | Rules |
|---|---|---|
| `run_id` | string | unique; minted by the shim before the counterparty is launched |
| `cell_id` | string | the logical cell |
| `config` | string | `normal` · `asan` · `ubsan` · `tsan` |
| `arm` | enum | `validation-off` · `validation-on` |
| `script_digest` | string | the script that drove it |
| `kind` | enum | `conformance` · `validator-positive-control` (FR-010a) |
| `expected_verdict` | enum | positive controls only — `diverged` |
| `authoritative` | boolean | exactly one `conformance` run per `(cell_id, config)` slot carries `true` |
| `terminal_state` | enum | `completed` · `aborted` · `error:enospc` |

**Validation rules**

- The `run_id` is written into **both** streams' hello records (§1, §1a) and into every cell row and witness
  row it produced, so a row joins to its stream **structurally** rather than by a hand-written path.
- A **terminal record (§12) is written to BOTH streams whatever the outcome**, including an abort. A stream
  with no terminal record is an incomplete run, not a passing one.
- ⚠️ **A `validator-positive-control` run sits OUTSIDE the 32-slot inventory** and never occupies a slot.
  FR-010a's divergence probe is a separate execution precisely so that seeding a message the dictionary
  should reject does not make FR-010's identical-accepted-sets assertion fail on all 32 conformance runs by
  construction.
- ⚠️ **Exactly one authoritative `conformance` run per slot.** A retry mints a new `run_id`; its rows carry
  `authoritative: false` and enter no gate. Without this rule the completeness projection — which drops
  `run_id` — collapses the duplicates, so a retry is invisible and a failed run can sit beside a retried
  pass with nothing saying which governs.

---

## 10. Validation pair — the cross-arm comparison (FR-012a)

The entity SC-004 and FR-010a/FR-012 range over. Nothing in the model carried it before Gate A round 1:
the arm axis appeared in the cell cardinality (8 = 4 × 2) and nowhere else.

| Field | Type | Rules |
|---|---|---|
| `pair_id` | string | identifies the pair |
| `cell_pair` | (cell_id, cell_id) | the validation-off and validation-on cells being compared |
| `config` | string | the configuration both arms ran under — a pair is within one config |
| `script_digest` | string | both arms must have run the same script |
| **`off_run_id`** | string | ⭐ the **run** `accepted_off` was read from |
| **`on_run_id`** | string | ⭐ the **run** `accepted_on` was read from |
| **`kind`** | enum | `conformance` · `positive-control` — matches the `kind` of both referenced runs |
| **`expected_verdict`** | enum \| absent | required when `kind: positive-control`; absent for `conformance` |
| `accepted_off` | set of `script_step_id` | messages the validation-off arm accepted |
| `accepted_on` | set of `script_step_id` | messages the validation-on arm accepted |
| `dispositions` | list | per message, each arm's validator disposition — accepted, or rejected with the objection |
| `verdict` | enum | `identical` · `diverged` |

**Validation rules**

- `verdict: diverged` MUST name **the message and the validator's objection** (FR-012). The witness
  `mismatch` vocabulary is field-level (`value_mismatch` / `missing` / `spurious`) and cannot express
  *"the on arm rejected a message the off arm accepted"*.
- ⛔ **`off_run_id` and `on_run_id` MUST name two DISTINCT runs whose arms are opposite.** Both referenced
  runs must match the pair's `cell_pair`, `config`, `script_digest` and `kind`, and the run named by
  `off_run_id` must have recorded `has_validator: false` while `on_run_id`'s recorded `true` (E-6).
  ⚠️ **Without this the pair is satisfiable by DEGENERATE CONSTRUCTION**: nothing otherwise forbids
  `accepted_off` and `accepted_on` being read from *one* execution, which yields `identical` across all 32
  slots with the two arms never actually compared — a green that means only that a set equals itself. That
  is a **spurious hit**, and it is not caught by any arm that forces a *divergence*, because the degenerate
  pair reports the same verdict a correct one does.
- `expected_verdict` is meaningful only for `kind: positive-control`; such pairs assert `diverged` and
  **remain outside the 32 conformance slots**, so a deliberately-diverging control can never be counted as
  a conformance result.
- ⚠️ **`identical` is not by itself evidence.** A validator that never runs produces `identical` by
  construction, and FR-011's "a production dictionary is loaded" establishes presence, not execution. The
  pair is admissible only alongside FR-010a's **divergence probe**: a seeded message the dictionary should
  reject must make this entity report `diverged`, with the objection recorded.

---

## 11. Run ledger — the committed, machine-independent record of the runs (FR-014b)

A `runs:` section of the same witness-evidence artifact (§6), not a third file. One entry per
**authoritative** run.

| Field | Type | Rules |
|---|---|---|
| `cell_id` · `config` | string | the slot; the set of slots MUST equal the 32-slot inventory exactly |
| `run_id` | string | the authoritative run for that slot |
| `authoritative` | boolean | `true` here by construction; retries are recorded with `false` and enter no gate |
| `kind` | enum | `conformance` · `validator-positive-control` (FR-010a). Only `conformance` runs occupy a slot |
| `expected_verdict` | enum | positive controls only — `diverged` |
| `run_timestamp` | timestamp | |
| `counterparty_flavour` · `counterparty_version` · `counterparty_digest` | string | version from the **hello**, never from a config file |
| `script_digest` | string | the shim's value, having matched both sides' recomputation (§1) |
| `has_validator` | boolean | fixpp's arm attestation (§1a); MUST equal `arm == "validation-on"` |
| `terminal_state` | enum | `completed` · `aborted` · `error:enospc` — from **both** streams' terminal records (§12) |
| `witness_count` | integer | rows this run produced; MUST equal the census figure for the slot |
| `evidence_relpath` | string | **relative** to `$FIXPP_INTEROP_EVIDENCE_ROOT`. ⛔ never an absolute path |
| `evidence_digest` | string | lowercase-hex SHA-256 over the persisted bundle, computed at promotion |

**Validation rules**

- ⚠️ **This exists because one artifact cannot be both a committed CI-checked manifest and a machine-local
  run ledger.** `cell_results_schema_check_test.py` is a ctest (`tests/interop/CMakeLists.txt:459`)
  provisioned in `tier1.yml`, `tier2.yml` and `tier3-libcxx.yml` on hosted runners that hold **no** run
  artifacts; every restatement that keeps them as one artifact fails on those runners for every 089 row.
- The **persisted evidence root** is `$FIXPP_INTEROP_EVIDENCE_ROOT`, defaulting to
  `/mnt/wsl/fixppbuild/interop-evidence/` — on `/dev/sde`, the separate VHD already holding `CCACHE_DIR`,
  whose backing file is **not** on `E:`. It is therefore outside every build tree (so reclaim cannot destroy
  it) and outside both disk predicates (`contracts/disk-preflight.md` D-11). Re-derive its free space with
  `df -k /mnt/wsl/fixppbuild`; no reading is recorded here, because it moves.
- The **promotion command** is named in FR-014b. Nothing else may write a `status: pass` conversation row.

---

## 12. Terminal record — the last line of each stream (FR-014a)

| Field | Type | Rules |
|---|---|---|
| `type` | string | literal `"terminal"` |
| `run_id` · `cell_id` · `config` · `script_digest` | string | the same join keys as the `hello` |
| `terminal_state` | enum | `completed` · `aborted` · `error:enospc` |
| `sent_count` · `readback_count` | integer | records this writer emitted |

**Validation rules**

- **Written by BOTH streams, whatever the outcome**, and written last. A stream with a `hello` and no
  `terminal` record is an **incomplete run**, never a passing one.
- ⚠️ **This is what makes a `pass` corroborable, and a `hello` alone never can be.** The `hello` is the
  first line, emitted before any message is processed — a counterparty that starts, announces itself, and
  conversates not at all satisfies every field a hello-only check reads. Requiring `terminal_state:
  completed` on **both** streams is what closes it.
- The record vocabulary is therefore **`{hello, sent, readback, terminal}`**.
