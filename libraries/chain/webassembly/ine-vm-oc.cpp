#include <inery/chain/webassembly/ine-vm-oc.hpp>
#include <inery/chain/wasm_inery_constraints.hpp>
#include <inery/chain/wasm_inery_injection.hpp>
#include <inery/chain/apply_context.hpp>
#include <inery/chain/exceptions.hpp>

#include <vector>
#include <iterator>

namespace inery { namespace chain { namespace webassembly { namespace inevmoc {

class inevmoc_instantiated_module : public wasm_instantiated_module_interface {
   public:
      inevmoc_instantiated_module(const digest_type& code_hash, const uint8_t& vm_version, inevmoc_runtime& wr) :
         _code_hash(code_hash),
         _vm_version(vm_version),
         _inevmoc_runtime(wr)
      {

      }

      ~inevmoc_instantiated_module() {
         _inevmoc_runtime.cc.free_code(_code_hash, _vm_version);
      }

      void apply(apply_context& context) override {
         const code_descriptor* const cd = _inevmoc_runtime.cc.get_descriptor_for_code_sync(_code_hash, _vm_version);
         INE_ASSERT(cd, wasm_execution_error, "INE VM OC instantiation failed");

         _inevmoc_runtime.exec.execute(*cd, _inevmoc_runtime.mem, context);
      }

      const digest_type              _code_hash;
      const uint8_t                  _vm_version;
      inevmoc_runtime&               _inevmoc_runtime;
};

inevmoc_runtime::inevmoc_runtime(const boost::filesystem::path data_dir, const inevmoc::config& inevmoc_config, const chainbase::database& db)
   : cc(data_dir, inevmoc_config, db), exec(cc) {
}

inevmoc_runtime::~inevmoc_runtime() {
}

std::unique_ptr<wasm_instantiated_module_interface> inevmoc_runtime::instantiate_module(const char* code_bytes, size_t code_size, std::vector<uint8_t> initial_memory,
                                                                                     const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) {

   return std::make_unique<inevmoc_instantiated_module>(code_hash, vm_type, *this);
}

//never called. INE VM OC overrides inery_exit to its own implementation
void inevmoc_runtime::immediately_exit_currently_running_module() {}

}}}}
