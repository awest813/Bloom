# SPDX-License-Identifier: GPL-2.0-only
# Use after sourcing DreamSDK's KallistiOS environment in its MSYS shell.
include("$ENV{KOS_BASE}/utils/cmake/kallistios.toolchain.cmake")

# The bundled GCC 15 runtime calls the legacy external mutex_lock symbol.
# Current KOS headers inline it, leaving libgcc's failing weak stub otherwise.
set(WITH_LEGACY_KOS_MUTEX ON CACHE BOOL "Provide legacy mutex_lock for the bundled DreamSDK runtime")

# Native Windows CMake cannot execute the upstream extensionless shell scripts
# during compiler identification. DreamSDK's launchers run those same wrappers.
set(_dreamsdk_bin "${KOS_CC_BASE}/bin")
foreach(_language C ASM OBJC)
    set(CMAKE_${_language}_COMPILER "${_dreamsdk_bin}/kos-cc.exe")
endforeach()
foreach(_language CXX OBJCXX)
    set(CMAKE_${_language}_COMPILER "${_dreamsdk_bin}/kos-c++.exe")
endforeach()
foreach(_language C CXX ASM)
    set(CMAKE_${_language}_COMPILER_AR "${_dreamsdk_bin}/kos-ar.exe")
    set(CMAKE_${_language}_COMPILER_RANLIB "${_dreamsdk_bin}/kos-ranlib.exe")
endforeach()
unset(_dreamsdk_bin)
unset(_language)
