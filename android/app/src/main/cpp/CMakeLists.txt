# MBROLA NG - Android native code (ANALYSIS 14.1)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   libmbrola_ng_jni.so     the portable core + the JNI bridge
#   libmbrola_ng_synth.so   NOT a library: the mbrola_ng_synth executable
#                           (MBROLA, AGPL v3, separate process). It carries a
#                           lib*.so name so that the APK installs it into the
#                           app's native library folder, the only place an
#                           app may execute a program from.
cmake_minimum_required(VERSION 3.22.1)
project(mbrola_ng_android C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../cmake/sources.cmake)

add_library(mbrola_ng_jni SHARED mbng_jni.cpp ${MBNG_CORE_SOURCES})
target_include_directories(mbrola_ng_jni PRIVATE ${MBNG_ROOT}/core/include ${MBNG_ROOT}/core/src)
target_compile_options(mbrola_ng_jni PRIVATE -Wall -Wextra -Wno-sign-compare -fvisibility=hidden)
target_link_libraries(mbrola_ng_jni PRIVATE log)

add_executable(mbrola_ng_synth ${MBNG_SYNTH_SOURCES})
target_include_directories(mbrola_ng_synth PRIVATE ${MBNG_SYNTH_INCLUDES})
target_compile_definitions(mbrola_ng_synth PRIVATE LIBRARY LITTLE_ENDIAN)
target_compile_options(mbrola_ng_synth PRIVATE ${MBNG_SYNTH_OPTIONS})
target_link_libraries(mbrola_ng_synth PRIVATE m)
set_target_properties(mbrola_ng_synth PROPERTIES
  OUTPUT_NAME "libmbrola_ng_synth.so"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

# 16 KB memory pages (Android 15+ devices, required by Google Play)
foreach(t mbrola_ng_jni mbrola_ng_synth)
  target_link_options(${t} PRIVATE "-Wl,-z,max-page-size=16384")
endforeach()
