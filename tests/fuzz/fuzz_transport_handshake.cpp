// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/fuzz/fuzz_transport_handshake.cpp
// T025 — [2h §9 seam #12] — libFuzzer handshake harness.
//
// FR-015 catalogue / scope entry (re-labelled 014 T025 — doc-only):
//
// CURRENT SCOPE (post-014 MVP): adversarial-input crash-safety witness at the
//   transport-layer boundary. The harness validates that the stub boundary is
//   crash-free under ASan/UBSan. The stub exercises the TLS record-header
//   parsing invariant (no crash on any byte sequence).
//
// DEFERRED SCOPE (015 + mock_transport T041):
//   Feed scripted partial TLS records via mock_transport (currently a MINIMAL
//   LOCAL STUB; see body). Verify async_handshake completes with
//   transport_handshake_failed on adversarial input and that no crash / UAF
//   of the captured pinset snapshot occurs. Replace the stub_handshake call
//   with the real mock_transport wiring once T041 ships.
//
// DEPENDENCY: US4 T041 mock_transport (include/fixpp/transport/test/mock_transport.hpp).
// Until T041 ships, this harness uses a MINIMAL LOCAL STUB.
//
// In the production path (post-T041):
//   asio_tls_transport::async_handshake runs OpenSSL's state machine against
//   the provided byte stream. Adversarial TLS records cause SSL_do_handshake()
//   to return an OpenSSL error → transport_handshake_failed.
//
// The fuzz goal is to find inputs that cause:
//   - Crash / UAF (ASan).
//   - UB (UBSan).
//   - Any non-transport_handshake_failed error on adversarial input (unexpected
//     success or unexpected panic).
//
// Build: Clang + libFuzzer + ASan + UBSan.
//   cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON
//   cmake --build build/linux-clang-asan --target fuzz_transport_handshake
//   ./build/linux-clang-asan/bin/fuzz_transport_handshake corpus/
//
// Campaign (T029): smoke-run ≥ 30 s with no crash per [const §IX.4].

#include <cstddef>
#include <cstdint>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <memory_resource>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Minimal local stub of async_handshake for fuzz use until T041 ships.
//
// APPROACH: Since async_handshake requires a running asio executor and an SSL*
// context, this harness drives the invariant at the TRANSPORT-LAYER BOUNDARY
// level rather than calling async_handshake directly:
//
//   - The fuzzer feeds random bytes that represent "what would come from the
//     peer as a TLS handshake response".
//   - We validate that the harness wrapper does not crash on any byte sequence.
//   - When T041 + T026 ship, replace this stub with the real transport wired
//     to the mock_transport's inbound_bytes script.
//
// [US4 T041 DEPENDENCY]: replace the stub_handshake call with:
//   mock_transport m{Script{.inbound_bytes = fuzzer_input, .handshake_succeeds = false}};
//   auto result = co_await m.async_handshake(ssl_cfg);
//   assert(result.error() == core::error::transport_handshake_failed);
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Stub: simulate what async_handshake does with adversarial bytes.
// Returns true if the stub correctly identified the input as adversarial
// (the real check is OpenSSL's SSL_do_handshake → SSL_ERROR_SSL).
bool stub_handshake_adversarial(const uint8_t* data, size_t size) noexcept {
    // Any byte sequence that doesn't start with a valid TLS ClientHello/ServerHello
    // record header (record type 0x16, version 0x03xx, length ≤ 16384) is
    // adversarial. Real OpenSSL will reject these.
    if (size < 5) return true;  // Too short: adversarial.

    // TLS record: byte[0] = content type (0x14-0x17), bytes[1-2] = version.
    uint8_t content_type = data[0];
    uint8_t major = data[1];
    // uint8_t minor = data[2];  // not checked here
    uint16_t length = (static_cast<uint16_t>(data[3]) << 8) | data[4];

    // Valid TLS record content types: 0x14 (ChangeCipherSpec), 0x15 (Alert),
    // 0x16 (Handshake), 0x17 (Application Data).
    if (content_type < 0x14 || content_type > 0x17) return true;
    // Valid TLS major version: 3 (for TLS 1.0-1.3, which all use 0x03xx).
    if (major != 0x03) return true;
    // Valid TLS record length: ≤ 16384.
    if (length > 16384) return true;

    // Might be a valid record header but content may still be adversarial.
    // For the stub, accept any record with a valid header as "potentially valid".
    (void)length;
    return false;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Invariant: stub_handshake_adversarial must not crash on any input.
    bool is_adversarial = stub_handshake_adversarial(data, size);
    (void)is_adversarial;

    // When T041 + T026 ship, the stub above is replaced by:
    //   mock_transport m{Script{.inbound_bytes = ..., .handshake_succeeds = false}};
    //   ... co_await m.async_handshake(ssl_cfg) ...
    //   assert result.error() == core::error::transport_handshake_failed
    //
    // Until then, the harness validates that the stub boundary is crash-free.
    // ASan/UBSan coverage comes from the stub's byte-range checks.

    // Verify that transport_handshake_failed has the expected slot (compile-time
    // guard embedded in the fuzzer binary for runtime-assertion purposes).
    static_assert(static_cast<int>(fixpp::core::error::transport_handshake_failed) == 107,
                  "transport_handshake_failed slot must be 107 per data-model E-13");

    return 0;
}
