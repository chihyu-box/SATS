cmake_minimum_required(VERSION 3.25)
project(sats CXX)

# A cache entry, so that a fetched SystemC (whose CMakeLists sets its own cache default of 98)
# is built with the same standard.
set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard for SATS and a fetched SystemC")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# SystemC is fetched and built here.
set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)   # keep the fetched copy out of ~/.cmake/packages
include(FetchContent)
FetchContent_Declare(
    SystemCLanguage
    GIT_REPOSITORY https://github.com/accellera-official/systemc.git
    GIT_TAG        2.3.4
    OVERRIDE_FIND_PACKAGE   # DRAMSys's find_package(SystemCLanguage) resolves to this copy
)
FetchContent_MakeAvailable(SystemCLanguage)

# DRAMSys is the DRAM model, vendored as a submodule (`git submodule update --init --recursive` on a
# fresh clone). Added as a subdirectory it would look for its dependencies on the system, so tell it to
# fetch them itself -- all but SystemC, which it takes from the copy fetched above.
set(DRAMSYS_USE_FETCH_CONTENT_NLOHMANN_JSON ON)
set(DRAMSYS_USE_FETCH_CONTENT_SQLITE3 ON)
set(DRAMSYS_USE_FETCH_CONTENT_DRAMPOWER ON)
set(DRAMSYS_USE_FETCH_CONTENT_FMT ON)
add_subdirectory(DRAMSys)
add_subdirectory(src)
add_subdirectory(testbench)
