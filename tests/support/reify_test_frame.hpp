// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/reify_test_frame.hpp
//
// 2b cutover (004 T059) shared test helper. Builds a minimal valid FIX 4.4
// NewOrderSingle frame so the reify suites can exercise the REAL owning
// deep-copy (from_view) + lazy view() rebuild on a frame-backed
// MessageView<Index>. Production frame_view producer is the Framer
// ([2b §4.2]); MessageView<Index>'s ctor then builds the OffsetTable
// dict-free. Each caller owns the arena lifetime it needs (e.g. the
// source-arena-reset survival tests destroy frame_mr deliberately).
#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fixpp::test_support {

// ClOrdID(11)="ORD1", MsgType(35)="D", v44. BodyLength + CheckSum computed.
[[nodiscard]] inline std::vector<std::byte> make_nos_frame() {
    std::string const body = std::string("35=D\x01") + "34=1\x01" + "49=S\x01"
                             + "56=T\x01" + "11=ORD1\x01" + "55=AAPL\x01";
    std::string pre = std::string("8=FIX.4.4\x01") + "9="
                      + std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (unsigned char c : pre) { sum += c; }
    std::array<char, 8> chk{};
    std::snprintf(chk.data(), chk.size(), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk.data();
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

}  // namespace fixpp::test_support
