// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/fixt_session_check.hpp
//
// 099-fixt-appl-version-validation (data-model E-9, research R-2, FR-003, FR-004): the one check
// Session::open() and Engine::register_session share. It replaces open()'s own FIXT guards.
#pragma once

#include <array>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/session/session_config.hpp>
#include <string_view>

namespace fixpp::session {

// Whether `d` can answer FIXT's admin messages: content, not label. All seven MsgTypes are in
// messages(), and its Logon accepts DefaultApplVerID(1137). So FIXT11 and FIX Latest pass, a
// FIX50-family file has no admin messages, and a FIX 4.4 file has them and rejects 1137. Walks
// messages(), so configuration time only.
[[nodiscard]] inline bool defines_fixt_admin(const fixpp::dict::Dictionary& d) noexcept {
    constexpr std::uint16_t kDefaultApplVerID = 1137;
    constexpr std::array<std::string_view, 7> kAdmin{"0", "1", "2", "3", "4", "5", "A"};
    auto const messages = d.messages();
    for (std::string_view const admin : kAdmin) {
        bool found = false;
        for (auto const& m : messages) {
            found = found || m.msg_type == admin;
        }
        if (!found) {
            return false;
        }
    }
    return d.field_valid_for("A", kDefaultApplVerID);
}

// False when a FIXT.1.1 config cannot run, on any of data-model E-9's four clauses. The caller
// decides when begin_string makes it apply. Each clause is computed on its own, so a config that
// fails one reaches the others safely.
[[nodiscard]] inline bool fixt_session_config_valid(
    const SessionConfig& cfg, const fixpp::dict::version_registry* registry) noexcept {
    auto const& default_appl = cfg.default_appl_ver_id;
    // No default: is_fixt() would be false and the session would emit a Logon with no 1137.
    const bool no_default = !default_appl.has_value();
    // No registry: nothing can answer the inbound Logon's serviceability test.
    const bool no_registry = registry == nullptr;
    // No dictionary for the default. get(), so the one-way fallback counts.
    const bool slot_miss = !no_default && !no_registry && !registry->get(*default_appl).has_value();
    // Validating, and neither S nor the registry's vt11 dictionary answers FIXT's admin messages.
    // A later vt11 dictionary failing the test hides an earlier one, as for every slot.
    const auto transport_answers = [&] {
        if (no_registry) {
            return false;
        }
        auto const transport = registry->transport();
        return transport.has_value() && defines_fixt_admin(**transport);
    };
    const bool no_transport = cfg.validate_inbound_messages &&
                              !(cfg.dictionary != nullptr && defines_fixt_admin(*cfg.dictionary)) &&
                              !transport_answers();
    return !(no_default || no_registry || slot_miss || no_transport);
}

}  // namespace fixpp::session
