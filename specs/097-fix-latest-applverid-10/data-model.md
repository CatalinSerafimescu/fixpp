# Data model: 097 — FIX Latest is ApplVerID 10

## §A — Version identities after 097

| ApplVerID wire value | `application_version` | Dictionary (`session_version`) that answers it | Registry slot |
|---|---|---|---|
| `"2"`..`"9"` | `v40`..`v50sp2` (unchanged) | the matching QuickFIX-XML dictionary | 1..8 (unchanged) |
| `"10"` | **`vlatest` (new, value 9)** | the Orchestra dictionary (`session_version::vlatest`) | **9 (new)** |
| `"0"`, `"1"`, empty-with-no-default, anything else | refused (unchanged errors) | — | — |

- `session_version::vt11` and `Unknown` still map to no slot.
- **Strict** (spec FR-006): a slot is filled only by a dictionary whose session version maps to it. No
  slot answers for another.
- Render (`render_appl_ver_id`) is the inverse over all ten members; `Unknown` stays an error.
- Config token (`default_appl_ver_id`): `"2"`..`"10"`, mapping as the table; the unknown-token diagnostic
  names `"2".."10"`.

## §B — What changes, by surface

| Surface | Change | Spec |
|---|---|---|
| `include/fixpp/dict/version_profile.hpp` | `vlatest = 9` enumerator; wire-table comment; `render_appl_ver_id` case `vlatest` → `"10"`; `session_version::vlatest` comment corrected | FR-001, FR-002, FR-012(c) |
| `src/dictionary/version_profile.cpp` | two-character arm, exactly `"10"` | FR-001 |
| `include/fixpp/dict/version_registry.hpp` | `kTableSize` 10; slot comment | FR-005 |
| `src/dictionary/version_registry.cpp` | `vlatest` → `vlatest`; FR-010 check + abort deleted; header comment names 097 | FR-004, FR-005, FR-012(c) |
| `src/config/scalar_mappers.cpp` | token `"10"`; diagnostic text | FR-003 |
| `tools/codegen/fixpp-codegen/emit_dispatch.cpp` | `kAppVersions` row `{vlatest, vlatest}`; its comments | FR-009 |
| `tools/codegen/fixpp-codegen/ir.cpp` | `kCodegenVersions` vlatest `.a = vlatest`; comment | FR-012(c) |
| `tools/codegen/fixpp-codegen/gen_util.hpp` | `app_version_enum("vlatest")` → `"vlatest"`; guard and comment | FR-012(c) |
| `specs/076-…/contracts/golden/vlatest_Messages.golden.hpp` | regenerated: the vlatest classes' `application_version::v50sp2;` → `::vlatest;`, nothing else | FR-012(c), research R-4 |
| tests | see contracts §W | FR-010 |
| records | §C, §D, §E, §F | FR-011, FR-012 |

No change: `src/session/*` (research R-6), the C ABI (`include/fix/c_api/*` carries the raw 1137 string),
`bindings/`, `dictionaries/*.xml`.

## §C — Superseded statements and their replacements (FR-012(c), (e))

Each rewritten comment states the current rule and names "097" (CLAUDE.md: a superseding decision names
the new one in a header comment). Re-derive the population with
`grep -rn -i "no distinct ApplVerID\|vlatest.*v50sp2\|v50sp2.*vlatest\|FR-010" include src tools tests --include='*.?pp'`
at the implement head; every hit is either rewritten or is a frozen `specs/` record.

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
  `1137=10`. A FIXT acceptor whose engine holds the FIX Latest dictionary establishes a FIX Latest peer
  (QuickFIX/J `DefaultApplVerID=FIX.Latest` sends `1137=10`); before 097 it answered Reject(373=5,
  371=1137). One engine may hold the FIX50SP2 and the FIX Latest dictionaries together. *(FR-001..FR-005,
  FR-007; witnesses: contracts W-1..W-6.)*
- **B-097-2 — BREAKING (public C++; C ABI unchanged): each application version is answered only by its
  own dictionary.** `version_registry::get(application_version::v50sp2)` no longer returns a FIX Latest
  dictionary, so an engine holding only FIX Latest refuses a peer's `1137=9` (Reject 373=5), and a FIXT
  session whose own default is `"9"` on such an engine fails `open()` with `invalid_session_config`;
  configure `"10"`, or add the FIX50SP2 dictionary. `application_version` gains the enumerator `vlatest`
  (a consumer's exhaustive `switch` sees it), and the generated `fixpp::vlatest` typed classes report
  `application_version::vlatest` (was `v50sp2`). *(FR-006, FR-011, FR-012(c); owner ruling Q1.)*
- **B-097-3 — DefaultApplExtID(1407) and ApplExtID(1156) are accepted and ignored; fixpp emits neither,
  nor a header ApplVerID(1128).** They do not affect version resolution, serviceability, validation or
  session state — the behaviour of QuickFIX C++, QuickFIX/J and QuickFIX/Go, none of which reads either
  tag. Extension Pack negotiation is post-1.0. *(FR-008; W-7, W-8.)*
- **B-097-4 — A message resolved to FIX Latest reifies as FIX Latest.** `dict::reify` under a FIX Latest
  resolution returns a handle whose application version is `vlatest` for every MsgType of the FIX Latest
  dictionary, and `dict_reify_unknown_msg_type` otherwise (also when the build disables the FIX Latest
  codegen tier). Before 097 no input resolved to FIX Latest, and a FIX Latest session's messages reified
  under FIX50SP2. *(FR-009; W-9.)*

Closed file: L-074-1 and L-080-1 moved verbatim, each preceded by
`<!-- L-0NN-1 — closed: resolved by 097 (FIX Latest is ApplVerID 10; own registry slot) -->`.

## §E — Constitution amendment text (v4.2 → v4.3, MINOR)

1. **Article I §1, FIX Latest bullet** — replace
   `(wire application version \`v50sp2\` via \`session_to_application\`; no distinct ApplVerID)`
   with
   `(wire application version FIX Latest, ApplVerID(1128)/DefaultApplVerID(1137) value \`10\`, its own \`application_version::vlatest\` — feature 097; feature 074 read the pre-EP260 ApplVerID enum, which stops at 9)`,
   and replace
   `**ApplExtID(1156)=303 differentiation and session negotiation remain post-1.0**`
   with
   `**only Extension Pack negotiation — DefaultApplExtID(1407) / ApplExtID(1156) beyond accept-and-ignore — remains post-1.0** (FIX Latest is negotiated by DefaultApplVerID like every application version, feature 097)`.
2. **Article I §1, post-1.0 milestone line** — `FIX Latest (session-negotiation tier)` →
   `FIX Latest (Extension Pack negotiation tier)`, and the parenthetical's closing
   `only its ApplExtID(1156)=303 differentiation and session-negotiation tiers remain post-1.0` →
   `its ApplVerID 10 identification and negotiation per feature 097; only its Extension Pack negotiation tier remains post-1.0`.
3. **Article XVIII §2, v1.2 bullet** — append the annotation
   `*(Amended v4.3, feature 097: "ApplExtID(1156)=303 on-wire differentiation" is superseded — FIX Latest is ApplVerID 10, delivered in v1.0 by 097; the EP-level field back-port stays here.)*`
4. **Sync Impact Report** v4.2 → v4.3 at the top: date, MINOR (Article XX §4), the three clauses, the
   rationale (research R-8; the premise sweep), affected rows S-025, S-043, D-011, Gate A / Gate B /
   owner sign-off slots, the PR. Amendment is the first commit of implementation.

## §F — Catalogue and coverage index

- `spec/feature-catalogue.md`: S-025 version column `5.0SP2, FIXT.1.1` → `5.0SP2, FIX Latest, FIXT.1.1`
  and a Verified-cell sentence naming the 097 witnesses (W-4, W-5); S-043 a sentence for strict lookup
  (W-6); D-011's note: FIX Latest's wire identity is ApplVerID 10 (097). No status changes.
- `spec/coverage-index.md`: the §4.3.7, §5.3.3, §5.3.5 rows and the DefaultApplVerID(1137) / ApplVerID(1128)
  rows gain "097: ApplVerID 10 = FIX Latest".
