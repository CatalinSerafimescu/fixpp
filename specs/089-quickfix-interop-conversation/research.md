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

**Why it matters.** The two ceilings are different quantities (see `plan.md` § Disk preflight): free space
*inside* the VHD bounds total resident footprint, while free space on the Windows host bounds how much the
VHD may still **grow**. If a build lands in blocks freed by a prior reclaim it may cost the host nothing; on
a VHD with no internal free blocks it costs 1:1.

⛔ **No reading is recorded here, deliberately.** Both move — one reclaim shifted them within the hour on
2026-09-10 — and this item exists precisely to replace inferred numbers with measured ones. Re-derive at
execution time: `df -k /` (build mount), `df -k /mnt/e` (host), `du -sh build/*/` (trees). `plan.md`
§ *The instrument that fails toward clean* carries the one dated illustration kept as motivation; it is
**never an operand**.

⚠️ **A reuse pool can be BOUNDED by comparing the materialised VHD file size against the space used
inside it, and that bound must never be mistaken for the answer.** Writes landing in already-materialised
blocks cost the host nothing; writes beyond them force VHD growth. **The bound is not a measurement**: ext4
does not preferentially allocate into already-materialised extents, so an allocator that picks fresh extents
converts a "free" write into 1:1 host growth. **Computing that bound and substituting it for a measurement
is precisely the reasoning this item exists to replace** — no task may do so, and R-1 stays mandatory.

⚠️ **Measure the TARGETED build, because that is the unit the matrix runs.** `run_interop_cell.py` builds
nothing — it expects a pre-built tree and runs one named gtest binary per cell — so `plan.md` fixes the
unit as *the interop driver targets only*, never `all`. Measuring a full build would derive thresholds for
a build this feature never performs, and it fails in the **expensive direction**: a threshold sized for the
full tree refuses targeted builds that would have succeeded.

⚠️ **ccache is out of scope for both predicates.** `CCACHE_DIR=/mnt/wsl/fixppbuild/ccache` is on
`/dev/sde`, a separate 64 G VHD whose backing file is not on `E:`, while `/` is `/dev/sdd`. Its growth
consumes neither ceiling, and sampling it into either predicate would inflate both.

**The experiment**: build **each** of the four configurations **targeted** — ⚠️ *amended 2026-09-11 (user
decision): incrementally, in the existing per-configuration trees, as the matrix performs it; not from
clean* — while sampling
**both** `df -k /` and `df -k /mnt/e` before, during and after; record the delta in each. Derive **two**
per-configuration values with headroom, and record each **with its date**:

| Value | Derived from | Compared against | Bounds |
|---|---|---|---|
| `required_internal_free` | the peak **build-mount** occupancy observed for that configuration | the build-mount reading | total data resident at once |
| `required_host_growth` | the observed **host delta** for that configuration | the host-mount reading | net new allocation the VHD may still need |

⚠️ **These are two different quantities and one may not stand in for the other.** Deriving a threshold
from the **host delta** and applying it to the **build-mount** reading authorises a large build on a VHD
with almost no internal free space — the exact ENOSPC the gate exists to prevent, produced by the gate's
own arithmetic. See `contracts/disk-preflight.md` D-1, and `plan.md` for the dated illustration.

⚠️ **Four configurations, not one.** This research item previously mandated measuring **one**
configuration while `plan.md` promised per-configuration budgets across the whole span. Under FR-021's
four-config matrix all four are built and run, so all four are measured — `linux-clang-ubsan` included; it
is a **required arm** now, not merely the first thing to delete.

⛔ **Do not treat `linux-clang-ubsan` as cheap on the strength of its directory size.** A tree that was
configured but never fully built is small *because it is empty*, not because that configuration is cheap —
and its size is the first thing a planner reads. **Probe populated-ness before believing any tree size**:
compare its object and executable counts against a known-complete tree —
`find build/<preset> -name '*.o' | wc -l` and `ls build/<preset>/bin | wc -l`. A tree an order of magnitude
below its siblings is unpopulated. What makes the fourth configuration affordable is the *targeted* build
unit, never a directory size.

⚠️ **This is the one place where an unmeasured guess would reproduce the exact defect the gate exists to
prevent.** Do not let a plausible number stand in for the measurement — and note that an unset or
unparseable threshold must be a hard error, never a `0` that makes `proceed` true while measuring nothing
(D-9, arm A-7).

**Alternatives considered**: assume 1:1 (safe, but may make the largest sanitizer configuration look
impossible when it is not); assume reuse (unsafe — this is the failure being designed against).

---

## R-2 — How does each counterparty get typed, dictionary-backed access? ✅ DECIDED

**Decision**: use each engine's **direct typed conversion**, not its `MessageCracker`.

⚠️ **Typed conversion alone contributes NOTHING to what R-8 enumerates, and the bundle previously let it
sit inert.** The evidence below says why: the `FIX44::` classes *"add **no data members** — they are
`FIELD_SET(...)` macros over `Message`"*, so the converting constructor is a copy that re-parses nothing,
a typed accessor resolves to `getField` on the same `FieldMap` the generic walk iterates, and group parsing
is decided **at parse time by the dictionary**, not by the wrapper. The typed conversion could have been
deleted and every witness would still have passed.

**What makes it worth having is a different assertion, and FR-003b now requires it.** A typed accessor is
not a *value* check but a **schema-conformance** check: `NewOrderSingle::get(Symbol&)` compiles only if
`Symbol` belongs to `NewOrderSingle` in FIX 4.4, and `FieldNotFound` fires if the peer did not receive it.
Generic enumeration cannot make that assertion at all; typed access cannot see missing/spurious fields or
group shape. **Both are required, and the script declares which fields go which way (FR-003b).**

⛔ **THE ANTI-VACUITY ARM IS COMPILE-TIME, and the runtime-bypass arm this item used to name is
SUPERSEDED — do not derive a task from it.** The deleted text read *"an arm bypasses the typed accessor
while leaving enumeration intact and requires RED"*. That arm is **structurally unsatisfiable**: the
generated getter returns **the caller's own object** — `getTag()` is caller-set before the call and
`getValue()` is the wire string — so no serialized value can witness that the accessor was invoked, and
every artifact-level observable proposed for it was synthesizable without calling it (user decision, Gate A
fresh loop round 1; `spec.md` § *Clarifications* → *Session 2026-09-10 (Gate A fresh loop, round 1)*).

**The decision that replaces it**: a **negative-compilation** arm. Mutate the counterparty **source file**
so a declared typed read calls the generated per-message accessor with a field that message does **not**
declare in FIX 4.4; the build of that file MUST fail, with the failure matching the expected
**missing-overload diagnostic** rather than merely a non-zero exit. Both directions are asserted — the
unmutated file compiles, the mutant does not. **Stated scope**: this proves the *schema-conformance* check
is really made; it does **not** prove runtime invocation, which is recorded as structurally unsatisfiable
under SC-003's one named exception. ⚠️ Do **not** propose a fourth artifact-level observable for runtime
invocation. The mechanism lives in `plan.md` § *External obligations*; the requirement is FR-003b.

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
contract.** FIX values may contain `"` and `\`, and data fields may contain **arbitrary bytes including
SOH and control characters**. A naive writer emits invalid JSON on precisely the messages most worth
inspecting. The rule must cover `"`, `\`, every byte `< 0x20` as `\u00XX`, and must state a decision for
non-UTF-8 bytes. **Each of the three emitters must be tested against a value containing each class**
(`contracts/readback-jsonl.md` § *THE THREE EMITTERS*: the QuickFIX-cpp counterparty, the QuickFIX-J
counterparty, and fixpp), or the escaping is
an untested claim.

**✅ The non-UTF-8 decision, taken at Gate A round 1 — it could not be deferred.** This item previously
handed the decision to the contract and the contract handed it back (*"the contract MUST state a single
decision"*, unmet, inside the artifact whose job is to be the decision). It cannot go to implementation
either: Java `String` is UTF-16 and C++ `std::string` is bytes, so two independent implementations of an
unstated rule **diverge**, and FR-004 breaks.

**Decision**: a field entry whose raw bytes are **not valid UTF-8** carries `value_b64` — base64
(RFC 4648 standard alphabet, `=` padding, no line breaks) of the raw bytes as received — and **no `value`
key**. Exactly one of the two is present on every entry. Base64 is byte-exact, has one canonical alphabet,
imposes no encoding assumption on either language's string type, and is trivially producible by a
hand-rolled writer in both. Alongside it the contract now pins a **canonical form** — fixed key order, no
insignificant whitespace, lower-case `\u00XX` hex, no optional escapes — because C-7's byte compatibility
admits exactly one spelling, and a **cross-language golden fixture** (one consumer, **all three emitters**, one
committed expected artifact) so C-7 is exercised rather than asserted.

⚠️ **Read the motivating field list with R-10 in hand**: `XmlData(213)` is a built-in **header** field on
both engines and `SecureData(91)` becomes header under `UseDataDictionary=Y`, so two of the three fields
named above are excluded from `fields` by C-6 under this feature's own configuration. `RawData(96)`
remains reachable. The rule and its witness stand; the justification does not rest on excluded fields.

**Alternatives rejected**: adding a JSON library (a new third-party dependency in two languages, for a
writer of this size); snakeyaml flow-style on the Java side only (YAML is a JSON superset, but it is a
workaround, and it would make the emitters structurally different — FR-004 requires one format across all
three).

---

## R-4 — Where does the readback record go, and what must the shim change **for collection**? ✅ DECIDED — **nothing for collection**

⚠️ **Headline narrowed at Gate A round 1.** It previously read *"what must the shim change? ✅ DECIDED —
**nothing**"*, which is true of **collection** and false of everything else. The body already said so
(*"a sibling file lands in the right place with **no collection change**"*, and the impact table's
*"**Zero** `run_interop_cell.py` changes **for collection**"*), but the headline is what gets quoted.
**Consumption** and the **FR-016a refusal** both require shim changes — decided in **R-4a** below.

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

**Alternatives rejected — for the readback file's PATH**: an `argv[3]` path (needs the C++ arg guard at
`interop_counterparty_main.cpp:227` relaxed); an env var (needs a new `cp_env` key in
`launch_counterparty`). Both cost changes the sibling approach does not, **for that purpose**.

⚠️ **REVERSED at Gate A round 2, for the METADATA handoff — and the rejection reason was never the cost it
was priced at.** FR-013b requires the counterparty's hello to carry `run_id`, the script digest, the
configuration name and the counterparty image digest. **The counterparty has no channel to receive any of
them**, so it was required to emit four fields it cannot obtain — a defect this item's own rejection
created. But *"needs a new `cp_env` key"* is not a cost: `launch_counterparty` **already assembles a
`cp_env` dict carrying ten `INTEROP_CP_*` knobs** and already passes it as `env=cp_env` to **both** the C++
and the Java branch. Adding six metadata keys is one more block in an existing pattern.

**Decision (round 2):** `INTEROP_CP_{RUN_ID, CELL_ID, CONFIG, IMAGE_DIGEST, SCRIPT_PATH, SCRIPT_DIGEST}` are
added to that block. The hello copies the first four verbatim and **recomputes** `script_digest` over the
file it opened — lowercase-hex SHA-256, computed identically by the shim (`hashlib`), the C++ counterparty
(OpenSSL, already linked) and the Java counterparty (`MessageDigest`), so **no new dependency on any side**
and R-3's Article III/V `PASS` is preserved. The shim compares before launching the gtest. A verbatim copy
of the digest would prove only that the counterparty can echo a string. See data-model §1.

⚠️ The sibling-file decision for the readback **path** is unaffected and stands; what is reversed is the
blanket rejection of `cp_env` as a channel.

---

## R-4a — Who CONSUMES the readback stream, and where does the hello gate live? ✅ DECIDED (Gate A round 1)

R-4 leaves the record on disk in the right place. Nothing in the bundle said which process reads it, how
the path reaches that process, or where the FR-016a refusal executes — and *"some handoff must be added"*
is not a design decision.

**Decision — three parts:**

1. **The fixpp-side gtest performs the field comparison**, because it is the only process holding fixpp's
   own sent and typed-read records. It receives the run's readback path through a **new environment
   variable**, set alongside the ones `run_interop_cell.py` already sets on the gtest — the same
   mechanism, not a new one. Both cell branches build an `env` dict carrying
   `INTEROP_<TOKEN>_PORT` / `_HOST`, `INTEROP_FIXPP_PORT` (acceptor branch), `FIXPP_TLS_FIXTURE_DIR` and
   `FIXPP_FIX44_DICT_XML`; the readback path is one more key of exactly that kind, and `run_dir` is in
   scope at both sites.
2. **The FR-016a hello gate runs shim-side, BEFORE the gtest is launched.** *"The harness MUST refuse to
   **run** a cell against a peer that announces nothing"* and R-7's *"the harness reads it **before the
   conversation**"* both place the check before the conversation starts — and at that moment only the shim
   is running. This is a `run_interop_cell.py` change, and R-4's *"nothing"* never covered it.
3. ⚠️ **The refusal MUST NOT be phrased in the `unavailable:` vocabulary.** `parse_gtest_status` greps
   `unavailable: .*` out of gtest stdout and returns `skip:<reason>`, and `probe_counterparty` already
   emits `"<cp> unavailable: …"` strings into that channel. A hello check implemented gtest-side in the
   idiom already present in that file would record a **present-but-stale peer** as `skip:` — exactly the
   collapse data-model §1 warns against (*"Collapsing the two reintroduces exactly the silence this feature
   exists to remove"*), reached by imitation rather than carelessness. Placing the gate shim-side, ahead of
   the gtest, removes the temptation structurally: there is no gtest stdout to grep yet.

**Alternatives rejected**: comparing shim-side in Python (the shim has no access to fixpp's builder inputs
or its typed reads, so it cannot form either sent record); passing the path via the counterparty's `cp_env`
(wrong process — the counterparty writes the file, the comparator reads it).

---

## R-5 — How widely is `UseDataDictionary=Y` enabled **on the PEER**? ✅ DECIDED — **per-cell, not globally**

⚠️ **Scope stated explicitly at Gate A round 1.** Everything in this item is **peer-side** evidence —
`DefaultSessionFactory`, `MessageSessionUtils`, `DataDictionaryProvider` — so it governs **FR-002** and
says nothing about FR-001. FR-001 governs fixpp's own `SessionConfig::dictionary`
(`tests/interop/happy/hp_support.hpp` — `c.dictionary = fixpp::test_support::make_minimal_dictionary();`),
a different mechanism on a different side. See **R-5a** for the half this item does not cover.

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

### ⛔ The seam that makes "per-cell" implementable — decided at Gate A round 2

*"Per-cell, not global"* had no structural seam, and `plan.md` § Project Structure read
`configs/*.cfg.in  # UseDataDictionary=Y for the FIX 4.4 cells`, which is the **global** flip this item
declines. Verified in the tree: `quickfix-cpp-{initiator,acceptor}-tls.cfg.in` and
`quickfixj-{initiator,acceptor}-tls.cfg.in` **all** carry `UseDataDictionary=N`, and
`quickfixj-acceptor-tls.cfg.in` is named by the idle-cadence cells and by the `PD-*` cells — exactly the
cells this item protects. Editing those templates flips them; not editing them cannot satisfy FR-002.

**Decision**: `config_template` is already a **per-cell** attribute selected in `run_interop_cell.py`, so
the eight new cells name **four new dedicated conversation templates** carrying `UseDataDictionary=Y`. The
existing `*-tls.cfg.in` templates are **not edited**, and a regression check asserts a protected cell still
renders `UseDataDictionary=N`.

⛔ **A "narrowly scoped renderer override" was rejected**: the per-cell `config_template` seam already
exists, and a renderer override is machinery for a problem the harness has already solved.

**A C++-only alternative, noted and not chosen**: `FIX::DataDictionary(path)` can be constructed
standalone (`DataDictionaryProvider.h:54`) purely for tag→name/type, leaving session validation
untouched. It gives naming with zero behavioural change — but it has **no clean QFJ equivalent for group
parsing**, because groups are decided at parse time inside the session. Using it would make the two
engines structurally different, which FR-004 forbids.

---

## R-5a — Does the same collateral-drift argument apply to the **fixpp** side? ✅ DECIDED — **yes, and FR-001 is scoped accordingly**

R-5's collateral-drift reasoning was never applied to the side R-5 does not cover, and it applies there
with equal force.

**The mechanism.** Swapping `SessionConfig::dictionary` from the FIX 4.2 single-Heartbeat sentinel to the
production FIX 4.4 dictionary changes **fixpp's own inbound parse** for those cells: group detection is
dictionary-driven on the read path, so an existing cell would begin seeing repeating-group structure where
it previously saw flat fields. That is the same class of change R-5 declines to make peer-side, and it is
unbudgeted if FR-001 is read as *"every live FIX 4.4 interop cell"*.

**Decision**: **FR-001 is scoped to the cells this feature adds**, exactly as FR-002 is. Existing live
cells keep their current dictionary on both sides and are covered by **FR-020**'s regression obligation
instead. US1's Independent Test is narrowed to match — it previously required running *"the **existing**
cells"* with the real dictionary on both sides, which asked for both flips this item and R-5 decline.

**Consequence for FR-019.** If a later feature does widen the scope, the golden re-captures it triggers are
not only about serialization and ordering: the dictionary flip changes the **membership** of the field set
under comparison (see **R-10**), so a re-capture must be reviewed as a set change, not a formatting change.

---

## R-1a — The gate cannot be calibrated under its own precondition ✅ DECIDED (Gate A round 2) — **bootstrap mode**

R-1's experiment is four clean targeted builds. `plan.md` makes the disk gate normative for *"every
build-bearing task"*, D-9 makes an unset threshold a hard error, and D-7 sources both values from R-1's
measurement. So R-1 cannot run: it needs thresholds that only it can produce. R-1's own fallback
(*"until R-1 has run, tasks assume the pessimistic model"*) was stated in this file and **forbidden** by
D-7/D-9 in the contract — one artifact permitting what another refuses.

**Decision**: `ci/disk-preflight.sh --bootstrap` (D-9a) accepts thresholds carrying the literal label
**`estimated, pending R-1`** with a date, taken from the pessimistic model (host cost = build size), and
prints that label on **every** output line. Admissible for the R-1 experiment and nothing else; any other
task invoking it is a violation that is **visible in the log** rather than inferable. The path is dead once
R-1's measured values land. One clause, and it fails loudly.

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

## R-10 — Where exactly is the header/body boundary, and does it move? ✅ DECIDED (Gate A round 1) — **it moves, and the two engines already disagree**

The bundle treated the exclusion as an enumeration (`8, 9, 35, 34, 49, 56, 52, 10`) in `spec.md` and as a
structural rule in `data-model.md` and `readback-jsonl.md` C-6. The structural rule is the correct one, and
resolving it turns up two consequences neither artifact had checked.

**Membership is `built-in list ∪ dictionary-declared header`, on both engines.**

- `quickfix-cpp/src/C++/Message.cpp` — `Message::isHeaderField(int field, const DataDictionary *pD)`
  returns true if the field is on the built-in `switch` list, and otherwise consults `pD` when one is
  present. (Spelled as an `if`-chain, not a single `||` expression — the behaviour is the disjunction, the
  source text is not.)
- `quickfixj-base/src/main/java/quickfix/Message.java` — the same rule, spelled as one disjunction:
  `isHeaderField(field.getField()) || (dd != null && dd.isHeaderField(field.getField()))`, over its own
  built-in `switch`.

⚠️ **Re-derive both by opening the two `isHeaderField` overloads** rather than trusting the paraphrase
above; the built-in lists are what move, and no line number is given here for that reason.

**(a) The body set SHRINKS when US1 turns the dictionary on.** `FIX44.xml`'s `<header>` block declares
fields that are **not** in QuickFIX-cpp's built-in list — `SecureData(91)` and the `NoHops` members among
them. Under `UseDataDictionary=N` those are **body** and appear in `fields`; under `=Y` they are **header**
and C-6 excludes them. So FR-006's exact-set equality ranges over a *different set* before and after the
flip this feature mandates, and any golden or expected set fixed against today's `=N` behaviour is wrong
afterwards. **FR-019's re-capture obligation must be read as covering set membership**, not only the
peer's serialization or ordering.

**(b) The two built-in lists are not identical, so FR-004/C-7 is violated by the engines themselves.**
QuickFIX-J's list contains **`ApplExtID(1156)`**; QuickFIX-cpp's does not. For identical bytes carrying tag
1156, QFJ classifies it **header** (excluded) and QuickFIX-cpp **body** (included) — two emitters, two
different `fields` sets, silently, with no configuration involved. (Tag 1156 is the field the library
records as open work under 074's `L-074-1`, so it is a live case, not a hypothetical.) Format identity is
therefore **not free**: the contract must **specify the partition itself** where the built-ins disagree
rather than delegating it to each engine, and C-7's cross-engine fixture must contain tag 1156.

**Decision**: state the rule structurally in FR-003 with the eight tags as an illustrative subset; pin the
reconciliation in `contracts/readback-jsonl.md`; put tag 1156 in the cross-engine fixture.

---

## R-11 — Republishing the counterparty image moves `:latest`. What rides it? ✅ DECIDED (Gate A round 1) · ⚠️ **PRESCRIPTION SUPERSEDED 2026-09-10 — see `spec.md` FR-026**

`plan.md` § Structure Decision gives the ordering — *parent counterparty change → image rebuild + publish
→ digest captured → library-side cells pinned to that digest* — and it is sound as far as it goes. FR-016b
pins **this feature's** cells to a digest. Nothing addressed what is **not** pinned.

- `.github/workflows/interop-smoke.yml`'s `IMAGE:` key names
  `ghcr.io/…/fixpp-interop-counterparties:latest` — the only tag that workflow knows.
- `spec.md` § Assumptions requires the image to be **rebuilt and republished**, because this feature
  changes both counterparty applications.

If the rebuild publishes to `:latest`, **every existing interop consumer immediately runs new counterparty
code**, with no digest pin and no gate:

- The cells **FR-020** requires to keep passing — they would be exercised against a rebuilt peer, a change
  FR-020 does not anticipate and cannot attribute.
- The cells **R-5 protects**: `INTEROP_CP_CORRUPT_ADMIN`, whose entire design is that QFJ does not
  validate, and the `PD-*` malformed-dup cells. R-5 carefully declines a global `UseDataDictionary` flip to
  avoid disturbing them; the republish disturbs the binary underneath them anyway, since the readback
  emitter is new code on the inbound path of **every** message those cells send.
- The **smoke workflow itself**, which is a required check.

**Decision** ~~pin `interop-smoke.yml` and the existing cells to the **pre-089 digest before republishing**,
so `:latest` moving is inert. It costs one line in the workflow. The alternative — publish under a new tag
and move `:latest` only after FR-020's regression run is green — is acceptable but leaves a window in which
`:latest` and the pinned digest disagree.~~ Recorded as **FR-026**.

⚠️ **SUPERSEDED 2026-09-10 (user decision). `spec.md` FR-026 is the normative home and now mandates the
OPPOSITE ordering — publish → verify → depend, with NOTHING pinned.** Both options struck above were rejected
on measurement, so this is not a choice between them: pinning the existing cells to the pre-089 digest
**obstructs the verification it exists to enable**, because FR-020 asks whether those cells still pass *against
the new counterparty* and they cannot exercise it while pinned away from it; publishing under a new tag defers
the `:latest` move to a step nothing forces, and fails quietly. ⭐ **R-11's CONCERN survives and is exactly what
the new ordering discharges** — nothing should silently depend on an unverified image. Only its prescription is
dead. ⛔ Do not re-derive a pin from the struck text above.

---

## Consolidated impact on the plan

| Item | Effect |
|---|---|
| R-2 | Typed access is **free**. The feature's cost is the readback *channel* and the *dictionary*, not typed parsing. FR-003b makes typed accessors a **required** assertion distinct from generic enumeration, so the wrapper is no longer inert |
| R-3 | Article III / Article V watch items resolve to **PASS** — no new dependency. The escaping rule, the **non-UTF-8 `value_b64` decision**, the canonical form and a cross-language golden fixture all become contract clauses with their own witnesses |
| R-4 | **Zero** `run_interop_cell.py` changes **for collection**; goldens inert by construction. Truncate mode is load-bearing |
| R-4a | Consumption and the FR-016a gate are **not** free: one new env var to the gtest, and a shim-side pre-conversation hello gate that must avoid the `unavailable:` idiom |
| R-4 (reversed, round 2) | The **metadata** handoff to the counterparty uses the existing `cp_env` block — six `INTEROP_CP_*` keys. R-4's rejection of an env var was priced on a cost that does not exist; without it the counterparty must emit four hello fields it cannot receive |
| R-1a | The disk gate's calibration circularity is closed by a **labelled bootstrap mode** (D-9a), admissible only for R-1's own experiment |
| R-5 | FR-002 is **per-cell** (peer side), and a global flip is an explicit non-goal that would drift unrelated goldens. The **seam** is four new dedicated conversation `config_template`s; the existing `*-tls.cfg.in` templates are not edited |
| R-5a | FR-001 is **per-cell** on the fixpp side too, for the same reason. US1's Independent Test narrows to this feature's cells |
| R-7 | FR-016a is new capability, distinct from the existing availability probe: *unavailable* = skip, *wrong version* = **failure** |
| R-8 | Two silent traps promoted into the contract, each needing a witness — reachable only because FR-008b puts a nested group in the script |
| R-10 | The header/body partition is **specified in the contract**, moves under US1's flip, and needs a tag-1156 reconciliation fixture |
| R-11 | The `:latest` blast radius is closed by **publishing first, running FR-020's regression against the new image, and only then letting anything depend on it** — ⛔ **nothing is pinned**, and no pull request touching the consumer paths is opened in between (FR-026, which **supersedes R-11's own pin-first prescription**) |
| R-1, R-6, R-9 | Remain open **deliberately**, each with a mandated measurement rather than an assumption. R-1 now measures **four** configurations and **two** predicates each |
