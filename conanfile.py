from conan import ConanFile


class FixppConan(ConanFile):
    name = "fixpp"
    version = "0.0.1"
    description = "Modern C++23 FIX protocol library"
    license = "AGPL-3.0-only"
    url = "https://github.com/CatalinSerafimescu/fixpp"
    homepage = "https://github.com/CatalinSerafimescu/fixpp"

    settings = "os", "compiler", "build_type", "arch"

    generators = "CMakeToolchain", "CMakeDeps"

    # ── Runtime dependencies ─────────────────────────────────────────────────
    # Phase 3: only smoke tests (gtest) and placeholder bench (benchmark) compile.
    # Pinned at the highest stable versions on Conan Center as of 2026-05-10.
    # asio/1.36.0 added 2026-05-18 for 006-async-mutex (first async/coroutine
    #   feature; user-approved per [const §III.2], pugixml precedent). Standalone
    #   asio, BSL-1.0 (LGPL-only ban [const §68] inapplicable). 001-004 were
    #   non-async so it was never added despite the Phase-4 note below.
    # openssl/3.6.2 added 2026-05-24 for 011-tls-policy (Phase 4 — TLS module
    #   first to materially link; 4.x is breaking, not yet on Conan). Vetted
    #   at Gate A (Phase-4) round 4, user-signed-off 2026-05-24 per plan.md
    #   Technical Context → Primary Dependencies. Apache-2.0, AGPL-compatible
    #   per [const §V.3]. tls/-touching targets only (PRIVATE link from
    #   fixpp_tls).
    # Returns in Phase 5: grpc/1.78.1 (Conan Center latest; upstream is at 1.81.0
    #   but not yet packaged on Conan), iceoryx2 via CMake FetchContent of
    #   v0.9.1 (upgrade target; was v0.8.1) gated on FIXPP_USE_ICEORYX2
    #   (Rust/cargo prereq; no Conan recipe exists for the Rust crate).
    # OTel gRPC transport: opentelemetry-cpp is added by 017 with with_otlp_grpc
    #   =False (HTTP-only — the v1.0 OtlpLogSink/OtlpMetricExporter default). When
    #   Phase 5 introduces grpc, flip OTel to with_otlp_grpc=True and pin grpc
    #   ONCE via Conan: OTel requires grpc/[>=1.67.1 <2] transitively, so the
    #   Conan resolver unifies it with our own grpc (1.78.1) to a single copy —
    #   no rebuild. Do NOT mix a Conan grpc (for OTel) with a FetchContent grpc
    #   (1.81.0) for us: that yields two grpc copies / an ODR+link conflict unless
    #   OTel is itself rebuilt from source against the FetchContent grpc. Prefer
    #   the highest Conan version available at build time (Conan trails GitHub
    #   only slightly). NB: grpc >=1.70 forces a C++17 consumer + cpp_plugin=True
    #   (free for us — C++23).
    # Bumped 2026-06-02 to Conan Center latest: pugixml 1.14->1.15 (MIT, trivial),
    #   asio 1.36.0->1.38.0 (async backbone; re-verify under the sanitizer matrix).
    # 002-dictionary-xml-loader adds pugixml/1.14 (MIT) for the XML data-
    # dictionary loader (research.md D-1 / D-15). Pinned at a tagged release;
    # user signed off the choice at /plan; Codex Gate A reviews it.
    # crc32c/1.1.2 added 2026-05-20 for 008-message-store (FileStore per-record
    #   + sentinel CRC32 over the Castagnoli polynomial 0x1EDC6F41; research D-3).
    #   BSD-3-Clause, AGPL-compatible per [const §V.3]. FileStore-touching
    #   targets only (PRIVATE link from fixpp_session).
    # opentelemetry-cpp/1.26.0 added 2026-06-02 for 017-log-otel (Phase 4 —
    #   the OTel observability surface: TracerProvider/MeterProvider wrappers,
    #   OtlpLogSink, dual metric export). Pinned EXACTLY per FR-023 / [const
    #   §XV.17] (latest on Conan Center; >=1.12 for the stable logs API).
    #   Apache-2.0, AGPL-compatible per [const §V.3]. otel/log-touching targets
    #   only. Option set is FR-driven (see default_options): with_abi_v2 (API V2)
    #   + with_no_deprecated_code (pairs with abi_v2) + with_prometheus (FR-017,
    #   recipe default False) + with_otlp_http (FR-018, HTTP transport) +
    #   with_otlp_grpc=False (Phase-5 deferral per the grpc note above).
    #   with_prometheus pulls prometheus-cpp/1.1.0 transitively.
    requires = [
        "gtest/1.17.0",
        "benchmark/1.9.5",
        "pugixml/1.15",
        "asio/1.38.0",
        "crc32c/1.1.2",
        "openssl/3.6.2",
        "opentelemetry-cpp/1.26.0",
    ]

    # quill/11.1.0 is the TS-13 spike-only benchmark backend (FR-023): pulled
    #   ONLY when the spike_quill option is on (paired with the CMake option
    #   FIXPP_LOG_SPIKE_QUILL=ON). A default build MUST NOT require quill.
    options = {"spike_quill": [True, False]}

    def requirements(self):
        if self.options.spike_quill:
            self.requires("quill/11.1.0")

    # ── Build-time tools ─────────────────────────────────────────────────────
    # cmake, ninja, swig provided by apt in CI and locally; Conan-pinned tools
    # collide with system versions and add resolution time. Restore in Phase 5.
    tool_requires = []

    # ── Default options ──────────────────────────────────────────────────────
    default_options = {
        "gtest*:shared": False,
        "benchmark*:shared": False,
        "spike_quill": False,
        # 017-log-otel FR-023 OTel SDK option set (recipe option names verified
        # against opentelemetry-cpp/1.26.0):
        "opentelemetry-cpp*:with_abi_v2": True,
        "opentelemetry-cpp*:with_no_deprecated_code": True,
        "opentelemetry-cpp*:with_prometheus": True,
        "opentelemetry-cpp*:with_otlp_http": True,
        "opentelemetry-cpp*:with_otlp_grpc": False,
    }
