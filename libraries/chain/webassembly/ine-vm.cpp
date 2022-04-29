#include <inery/chain/webassembly/ine-vm.hpp>
#include <inery/chain/apply_context.hpp>
#include <inery/chain/transaction_context.hpp>
#include <inery/chain/wasm_inery_constraints.hpp>
//ine-vm includes
#include <inery/vm/backend.hpp>

namespace inery { namespace chain { namespace webassembly { namespace ine_vm_runtime {

using namespace inery::vm;

namespace wasm_constraints = inery::chain::wasm_constraints;

namespace {

  struct checktime_watchdog {
     checktime_watchdog(transaction_checktime_timer& timer) : _timer(timer) {}
     template<typename F>
     struct guard {
        guard(transaction_checktime_timer& timer, F&& func)
           : _timer(timer), _func(static_cast<F&&>(func)) {
           _timer.set_expiration_callback(&callback, this);
           if(_timer.expired) {
              _func(); // it's harmless if _func is invoked twice
           }
        }
        ~guard() {
           _timer.set_expiration_callback(nullptr, nullptr);
        }
        static void callback(void* data) {
           guard* self = static_cast<guard*>(data);
           self->_func();
        }
        transaction_checktime_timer& _timer;
        F _func;
     };
     template<typename F>
     guard<F> scoped_run(F&& func) {
        return guard{_timer, static_cast<F&&>(func)};
     }
     transaction_checktime_timer& _timer;
  };

}

template<typename Impl>
class ine_vm_instantiated_module : public wasm_instantiated_module_interface {
      using backend_t = backend<apply_context, Impl>;
   public:
      
      ine_vm_instantiated_module(ine_vm_runtime<Impl>* runtime, std::unique_ptr<backend_t> mod) :
         _runtime(runtime),
         _instantiated_module(std::move(mod)) {}

      void apply(apply_context& context) override {
         _instantiated_module->set_wasm_allocator(&context.control.get_wasm_allocator());
         _runtime->_bkend = _instantiated_module.get();
         auto fn = [&]() {
            _runtime->_bkend->initialize(&context);
            const auto& res = _runtime->_bkend->call(
                &context, "env", "apply", context.get_receiver().to_uint64_t(),
                context.get_action().account.to_uint64_t(),
                context.get_action().name.to_uint64_t());
         };
         try {
            checktime_watchdog wd(context.trx_context.transaction_timer);
            _runtime->_bkend->timed_run(wd, fn);
         } catch(inery::vm::timeout_exception&) {
            context.trx_context.checktime();
         } catch(inery::vm::wasm_memory_exception& e) {
            FC_THROW_EXCEPTION(wasm_execution_error, "access violation");
         } catch(inery::vm::exception& e) {
            // FIXME: Do better translation
            FC_THROW_EXCEPTION(wasm_execution_error, "something went wrong...");
         }
         _runtime->_bkend = nullptr;
      }

   private:
      ine_vm_runtime<Impl>*            _runtime;
      std::unique_ptr<backend_t> _instantiated_module;
};

template<typename Impl>
ine_vm_runtime<Impl>::ine_vm_runtime() {}

template<typename Impl>
void ine_vm_runtime<Impl>::immediately_exit_currently_running_module() {
   throw wasm_exit{};
}

template<typename Impl>
bool ine_vm_runtime<Impl>::inject_module(IR::Module& module) {
   return false;
}

template<typename Impl>
std::unique_ptr<wasm_instantiated_module_interface> ine_vm_runtime<Impl>::instantiate_module(const char* code_bytes, size_t code_size, std::vector<uint8_t>,
                                                                                             const digest_type&, const uint8_t&, const uint8_t&) {
   using backend_t = backend<apply_context, Impl>;
   try {
      wasm_code_ptr code((uint8_t*)code_bytes, code_size);
      std::unique_ptr<backend_t> bkend = std::make_unique<backend_t>(code, code_size);
      registered_host_functions<apply_context>::resolve(bkend->get_module());
      return std::make_unique<ine_vm_instantiated_module<Impl>>(this, std::move(bkend));
   } catch(inery::vm::exception& e) {
      FC_THROW_EXCEPTION(wasm_execution_error, "Error building ine-vm interp: ${e}", ("e", e.what()));
   }
}

template class ine_vm_runtime<inery::vm::interpreter>;
template class ine_vm_runtime<inery::vm::jit>;

}}}}
