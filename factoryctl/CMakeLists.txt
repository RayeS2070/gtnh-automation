cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

project(factoryctl LANGUAGES C CXX)

# userver 3.2 does not build as C++26 with libstdc++ 16 (std::make_optional<T>
# with a braced list in core/src/baggage/baggage.cpp); its standard is global.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CPM.cmake)

# Only what the server uses; userver builds no other drivers by default.
CPMAddPackage(URI "gh:userver-framework/userver@3.2"
        OPTIONS
        "USERVER_FEATURE_POSTGRESQL ON"
        # The patched driver needs a static libpq.a, which Arch does not ship;
        # without the patch it links libpq.so and loses only portals.
        "USERVER_FEATURE_PATCH_LIBPQ OFF"
        # Functional tests need a Python venv; chaotic (json schema codegen) is
        # required by core and makes one anyway.
        "USERVER_FEATURE_CHAOTIC_OPENAPI OFF"
        "USERVER_FEATURE_TESTSUITE OFF"
        # With clang it prefers static libraries, Arch ships shared ones only:
        # it would build OpenSSL and others from source instead.
        "USERVER_USE_STATIC_LIBS OFF"
)
userver_setup_environment()

# Warnings for our own targets only.
add_library(fctl_warnings INTERFACE)
target_compile_options(fctl_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -pedantic>
)

add_subdirectory(server)
