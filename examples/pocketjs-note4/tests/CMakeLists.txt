cmake_minimum_required(VERSION 3.22)
project(note4_display_adapter_tests C)

enable_testing()
add_executable(test_display_adapter
    test_display_adapter.c
    ../main/display_adapter.c)
target_include_directories(test_display_adapter PRIVATE ../main)
target_compile_features(test_display_adapter PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_display_adapter PRIVATE /W4 /WX)
else()
    target_compile_options(test_display_adapter PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
add_test(NAME display_adapter COMMAND test_display_adapter)
