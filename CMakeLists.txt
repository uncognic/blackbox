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
    src/utils/data.cpp
    src/utils/string_utils.cpp
    src/utils/asm_parser.cpp
    src/utils/preprocessor.cpp
    src/utils/macro_expansion.cpp
    src/utils/symbol_table.cpp
    src/utils/random_utils.cpp
)
target_include_directories(bbx_utils PUBLIC src)

# compiler
add_executable(bbxc
    src/blackboxc/compiler.cpp
    src/blackboxc/asm.cpp
    src/blackboxc/asm_util.cpp
    src/blackboxc/basic.cpp
)
target_include_directories(bbxc PRIVATE src/blackboxc)
target_link_libraries(bbxc PRIVATE bbx_utils)

# interpreter
#add_executable(bbx
#    src/blackbox/blackbox.cpp
#    src/blackbox/debug.cpp
#    src/utils/fmt.cpp
#)
#target_include_directories(bbx PRIVATE src/blackbox src/blackboxc)
#target_link_libraries(bbx PRIVATE bbx_utils)

# disassembler
add_executable(bbxd
    src/blackboxd/blackboxd.cpp
)
target_include_directories(bbxd PRIVATE src)
target_link_libraries(bbxd PRIVATE bbx_utils)

# win32 stuff
if(WIN32)
    target_link_libraries(bbxc PRIVATE bcrypt)
    #target_link_libraries(bbx PRIVATE bcrypt)
endif()