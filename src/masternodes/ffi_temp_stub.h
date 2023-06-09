#ifndef DEFI_MASTERNODES_FFI_TEMP_STUB_H
#define DEFI_MASTERNODES_FFI_TEMP_STUB_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rust {
    template<typename T>
    using Vec = std::vector<T>;

    using Str = std::string;
} // namespace rust

struct CreateTransactionContext {
    ::std::uint64_t chain_id;
    ::std::array<::std::uint8_t, 32> nonce;
    ::std::array<::std::uint8_t, 32> gas_price;
    ::std::array<::std::uint8_t, 32> gas_limit;
    ::std::array<::std::uint8_t, 20> to;
    ::std::array<::std::uint8_t, 32> value;
    ::rust::Vec<::std::uint8_t> input;
    ::std::array<::std::uint8_t, 32> priv_key;

    using IsRelocatable = ::std::true_type;
};

struct FinalizeBlockResult final {
    ::std::array<::std::uint8_t, 32> block_hash;
    ::rust::Vec<::rust::Str> failed_transactions;
    ::std::uint64_t miner_fee;

    using IsRelocatable = ::std::true_type;
};

struct RustRes final {
    bool ok;
    ::rust::Str reason;

    using IsRelocatable = ::std::true_type;
};

struct ValidateTxResult final {
    ::std::uint64_t nonce;
    ::std::array<::std::uint8_t, 20> sender;
    ::std::uint64_t used_gas;

    using IsRelocatable = ::std::true_type;
};

inline ::rust::Vec<::std::uint8_t> create_and_sign_tx(::CreateTransactionContext ctx) {
    return {};
}

inline uint64_t evm_get_context() {
    return {};
}

inline FinalizeBlockResult evm_finalize(::std::uint64_t context, bool update_state, ::std::uint32_t difficulty, ::std::array<::std::uint8_t, 20> miner_address, ::std::uint64_t timestamp) {
    return {};
}

inline void evm_discard_context(::std::uint64_t context) {}

inline uint64_t evm_get_balance(::std::array<::std::uint8_t, 20> address) {
    return {};
}

inline void evm_add_balance(::std::uint64_t context, ::rust::Str address, ::std::array<::std::uint8_t, 32> amount, ::std::array<::std::uint8_t, 32> native_tx_hash) {}

inline bool evm_sub_balance(::std::uint64_t context, ::rust::Str address, ::std::array<::std::uint8_t, 32> amount, ::std::array<::std::uint8_t, 32> native_tx_hash) {
    return {};
}

inline ValidateTxResult evm_try_prevalidate_raw_tx(::RustRes &result, ::rust::Str tx) {
    return {};
}

inline bool evm_try_queue_tx(::RustRes &result, ::std::uint64_t context, ::rust::Str raw_tx, ::std::array<::std::uint8_t, 32> native_tx_hash) {
    return {};
}

inline bool evm_prevalidate_raw_tx(::rust::Str tx) {
    return {};
}

inline uint64_t evm_get_next_valid_nonce_in_context(uint64_t context, ::std::array<::std::uint8_t, 20> address) {
    return {};
}

inline void preinit() {}
inline void init_evm_runtime() {}
inline void start_servers(::rust::Str json_addr, ::rust::Str grpc_addr) {}
inline void stop_evm_runtime() {}
inline void evm_disconnect_latest_block() {};

#endif  // DEFI_MASTERNODES_FFI_TEMP_STUB_H

