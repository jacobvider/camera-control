find_package(GTest 1.11 QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG b514bdc898e2951020cbdca1304b75f5950d1f59  # v1.15.2, pinned by commit
        GIT_PROGRESS FALSE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(superlotis_tests
    spy_backend.hpp
    test_config.cpp
    test_controller.cpp
    test_fits_writer.cpp
    test_protocol.cpp
    test_simulated_backend.cpp
    test_tcp_server.cpp
    test_util.cpp)
target_link_libraries(superlotis_tests PRIVATE superlotis_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(superlotis_tests DISCOVERY_TIMEOUT 30)
