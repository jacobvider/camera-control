find_package(Threads REQUIRED)

configure_file(include/superlotis/version.hpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/superlotis/version.hpp @ONLY)

# Shared compile settings for every target in this project.
add_library(superlotis_options INTERFACE)
target_compile_options(superlotis_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion>)
if(SUPERLOTIS_WERROR)
    target_compile_options(superlotis_options INTERFACE $<$<CXX_COMPILER_ID:GNU,Clang>:-Werror>)
endif()
if(SUPERLOTIS_SANITIZE)
    target_compile_options(superlotis_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(superlotis_options INTERFACE -fsanitize=address,undefined)
endif()
# GCC < 9 (e.g. devtoolset-8 on CentOS 7) keeps std::filesystem in a separate library.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(superlotis_options INTERFACE stdc++fs)
endif()

add_library(superlotis_core STATIC
    src/config.cpp
    src/controller.cpp
    src/fits_writer.cpp
    src/log.cpp
    src/protocol.cpp
    src/simulated_backend.cpp
    src/tcp_server.cpp
    src/types.cpp
    src/util.cpp)
target_include_directories(superlotis_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(superlotis_core PUBLIC superlotis_options Threads::Threads)

if(SUPERLOTIS_WITH_PICAM)
    set(PICAM_ROOT "/opt/PrincetonInstruments/picam" CACHE PATH "PICam SDK install prefix")
    find_path(PICAM_INCLUDE_DIR picam.h
        HINTS ${PICAM_ROOT}/includes ${PICAM_ROOT}/include /usr/local/include)
    find_library(PICAM_LIBRARY picam
        HINTS ${PICAM_ROOT}/runtime ${PICAM_ROOT}/lib /usr/local/lib)
    if(NOT PICAM_INCLUDE_DIR OR NOT PICAM_LIBRARY)
        message(FATAL_ERROR "SUPERLOTIS_WITH_PICAM=ON but the PICam SDK was not found. "
                            "Set -DPICAM_ROOT=/path/to/picam.")
    endif()
    message(STATUS "PICam: ${PICAM_LIBRARY}")
    target_sources(superlotis_core PRIVATE src/picam_backend.cpp)
    target_include_directories(superlotis_core SYSTEM PRIVATE ${PICAM_INCLUDE_DIR})
    target_link_libraries(superlotis_core PUBLIC ${PICAM_LIBRARY} ${CMAKE_DL_LIBS})
    target_compile_definitions(superlotis_core PUBLIC SUPERLOTIS_WITH_PICAM=1)
endif()

add_executable(slcamd src/main.cpp)
target_link_libraries(slcamd PRIVATE superlotis_core)

install(TARGETS slcamd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/config/slcamd.example.conf
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/superlotis)
install(FILES ${PROJECT_SOURCE_DIR}/deploy/systemd/slcamd.service
        DESTINATION lib/systemd/system)
