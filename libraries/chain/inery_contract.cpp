#include <inery/chain/inery_contract.hpp>
#include <inery/chain/contract_table_objects.hpp>

#include <inery/chain/controller.hpp>
#include <inery/chain/transaction_context.hpp>
#include <inery/chain/apply_context.hpp>
#include <inery/chain/transaction.hpp>
#include <inery/chain/exceptions.hpp>

#include <inery/chain/account_object.hpp>
#include <inery/chain/code_object.hpp>
#include <inery/chain/permission_object.hpp>
#include <inery/chain/permission_link_object.hpp>
#include <inery/chain/global_property_object.hpp>
#include <inery/chain/contract_types.hpp>

#include <inery/chain/wasm_interface.hpp>
#include <inery/chain/abi_serializer.hpp>

#include <inery/chain/authorization_manager.hpp>
#include <inery/chain/resource_limits.hpp>

namespace inery { namespace chain {



uint128_t transaction_id_to_sender_id( const transaction_id_type& tid ) {
   fc::uint128_t _id(tid._hash[3], tid._hash[2]);
   return (unsigned __int128)_id;
}

void validate_authority_precondition( const apply_context& context, const authority& auth ) {
   for(const auto& a : auth.accounts) {
      auto* acct = context.db.find<account_object, by_name>(a.permission.actor);
      INE_ASSERT( acct != nullptr, action_validate_exception,
                  "account '${account}' does not exist",
                  ("account", a.permission.actor)
                );

      if( a.permission.permission == config::owner_name || a.permission.permission == config::active_name )
         continue; // account was already checked to exist, so its owner and active permissions should exist

      if( a.permission.permission == config::inery_code_name ) // virtual inery.code permission does not really exist but is allowed
         continue;

      try {
         context.control.get_authorization_manager().get_permission({a.permission.actor, a.permission.permission});
      } catch( const permission_query_exception& ) {
         INE_THROW( action_validate_exception,
                    "permission '${perm}' does not exist",
                    ("perm", a.permission)
                  );
      }
   }

   if( context.trx_context.enforce_whiteblacklist && context.control.is_producing_block() ) {
      for( const auto& p : auth.keys ) {
         context.control.check_key_list( p.key );
      }
   }
}

/**
 *  This method is called assuming precondition_system_newaccount succeeds a
 */
void apply_inery_newaccount(apply_context& context) {
   auto create = context.get_action().data_as<newaccount>();
   try {
   context.require_authorization(create.creator);
//   context.require_write_lock( config::inery_auth_scope );
   auto& authorization = context.control.get_mutable_authorization_manager();

   INE_ASSERT( validate(create.owner), action_validate_exception, "Invalid owner authority");
   INE_ASSERT( validate(create.active), action_validate_exception, "Invalid active authority");

   auto& db = context.db;

   auto name_str = name(create.name).to_string();

   INE_ASSERT( !create.name.empty(), action_validate_exception, "account name cannot be empty" );
   INE_ASSERT( name_str.size() <= 12, action_validate_exception, "account names can only be 12 chars long" );

   // Check if the creator is privileged
   const auto &creator = db.get<account_metadata_object, by_name>(create.creator);
   if( !creator.is_privileged() ) {
      INE_ASSERT( name_str.find( "inery." ) != 0, action_validate_exception,
                  "only privileged accounts can have names that start with 'inery.'" );
   }

   auto existing_account = db.find<account_object, by_name>(create.name);
   INE_ASSERT(existing_account == nullptr, account_name_exists_exception,
              "Cannot create account named ${name}, as that name is already taken",
              ("name", create.name));

   const auto& new_account = db.create<account_object>([&](auto& a) {
      a.name = create.name;
      a.creation_date = context.control.pending_block_time();
   });

   db.create<account_metadata_object>([&](auto& a) {
      a.name = create.name;
   });

   for( const auto& auth : { create.owner, create.active } ){
      validate_authority_precondition( context, auth );
   }

   const auto& owner_permission  = authorization.create_permission( create.name, config::owner_name, 0,
                                                                    std::move(create.owner) );
   const auto& active_permission = authorization.create_permission( create.name, config::active_name, owner_permission.id,
                                                                    std::move(create.active) );

   context.control.get_mutable_resource_limits_manager().initialize_account(create.name);

   int64_t mem_delta = config::overhead_per_account_mem_bytes;
   mem_delta += 2*config::billable_size_v<permission_object>;
   mem_delta += owner_permission.auth.get_billable_size();
   mem_delta += active_permission.auth.get_billable_size();

   context.add_mem_usage(create.name, mem_delta);

} FC_CAPTURE_AND_RETHROW( (create) ) }

void apply_inery_setcode(apply_context& context) {
   const auto& cfg = context.control.get_global_properties().configuration;

   auto& db = context.db;
   auto  act = context.get_action().data_as<setcode>();
   context.require_authorization(act.account);

   INE_ASSERT( act.vmtype == 0, invalid_contract_vm_type, "code should be 0" );
   INE_ASSERT( act.vmversion == 0, invalid_contract_vm_version, "version should be 0" );

   fc::sha256 code_hash; /// default is the all zeros hash

   int64_t code_size = (int64_t)act.code.size();

   if( code_size > 0 ) {
     code_hash = fc::sha256::hash( act.code.data(), (uint32_t)act.code.size() );
     wasm_interface::validate(context.control, act.code);
   }

   const auto& account = db.get<account_metadata_object,by_name>(act.account);
   bool existing_code = (account.code_hash != digest_type());

   INE_ASSERT( code_size > 0 || existing_code, set_exact_code, "contract is already cleared" );

   int64_t old_size  = 0;
   int64_t new_size  = code_size * config::setcode_mem_bytes_multiplier;

   if( existing_code ) {
      const code_object& old_code_entry = db.get<code_object, by_code_hash>(boost::make_tuple(account.code_hash, account.vm_type, account.vm_version));
      INE_ASSERT( old_code_entry.code_hash != code_hash, set_exact_code,
                  "contract is already running this version of code" );
      old_size  = (int64_t)old_code_entry.code.size() * config::setcode_mem_bytes_multiplier;
      if( old_code_entry.code_ref_count == 1 ) {
         db.remove(old_code_entry);
         context.control.get_wasm_interface().code_block_num_last_used(account.code_hash, account.vm_type, account.vm_version, context.control.head_block_num() + 1);
      } else {
         db.modify(old_code_entry, [](code_object& o) {
            --o.code_ref_count;
         });
      }
   }

   if( code_size > 0 ) {
      const code_object* new_code_entry = db.find<code_object, by_code_hash>(
                                             boost::make_tuple(code_hash, act.vmtype, act.vmversion) );
      if( new_code_entry ) {
         db.modify(*new_code_entry, [&](code_object& o) {
            ++o.code_ref_count;
         });
      } else {
         db.create<code_object>([&](code_object& o) {
            o.code_hash = code_hash;
            o.code.assign(act.code.data(), code_size);
            o.code_ref_count = 1;
            o.first_block_used = context.control.head_block_num() + 1;
            o.vm_type = act.vmtype;
            o.vm_version = act.vmversion;
         });
      }
   }

   db.modify( account, [&]( auto& a ) {
      a.code_sequence += 1;
      a.code_hash = code_hash;
      a.vm_type = act.vmtype;
      a.vm_version = act.vmversion;
      a.last_code_update = context.control.pending_block_time();
   });

   if (new_size != old_size) {
      context.add_mem_usage( act.account, new_size - old_size );
   }
}

void apply_inery_setabi(apply_context& context) {
   auto& db  = context.db;
   auto  act = context.get_action().data_as<setabi>();

   context.require_authorization(act.account);

   const auto& account = db.get<account_object,by_name>(act.account);

   int64_t abi_size = act.abi.size();

   int64_t old_size = (int64_t)account.abi.size();
   int64_t new_size = abi_size;

   db.modify( account, [&]( auto& a ) {
      if (abi_size > 0) {
         a.abi.assign(act.abi.data(), abi_size);
      } else {
         a.abi.resize(0);
      }
   });

   const auto& account_metadata = db.get<account_metadata_object, by_name>(act.account);
   db.modify( account_metadata, [&]( auto& a ) {
      a.abi_sequence += 1;
   });

   if (new_size != old_size) {
      context.add_mem_usage( act.account, new_size - old_size );
   }
}

void apply_inery_updateauth(apply_context& context) {

   auto update = context.get_action().data_as<updateauth>();
   context.require_authorization(update.account); // only here to mark the single authority on this action as used

   auto& authorization = context.control.get_mutable_authorization_manager();
   auto& db = context.db;

   INE_ASSERT(!update.permission.empty(), action_validate_exception, "Cannot create authority with empty name");
   INE_ASSERT( update.permission.to_string().find( "inery." ) != 0, action_validate_exception,
               "Permission names that start with 'inery.' are reserved" );
   INE_ASSERT(update.permission != update.parent, action_validate_exception, "Cannot set an authority as its own parent");
   db.get<account_object, by_name>(update.account);
   INE_ASSERT(validate(update.auth), action_validate_exception,
              "Invalid authority: ${auth}", ("auth", update.auth));
   if( update.permission == config::active_name )
      INE_ASSERT(update.parent == config::owner_name, action_validate_exception, "Cannot change active authority's parent from owner", ("update.parent", update.parent) );
   if (update.permission == config::owner_name)
      INE_ASSERT(update.parent.empty(), action_validate_exception, "Cannot change owner authority's parent");
   else
      INE_ASSERT(!update.parent.empty(), action_validate_exception, "Only owner permission can have empty parent" );

   if( update.auth.waits.size() > 0 ) {
      auto max_delay = context.control.get_global_properties().configuration.max_transaction_delay;
      INE_ASSERT( update.auth.waits.back().wait_sec <= max_delay, action_validate_exception,
                  "Cannot set delay longer than max_transacton_delay, which is ${max_delay} seconds",
                  ("max_delay", max_delay) );
   }

   validate_authority_precondition(context, update.auth);



   auto permission = authorization.find_permission({update.account, update.permission});

   // If a parent_id of 0 is going to be used to indicate the absence of a parent, then we need to make sure that the chain
   // initializes permission_index with a dummy object that reserves the id of 0.
   authorization_manager::permission_id_type parent_id = 0;
   if( update.permission != config::owner_name ) {
      auto& parent = authorization.get_permission({update.account, update.parent});
      parent_id = parent.id;
   }

   if( permission ) {
      INE_ASSERT(parent_id == permission->parent, action_validate_exception,
                 "Changing parent authority is not currently supported");


      int64_t old_size = (int64_t)(config::billable_size_v<permission_object> + permission->auth.get_billable_size());

      authorization.modify_permission( *permission, update.auth );

      int64_t new_size = (int64_t)(config::billable_size_v<permission_object> + permission->auth.get_billable_size());

      context.add_mem_usage( permission->owner, new_size - old_size );
   } else {
      const auto& p = authorization.create_permission( update.account, update.permission, parent_id, update.auth );

      int64_t new_size = (int64_t)(config::billable_size_v<permission_object> + p.auth.get_billable_size());

      context.add_mem_usage( update.account, new_size );
   }
}

void apply_inery_deleteauth(apply_context& context) {
//   context.require_write_lock( config::inery_auth_scope );

   auto remove = context.get_action().data_as<deleteauth>();
   context.require_authorization(remove.account); // only here to mark the single authority on this action as used

   INE_ASSERT(remove.permission != config::active_name, action_validate_exception, "Cannot delete active authority");
   INE_ASSERT(remove.permission != config::owner_name, action_validate_exception, "Cannot delete owner authority");

   auto& authorization = context.control.get_mutable_authorization_manager();
   auto& db = context.db;



   { // Check for links to this permission
      const auto& index = db.get_index<permission_link_index, by_permission_name>();
      auto range = index.equal_range(boost::make_tuple(remove.account, remove.permission));
      INE_ASSERT(range.first == range.second, action_validate_exception,
                 "Cannot delete a linked authority. Unlink the authority first. This authority is linked to ${code}::${type}.",
                 ("code", range.first->code)("type", range.first->message_type));
   }

   const auto& permission = authorization.get_permission({remove.account, remove.permission});
   int64_t old_size = config::billable_size_v<permission_object> + permission.auth.get_billable_size();

   authorization.remove_permission( permission );

   context.add_mem_usage( remove.account, -old_size );

}

void apply_inery_linkauth(apply_context& context) {
//   context.require_write_lock( config::inery_auth_scope );

   auto requirement = context.get_action().data_as<linkauth>();
   try {
      INE_ASSERT(!requirement.requirement.empty(), action_validate_exception, "Required permission cannot be empty");

      context.require_authorization(requirement.account); // only here to mark the single authority on this action as used

      auto& db = context.db;
      const auto *account = db.find<account_object, by_name>(requirement.account);
      INE_ASSERT(account != nullptr, account_query_exception,
                 "Failed to retrieve account: ${account}", ("account", requirement.account)); // Redundant?
      const auto *code = db.find<account_object, by_name>(requirement.code);
      INE_ASSERT(code != nullptr, account_query_exception,
                 "Failed to retrieve code for account: ${account}", ("account", requirement.code));
      if( requirement.requirement != config::inery_any_name ) {
         const permission_object* permission = nullptr;
         if( context.control.is_builtin_activated( builtin_protocol_feature_t::only_link_to_existing_permission ) ) {
            permission = db.find<permission_object, by_owner>(
                           boost::make_tuple( requirement.account, requirement.requirement )
                         );
         } else {
            permission = db.find<permission_object, by_name>(requirement.requirement);
         }

         INE_ASSERT(permission != nullptr, permission_query_exception,
                    "Failed to retrieve permission: ${permission}", ("permission", requirement.requirement));
      }

      auto link_key = boost::make_tuple(requirement.account, requirement.code, requirement.type);
      auto link = db.find<permission_link_object, by_action_name>(link_key);

      if( link ) {
         INE_ASSERT(link->required_permission != requirement.requirement, action_validate_exception,
                    "Attempting to update required authority, but new requirement is same as old");
         db.modify(*link, [requirement = requirement.requirement](permission_link_object& link) {
             link.required_permission = requirement;
         });
      } else {
         const auto& l =  db.create<permission_link_object>([&requirement](permission_link_object& link) {
            link.account = requirement.account;
            link.code = requirement.code;
            link.message_type = requirement.type;
            link.required_permission = requirement.requirement;
         });

         context.add_mem_usage(
            l.account,
            (int64_t)(config::billable_size_v<permission_link_object>)
         );
      }

  } FC_CAPTURE_AND_RETHROW((requirement))
}

void apply_inery_unlinkauth(apply_context& context) {
//   context.require_write_lock( config::inery_auth_scope );

   auto& db = context.db;
   auto unlink = context.get_action().data_as<unlinkauth>();

   context.require_authorization(unlink.account); // only here to mark the single authority on this action as used

   auto link_key = boost::make_tuple(unlink.account, unlink.code, unlink.type);
   auto link = db.find<permission_link_object, by_action_name>(link_key);
   INE_ASSERT(link != nullptr, action_validate_exception, "Attempting to unlink authority, but no link found");
   context.add_mem_usage(
      link->account,
      -(int64_t)(config::billable_size_v<permission_link_object>)
   );

   db.remove(*link);
}

void apply_inery_canceldelay(apply_context& context) {
   auto cancel = context.get_action().data_as<canceldelay>();
   context.require_authorization(cancel.canceling_auth.actor); // only here to mark the single authority on this action as used

   const auto& trx_id = cancel.trx_id;

   context.cancel_deferred_transaction(transaction_id_to_sender_id(trx_id), account_name());
}

} } // namespace inery::chain