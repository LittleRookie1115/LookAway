cmake_minimum_required(VERSION 3.20)

project(LookAway VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lookaway_core
    src/work_timer.cpp
)
target_include_directories(lookaway_core PUBLIC src)

if(WIN32)
    enable_language(RC)
    add_executable(LookAway WIN32
        src/main.cpp
        resources/app.rc
    )
    set_property(SOURCE resources/app.rc PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico"
    )
    target_include_directories(LookAway PRIVATE resources)
    target_compile_definitions(LookAway PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
    target_link_libraries(LookAway PRIVATE lookaway_core comctl32 gdi32 shell32 user32)

    if(MSVC)
        target_compile_options(LookAway PRIVATE /utf-8 /W4)
    else()
        target_compile_options(LookAway PRIVATE -Wall -Wextra -Wpedantic -finput-charset=UTF-8)
    endif()

    if(MINGW)
        target_link_options(LookAway PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()

    install(TARGETS LookAway RUNTIME DESTINATION .)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(lookaway_tests tests/work_timer_tests.cpp)
    target_link_libraries(lookaway_tests PRIVATE lookaway_core)
    add_test(NAME work_timer COMMAND lookaway_tests)
endif()
