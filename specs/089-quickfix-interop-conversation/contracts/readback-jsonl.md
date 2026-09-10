# Contract: counterparty readback stream

**Producers**: `phase-9-harness/quickfix-cpp/counterparty/interop_counterparty_main.cpp` (C++),
`phase-9-harness/quickfixj/.../InteropCounterparty.java` (Java).
**Consumer**: the fixpp-side comparator + the harness shim.

**FR-004 requires ONE format.** Both producers emit byte-compatible records; a single consumer parses
both. A difference between the two emitters is a contract violation, not an implementation detail.

## Transport

- **Location — TWO files, one per emitting process, both in the run directory.** ⚠️ Both sides emit both
  record kinds (see § Records), and they are **separate processes with separate lifetimes**, so they must
  not share one path:

  | Writer | File | How it resolves the path |
  |---|---|---|
  | the counterparty | `counterparty-readback.jsonl` | a **sibling of `argv[2]`** (the transcript path). The run directory *is* the results directory, so the shim needs **no collection change** (research R-4) |
  | fixpp (the gtest) | `fixpp-readback.jsonl` | from the environment variable the shim sets (FR-024 / R-4a) — fixpp has no `argv[2]` here |

  ⚠️ **One shared file under truncate mode is a data-loss bug, not a tidiness question**: whichever process
  opens second wipes the other's records, reproducing at the *file* level exactly the stale/absent-record
  hazard truncate mode exists to close at the *record* level. Data-model §3's *"same `path` grammar, same
  **format**, same correlation key"* is deliberately worded that way: it is the **format** and the **key**
  that are shared; the file is per-writer.
- **Joining them**: the comparator reads both streams and pairs a `sent` record with its `readback`
  counterpart on `(seq_num, direction, occurrence)` — the same key regardless of which file each came from.
  That is what makes one comparator serve both directions.
- **Mode**: ⚠️ **truncate, not append**, for **each** file. Both transcripts open in append mode; the shim
  truncates the transcript per run but `shutil.rmtree` clears only subdirectories. An appended readback
  file would **silently accumulate stale records across runs**, and a stale record is worse than a missing
  one — it can satisfy a comparator looking for a witness the current run never produced.
- **Both streams carry a `hello` and a `terminal`.** The counterparty's `hello` is what the shim-side gate
  reads before the conversation (FR-016a / R-4a); fixpp's `hello` additionally carries the **arm
  attestation** `has_validator` and the dictionary digest (data-model §1a). The counterparty receives its
  hello fields through the `cp_env` block `launch_counterparty` already builds — the six
  `INTEROP_CP_{RUN_ID,CELL_ID,CONFIG,IMAGE_DIGEST,SCRIPT_PATH,SCRIPT_DIGEST}` keys of data-model §1 — and
  **recomputes** `script_digest` over the file it opened rather than echoing the value it was handed.
- **Framing**: JSON Lines — one complete JSON object per line, flushed per line so a killed process still
  yields the records it had emitted.
- **Ordering**: the `hello` record is first. Readback records follow in the order messages were received.

## Records

Four `type`s: `hello` (exactly one, first), `sent` (zero or more), `readback` (zero or more) and
`terminal` (exactly one, **last**). ⚠️ `terminal` was added because the `hello` is written *before any
message is processed*: a counterparty that starts, announces itself and conversates not at all satisfies
every field a hello-only corroboration reads (data-model §12).
Field-level definitions live in [data-model.md](../data-model.md) §1–§3 and are not restated here.

⚠️ **`sent` and `readback` are symmetric and BOTH sides emit BOTH — into their own file each** (see
§ Transport). The counterparty emits a `sent` record for every application message it builds and a
`readback` record for every one it parses; fixpp does the same. Two comparisons run per business message —
`(fixpp sent → peer readback)` and `(peer sent → fixpp readback)` — through one comparator, one grammar and
one correlation key (`seq_num`, `direction`, `occurrence`), spanning the two files. Without the peer's
`sent` record the second comparison has no left-hand side, and the peer's run-time-generated
`OrderID`/`ExecID` cannot be supplied by any declarative script.

## ⚠️ Escaping and encoding — a correctness clause, not formatting

Neither engine has a JSON library (verified: no Jackson/Gson/JSON-B in `pom.xml`; the C++ counterparty
links only quickfix + OpenSSL + Threads), so **both writers are hand-rolled** and both must implement the
same rule. The two languages have different string models — Java `String` is UTF-16, C++ `std::string` is
bytes — so an unstated decision here does not defer to implementation, it **diverges** and breaks FR-004.

| Input | Requirement |
|---|---|
| `"` and `\` | escaped as `\"` and `\\` |
| every byte `< 0x20` | `\u00XX`, **lower-case hex** — including SOH, which appears inside data fields |
| `/` and every byte `≥ 0x20` that is valid UTF-8 | emitted literally. **No optional escaping**: `\/`, `\uXXXX` for non-ASCII, and any other permitted-but-unnecessary escape are forbidden, because byte compatibility (C-7) admits exactly one spelling |
| **non-UTF-8 bytes** | **DECIDED**: the field entry carries **`value_b64`** — base64 (RFC 4648 standard alphabet, `=` padding, no line breaks) of the **raw bytes as received** — and **no `value` key**. Exactly one of `value` / `value_b64` is present on every field entry |

**Why base64 and not an escape extension**: it is byte-exact, has one canonical alphabet, imposes no
encoding assumption on either language's string type, and both a hand-rolled C++ writer and a Java writer
produce identical output for identical input. A `\xNN`-style extension would not be JSON, and a
lossy-replacement policy would silently corrupt exactly the fields worth inspecting.

**Canonical form** — required for C-7's byte compatibility, not stylistic:

- **Key order is fixed** by this contract, in the order the fields are listed in `data-model.md` §1–§3.
- **No insignificant whitespace**: one record per line, no spaces after `:` or `,`.
- **Numbers** are emitted as bare decimal integers (`seq_num`, `occurrence`, `readback_protocol`); no
  exponent form, no leading `+`, no leading zeros.
- **Field entries are SORTED by canonical parsed-path order** — not by the engine's walk. ⚠️ The previous
  rule (*"the order the engine's walk produced them"*) cannot hold: two engines' walks are not one order, so
  a committed cross-language golden fixture could not be byte-compatible for both producers, which is what
  this section exists to deliver.

  **The rule, as a value.** Parse each `path` into a tuple of integers by splitting on `.` and on `[`/`]`:
  `"40"` → `(40)`; `"453[0].448"` → `(453, 0, 448)`; `"453[1].802[0].523"` → `(453, 1, 802, 0, 523)`. Sort
  the entries by that tuple, comparing **element by element numerically**, a shorter tuple sorting before a
  longer one that shares its prefix. Sort `fields` and `typed_reads` independently, each by the same rule.

  **Group instance order stays significant** and is preserved automatically: the instance index is *inside*
  the path, so `453[0].448` sorts before `453[1].448` by the same numeric comparison. Nothing about the
  message's own ordering is lost; what is removed is the engine's walk order, which was never part of the
  data.

**Why this is load-bearing**: `RawData(96)`, `XmlData(213)` and `SecureData(91)` may carry arbitrary
bytes. A naive writer emits invalid JSON on exactly the messages most worth inspecting, and the failure
appears as a parse error attributed to the consumer.

⚠️ **Two of those three fields are not reachable through a readback record under this feature's own
configuration**, and the motivation above must be read with that in mind: `XmlData(213)` is a **built-in
header** field on both engines, and `SecureData(91)` is declared in `FIX44.xml`'s `<header>` block, so it
becomes header under `UseDataDictionary=Y` — both are then excluded by C-6. `RawData(96)` is the one that
remains reachable in `fields`. The escaping rule still applies to every emitted value and its witness is
still required; the rule simply is not justified by fields C-6 excludes.

**Witness required**: each writer is exercised against a value containing each class above — `"`, `\`, a
byte `< 0x20`, a multi-byte UTF-8 sequence, and a byte sequence that is **not** valid UTF-8 — and the
consumer parses the result. Untested escaping is an untested claim.

## ⚠️ The header/body partition is specified HERE, not delegated to the engines

C-6 excludes header and trailer fields. That partition is **not** a fixed tag list, and delegating it to
each engine violates C-7:

**What each ENGINE does — the problem statement, not the rule** (the rule is the boxed decision below):

- On both engines, each engine's *own* membership is *its* built-in list ∪ dictionary-declared header,
  computed by the two-argument `isHeaderField` overload — `quickfix-cpp/src/C++/Message.cpp`'s
  `Message::isHeaderField(int field, const DataDictionary *pD)` (built-in `switch` first, then the
  dictionary when one is present) and the equivalent in
  `quickfixj-base/src/main/java/quickfix/Message.java` over its own built-in `switch`.
- ⚠️ **The two built-in lists are not identical.** QuickFIX-J's contains **`ApplExtID(1156)`**;
  QuickFIX-cpp's does not. For identical bytes carrying tag 1156, QFJ classifies it **header** (excluded)
  and QuickFIX-cpp **body** (included) — two emitters, two different `fields` sets, silently, with no
  configuration involved. (Tag 1156 is not an invented example: it is the field the library records as
  open work under 074's `L-074-1`.)
- ⚠️ **The partition MOVES under this feature's own dictionary flip.** Fields declared in `FIX44.xml`'s
  `<header>` block but absent from an engine's built-in list are **body** under `UseDataDictionary=N` and
  **header** under `=Y`. So FR-006's exact-set equality ranges over a *different set* before and after US1,
  and any golden or expected set fixed against today's `=N` behaviour is wrong afterwards.

### ⛔ THE CANONICAL PARTITION — the decision, as a value

> **`header` = (QuickFIX-cpp's built-in list) ∪ (QuickFIX-J's built-in list) ∪ (the fields declared in the
> `<header>` block of the dictionary loaded for that cell).** A field in that set is excluded from `fields`
> by **both** emitters, whichever engine's own classifier would have called it body.

> **Tag `1156` (`ApplExtID`) is HEADER. It is excluded from `fields` on both engines.**

That is the disposition, not a promise to have one. It resolves a contradiction this contract used to carry
in two places at once: the membership rule above gave *"QFJ excludes 1156, QuickFIX-cpp includes it"*, while
C-6's failing behaviour called an engine-driven exclusion the violation — i.e. that 1156 is body. Both
cannot hold.

**Why union and not intersection**, so the next editor does not re-litigate it:

- The QuickFIX-cpp emitter closes the gap by **adding** a tag to a static exclusion set — one line. The
  intersection rule would instead require the QuickFIX-J emitter to **recover** a field its parser has
  already routed into the header `FieldMap` and re-inject it into `fields` — more code, and it would have
  to be re-decided for every future built-in divergence.
- `ApplExtID(1156)` is not a FIX 4.4 field at all, so under this feature's own `UseDataDictionary=Y` cells
  the intersection rule would put an **undefined tag** into the fidelity comparison set on one engine only.

**Re-derivation, not a copy.** The two built-in lists are what move. Re-derive them by opening
`quickfix-cpp/src/C++/Message.cpp`'s `Message::isHeaderField(int)` switch and
`quickfixj-base/src/main/java/quickfix/Message.java`'s `isHeaderField(int)` switch and taking the set
difference; the emitters' exclusion set is generated from that difference plus the dictionary's `<header>`
block, never hand-listed. `1156` is the one member of that difference this contract pins by name, because
it is the one the cross-engine fixture must contain and the one the library already tracks as open work
(`L-074-1`, live).

**C-6 is restated accordingly**: a violation is a field whose classification differs from **the canonical
partition above**, in *either* direction — a partition-header field appearing in `fields`, or a
partition-body field excluded because one engine's built-in list happens to contain it.

FR-019's re-capture obligation covers set **membership**, not only serialization and ordering.

## Conformance obligations

| # | Obligation | Failing behaviour |
|---|---|---|
| C-1 | `hello` present and first | absent ⇒ cell **FAILS** (not skip) |
| C-2 | `readback_protocol` ≥ what the cell requires | older ⇒ cell **FAILS** |
| C-3 | Every body field the peer parsed appears | a `NoXxx` count with no member at that path is **malformed** |
| C-4 | Group instances carry a path, not just a count | see C-3; ⚠️ C++ trap: instances live in `m_groups`, the count in `m_fields` |
| C-5 | Enumeration does not mutate the message | ⚠️ Java trap: `getGroups(int)` is `computeIfAbsent`; drive from `groupKeyIterator()` |
| C-6 | Header/trailer excluded **per the canonical partition specified above** — union of both built-in lists ∪ the dictionary's `<header>` block; tag `1156` is **header** | a **partition**-header field appearing in `fields` is a violation, and so is a **partition**-body field excluded because one engine's built-in list happens to contain it. ⚠️ Both directions, judged against the partition — never against either engine's own classifier |
| C-7 | Both emitters **byte-identical** for identical input — that is the assertion, not the weaker *"parses to the same records"* | one consumer, both producers, **one committed cross-language golden fixture** — containing at least one nested multi-instance group, one non-UTF-8 value, one value carrying each escape class, and **tag 1156**. ⚠️ The fixture is produced by **invoking each emitter directly on a constructed message**, not by running a live cell: `ApplExtID(1156)` is not a FIX 4.4 field, so under this feature's own `UseDataDictionary=Y` cells with validation on a message carrying it would be rejected before any readback existed |
| C-8 | `sent` records emitted by both sides, with **`fields` from builder inputs** | a `sent` record whose **`fields`** are derived from the serialized frame is a violation (data-model §3). ⚠️ Reading **`MsgSeqNum(34)` and direction** from the outbound seam is **required** by the two-stage rule and is **not** a violation — the restriction is on *what* is read there, not on *where* |
| C-9 | Every **`sent`** and **`readback`** record carries `occurrence`; every **`sent`** record carries `script_step_id` | a repeat of `(seq_num, direction)` with no distinguishing ordinal is a violation. ⚠️ Scoped: the mandatory `hello` (data-model §1) and `terminal` (§12) schemas have neither, so *"every record"* contradicted the record grammar. A **readback** carries no `script_step_id` at all — nothing puts a step identifier on the wire — and inherits it from the paired `sent` record after correlation |
| C-10 | Each stream carries a `hello` **first** and a `terminal` **last**, whatever the outcome | a stream with a `hello` and no `terminal` is an incomplete run, never a pass — the pre-conversation hello alone does not corroborate anything |

**C-4 and C-5 are silent failures.** Neither is caught by a green run: C-4 yields a populated-looking
record missing exactly the structure under test, and C-5 corrupts the subject while observing it. Both
need their own witness — and both are **only reachable because FR-008b puts a multi-instance nested group
in the script**. A script declaring no field content leaves this contract's most load-bearing anti-vacuity
clause unsatisfiable from the script it derives from.

## Witnesses this contract requires

Every one of these is a *"needs a witness"* clause above; they are listed here so `quickstart.md` Step 4
can be checked against a closed set rather than against prose.

| # | Witness |
|---|---|
| C-2 | a `readback_protocol` **older** than the cell requires ⇒ cell FAILS |
| C-3 / C-4 | group **instances** present with their paths, not just the `NoXxx` count |
| C-5 | enumeration does not mutate the message (the QFJ `computeIfAbsent` trap) |
| C-6 | a **partition**-header field appearing in `fields` is rejected, **and** a partition-body field excluded because one engine's built-in list contains it is rejected — both directions. Tag 1156 is the pinned case: it is **header**, excluded by both emitters |
| C-10 | a stream carrying a `hello` and no `terminal` ⇒ the run is incomplete, never a pass |
| sort order | two emitters given the same constructed message produce **byte-identical** records; a walk-order-dependent emitter fails |
| C-7 | the cross-language golden fixture parses identically from both producers |
| escaping | each byte class, **both** writers, including a non-UTF-8 value routed to `value_b64` |
| C-8 | an intent/sent record derived from the serialized frame ⇒ RED (FR-018's first spurious-hit arm) |
| R-4 | stale **append-mode** readback ⇒ RED — this is a load-bearing decision, not a formatting choice |
