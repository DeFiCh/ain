#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test eth tracer RPC behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.util import assert_equal

from decimal import Decimal
import json
import os

TESTSDIR = os.path.dirname(os.path.realpath(__file__))


class EvmTracerTest(DefiTestFramework):
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
                "-df23height=150",
                "-subsidytest=1",
                "-ethmaxresponsesize=100",
            ],
        ]

    def add_options(self, parser):
        parser.add_argument(
            "--native-td-in-file",
            dest="native_td_in_file",
            default="data/trace_native_transferdomain_in.json",
            action="store",
            metavar="FILE",
            help="Native transferdomain in data file",
        )
        parser.add_argument(
            "--native-td-out-file",
            dest="native_td_out_file",
            default="data/trace_native_transferdomain_out.json",
            action="store",
            metavar="FILE",
            help="Native transferdomain out data file",
        )
        parser.add_argument(
            "--dst20-td-in-file",
            dest="dst20_td_in_file",
            default="data/trace_dst20_transferdomain_in.json",
            action="store",
            metavar="FILE",
            help="DST20 transferdomain in data file",
        )
        parser.add_argument(
            "--dst20-td-out-file",
            dest="dst20_td_out_file",
            default="data/trace_dst20_transferdomain_out.json",
            action="store",
            metavar="FILE",
            help="DST20 transferdomain out data file",
        )
        parser.add_argument(
            "--contract-creation-tx-file",
            dest="contract_creation_tx_file",
            default="data/trace_contract_creation_tx.json",
            action="store",
            metavar="FILE",
            help="Contract creation tx data file",
        )

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address_erc55 = self.nodes[0].addressmap(self.address, 1)["format"][
            "erc55"
        ]
        self.ethAddress = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
        self.ethPrivKey = (
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.toPrivKey = (
            "17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35"
        )
        self.nodes[0].importprivkey(self.ethPrivKey)  # ethAddress
        self.nodes[0].importprivkey(self.toPrivKey)  # toAddress

        # Generate chain and move to fork height
        self.nodes[0].generate(105)
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
        self.nodes[0].generate(2)
        self.nodes[0].transferdomain(
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
        self.nodes[0].generate(50)
        self.start_height = self.nodes[0].getblockcount()
        self.load_test_data()

    def load_test_data(self):
        native_td_in_f = os.path.join(TESTSDIR, self.options.native_td_in_file)
        native_td_out_f = os.path.join(TESTSDIR, self.options.native_td_out_file)
        dst20_td_in_f = os.path.join(TESTSDIR, self.options.dst20_td_in_file)
        dst20_td_out_f = os.path.join(TESTSDIR, self.options.dst20_td_out_file)
        contract_creation_tx_f = os.path.join(
            TESTSDIR, self.options.contract_creation_tx_file
        )
        with open(native_td_in_f, "r", encoding="utf8") as f:
            self.native_td_in_data = json.load(f)
        with open(native_td_out_f, "r", encoding="utf8") as f:
            self.native_td_out_data = json.load(f)
        with open(dst20_td_in_f, "r", encoding="utf8") as f:
            self.dst20_td_in_data = json.load(f)
        with open(dst20_td_out_f, "r", encoding="utf8") as f:
            self.dst20_td_out_data = json.load(f)
        with open(contract_creation_tx_f, "r", encoding="utf8") as f:
            self.contract_creation_tx_data = json.load(f)

    def test_tracer_on_trace_call(self):
        self.rollback_to(self.start_height)

        # Test tracer with eth call for transfer tx
        call = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gas": "0x5209",
            "gasPrice": "0x5D21DBA00",  # 25_000_000_000
        }
        assert_equal(
            self.nodes[0].debug_traceCall(call, "latest"),
            {
                "gas": "0x5208",
                "failed": False,
                "returnValue": "",
                "structLogs": [],
            },
        )

    def test_tracer_on_transfer_tx(self):
        self.rollback_to(self.start_height)

        start_nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for i in range(3):
            _ = self.nodes[0].eth_sendTransaction(
                {
                    "nonce": hex(start_nonce + i),
                    "from": self.ethAddress,
                    "to": self.toAddress,
                    "value": "0xDE0B6B3A7640000",  # 1 DFI
                    "gas": "0x5209",
                    "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                }
            )
        self.nodes[0].generate(1)
        block_info = self.nodes[0].eth_getBlockByNumber("latest", True)
        block_txs = block_info["transactions"]

        # Test tracer for every tx
        block_trace = []
        for tx in block_txs:
            assert_equal(
                self.nodes[0].debug_traceTransaction(tx["hash"]),
                {"gas": "0x5208", "failed": False, "returnValue": "", "structLogs": []},
            )
            # Accumulate tx traces
            res = self.nodes[0].debug_traceTransaction(tx["hash"])
            block_trace.append({"result": res, "txHash": tx["hash"]})

        # Test block tracer
        assert_equal(block_trace, self.nodes[0].debug_traceBlockByNumber("latest"))
        assert_equal(
            block_trace, self.nodes[0].debug_traceBlockByHash(block_info["hash"])
        )

    def test_tracer_on_transfer_tx_with_transferdomain_txs(self):
        self.rollback_to(self.start_height)

        start_nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for i in range(3):
            _ = self.nodes[0].eth_sendTransaction(
                {
                    "nonce": hex(start_nonce + i),
                    "from": self.ethAddress,
                    "to": self.toAddress,
                    "value": "0xDE0B6B3A7640000",  # 1 DFI
                    "gas": "0x5209",
                    "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                }
            )
        # Add transfer domain in inside block
        in_hash = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@DFI", "domain": 2},
                    "dst": {
                        "address": self.ethAddress,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        # Add transfer domain out inside block
        out_hash = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.ethAddress, "amount": "1@DFI", "domain": 3},
                    "dst": {
                        "address": self.address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        for i in range(4, 7):
            _ = self.nodes[0].eth_sendTransaction(
                {
                    "nonce": hex(start_nonce + i),
                    "from": self.ethAddress,
                    "to": self.toAddress,
                    "value": "0xDE0B6B3A7640000",  # 1 DFI
                    "gas": "0x5209",
                    "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                }
            )
        self.nodes[0].generate(1)
        block_info = self.nodes[0].eth_getBlockByNumber("latest", True)
        block_txs = block_info["transactions"]

        evm_in_hash = self.nodes[0].vmmap(in_hash, 5)["output"]
        evm_out_hash = self.nodes[0].vmmap(out_hash, 5)["output"]

        # Test tracer for every tx
        block_trace = []
        for tx in block_txs:
            if tx["hash"] == evm_in_hash:
                # Test trace for transferdomain evm-in tx
                assert_equal(
                    self.nodes[0].debug_traceTransaction(tx["hash"]),
                    self.native_td_in_data,
                )
            elif tx["hash"] == evm_out_hash:
                # Test trace for transferdomain evm-in tx
                assert_equal(
                    self.nodes[0].debug_traceTransaction(tx["hash"]),
                    self.native_td_out_data,
                )
            else:
                assert_equal(
                    self.nodes[0].debug_traceTransaction(tx["hash"]),
                    {
                        "gas": "0x5208",
                        "failed": False,
                        "returnValue": "",
                        "structLogs": [],
                    },
                )
            # Accumulate tx traces
            res = self.nodes[0].debug_traceTransaction(tx["hash"])
            block_trace.append({"result": res, "txHash": tx["hash"]})

        # Test block tracer
        assert_equal(block_trace, self.nodes[0].debug_traceBlockByNumber("latest"))
        assert_equal(
            block_trace, self.nodes[0].debug_traceBlockByHash(block_info["hash"])
        )

    def test_tracer_on_transfer_tx_with_dst20_transferdomain_txs(self):
        self.rollback_to(self.start_height)

        # Create tokens
        symbolBTC = "BTC"
        self.nodes[0].createtoken(
            {
                "symbol": symbolBTC,
                "name": "BTC token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )

        self.nodes[0].generate(1)

        # These seemingly benign read-only calls are failing up to commit ffe007f86826cad654cec8ddfeb6e9436206ed08.
        # Tracing of new contract creation on EVM was overwriting the contract
        # state trie and corrupted the underlying backend.
        # Fixed in https://github.com/DeFiCh/ain/pull/2941 by adding new contract state trie creation to overlay.
        self.nodes[0].debug_traceBlockByNumber("latest")
        self.nodes[0].eth_getStorageAt(
            "0xff00000000000000000000000000000000000001", "0x0"
        )

        self.nodes[0].minttokens("100@BTC")
        self.nodes[0].generate(1)
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "10@BTC", "domain": 2},
                    "dst": {
                        "address": self.ethAddress,
                        "amount": "10@BTC",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        start_nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for i in range(3):
            _ = self.nodes[0].eth_sendTransaction(
                {
                    "nonce": hex(start_nonce + i),
                    "from": self.ethAddress,
                    "to": self.toAddress,
                    "value": "0xDE0B6B3A7640000",  # 1 DFI
                    "gas": "0x5209",
                    "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                }
            )
        # Add transfer domain in inside block
        in_hash = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.ethAddress,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        # Add transfer domain out inside block
        out_hash = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.ethAddress, "amount": "1@BTC", "domain": 3},
                    "dst": {
                        "address": self.address,
                        "amount": "1@BTC",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        for i in range(4, 7):
            _ = self.nodes[0].eth_sendTransaction(
                {
                    "nonce": hex(start_nonce + i),
                    "from": self.ethAddress,
                    "to": self.toAddress,
                    "value": "0xDE0B6B3A7640000",  # 1 DFI
                    "gas": "0x5209",
                    "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                }
            )
        self.nodes[0].generate(1)
        block_info = self.nodes[0].eth_getBlockByNumber("latest", True)
        block_txs = block_info["transactions"]

        evm_in_hash = self.nodes[0].vmmap(in_hash, 5)["output"]
        evm_out_hash = self.nodes[0].vmmap(out_hash, 5)["output"]

        # Test tracer for every tx
        block_trace = []
        for tx in block_txs:
            if tx["hash"] == evm_in_hash:
                # Test trace for transferdomain evm-in tx
                assert_equal(
                    self.nodes[0].debug_traceTransaction(tx["hash"]),
                    self.dst20_td_in_data,
                )
            elif tx["hash"] == evm_out_hash:
                # Test trace for transferdomain evm-out tx
                assert_equal(
                    self.nodes[0].debug_traceTransaction(tx["hash"]),
                    self.dst20_td_out_data,
                )
            else:
                assert_equal(
                    self.nodes[0].debug_traceTransaction(tx["hash"]),
                    {
                        "gas": "0x5208",
                        "failed": False,
                        "returnValue": "",
                        "structLogs": [],
                    },
                )
            # Accumulate tx traces
            res = self.nodes[0].debug_traceTransaction(tx["hash"])
            block_trace.append({"result": res, "txHash": tx["hash"]})

        # Test block tracer
        assert_equal(block_trace, self.nodes[0].debug_traceBlockByNumber("latest"))
        assert_equal(
            block_trace, self.nodes[0].debug_traceBlockByHash(block_info["hash"])
        )

    def test_tracer_on_transfer_tx_with_deploy_dst20_txs(self):
        self.rollback_to(self.start_height)

        start_nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for i in range(6):
            _ = self.nodes[0].eth_sendTransaction(
                {
                    "nonce": hex(start_nonce + i),
                    "from": self.ethAddress,
                    "to": self.toAddress,
                    "value": "0xDE0B6B3A7640000",  # 1 DFI
                    "gas": "0x5209",
                    "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                }
            )
        # Create tokens
        self.nodes[0].createtoken(
            {
                "symbol": "USDT",
                "name": "USDT token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].createtoken(
            {
                "symbol": "BTC",
                "name": "BTC token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)
        block_info = self.nodes[0].eth_getBlockByNumber("latest", True)
        block_txs = block_info["transactions"]

        # Test tracer for every DST20 creation tx
        block_trace = []
        for tx in block_txs[:2]:
            assert_equal(
                self.nodes[0].debug_traceTransaction(tx["hash"]),
                {
                    "gas": "0x0",
                    "failed": False,
                    "returnValue": "",
                    "structLogs": [],
                },
            )
            # Accumulate tx traces
            res = self.nodes[0].debug_traceTransaction(tx["hash"])
            block_trace.append({"result": res, "txHash": tx["hash"]})

        # Test tracer for every transfer tx
        for tx in block_txs[2:]:
            assert_equal(
                self.nodes[0].debug_traceTransaction(tx["hash"]),
                {
                    "gas": "0x5208",
                    "failed": False,
                    "returnValue": "",
                    "structLogs": [],
                },
            )
            # Accumulate tx traces
            res = self.nodes[0].debug_traceTransaction(tx["hash"])
            block_trace.append({"result": res, "txHash": tx["hash"]})

        # Test block tracer
        assert_equal(block_trace, self.nodes[0].debug_traceBlockByNumber("latest"))
        assert_equal(
            block_trace, self.nodes[0].debug_traceBlockByHash(block_info["hash"])
        )

    def test_tracer_on_transfer_tx_with_deploy_and_update_dst20_txs(self):
        self.rollback_to(self.start_height)

        # Create tokens
        self.nodes[0].createtoken(
            {
                "symbol": "USDT",
                "name": "USDT token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)
        block_info = self.nodes[0].eth_getBlockByNumber("latest", True)
        block_txs = block_info["transactions"]

        # Test DST20 token creation tx
        assert_equal(
            self.nodes[0].debug_traceTransaction(block_txs[0]["hash"]),
            {
                "gas": "0x0",
                "failed": False,
                "returnValue": "",
                "structLogs": [],
            },
        )

        # Update tokens
        token_info = self.nodes[0].listtokens()["1"]
        self.nodes[0].updatetoken("1", {"symbol": "goldy", "name": "GOLD token"})
        start_nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for i in range(6):
            _ = self.nodes[0].eth_sendTransaction(
                {
                    "nonce": hex(start_nonce + i),
                    "from": self.ethAddress,
                    "to": self.toAddress,
                    "value": "0xDE0B6B3A7640000",  # 1 DFI
                    "gas": "0x5209",
                    "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                }
            )
        self.nodes[0].generate(1)
        block_info = self.nodes[0].eth_getBlockByNumber("latest", True)
        block_txs = block_info["transactions"]
        block_trace = []

        # Test DST20 token update tx
        assert_equal(
            self.nodes[0].debug_traceTransaction(block_txs[0]["hash"]),
            {
                "gas": "0x0",
                "failed": False,
                "returnValue": "",
                "structLogs": [],
            },
        )
        # Accumulate tx traces
        res = self.nodes[0].debug_traceTransaction(block_txs[0]["hash"])
        block_trace.append({"result": res, "txHash": block_txs[0]["hash"]})

        # Test tracer for every tx
        for tx in block_txs[1:]:
            assert_equal(
                self.nodes[0].debug_traceTransaction(tx["hash"]),
                {
                    "gas": "0x5208",
                    "failed": False,
                    "returnValue": "",
                    "structLogs": [],
                },
            )
            # Accumulate tx traces
            res = self.nodes[0].debug_traceTransaction(tx["hash"])
            block_trace.append({"result": res, "txHash": tx["hash"]})

        # Test block tracer
        assert_equal(block_trace, self.nodes[0].debug_traceBlockByNumber("latest"))
        assert_equal(
            block_trace, self.nodes[0].debug_traceBlockByHash(block_info["hash"])
        )

        # Check DST20 update tokens
        token_info = self.nodes[0].listtokens()["1"]
        assert_equal(token_info["symbol"], "goldy")
        assert_equal(token_info["name"], "GOLD token")

    def test_tracer_on_contract_call_tx(self):
        self.rollback_to(self.start_height)

        before_balance = Decimal(
            self.nodes[0].getaccount(self.ethAddress)[0].split("@")[0]
        )
        assert_equal(before_balance, Decimal("100"))

        # Deploy StateChange contract
        abi, bytecode, _ = EVMContract.from_file(
            "StateChange.sol", "StateChange"
        ).compile()
        compiled = self.nodes[0].w3.eth.contract(abi=abi, bytecode=bytecode)
        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(self.ethAddress),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        contract_address = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)[
            "contractAddress"
        ]
        contract = self.nodes[0].w3.eth.contract(address=contract_address, abi=abi)

        # Test tracer for contract creation tx
        assert_equal(
            self.nodes[0].debug_traceTransaction(hash.hex()),
            self.contract_creation_tx_data,
        )

        # Set state to true
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        tx = contract.functions.changeState(True).build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": nonce,
                "gasPrice": 25_000_000_000,
                "gas": 30_000_000,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
        state_change_tx_hash = self.nodes[0].w3.eth.send_raw_transaction(
            signed.rawTransaction
        )

        # Run loop contract call in the same block
        tx = contract.functions.loop(1_000).build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": nonce + 1,
                "gasPrice": 25_000_000_000,
                "gas": 30_000_000,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
        loop_tx_hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        # Test tracer for contract call txs
        state_change_gas_used = Decimal(
            self.nodes[0].w3.eth.wait_for_transaction_receipt(state_change_tx_hash)[
                "gasUsed"
            ]
        )
        loop_gas_used = Decimal(
            self.nodes[0].w3.eth.wait_for_transaction_receipt(loop_tx_hash)["gasUsed"]
        )
        # Test tracer for state change tx
        assert_equal(
            int(
                self.nodes[0].debug_traceTransaction(state_change_tx_hash.hex())["gas"],
                16,
            ),
            state_change_gas_used,
        )
        assert_equal(
            self.nodes[0].debug_traceTransaction(state_change_tx_hash.hex())["failed"],
            False,
        )
        # Test tracer for loop tx
        assert_equal(
            int(
                self.nodes[0].debug_traceTransaction(loop_tx_hash.hex())["gas"],
                16,
            ),
            loop_gas_used,
        )
        assert_equal(
            self.nodes[0].debug_traceTransaction(loop_tx_hash.hex())["failed"],
            False,
        )

    def run_test(self):
        self.setup()

        self.test_tracer_on_trace_call()

        self.test_tracer_on_transfer_tx()

        self.test_tracer_on_transfer_tx_with_transferdomain_txs()

        self.test_tracer_on_transfer_tx_with_dst20_transferdomain_txs()

        self.test_tracer_on_transfer_tx_with_deploy_dst20_txs()

        self.test_tracer_on_transfer_tx_with_deploy_and_update_dst20_txs()

        self.test_tracer_on_contract_call_tx()


if __name__ == "__main__":
    EvmTracerTest().main()
