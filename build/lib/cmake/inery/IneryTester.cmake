cmake_minimum_required( VERSION 3.5 )
message(STATUS "Setting up Inery Tester 2.0.7 at /home/dusan/Desktop/inery/build")

set(CMAKE_CXX_COMPILER /usr/bin/clang++)
set(CMAKE_C_COMPILER   /usr/bin/clang)

set(INERY_VERSION "2.0.7")

enable_testing()

if (LLVM_DIR STREQUAL "" OR NOT LLVM_DIR)
   set(LLVM_DIR /home/dusan/inery/2.0/opt/llvm/lib/cmake/llvm)
endif()

find_package( Gperftools QUIET )
if( GPERFTOOLS_FOUND )
    message( STATUS "Found gperftools; compiling tests with TCMalloc")
    list( APPEND PLATFORM_SPECIFIC_LIBS tcmalloc )
endif()

if(NOT "1" STREQUAL "")
   find_package(LLVM 7.0.0 EXACT REQUIRED CONFIG)
   llvm_map_components_to_libnames(LLVM_LIBS support core passes mcjit native DebugInfoDWARF orcjit)
   link_directories(${LLVM_LIBRARY_DIR})
endif()

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_EXTENSIONS ON )
set( CXX_STANDARD_REQUIRED ON )

if ( APPLE )
   set( CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_CXX_FLAGS} -Wall -Wno-deprecated-declarations" )
else ( APPLE )
   set( CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_CXX_FLAGS} -Wall")
endif ( APPLE )

### Remove after Boost 1.70 CMake fixes are in place
set( Boost_NO_BOOST_CMAKE ON CACHE STRING "ON or OFF" )
set( Boost_USE_STATIC_LIBS ON CACHE STRING "ON or OFF" )
find_package(Boost 1.67 REQUIRED COMPONENTS
   date_time
   filesystem
   system
   chrono
   iostreams
   unit_test_framework)

find_library(libtester inery_testing /home/dusan/Desktop/inery/build/libraries/testing NO_DEFAULT_PATH)
find_library(libchain inery_chain /home/dusan/Desktop/inery/build/libraries/chain NO_DEFAULT_PATH)
if ( "${CMAKE_BUILD_TYPE}" STREQUAL "Debug" )
   find_library(libfc fc_debug /home/dusan/Desktop/inery/build/libraries/fc NO_DEFAULT_PATH)
   find_library(libsecp256k1 secp256k1_debug /home/dusan/Desktop/inery/build/libraries/fc/secp256k1 NO_DEFAULT_PATH)

else()
   find_library(libfc fc /home/dusan/Desktop/inery/build/libraries/fc NO_DEFAULT_PATH)
   find_library(libsecp256k1 secp256k1 /home/dusan/Desktop/inery/build/libraries/fc/secp256k1 NO_DEFAULT_PATH)
endif()

find_library(libwasm WASM /home/dusan/Desktop/inery/build/libraries/wasm-jit/Source/WASM NO_DEFAULT_PATH)
find_library(libwast WAST /home/dusan/Desktop/inery/build/libraries/wasm-jit/Source/WAST NO_DEFAULT_PATH)
find_library(libir IR     /home/dusan/Desktop/inery/build/libraries/wasm-jit/Source/IR NO_DEFAULT_PATH)
find_library(libwabt wabt /home/dusan/Desktop/inery/build/libraries/wabt NO_DEFAULT_PATH)
find_library(libplatform Platform /home/dusan/Desktop/inery/build/libraries/wasm-jit/Source/Platform NO_DEFAULT_PATH)
find_library(liblogging Logging /home/dusan/Desktop/inery/build/libraries/wasm-jit/Source/Logging NO_DEFAULT_PATH)
find_library(libruntime Runtime /home/dusan/Desktop/inery/build/libraries/wasm-jit/Source/Runtime NO_DEFAULT_PATH)
find_library(libsoftfloat softfloat /home/dusan/Desktop/inery/build/libraries/softfloat NO_DEFAULT_PATH)
get_filename_component(cryptodir /usr/lib/x86_64-linux-gnu/libcrypto.so DIRECTORY)
find_library(liboscrypto crypto "${cryptodir}" NO_DEFAULT_PATH)
get_filename_component(ssldir /usr/lib/x86_64-linux-gnu/libssl.so DIRECTORY)
find_library(libosssl ssl "${ssldir}" NO_DEFAULT_PATH)
find_library(libchainbase chainbase /home/dusan/Desktop/inery/build/libraries/chainbase NO_DEFAULT_PATH)
find_library(libbuiltins builtins /home/dusan/Desktop/inery/build/libraries/builtins NO_DEFAULT_PATH)
find_library(GMP_LIBRARIES NAMES libgmp.a gmp.lib gmp libgmp-10 mpir
    HINTS ENV GMP_LIB_DIR
          ENV GMP_DIR
    PATH_SUFFIXES lib
    DOC "Path to the GMP library"
)

set(INERY_WASM_RUNTIMES wabt;ine-vm-oc;ine-vm;ine-vm-jit)
if("ine-vm-oc" IN_LIST INERY_WASM_RUNTIMES)
   set(WRAP_MAIN "-Wl,-wrap=main")
endif()

macro(add_inery_test_executable test_name)
   add_executable( ${test_name} ${ARGN} )
   target_link_libraries( ${test_name}
       ${libtester}
       ${libchain}
       ${libfc}
       ${libwast}
       ${libwasm}
       ${libwabt}
       ${libruntime}
       ${libplatform}
       ${libir}
       ${libsoftfloat}
       ${liboscrypto}
       ${libosssl}
       ${liblogging}
       ${libchainbase}
       ${libbuiltins}
       ${libsecp256k1}
       ${GMP_LIBRARIES}

       ${Boost_FILESYSTEM_LIBRARY}
       ${Boost_SYSTEM_LIBRARY}
       ${Boost_CHRONO_LIBRARY}
       ${Boost_IOSTREAMS_LIBRARY}
       "-lz" # Needed by Boost iostreams
       ${Boost_DATE_TIME_LIBRARY}

       ${LLVM_LIBS}

       ${PLATFORM_SPECIFIC_LIBS}

       ${WRAP_MAIN}
      )

   target_include_directories( ${test_name} PUBLIC
                               ${Boost_INCLUDE_DIRS}
                               /usr/include
                               /home/dusan/Desktop/inery/libraries/chain/include
                               /home/dusan/Desktop/inery/build/libraries/chain/include
                               /home/dusan/Desktop/inery/libraries/fc/include
                               /home/dusan/Desktop/inery/libraries/softfloat/source/include
                               /home/dusan/Desktop/inery/libraries/appbase/include
                               /home/dusan/Desktop/inery/libraries/chainbase/include
                               /home/dusan/Desktop/inery/libraries/testing/include
                               /home/dusan/Desktop/inery/libraries/wasm-jit/Include )
endmacro()

macro(add_inery_test test_name)
   add_inery_test_executable( ${test_name} ${ARGN} )
   #This will generate a test with the default runtime
   add_test(NAME ${test_name} COMMAND ${test_name} --report_level=detailed --color_output)
   #Manually run unit_test for all supported runtimes
   #To run unit_test with all log from blockchain displayed, put --verbose after --, i.e. unit_test -- --verbose
endmacro()

if(ENABLE_COVERAGE_TESTING)

  set(Coverage_NAME ${PROJECT_NAME}_ut_coverage)

  if(NOT LCOV_PATH)
    message(FATAL_ERROR "lcov not found! Aborting...")
  endif() # NOT LCOV_PATH

  if(NOT LLVMCOV_PATH)
    message(FATAL_ERROR "llvm-cov not found! Aborting...")
  endif() # NOT LCOV_PATH

  if(NOT GENHTML_PATH)
    message(FATAL_ERROR "genhtml not found! Aborting...")
  endif() # NOT GENHTML_PATH

  # no spaces allowed within tests list
  set(ctest_tests 'unit_test_binaryen|unit_test_wavm')
  set(ctest_exclude_tests '')

  # Setup target
  add_custom_target(${Coverage_NAME}

    # Cleanup lcov
    COMMAND ${LCOV_PATH} --directory . --zerocounters

    # Run tests
    COMMAND ./tools/ctestwrapper.sh -R ${ctest_tests} -E ${ctest_exclude_tests}

    COMMAND ${LCOV_PATH} --directory . --capture --gcov-tool ${CMAKE_SOURCE_DIR}/tools/llvm-gcov.sh --output-file ${Coverage_NAME}.info

    COMMAND ${LCOV_PATH} -remove ${Coverage_NAME}.info '*/boost/*' '/usr/lib/*' '/usr/include/*' '*/externals/*' '*/fc/*' '*/wasm-jit/*' --output-file ${Coverage_NAME}_filtered.info

    COMMAND ${GENHTML_PATH} -o ${Coverage_NAME} ${PROJECT_BINARY_DIR}/${Coverage_NAME}_filtered.info

    COMMAND if [ "$CI" != "true" ]\; then ${CMAKE_COMMAND} -E remove ${Coverage_NAME}.base ${Coverage_NAME}.info ${Coverage_NAME}_filtered.info ${Coverage_NAME}.total ${PROJECT_BINARY_DIR}/${Coverage_NAME}.info.cleaned ${PROJECT_BINARY_DIR}/${Coverage_NAME}_filtered.info.cleaned\; fi

    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    COMMENT "Resetting code coverage counters to zero. Processing code coverage counters and generating report. Report published in ./${Coverage_NAME}"
    )

  # Show info where to find the report
  add_custom_command(TARGET ${Coverage_NAME} POST_BUILD
    COMMAND ;
    COMMENT "Open ./${Coverage_NAME}/index.html in your browser to view the coverage report."
    )
endif()
