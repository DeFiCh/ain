from eth_account import Account
from web3 import Web3

from .test_node import TestNode


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
    def __init__(self, node: TestNode = None, pkey: str = None, pkey_address: str = None):
        if pkey and pkey_address:
            self.pkey, self.address = pkey, pkey_address
        elif (pkey is None or pkey_address is None) and node:
            # get address from node
            # TODO: remove to_checksum_address(), getnewaddress should return a checksum address
            self.address = Web3.to_checksum_address(node.getnewaddress("", "eth"))
            self.pkey = node.dumpprivkey(self.address)
        else:
            raise RuntimeError("Unable to get signing keys. Provide node or a key pair.")

        self.pkey, self.address = validate_keys(self.pkey, self.address)
