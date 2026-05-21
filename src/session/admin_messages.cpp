// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/admin_messages.cpp
//
// fixpp::session admin-message build/interpret out-of-line bodies (005, T018 stub).
//
// Phase 2 ships STUBS that link cleanly (T018). The full implementations land
// per user story:
//   T021 (Phase 3 / US1): build_logon / interpret_logon (HeartBtInt, CompID,
//         BeginString gate) over wire::Writer + typed dictionary access.
//   T040 (Phase 5 / US3): build_heartbeat / build_test_request + TestReqID echo.
//   T046 (Phase 6 / US4): build_logout.
//   T054 (Phase 7 / US5): build_reject (RefSeqNum/RefTagID/RefMsgType/
//         SessionRejectReason) + no-reject-loop guard (I-5).
//
// Anchors: data-model.md E5; contracts/admin_messages.hpp; spec FR-002..007;
// [FIX-SL §4.2]–§4.6.
#include <fixpp/session/admin_messages.hpp>

namespace fixpp::session {

[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_logon(std::span<std::byte> /*out*/,
                seqnum_t /*seq*/,
                std::string_view /*sender_comp_id*/,
                std::string_view /*target_comp_id*/,
                std::string_view /*begin_string*/,
                int /*heartbt_int*/) noexcept {
    // PLACEHOLDER — body lands T021 (Phase 3 / US1).
    return std::unexpected(fixpp::core::error::wire_required_field_missing);
}

[[nodiscard]] fixpp::core::expected_t<int>
    interpret_logon(std::span<const std::byte> /*frame*/,
                    std::string_view /*expected_sender*/,
                    std::string_view /*expected_target*/,
                    std::string_view /*expected_begin*/) noexcept {
    // PLACEHOLDER — body lands T024/T025 (Phase 3 / US1).
    return std::unexpected(fixpp::core::error::wire_required_field_missing);
}

[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_logout(std::span<std::byte> /*out*/,
                 seqnum_t /*seq*/,
                 std::string_view /*sender_comp_id*/,
                 std::string_view /*target_comp_id*/,
                 std::string_view /*text*/) noexcept {
    // PLACEHOLDER — body lands T046 (Phase 6 / US4).
    return std::unexpected(fixpp::core::error::wire_required_field_missing);
}

[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_heartbeat(std::span<std::byte> /*out*/,
                    seqnum_t /*seq*/,
                    std::string_view /*sender_comp_id*/,
                    std::string_view /*target_comp_id*/,
                    std::string_view /*test_req_id*/) noexcept {
    // PLACEHOLDER — body lands T040 (Phase 5 / US3).
    return std::unexpected(fixpp::core::error::wire_required_field_missing);
}

[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_test_request(std::span<std::byte> /*out*/,
                       seqnum_t /*seq*/,
                       std::string_view /*sender_comp_id*/,
                       std::string_view /*target_comp_id*/,
                       std::string_view /*test_req_id*/) noexcept {
    // PLACEHOLDER — body lands T040 (Phase 5 / US3).
    return std::unexpected(fixpp::core::error::wire_required_field_missing);
}

[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>>
    build_reject(std::span<std::byte> /*out*/,
                 seqnum_t /*seq*/,
                 std::string_view /*sender_comp_id*/,
                 std::string_view /*target_comp_id*/,
                 seqnum_t /*ref_seq_num*/,
                 int /*ref_tag_id*/,
                 std::string_view /*ref_msg_type*/,
                 int /*session_reject_reason*/) noexcept {
    // PLACEHOLDER — body lands T054 (Phase 7 / US5).
    return std::unexpected(fixpp::core::error::wire_required_field_missing);
}

}  // namespace fixpp::session
