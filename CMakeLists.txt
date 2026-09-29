# MBROLA NG
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.20)
project(MBROLA_NG VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  # static C runtime: no Visual C++ redistributable needed (NVDA, SAPI hosts)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8 $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->
                      $<$<COMPILE_LANGUAGE:CXX>:/EHsc> /wd4244 /wd4267)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra> -Wno-sign-compare)
endif()

# Developer source mode (ANALYSIS 9.2): a host/tool that links mbng_langsrc
# and calls mbng::set_source_loader(load_source_language) can load language
# SOURCE folders directly. End-user builds never link mbng_langsrc.

# ---------------------------------------------------------------------------
# Portable core (no OS-specific code, no language knowledge)
# ---------------------------------------------------------------------------
set(MBNG_CORE_SOURCES
  core/src/util/text.cpp
  core/src/util/bytes.cpp
  core/src/lang/langdata.cpp
  core/src/lang/datfile.cpp
  core/src/lang/rbnf.cpp
  core/src/lang/language.cpp
  core/src/text/canon.cpp
  core/src/text/normalizer.cpp
  core/src/phon/phonology.cpp
  core/src/prosody/prosody.cpp
  core/src/engine/pipeline.cpp
  core/src/engine/loader.cpp
  core/src/engine/synthproc.cpp
  core/src/engine/capi.cpp
)
add_library(mbng_core STATIC ${MBNG_CORE_SOURCES})
target_include_directories(mbng_core PUBLIC core/include core/src)

# ---------------------------------------------------------------------------
# MBROLA_NG.dll / libmbrola_ng.so: the core with the exported C API
# and, on Windows, the SAPI 5 engine (D8)
# ---------------------------------------------------------------------------
add_library(MBROLA_NG SHARED ${MBNG_CORE_SOURCES})
target_include_directories(MBROLA_NG PRIVATE core/include core/src)
target_compile_definitions(MBROLA_NG PRIVATE MBNG_BUILD_SHARED)
if(WIN32)
  # SAPI 5 engine in the same DLL (ANALYSIS 11). The DLL registers nothing by
  # itself: only DllRegisterServer (called by the Windows installer) writes
  # the COM class and the voice tokens, so the NVDA add-on is unaffected.
  target_sources(MBROLA_NG PRIVATE core/src/sapi/sapi_engine.cpp
                 core/src/sapi/mbrola_ng.def core/src/sapi/mbrola_ng.rc)
  set_source_files_properties(core/src/sapi/mbrola_ng.rc PROPERTIES COMPILE_DEFINITIONS
    "MBNG_VER_MAJOR=${PROJECT_VERSION_MAJOR};MBNG_VER_MINOR=${PROJECT_VERSION_MINOR};MBNG_VER_PATCH=${PROJECT_VERSION_PATCH}")
  target_link_libraries(MBROLA_NG PRIVATE sapi ole32 advapi32 shell32)
endif()

# ---------------------------------------------------------------------------
# mbrola_ng_synth: unmodified MBROLA + pipe loop. SEPARATE PROGRAM, AGPL v3
# (ANALYSIS 4.2). Never link MBROLA code into any other target.
# ---------------------------------------------------------------------------
set(MBROLA_DIR external/mbrola)
add_executable(mbrola_ng_synth synth/mbrola_ng_synth.c
  ${MBROLA_DIR}/Database/database.c ${MBROLA_DIR}/Database/database_old.c
  ${MBROLA_DIR}/Database/diphone_info.c ${MBROLA_DIR}/Database/hash_tab.c
  ${MBROLA_DIR}/Database/little_big.c ${MBROLA_DIR}/Database/zstring_list.c
  ${MBROLA_DIR}/Engine/diphone.c ${MBROLA_DIR}/Engine/mbrola.c
  ${MBROLA_DIR}/LibOneChannel/onechannel.c
  ${MBROLA_DIR}/Misc/audio.c ${MBROLA_DIR}/Misc/common.c ${MBROLA_DIR}/Misc/g711.c
  ${MBROLA_DIR}/Misc/mbralloc.c ${MBROLA_DIR}/Misc/vp_error.c
  ${MBROLA_DIR}/Parser/fifo.c ${MBROLA_DIR}/Parser/input_fifo.c ${MBROLA_DIR}/Parser/input_file.c
  ${MBROLA_DIR}/Parser/parser_input.c ${MBROLA_DIR}/Parser/phonbuff.c ${MBROLA_DIR}/Parser/phone.c)
target_include_directories(mbrola_ng_synth PRIVATE
  external/mbrola/Parser external/mbrola/Misc external/mbrola/LibOneChannel
  external/mbrola/Engine external/mbrola/Database)
target_compile_definitions(mbrola_ng_synth PRIVATE LIBRARY LITTLE_ENDIAN $<$<C_COMPILER_ID:MSVC>:TARGET_OS_DOS>)
if(MSVC)
  target_compile_options(mbrola_ng_synth PRIVATE /W0)
else()
  target_link_libraries(mbrola_ng_synth PRIVATE m)
endif()

# ---------------------------------------------------------------------------
# Language source parser (used by langc, mbtts and developer source mode)
# ---------------------------------------------------------------------------
add_library(mbng_langsrc STATIC
  tools/langsrc/langsrc.cpp
  tools/langsrc/testrunner.cpp
)
target_include_directories(mbng_langsrc PUBLIC tools/langsrc)
target_link_libraries(mbng_langsrc PUBLIC mbng_core)

# ---------------------------------------------------------------------------
# Developer tools (Language SDK) - never part of end-user packages
# ---------------------------------------------------------------------------
add_executable(langc tools/langc/main.cpp)
target_link_libraries(langc PRIVATE mbng_langsrc)

add_executable(mbtts tools/mbtts/main.cpp)
target_link_libraries(mbtts PRIVATE mbng_langsrc)

enable_testing()
add_test(NAME lang_hr
  COMMAND langc ${CMAKE_SOURCE_DIR}/languages/hr -o ${CMAKE_BINARY_DIR}/hr.dat)
add_test(NAME lang_en
  COMMAND langc ${CMAKE_SOURCE_DIR}/languages/en -o ${CMAKE_BINARY_DIR}/en.dat)

# ---------------------------------------------------------------------------
# Linux: Speech Dispatcher module, voice manager, install rules, packages
# (ANALYSIS 13). build: linux/build.sh
# ---------------------------------------------------------------------------
add_executable(test_ssml linux/speechd/test_ssml.cpp linux/speechd/ssml.cpp)
target_include_directories(test_ssml PRIVATE core/include)
add_test(NAME ssml COMMAND test_ssml)

if(UNIX AND NOT APPLE AND NOT ANDROID)
  include(GNUInstallDirs)
  find_package(Threads REQUIRED)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  set(MBNG_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/mbrola-ng")
  set(MBNG_SYNTH_DIR "${CMAKE_INSTALL_FULL_LIBEXECDIR}/mbrola-ng")

  # A prefix inside the home folder (linux/build.sh --user: ~/.local) is a
  # per-user installation: speech-dispatcher 0.11+ also finds modules in
  # ~/.local/libexec/speech-dispatcher-modules. Otherwise the module goes to
  # the system module folder, which differs between distributions.
  set(MBNG_USER_INSTALL OFF)
  if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "" AND NOT "$ENV{HOME}" STREQUAL "/")
    string(FIND "${CMAKE_INSTALL_PREFIX}/" "$ENV{HOME}/" _home_pos)
    if(_home_pos EQUAL 0)
      set(MBNG_USER_INSTALL ON)
    endif()
  endif()
  set(MBNG_SPEECHD_MODULE_DIR "" CACHE PATH "Speech Dispatcher output module folder (empty = detect)")
  if(NOT MBNG_SPEECHD_MODULE_DIR)
    if(MBNG_USER_INSTALL)
      set(MBNG_SPEECHD_MODULE_DIR "${CMAKE_INSTALL_PREFIX}/libexec/speech-dispatcher-modules")
    else()
      find_package(PkgConfig QUIET)
      if(PKG_CONFIG_FOUND)
        pkg_get_variable(_sd_modules speech-dispatcher modulebindir)
      endif()
      if(_sd_modules)
        set(MBNG_SPEECHD_MODULE_DIR "${_sd_modules}")
      else()
        set(MBNG_SPEECHD_MODULE_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/speech-dispatcher-modules")
        foreach(_d "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/speech-dispatcher-modules"
                   "/usr/libexec/speech-dispatcher-modules" "/usr/lib/speech-dispatcher-modules"
                   "/usr/lib64/speech-dispatcher-modules")
          if(EXISTS "${_d}")
            set(MBNG_SPEECHD_MODULE_DIR "${_d}")
            break()
          endif()
        endforeach()
      endif()
    endif()
  endif()
  message(STATUS "Speech Dispatcher module folder: ${MBNG_SPEECHD_MODULE_DIR}")

  add_executable(sd_mbrola_ng linux/speechd/sd_mbrola_ng.cpp linux/speechd/ssml.cpp linux/speechd/voices.cpp)
  target_link_libraries(sd_mbrola_ng PRIVATE mbng_core Threads::Threads)
  target_compile_definitions(sd_mbrola_ng PRIVATE
    MBNG_DATADIR="${MBNG_DATADIR}"
    MBNG_SYNTH_PATH="${MBNG_SYNTH_DIR}/mbrola_ng_synth"
    MBNG_VERSION_STRING="${PROJECT_VERSION}")

  # build tree = the installed share/mbrola-ng layout (usable for tests)
  set(MBNG_STAGE "${CMAKE_BINARY_DIR}/share/mbrola-ng")
  file(GLOB _lang_dirs LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/languages/*")
  set(_dats "")
  foreach(_dir ${_lang_dirs})
    if(IS_DIRECTORY "${_dir}")
      get_filename_component(_code "${_dir}" NAME)
      file(GLOB _srcs CONFIGURE_DEPENDS "${_dir}/*")
      add_custom_command(OUTPUT "${MBNG_STAGE}/languages/${_code}.dat"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MBNG_STAGE}/languages"
        COMMAND langc "${_dir}" -o "${MBNG_STAGE}/languages/${_code}.dat"
        DEPENDS langc ${_srcs}
        COMMENT "Compiling and testing language ${_code}")
      list(APPEND _dats "${MBNG_STAGE}/languages/${_code}.dat")
    endif()
  endforeach()
  configure_file(catalog/catalog.json "${MBNG_STAGE}/catalog.json" COPYONLY)
  set(MBNG_VERSION "${PROJECT_VERSION}")
  configure_file(linux/mbrola-ng-voices.py "${CMAKE_BINARY_DIR}/mbrola-ng-voices" @ONLY)
  add_custom_command(OUTPUT "${MBNG_STAGE}/voices.d/.stamp"
    COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/linux/mbrola-ng-voices.py"
            --catalog "${CMAKE_SOURCE_DIR}/catalog/catalog.json" write-voice-files "${MBNG_STAGE}/voices.d"
    COMMAND ${CMAKE_COMMAND} -E touch "${MBNG_STAGE}/voices.d/.stamp"
    DEPENDS catalog/catalog.json linux/mbrola-ng-voices.py
    COMMENT "Writing voice descriptions")
  add_custom_target(mbng_linux_data ALL DEPENDS ${_dats} "${MBNG_STAGE}/voices.d/.stamp")

  install(TARGETS mbrola_ng_synth DESTINATION "${MBNG_SYNTH_DIR}")
  install(TARGETS sd_mbrola_ng DESTINATION "${MBNG_SPEECHD_MODULE_DIR}")
  install(PROGRAMS "${CMAKE_BINARY_DIR}/mbrola-ng-voices" DESTINATION "${CMAKE_INSTALL_FULL_BINDIR}")
  install(FILES ${_dats} DESTINATION "${MBNG_DATADIR}/languages")
  install(FILES catalog/catalog.json DESTINATION "${MBNG_DATADIR}")
  install(DIRECTORY "${MBNG_STAGE}/voices.d/" DESTINATION "${MBNG_DATADIR}/voices.d"
          FILES_MATCHING PATTERN "*.voice")
  if(NOT MBNG_USER_INSTALL)
    install(FILES linux/speechd/mbrola_ng.conf DESTINATION /etc/speech-dispatcher/modules)
  endif()
  install(FILES README.md LICENSE linux/README-linux.md DESTINATION "${CMAKE_INSTALL_FULL_DOCDIR}")
  install(FILES synth/LICENSE DESTINATION "${CMAKE_INSTALL_FULL_DOCDIR}" RENAME LICENSE.mbrola_ng_synth)

  # packages: cpack -G DEB / cpack -G TGZ (linux/build.sh --deb)
  set(CPACK_PACKAGE_NAME "mbrola-ng")
  set(CPACK_PACKAGE_VENDOR "Darko Milošević")
  set(CPACK_PACKAGE_CONTACT "Darko Milošević <daremc86@gmail.com>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MBROLA NG text-to-speech (Croatian, English) for Speech Dispatcher and Orca")
  set(CPACK_SET_DESTDIR ON)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "speech-dispatcher (>= 0.11), python3")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SUGGESTS "mbrola-cr1, mbrola-us1, mbrola-us2, mbrola-us3, mbrola-en1")
  set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "MBROLA NG text-to-speech for Speech Dispatcher and Orca.\n Croatian and English with the MBROLA diphone voices; voices are downloaded\n with mbrola-ng-voices or taken from the distribution's mbrola-* packages.")
  file(WRITE "${CMAKE_BINARY_DIR}/debian/conffiles" "/etc/speech-dispatcher/modules/mbrola_ng.conf\n")
  set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_BINARY_DIR}/debian/conffiles")
  include(CPack)
endif()
