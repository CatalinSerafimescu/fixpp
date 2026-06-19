# Quickstart: Native TOML config-file loading

This shows the migration MVP (US1): bring up a session from a TOML file, injecting only the host-supplied values a file cannot express.

## 1. An example config file (`fix-session.toml`)

```toml
# Engine-level establishment anchors (shared).
[default]
begin_string          = "FIXT.1.1"
default_appl_ver_id   = "FIX.5.0SP2"
heartbeat_interval    = "30s"          # explicit unit required (FR / edge case)
reset_on_logon        = true
check_comp_id         = true

# Object selectors: {kind, params}.
[default.clock]
kind = "system"

[default.dictionary]
kind = "path"                           # by-path only this step; kind="version" is deferred (FR-007a)
path = "dicts/FIX50SP2.xml"             # relative → resolved vs this file's dir (FR-016a)

[default.store]
kind      = "file"
directory = "./store"                   # relative → resolved vs this file's dir (FR-016a)
policy    = "commit_per_message"

# One session.
[[session]]
sender_comp_id = "CLIENT1"
target_comp_id = "EXCHANGE"
role           = "initiator"

[session.security_profile]
kind = "mtls_ca"                        # explicit; no implicit default (FR / §XII.5)
                                        # step-1 accepted: {mtls_ca, one_way_ca, insecure_plain_tcp};
                                        # mtls_pinned is recognized-but-deferred (FR-006b)

[session.cert_source]
kind             = "file"
leaf_path        = "certs/client.pem"
private_key_path = "certs/client.key"
ca_bundle_path   = "certs/ca.pem"

[session.transport]
kind = "tls"                            # kind() must match security_profile (open()-enforced)
host = "fix.exchange.example"
port = 9443

[session.reconnect_policy]
# backoff schedule …
```

A bad file is rejected **before any session opens**, reporting **every** error in one pass (FR-018):

```
fix-session.toml: 3 errors
  session[0].heartbeat_interval (line 5): malformed_value — duration needs an explicit unit
  session[0].security_profile.kind (line 22): unknown_enum — expected one of {mtls_ca, one_way_ca, insecure_plain_tcp} (mtls_pinned recognized but deferred to step 2)
  session[0].password (line 30): malformed_value — ***REDACTED***
```

(A file that selects `security_profile.kind = "mtls_pinned"` is recognized but rejected under `recognized_not_yet_supported_step2`, not `unknown_enum` — no file input can express pin material this step, FR-006b.)

## 2. Host completion + open

```cpp
#include <fixpp/config/toml_config_loader.hpp>

// The host builds its io_context FIRST; the engine executor is the one
// host-supplied value available before load. The loader needs it to build the
// executor-dependent objects (the system_clock_source, and transitively the
// TLS transport factory) at load time (DECISION-1). LoadOptions also carries a
// COLD-PATH load-time memory resource for the dictionary/cert load-time
// allocation (XmlLoader::load / make_file_cert_source) — distinct from the
// session/message arenas, which stay host-supplied AFTER load (Codex r2 #1).
auto engine_exec = my_io_context.get_executor();

fixpp::config::LoadOptions opts{
    .engine_executor = engine_exec,
    .resource        = std::pmr::get_default_resource(),  // or a host load-time arena
};
auto result = fixpp::config::load_toml_config("fix-session.toml", opts);
if (!result) {
    for (const auto& d : result.error())            // collected diagnostics
        log_error(d.key_path, d.location, d.message);
    return;                                          // fail closed: nothing opened
}
auto bundle = std::move(*result);

// The loader already built clock + factories (incl. TLS) from engine_exec.
// The host supplies ONLY what no file can express and what the loader did NOT
// build: the SAME executor instance on EngineConfig::executor (the loader does
// not set that field), file_io_executor, arenas, and the Application (FR-010).
fixpp::core::EngineConfig eng;
eng.clock                   = bundle.engine.clock;            // loader-built (uses engine_exec)
eng.default_store_factory   = bundle.engine.default_store_factory;
eng.default_cert_source     = bundle.engine.default_cert_source;
eng.default_transport_factory = bundle.engine.default_transport_factory;  // loader-built (TLS embeds the clock)
eng.dictionaries            = bundle.engine.dictionaries;
eng.executor                = engine_exec;                   // SAME instance passed to the loader
eng.file_io_executor        = my_file_pool.get_executor();   // host-supplied, post-load
eng.application             = my_app;                        // business callbacks, post-load

Engine engine{engine_exec, std::move(eng)};         // Engine(executor, EngineConfig) — no default ctor
if (auto r = engine.start(); !r) { /* handle engine-open failure (e.g. clock gate) */ return; }
for (auto& sd : bundle.sessions)
    engine.register_session(std::move(sd.config));  // 043 profile↔factory check fires here
```

## Verify (acceptance)

- **US1 / SC-002:** the resulting `SessionConfig` equals an equivalent hand-built config field-for-field, and the session opens.
- **US2 / SC-003 / SC-007:** an N-error file yields N distinct per-key diagnostics in one load; a deferred step-2 key (`logger`, `tap`, `arena`, `dialect_overlay`) reports `recognized_not_yet_supported_step2`, distinct from an `unknown_key` typo.
- **US3:** each selector kind resolves to the requested built-in (`store=file`/`memory`, `transport=tls`/`plaintext`, dictionary by path); an unknown kind is rejected, and a deferred sub-selector (`dictionary.kind="version"`, `dialect_overlay`, `security_profile.kind="mtls_pinned"`) reports `recognized_not_yet_supported_step2`.
- **Step-1 TLS boundaries (FR-006b / D-9a):** a config selecting `security_profile.kind="mtls_pinned"` reports `recognized_not_yet_supported_step2` (no file pin-material channel); a cert selector referencing an **encrypted** PEM key fails closed as `invalid_or_contradictory_selector` naming the cert selector (step 1 = plaintext-key-only) — a graceful diagnostic, never a terminate.
- **US4 / SC-004 / SC-005:** every QuickFIX establishment setting has a documented equivalent key (parity table); a multi-session `[default]`+`[[session]]` file applies defaults with per-session overrides.
