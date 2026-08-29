---
type: Component Decision Map
title: SecurityProfile — trust mode selected at Session::open
description: The 2g-tls design doc quotes a constitutional article that was later amended, so its "normative" quote is stale. Found by Step R.
status: stable
refs:
  - include/fixpp/session/security_profile.hpp
  - .specify/constitution.md
codegraph_entry: [SecurityProfile]
constitution: ["§XII.5"]
---

# `SecurityProfile`

> ## ⚠️ The CODE is authoritative. This page is not.
>
> SecondBrain is a **consultant**, not a source of truth. It points you at the right files and explains
> **why** a decision was taken and what was **rejected** — that half is historical and does not change
> retroactively. It does **not** establish what the code does today.
>
> **Anything here describing current behaviour is a LEAD TO CHECK, not a fact to cite.** Verify against
> source before you rely on it, and cite the source, not this page.
>
> This page exists because signed-off design documents rotted. **It has no immunity from that** — a page
> trusted instead of read becomes the next fossil, and it would be a worse one, because it is the page
> people come to for the fossil list.

## Current state — a LEAD, verify against source before citing

`SecurityProfile` is the trust mode chosen explicitly at `Session` construction — there is no implicit
default. It lives in **`fixpp::session`** (`include/fixpp/session/security_profile.hpp`), and the
shipped set has **four** members: `mtls_ca`, `mtls_pinned`, `one_way_ca` (deprecated), and
`insecure_plain_tcp` — no TLS at all, opt-in, appended by feature **043**.

## ⚠️ `.specify/2g-tls.md` is a fossil, on two axes

**This is a sharper class than issue #334, and worth understanding as a pattern.**

| Claim in `2g-tls.md` | Refuted by |
|---|---|
| *"**Lock the `fixpp::tls::SecurityProfile` enum** … `mtls_ca`, `mtls_pinned`, `one_way_ca [[deprecated]]`"* — three members, presented as locked | The shipped header carries a **fourth**, `insecure_plain_tcp`. `2g-tls.md` does not contain the string at all |
| The namespace `fixpp::tls::SecurityProfile` | The shipped header declares `namespace fixpp::session` |
| §XII.5 **quoted verbatim, "because the enum signature in §4.5 is normative"** | **Article XII §5 was amended** — constitution **v0.3, 2026-06-17**, Gate A folded into feature 043. The article now lists four profiles. The verbatim quote is a quote of a **superseded** article |

### Why this class is worse than #334's

#334's fossil is a doc disagreeing with code. Here the doc **quotes the constitution verbatim,
declaring the quote normative** — so it looks *more* authoritative than an ordinary claim, and it
became false without anyone editing it. **Copying a governing text into a second location creates a
claim that rots the moment the governing text is amended, and nothing links the two.** Any doc quoting
a constitutional article "verbatim because normative" carries this hazard.

⚠️ **Re-derive rather than trusting the table above** — these are claims about a moving tree.

## The header did the job the design doc did not

`security_profile.hpp` names its own amendment in a comment: *"NOTE (043, 2026-06-17):
`insecure_plain_tcp` (k=4) was appended via the …"*, and restates the current constitutional set with
the amendment date. So a reader of the **code** gets the correct picture and a reader of the
**signed-off design doc** does not.

This is the third independent confirmation of the same convention: where a file names the feature id
that superseded a prior decision, the supersession is discoverable; where it does not, a careful
reader reports agreement over a contradiction. Compare `async_mutex.hpp`'s *"Erratum E-5 (048)"* and
the absence of any such pointer in the engine accept path.

## Why the profile exists at all

`Session` construction takes an explicit choice because a defaulted trust mode is a silent security
downgrade. `one_way_ca` carries a compile-time `[[deprecated]]` diagnostic; `insecure_plain_tcp` was
added opt-in-only with deliberate friction, and the amendment that permitted it is recorded in the
constitution's own v0.3 entry rather than only in the feature.
