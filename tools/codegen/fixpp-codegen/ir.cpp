// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/codegen/fixpp-codegen/ir.cpp — T014 (see ir.hpp banner).
#include "ir.hpp"

#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <stdexcept>
#include <utility>

namespace fixpp::codegen {

namespace {

// session_version -> (codegen application_version, namespace tag). Only the
// four codegen-target versions ([2c §1.3]); anything else is rejected here
// (runtime-XML-only versions get NO typed namespace — AC-D5 boundary).
struct VersionMap {
    fixpp::dict::session_version s;
    fixpp::dict::application_version a;
    char const* ns;
};

constexpr VersionMap kCodegenVersions[] = {
    {.s = fixpp::dict::session_version::v42,
     .a = fixpp::dict::application_version::v42,
     .ns = "v42"},
    {.s = fixpp::dict::session_version::v44,
     .a = fixpp::dict::application_version::v44,
     .ns = "v44"},
    {.s = fixpp::dict::session_version::v50sp2,
     .a = fixpp::dict::application_version::v50sp2,
     .ns = "v50sp2"},
    // FIXT.1.1 session layer: vt11 namespace, application axis Unknown
    // (admin frames resolve {session_admin, vt11, Unknown} — [2c §4.3]).
    {.s = fixpp::dict::session_version::vt11,
     .a = fixpp::dict::application_version::Unknown,
     .ns = "vt11"},
};

// Highest tag the v1.0 locked set uses; the Length+Data scan walks [1, kMaxTag]
// in ascending order so the emitted pair table is bytewise-stable (A4).
constexpr std::uint16_t kMaxTag = 2500;

}  // namespace

VersionIR build_ir(std::filesystem::path const& xml_path, std::pmr::memory_resource* mr) {
    fixpp::dict::XmlLoader loader;
    fixpp::dict::Dictionary dict = loader.load(xml_path, mr);  // throws on bad XML

    VersionIR ir;
    ir.session = dict.which_session_version();

    bool mapped = false;
    for (auto const& vm : kCodegenVersions) {
        if (vm.s == ir.session) {
            ir.application = vm.a;
            ir.ns = vm.ns;
            mapped = true;
            break;
        }
    }
    if (!mapped) {
        throw std::runtime_error(
            "fixpp-codegen: XML version is not a codegen-target version "
            "(v42/v44/v50sp2/vt11); runtime-XML-only versions get no typed "
            "namespace per [2c §1.3]");
    }

    // Messages — Dictionary::messages() is bytewise-sorted by MsgType
    // (002 research D-6), so iteration preserves determinism (A4 / NFR-003-7).
    // Per message, the RC#5 additive accessors give the full field run +
    // tag→name (D-24); message_fields() preserves the deterministic
    // per-MsgType concatenated order.
    for (auto const& m : dict.messages()) {
        MessageIR msg{
            .msg_type = std::string(m.msg_type), .name = std::string(m.name), .fields = {}};
        for (fixpp::dict::FieldRef const& fr : dict.message_fields(m.msg_type)) {
            msg.fields.push_back(FieldIR{.ref = fr, .name = std::string(dict.field_name(fr.tag))});
        }
        ir.messages.push_back(std::move(msg));
    }

    // Length+Data pairs — ascending tag scan (deterministic order). AC-V4 is
    // verified exhaustively against source XML in seam #19; here we project
    // every paired LENGTH tag the loaded Dictionary knows.
    for (std::uint16_t t = 1; t <= kMaxTag; ++t) {
        std::uint16_t data_tag = dict.length_pair_data_tag(t);
        if (data_tag != 0) {
            ir.length_pairs.push_back(LengthPairIR{.length_tag = t, .data_tag = data_tag});
        }
    }

    return ir;
}

}  // namespace fixpp::codegen
