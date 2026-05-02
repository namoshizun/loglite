set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    message(FATAL_ERROR "linux-musl-arm64 builds must run inside a Linux arm64 environment")
endif()

set(LOGLITE_STATIC_MUSL ON CACHE BOOL "Build a fully static Linux/musl executable")
