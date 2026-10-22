# toolchain.cmake: cross-compiling for Windows (x86_64) from Linux with clang-cl and lld-link, against Microsoft's C
# runtime and Windows SDK as xwin lays them out (tools/windows/build_windows.sh fetches them into build/xwin; their
# licence is Microsoft's: they are only used to build, never committed or shipped).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(XWIN "${_root}/build/xwin" CACHE PATH "xwin's splat of the CRT and Windows SDK")

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)

set(_inc "/imsvc ${XWIN}/crt/include /imsvc ${XWIN}/sdk/include/ucrt /imsvc ${XWIN}/sdk/include/um /imsvc ${XWIN}/sdk/include/shared")
set(CMAKE_C_FLAGS_INIT "${_inc}")
set(CMAKE_CXX_FLAGS_INIT "${_inc} /EHsc")
set(CMAKE_RC_FLAGS_INIT "/I ${XWIN}/sdk/include/um /I ${XWIN}/sdk/include/shared /I ${XWIN}/sdk/include/ucrt")
set(_lib "/libpath:${XWIN}/crt/lib/x86_64 /libpath:${XWIN}/sdk/lib/um/x86_64 /libpath:${XWIN}/sdk/lib/ucrt/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_lib}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_lib}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_lib}")

# Programs are the build machine's; libraries and headers the target's.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# SDL2 for Windows comes with RT64 (src/contrib/mupen64plus-win32-deps).
set(SDL2_DIR "${_root}/lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/cmake" CACHE PATH "")

# xwin brings the release C runtime only (no msvcrtd.lib): every configuration, CMake's own checks included, uses it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
# RT64's Windows SDL2 package (and some bundled libraries) declare CMake versions current CMake no longer accepts.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(RT64_HOST_FILE_TO_C "${_root}/build/cmake/file_to_c" CACHE FILEPATH "file_to_c built for this machine (by the Linux build)")

# RT64 includes some SDK headers with other capitals than the SDK's (Windows doesn't mind, Linux does): links with its
# spelling, on the include path.
set(_case "${_root}/build/windows-case")
file(MAKE_DIRECTORY "${_case}")
foreach(_pair "Shlobj.h=include/um/shlobj.h" "ShellScalingAPI.h=include/um/shellscalingapi.h"
              "Shcore.lib=lib/um/x86_64/shcore.lib")
    string(REPLACE "=" ";" _p "${_pair}")
    list(GET _p 0 _name)
    list(GET _p 1 _target)
    if (NOT EXISTS "${_case}/${_name}")
        file(CREATE_LINK "${XWIN}/sdk/${_target}" "${_case}/${_name}" SYMBOLIC)
    endif()
endforeach()
string(APPEND CMAKE_C_FLAGS_INIT " /imsvc ${_case}")
string(APPEND CMAKE_CXX_FLAGS_INIT " /imsvc ${_case}")
