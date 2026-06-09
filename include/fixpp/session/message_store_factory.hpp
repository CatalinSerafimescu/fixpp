// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/message_store_factory.hpp
//
// fixpp::session::MessageStoreFactory — abstract factory minting unique
// ownership of a MessageStore for a given <sender, target> session identity.
//
// History: 007 shipped the MINIMAL polymorphic-bind-target stub (deleted
// move/copy + virtual destructor only) so SessionConfig's
// shared_ptr<MessageStoreFactory> at session_config.hpp:127 could carry a
// complete type ([2d §4.5] Appendix D §D.1). 008 EXTENDS the class in place
// (preserving the class identity, the deleted move/copy, the virtual
// destructor) by adding the make() pure-virtual. 010 FR-001a amended
// SessionConfig::store_factory from unique_ptr to shared_ptr (W-5 enabler:
// makes SessionConfig copy-constructible for the by-value Session::cfg_
// membership decided at /speckit-clarify); the factory is stateless and
// the per-Session MessageStore uniqueness invariant is preserved because
// each Session calls make() to mint its own store. See specs/010-session-
// cfg-lifetime/spec.md FR-001a and the Gate A inheritance addendum at
// library/.specify/decisions/010-session-cfg-lifetime-gatea.md (T027b).
//
// Anchor: .specify/2e-msgstore.md v0.5 §4.4 (N1 — unique_ptr ownership) +
// Appendix D §D.6 (Gap 3 close — store-object deleter contract). FR-005 /
// FR-025 / research D-7. Entity E5.
//
// Mirror of specs/008-message-store/contracts/message_store_factory.hpp.
#pragma once

#include <asio/any_io_executor.hpp>
#include <fixpp/core/error.hpp>             // expected_t
#include <fixpp/session/message_store.hpp>  // MessageStore
#include <memory>
#include <memory_resource>
#include <string_view>

namespace fixpp::session {

class MessageStoreFactory {
public:
    MessageStoreFactory() = default;
    MessageStoreFactory(const MessageStoreFactory&) = delete;
    MessageStoreFactory& operator=(const MessageStoreFactory&) = delete;
    MessageStoreFactory(MessageStoreFactory&&) = delete;
    MessageStoreFactory& operator=(MessageStoreFactory&&) = delete;
    virtual ~MessageStoreFactory() = default;

    // yields_persistent_store(): true iff the stores minted by this factory
    // are durable across process restart (029 C2.2 / FR-005 / research D-10).
    // Non-pure; default true — a custom store hydrates unless it opts out
    // (safe default: unknown factory → hydrate; MemoryStoreFactory overrides
    // to false; FileStoreFactory inherits true). Accessed ONCE at Session::open()
    // to capture store_is_persistent_; no runtime overhead on the hot path.
    // Does NOT touch the MessageStore 4-pure-virtual cap (Article XIV.2 governs
    // MessageStore, not the factory; this is a factory-only surface).
    [[nodiscard]] virtual bool yields_persistent_store() const noexcept { return true; }

    // make: mint a MessageStore for the given <sender, target> identity.
    //
    // sender / target are FIX-CompID strings consumed for the on-disk log
    // filename (FileStore) and for the session_triple_hash in the sentinel
    // record (FileStore). mr is the per-session PMR resource the impl uses
    // for its internal allocations (slab, ring, framing scratch, index,
    // persisted-frame copy). max_store_memory_bytes is the engine-resolved
    // EngineConfig::max_store_memory_per_session value the engine threads
    // in at call time so the factory can enforce the storage-DoS guard
    // (FR-014 / SC-004 / I-11) without taking an EngineConfig& back-channel
    // through the constructor — keeps the factory CTOR Config-only per the
    // design-doc §4.4 frozen surface. file_io_executor is the
    // engine-resolved EngineConfig::file_io_executor value the engine
    // threads in at call time for FileStore impls that need it (FR-024 /
    // I-13 / research D-7); the FileStoreFactory populates the minted
    // FileStore::Config::file_io_executor with this value (preserving
    // [2e §4.3.2]:665 required-at-construction on FileStore itself, since
    // FileStore is constructed inside make()) UNLESS the factory's Config
    // already carries a caller-supplied executor, in which case the
    // Config-supplied executor wins (caller override; Path-B user shape).
    // MemoryStore impls ignore the file_io_executor parameter (the empty /
    // default-constructed value is permitted on the MemoryStoreFactory
    // path).
    //
    // Returns std::unique_ptr<MessageStore> ownership (N1) — no shared_ptr,
    // no sharing across sessions, no mid-session swap ([arch §5.6]).
    //
    // Store-object allocation contract (v0.5 per [2e §D.6] — Gap 3 close):
    // the make() return type std::unique_ptr<MessageStore> commits the v1.0
    // contract to std::default_delete<MessageStore> destruction (the default
    // unique_ptr deleter): the concrete store object MUST be destructible
    // via `delete static_cast<MessageStore*>(p)`. Factory impls that wish
    // to use a PMR allocator for the store OBJECT ITSELF MUST wrap
    // deallocation into a std::default_delete-compatible path (typical
    // pattern: a static `operator delete` overload on the concrete store
    // class that routes back to the PMR resource, paired with
    // std::pmr::polymorphic_allocator::new_object for the matching
    // allocation). A std::unique_ptr<MessageStore, CustomDeleter> return
    // type is NOT supported in v1.0; reserved for a possible post-v1.0
    // evolution per [const §X.4]. The `mr` PMR parameter threaded into
    // make() and the [2e §6.1.1] / [2e §8] / FR-026 / FR-027 PMR contracts
    // govern ONLY the store's INTERNAL storage — they do NOT govern the
    // deleter shape of the store object itself.
    //
    // Validation: both default factories (MemoryStoreFactory,
    // FileStoreFactory) enforce the storage-DoS guard against
    // max_store_memory_bytes (FR-014 / SC-004 / I-11) using checked
    // overflow-safe arithmetic; on any overflow / cap-exceeded condition
    // returns store_factory_failed. FileStoreFactory also (per [2e §D.4] —
    // Gap 1 close) validates sender / target CompID filesystem safety
    // BEFORE composing the log path and BEFORE taking the advisory lock;
    // takes an flock / LockFileEx advisory exclusive lock on the live log
    // (FR-013 / I-16) — contention → store_factory_failed; verifies the
    // sentinel record's session_triple_hash on re-open — mismatch →
    // store_factory_failed; rejects with store_factory_failed if the
    // resolved file_io_executor (Config-supplied OR threaded-in) is empty.
    [[nodiscard]] virtual fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view sender, std::string_view target, std::pmr::memory_resource* mr,
        std::size_t max_store_memory_bytes, asio::any_io_executor file_io_executor) noexcept = 0;
};

}  // namespace fixpp::session
