#include <grpc/wallet.h>
#include <key_io.h>
#include <rpc/protocol.h>
#include <rpc/request.h>

void GetNewAddress(const Context& ctx, AddressInput& address_input, AddressResult& result)
{
    auto name = std::string((ctx.url != "") ? ctx.url : address_input.wallet);
    std::shared_ptr<CWallet> const wallet = GetWalletFromURL(name, false); // FIXME: Check if help needs to make a difference
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, false)) {
        return;
    }

    LOCK(pwallet->cs_wallet);

    if (!pwallet->CanGetAddresses()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }

    // Parse the label first so we don't generate a key if there's an error
    auto label = std::string(address_input.label);
    if (label == "*")
        label = "";

    OutputType output_type = pwallet->m_default_address_type;
    auto addr_type = std::string(address_input.field_type);
    if (addr_type != "") {
        if (!ParseOutputType(addr_type, output_type)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Unknown address type '%s'", addr_type));
        }
    }

    CTxDestination dest;
    std::string error;
    if (!pwallet->GetNewDestination(output_type, label, dest, error)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, error);
    }

    result.address = EncodeDestination(dest);
}
