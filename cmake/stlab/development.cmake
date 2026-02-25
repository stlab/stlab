add_library(development INTERFACE)
add_library(stlab::development ALIAS development)

include(stlab/development/AppleClang)
include(stlab/development/Clang)
include(stlab/development/GNU)
include(stlab/development/MSVC)

if(STLAB_SANITIZER STREQUAL "address")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # Compiler only; linker infers ASan libs automatically
        add_compile_options(/fsanitize=address)
        # Disable incremental linking to avoid LNK4300 with ASan metadata
        add_link_options(/INCREMENTAL:NO)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()
