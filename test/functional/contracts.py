from typing import Type, Callable

from solcx import compile_standard, install_solc
from web3 import Web3
from web3.contract.contract import ContractFunction
from web3.eth import Contract
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


class EVMContract:
    # Solidity compiler version
    _solc_version = "0.8.10"
    _path_prefix = "contracts"

    def __init__(self, file_name: str, contract_name: str, provider: str, generator: Callable,
                 pkey: str = "6dff3b5d0e3dd33c391caf81a4e7bc54e800b7a55cb8bfe3ade558ba945790b2",
                 pkey_address: str = "0x16b84F92f4F265f4D969378ad7394056FB56d1eb"):
        # Validate keys
        validate_keys(pkey, pkey_address)
        self.pkey = pkey
        self.address = pkey_address
        self.generator = generator

        # Install solc
        install_solc(self._solc_version)

        with open(f"./{self._path_prefix}/{file_name}", "r") as file:
            compiled_sol = compile_standard(
                {
                    "language": "Solidity",
                    "sources": {file_name: {"content": file.read()}},
                    "settings": {
                        "outputSelection": {
                            "*": {
                                "*": [
                                    "abi",
                                    "metadata",
                                    "evm.bytecode",
                                    "evm.sourceMap",
                                ]
                            }
                        }
                    },
                },
                solc_version=self._solc_version,
            )

        data = compiled_sol["contracts"][file_name][contract_name]
        abi = data["abi"]
        bytecode = data["evm"]["bytecode"]["object"]

        self.w3 = Web3(Web3.HTTPProvider(provider))
        self.contract = self.w3.eth.contract(abi=abi, bytecode=bytecode)

    def deploy_contract(self, constructor) -> Type[Contract]:
        nonce = self.w3.eth.get_transaction_count(self.address)
        tx = self.contract.constructor(constructor).build_transaction({
            'chainId': 1133,
            'nonce': nonce,
            'gasPrice': Web3.to_wei(5, "gwei")
        })


        signed_tx = self.w3.eth.account.sign_transaction(tx, private_key=self.pkey)
        deploy_tx_hash = self.w3.eth.send_raw_transaction(signed_tx.rawTransaction)

        self.generator(1)

        tx_receipt = self.w3.eth.wait_for_transaction_receipt(deploy_tx_hash)
        return self.w3.eth.contract(address=tx_receipt.contractAddress, abi=self.contract.abi)

    def get_address(self) -> str:
        return self.address

    def sign_and_send(self, fn: ContractFunction):
        nonce = self.w3.eth.get_transaction_count(self.address)
        tx = fn.build_transaction({
            'chainId': 1133,
            'nonce': nonce,
            'gasPrice': Web3.to_wei(5, "gwei")
        })

        signed_tx = self.w3.eth.account.sign_transaction(tx, private_key=self.pkey)
        tx_hash = self.w3.eth.send_raw_transaction(signed_tx.rawTransaction)

        self.generator(1)

        self.w3.eth.wait_for_transaction_receipt(tx_hash)
