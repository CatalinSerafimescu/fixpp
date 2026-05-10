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
    requires = [
        # Test framework [const §VII.1]
        "gtest/1.14.0",
        # Benchmarks [const §VIII.1]
        "benchmark/1.8.3",
        # Async I/O + coroutines [const §XI.1]
        "asio/1.30.2",
        # TLS: OpenSSL only per [const §XII.1]
        "openssl/3.2.1",
        # gRPC control plane [const §XIV.1]
        "grpc/1.62.0",
        # iceoryx2: tracked but not yet packaged in Conan; wired in Phase 5
    ]

    # ── Build-time tools ─────────────────────────────────────────────────────
    tool_requires = [
        "cmake/3.28.1",
        "ninja/1.11.1",
        # swig only when python bindings are requested
        # Phase 3 default — always include swig; refined in Phase 5
        "swig/4.2.1",
    ]

    # ── Default options ──────────────────────────────────────────────────────
    default_options = {
        "openssl*:shared": False,
        "grpc*:shared": False,
        "asio*:shared": False,
        "gtest*:shared": False,
        "benchmark*:shared": False,
        # Enable position-independent code for shared library builds
        "openssl*:fPIC": True,
        "grpc*:fPIC": True,
    }

    def layout(self):
        cmake_layout(self)
