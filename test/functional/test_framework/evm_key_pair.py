from eth_account import Account
from web3 import Web3


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
        # TODO: remove to_checksum_address(), getnewaddress should return a checksum address
        address = Web3.to_checksum_address(node.getnewaddress("", "eth"))
        pkey = node.dumpprivkey(address)

        return KeyPair(pkey, address)
