cmake_minimum_required(VERSION 3.22)
set(SC_MODULE fleet-feed)
project(simply-cpp-${SC_MODULE} HOMEPAGE_URL "https://github.com/roelofrossouw/simply-cpp-fleet-feed" LANGUAGES CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_FLEET_FEED_BUILD_TESTS "Build simply-cpp-fleet-feed tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

# nlohmann/json comes with sc-core; the deliveries go over sc-http (POST), sc-kafka and sc-sftp.
find_or_install_package(sc-core simply-cpp-dev simply-cpp 4.0.0)
find_or_install_package(sc-http simply-cpp-http-dev simply-cpp-http 4.0.0)
find_or_install_package(sc-kafka simply-cpp-kafka-dev simply-cpp-kafka 6.0.0)
find_or_install_package(sc-sftp simply-cpp-sftp-dev simply-cpp-sftp 4.0.0)

set(SOURCE_OBJECTS)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

# feed.h: positions from the Redis status hashes and the receivers' payloads.
add_sc_object(feed)
# sender.h: one payload to an HTTP, Kafka or SFTP receiver.
add_sc_object(sender
        LINK_LIBRARIES sc::sc-http sc::sc-kafka sc::sc-sftp
        PUBLIC_LINK_LIBRARIES sc::sc-http sc::sc-kafka sc::sc-sftp)

add_sc_libraries(DESCRIPTION "simply-cpp device positions to third-party fleet systems: payloads, sign-in styles, HTTP, Kafka and SFTP delivery")
target_compile_features(sc-fleet-feed PUBLIC cxx_std_20)
target_compile_features(sc-fleet-feed-shared PUBLIC cxx_std_20)

install_sc_module(PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
install(FILES cmake/SimplyCppFunctions.cmake cmake/sc_bootstrap.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sc-fleet-feed
        COMPONENT development)

if (SC_FLEET_FEED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests sc-fleet-feed-test)
endif ()

package_sc_module(DEV_DEPENDS "simply-cpp-dev (>= 4.0.0~)" "simply-cpp-http-dev (>= 4.0.0~)" "simply-cpp-kafka-dev (>= 6.0.0~)" "simply-cpp-sftp-dev (>= 4.0.0~)")
