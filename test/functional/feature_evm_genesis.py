#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
import os

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256,
)

TESTSDIR = os.path.dirname(os.path.realpath(__file__))


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
        self.ethAddress = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.ethPrivKey = (
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )
        self.nodes[0].importprivkey(self.ethPrivKey)  # ethAddress

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(
            -32600,
            "called before Metachain height",
            self.nodes[0].evmtx,
            self.ethAddress,
            0,
            21,
            21000,
            self.toAddress,
            0.1,
        )

        # Move to fork height
        self.nodes[0].generate(4)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
                    "v0/transferdomain/dvm-evm/src-formats": ["p2pkh", "bech32"],
                    "v0/transferdomain/dvm-evm/dest-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/src-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/auth-formats": ["bech32-erc55"],
                    "v0/transferdomain/evm-dvm/dest-formats": ["p2pkh", "bech32"],
                }
            }
        )
        self.nodes[0].generate(1)

    def tx_should_not_go_into_genesis_block(self):
        # Send transferdomain tx on genesis block
        hash = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.ethAddress,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)
        # Transferdomain tx should not be minted after genesis block
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        assert_equal(len(block_info["tx"]), 1)
        balance = self.nodes[0].eth_getBalance(self.ethAddress)
        assert_equal(balance, int_to_eth_u256(0))
        assert_equal(self.nodes[0].getrawmempool()[0], hash)

        # Transferdomain tx should be minted after genesis block
        self.nodes[0].generate(1)
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        assert_equal(len(block_info["tx"]), 2)
        tx_info = block_info["tx"][1]
        assert_equal(tx_info["vm"]["txtype"], "TransferDomain")
        assert_equal(tx_info["txid"], hash)
        balance = self.nodes[0].eth_getBalance(self.ethAddress)
        assert_equal(balance, int_to_eth_u256(100))

    def test_start_state_from_json(self):
        genesis = os.path.join(TESTSDIR, "data/evm-genesis.json")
        self.extra_args = [
            [
                "-ethstartstate={}".format(genesis),
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
                "-metachainheight=105",
                "-subsidytest=1",
            ],
        ]
        self.stop_nodes()
        self.start_nodes(self.extra_args)
        self.nodes[0].generate(111)

        ethblock0 = self.nodes[0].eth_getBlockByNumber(0)
        assert_equal(ethblock0["difficulty"], "0x400000")
        assert_equal(ethblock0["extraData"], "0x686f727365")
        assert_equal(ethblock0["gasLimit"], "0x1388")
        assert_equal(
            ethblock0["parentHash"],
            "0x0000000000000000000000000000000000000000000000000000000000000000",
        )
        # NOTE(canonbrother): overwritten by block.header.hash
        # assert_equal(ethblock0['mixHash'], '0x0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(ethblock0["nonce"], "0x123123123123123f")
        assert_equal(ethblock0["timestamp"], "0x539")

        balance = self.nodes[0].eth_getBalance(
            "a94f5374fce5edbc8e2a8697c15331677e6ebf0b"
        )
        assert_equal(balance, "0x9184e72a000")

    def test_rich_account_estimate_gas(self):
        self.create_contract_with_rich_account = {
            "from": "0x6be02d1d3665660d22ff9624b7be0551ee1ac91b",
            "data": "0x608060405234801561001057600080fd5b5061029a806100206000396000f3fe608060405234801561001057600080fd5b506004361061004c5760003560e01c806385df51fd14610051578063c6888fa114610081578063e12ed13c146100b1578063f68016b7146100cf575b600080fd5b61006b60048036038101906100669190610133565b6100ed565b604051610078919061017a565b60405180910390f35b61009b60048036038101906100969190610133565b6100f8565b6040516100a89190610195565b60405180910390f35b6100b961010e565b6040516100c69190610195565b60405180910390f35b6100d7610116565b6040516100e49190610195565b60405180910390f35b600081409050919050565b600060078261010791906101b0565b9050919050565b600043905090565b600045905090565b60008135905061012d8161024d565b92915050565b60006020828403121561014557600080fd5b60006101538482850161011e565b91505092915050565b6101658161020a565b82525050565b61017481610214565b82525050565b600060208201905061018f600083018461015c565b92915050565b60006020820190506101aa600083018461016b565b92915050565b60006101bb82610214565b91506101c683610214565b9250817fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff04831182151516156101ff576101fe61021e565b5b828202905092915050565b6000819050919050565b6000819050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b61025681610214565b811461026157600080fd5b5056fea264697066735822122007e3a06d62e2c601013aab5d0ae1a04d589ebdf57c4c56a01fa5c872e3e849d164736f6c63430008020033",
            "gas": "0x1c9c380",
        }

        gas = self.nodes[0].eth_estimateGas(self.create_contract_with_rich_account)
        assert gas

    def run_test(self):
        self.setup()

        self.tx_should_not_go_into_genesis_block()

        self.test_start_state_from_json()

        self.test_rich_account_estimate_gas()


if __name__ == "__main__":
    EVMTest().main()
