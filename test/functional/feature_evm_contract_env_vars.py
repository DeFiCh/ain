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
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
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
        self.nodes[0].generate(1)

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
                }
            ]
        )
        node.generate(1)

        abi, bytecode = EVMContract.from_file("GlobalVariable.sol", "GlobalVariable").compile()
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

        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        self.contract = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

    def run_test(self):
        self.setup()

        self.should_create_contract()

        node = self.nodes[0]

        tx = {
            "from": self.evm_key_pair.address,
            "value": "0x1a",
            # "data": self.contract.encodeABI('mul', [2,3]),
            "chainId": node.w3.eth.chain_id,
            "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
            "gasPrice": 10_000_000_000,
            "gas": 1_000_000,
        }

        b1hash = self.contract.functions.blockHash(1).call().hex()
        b1 = node.eth_getBlockByNumber(1, False)
        assert_equal(f"0x{b1hash}", b1["hash"])

        base_fee = self.contract.functions.baseFee().call()
        print('base_fee: ', base_fee)

        chain_id = self.contract.functions.chainId().call()
        assert_equal(chain_id, 1133)

        block = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        raw_tx = self.nodes[0].getrawtransaction(block['tx'][0], 1)
        opreturn_miner_keyid = raw_tx['vout'][1]['scriptPubKey']['hex'][120:]
        miner_eth_address = self.nodes[0].addressmap(self.nodes[0].get_genesis_keys().operatorAuthAddress, 1)
        miner_eth_keyid = self.nodes[0].getaddressinfo(miner_eth_address['format']['erc55'])['witness_program']
        assert_equal(opreturn_miner_keyid, miner_eth_keyid)
        coinbase = self.contract.functions.coinbase().call()
        assert_equal(coinbase, self.nodes[0].w3.to_checksum_address(f"0x{opreturn_miner_keyid}"))

        difficulty = self.contract.functions.difficulty().call()
        assert_equal(difficulty, 0)

        gas = self.contract.functions.gasLimit().call()
        assert_equal(gas, 30_000_000)

        n = self.contract.functions.blockNumber().call()
        bn = node.eth_blockNumber()
        assert_equal(f"0x{n}", bn)

        ts = self.contract.functions.timestamp().call()
        print('ts: ', ts)

        # gasLeft = self.contract.functions.gasLeft().call()
        # print('gasLeft: ', gasLeft)

        sender = self.contract.functions.getSender().call()
        assert_equal(sender, self.evm_key_pair.address)

        # data = self.contract.functions.getData().call(tx)
        # print('data: ', data)
        # assert_equal(data, tx["data"])

        value = self.contract.functions.getValue().call(tx)
        assert_equal(value, int(tx['value'], base=16))

        # sig = self.contract.functions.getSig().call(tx)
        # print('sig: ', sig)

        # tx_gas_price = self.contract.functions.getTxGasPrice().call(tx)
        # print('tx_gas_price: ', tx_gas_price)

        # tx_origin = self.contract.functions.getTxOrigin().call(tx)
        # print('tx_origin: ', tx_origin)

        count = self.contract.functions.count().call()
        print('count: ', count)

if __name__ == "__main__":
    EVMTest().main()
