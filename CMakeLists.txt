cmake_minimum_required(VERSION 3.20)
project(toy_isa_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ищем AsmJit (установлен через pacman)
find_library(ASMJIT_LIB asmjit REQUIRED)
find_path(ASMJIT_INCLUDE asmjit/x86.h REQUIRED)

# Исполняемый файл симулятора
add_executable(sim
    src/main.cpp
    src/generate_block.cpp
    src/elf_loader.cpp
)

target_include_directories(sim PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${ASMJIT_INCLUDE}
)

target_compile_definitions(sim PRIVATE ASMJIT_STATIC)

target_link_libraries(sim PRIVATE ${ASMJIT_LIB})

# Тесты
add_executable(tests tests/tests.cpp)
target_include_directories(tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)