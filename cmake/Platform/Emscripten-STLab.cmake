#
# This toolchain file extends `Emscripten.cmake` provided by the Emscripten SDK,
# and set options required to run STLab test drivers with
# CTest (using a node runner).
#

#
# Find the Emscripten SDK and include its CMake toolchain.
#
find_program( EM_CONFIG_EXECUTABLE em-config )
if ( NOT EM_CONFIG_EXECUTABLE )
    message( FATAL_ERROR "Could not find emsdk installation. Please install the Emscripten SDK.\nhttps://emscripten.org/docs/getting_started/downloads.html" )
endif()

execute_process( COMMAND ${EM_CONFIG_EXECUTABLE} EMSCRIPTEN_ROOT OUTPUT_VARIABLE EMSDK_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE )
include( ${EMSDK_ROOT}/cmake/Modules/Platform/Emscripten.cmake )

#
# Set compiler and linker flags.
#

option( STLAB_EMSCRIPTEN_PTHREADS "Build Emscripten targets with pthread support." ON )

#
# `-pthread`
# STLab's default Emscripten test suite uses threads. Targeted non-pthread
# builds can disable these flags with STLAB_EMSCRIPTEN_PTHREADS=OFF.
#
if ( STLAB_EMSCRIPTEN_PTHREADS )
    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread" )
    set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread" )
endif()

#
# `-fwasm-exceptions`:
# STLab uses exceptions. Without these, the tests error out with:
#
#   Pthread 0x005141d0 sent an error! http://localhost:6931/<throwing test>: uncaught exception: 10570976 \
#   - Exception catching is disabled, this exception cannot be caught.
#
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fwasm-exceptions" )
set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fwasm-exceptions" )

#
# `-sSUPPORT_LONGJMP=wasm`
# Enables experimental support for LONGJMP in functions which may throw exceptions.
# Without this, Boost doesn't compile (LLVM errors out). 
#
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sSUPPORT_LONGJMP=wasm" )

#
# `-sEXIT_RUNTIME=1`
# Indicates the runtime environment (node) should exit when `main()` returns.
#
set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sEXIT_RUNTIME=1" )

#
# `-sINITIAL_MEMORY=300MB` 
# Without this, the tests throw an Out Of Memory error (OOM). The first sign is an error out with the following:
#
#   Pthread 0x0058faf8 sent an error! http://localhost:6931/<test>: RuntimeError: unreachable executed
#
# If the problematic test is run in a browser with `emrun`, JavaScript errors are emitted that explain:
#
#   Aborted(Cannot enlarge memory arrays to size 17457152 bytes (OOM). Either
#   (1) compile with -sINITIAL_MEMORY=X with X higher than the current value 16777216, 
#   (2) compile with -sALLOW_MEMORY_GROWTH which allows increasing the size at runtime, or 
#   (3) if you want malloc to return  NULL (0) instead of this abort, compile with -sABORTING_MALLOC=0)
#
# Note that (2) is not an option because pthread cannot yet be combined with -sALLOW_MEMORY_GROWTH:
# See https://github.com/WebAssembly/design/issues/1271
# Smaller values (150MB, 200MB) produce intermittent failures. 300MB was chosen to give enough headroom for
# tests written in the future. 
#
if ( STLAB_EMSCRIPTEN_PTHREADS )
    set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sINITIAL_MEMORY=300MB" )
endif()

#
# `-sPTHREAD_POOL_SIZE=32` 
# Without this, the tests deadlock. Lower values were tested. 
# 8 threads deadlocked consistently, 16 threads passed consistently. 
# 32 was chosen to give enough headroom for tests written in the future. 
#
if ( STLAB_EMSCRIPTEN_PTHREADS )
    set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sPTHREAD_POOL_SIZE=32" )
endif()

#
# `-sPROXY_TO_PTHREAD`
# This flag wraps our executable's main function in a pthread. 
# Without this, we exhaust the thread pool very quickly. The error looks like this:
#
#   Tried to spawn a new thread, but the thread pool is exhausted.
#   This might result in a deadlock unless some threads eventually exit or the code explicitly breaks out to the event loop.
# 
# You can read more about the setting here: https://emscripten.org/docs/porting/pthreads.html#blocking-on-the-main-browser-thread
#
if ( STLAB_EMSCRIPTEN_PTHREADS )
    set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sPROXY_TO_PTHREAD" )
endif()

#
# Set the minimum required version for node; earlier versions lack sufficient exception support.
# Note: https://www.npmjs.com/package/wasm-check is a useful utility to find which 
# --experimental-wasm-xxx flags are supported by node.
#
set( STLAB_WASM_NODE_JS_MIN_VERSION "16.16.0" )

# set( NODE_JS_FLAGS "--experimental-wasm-threads;--experimental-wasm-eh" )

#
# Check if NODE_JS_EXECUTABLE (found by find_program() in Emscripten.cmake) is recent enough for STLab.
# Set CMAKE_CROSSCOMPILING_EMULATOR to a sufficiently recent node + required experimental flags.
#
if ( NOT NODE_JS_EXECUTABLE )
    message( FATAL_ERROR "stlab:wasm: Unable to find node. Please install ${STLAB_WASM_NODE_JS_MIN_VERSION} or newer." )
endif()
    
message( STATUS "stlab:wasm: Ensuring ${NODE_JS_EXECUTABLE} is at least ${STLAB_WASM_NODE_JS_MIN_VERSION}..." )
execute_process( COMMAND ${NODE_JS_EXECUTABLE} --version OUTPUT_VARIABLE NODE_JS_EXECUTABLE_V_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE )
STRING( REPLACE "v" "" NODE_JS_EXECUTABLE_VERSION ${NODE_JS_EXECUTABLE_V_VERSION} )

if ( NODE_JS_EXECUTABLE_VERSION VERSION_LESS ${STLAB_WASM_NODE_JS_MIN_VERSION} )
    message( FATAL_ERROR "stlab:wasm: Unsupported node: ${NODE_JS_EXECUTABLE_VERSION}. Please install ${STLAB_WASM_NODE_JS_MIN_VERSION} or newer." )
endif()

message( STATUS "stlab:wasm: Installed node satisfies requirements: ${NODE_JS_EXECUTABLE_VERSION}" )
set( CMAKE_CROSSCOMPILING_EMULATOR "${NODE_JS_EXECUTABLE};${NODE_JS_FLAGS}" )

#
# Emscripten supports dynamic linking, but doing so introduces some complexity:
# https://emscripten.org/docs/compiling/Dynamic-Linking.html
# We presently have no need to dynamically link WASM modules, so we instruct
# boost to link statically.
# 
# It would be nice if Boost respected (BUILD_SHARED_LIBS OFF), but it does not.
#
set( BUILD_SHARED_LIBS OFF )
set( Boost_USE_STATIC_LIBS ON )

#
# Print the emcc version information, if relevant.
#
execute_process( COMMAND emcc -v ERROR_VARIABLE EMCC_VERSION )
STRING( REGEX REPLACE "\n" ";" EMCC_VERSION "${EMCC_VERSION}" )
message ( STATUS "stlab: Emscripten version:" )
foreach( LINE ${EMCC_VERSION} )
  message ( STATUS "\t${LINE}" )
endforeach()
