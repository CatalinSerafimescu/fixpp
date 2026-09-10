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
                                                     gate per CONFIG over the
                                                     census's full 100-key
                                                     projection, AND over the
                                                     union.  `arm` is INSIDE
                                                     `cell_id` — never a gate
                                                     axis: W-3a)

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
record (§12)** is the other half; FR-014's corroboration requires both.

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

⛔ **`fix_type` is evidence of DICTIONARY-BACKED RESOLUTION. It is NOT accessor provenance, and no
record field is.** A generic enumeration walks the message's own field map and performs **no dictionary
lookup at all**, so an entry with `fix_type` absent or empty shows the dictionary was not consulted — that
is the property this column carries, and the only one. The script deliberately does not carry `fix_type`
and the comparator resolves the expected value from its **own** copy of `FIX44.xml`.

⚠️ **`fix_type` does NOT witness typed-accessor invocation, and neither does any other serialized value.**
The generated getter returns the **caller's own object**, so every typed output value is derivable without
calling it. FR-003b's anti-vacuity arm is therefore a **compile-time** arm over the counterparty source,
not a record arm — see `spec.md` § *Clarifications* → *Session 2026-09-10 (Gate A fresh loop, round 1)*
for the source evidence, and FR-003b for the arm. ⚠️ **No `accessor_witness` field exists in this schema
and none is to be added**; three rounds proposed one and each proposal was synthesizable.

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

- **Body only, structurally determined — and the partition is the CANONICAL one, not the RECEIVER's.**
  ⚠️ **What each ENGINE classifies is the PROBLEM STATEMENT, not the rule** — the same demotion
  `contracts/readback-jsonl.md` § *⚠️ The header/body partition is specified HERE, not delegated to the
  engines* already makes. Each engine's own membership is *its built-in list ∪ dictionary-declared header*,
  and **the two built-in lists differ** (QuickFIX-J's contains `ApplExtID(1156)`, QuickFIX-cpp's does not),
  so a receiver-specific rule yields two different `fields` sets for identical bytes and breaks FR-004.
  ⛔ **The normative inclusion rule is the CANONICAL UNION PARTITION**
  (`contracts/readback-jsonl.md` § *⛔ THE CANONICAL PARTITION — the decision, as a value*, restated at
  FR-004): `header` = (QuickFIX-cpp's built-in list) ∪ (QuickFIX-J's built-in list) ∪ (the fields declared
  in the `<header>` block of the dictionary loaded for that cell); a field in that set is excluded from
  `fields` by **both** emitters, whichever engine's own classifier would have called it body. An engine's
  own classification survives only as **captured diagnostic evidence**, never as the inclusion rule.
  `8, 9, 35, 34, 49, 56, 52, 10` are an illustrative subset, **not the rule**.
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
| **`kind`** | enum | `conformance` · `validator-positive-control` — ⭐ **required.** W-3a's projection and the 32-slot rules **filter on this field**, and the entity omitted it: the scoping was applied to the rules and not to the schema that carries the discriminator |
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

⚠️ **Exactly one authoritative `conformance` run per `(cell_id, config)` slot** — and π ranges over
`kind: conformance` rows only, so a `validator-positive-control` run's rows never enter this equality.
`(cell_id, config, run_id)` uniqueness
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
  never committed, and **`validator-positive-control` runs never committed *as a row of THIS manifest*** (they
  occupy no slot, §11). ⚠️ **Qualify the subject, because two different files in this bundle are "committed"**:
  this manifest, and the `witness_evidence.yaml` carrying the run **ledger**. §11 says the opposite about the
  ledger — *"the `runs:` ledger holds **every** run — `conformance` and `validator-positive-control`,
  authoritative and superseded"* — and E-7 / **E-7b** depend on exactly that. Read unqualified, this sentence
  makes E-7's control-pair reference resolution unsatisfiable.
  ⚠️ So this manifest's population is `kind: conformance`, `authoritative: true` runs — exactly 32. The shipped `id` field is **retained** and derived as `"<cell_id>@<config>"`, so
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
- The gate compares **π(`kind: conformance` authoritative rows of config `c`)** against **π(the same, for
  config `c'`)** for every ordered pair of the four configs, and against the census set — **exact set
  equality**, not containment. Only rows with `authoritative: true` **and `kind: conformance`**
  participate. ⚠️ **Both discriminators, not one** (§11 § *THE TWO DISCRIMINATORS*): a
  `validator-positive-control` run's rows are not census keys, so unscoped the first control run breaks
  this equality on the config it ran under. It *also* runs over the union after the
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
  resident footprint. Those two quantities differ by **more than an order of magnitude** on a sanitizer
  configuration, so one threshold sized for the smaller ceiling authorises a build the larger ceiling
  cannot hold: the exact ENOSPC the gate exists to prevent, reproduced by the gate's own arithmetic.
  **One threshold applied to two ceilings under-checks whichever ceiling is larger.** ⚠️ **No figures
  here, deliberately** — every disk figure this bundle carried was false within the day it was written.
  Re-derive with `du -sh build/*/` and `plan.md` § *Disk preflight*'s recipe; the argument needs the
  *relation*, never an operand.
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
- A **terminal record (§12) is written to BOTH streams whatever the outcome** — the run's two processes each write one (⚠️ process count, not emitter count) — including an abort. A stream
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

⭐ **WHERE IT LANDS AND WHAT PRODUCES IT** — a schema with no producer is the defect Gate A fresh-loop
round 2 filed against this very section. **File**: a `validation_pairs:` section of the same committed
`library/tests/interop/witness_evidence.yaml` that carries the witness rows (§6) and the run ledger (§11)
— not a fourth file. **Producer**: the named promotion command
(`phase-9-harness/tools/promote_interop_evidence.py`, FR-014b), which **constructs** each pair from two
already-promoted runs and **evaluates E-7** on it before writing it. **Gated by**: E-7 (well-formedness,
at promotion) and **E-7a** (existence and completeness, in the committed schema check —
`contracts/witness-evidence.md`). ⚠️ Without E-7a **zero pairs is green**: nothing else in this bundle
ranges over a pair's cardinality, so an implementation emitting none satisfies every other gate.

| Field | Type | Rules |
|---|---|---|
| `pair_id` | string | identifies the pair |
| `cell_pair` | (cell_id, cell_id) | the validation-off and validation-on cells being compared |
| `config` | string | the configuration both arms ran under — a pair is within one config |
| `script_digest` | string | both arms must have run the same script |
| **`off_run_id`** | string | ⭐ the **run** `accepted_off` was read from |
| **`on_run_id`** | string | ⭐ the **run** `accepted_on` was read from |
| **`kind`** | enum | `conformance` · `validator-positive-control` — **the same two spellings the Run entity and the run ledger use** (§9, §11); it MUST match the `kind` of both referenced runs. ⚠️ An earlier revision spelled the second value `positive-control` here and `validator-positive-control` everywhere else, which made the match-both-runs rule unsatisfiable — one enum, one spelling, bundle-wide |
| **`expected_verdict`** | enum \| absent | **required when `kind: validator-positive-control`** — that exact value; absent for `conformance` |
| `accepted_off` | set of `script_step_id` | messages the validation-off arm accepted |
| `accepted_on` | set of `script_step_id` | messages the validation-on arm accepted |
| `dispositions` | list | per message, each arm's validator disposition — accepted, or rejected with the objection |
| **`authoritative`** | bool | ⭐ **exactly one `authoritative: true` pair per `(cell_pair, config)`.** ⚠️ The pair entity lacked this field entirely while the Run entity has it — the very discriminator invented to close the duplicate-collapse problem, absent one entity over. ⛔ **TRUTH CONDITIONS, WRITER AND SUPERSESSION — modelled on §11's, because a field with no producer is the RC-A shape**: the `validation_pairs:` section records **every** pair, authoritative and superseded alike, and is not filtered on the way in; a pair is **`authoritative: false` iff either referenced run has been superseded** (mirroring §11's *"a superseded retry — recorded, and entering no gate"*); and the **named promotion command sets it** — it writes each new pair `authoritative: true` and **demotes the prior pair for that `(cell_pair, config)`** in the same write. ⚠️ Non-authoritative pairs **enter no gate**: E-7a's set equality is scoped onto this field, exactly as §11 scopes the slot equality |
| `verdict` | enum | `identical` · `diverged` — ⛔ **DERIVED, never asserted**; see the validation rules |

**Validation rules**

- ⛔ **BOTH REFERENCED RUNS MUST BE `authoritative: true`, AND FOR A `conformance` PAIR EACH MUST BE THE RUN
  SELECTED FOR ITS `(cell_id, config)` SLOT.** ⚠️ Without this the pair is satisfiable by two **superseded**
  retries: the ledger records `authoritative: false` rows and §11 says those *enter no gate*, yet distinct
  ids, opposite arms, matching metadata and opposite `has_validator` are all satisfiable by them. All 16
  conformance slots could then be backed by runs the ledger says govern nothing, with E-1c, E-7 **and** E-7a
  green. **Fails toward GREEN.**
  ⛔ **AND THIS PREDICATE NEEDS A HOST THAT RE-EVALUATES IT, NOT ONLY ONE THAT FIRES ONCE** — it is promoted
  as **both** `contracts/witness-evidence.md` **E-7** (at promotion, when the pair is constructed) and
  **E-7c** (in the committed schema check, on every CI run). ⚠️ `authoritative` is **mutable after the pair
  is written**: a retry landing later supersedes a referenced run, and E-7 does not re-run. The committed
  check's *"every `off_run_id`/`on_run_id` resolves to a `runs:` row"* does not catch it — a superseded row
  resolves. Both operands are in the committed file, so E-7c opens nothing.
- ⛔ **`accepted_off`, `accepted_on` AND `dispositions` MUST BE EXTRACTED FROM THE TWO REFERENCED RUN
  ARTIFACTS, AND `verdict` MUST BE COMPUTED FROM THEM** — exact set equality over `accepted_off` /
  `accepted_on`. A `conformance` pair MUST yield `verdict: identical`; a `validator-positive-control` pair
  MUST yield `diverged` and match its `expected_verdict`.
  ⚠️ **This is a SPURIOUS HIT, not a missing check**: with the result merely *carried* rather than
  *derived*, a producer that hard-codes `identical` for conformance pairs passes E-7, E-7a **and** FR-010a's
  divergence probe — the pair reports the expected answer without ever measuring the property, and the whole
  cross-arm claim (FR-010 / SC-004) is unevidenced. A forced-**miss** arm cannot catch it, because a
  hard-coded pair reports the same shape a correct one does.
- ⛔ **EXACTLY ONE `authoritative: true`, `kind: conformance` PAIR PER `(cell_pair, config)`, AND E-7a's SET
  EQUALITY IS SCOPED `kind: conformance ∧ authoritative: true`** — the same scoping §11 applies to the slot
  equality. ⚠️ **Without the scoping the rule is one field short of the defect it closes**: two
  `authoritative: false` pairs plus one `true` pair for one slot satisfy the cardinality sentence, and an
  unscoped equality collapses all three — the same set-collapse, one discriminator over. ⚠️ E-7a asserts *set* equality
  against the 16-pair inventory, and a set equality keyed on the slot is **satisfied when a slot is claimed
  twice** — the duplicates collapse. An `identical` pair and a `diverged` pair for the same slot can both
  sit in the committed artifact with no rule saying which governs. That is verbatim the collapse E-1c and
  W-3c were written to close **for runs** (§11), and the 15-of-16 fixture cannot catch it: that fixture
  forces a *miss*, and a duplicate slot is a *spurious hit*.
- `verdict: diverged` MUST name **the message and the validator's objection** (FR-012). The witness
  `mismatch` vocabulary is field-level (`value_mismatch` / `missing` / `spurious`) and cannot express
  *"the on arm rejected a message the off arm accepted"*.
- ⛔ **`off_run_id` and `on_run_id` MUST name two DISTINCT runs whose arms are opposite.** ⚠️ Stated
  **positionally**, because a run carries one `cell_id` while the pair carries a pair and *"both match the
  pair's `cell_pair`"* has no defined truth value: the run named by **`off_run_id` carries `cell_pair[0]`**
  (the validation-**off** cell) and recorded `has_validator: false`; the run named by **`on_run_id` carries
  `cell_pair[1]`** (the validation-**on** cell) and recorded `has_validator: true`; and **both** carry the
  pair's `config`, `script_digest` and `kind`.
  ⚠️ **Without this the pair is satisfiable by DEGENERATE CONSTRUCTION**: nothing otherwise forbids
  `accepted_off` and `accepted_on` being read from *one* execution, which yields `identical` across all 32
  slots with the two arms never actually compared — a green that means only that a set equals itself. That
  is a **spurious hit**, and it is not caught by any arm that forces a *divergence*, because the degenerate
  pair reports the same verdict a correct one does.
  ⛔ **This rule is promoted as `contracts/witness-evidence.md` E-7 and has its OWN arm there** — a
  fixture constructing a pair whose `off_run_id == on_run_id` (and a second whose two runs both recorded
  the same `has_validator`) must go **RED at promotion**, with a matching row in `quickstart.md` Step 4's
  spurious-hit table. ⚠️ **Not E-6.** E-6 is a *single run's* arm attestation and says nothing about a
  *pair's* two referenced runs; citing it here left this rule with no arm at all, which is precisely the
  shape — *a clause that names a spurious hit and instantiates nothing* — that this bundle keeps
  reproducing. A forced-MISS arm cannot catch a spurious HIT.
- ⛔ **The `kind: conformance` pair inventory is EXACTLY 16, and the set must equal it — not be contained
  in it** (E-7a). **16 is derived, never an independent count**: it is the 32 conformance slots (§11)
  quotiented by the arm axis — `cell_id ≡ (combo_id, arm)`, so the two arms of one `combo_id` under one
  `config` are one pair ⇒ 4 combos × 4 configs = 16. ⚠️ Scoped to `kind: conformance`: control pairs are
  **additional** and carry no slot, so an unscoped cardinality would be `16 + N` and unsatisfiable in
  exactly the way the ledger's slot rule was before it was scoped.
- `expected_verdict` is meaningful only for **`kind: validator-positive-control`** — that exact value — and such pairs MUST assert `diverged`, and
  **remain outside the 32 conformance slots**, so a deliberately-diverging control can never be counted as
  a conformance result.
- ⚠️ **`identical` is not by itself evidence.** A validator that never runs produces `identical` by
  construction, and FR-011's "a production dictionary is loaded" establishes presence, not execution. The
  pair is admissible only alongside FR-010a's **divergence probe**: a seeded message the dictionary should
  reject must make this entity report `diverged`, with the objection recorded.
  ⛔ **AND THAT ADMISSIBILITY CONDITION HAS AN INSTRUMENT — `contracts/witness-evidence.md` E-7b**, which
  requires at least one `kind: validator-positive-control` pair with `expected_verdict: diverged` and
  `verdict: diverged` to exist in the committed section. ⚠️ **It exists because ZERO CONTROL pairs was
  GREEN**: E-7a excludes control pairs from its equality by design, the committed check's other operands do
  not mention them, and E-7 is vacuous over a `kind` nothing emitted — so 16 conformance pairs with no
  control pair at all passed every standing gate, while this paragraph says those 16 are inadmissible. A
  requirement and a success criterion are not gates.

---

## 11. Run ledger — the committed, machine-independent record of the runs (FR-014b)

A `runs:` section of the same witness-evidence artifact (§6), not a third file. **One entry per RUN** —
authoritative and superseded alike, and `conformance` and `validator-positive-control` alike. The
`authoritative` and `kind` columns discriminate; the section is not filtered on the way in.
⚠️ An earlier revision said *"one entry per **authoritative** run"* while the `authoritative` column said
*"retries are recorded with `false`"* — recorded **where**, if the section holds only authoritative runs?
Resolved here in favour of recording: a retry that is never written down cannot be shown not to have been
counted, which is what SC-009b demands.

> ### ⛔ THE TWO DISCRIMINATORS — read before restating any rule about *slots* or *authoritative runs*
>
> A ledger row is admitted to a gate by **two independent axes**, and scoping one while leaving the other
> is the same partial-restatement defect one dimension over:
>
> | axis | value | meaning |
> |---|---|---|
> | `kind` | `conformance` | **occupies a slot**; enters the 32-slot inventory, the manifest, the completeness projection π, and every W-*/E-* gate below |
> | ″ | `validator-positive-control` | **occupies NO slot**; carries `cell_id`/`config` to name the cell it *probes*, never a slot claim. Enters **only** E-7 / E-7a / FR-010a. ⛔ It is **`authoritative: true`** — making it `false` would place it in "enter no gate" and E-7's entire purpose is to gate it |
> | `authoritative` | `true` | the run that governs its slot |
> | ″ | `false` | a superseded retry — recorded, and entering no gate |
>
> **Therefore**: every clause in this bundle that says *slot*, *the 32-slot inventory*, *authoritative
> run/entry/row* or ranges over π means **`kind: conformance` ∧ `authoritative: true`**, unless it names
> `validator-positive-control` explicitly. There is no `occupies_slot` column: it would be a second copy of
> `kind` that can disagree with it. ⚠️ **A separate `control_runs:` section was REJECTED** — §9 already
> models the control as a *Run*, and a second section would force E-7 to resolve `off_run_id`/`on_run_id`
> across two populations.

| Field | Type | Rules |
|---|---|---|
| `cell_id` · `config` | string | for `kind: conformance` — **the slot**; the set of slots carried by rows with `kind: conformance` **and** `authoritative: true` MUST equal the 32-slot inventory **exactly**. For `kind: validator-positive-control` — the cell this control **probes**, which is **not** a slot claim and is excluded from that equality |
| `run_id` | string | for `kind: conformance` — the run designated for that slot (one carries `authoritative: true`); for a control run — its own identity, which is what `off_run_id`/`on_run_id` resolve to |
| `authoritative` | boolean | **not `true` by construction** — the section records every run, so a superseded retry is written here with `false` and enters no gate. ⚠️ A `validator-positive-control` run is `true`: it is not a retry, and making it `false` would place it in *"enters no gate"* while E-7/E-7a exist precisely to gate it |
| `kind` | enum | `conformance` · `validator-positive-control` (FR-010a) — the same two spellings §9 and §10 use. **Only `conformance` runs occupy a slot**; see *THE TWO DISCRIMINATORS* above. ⚠️ A `validator-positive-control` row is legal and expected here — this is the row `off_run_id`/`on_run_id` resolve against for a control pair, and §10's *"`kind` MUST match the `kind` of both referenced runs"* is unsatisfiable without it |
| `expected_verdict` | enum | positive controls only — `diverged` |
| `run_timestamp` | timestamp | |
| `counterparty_flavour` · `counterparty_version` · `counterparty_digest` | string | version from the **hello**, never from a config file |
| `script_digest` | string | the shim's value, having matched both sides' recomputation (§1) |
| `has_validator` | boolean | fixpp's arm attestation (§1a); MUST equal `arm == "validation-on"` |
| `terminal_state` | enum | `completed` · `aborted` · `error:enospc` — from the terminal records of **both streams**, i.e. both of the run's two processes (§12). ⚠️ **TWO is the process count, not the emitter count** — see `contracts/readback-jsonl.md` § *THE THREE EMITTERS* |
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
