from eth_account import Account


def validate_keys(pkey, pkey_address):
    account = Account.from_key(pkey)

    if account.address != pkey_address:
        raise RuntimeError(
            f"""
            Private key does not correspond to provided address.
            Address provided: {pkey_address}
            Address computed: {account.address}
            """)
    else:
        return pkey, pkey_address


class KeyPair:
    def __init__(self, pkey: str = None, pkey_address: str = None):
        self.pkey, self.address = validate_keys(pkey, pkey_address)

    @staticmethod
    def from_node(node):
        # get address from node
        address = node.getnewaddress("", "eth")
        pkey = node.dumpprivkey(address)

        return KeyPair(pkey, address)
