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
    # Returns in Phase 4: asio/1.30.2, openssl/3.2.1
    # Returns in Phase 5: grpc/1.62.0, iceoryx2
    requires = [
        "gtest/1.14.0",
        "benchmark/1.8.3",
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
