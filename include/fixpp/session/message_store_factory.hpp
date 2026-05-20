// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/message_store_factory.hpp
//
// fixpp::session::MessageStoreFactory — MINIMAL abstract interface stub.
// SessionConfig holds a std::unique_ptr<MessageStoreFactory> (data-model E5
// pinned shape — unique ownership), so the value type needs this base
// COMPLETE now (a unique_ptr-to-incomplete member makes SessionConfig's
// implicit destructor ill-formed). Same "minimal real skeleton, downstream
// extends" pattern as D-1 (trace_context / 2k) and D-4 (Session / 005): the
// concrete store factory + the full virtual surface are owned by 2e /
// 005; this feature ships only the polymorphic bind target.
#pragma once

namespace fixpp::session {

class MessageStoreFactory {
public:
    MessageStoreFactory()                                      = default;
    MessageStoreFactory(const MessageStoreFactory&)            = delete;
    MessageStoreFactory& operator=(const MessageStoreFactory&) = delete;
    MessageStoreFactory(MessageStoreFactory&&)                 = delete;
    MessageStoreFactory& operator=(MessageStoreFactory&&)      = delete;
    virtual ~MessageStoreFactory()                             = default;
    // Full create()/MessageStore surface owned by 2e/005 (extends this base).
};

}  // namespace fixpp::session
