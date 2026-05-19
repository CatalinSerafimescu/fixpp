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
    # Returns in Phase 4: openssl/3.6.2 (4.x is breaking, not yet on Conan).
    # Returns in Phase 5: grpc/1.78.1 (1.80 is upstream-only), iceoryx2 via CMake
    #   FetchContent of v0.8.1 gated on FIXPP_USE_ICEORYX2 (Rust/cargo prereq).
    # 002-dictionary-xml-loader adds pugixml/1.14 (MIT) for the XML data-
    # dictionary loader (research.md D-1 / D-15). Pinned at a tagged release;
    # user signed off the choice at /plan; Codex Gate A reviews it.
    requires = [
        "gtest/1.17.0",
        "benchmark/1.9.5",
        "pugixml/1.14",
        "asio/1.36.0",
    ]

    # ── Build-time tools ─────────────────────────────────────────────────────
    # cmake, ninja, swig provided by apt in CI and locally; Conan-pinned tools
    # collide with system versions and add resolution time. Restore in Phase 5.
    tool_requires = []

    # ── Default options ──────────────────────────────────────────────────────
    default_options = {
        "gtest*:shared": False,
        "benchmark*:shared": False,
    }
