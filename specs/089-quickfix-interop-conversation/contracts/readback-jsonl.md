# Contract: counterparty readback stream

**Producers**: `phase-9-harness/quickfix-cpp/counterparty/interop_counterparty_main.cpp` (C++),
`phase-9-harness/quickfixj/.../InteropCounterparty.java` (Java).
**Consumer**: the fixpp-side comparator + the harness shim.

**FR-004 requires ONE format.** Both producers emit byte-compatible records; a single consumer parses
both. A difference between the two emitters is a contract violation, not an implementation detail.

## Transport

- **Location**: a sibling of `argv[2]` (the transcript path). The run directory *is* the results
  directory, so the shim needs **no collection change** (research R-4).
- **Mode**: ⚠️ **truncate, not append.** Both transcripts open in append mode; the shim truncates the
  transcript per run but `shutil.rmtree` clears only subdirectories. An appended readback file would
  **silently accumulate stale records across runs**, and a stale record is worse than a missing one — it
  can satisfy a comparator looking for a witness the current run never produced.
- **Framing**: JSON Lines — one complete JSON object per line, flushed per line so a killed process still
  yields the records it had emitted.
- **Ordering**: the `hello` record is first. Readback records follow in the order messages were received.

## Records

Two `type`s: `hello` (exactly one, first) and `readback` (zero or more). Field-level definitions live in
[data-model.md](../data-model.md) §1–§2 and are not restated here.

## ⚠️ Escaping — a correctness clause, not formatting

Neither engine has a JSON library (verified: no Jackson/Gson/JSON-B in `pom.xml`; the C++ counterparty
links only quickfix + OpenSSL + Threads), so **both writers are hand-rolled** and both must implement the
same rule:

| Input | Requirement |
|---|---|
| `"` and `\` | escaped |
| every byte `< 0x20` | `\u00XX` — **including SOH**, which appears inside data fields |
| non-UTF-8 bytes | the contract MUST state a single decision; both writers MUST implement that same decision |

**Why this is load-bearing**: `RawData(96)`, `XmlData(213)` and `SecureData(91)` may carry arbitrary
bytes. A naive writer emits invalid JSON on exactly the messages most worth inspecting, and the failure
appears as a parse error attributed to the consumer.

**Witness required**: each writer is exercised against a value containing each class above, and the
consumer parses the result. Untested escaping is an untested claim.

## Conformance obligations

| # | Obligation | Failing behaviour |
|---|---|---|
| C-1 | `hello` present and first | absent ⇒ cell **FAILS** (not skip) |
| C-2 | `readback_protocol` ≥ what the cell requires | older ⇒ cell **FAILS** |
| C-3 | Every body field the peer parsed appears | a `NoXxx` count with no member at that path is **malformed** |
| C-4 | Group instances carry a path, not just a count | see C-3; ⚠️ C++ trap: instances live in `m_groups`, the count in `m_fields` |
| C-5 | Enumeration does not mutate the message | ⚠️ Java trap: `getGroups(int)` is `computeIfAbsent`; drive from `groupKeyIterator()` |
| C-6 | Header/trailer excluded | a header field in `fields` is a violation |
| C-7 | Both emitters byte-compatible | one consumer, both producers, same fixture |

**C-4 and C-5 are silent failures.** Neither is caught by a green run: C-4 yields a populated-looking
record missing exactly the structure under test, and C-5 corrupts the subject while observing it. Both
need their own witness.
