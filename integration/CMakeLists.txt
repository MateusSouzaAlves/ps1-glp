cmake_minimum_required(VERSION 3.22.1)
project(emuorbit_ps1_native_components LANGUAGES C CXX)
include(../cmake/protected_symbols.cmake)
emuorbit_compute_ds_symbols("${EMUORBIT_BUILD_SEED}" "${ANDROID_ABI}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(../app/src/main/cpp/bridge/protected_ds_symbols.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/protected_ds_symbols.h" @ONLY)
find_library(log_library log REQUIRED)
find_library(dl_library dl REQUIRED)
# PS1 keeps its own session/callback state and dynamically loads the pinned
# libretro core. Never link retro_* into the existing GB/DS bridge.
add_library(emuorbit_ps1_session SHARED
        ../app/src/main/cpp/playstation/session_jni.cpp
        ../app/src/main/cpp/bridge/frontend_callbacks.cpp ../app/src/main/cpp/bridge/input_callbacks.cpp
        ../app/src/main/cpp/bridge/core_options.cpp ../app/src/main/cpp/bridge/native_runtime.cpp)
target_compile_features(emuorbit_ps1_session PRIVATE cxx_std_17)
target_compile_definitions(emuorbit_ps1_session PRIVATE EMUORBIT_PROTECTED_DS=1)
target_include_directories(emuorbit_ps1_session PRIVATE ../app/src/main/cpp/bridge
        "${CMAKE_CURRENT_BINARY_DIR}/generated" include)
target_compile_options(emuorbit_ps1_session PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_options(emuorbit_ps1_session PRIVATE -Wl,-Bsymbolic -Wl,--build-id
        -Wl,-z,max-page-size=16384 -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
target_link_libraries(emuorbit_ps1_session PRIVATE ${log_library} ${dl_library})

# Bounded positional CHD reader used by SAF validation/preparation.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CHDR_WANT_TESTS OFF CACHE BOOL "" FORCE)
set(CHDR_VERIFY_BLOCK_CRC ON CACHE BOOL "" FORCE)
set(CHDR_LOWRAM_MAP OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(../core/deps/libchdr ps1-libchdr EXCLUDE_FROM_ALL)
add_library(emuorbit_ps1_chd SHARED ../app/src/main/cpp/playstation/chd_reader_jni.cpp)
target_compile_features(emuorbit_ps1_chd PRIVATE cxx_std_17)
target_compile_options(emuorbit_ps1_chd PRIVATE -Wall -Wextra -Werror)
target_link_options(emuorbit_ps1_chd PRIVATE -Wl,--build-id -Wl,-z,max-page-size=16384
        -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
target_link_libraries(emuorbit_ps1_chd PRIVATE chdr-static)
