from conan import ConanFile
from conan.tools.cmake import cmake_layout


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
    # Returns in Phase 4: asio/1.36.0, openssl/3.6.2 (4.x is breaking, not yet on Conan).
    # Returns in Phase 5: grpc/1.78.1 (1.80 is upstream-only), iceoryx2 via CMake
    #   FetchContent of v0.8.1 gated on FIXPP_USE_ICEORYX2 (Rust/cargo prereq).
    requires = [
        "gtest/1.17.0",
        "benchmark/1.9.5",
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

    def layout(self):
        cmake_layout(self)
