from typing import Type

from web3 import Web3
from web3.contract import Contract
from web3.contract.contract import ContractFunction

from .evm_key_pair import KeyPair


class EVMProvider:
    def __init__(self, rpc_url, generate):
        self.w3 = Web3(Web3.HTTPProvider(rpc_url))
        self.generator = generate

    @staticmethod
    def from_node(node):
        return EVMProvider(node.get_evm_rpc(), node.generate)

    def deploy_compiled_contract(self, signer: KeyPair, compiled_contract, constructor=None) -> Type[Contract]:
        abi, bytecode = compiled_contract

        nonce = self.w3.eth.get_transaction_count(signer.address)
        tx = self.w3.eth.contract(abi=abi, bytecode=bytecode).constructor(constructor).build_transaction({
            'chainId': 1133,
            'nonce': nonce,
            'gasPrice': Web3.to_wei(10, "gwei")
        })

        signed_tx = self.w3.eth.account.sign_transaction(tx, private_key=signer.pkey)
        deploy_tx_hash = self.w3.eth.send_raw_transaction(signed_tx.rawTransaction)

        self.generator(1)

        tx_receipt = self.w3.eth.wait_for_transaction_receipt(deploy_tx_hash)
        return self.w3.eth.contract(address=tx_receipt.contractAddress, abi=abi)

    def sign_and_send(self, fn: ContractFunction, signer: KeyPair, gasprice: int = 10):
        nonce = self.w3.eth.get_transaction_count(signer.address)
        tx = fn.build_transaction({
            'chainId': self.w3.eth.chain_id,
            'nonce': nonce,
            'gasPrice': Web3.to_wei(gasprice, "gwei")
        })

        signed_tx = self.w3.eth.account.sign_transaction(tx, private_key=signer.pkey)
        tx_hash = self.w3.eth.send_raw_transaction(signed_tx.rawTransaction)

        self.generator(1)

        print(self.w3.eth.get_transaction(tx_hash))

        self.w3.eth.wait_for_transaction_receipt(tx_hash)
