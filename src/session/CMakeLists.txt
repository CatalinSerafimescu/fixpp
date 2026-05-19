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

# Allowed dependencies: core, dictionary, wire, transport (interface),
#                        log (interface), otel (interface) [arch §2.3]
#                        + asio (awaitable / cancellation slots — 007).
target_link_libraries(fixpp_session PUBLIC
  fixpp_core
  fixpp_dictionary
  fixpp_wire
  fixpp_transport
  fixpp_log
  fixpp_otel
  asio::asio
)
