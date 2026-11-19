cmake_minimum_required(VERSION 3.16)
project(StoreBarcodeTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pos_core
    src/barcode.cpp
    src/image.cpp
    src/models.cpp
    src/store_repository.cpp
    src/pos_service.cpp
)
target_include_directories(pos_core PUBLIC src)
if(MSVC)
    target_compile_options(pos_core PRIVATE /W4)
else()
    target_compile_options(pos_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(store_barcode src/main.cpp)
target_link_libraries(store_barcode PRIVATE pos_core)

option(BUILD_POS_GUI "Build the Qt and OpenCV point-of-sale application" ON)
if(BUILD_POS_GUI)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

    add_executable(store_pos
        src/gui_main.cpp
        src/main_window.cpp
        src/main_window.h
        src/camera_frame_decoder.cpp
        src/camera_frame_decoder.h
        src/camera_scanner.cpp
        src/camera_scanner.h
    )
    target_include_directories(store_pos PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(store_pos PRIVATE pos_core Qt6::Widgets ${OpenCV_LIBS})
    if(MSVC)
        target_compile_options(store_pos PRIVATE /W4)
    else()
        target_compile_options(store_pos PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    set_target_properties(store_pos PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(barcode_tests tests/barcode_tests.cpp)
    target_link_libraries(barcode_tests PRIVATE pos_core)
    add_test(NAME barcode_tests COMMAND barcode_tests)

    add_executable(pos_tests tests/pos_tests.cpp)
    target_link_libraries(pos_tests PRIVATE pos_core)
    add_test(NAME pos_tests COMMAND pos_tests)

    if(BUILD_POS_GUI)
        add_executable(camera_frame_decoder_tests
            tests/camera_frame_decoder_tests.cpp
            src/camera_frame_decoder.cpp
        )
        target_include_directories(camera_frame_decoder_tests PRIVATE
            src ${OpenCV_INCLUDE_DIRS}
        )
        target_link_libraries(camera_frame_decoder_tests PRIVATE
            pos_core ${OpenCV_LIBS}
        )
        if(MSVC)
            target_compile_options(camera_frame_decoder_tests PRIVATE /W4)
        else()
            target_compile_options(camera_frame_decoder_tests PRIVATE
                -Wall -Wextra -Wpedantic
            )
        endif()
        add_test(NAME camera_frame_decoder_tests COMMAND camera_frame_decoder_tests)
    endif()
endif()
