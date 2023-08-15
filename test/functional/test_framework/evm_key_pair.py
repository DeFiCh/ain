from eth_account import Account


class EvmKeyPair:
    def __init__(self, privkey: str = None, address: str = None):
        self.privkey, self.address = EvmKeyPair.validate_key(privkey, address)

    @staticmethod
    def from_node(node):
        # get address from node
        address = node.getnewaddress("", "erc55")
        privkey = node.dumpprivkey(address)

        return EvmKeyPair(privkey, address)

    @staticmethod
    def validate_key(privkey, address):
        account = Account.from_key(privkey)

        if account.address != address:
            raise RuntimeError(
                f"""
                Private key does not correspond to provided address.
                Address provided: {address}
                Address computed: {account.address}
                """
            )
        else:
            return privkey, address
