# DETH targets Linux (SteamOS), Android and Windows (x64). Other platforms from DevilutionX were removed.
if(ANDROID)
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Android")
    include(platforms/android_termux)
  else()
    include(platforms/android)
  endif()
elseif(WIN32)
  include(platforms/windows)
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "DETH only builds for Linux, Android and Windows. Use upstream DevilutionX for other platforms.")
endif()

set(TARGET_PLATFORM host CACHE STRING "Target platform")
