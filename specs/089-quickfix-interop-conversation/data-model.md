# Data Model — 089 live QuickFIX interop fidelity

**Date**: 2026-09-10 · Derived from [spec.md](./spec.md) + [research.md](./research.md).

Five entities carry this feature. Three are new artifacts; two extend existing ones.

```
Conversation script ──drives──▶ Cell (8) ──contains──▶ Witness (message × direction)
                                  │                        ▲
                                  │                        │ compared
                    Intent record ┘                        │
                     (fixpp side)  └──────────────────┐    │
                                                      ▼    │
                              Readback record (peer side) ─┘
                                       ▲
                              Hello record (first line, once per run)
```

---

## 1. Hello record — the capability handshake (FR-016a)

Emitted **once**, as the **first line** of the readback stream, before any message is processed.

| Field | Type | Rules |
|---|---|---|
| `type` | string | literal `"hello"` |
| `engine` | string | which counterparty — the two flavours must be distinguishable |
| `engine_version` | string | the QuickFIX version actually running (pinned: cpp `1.16.0`, J `3.0.1`) |
| `readback_protocol` | integer | the record-format version this build emits |
| `dictionary_enabled` | boolean | whether this cell's session has a data dictionary — see *Validation rules* |

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
  group-free field set and compare clean against a group-free intent — a false green.

---

## 2. Readback record — what the peer parsed (FR-003)

One record per application message the counterparty receives.

| Field | Type | Rules |
|---|---|---|
| `type` | string | literal `"readback"` |
| `msg_type` | string | FIX `MsgType(35)` as received |
| `seq_num` | integer | `MsgSeqNum(34)` — half the correlation key (FR-005) |
| `direction` | enum | the other half; which way the message travelled |
| `poss_dup` | boolean | `PossDupFlag(43)`; disambiguates a replayed sequence number |
| `fields` | list of *field entries* | **every** body field parsed; header/trailer excluded |

**Field entry**

| Field | Type | Rules |
|---|---|---|
| `path` | string | `"40"` for a top-level field; `"453[0].448"` for a group member |
| `value` | string | the value as decoded, escaped per the contract |

**Validation rules**

- **Body only.** `8, 9, 35, 34, 49, 56, 52, 10` and every other header/trailer field are excluded — they
  are session-managed and outside the fidelity claim (spec Clarification 1).
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

## 3. Intent record — what fixpp meant to send

Produced fixpp-side when a message is built; the comparison's left-hand side.

| Field | Type | Rules |
|---|---|---|
| `msg_type` | string | must equal the readback's |
| `seq_num` | integer | correlation key with `direction` |
| `direction` | enum | as above |
| `fields` | list of *field entries* | the declared field set, same `path` grammar |

**Validation rules**

- Same `path` grammar as the readback record, or set equality is comparing incomparable things.
- The intent record is the **single source of truth** for what was sent. It must be derived from the
  same values handed to the builder, never re-read from the serialized frame — re-reading would compare
  fixpp's writer to fixpp's reader and prove nothing about the peer.

---

## 4. Witness — one (message × direction) fidelity result

The unit a catalogue row cites (FR-015a).

| Field | Type | Rules |
|---|---|---|
| `witness_id` | string | stable and derivable from the conversation script |
| `cell_id` | string | the run it belongs to |
| `msg_type` | string | |
| `direction` | enum | |
| `verdict` | enum | `pass` · `fail` · `skip` — and `skip` may not be produced by a missing record |
| `mismatch` | list | on failure: the offending paths, each classified |

**Validation rules — the comparison (FR-006)**

Exact set equality between intent and readback, failing on **any** of three, each named distinctly:

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

## 5. Cell — one process run (FR-015a)

Extends the existing `cell_results.yaml` row.

**Existing required fields** (`cell_results_schema_check_test.py:23`): `id`, `config`, `kind`, `status`,
`matrix_disposition`, `spec_ref`. Conditionals: `deferred_reason`, `priority`, `tracking_issue_state`.

**New — run evidence (FR-013)**

| Field | Type | Rules |
|---|---|---|
| `run_id` | string | identifies the run that produced this row |
| `run_timestamp` | timestamp | when it ran |
| `counterparty_flavour` | string | which engine |
| `counterparty_version` | string | from the hello record, **not** from a config file |
| `counterparty_digest` | string | the image digest actually used (FR-016b) |
| `artifact_path` | string | pointer to the run directory |

**Validation rules**

- **`status: pass` without complete evidence ⇒ the schema check FAILS** (FR-014). This is the property
  the current schema lacks entirely: today a hand-edited `pass` satisfies all nine tests.
- `counterparty_version` comes from the **hello record** — a value read from a config file describes what
  was *asked for*, not what *ran*, and this feature exists because those diverge.
- Cardinality: **8 cells** = 4 role×flavour × 2 validation arms, **per config**; 24 rows across the three
  configs. FR-021 forbids folding a config into another's row.
- ⚠️ A cell that died on `ENOSPC` is **neither `pass` nor `skip`** — see plan.md § Disk preflight.

---

## 6. Witness evidence record — the second level (FR-015b)

A separate artifact accumulating witness rows across all cells and all configs.

**Validation rules**

- **Exact-set completeness gate over the union**, mirroring the existing cell-id set equality, so a
  witness that silently stops being produced is detected rather than absent.
- ⚠️ **The expected witness set is DERIVED from the conversation script, never hand-listed.** A
  hand-maintained list drifts toward whatever is currently produced, which makes the gate agree with
  reality by construction — a check that cannot fail.
- ⚠️ **Accumulated, not rewritten per config.** The three configs run in sequence with reclaim between
  them (plan.md § Matrix sequencing); a later config overwriting the record erases the earlier one's
  rows, and the completeness gate would then pass over a third of the evidence.
- The gate runs **after the last configuration**, over the union.

---

## 7. Conversation script step

| Field | Type | Rules |
|---|---|---|
| `step` | integer | order within the conversation |
| `phase` | enum | `logon` · `admin` · `business` · `logout` |
| `msg_type` | string | |
| `originator` | enum | which side sends |
| `expects_witness` | boolean | business messages yes; admin exchanges assert session behaviour |

The script is the **source of the expected witness set**. One script, parameterised by role×flavour, so
the four combinations cannot drift apart.

---

## 8. Disk preflight reading

| Field | Type | Rules |
|---|---|---|
| `build_mount_free` | bytes | `df` on the build mount |
| `host_mount_free` | bytes | `df` on the Windows host mount; **N/A off WSL** |
| `is_wsl` | boolean | `microsoft` in `/proc/version` |
| `threshold` | bytes | from R-1's measurement, with the date it was taken |
| `verdict` | enum | `proceed` · `reclaim-first` · `stop` |

**Validation rules**

- Gate on the **minimum** of the two readings. Gating on `build_mount_free` alone is the defect.
- ⚠️ **On WSL, an unreadable or absent host mount ⇒ `stop`.** "Could not read the binding constraint"
  must never resolve to `proceed`. This is the arm most likely to be written backwards, and it needs its
  own witness.
- Off WSL (a CI runner, where the host mount does not exist), `host_mount_free` is **not applicable** and
  the build-mount reading governs — a distinct case from *unreadable*, and the two must not share a code
  path.
- Both readings and the verdict are printed on **every** invocation, pass or fail.
- `verdict: stop` exits non-zero. It must not warn-and-continue and must not exit 0.
