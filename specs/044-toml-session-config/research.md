# Phase 0 Research: Native TOML config-file loading (session establishment)

All NEEDS-CLARIFICATION items from the spec were resolved at `/speckit-clarify` (4 decisions, baked into the spec's Clarifications section). This document records the *design* decisions that inform Phase 1.

---

## D-1 — TOML parser library

**Decision:** `tomlplusplus` (toml++), pinned **`tomlplusplus/3.4.0`**, header-only.

**Rationale:**
- **Verified latest on Conan Center 2026-06-19** (`conan search tomlplusplus -r conancenter` → 1.3.3 / 2.5.0 / 3.0.1 / 3.1.0 / 3.2.0 / 3.3.0 / **3.4.0**). Pin is current, not propagated from recall (avoids the OTEL/quill stale-pin class per project memory).
- **MIT** — AGPL-compatible (`[const §V.3]`), not LGPL (§XV.12).
- **Header-only** — the decisive property: the parser becomes a *compile-time include in `src/config/*.cpp` only*, never a link edge of `fixpp_core`/`fixpp_session`. FR-004 / SC-006 ("a host that does not use file config takes on no dependency") is then satisfied **structurally**, not by convention.
- Conformant TOML 1.0, typed values, native `std::filesystem`/`std::chrono`-friendly extraction, good error locations (`source_region` with line/column) — directly feeds FR-017's "location within the file".

**Alternatives considered:**
- `toml11` (MIT, header-only) — comparable; tomlplusplus has the larger Conan-Center cadence and richer `source_region` diagnostics.
- Hand-rolled parser — rejected: re-implements a solved problem, and §XV.16 already blesses adopting TOML, not writing a new parser.

**Action (Article III):** add `"tomlplusplus/3.4.0"` to `conanfile.py` `requires`; user signs off at `/plan`; Codex Gate A reviews the dep.

---

## D-2 — Component isolation boundary

**Decision:** a new standalone CMake target **`fixpp_config_toml`** (`src/config/` + `include/fixpp/config/`) that links the existing `fixpp_session`/`_core`/`_tls`/`_dict`/`_transport` libraries and PRIVATE-includes tomlplusplus. **Nothing in core links `fixpp_config_toml`.**

**Rationale:** `quickfix_compat::cfg_loader` is the cited precedent but is *not* a precedent for isolation — it compiles **into** `fixpp_session` (`src/session/CMakeLists.txt:69`). Following that would pull the parser into the embeddable core and violate FR-004. A dedicated target inverts the dependency: core stays parser-free; only hosts that opt into file config link the new target. The namespace is `fixpp::config` (new), keeping it out of `fixpp::session`.

**Alternatives considered:** put it in `fixpp::session::toml` compiled into `fixpp_session` (the cfg_loader shape) — rejected, violates the parser-out-of-core requirement.

---

## D-3 — Result / diagnostic type (collect-ALL, no C-ABI surface)

**Decision:** the loader returns a C++-only `LoadResult` = `std::expected<ConfigBundle, std::vector<LoadDiagnostic>>`. (Note: `fixpp::core::expected_t<T>` is a **single-param** alias hard-bound to `error` — `error.hpp:780` — so it cannot carry a custom error payload; the loader uses `std::expected` directly. This is still a loader-local C++-only type; it mints **no** new `fixpp_error_t`.) Each `LoadDiagnostic` carries `{ key_path, reason_class, location (line/col), message }`. The error channel is the **collected vector** of every per-key failure found in one pass (FR-018).

**Rationale:** "collect and report ALL" (FR-018) makes a single `fixpp_error_t` slot structurally wrong — exactly the `store_factory_failed` collapse the spec replaces. The diagnostic taxonomy is loader-local and **never crosses the C ABI** (`[const §X.4]`): **no new `fixpp_error_t` value is minted.** `reason_class` is a closed enum: `{ parse_error, unknown_key, recognized_not_yet_supported_step2, missing_required, empty_required, malformed_value, out_of_range, unknown_enum, invalid_or_contradictory_selector }` (FR-017).

**Credential redaction (FR-019):** `LoadDiagnostic::message` for any key whose path matches the credential set (`username`/`password`) substitutes `***REDACTED***` for the value, consistent with existing redaction. The diagnostic still names the *key* and *reason* (so "password malformed" is actionable) without echoing the secret.

**Alternatives considered:** stop-at-first-error returning one `expected_t<…, error>` — rejected by the FR-018 clarification (multi-error files must be fixable in one iteration).

**noexcept-boundary (Gate A round 1 — fail-closed design rule).** `load_toml_config(...)` is `noexcept` (all failure flows through `LoadResult`), but several construction/parse sites it must call **throw** — a `std::terminate` on malformed input would directly contradict fail-closed (FR-012), the exact input US2 exists to handle. The loader MUST wrap every throwing site in `try/catch` (or `trap_throw`) and convert the throw to a `LoadDiagnostic`. Enumerated throwing sites:
- **toml++ parse** — pin the toml++ exception mode and route its parse failure to a `parse_error` diagnostic without a throw escaping the boundary.
- **`system_clock_source` ctor** — NOT noexcept (`system_clock_source.hpp:55`); **live** under DECISION-1 (the loader builds the clock at load time). Wrap → `invalid_or_contradictory_selector` (or an internal-error diagnostic) on allocation failure.
- **`XmlLoader::load(path, mr)`** — throws `dict::xml_parse_error`/`unknown_version_error`/`xml_oom_error` (no noexcept variant). Wrap → `invalid_or_contradictory_selector` on the `dictionary` selector. The `mr` is `LoadOptions.resource` (the cold-path load arena — DECISION-1 / Codex r2 #1; its precondition `mr != nullptr` is satisfied by the `get_default_resource()` default, never null).
- **`file_cert_source` direct ctor** — throws. The loader MUST use the noexcept `make_file_cert_source(cfg, mr)` factory (`file_cert_source.hpp:56`, returns `expected_t`) instead, with `mr = LoadOptions.resource`. An **encrypted** PEM key (no `password_cb` file channel) surfaces here as a graceful `expected_t` failure → `invalid_or_contradictory_selector` on the cert selector — step 1 is plaintext-key-only (D-5).

Prefer the noexcept `expected_t`-returning factories everywhere they exist (`make_file_cert_source`, `make_asio_{tls,plain}_transport_factory`, `make_ssl_ctx_config`) — those are check-the-`expected`, not try/catch. This is the known `[[feedback_trap_throw_pmr_witness_enumerate_sites]]` class. Negative fixtures: a malformed cert and a malformed/unknown-version dictionary XML must each yield a diagnostic, NOT a terminate.

---

## D-4 — Bundle is deliberately incomplete (host-completion boundary)

**Decision:** `ConfigBundle` holds engine-establishment settings + N `SessionDefinition`s, where each `SessionDefinition` carries a **partially-populated `SessionConfig`** — every file-expressible field set, and the host-supplied fields (`executor_override` instance, and the engine-level `Application`) **left at their defaults for the host to inject** before `Session::open`. The data-model enumerates exactly which `SessionConfig`/`EngineConfig` fields the loader sets vs. leaves.

**Rationale:** FR-010 — no file can express the application callbacks or the executor *instance*. The bundle must not masquerade as ready-to-open; the data-model's field-population map is the contract that keeps this explicit. The host stub (quickstart) shows the minimal completion: set `EngineConfig::application`, provide the executor, then open.

---

## D-5 — Object-selector resolution (re-grounded against real headers, Gate A round 1)

**Decision:** a `selector_resolver` dispatches `{kind, params}` to the existing built-in factories. Per-selector strategy + readiness (mapped against the real factory APIs):

| Selector | Target field | Factory/ctor called | Readiness | Action |
|---|---|---|---|---|
| `store=file` | `SessionConfig::store_factory` / `EngineConfig::default_store_factory` | `FileStoreFactory(FileStore::Config{directory,…,policy,max_frame_bytes})` | **ready** | map path + policy + caps; `file_io_executor`/`mr` stay host-threaded at `make()` |
| `store=memory` | same | **`MemoryStoreFactory(MemoryStore::Config{…})`** | **ready (reuse existing)** | `MemoryStoreFactory` ALREADY EXISTS (`memory_store_factory.hpp:41`, `final : MessageStoreFactory`, `yields_persistent_store()→false`, `make(sender,target,mr,max_bytes,file_io_executor)` discarding the executor). The loader just constructs it from a parsed `MemoryStore::Config` — no new artifact (D-5a). |
| `cert_source` | `SessionConfig::cert_source` / `EngineConfig::default_cert_source` | `make_file_cert_source(file_cert_source::Config{leaf/chain/key/ca + limits}, mr)` | **ready (noexcept factory)** | `make_file_cert_source` returns `expected_t` (`file_cert_source.hpp:56`, noexcept) — prefer it over the throwing direct ctor (D-3 rule). map paths + limits; **`password_cb` left null → step 1 is PLAINTEXT-KEY-ONLY** (a file cannot carry a passphrase securely; an encrypted key fails gracefully → `invalid_or_contradictory_selector`, NOT a deferral — see D-9a note); `mr` = `LoadOptions.resource` (cold-path load arena, Codex r2 #1) |
| `dictionary` (by path) | `SessionConfig::dictionary` | `XmlLoader::load(path, mr)` | **ready (THROWS — wrap)** | `XmlLoader::load` is NOT noexcept; throws `dict::xml_parse_error`/`unknown_version_error`/`xml_oom_error` — the loader MUST `try/catch` → `invalid_or_contradictory_selector` (D-3 noexcept-boundary rule). map path; `mr` = `LoadOptions.resource` (cold-path load arena; precondition `mr != nullptr` met by the `get_default_resource()` default) |
| `dictionary` (by version) | — | — | **DEFERRED (OQ-1 option A)** | NOT self-contained: the registry only resolves versions already in `EngineConfig::dictionaries`; nothing in the file populates that mapping. Reject `kind="version"` as `recognized_not_yet_supported_step2`; by-path meets the SC-004 parity floor (QuickFIX is path-based). |
| `dialect_overlay` | `SessionConfig::dialect_overlay` | — | **DEFERRED** | overlay construction API not public in v1.0 → reject as `recognized_not_yet_supported_step2` (spec FR-007a) |
| `transport` (tls/plaintext) | engine `default_transport_factory` (by `kind()`) + per-session `reconnect_endpoint` | `make_asio_tls_transport_factory(cfg, ssl_cfg)` / `make_asio_plain_transport_factory(cfg)` | **ready** (see D-6, D-6a) | **host/port → `SessionConfig::reconnect_endpoint` ONLY** (the connect target; `transport.kind` picks the factory `kind()`) |
| `clock=system` | `EngineConfig::clock` | `system_clock_source(LoadOptions.engine_executor)` | **ready (executor is a load input; ctor THROWS — wrap)** | **`clock.kind` is REQUIRED at load** (engine/default scope; absent → `missing_required`, empty → `empty_required`, non-`system` → `unknown_enum`) — `EngineConfig::clock` is mandatory (`clock_not_set` at engine open, `engine_config.hpp:188-192`), so a missing clock selector MUST fail per-key at load, not escape to engine-open (Codex r2 #2). The ctor REQUIRES the engine executor, is non-copyable/non-movable + NOT noexcept (`system_clock_source.hpp:55`). DECISION-1 passes the engine executor into `load_toml_config` via `LoadOptions`, so the loader CAN build the live clock; it MUST `try/catch` the ctor (D-3 rule). reject any other kind. |

**Zero new core artifacts.** Every selector resolves over an existing, verified API: `MemoryStoreFactory` already exists; the cert/transport/SSL factories return `expected_t`; `XmlLoader::load` and the `system_clock_source` ctor are throwing sites the loader wraps (D-3). The only new *component* is the `fixpp_config_toml` target (selector-mapping + scalar validation + tests) — not a new core type.

---

## D-5a — `store=memory` reuses the existing `MemoryStoreFactory` (NOT a gap)

**Decision:** `store=memory` resolves to the **existing** `fixpp::session::MemoryStoreFactory` (`include/fixpp/session/memory_store_factory.hpp:41`), constructed from a parsed `MemoryStore::Config`. **No new factory is written.**

**Correction (Gate A round 1):** an earlier (discredited) Explore pass claimed `MemoryStoreFactory` did not exist and proposed a ~20-line adapter. That is **false in this checkout**: the class is already `final : public MessageStoreFactory` (`:41`), with `yields_persistent_store()→false` (`:49`) and the exact `make(sender, target, mr, max_store_memory_bytes, file_io_executor)` signature (`:69-73`) that discards the executor per its E6 contract. The loader simply maps the TOML `[store] kind="memory"` params onto `MemoryStore::Config` and constructs it. TDD: a selector test that `store=memory` resolves to a working `MemoryStore`-backed factory.

---

## D-6 — transport↔profile consistency: single enforcement point (no divergent check)

**Decision:** the loader builds a transport factory whose `kind()` matches the parsed `SecurityProfile::kind` and populates `cert_source`/`security_profile`/`reconnect_endpoint`; it relies on the **existing 043 `Session::open()` consistency check** (`src/session/session.cpp:946-966`: `resolved_transport_factory->kind() != required_early` → fail-closed pre-mutation) as the **single runtime enforcement point**. The loader does **not** re-implement that invariant.

The loader's *own* validation is scoped to **internal file contradiction** (an authoring error the operator needs named as a per-key diagnostic, FR-016): e.g. `transport.kind=plaintext` (or `security_profile.kind=insecure_plain_tcp`) while TLS material (cert/key/ca paths) is also supplied → `invalid_or_contradictory_selector`; or `transport.kind=tls` with no cert material → `missing_required`. These are file-input checks, distinct in purpose from open()'s resolved-pair invariant — not a duplicate of it.

**Rationale:** the advisor flagged "two divergent checks is the trap." Splitting by *purpose* (file-contradiction detection at load vs. resolved-pair invariant at open) keeps a single source of truth for the runtime invariant while still giving the operator actionable load-time diagnostics. The 043 `Session::open()` `kind()` consistency check is at `session.cpp:946-966` (resolve effective factory ONCE pre-mutation, then `kind() != required_early → invalid_session_config`).

**Building the TLS factory at load (DECISION-1).** `make_ssl_ctx_config(profile, cs, clock, …)` REQUIRES a non-null `clock` and rejects null with `tls_invalid_security_profile` (`security_profile.hpp:97`). So the TLS `TransportFactory` transitively needs the executor-derived clock. With DECISION-1 the loader receives the engine executor as `LoadOptions.engine_executor`, builds **one** `system_clock_source(LoadOptions.engine_executor)` and reuses that **same `shared_ptr<Clock>`** both as `EngineEstablishment.clock` (E-2) and inside the TLS factory's `SslCtxConfig` — not two clocks. `make_ssl_ctx_config` and `make_asio_{tls,plain}_transport_factory` return `expected_t` (noexcept), so the loader checks the `expected` (no try/catch); only the `system_clock_source` ctor on the path throws and is wrapped (D-3). **The TLS factory's at-load dependency set is `{clock(←executor), cert_source(←LoadOptions.resource), Pinset}` — and `Pinset` has NO file channel, which is why `mtls_pinned` is deferred (D-9a).**

---

## D-6a — Transport endpoint flows through `reconnect_endpoint`, NOT the factory (multi-session)

**Decision:** the per-session connect target (`host`/`port`) is written to **`SessionConfig::reconnect_endpoint` only**. The `TransportFactory` does **not** bake the connect endpoint.

**Evidence (discriminating grep):** the initiator connect path is `reconnect_fsm.cpp:250` — `co_await t->async_connect(endpoint_)`, where `endpoint_` is the FSM's stored endpoint set from `SessionConfig::reconnect_endpoint` via `set_reconnect_endpoint()` (per the `reconnect_endpoint` field comment). `Transport::async_connect(endpoint)` takes the endpoint as a **connect-time argument**; the factory's `Transport::Config.remote` is not the connect target.

**Consequence for FR-008 (multi-session):**
- One **engine-shared** `default_transport_factory` per `kind()` serves **all** sessions of that `transport.kind`; each session's distinct endpoint lives in its own `reconnect_endpoint`. The earlier "host/port → `Transport::Config`" half of D-5 is dropped.
- The TLS factory *does* bake `SslCtxConfig{cert_source, profile, clock}`. So when a `[[session]]` declares cert material / profile that **differs from the engine default**, the loader builds a **freshly-minted, per-session `transport_factory_override`** (the existing override field) for that session instead of the engine default.

**`use_count()==1` hygiene (correction, Gate A round 1).** `Session::open()` carries a hygiene assertion `transport_factory_override.use_count()==1` — "no factory shared across Sessions … cross-Session sharing is FORBIDDEN" (`session_config.hpp:298-307`). The two sharing paths must therefore stay distinct:
  - **The engine `default_transport_factory` IS designed to be shared** — it is an engine anchor (one SSL_CTX cached across reconnects), shared via `EngineConfig::default_transport_factory`, NOT via any override. Sessions whose cert/profile match the engine default use it and set **no** override.
  - **A per-session `transport_factory_override` is minted ONLY for a session whose cert/profile diverges from the engine default**, and each such override is a **distinct factory instance owned by exactly that one session** → `use_count()==1`, so it never trips the hygiene assertion. Even if two sessions declare the *same* non-default cert/profile, the loader MUST mint a separate factory per session — it MUST NEVER set the same override `shared_ptr` on multiple sessions. (Interacts with DECISION-1: each override embeds the shared engine `Clock`, but the factory object itself is per-session.)

---

## D-6b — Threading-combination validation: `direct_executor` + `spin` (load-time reject)

**Decision:** the loader rejects `mode=direct_executor` with `locks=spin` at LOAD as `invalid_or_contradictory_selector` on the threading key group, **regardless of `already_serialized_executor`**. This is a SECOND unsafe threading combination, distinct from the FR-011 attestation guard (D-9 / data-model rule 7).

**Evidence:** `Session::open()` rejects this combination at `src/session/session.cpp:904` (`mode == direct_executor && locks == spin → invalid_session_config`), checked independently of the attestation guard — "the store-write path is always mutex; spin under a bare attested executor has no engine-internal serialisation to fall back on" (`[const §XI.5]`). A TOML file can set both `mode` and `locks`, so without a per-key loader rule this known-invalid combination reaches `Session::open()` as one opaque failure, weakening the fail-closed-before-open promise (FR-012/FR-017). Negative fixture: `mode="direct_executor"` + `locks="spin"` → diagnostic at load.

---

## D-7 — Relative-path resolution base

**Decision (clarified):** relative filesystem paths (store `directory`, PEM cert/key/ca paths, dictionary path) resolve against **the directory containing the config file**, not the process CWD (FR-016a). Absolute paths used verbatim. Implemented by capturing `config_path.parent_path()` once and `weakly_canonical(base / rel)`-joining each relative path before handing it to a factory.

**Rationale:** a config bundle (file + its referenced PEM/XML/store dirs) is then relocatable and launch-CWD-independent. Clarified 2026-06-19.

---

## D-8 — Multi-session + shared defaults model

**Decision:** the TOML shape is a top-level `[default]` table (shared establishment settings) plus an array-of-tables `[[session]]` (one per session). The loader deep-merges `[default]` under each `[[session]]`, with the session's own keys overriding. An override of a key absent from `[default]` is valid (spec edge case). Each merged result is validated independently and produces one `SessionDefinition` (FR-008 / SC-005; QuickFIX `[DEFAULT]`/`[SESSION]` parity).

**Rationale:** mirrors the QuickFIX `[DEFAULT]`/`[SESSION]` semantics operators already know (US4), in TOML-idiomatic table/array-of-tables form.

---

## D-9 — Enum-token canonical spellings + no-implicit-default

**Decision:** every enumerated key accepts **only** the canonical spelling set, rejecting unknown tokens with `unknown_enum` + the legal set (FR-014). An absent *optional* key yields its **documented explicit default** (the same default the struct's in-source default expresses); an absent or empty *required* key is an error (`missing_required`/`empty_required`) — never an implicit substitution (FR-013, `[const §XII.5]`). The canonical token tables (e.g. `role ∈ {initiator, acceptor}`, `reset_seqnum_policy ∈ {bilateral_strict, bilateral_lenient, unilateral}`, `backpressure ∈ {block, disconnect_and_recover}`, `security_profile.kind ∈ {mtls_ca, mtls_pinned, one_way_ca, insecure_plain_tcp}`) are sourced directly from the enum definitions in `session_config.hpp` / `security_profile.hpp` — single source of truth.

**`insecure_plain_tcp` friction:** selecting it is allowed (opt-in) but never an implicit default; the loud `[[deprecated]]`-class friction lives at the session-layer enumerator (`[const §XII.5]`), unchanged by the loader.

**`mtls_pinned` is a recognized token but NOT selectable from a file in step 1** (D-9a): it lives in the canonical enum (`security_profile.hpp`) so the loader RECOGNIZES it, but selecting it resolves to `recognized_not_yet_supported_step2` (a deferral), not `unknown_enum` (a typo). Step-1 accepted profiles: `{mtls_ca, one_way_ca, insecure_plain_tcp}`.

---

## D-9a — `mtls_pinned` deferral + plaintext-key-only (Gate A round 2 — load-boundary completeness)

**Decision:** `security_profile.kind="mtls_pinned"` is **deferred to step 2** — the loader rejects its selection under `recognized_not_yet_supported_step2` (exactly parallel to `dictionary.kind="version"` and `dialect_overlay`, FR-007a). Step-1 accepted security profiles: `{mtls_ca, one_way_ca, insecure_plain_tcp}`.

**Why `mtls_pinned` is non-buildable from a file (verified chain, real code):**
1. `make_ssl_ctx_config(mtls_pinned, …, pinset=nullptr, …)` hard-rejects a null pinset with `tls_invalid_security_profile`, and a non-null EMPTY pinset with `tls_pin_empty_at_open` (`security_profile.cpp:72-78`). So `mtls_pinned` REQUIRES a non-empty `Pinset` at config-build time.
2. The ONLY pinset-population path is `Pinset::add(Certificate const&)` (`pinset.hpp:112`) — it takes a **parsed `Certificate` runtime object**, not a file path or fingerprint string. **There is no file-based pin loader anywhere in the tree.** Pin material is intrinsically host-supplied.
3. Under DECISION-1 the loader builds the TLS `TransportFactory` at load (D-6), which calls `make_ssl_ctx_config`. With no pin-material file channel, a well-formed `mtls_pinned` config would die at load with a misleading `invalid_or_contradictory_selector` — so the clean disposition is the fail-closed deferral.

This is independently defensible: a `Pinset` is mid-session-mutable / rotatable (`[const §XII.6]` pinset rotation is its own v1.0 feature), so a static config file genuinely cannot express by-design runtime-rotated pin material. The programmatic path to `mtls_pinned` is **unaffected**; only the FILE cannot select it pending a future file-based pinset/rotation slice. The QuickFIX `.cfg` establishment vocabulary has no pinning equivalent, so the SC-004 parity floor is unaffected.

**Plaintext-key-only (the P3 — encrypted PEM):** `file_cert_source::Config::password_cb` is "Optional; called once at construction-time. May be empty" (`file_cert_source.hpp:43-44`), used to obtain an encrypted-PEM passphrase. A config file cannot express a passphrase securely and `password_cb` has no file channel, so **step 1 supports plaintext (unencrypted) private keys only**. This is a **capability** boundary, not a safety hole: `make_file_cert_source` returns `expected_t` (noexcept), so a config referencing an encrypted key fails **gracefully** (no terminate) and the loader converts it to an `invalid_or_contradictory_selector` diagnostic naming the cert selector (fail-closed, FR-012; covered by the D-3 noexcept-boundary wrapping rule). `password_cb` is deliberately **NOT** added to `LoadOptions` (step-1 `LoadOptions` is exactly `{engine_executor, resource}`); a file channel for encrypted keys is a future step-2 concern. (An encrypted key is therefore NOT a `recognized_not_yet_supported_step2` deferral — that reason class is for recognized-but-deferred *selections*; an encrypted key is a runtime cert-load failure.)

---

## D-10 — QuickFIX parity table (SC-004 source)

**Decision:** build the parity table from the **cloned QuickFIX-cpp reference source** (`reference-engines/` per project memory), not from memory. Enumerate QuickFIX's session-establishment settings (the `Session::Settings` / `SessionSettings` keys: `BeginString`, `SenderCompID`, `TargetCompID`, `HeartBtInt`, `ResetOnLogon/Logout/Disconnect`, `RefreshOnLogon`, `CheckCompID`, `ValidateSequenceNumbers`, `SocketConnectHost/Port`, `DataDictionary`, `FileStorePath`, `StartTime/EndTime` [out-of-scope schedule], etc.) and map each to its 044 equivalent key or record an explicit gap/deferral. This becomes `test_quickfix_parity_table.cpp` (SC-004) + a doc table.

**Rationale:** SC-004 is unverifiable without a concrete oracle. The cloned source is the authoritative QuickFIX vocabulary; memory is not. Schedule keys (`StartTime`/`EndTime`/`WeekDays`) are session *scheduling*, not establishment — record as out-of-scope-for-this-step with rationale, not as a parity gap.

---

## OQ-1 — dictionary "by version": where does the version→XML-path mapping come from? (RESOLVED — Gate A → option A)

**Status: RESOLVED (Gate A round 1 → option A, by-path only for step 1).** `dictionary` by-**path** is self-contained (`XmlLoader::load(path, mr)` — D-5). by-**version** ("resolve `FIX.5.0SP2` against `EngineConfig::dictionaries`") presumes the registry is already populated, but **nothing in the file populates version→path** — and QuickFIX itself is path-based (`DataDictionary=FIX44.xml`). A `kind="version"` file is therefore not self-contained.

**Resolution — option A (by-path only):** for step 1 the loader accepts the `dictionary` selector **by path only**; `dictionary.kind="version"` is rejected under the `recognized_not_yet_supported_step2` reason class (like `dialect_overlay`), never silently ignored. This is the least-machinery option and still meets the SC-004 parity floor because QuickFIX is itself path-based. by-version (the option-B explicit preload table, e.g. `[dictionaries] "FIX.5.0SP2" = "dicts/FIX50SP2.xml"` populating the registry) is deferred to a later step.

Applied to: spec FR-007 / FR-007a, US3 acceptance + independent test, quickstart (already by-path — confirmed), data-model E-2/E-4 selector grammar, D-5 table.

## Open items carried to Phase 1 / later

- **Catalogue + coverage-index** (Article VI §4): add the `[const §XV.16]` design row to `spec/feature-catalogue.md` + a design-choice note to `spec/coverage-index.md` **before merge**. This MUST become a **named pre-merge task in `tasks.md`** naming BOTH files (not a prose-only action) so it is not lost.
- **`dialect_overlay`**: deferred (FR-007a) until the dict-overlay construction API is public; reported as `recognized_not_yet_supported_step2`.
- **`mtls_pinned` (deferred) + encrypted-PEM keys (fail-closed, NOT deferred)** (D-9a) — `mtls_pinned` has no file-based pinset channel and IS deferred (reported `recognized_not_yet_supported_step2`); encrypted PEM keys are a different disposition — they have no `password_cb` file channel, so step 1 = plaintext-key-only and an encrypted key is a runtime cert-load failure → `invalid_or_contradictory_selector` (**a fail-closed boundary, not a deferral, never `recognized_not_yet_supported_step2`**). A file-based pinset/rotation slice + an encrypted-key channel are future step-2 concerns.
- **Step-2 deferred surface** (FR-009): logger/tracer/meter/tap/arena keys form the known step-2 key set the loader must recognize-and-reject distinctly (FR-018a). With D-9a the full step-2 deferred set is **{observability keys, tap, arena, dialect_overlay, dictionary kind="version", security_profile mtls_pinned}** (encrypted-PEM keys are NOT a deferral — they are a plaintext-only fail-closed runtime cert-load failure; see D-9a / data-model E-6). The set is enumerated in data-model (E-6) so the reason class is precise, not a typo "unknown key".
