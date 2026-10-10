# cmake/FixppAsioRecycler.cmake — fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.4,
# owner ruling R-1′): read asio's recycler slot count from its one source.
#
# The value is written once, as `#define FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE <n>` in
# include/fixpp/core/detail/asio_recycler_config.hpp. Every build file and script that needs
# it calls fixpp_read_asio_recycler_cache_size() on that header; none writes the number.
#
# Usable from a project and from `cmake -P` (tests/consumer/run_consumer_witness.cmake).

# fixpp_read_asio_recycler_cache_size(<header> <out-var>)
#
# Sets <out-var> to the value. Fails the configure unless the header holds exactly one
# matching line, so a renamed or duplicated constant cannot fall back to a default.
function(fixpp_read_asio_recycler_cache_size _header _out)
  if(NOT EXISTS "${_header}")
    message(FATAL_ERROR "fixpp_read_asio_recycler_cache_size: ${_header} does not exist")
  endif()
  file(STRINGS "${_header}" _lines
       REGEX "^#define FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE [0-9]+$")
  list(LENGTH _lines _count)
  if(NOT _count EQUAL 1)
    message(FATAL_ERROR
      "fixpp_read_asio_recycler_cache_size: expected exactly one "
      "`#define FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE <n>` line in ${_header}, "
      "found ${_count}")
  endif()
  string(REGEX REPLACE "^#define FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE ([0-9]+)$" "\\1"
         _value "${_lines}")
  set(${_out} "${_value}" PARENT_SCOPE)
endfunction()
