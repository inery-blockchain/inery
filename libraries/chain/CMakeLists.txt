configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/inery/chain/core_symbol.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/include/inery/chain/core_symbol.hpp)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/genesis_state_root_key.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/genesis_state_root_key.cpp)

file(GLOB HEADERS "include/inery/chain/*.hpp"
                  "include/inery/chain/webassembly/*.hpp"
                  "${CMAKE_CURRENT_BINARY_DIR}/include/inery/chain/core_symbol.hpp" )

if(APPLE AND UNIX)
   set(PLATFORM_TIMER_IMPL platform_timer_macos.cpp)
else()
   try_run(POSIX_TIMER_TEST_RUN_RESULT POSIX_TIMER_TEST_COMPILE_RESULT ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform_timer_posix_test.c)
   if(POSIX_TIMER_TEST_RUN_RESULT EQUAL 0)
      set(PLATFORM_TIMER_IMPL platform_timer_posix.cpp)
      set(CHAIN_RT_LINKAGE rt)
   else()
      set(PLATFORM_TIMER_IMPL platform_timer_asio_fallback.cpp)
   endif()
endif()

if("ine-vm-oc" IN_LIST INERY_WASM_RUNTIMES)
   set(CHAIN_INEVMOC_SOURCES webassembly/ine-vm-oc/code_cache.cpp
                             webassembly/ine-vm-oc/executor.cpp
                             webassembly/ine-vm-oc/memory.cpp
                             webassembly/ine-vm-oc/intrinsic.cpp
                             webassembly/ine-vm-oc/LLVMJIT.cpp
                             webassembly/ine-vm-oc/LLVMEmitIR.cpp
                             webassembly/ine-vm-oc/compile_monitor.cpp
                             webassembly/ine-vm-oc/compile_trampoline.cpp
                             webassembly/ine-vm-oc/ipc_helpers.cpp
                             webassembly/ine-vm-oc/gs_seg_helpers.c
                             webassembly/ine-vm-oc.cpp)

   if(LLVM_VERSION VERSION_LESS 7.1 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      enable_language(ASM-LLVMWAR)
      list(APPEND CHAIN_INEVMOC_SOURCES webassembly/ine-vm-oc/llvmWARshim.llvmwar)
   else()
      list(APPEND CHAIN_INEVMOC_SOURCES webassembly/ine-vm-oc/llvmWARshim.cpp)
   endif()

   llvm_map_components_to_libnames(LLVM_LIBS support core passes mcjit native orcjit)
   include_directories(${LLVM_INCLUDE_DIRS})
   add_definitions(${LLVM_DEFINITIONS})

   option(INEVMOC_ENABLE_DEVELOPER_OPTIONS "enable developer options for INE VM OC" OFF)
endif()

if("ine-vm" IN_LIST INERY_WASM_RUNTIMES OR "ine-vm-jit" IN_LIST INERY_WASM_RUNTIMES)
   set(CHAIN_INEVM_SOURCES "webassembly/ine-vm.cpp")
   set(CHAIN_INEVM_LIBRARIES ine-vm)
endif()

## SORT .cpp by most likely to change / break compile
add_library( inery_chain
             merkle.cpp
             name.cpp
             transaction.cpp
             block.cpp
             block_header.cpp
             block_header_state.cpp
             block_state.cpp
             fork_database.cpp
             controller.cpp
             authorization_manager.cpp
             resource_limits.cpp
             block_log.cpp
             transaction_context.cpp
             inery_contract.cpp
             inery_contract_abi.cpp
             inery_contract_abi_bin.cpp
             chain_config.cpp
             chain_id_type.cpp
             genesis_state.cpp
             ${CMAKE_CURRENT_BINARY_DIR}/genesis_state_root_key.cpp

#             chain_config.cpp
#             block_trace.cpp
              wast_to_wasm.cpp
              wasm_interface.cpp
              wasm_inery_validation.cpp
              wasm_inery_injection.cpp
              apply_context.cpp
              abi_serializer.cpp
              asset.cpp
              snapshot.cpp

             webassembly/wabt.cpp
             ${CHAIN_INEVMOC_SOURCES}
             ${CHAIN_INEVM_SOURCES}

#             get_config.cpp
#
#             contracts/chain_initializer.cpp

             trace.cpp
             transaction_metadata.cpp
             protocol_state_object.cpp
             protocol_feature_activation.cpp
             protocol_feature_manager.cpp
             producer_schedule.cpp
             genesis_intrinsics.cpp
             whitelisted_intrinsics.cpp
             thread_utils.cpp
             platform_timer_accuracy.cpp
             ${PLATFORM_TIMER_IMPL}
             ${HEADERS}
             )

target_link_libraries( inery_chain fc chainbase Logging IR WAST WASM Runtime
                       softfloat builtins wabt ${CHAIN_INEVM_LIBRARIES} ${LLVM_LIBS} ${CHAIN_RT_LINKAGE}
                     )
target_include_directories( inery_chain
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include"
                                   "${CMAKE_CURRENT_SOURCE_DIR}/../wasm-jit/Include"
                                   "${CMAKE_CURRENT_SOURCE_DIR}/libraries/ine-vm/include"
                                   "${CMAKE_SOURCE_DIR}/libraries/wabt"
                                   "${CMAKE_BINARY_DIR}/libraries/wabt"
                            )

if("ine-vm-oc" IN_LIST INERY_WASM_RUNTIMES)
   target_link_libraries(inery_chain "-Wl,-wrap=main")
endif()

foreach(RUNTIME ${INERY_WASM_RUNTIMES})
   string(TOUPPER "${RUNTIME}" RUNTIMEUC)
   string(REPLACE "-" "_" RUNTIMEUC ${RUNTIMEUC})
   target_compile_definitions(inery_chain PUBLIC "INERY_${RUNTIMEUC}_RUNTIME_ENABLED")
endforeach()

if(INEVMOC_ENABLE_DEVELOPER_OPTIONS)
   message(WARNING "INE VM OC Developer Options are enabled; these are NOT supported")
   target_compile_definitions(inery_chain PUBLIC INERY_INE_VM_OC_DEVELOPER)
endif()

install( TARGETS inery_chain
   RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
)
install( DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/inery/chain/
      DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/inery/chain
      FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" EXCLUDE
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/inery/chain/core_symbol.hpp DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/inery/chain)
#if(MSVC)
#  set_source_files_properties( db_init.cpp db_block.cpp database.cpp block_log.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
#endif(MSVC)
