cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(GPP_CXX g++-14)
    if(GPP_CXX)
        set(CMAKE_CXX_COMPILER "${GPP_CXX}")
    else()
        find_program(GPP_CXX g++)
        if(GPP_CXX)
            set(CMAKE_CXX_COMPILER "${GPP_CXX}")
        endif()
    endif()
endif()

if(NOT DEFINED CMAKE_C_COMPILER)
    find_program(GCC_C gcc-14)
    if(GCC_C)
        set(CMAKE_C_COMPILER "${GCC_C}")
    else()
        find_program(GCC_C gcc)
        if(GCC_C)
            set(CMAKE_C_COMPILER "${GCC_C}")
        endif()
    endif()
endif()

project(Blackbox CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# utils
add_library(bbx_utils STATIC
        src/utils/string_utils.cpp
        src/utils/preprocessor.cpp
        src/utils/macro_expansion.cpp
        src/utils/random_utils.cpp
        src/utils/fmt.cpp
        src/utils/data.cpp
)
target_include_directories(bbx_utils PUBLIC src)

# compiler
add_executable(bbxc
        src/blackboxc/compiler.cpp
        src/blackboxc/assembler.cpp
        src/blackboxc/encoder.cpp
        src/blackboxc/asm_util.cpp
        src/blackboxc/basic/basic.cpp
        src/blackboxc/basic/parser.cpp
        src/blackboxc/basic/expressions.cpp
        src/blackboxc/basic/statements.cpp
        src/blackboxc/basic/scope.cpp
        src/blackboxc/basic/bbx_codegen.cpp
)
target_include_directories(bbxc PRIVATE src src/blackboxc src/blackboxc/basic)
target_link_libraries(bbxc PRIVATE bbx_utils)

# vm
add_executable(bbx
        src/blackbox/main.cpp
        src/blackbox/vm.cpp
        src/blackbox/program.cpp
        src/blackbox/debugger.cpp
        src/blackbox/ops/ops_arithmetic.cpp
        src/blackbox/ops/ops_bitwise.cpp
        src/blackbox/ops/ops_control.cpp
        src/blackbox/ops/ops_memory.cpp
        src/blackbox/ops/ops_registers.cpp
        src/blackbox/ops/ops_io.cpp
        src/blackbox/ops/ops_system.cpp
        src/blackbox/ops/ops_priv.cpp
        src/blackbox/ops/ops_debug.cpp
)
target_include_directories(bbx PRIVATE src src/blackbox)
target_link_libraries(bbx PRIVATE bbx_utils)

 #windows
if(WIN32)
    target_link_libraries(bbxc PRIVATE bcrypt)
    target_link_libraries(bbx  PRIVATE bcrypt)
endif()