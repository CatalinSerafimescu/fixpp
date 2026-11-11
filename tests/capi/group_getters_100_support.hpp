#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/group_getters_100_support.hpp — TEST-ONLY C-ABI support for
// 100-wire-group-read-integrity's group getter cells (tasks T010 to T012).
//
//   - `inbound_handle`: a stack `MessageView<Index>` wrapped as `fixpp_msg_t`, as
//     tests/capi/message_read_failloud_test.cpp wraps one.
//   - `get_group` / `get_nested`: call the C getters and turn an exception that leaves one into
//     the sentinel `kEscaped`, so a cell reports it as a wrong code instead of the process
//     terminating. Each pre-sets its outputs to a sentinel unless the caller asks for null
//     pointers, so a cell can tell "written null/0" from "not written".
//   - `rc_name`: a printable name for a code, for failure messages.

#include <cstddef>
#include <cstdint>
#include <string>

#include "capi_internal.hpp"
#include "fix/c_api/error.h"
#include "fix/c_api/message.h"

namespace fixpp::test100 {

struct inbound_handle {
    fixpp_msg msg{};
    explicit inbound_handle(fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& mv) {
        msg.view = &mv;
    }
    [[nodiscard]] fixpp_msg_t const* ptr() const noexcept {
        return reinterpret_cast<fixpp_msg_t const*>(&msg);
    }
};

// An exception left the getter.
inline constexpr int kEscaped = -1000;

[[nodiscard]] inline std::string rc_name(int rc) {
    switch (rc) {
        case FIXPP_ERR_OK:
            return "FIXPP_ERR_OK";
        case FIXPP_ERR_TAG_NOT_FOUND:
            return "FIXPP_ERR_TAG_NOT_FOUND";
        case FIXPP_ERR_TYPE_MISMATCH:
            return "FIXPP_ERR_TYPE_MISMATCH";
        case FIXPP_ERR_WIRE_LIMIT_EXCEEDED:
            return "FIXPP_ERR_WIRE_LIMIT_EXCEEDED";
        case FIXPP_ERR_NULL_HANDLE:
            return "FIXPP_ERR_NULL_HANDLE";
        case FIXPP_ERR_INVALID_HANDLE:
            return "FIXPP_ERR_INVALID_HANDLE";
        case FIXPP_ERR_INDEX_OUT_OF_RANGE:
            return "FIXPP_ERR_INDEX_OUT_OF_RANGE";
        case kEscaped:
            return "an exception escaped the getter";
        default:
            return "code " + std::to_string(rc);
    }
}

// The outputs of one getter call.
struct read_out {
    int rc = -1;
    fixpp_group_t const* g = nullptr;
    std::size_t count = 0;
};

// Sentinels written into the outputs before a call; a getter that returns without writing them
// leaves them in place.
inline fixpp_group_t const* const kSentinelGroup =
    reinterpret_cast<fixpp_group_t const*>(static_cast<std::uintptr_t>(0x100100));
inline constexpr std::size_t kSentinelCount = 0x100100;

[[nodiscard]] inline read_out get_group(fixpp_msg_t const* m, std::uint16_t tag) noexcept {
    read_out r;
    r.g = kSentinelGroup;
    r.count = kSentinelCount;
    try {
        r.rc = fixpp_msg_get_group(m, tag, &r.g, &r.count);
    } catch (...) {
        r.rc = kEscaped;
    }
    return r;
}

[[nodiscard]] inline read_out get_nested(fixpp_group_t const* g, std::size_t i,
                                         std::uint16_t tag) noexcept {
    read_out r;
    r.g = kSentinelGroup;
    r.count = kSentinelCount;
    try {
        r.rc = fixpp_group_get_nested_group(g, i, tag, &r.g, &r.count);
    } catch (...) {
        r.rc = kEscaped;
    }
    return r;
}

}  // namespace fixpp::test100
