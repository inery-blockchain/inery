#pragma once
#include <fc/variant.hpp>
#include <inery/trace_api/trace.hpp>
#include <inery/chain/abi_def.hpp>
#include <inery/chain/protocol_feature_activation.hpp>

namespace inery { namespace trace_api {

   using data_log_entry = fc::static_variant<
      block_trace_v0,
      block_trace_v1
   >;

}}
