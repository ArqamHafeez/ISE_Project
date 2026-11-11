cmake_minimum_required(VERSION 3.16)
project(StoreBarcodeTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(store_barcode
    src/main.cpp
    src/barcode.cpp
    src/image.cpp
)
target_include_directories(store_barcode PRIVATE src)

include(CTest)
if(BUILD_TESTING)
    add_executable(barcode_tests
        tests/barcode_tests.cpp
        src/barcode.cpp
        src/image.cpp
    )
    target_include_directories(barcode_tests PRIVATE src)
    add_test(NAME barcode_tests COMMAND barcode_tests)
endif()
