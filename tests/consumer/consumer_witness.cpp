// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/consumer_witness.cpp
//
// 061-typed-app-messages (061-slim) T024 — FR-008 / SC-004 external-consumer
// compile+link witness. Built as a STANDALONE CMake project (see
// tests/consumer/CMakeLists.txt + run_consumer_witness.cmake) against the
// INSTALLED fixpp package, not the build tree: the only include path this
// TU is compiled with is <installed-prefix>/include (populated by the T023
// install(DIRECTORY) rule). If that install rule regresses (e.g. drops v44,
// or the typed headers go back to $<BUILD_INTERFACE:> only), this TU fails
// to find <fixpp/v44/Messages.hpp> and the witness fails closed at compile
// time, not silently.
//
// Includes a fixpp::v44 typed application-message header and constructs a
// flyweight (fixpp::v44::NewOrderSingle) over a MessageView parsed via the
// dict-aware Parser<Index>{tv} path (mirrors
// tests/support/app_message_read_scaffold.hpp — the 5-exemplar shape-oracle
// harness this feature also ships), then asserts real field values decode
// correctly, proving the linked archives are functional, not just present.

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/v44/Messages.hpp>

#include <cstdio>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

namespace {

// Mirrors tests/support/app_message_read_scaffold.hpp::make_frame — this TU
// cannot #include that gtest-based header (external consumers don't link
// GTest), so the minimal frame-assembly logic is duplicated here.
std::vector<std::byte> make_frame(std::string_view begin_string, std::string_view body) {
    std::string pre = "8=" + std::string(begin_string) + "\x01" + "9=" +
                       std::to_string(body.size()) + "\x01" + std::string(body);
    unsigned sum = 0;
    for (unsigned char c : pre) sum += c;
    char checksum[16]{};
    std::snprintf(checksum, sizeof(checksum), "10=%03u\x01", sum % 256U);
    std::string full = pre + checksum;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path-to-FIX44.xml>\n", argv[0]);
        return 2;
    }
    std::pmr::monotonic_buffer_resource arena{1 << 16};

    fixpp::dict::XmlLoader loader;
    fixpp::dict::Dictionary dict = loader.load(argv[1], &arena);
    fixpp::dict::table_view tv = dict.as_table_view();

    // Same NewOrderSingle (D) body as tests/session/golden/new_order_single.fix
    // / tests/session/exemplar_seeds.hpp::kNewOrderSingleSeed.
    std::string_view body =
        "35=D\x01" "11=ORD-001\x01" "38=100\x01" "40=2\x01" "44=190.5\x01"
        "54=1\x01" "55=MSFT\x01" "60=20240101-10:00:00\x01";
    std::vector<std::byte> buf = make_frame("FIX.4.4", body);

    fixpp::wire::pmr_carry_buffer carry{buf.size(), &arena};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{buf.data(), buf.size()}, carry,
                           std::span<fixpp::wire::frame_view>{fvs, 1});
    if (!framed.has_value() || framed->empty()) {
        std::fprintf(stderr, "FAIL: Framer::feed produced no frame\n");
        return 1;
    }

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{tv};
    auto mv = parser.parse((*framed)[0], &arena);
    if (!mv.has_value()) {
        std::fprintf(stderr, "FAIL: Parser::parse failed\n");
        return 1;
    }

    fixpp::v44::NewOrderSingle nos{*mv};
    auto cl_ord_id = nos.cl_ord_id();
    auto symbol = nos.symbol();
    if (!cl_ord_id.has_value() || *cl_ord_id != "ORD-001") {
        std::fprintf(stderr, "FAIL: cl_ord_id mismatch\n");
        return 1;
    }
    if (!symbol.has_value() || *symbol != "MSFT") {
        std::fprintf(stderr, "FAIL: symbol mismatch\n");
        return 1;
    }

    std::printf(
        "PASS: fixpp::v44::NewOrderSingle flyweight constructed from installed "
        "headers, cl_ord_id=%.*s symbol=%.*s\n",
        static_cast<int>(cl_ord_id->size()), cl_ord_id->data(),
        static_cast<int>(symbol->size()), symbol->data());
    return 0;
}
