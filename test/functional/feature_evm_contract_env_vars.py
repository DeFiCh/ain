#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM contract"""

from test_framework.util import assert_equal
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair


class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-dakotaheight=51",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-metachainheight=105",
                "-subsidytest=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Generate chain
        self.nodes[0].generate(105)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/src-formats": ["p2pkh", "bech32"],
                    "v0/transferdomain/dvm-evm/dest-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/src-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/auth-formats": ["bech32-erc55"],
                    "v0/transferdomain/evm-dvm/dest-formats": ["p2pkh", "bech32"],
                }
            }
        )
        self.nodes[0].generate(2)

    def should_create_contract(self):
        node = self.nodes[0]
        self.evm_key_pair = EvmKeyPair.from_node(node)

        node.transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "50@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        node.generate(1)

        abi, bytecode, _ = EVMContract.from_file(
            "GlobalVariable.sol", "GlobalVariable"
        ).compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)
        self.block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash())

        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        self.contract = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

    def test_blockhash(self):
        b1hash = self.contract.functions.blockHash(1).call().hex()
        b1 = self.nodes[0].eth_getBlockByNumber(1, False)
        assert_equal(f"0x{b1hash}", b1["hash"])

    def test_block_basefee(self):
        base_fee = self.contract.functions.baseFee().call()
        assert_equal(base_fee, 10_000_000_000)

    def test_block_chainid(self):
        chain_id = self.contract.functions.chainId().call()
        assert_equal(chain_id, 1133)

    def test_coinbase(self):
        block = self.nodes[0].getblock(
            self.nodes[0].getblockhash(self.nodes[0].getblockcount()), 3
        )
        coinbase_xvm = block["tx"][0]["vm"]
        assert_equal(coinbase_xvm["vmtype"], "coinbase")
        assert_equal(coinbase_xvm["txtype"], "coinbase")
        opreturn_miner_keyid = coinbase_xvm["msg"]["evm"]["beneficiary"]
        coinbase = self.contract.functions.coinbase().call()
        assert_equal(
            coinbase, self.nodes[0].w3.to_checksum_address(opreturn_miner_keyid)
        )

    def test_difficulty(self):
        difficulty = int(self.contract.functions.difficulty().call())
        assert_equal(difficulty, int(self.block_info["bits"], base=16))

    def test_block_gaslimit(self):
        gas = self.contract.functions.gasLimit().call()
        assert_equal(gas, 30_000_000)

    def test_block_number(self):
        n = self.contract.functions.blockNumber().call()
        bn = self.nodes[0].eth_blockNumber()
        assert_equal(f"0x{n}", bn)

    def test_timestamp(self):
        block = self.nodes[0].getblock(
            self.nodes[0].getblockhash(self.nodes[0].getblockcount())
        )
        ts = self.contract.functions.timestamp().call()
        assert_equal(ts, block["time"])

    def test_gasleft(self):
        tx = {
            "from": self.evm_key_pair.address,
            "value": "0x0",
            "chainId": self.nodes[0].w3.eth.chain_id,
            "nonce": self.nodes[0].w3.eth.get_transaction_count(
                self.evm_key_pair.address
            ),
            "gas": 1_000_000,
            "maxFeePerGas": 15_000_000_000,
            "maxPriorityFeePerGas": 9_000_000_000,
            "type": "0x2",
        }
        gas_left = self.contract.functions.gasLeft().call(tx)  # 978_708
        assert tx["gas"] > gas_left

    def test_msg_sender(self):
        sender = self.contract.functions.getSender().call()
        assert_equal(sender, self.evm_key_pair.address)

    def test_msg_value(self):
        tx = {
            "from": self.evm_key_pair.address,
            "value": "0x1a",
            "chainId": self.nodes[0].w3.eth.chain_id,
            "nonce": self.nodes[0].w3.eth.get_transaction_count(
                self.evm_key_pair.address
            ),
            "gas": 1_000_000,
            "maxFeePerGas": 15_000_000_000,
            "maxPriorityFeePerGas": 9_000_000_000,
            "type": "0x2",
        }
        value = self.contract.functions.getValue().call(tx)
        assert_equal(value, int(tx["value"], base=16))

    def test_msg_data(self):
        tx = {
            "from": self.evm_key_pair.address,
            "value": "0x0",
            "chainId": self.nodes[0].w3.eth.chain_id,
            "nonce": self.nodes[0].w3.eth.get_transaction_count(
                self.evm_key_pair.address
            ),
            "gas": 1_000_000,
            "maxFeePerGas": 15_000_000_000,
            "maxPriorityFeePerGas": 9_000_000_000,
            "type": "0x2",
        }
        data = self.contract.encodeABI("getData", [])
        d = self.contract.functions.getData().call(tx)
        assert_equal(data, f"0x{d.hex()}")

    def test_msg_sig(self):
        tx = {
            "from": self.evm_key_pair.address,
            "value": "0x0",
            "chainId": self.nodes[0].w3.eth.chain_id,
            "nonce": self.nodes[0].w3.eth.get_transaction_count(
                self.evm_key_pair.address
            ),
            "gas": 1_000_000,
            "maxFeePerGas": 15_000_000_000,
            "maxPriorityFeePerGas": 9_000_000_000,
            "type": "0x2",
        }
        data = self.contract.encodeABI("getSig", [])
        sig = self.contract.functions.getSig().call(tx)
        assert_equal(data, f"0x{sig.hex()}")

    def test_tx_gasprice(self):
        tx = {
            "from": self.evm_key_pair.address,
            "value": "0x0",
            "chainId": self.nodes[0].w3.eth.chain_id,
            "nonce": self.nodes[0].w3.eth.get_transaction_count(
                self.evm_key_pair.address
            ),
            "gasPrice": 10_000_000_000,
            "gas": 1_000_000,
        }
        tx_gas_price = self.contract.functions.getTxGasPrice().call(tx)
        assert_equal(tx_gas_price, tx["gasPrice"])

        tx = {
            "from": self.evm_key_pair.address,
            "value": "0x0",
            "chainId": self.nodes[0].w3.eth.chain_id,
            "nonce": self.nodes[0].w3.eth.get_transaction_count(
                self.evm_key_pair.address
            ),
            "gas": 1_000_000,
            "maxFeePerGas": 15_000_000_000,
            "maxPriorityFeePerGas": 9_000_000_000,
            "type": "0x2",
        }
        tx_gas_price = self.contract.functions.getTxGasPrice().call(tx)
        assert_equal(tx_gas_price, tx["maxFeePerGas"])

    def test_tx_origin(self):
        tx = {
            "from": self.evm_key_pair.address,
            "value": "0x0",
            "chainId": self.nodes[0].w3.eth.chain_id,
            "nonce": self.nodes[0].w3.eth.get_transaction_count(
                self.evm_key_pair.address
            ),
            "gas": 1_000_000,
            "maxFeePerGas": 15_000_000_000,
            "maxPriorityFeePerGas": 9_000_000_000,
            "type": "0x2",
        }
        tx_origin = self.contract.functions.getTxOrigin().call(tx)
        assert_equal(tx_origin, self.evm_key_pair.address)

    def test_call_contract_property(self):
        count = self.contract.functions.count().call()
        assert_equal(count, 45)

    def run_test(self):
        self.setup()

        self.should_create_contract()

        self.test_blockhash()  # blockhash(number)

        self.test_block_basefee()  # block.basefee

        self.test_block_chainid()  # block.chainid

        self.test_coinbase()  # block.coinbase

        self.test_difficulty()  # block.difficulty

        self.test_block_gaslimit()  # block.gaslimit

        self.test_block_number()  # block.number

        self.test_timestamp()  # block.timestamp

        self.test_gasleft()  # gasleft()

        self.test_msg_sender()  # msg.sender

        self.test_msg_value()  # msg.value

        self.test_msg_data()  # msg.data

        self.test_msg_sig()  # msg.sig

        self.test_tx_gasprice()  # tx.gasprice

        self.test_tx_origin()  # tx.origin

        self.test_call_contract_property()


if __name__ == "__main__":
    EVMTest().main()
