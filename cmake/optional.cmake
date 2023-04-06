# Copyright (c) 2023 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Optional features and packages.

if(CCACHE)
  find_program(CCACHE_EXECUTABLE ccache)
  if(CCACHE_EXECUTABLE)
    set(CCACHE ON)
    if(MSVC)
      # See https://github.com/ccache/ccache/wiki/MS-Visual-Studio
      set(MSVC_CCACHE_WRAPPER_CONTENT "\"${CCACHE_EXECUTABLE}\" \"${CMAKE_CXX_COMPILER}\"")
      set(MSVC_CCACHE_WRAPPER_FILENAME wrapped-cl.bat)
      file(WRITE ${CMAKE_BINARY_DIR}/${MSVC_CCACHE_WRAPPER_FILENAME} "${MSVC_CCACHE_WRAPPER_CONTENT} %*")
      set(CMAKE_VS_GLOBALS
        "CLToolExe=${MSVC_CCACHE_WRAPPER_FILENAME}"
        "CLToolPath=${CMAKE_BINARY_DIR}"
        "TrackFileAccess=false"
        "UseMultiToolTask=true"
        "DebugInformationFormat=OldStyle"
      )
    else()
      list(APPEND CMAKE_C_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE})
      list(APPEND CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE})
    endif()
  elseif(CCACHE STREQUAL "AUTO")
    set(CCACHE OFF)
  else()
    message(FATAL_ERROR "ccache requested, but not found.")
  endif()
  mark_as_advanced(CCACHE_EXECUTABLE)
endif()

if(WITH_NATPMP)
  find_package(NATPMP MODULE)
  if(NATPMP_FOUND)
    set(WITH_NATPMP ON)
  elseif(WITH_NATPMP STREQUAL "AUTO")
    message(WARNING "libnatpmp not found, disabling.\n"
                    "To skip libnatpmp check, use \"-DWITH_NATPMP=OFF\".\n")
    set(WITH_NATPMP OFF)
  else()
    message(FATAL_ERROR "libnatpmp requested, but not found.")
  endif()
endif()

if(WITH_MINIUPNPC)
  find_package(MiniUPnPc MODULE)
  if(MiniUPnPc_FOUND)
    set(WITH_MINIUPNPC ON)
  elseif(WITH_MINIUPNPC STREQUAL "AUTO")
    message(WARNING "libminiupnpc not found, disabling.\n"
                    "To skip libminiupnpc check, use \"-DWITH_MINIUPNPC=OFF\".\n")
    set(WITH_MINIUPNPC OFF)
  else()
    message(FATAL_ERROR "libminiupnpc requested, but not found.")
  endif()
endif()
