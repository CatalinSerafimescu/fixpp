# src/session/CMakeLists.txt
# fixpp::session — Session, Application, FSM, seqnum, MessageStore [arch §4.4]
#
# Promoted from INTERFACE to a compiled STATIC library in Phase 4
# (007-threading-clock T012/T014): the minimal real Session skeleton (D-4 /
# E10) ships an out-of-line src/session/session.cpp (ctor + never-null
# session_arena() resolution chain + per-story open()/close() bodies). Mirrors
# the fixpp_core INTERFACE→STATIC and fixpp_sync precedents. asio::asio is now
# a direct dependency (Session uses asio::awaitable / cancellation_state);
# core stays the asio-free leaf [arch §2.2] — the asio-dependent threading
# headers (clock/session_executor/…) are header-only and pull asio through
# whoever includes them.

add_library(fixpp_session STATIC
  session.cpp
  engine.cpp             # 015-runtime-engine T006: Engine lifecycle substrate
  session_executor.cpp   # fixpp::core::make_session_executor (T019; the
                         # core-namespace bridge defined session-side so
                         # core/ stays the asio-free leaf — [arch §2.3])
  ${CMAKE_SOURCE_DIR}/src/core/system_clock_source.cpp  # production fixpp::core
                         # default Clock impl (T028); asio-dependent so it is
                         # compiled here, NOT into the asio-free fixpp_core
                         # leaf [arch §2.2] (same rationale as the bridge).
)
add_library(fixpp::session ALIAS fixpp_session)

target_include_directories(fixpp_session PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# engine.cpp (015 T012) needs src/transport/asio_listener.hpp (internal header).
# The src/ path is PRIVATE — not exposed to consumers of fixpp_session.
# [arch §5.3 engine-bootstrap carve-out]
target_include_directories(fixpp_session PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
)

# Allowed dependencies: core, dictionary, wire, transport (interface),
#                        log (interface), otel (interface) [arch §2.3]
#                        + asio (awaitable / cancellation slots — 007).
target_link_libraries(fixpp_session PUBLIC
  fixpp_core
  fixpp_sync   # 046 (NFR-017): atomic_shared_ptr fallback shard_guard symbols (reader_snapshot_ + async_mutex)
  fixpp_dictionary
  fixpp_wire
  fixpp_transport
  fixpp_log
  fixpp_otel
  asio::asio
)

# 008-message-store: Crc32c::crc32c is added PRIVATE in T024 when file_store.cpp
# ships (per-record + sentinel CRC32 over Castagnoli polynomial 0x1EDC6F41;
# research D-3; tasks.md T003 "FileStore-touching targets only"). The find_package
# at the top-level CMakeLists.txt is already in place.

# T021 (008-message-store / US1): MemoryStore is header-only.
# NO src/session/memory_store.cpp ships. Decision rationale: hot-path
# primitives benefit from HALO-elision ([const §XI.6]) — the same
# rationale that kept 006/007 async_mutex/Clock header-inline. The
# 007 precedent for header-inline session primitives is canonical.
# See include/fixpp/session/memory_store.hpp for the full implementation.

# T024 (008-message-store / US1): FileStore compiled translation unit.
# T026 (008-message-store / US1): FileStoreFactory compiled translation unit.
# Both are added to the fixpp_session STATIC target below once T024 ships.
target_sources(fixpp_session PRIVATE
  file_store.cpp
  file_store_factory.cpp
  quickfix_compat/cfg_loader.cpp  # T047 (008-message-store / US4): QuickFIX .cfg → FileStoreFactory config translation
  # ── 005-session-establishment-fsm stubs (T018) ─────────────────────────────
  # Phase 2 scaffold: link-green before user-story bodies land (Phase 3–7).
  seqnum_manager.cpp   # T018: Phase 2 stub; full impl T031/T032/T033 (Phase 4 / US2)
  admin_messages.cpp   # T018: Phase 2 stub; full impl T021/T040/T046/T054 (Phase 3-7)
  sending_time.cpp     # T018: Phase 2 stub; full impl T022/T055 (Phase 3/7)
  # ── 013-session-reconnect-binding Phase 2 (T006–T013a) ────────────────────
  compid_authorization_policy.cpp  # T008: Phase 2 shape; behavioral body Phase 3 T023/T024
  resend_state.cpp                 # T009: Phase 2 shape; behavioral body Phase 3 T025/T026
  reconnect_fsm.cpp                # T010: Phase 2 stubs; behavioral body Phase 3 T027–T031
  # ── 020-g2-business-messages Phase 2 (T004) ───────────────────────────────
  business_messages.cpp            # T004: Phase 2 stub; builder bodies Phase 3 T008
)

target_link_libraries(fixpp_session PRIVATE Crc32c::crc32c)
