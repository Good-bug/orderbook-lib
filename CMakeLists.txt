cmake_minimum_required(VERSION 3.25)
project(orderbook_lib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14.0)
    message(WARNING "GCC 14 or newer is recommended for full C++23 support (std::expected, std::println).")
endif()

find_package(OpenSSL REQUIRED)
find_package(quill CONFIG REQUIRED)
find_package(glaze CONFIG QUIET)
find_package(boost_asio CONFIG QUIET)
find_package(boost_beast CONFIG QUIET)

add_library(orderbook_lib
    src/gateway/binance_listener_service.cpp
    src/orderbook/order_book.cpp
)
add_library(orderbook::lib ALIAS orderbook_lib)

target_include_directories(orderbook_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(orderbook_lib
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
        quill::quill
)

if(TARGET glaze::glaze)
    target_link_libraries(orderbook_lib PUBLIC glaze::glaze)
endif()
if(TARGET Boost::beast)
    target_link_libraries(orderbook_lib PUBLIC Boost::beast)
endif()
if(TARGET Boost::asio)
    target_link_libraries(orderbook_lib PUBLIC Boost::asio)
endif()

target_compile_options(orderbook_lib PRIVATE -Wall -Wextra -Wpedantic)

# Unit testing
include(CTest)
if(BUILD_TESTING)
    find_package(Catch2 CONFIG QUIET)
    if(TARGET Catch2::Catch2WithMain)
        add_executable(orderbook_tests
            tests/binance_convert_test.cpp
            tests/naive_book_test.cpp
            tests/book_synchronizer_test.cpp
        )
        target_link_libraries(orderbook_tests PRIVATE orderbook_lib Catch2::Catch2WithMain)
        target_compile_options(orderbook_tests PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME orderbook_tests COMMAND orderbook_tests)
    endif()
endif()
