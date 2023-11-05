# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# This file is part of the transition from Autotools to CMake. Once CMake
# support has been merged we should switch to using the upstream CMake
# buildsystem.

include(CheckCXXSymbolExists)
check_cxx_symbol_exists(F_FULLFSYNC "fcntl.h" HAVE_FULLFSYNC)

add_library(leveldb STATIC EXCLUDE_FROM_ALL
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/builder.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/c.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/db_impl.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/db_iter.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/dbformat.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/dumpfile.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/filename.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/log_reader.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/log_writer.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/memtable.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/repair.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/table_cache.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/version_edit.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/version_set.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/db/write_batch.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/table/block.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/table/block_builder.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/table/filter_block.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/table/format.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/table/iterator.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/table/merger.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/table/table.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/table/table_builder.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/table/two_level_iterator.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/arena.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/bloom.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/cache.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/coding.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/comparator.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/crc32c.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/env.cc
  $<$<NOT:$<BOOL:${WIN32}>>:${PROJECT_SOURCE_DIR}/src/leveldb/util/env_posix.cc>
  $<$<BOOL:${WIN32}>:${PROJECT_SOURCE_DIR}/src/leveldb/util/env_windows.cc>
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/filter_policy.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/hash.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/histogram.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/logging.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/options.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/util/status.cc
  ${PROJECT_SOURCE_DIR}/src/leveldb/helpers/memenv/memenv.cc
)

target_compile_definitions(leveldb
  PRIVATE
    HAVE_SNAPPY=0
    HAVE_CRC32C=1
    HAVE_FDATASYNC=$<BOOL:${HAVE_FDATASYNC}>
    HAVE_FULLFSYNC=$<BOOL:${HAVE_FULLFSYNC}>
    HAVE_O_CLOEXEC=$<BOOL:${HAVE_O_CLOEXEC}>
    FALLTHROUGH_INTENDED=[[fallthrough]]
    LEVELDB_IS_BIG_ENDIAN=${WORDS_BIGENDIAN}
    $<$<NOT:$<BOOL:${WIN32}>>:LEVELDB_PLATFORM_POSIX>
    $<$<BOOL:${WIN32}>:LEVELDB_PLATFORM_WINDOWS>
    $<$<BOOL:${WIN32}>:_UNICODE;UNICODE>
    $<$<BOOL:${MINGW}>:__USE_MINGW_ANSI_STDIO=1>
)

target_include_directories(leveldb
  PRIVATE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb>
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/leveldb/include>
)

if(MSVC)
  target_compile_options(leveldb
    PRIVATE
      # Disable warning C4244 "'conversion' conversion from 'type1' to 'type2', possible loss of data".
      # Also see: https://github.com/google/leveldb/pull/1155.
      /wd4244
      # Disable warning C4267 "'var' : conversion from 'size_t' to 'type', possible loss of data".
      /wd4267
      # Disable warning C4722 "'function' : destructor never returns, potential memory leak".
      $<$<CONFIG:Release>:/wd4722>
  )
  target_compile_definitions(leveldb
    PRIVATE
      # Disable warning C4996 "The POSIX name for this item is deprecated. ...".
      _CRT_NONSTDC_NO_DEPRECATE
      # Disable warning C4996 "This function or variable may be unsafe. Consider using safe-version instead. ..."
      _CRT_SECURE_NO_WARNINGS
  )
endif()

#TODO: figure out how to filter out:
# -Wconditional-uninitialized -Werror=conditional-uninitialized -Wsuggest-override -Werror=suggest-override

target_link_libraries(leveldb PRIVATE core crc32c)
