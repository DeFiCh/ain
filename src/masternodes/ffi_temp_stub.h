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

inline ::rust::Vec<::std::uint8_t> create_and_sign_tx(::CreateTransactionContext ctx) {
    return {};
}

inline uint64_t evm_get_context() {
    return {};
}

inline ::rust::Vec<::std::uint8_t> evm_finalize(::std::uint64_t context, bool update_state, ::std::uint32_t difficulty, ::std::array<::std::uint8_t, 20> miner_address) {
    return {};
}

inline void evm_discard_context(::std::uint64_t context) {}

inline uint64_t evm_get_balance(::rust::Str address) {
    return {};
}

inline void evm_add_balance(::std::uint64_t context, ::rust::Str address, ::std::array<::std::uint8_t, 32> amount) {}

inline bool evm_sub_balance(::std::uint64_t context, ::rust::Str address, ::std::array<::std::uint8_t, 32> amount) {
    return {};
}

inline bool evm_validate_raw_tx(::rust::Str tx) {
    return {};
}

inline bool evm_queue_tx(::std::uint64_t context, ::rust::Str raw_tx) {
    return {};
}

inline void init_runtime() {}
inline void start_servers(::rust::Str json_addr, ::rust::Str grpc_addr) {}
inline void stop_runtime() {}

#endif  // DEFI_MASTERNODES_FFI_TEMP_STUB_H

