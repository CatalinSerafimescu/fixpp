# Phase 0 Research — 089 live QuickFIX interop fidelity

**Date**: 2026-09-10 · **Branch**: `089-quickfix-interop-conversation`

**Version authority.** Both reference engines are CodeGraph-indexed as their own roots and both are
**exactly the pinned versions** — `quickfix-cpp` `git describe` = `v1.16.0` (`configure.ac:6`),
`quickfixj` = `QFJ_RELEASE_3_0_1`, matching `phase-9-harness/quickfixj/pom.xml:12`. No version skew, so
findings below describe the builds that actually ship in the counterparty image.

---

## ★ The headline: two things this feature treated as one are independent

- **Typed access is FREE on both engines and works today.** No config change, no new dependency.
- **Dictionary-backed access — tag→name, tag→type, and above all *repeating-group structure* — is a
  separate mechanism, and it is OFF in exactly the cells that matter.**

Verified in the *rendered* configs, not in the templates:
`results/BM-QFcpp-init-fix44-nos-execrpt/counterparty.cfg` and the QFj twin both carry
**`UseDataDictionary=N`**. Only the eight FIXT templates set `Y` — and those are logon/heartbeat/logout
admin cells, not business-message cells.

**Consequence.** On today's app-message cells, repeating groups are **not parsed as groups** on either
engine: every group member sits flat among the body fields and the count field is indistinguishable
from an ordinary `int`. Both engines gate group parsing on a non-null dictionary —
`quickfixj Message.java:707-708`, `quickfix-cpp src/C++/Message.cpp:343,381`. So FR-003's
"nested group instances with their path" is **unreachable** until the dictionary is on.

---

## R-1 — Disk: does a build cost host space 1:1, or reuse freed VHD blocks? ⏳ **OPEN — measurement mandated**

**Decision**: **not decidable by reasoning; it must be measured.** Until it is, every task assumes the
**pessimistic model** (host cost = build size).

**Why it matters.** The two ceilings are different quantities (see `plan.md` § Disk preflight): 83.0 GiB
free inside the VHD vs 16.9 GiB of host growth on `E:\`. If a build lands in blocks freed by a prior
reclaim it may cost the host nothing; on a VHD with no internal free blocks it costs 1:1.

**The experiment**: build one configuration from clean while sampling **both** `df -k /` and
`df -k /mnt/e` before, during and after; record the delta in each. Derive the per-configuration
threshold from the observed host delta with headroom, and record the figure **with its date**.

⚠️ **This is the one place where an unmeasured guess would reproduce the exact defect the gate exists to
prevent.** Do not let a plausible number stand in for the measurement.

**Alternatives considered**: assume 1:1 (safe but may make the 34 GiB ASan config look impossible when it
is not); assume reuse (unsafe — this is the failure being designed against).

---

## R-2 — How does each counterparty get typed, dictionary-backed access? ✅ DECIDED

**Decision**: use each engine's **direct typed conversion**, not its `MessageCracker`.

- **QuickFIX-cpp** — a converting constructor exists: `fix44/NewOrderSingle.h:13`
  `NewOrderSingle(const FIX::Message& m) : Message(m) {}`. So `FIX44::NewOrderSingle nos(message);`
  inside `fromApp` is sufficient. The downcast the cracker performs (`fix44/MessageCracker.h:463`) is
  sound only because `FIX44::` classes add **no data members** — they are `FIELD_SET(...)` macros over
  `Message`.
- **QuickFIX-J** — `instanceof quickfix.fix44.NewOrderSingle` + pattern-match cast.

**⭐ The counterparty's own comment is FALSE and must be deleted, not worked around.**
`InteropCounterparty.java:460` says *"QFJ has no Message→typed-msg ctor."* The inbound path calls
`messageFactory.create(...)` **unconditionally**, before and independently of any dictionary
(`MessageSessionUtils.java:71`); the counterparty already passes `new DefaultMessageFactory()`
(`InteropCounterparty.java:104`), which reflectively loads `quickfix.fix44.MessageFactory`
(`DefaultMessageFactory.java:92-94`), and `quickfixj-messages-fix44` is already a compile dependency
(`pom.xml:31-34`). **The object handed to `fromApp` is already a `quickfix.fix44.NewOrderSingle` at
runtime.** Lines 461-464 can be replaced outright.

**Cost**: **zero** — no config change, no new dependency, no new link deps. `src/C++/fix44/` contains no
`.cpp` files (header-only); the counterparty's `CMakeLists.txt:29-35` already links what is needed and
the `FIX44::` headers are already included (`interop_counterparty_main.cpp:17-19`) because it already
*builds* typed outbound messages (`:191`, `:108`).

**Alternatives rejected**:
- `quickfix.MessageCracker` (Java) — `MessageCracker.java:124` dispatches on **exact class identity**
  (`invokers.get(message.getClass())`), so `fix50sp2.NewOrderSingle` will not reach a `fix44` handler;
  it needs one overload per version, throws `RedundantHandlerException` on duplicates, and its
  unmatched fallback **throws** `UnsupportedMessageType` (`:157-160`) — a real regression risk for a
  counterparty that must tolerate arbitrary inbound types.
- `FIX::MessageCracker` (C++) — inherits all nine version crackers, drags in every generated header, and
  resolves ApplVerID via `Session::lookupSession` on the FIXT path. A `FIX44::MessageCracker`-only
  approach covers two of the three protocol variants in the matrix.

---

## R-3 — Serialization of the readback record. Does it need a new dependency? ✅ DECIDED — **no**

**Decision**: **hand-rolled JSON Lines**, one record per line. **No new dependency on either side** —
this resolves the Article III / Article V watch item in `plan.md` as a **PASS**, not a waiver.

**Verified absence** (this is why it is hand-rolled, not a preference): QuickFIX-J's `pom.xml` declares
quickfixj-core / messages-fix44 / fixt11 / fix50sp2, snakeyaml, HdrHistogram, slf4j, logback, junit,
assertj — **no Jackson, no Gson, no JSON-B**. The QuickFIX-cpp counterparty links only quickfix +
OpenSSL + Threads (`counterparty/CMakeLists.txt:29-35`) — **no JSON library**.

⚠️ **The escaping rule is a correctness requirement, not a formatting detail, and it belongs in the
contract.** FIX values may contain `"` and `\`, and the data fields — `RawData(96)`, `XmlData(213)`,
`SecureData(91)` — may contain **arbitrary bytes including SOH and control characters**. A naive writer
emits invalid JSON on precisely the messages most worth inspecting. The rule must cover `"`, `\`, every
byte `< 0x20` as `\u00XX`, and must state a decision for non-UTF-8 bytes. **Both writers must be tested
against a value containing each class**, or the escaping is an untested claim.

**Alternatives rejected**: adding a JSON library (a new third-party dependency in two languages, for a
writer of this size); snakeyaml flow-style on the Java side only (YAML is a JSON superset, but it is a
workaround, and it would make the two emitters structurally different — FR-004 requires one format).

---

## R-4 — Where does the readback record go, and what must the shim change? ✅ DECIDED — **nothing**

**Decision**: write `counterparty-readback.jsonl` as a **sibling of `argv[2]`** (the transcript path),
opened in **truncate** mode.

**Why this is free**: `launch_counterparty` (`run_interop_cell.py:989-1049`) passes `str(cfg),
str(transcript)` and the `run_dir` **is** the results directory — the shim never enumerates or copies
artifacts. So a sibling file lands in the right place with **no collection change**.

**Why it is inert to the goldens**: `golden_step` (`:734-753`), `_transcript_to_inrepo_golden`
(`:786-805`) and `normalize_transcript` (`:712-731`) all read **only** the passed transcript path. No
golden logic needs to learn to ignore the new file — it is invisible by construction.

⚠️ **Truncate, not append — and this is a real trap.** The shim explicitly truncates the transcript per
run (`run_interop_cell.py:869`, `:937`), while `shutil.rmtree` (`:853-855`, `:921-923`) clears only
*subdirectories*. A readback file opened in **append** mode (which is how both transcripts are opened —
C++ `std::ios::app` at `:232-241`, Java `StandardOpenOption.APPEND` at `:520-528`) would **silently
accumulate stale records across runs**, and stale records are worse than none: they would satisfy a
comparator looking for a witness that this run never produced.

**Alternatives rejected**: an `argv[3]` path (needs the C++ arg guard at
`interop_counterparty_main.cpp:227` relaxed); an env var (needs a new `cp_env` key in
`launch_counterparty`). Both cost changes the sibling approach does not.

---

## R-5 — How widely is `UseDataDictionary=Y` enabled? ✅ DECIDED — **per-cell, not globally**

**Decision**: enable the dictionary **only on the business-message conversation cells this feature
adds**. Leave every existing cell as it is.

**Why not globally.** Flipping the flag also turns **inbound validation** on — QFJ
`DefaultSessionFactory.java:161-170` creates the provider iff `UseDataDictionary`, and
`MessageSessionUtils.java:68` then sets `doValidation = payloadDictionary != null`; QuickFIX-cpp has the
same structure via `DataDictionaryProvider`. Several existing cells **depend on validation being off** —
notably `INTEROP_CP_CORRUPT_ADMIN` (`InteropCounterparty.java:202-221`), whose entire design is that QFJ
does not validate, and the `PD-*` malformed-dup cells. A global flip changes counterparty behaviour and
drifts their goldens — collateral damage with no benefit to this feature.

**⚠️ `ValidateIncomingMessage=N` is NOT an escape hatch.** It gates only Session-level validation
(`Session.java:1055`); parse-time validation keys off the provider alone (`MessageSessionUtils.java:68`).
Do not plan around it.

**The keys to add** — note this is a single key, not the FIXT Transport/App pair:

| Engine | Key | Path resolution |
|---|---|---|
| QuickFIX-cpp | `DataDictionary=${QFCPP_SPEC}/FIX44.xml` | filesystem; `QFCPP_SPEC` substituted at `run_interop_cell.py:864`/`:932` from `INTEROP_QFCPP_SPEC_DIR` (default `reference-engines/quickfix-cpp/spec`) |
| QuickFIX-J | `DataDictionary=FIX44.xml` | **classpath** — verified present at the root of `quickfixj-messages-fix44-3.0.1.jar`. There is deliberately no `${QFJ_SPEC}` |

⚠️ **That asymmetry is real** and a config template that copies the C++ form to the Java side, or vice
versa, will fail at session construction rather than at review.

**A C++-only alternative, noted and not chosen**: `FIX::DataDictionary(path)` can be constructed
standalone (`DataDictionaryProvider.h:54`) purely for tag→name/type, leaving session validation
untouched. It gives naming with zero behavioural change — but it has **no clean QFJ equivalent for group
parsing**, because groups are decided at parse time inside the session. Using it would make the two
engines structurally different, which FR-004 forbids.

---

## R-6 — Is the Article IX §1 coverage gate applicable at all? ⏳ **OPEN — must be measured, not assumed**

**Decision deferred to implementation, deliberately.** The gate measures `include/fixpp/<mod>/*` +
`src/<mod>/*` with test files excluded. This feature's diff is expected to be tests + harness + parent
repo, in which case the module glob selects **nothing** and the gate is vacuous.

⚠️ **"Vacuous" must be established as a measured fact and recorded**, not assumed from the shape of the
work. A coverage gate that selects an empty set and reports success is indistinguishable from one that
passed — the repository's dominant failure class. The `/speckit-verify` record must state which files the
glob actually selected, even when the answer is none.

---

## R-7 — The FR-016a capability handshake seam ✅ DECIDED

**Decision**: emit a `hello` record as the **first line** of the readback JSONL, carrying engine,
engine version, and readback-protocol version. The harness reads it before the conversation and fails
the cell if it is absent or announces a protocol older than the cell requires.

**Why here**: R-4 makes the JSONL free to collect, so the handshake costs nothing extra. The
alternatives cost shim changes — **nothing reads counterparty stdout today**: `counterparty-stdout.txt`
is opened purely as a `Popen` sink (`run_interop_cell.py:992`, `:1025-1026`, `:1047-1048`) and is
**never read back**.

**What exists and what does not.** Readiness detection exists and is already wired to the verdict:
`probe_counterparty()` (`tests/interop/support/counterparty_probe.hpp:178`) checks env tokens and TCP
connectability and emits `"<cp> unavailable: ..."` strings that `parse_gtest_status`
(`run_interop_cell.py:645`) greps. **Identity and version reporting do not exist** — neither
counterparty prints a startup banner (C++ `main` `:226-286` prints only usage and errors; Java `main`
`:85-140` only errors, with `ScreenLogFactory(false, false, false)` at `:103`).

⇒ **The probe knows *whether* a peer is up, never *what* it is.** FR-016a is genuinely new capability,
and it must not be confused with, or folded into, the existing availability skip — *unavailable* is a
skip, *present but wrong version* is a **failure**.

---

## R-8 — Enumerating every field, including groups ✅ DECIDED — with two traps that must be in the contract

**Decision**: walk header, body and trailer separately; recurse into groups from the group map, never by
probing per tag.

**QuickFIX-cpp** (`include/quickfix/FieldMap.h`): scalars via `begin()`/`end()` (`:233-236`); groups via
`g_begin()`/`g_end()` (`:237-240`), each dereferencing to `pair<const int, vector<FieldMap*>>`; recurse
because every `FieldMap*` is itself iterable. `getTag()`/`getString()` at `Field.h:128,136`.
`Header`/`Trailer` are themselves `FieldMap` subclasses (`Message.h:41,69`), reached via `getHeader()`
(`:241`) / `getTrailer()` (`:245`); `Message` *is* the body map.

> ⚠️ **Trap 1 (C++)**: the group **count** field lives in `m_fields` while the instances live in
> `m_groups`. A naive `begin()..end()` walk emits `453=2` and **silently drops both instances** — a
> readback that looks populated and is missing exactly the structure this feature exists to check.

**QuickFIX-J** (`quickfix/FieldMap.java`): scalars via `iterator()` (`:452`); group tags via
`groupKeyIterator()` (`:609`); instances via `getGroups(int)` (`:653`). `Group extends FieldMap`, so
recursion is uniform. Path form `453[0].448`.

> ⚠️ **Trap 2 (Java) — worse, because it corrupts the subject**: `getGroups(int)` is
> `computeIfAbsent(field, k -> new ArrayList<>())` (`:653`). Calling it on a **non-group** tag **mutates
> the message**, inserting an empty list that then appears in `groupKeyIterator()` and can perturb
> `toString()`. Drive the walk from `groupKeyIterator()`; **never** probe `getGroups(tag)` per field.

Both traps are silent and neither is caught by a green run, so both belong in the readback contract and
each needs a witness.

---

## R-9 — TSan on the paired live matrix ⏳ **OPEN by design (FR-022)**

**Decision**: carry as bring-up, contained rather than resolved in advance.

TSan has never been run on the paired live matrix
(`phase-9-harness/INTEROP-COVERAGE-REPORT.md:53,140`). It may not come up at all: instrumentation moves
timing, and heartbeat/test-request cadence is exactly what a live session is sensitive to. FR-022
contains the risk by forbidding a vacuous green — a TSan arm that skips, dies in setup, or produces no
witnesses is a **failure**, and SC-011 requires the TSan arm to produce the **same witness set** as the
`normal` arm. If bring-up proves to be its own investigation, it is escalated as a filed issue, never
quietly downgraded to `n/a`.

---

## Consolidated impact on the plan

| Item | Effect |
|---|---|
| R-2 | Typed access is **free**. The feature's cost is the readback *channel* and the *dictionary*, not typed parsing |
| R-3 | Article III / Article V watch items resolve to **PASS** — no new dependency. The JSON escaping rule becomes a contract clause with its own witness |
| R-4 | **Zero** `run_interop_cell.py` changes for collection; goldens inert by construction. Truncate mode is load-bearing |
| R-5 | FR-002 is **per-cell**, and a global flip is now an explicit non-goal that would drift unrelated goldens |
| R-7 | FR-016a is new capability, distinct from the existing availability probe: *unavailable* = skip, *wrong version* = **failure** |
| R-8 | Two silent traps promoted into the contract, each needing a witness |
| R-1, R-6, R-9 | Remain open **deliberately**, each with a mandated measurement rather than an assumption |
