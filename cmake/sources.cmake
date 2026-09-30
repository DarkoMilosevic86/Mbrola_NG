# MBROLA NG - source lists shared by the desktop build (CMakeLists.txt) and
# the Android build (android/app/src/main/cpp/CMakeLists.txt)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later

get_filename_component(MBNG_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Portable core (no OS-specific code except engine/synthproc.cpp)
set(MBNG_CORE_SOURCES
  ${MBNG_ROOT}/core/src/util/text.cpp
  ${MBNG_ROOT}/core/src/util/bytes.cpp
  ${MBNG_ROOT}/core/src/lang/langdata.cpp
  ${MBNG_ROOT}/core/src/lang/datfile.cpp
  ${MBNG_ROOT}/core/src/lang/rbnf.cpp
  ${MBNG_ROOT}/core/src/lang/language.cpp
  ${MBNG_ROOT}/core/src/text/canon.cpp
  ${MBNG_ROOT}/core/src/text/normalizer.cpp
  ${MBNG_ROOT}/core/src/phon/phonology.cpp
  ${MBNG_ROOT}/core/src/prosody/prosody.cpp
  ${MBNG_ROOT}/core/src/engine/pipeline.cpp
  ${MBNG_ROOT}/core/src/engine/loader.cpp
  ${MBNG_ROOT}/core/src/engine/synthproc.cpp
  ${MBNG_ROOT}/core/src/engine/capi.cpp
)

# mbrola_ng_synth: unmodified MBROLA + pipe loop (separate program, AGPL v3)
set(MBROLA_DIR ${MBNG_ROOT}/external/mbrola)
if(MSVC)
  set(MBNG_SYNTH_SOURCES
    ${MBNG_ROOT}/synth/mbrola_ng_synth.c
    ${MBROLA_DIR}/Database/database.c ${MBROLA_DIR}/Database/database_old.c
    ${MBROLA_DIR}/Database/diphone_info.c ${MBROLA_DIR}/Database/hash_tab.c
    ${MBROLA_DIR}/Database/little_big.c ${MBROLA_DIR}/Database/zstring_list.c
    ${MBROLA_DIR}/Engine/diphone.c ${MBROLA_DIR}/Engine/mbrola.c
    ${MBROLA_DIR}/LibOneChannel/onechannel.c
    ${MBROLA_DIR}/Misc/audio.c ${MBROLA_DIR}/Misc/common.c ${MBROLA_DIR}/Misc/g711.c
    ${MBROLA_DIR}/Misc/mbralloc.c ${MBROLA_DIR}/Misc/vp_error.c
    ${MBROLA_DIR}/Parser/fifo.c ${MBROLA_DIR}/Parser/input_fifo.c ${MBROLA_DIR}/Parser/input_file.c
    ${MBROLA_DIR}/Parser/parser_input.c ${MBROLA_DIR}/Parser/phonbuff.c ${MBROLA_DIR}/Parser/phone.c)
  set(MBNG_SYNTH_OPTIONS /W0)
else()
  # GCC / Clang: MBROLA's library is ONE translation unit, lib1.c, which
  # includes every source file (as MBROLA's own Makefile builds it; the files
  # do not compile separately outside Visual C++).
  set(MBNG_SYNTH_SOURCES
    ${MBNG_ROOT}/synth/mbrola_ng_synth.c
    ${MBROLA_DIR}/LibOneChannel/lib1.c)
  # -Wno-incompatible-pointer-types: input_fifo.c assigns an int-returning
  # function to a long-returning pointer (an error in GCC 14 / Clang 16);
  # the value is only tested for zero, so it is harmless.
  set(MBNG_SYNTH_OPTIONS -w -Wno-incompatible-pointer-types)
endif()
set(MBNG_SYNTH_INCLUDES
  ${MBROLA_DIR}/Parser ${MBROLA_DIR}/Misc ${MBROLA_DIR}/LibOneChannel
  ${MBROLA_DIR}/Engine ${MBROLA_DIR}/Database)
