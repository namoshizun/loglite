set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    message(FATAL_ERROR "linux-musl-x86_64 builds must run inside a Linux x86_64 environment")
endif()

set(LOGLITE_STATIC_MUSL ON CACHE BOOL "Build a fully static Linux/musl executable")
