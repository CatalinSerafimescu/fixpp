# Data model: 097 — FIX Latest is ApplVerID 10

## §A — Version identities after 097

| ApplVerID wire value | `application_version` | Dictionary (`session_version`) that answers it | Registry slot |
|---|---|---|---|
| `"2"`..`"9"` | `v40`..`v50sp2` (unchanged) | the matching QuickFIX-XML dictionary | 1..8 (unchanged) |
| `"10"` | **`vlatest` (new, value 9)** | the Orchestra dictionary (`session_version::vlatest`) | **9 (new)** |
| `"0"`, `"1"`, empty-with-no-default, anything else | refused (unchanged errors) | — | — |

- `session_version::vt11` and `Unknown` still map to no slot.
- A slot is filled only by a dictionary whose session version maps to it. **Lookup, one-way fallback**
  (spec FR-006): exact slot first; an empty v50sp2 slot answers from the vlatest slot; no other fallback.
  | Registered | `get(v50sp2)` | `get(vlatest)` |
  |---|---|---|
  | {FIX50SP2} | FIX50SP2 | none |
  | {FIX Latest} | FIX Latest (fallback, as today) | FIX Latest |
  | {FIX50SP2, FIX Latest} | FIX50SP2 | FIX Latest |
  | {} | none | none |
- After 097 the enum has `Unknown` plus nine renderable versions, `v40`..`vlatest`. Render
  (`render_appl_ver_id`) is the inverse of parse over those nine, and its nine strings are pairwise
  distinct. `render(Unknown)` stays the error `dict_unknown_appl_ver_id`.
- **Which sessions negotiate FIX Latest.** A session negotiates `vlatest` exactly when the 1137 it
  negotiates is `"10"`: the peer's Logon 1137 as the acceptor reads it, or the Logon reply's 1137 the
  initiator records. A session configured `default_appl_ver_id = "10"` advertises `1137=10`. A session
  whose own dictionary is FIX Latest but whose `default_appl_ver_id` is `"9"` negotiates `v50sp2`,
  answered by the FIX Latest dictionary through the fallback. It reifies under the `v50sp2` arm.
- Config token (`default_appl_ver_id`): `"2"`..`"10"`, mapping as the table; the unknown-token diagnostic
  names `"2".."10"`.

## §B — What changes, by surface

| Surface | Change | Spec |
|---|---|---|
| `include/fixpp/dict/version_profile.hpp` | `vlatest = 9` enumerator; wire-table comment; `render_appl_ver_id` case `vlatest` → `"10"`; `session_version::vlatest` comment corrected | FR-001, FR-002, FR-012(c) |
| `src/dictionary/version_profile.cpp` | two-character arm, exactly `"10"` | FR-001 |
| `include/fixpp/dict/version_registry.hpp` | `kTableSize` 10; slot comment. `version_registry` (and `Engine`, which holds it by value) grows by one slot (B-097-2) | FR-005, FR-011 |
| `src/dictionary/version_registry.cpp` | `vlatest` → `vlatest`; FR-010 check + abort deleted; `get` gains the one-way fallback; header comment names 097 | FR-004..FR-006, FR-012(c) |
| `src/config/scalar_mappers.cpp` | token `"10"`; diagnostic text | FR-003 |
| `tools/codegen/fixpp-codegen/emit_dispatch.cpp` | `kAppVersions` row `{vlatest, vlatest}`, appended last (this keeps the bytewise order); an outer `case` is emitted only for a row whose IR is present (OFF tier: no `vlatest` case, falls to the outer `default`). Comments 097 makes false, each rewritten to derive from `kAppVersions` with no count and no closed version list (Gate A round 2, G097-016): the file-header comment's (version, MsgType) case count; the comment above `kAppVersions` ("three codegen application versions" and its sort example); `emit_dispatch_application`'s block comment (its version list, case count and outer-default list); and the generated header's two comment lines, the banner naming the dispatched versions and the outer `default:` comment listing the runtime-XML-only versions (under OFF, `vlatest` also falls there). A rewritten generated comment never contains the literal `application_version::vlatest:` (quickstart §1.4's grep and W-9(b) count that string) | FR-009, FR-012(c) |
| `tools/codegen/fixpp-codegen/ir.cpp` | `kCodegenVersions` vlatest `.a = vlatest`; comment | FR-012(c) |
| `tools/codegen/fixpp-codegen/gen_util.hpp` | `app_version_enum("vlatest")` → `"vlatest"`; guard and comment | FR-012(c) |
| `specs/076-…/contracts/golden/vlatest_Messages.golden.hpp` | regenerated: the vlatest classes' `application_version::v50sp2;` → `::vlatest;`, nothing else | FR-012(c), research R-4 |
| `tests/config/fixtures/pos_fixt_appl_ver_ids.toml`, `tests/config/test_load_happy_path.cpp` | the comment restating `"2".."9"` / an arm count points at the mapper instead | FR-012(c) |
| tests | see contracts §W | FR-010 |
| `.specify/2c-codegen.md` | §4.3 enum and wire table, §4.8 runtime-dispatch paragraph: in-place `Updated (097 …)` notes (§C) | FR-012(f) |
| records | §C, §D, §E, §F | FR-011, FR-012 |

No change: `src/session/*` (research R-6), the validator (`include/fixpp/wire/validator.hpp`,
`src/dictionary/fixt_framing_table.hpp`; owner ruling, no new rule), the C ABI (`include/fix/c_api/*`
carries the raw 1137 string), `bindings/`, `dictionaries/*.xml`.

## §C — Superseded statements and their replacements (FR-012(c), (e), (f))

Each rewritten comment states the current rule and names "097" (CLAUDE.md: a superseding decision names
the new one in a header comment). Re-derive the population at the implement head with quickstart §4's
union grep over the whole tree (code, `brain/`, `.specify/*.md`, `spec/`, the library `CLAUDE.md`). Every
hit is rewritten, or is a frozen `specs/` record, a historical constitution Sync Impact Report entry,
or the changelog `CLAUDE-history.md` (all excluded in the grep's own pathspecs and line ranges,
quickstart §4.1). The library `CLAUDE.md`'s "Also open, no issue filed: ApplExtID(1156)=303 …" line is
deleted **in implementation** (plan phase 6, Gate A round 2, G097-018), not at close-out, so W-11 sees it
gone at verify. The grep cannot see an
**omitted addition**, so quickstart §4's positive-presence checks (W-11) confirm each addition below.

- **`.specify/2c-codegen.md`** (the live owning design doc, `brain/components/dictionary.md`). Three
  in-place notes, in 2c's own `Updated (fixpp#458 …)` form, with the text before each kept as history:
  - at §4.3's `enum class application_version` sketch:
    `Updated (097-fix-latest-applverid-10): the enum gains vlatest = 9 (FIX Latest, wire "10"), appended after v50sp2; see include/fixpp/dict/version_profile.hpp.`;
  - at §4.3's "Wire `ApplVerID(1128)` → C++ `application_version` enum mapping" table:
    `Updated (097-fix-latest-applverid-10): "10" (FIX Latest; FIXLatest in FIX Session Layer §11.2 and the Orchestra ApplVerID codeset) → application_version::vlatest.`;
  - at §4.8's "Runtime-dispatch switch" paragraph:
    `Updated (097-fix-latest-applverid-10): the application switch gains a vlatest arm when the FIX Latest codegen tier is on; under FIXPP_CODEGEN_FIX_LATEST=OFF there is no vlatest case and it falls to the outer fail-loud default.`

  2c's "four codegen versions" statements elsewhere predate 097 (076 added `vlatest` without annotating
  them) and are not 097's to fix. `brain/components/dictionary.md` says 2c carries the 097 notes. 2c's
  C-ABI application-version constant set was never implemented and is untouched (plan Gate A,
  disagreements).

- Frozen records not edited (flagged in `brain/components/dictionary.md` instead):
  `specs/074-orchestra-native-reader/` (the reconcile, FR-010, L-074-1), `specs/076-…` prose describing
  `.a = v50sp2` / `app_version_enum` (the golden file is a test oracle and is regenerated, §B),
  `specs/080-orchestra-runtime-load/` (L-080-1).
- Parent records (not in this repo): REMAINING-WORK row 4b and the 4b detail file already carry the
  owner's re-scope (parent `610eb57e`); the close-out marks row 4b DONE.

## §D — B&L text

Live file, a new `## 097 — FIX Latest is ApplVerID 10 (2026-10-29)` section:

- **B-097-1 — FIX Latest is ApplVerID `10`, in both directions.** `DefaultApplVerID(1137)=10` on a FIXT
  Logon and `ApplVerID(1128)=10` in a header resolve to `application_version::vlatest`, answered by the
  FIX Latest (Orchestra) dictionary; a session configured with `default_appl_ver_id = "10"` sends
  `1137=10`. A session negotiates FIX Latest only through the value `10`. A session whose own dictionary
  is FIX Latest but which is configured `"9"` sends `1137=9` and negotiates FIX50SP2 (see B-097-4).
  A FIXT acceptor whose engine holds the FIX Latest dictionary establishes a FIX Latest peer (QuickFIX/J `DefaultApplVerID=FIX.Latest` sends `1137=10`); before 097 it answered Reject(373=5,
  371=1137). An engine built in C++ may hold the FIX50SP2 and the FIX Latest dictionaries together; each
  version is then answered by its own. An engine holding only FIX Latest still admits `1137=9` and still
  opens a session whose own default is `"9"` (the FIX Latest dictionary answers FIX50SP2 when no FIX50SP2
  dictionary is registered — never the reverse: an engine holding only FIX50SP2 refuses `1137=10`).
  Admission is engine-wide, as for every version: a session whose own dictionary is FIX50SP2 admits a
  `1137=10` peer when the engine holds FIX Latest, and validates that peer's messages with its own
  dictionary.
  *(FR-001..FR-007; owner ruling Q1; witnesses: contracts W-1..W-6.)*
- **B-097-2 — BREAKING (public C++; C ABI unchanged): `application_version` gains `vlatest`, and the
  generated `fixpp::vlatest` typed classes report it.** A consumer's exhaustive `switch` over
  `application_version` sees the new enumerator; `fixpp::vlatest::owning_<Msg>::which()` returns
  `application_version::vlatest` (was `v50sp2`). `dict::version_registry` and `Engine` grow by one
  slot; C++ consumers rebuild against the new headers (no C++ ABI promise pre-release). The BREAKING
  label is for the two answers above, not for the layout. *(FR-011, FR-012(c); Gate A round 2,
  G097-017.)*
- **B-097-3 — fixpp has no rule for DefaultApplExtID(1407) or ApplExtID(1156): neither affects
  version resolution, serviceability or session state, and fixpp originates neither.** No session code
  reads either tag, as in QuickFIX C++, QuickFIX/J and QuickFIX/Go. With `validate_inbound_messages` on,
  whether a frame carrying one passes is decided by the session's own dictionary, as for every tag
  (L-041-2).
  - The vendored `FIXT11.xml` (Logon 1407, header 1156) and the Orchestra FIX Latest file declare both,
    so a session validating with either accepts them. A FIX50/SP1/SP2 session dictionary accepts header
    1156 through the FIXT framing surface (B-081-1).
  - A session dictionary that declares neither rejects them. One example is a FIX4.x-shaped dictionary
    with 1137 injected: a Logon carrying 1407 gets a session Reject on tag 1407 before the
    DefaultApplVerID check. QuickFIX/Go behaves the same way when configured with its shipped
    FIXT11.xml, which declares neither tag. From source, it fails such a Logon with `InvalidTagNumber`
    and disconnects.
  - fixpp originates no 1407, no 1156 and no header ApplVerID(1128) on any frame it builds itself. An
    application payload that carries 1128 or 1156 is moved into the header and sent as given,
    unchanged.

  For FIX Latest, only the EP-level field back-port into the legacy dictionaries and Extension Pack negotiation (DefaultApplExtID(1407) / ApplExtID(1156), which fixpp does not read) remain post-1.0, both in the Article XVIII §2 v1.2 milestone. *(FR-008; owner ruling, Gate A round 1; W-7, W-8.)*
- **B-097-4 — An application message resolved to FIX Latest reifies with a FIX Latest label.**
  `dict::reify` under a FIX Latest resolution returns an `owning_message_handle` whose
  `version().application` is `vlatest` for every application MsgType of the FIX Latest dictionary, and
  `dict_reify_unknown_msg_type` for any other MsgType. It returns that error too when the build disables
  the FIX Latest codegen tier. FIXT admin MsgTypes (Logon, Heartbeat, …) keep routing to the FIXT admin
  dispatch and are labelled as session-admin, unchanged. The generated
  `fixpp::vlatest::owning_<Msg>::which()` returns `vlatest` (B-097-2).
  - **Condition:** a message resolves to FIX Latest when its header 1128 is `10`, or when 1128 is absent
    and the session negotiated `10`.
  - A session whose own dictionary is FIX Latest but which is configured `default_appl_ver_id = "9"`
    (the FIX50SP2 value, admitted by the one-way fallback) negotiates FIX50SP2. Its messages without
    1128 reify under the FIX50SP2 arm: they come back as `v50sp2`-labelled handles, and MsgTypes outside
    that arm get `dict_reify_unknown_msg_type`. This is as before 097. To be reified as FIX Latest, such
    a session is configured `"10"`.

  Before 097 no input resolved to FIX Latest. *(FR-009; W-9.)*

- **L-097-1 — Only direct C++ can register the FIX50SP2 and the FIX Latest dictionaries in one engine.**
  The TOML resolver loads one `[dictionary]`, and the C API does not fill `EngineConfig::dictionaries`. A
  TOML engine holding FIX Latest admits `1137=9` and `1137=10` through it (the one-way fallback, B-097-1);
  one holding FIX50SP2 refuses `1137=10`. Status: documented; a multi-dictionary config grammar is not
  planned. *(Successor to L-080-1's config half; FR-011.)*

Closed file: L-074-1 and L-080-1 moved verbatim, each preceded by
`<!-- L-0NN-1 — closed: resolved by 097 (FIX Latest is ApplVerID 10; own registry slot) -->`.

## §E — Constitution amendment text (v4.2 → v4.3, MINOR)

0. **The canonical residue sentence (Gate A round 2, RC-A).** What stays post-1.0 for FIX Latest after
   097 is stated by this one sentence. It is defined here; every locus below copies it:

   For FIX Latest, only the EP-level field back-port into the legacy dictionaries and Extension Pack negotiation (DefaultApplExtID(1407) / ApplExtID(1156), which fixpp does not read) remain post-1.0, both in the Article XVIII §2 v1.2 milestone.

   - Every locus copies it **byte for byte, on one physical line**, with no markdown or line break
     inside it, so `grep -F` finds every copy (quickstart §4.2 counts them at the head and at base).
   - Its placement follows the current constitution: Article XVIII §2's v1.2 bullet already holds the
     EP-level field back-port and the only roadmap text about 1156 ("ApplExtID(1156)=303 on-wire
     differentiation"). So no roadmap item moves; Article I is brought into line with Article XVIII §2.
   - It carries no `=303` and no L-074-1 / L-080-1 token, so a copy is not a quickstart §4.1 hit.
   - Loci: the Article I §1 bullet (E.1), the Article I §1 milestone line (E.2), the Article XVIII §2
     annotation (E.3), the coverage-index Post-1.0 Gap Registry D-011 row and the catalogue D-011 row
     (§F), B-097-3 (§D), spec US4 AS-1 and research R-8.
1. **Article I §1, FIX Latest bullet** — replace
   `(wire application version \`v50sp2\` via \`session_to_application\`; no distinct ApplVerID)`
   with
   `(wire application version FIX Latest, ApplVerID(1128)/DefaultApplVerID(1137) value \`10\`, its own \`application_version::vlatest\`, negotiated when the configured or advertised DefaultApplVerID is \`10\`; a session on this dictionary configured \`9\` negotiates FIX50SP2, which this dictionary answers when no FIX50SP2 dictionary is registered — feature 097; feature 074 read the pre-EP260 ApplVerID enum, which stops at 9)`,
   and replace
   `: **ApplExtID(1156)=303 differentiation and session negotiation remain post-1.0** (the typed read tier is delivered by 076, the typed builder tier by 077).`
   with
   `. **For FIX Latest, only the EP-level field back-port into the legacy dictionaries and Extension Pack negotiation (DefaultApplExtID(1407) / ApplExtID(1156), which fixpp does not read) remain post-1.0, both in the Article XVIII §2 v1.2 milestone.** (The typed read tier is delivered by 076, the typed builder tier by 077, and FIX Latest's ApplVerID 10 identification and negotiation by feature 097; a validated frame carrying 1407 or 1156 passes or fails on the session's own dictionary.)`
2. **Article I §1, post-1.0 milestone line** — on that line (the same label also appears in a
   historical Sync Impact Report entry, which is left alone), `FIX Latest (session-negotiation tier)` →
   `FIX Latest (EP-level field back-port and Extension Pack negotiation tiers)`, and the parenthetical's
   closing
   `; only its ApplExtID(1156)=303 differentiation and session-negotiation tiers remain post-1.0.)` →
   `; its ApplVerID 10 identification and negotiation per feature 097. For FIX Latest, only the EP-level field back-port into the legacy dictionaries and Extension Pack negotiation (DefaultApplExtID(1407) / ApplExtID(1156), which fixpp does not read) remain post-1.0, both in the Article XVIII §2 v1.2 milestone.)`
3. **Article XVIII §2, v1.2 bullet** — append the annotation
   `*(Amended v4.3, feature 097: "ApplExtID(1156)=303 on-wire differentiation" as FIX Latest's on-wire identifier is superseded — FIX Latest is ApplVerID 10, delivered in v1.0 by feature 097. For FIX Latest, only the EP-level field back-port into the legacy dictionaries and Extension Pack negotiation (DefaultApplExtID(1407) / ApplExtID(1156), which fixpp does not read) remain post-1.0, both in the Article XVIII §2 v1.2 milestone.)*`
4. **Sync Impact Report** v4.2 → v4.3 at the top, in the v4.2 report's shape. Amendment is the first
   commit of implementation. Contents:
   - the date, `PENDING`, and the bump: MINOR (Article XX §4; no `CHANGELOG.md` entry). Does a PR
     mergeable under v4.2 fail under v4.3? None does: no banned pattern, budget or obligation is added;
   - **Modified**: the three clauses above (Article I §1 bullet, Article I §1 milestone line, Article
     XVIII §2 annotation), and the `> **Status:**` blockquote at the RATIFIED flip (below);
   - **Read and unchanged**:
     - Article XVIII §1. "FIX 4.0–5.0SP2 + FIXT.1.1 session + application layers": FIX Latest
       negotiation by DefaultApplVerID(1137) is S-025's v1.0 FIXT.1.1 session mechanism with one more
       value, so it sits inside that scope.
     - Article XVIII §5, no early shipping. This is a correction of a factual premise (ApplVerID 10
       exists, FIX Session Layer §11.2), not a post-1.0 protocol shipped early. The milestone line's
       label "session-negotiation tier" becomes "EP-level field back-port and Extension Pack
       negotiation tiers" by this amendment (§XVIII.4, Article XX), not by silent shipping. Article I
       and Article XVIII §2 then state the same residue, the canonical sentence of item 0. No roadmap
       milestone gains or loses an item: Article XVIII §2's v1.2 bullet keeps the field back-port and
       Extension Pack negotiation, and only its "ApplExtID(1156)=303 on-wire differentiation" wording,
       as FIX Latest's identifier, is superseded.
     - Article X §7. The C ABI is unchanged; it carries the raw 1137 string.
   - **Rationale**: research R-8 and the premise sweep (parent
     `research/findings/v1-row4b-applverid-10-sweep-2026-10-29.md`), and the owner rulings of
     2026-10-29, including Gate A round 1's "no new rule" for 1407/1156;
   - **Affected catalogue rows**: S-025, S-043, D-011 (note, residue sentence and Tests cell), and
     A-035 through A-065 (the one sentence of §F); in the coverage index, the Post-1.0 Gap Registry
     D-011 row (§F);
   - **Affected feature specs**: `specs/097-fix-latest-applverid-10`. Merged `specs/<id>/` bundles stay
     as written; the superseded records are `specs/074-orchestra-native-reader/` (the reconcile, FR-010,
     L-074-1), `specs/076-fix-latest-typed-codegen/` (prose on `.a = v50sp2` / `app_version_enum`; its
     golden is regenerated as a test oracle) and `specs/080-orchestra-runtime-load/` (L-080-1), flagged
     in `brain/components/dictionary.md`. The companion design document annotated by 097 is
     `.specify/2c-codegen.md` (§4.3, §4.8; §C);
   - **Process**: rides feature 097. Its PR is titled in Article XX §2 form, "Constitution: amend §I.1,
     §XVIII.2 — FIX Latest is ApplVerID 10 (v4.3) (+ 097 FIX Latest ApplVerID 10)";
   - **Review and sign-off**: Gate A / Gate B / owner sign-off slots, filled in that order before the
     RATIFIED flip.
   - **Status line (Gate A round 2, O097-103)**: at the RATIFIED flip the live `> **Status:**`
     blockquote is rewritten in v4.2's form: `user-ratified **v4.3** (<date>) — backwards-compatible
     (Article XX §4 minor, no CHANGELOG.md entry); …; amendment PR #<n>. <what v4.3 changes>. Rides
     feature 097; its PR is titled in Article XX §2 form. Prior: user-ratified **v4.2** …`, the v4.2
     text kept after "Prior:" unchanged.

## §F — Catalogue and coverage index

- `spec/feature-catalogue.md`: S-025 version column `5.0SP2, FIXT.1.1` → `5.0SP2, FIX Latest, FIXT.1.1`
  and a Verified-cell sentence naming the 097 witnesses (W-4, W-5, W-6); S-043 a sentence naming 097 for
  the one-way fallback (W-5, W-6). D-011:
  - its note's `L-074-1 (interim v50sp2 registry-slot coexistence, FR-010 fail-loud guard)` sentence
    gains "— retired by 097: FIX Latest is ApplVerID 10, own registry slot";
  - its note's `wire app-version = \`v50sp2\`, no distinct ApplVerID` gains "(superseded by 097:
    FIX Latest is ApplVerID 10, its own application version)";
  - its note's residue sentence `Only ApplExtID(1156)=303 differentiation and session-negotiation tiers
    remain post-1.0 (Non-Goals).` is replaced by the canonical sentence (§E.0), verbatim (Gate A round 2,
    O097-101);
  - its note's `the FIX50SP2+FIX-Latest \`version_registry\` \`std::abort\` is retained as the
    direct-C++ fail-loud backstop (no config surface can express the colliding pair — L-080-1)` gains
    "— retired by 097: FIX Latest has its own registry slot";
  - its Tests cell's `FR010Guard×2` becomes the names W-4/W-5 leave. The death test
    `OrchestraFR010Guard.BothDictsFailLoud` becomes the coexistence test
    `OrchestraRegistry097.BothDictsCoexist`; `OrchestraFR010Guard.SingleDictConfigsSucceed` is kept;
    `OrchestraRegistry097.OneWayFallback` is new;
  - its case count is marked "(count at 074)" and is not recomputed.

  The rows A-035..A-065 each carry the sentence `FIX Latest goes on the wire as ApplVerID 9 with no
  ApplExtID(1156) (L-074-1, REMAINING-WORK row 4b)`, which 097 makes false. Each occurrence is replaced by
  `FIX Latest goes on the wire as ApplVerID 10 when the session's DefaultApplVerID is 10; a session
  configured 9 negotiates FIX50SP2 (097, B-097-1, B-097-4)`. This is a mechanical replacement. The count
  of old-sentence occurrences before the replacement equals the count of new-sentence occurrences after
  it (quickstart §4 checks both). No status changes.
- `spec/coverage-index.md`: the §4.3.7, §5.3.3, §5.3.5 rows and the DefaultApplVerID(1137) / ApplVerID(1128)
  rows gain "097: ApplVerID 10 = FIX Latest". The Post-1.0 Gap Registry D-011 row (Article XVIII §2's
  v1.2 bullet points at this registry by name; Gate A round 2, O097-101):
  - item column: `; only ApplExtID(1156)=303 / session-negotiation tiers remain deferred` →
    `; ApplVerID 10 identification and negotiation DELIVERED in v1.0 by 097-fix-latest-applverid-10`;
  - reason column: `FIX50SP2+FIX-Latest \`version_registry\` \`std::abort\` retained as the direct-C++
    backstop, L-080-1` gains "(retired by 097: own registry slot, B-097-1)", and
    `Remaining tiers: ApplExtID(1156)=303 differentiation + session negotiation stay post-1.0` is
    replaced by the canonical sentence (§E.0), verbatim;
  - target column: `v1.2+ (ApplExtID / session-negotiation tiers only)` →
    `v1.2 (EP-level field back-port; Extension Pack negotiation)`.
- In-place supersession comments, in 096's own form (the comment on the same line as the text it
  supersedes, so quickstart §4.1's line filter sees "097"):
  `<!-- SUPERSEDED (097-fix-latest-applverid-10, constitution v4.3): FIX Latest is ApplVerID 10 with its own application version and registry slot (B-097-1); its post-1.0 residue is stated on D-011; the text before this comment is history. -->`
  appended to:
  - the catalogue's 076 partial-progress note (`> **Partial-progress note (076-fix-latest-typed-codegen, …`);
  - the coverage index's 074 link note (`> **074-orchestra-native-reader link (read/dictionary tier).** …`),
    after 096's comment;
  - the live B&L 074 section intro (`Feature 074 adds \`dict::OrchestraLoader\` …`, "wire application
    version maps to the existing `v50sp2`, no distinct ApplVerID").
