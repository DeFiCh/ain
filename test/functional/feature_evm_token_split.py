#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test DFI intrinsics contract"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, get_solc_artifact_path, int_to_eth_u256

from decimal import Decimal
import math
import time
from web3 import Web3


class EVMTokenSplitTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txnotokens=0",
                "-subsidytest=1",
                "-amkheight=1",
                "-bayfrontheight=1",
                "-dakotaheight=1",
                "-eunosheight=1",
                "-fortcanningheight=1",
                "-fortcanninghillheight=1",
                "-fortcanningroadheight=1",
                "-fortcanningcrunchheight=1",
                "-fortcanningspringheight=1",
                "-fortcanninggreatworldheight=1",
                "-fortcanningepilogueheight=1",
                "-grandcentralheight=1",
                "-metachainheight=105",
                "-df23height=150",
            ],
        ]

    def run_test(self):

        # Run setup
        self.setup()

        # Check intrinsics contract backwards compatibility
        self.check_backwards_compatibility()

        # Store block height for rollback
        self.block_height = self.nodes[0].getblockcount()

        # Split token via transfer domain
        self.transfer_domain_split()

        # Split token multiple times via transfer domain
        self.transfer_domain_multiple_split()

        # Split tokens via intrinsics contract
        self.intrinsic_token_split(20)

        # Partial split tokens via intrinsics contract
        self.intrinsic_token_split(10)

    def setup(self):

        # Define address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.splitMultiplier = 2

        # Generate chain
        self.nodes[0].generate(105)

        # Setup oracles
        self.setup_oracles()

        # Setup tokens
        self.setup_tokens()

        # Setup Gov vars
        self.setup_govvars()

        # Move to fork height
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Setup variables
        self.setup_variables()

    def setup_variables(self):

        # Define address
        self.evm_address = self.nodes[0].getnewaddress("", "erc55")
        self.evm_privkey = self.nodes[0].dumpprivkey(self.evm_address)

        self.contract_address_metav1 = self.nodes[0].w3.to_checksum_address(
            "0xff00000000000000000000000000000000000001"
        )

        self.contract_address_metav2 = self.nodes[0].w3.to_checksum_address(
            "0xff00000000000000000000000000000000000002"
        )

        self.contract_address_metav3 = self.nodes[0].w3.to_checksum_address(
            "0xff00000000000000000000000000000000000003"
        )

        # Registry ABI
        registry_abi = open(
            get_solc_artifact_path("dfi_intrinsics_registry", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        # DFI intrinsics V2 ABI
        self.intinsics_abi = open(
            get_solc_artifact_path("dfi_intrinsics_v2", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        # DST20 ABI
        self.dst20_abi = open(
            get_solc_artifact_path("dst20_v1", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        # Get registry contract
        registry = self.nodes[0].w3.eth.contract(
            address=self.nodes[0].w3.to_checksum_address(
                "0xdf00000000000000000000000000000000000000"
            ),
            abi=registry_abi,
        )

        # Get DFI Intrinsics V2 address
        self.v2_address = registry.functions.get(1).call()

        self.intrinsics_contract = self.nodes[0].w3.eth.contract(
            address=self.v2_address,
            abi=self.intinsics_abi,
        )

        # Check META variables
        self.meta_contract = self.nodes[0].w3.eth.contract(
            address=self.contract_address_metav1, abi=self.dst20_abi
        )
        assert_equal(self.meta_contract.functions.name().call(), "Meta")
        assert_equal(self.meta_contract.functions.symbol().call(), "META")

    def setup_oracles(self):

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "META"},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
            {"currency": "USD", "tokenAmount": "1@META"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

    def setup_tokens(self):

        # Set loan tokens
        self.nodes[0].setloantoken(
            {
                "symbol": "META",
                "name": "Meta",
                "fixedIntervalPriceId": "META/USD",
                "isDAT": True,
                "interest": 0,
            }
        )
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken(
            {
                "token": "DFI",
                "factor": 1,
                "fixedIntervalPriceId": "DFI/USD",
            }
        )
        self.nodes[0].generate(1)

        # Mint tokens
        self.nodes[0].minttokens(["1000@META"])
        self.nodes[0].generate(1)

        # Create account DFI
        self.nodes[0].utxostoaccount({self.address: "50@DFI"})
        self.nodes[0].generate(1)

    def setup_govvars(self):

        # Activate EVM and transfer domain
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/evm-dvm/enabled": "true",
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
                }
            }
        )
        self.nodes[0].generate(1)

    def check_backwards_compatibility(self):

        # Check version variable
        assert_equal(self.intrinsics_contract.functions.version().call(), 2)

        for _ in range(5):
            self.nodes[0].generate(1)

            # check evmBlockCount variable
            assert_equal(
                self.intrinsics_contract.functions.evmBlockCount().call(),
                self.nodes[0].w3.eth.get_block_number(),
            )

            # check dvmBlockCount variable
            assert_equal(
                self.intrinsics_contract.functions.dvmBlockCount().call(),
                self.nodes[0].getblockcount(),
            )

    def fund_address(self):

        # Fund address with META
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "20@META", "domain": 2},
                    "dst": {
                        "address": self.evm_address,
                        "amount": "20@META",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        # Fund address with DFI
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "10@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_address,
                        "amount": "10@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        # Check META balance
        self.meta_contract = self.nodes[0].w3.eth.contract(
            address=self.contract_address_metav1, abi=self.dst20_abi
        )
        assert_equal(
            self.meta_contract.functions.balanceOf(self.evm_address).call()
            / math.pow(10, self.meta_contract.functions.decimals().call()),
            Decimal(20),
        )

        # Check DFI balance
        balance = self.nodes[0].eth_getBalance(self.evm_address)
        assert_equal(balance, int_to_eth_u256(10))

    def transfer_domain_split(self):

        # Rollback
        self.rollback_to(self.block_height)

        # Fund address
        self.fund_address()

        # Split token
        self.split_token(
            self.contract_address_metav1, self.contract_address_metav2, "v1"
        )

        # Generate new destination address
        destination_address = self.nodes[0].getnewaddress("", "legacy")

        # Transfer out METAv1 tokens
        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.evm_address,
                        "amount": "20@META/v1",
                        "domain": 3,
                    },
                    "dst": {
                        "address": destination_address,
                        "amount": "20@META/v1",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        # Check META balance
        assert_equal(
            self.nodes[0].getaccount(destination_address)[0], "40.00000000@META"
        )

        # Check minted balance
        assert_equal(
            self.nodes[0].gettoken("META")[f"{self.meta_final_id}"]["minted"],
            Decimal(2000.00000000),
        )

    def transfer_domain_multiple_split(self):

        # Rollback
        self.rollback_to(self.block_height)

        # Fund address
        self.fund_address()

        # Split token
        self.split_token(
            self.contract_address_metav1, self.contract_address_metav2, "v1"
        )

        # Split token twice
        self.split_token(
            self.contract_address_metav2, self.contract_address_metav3, "v2"
        )

        # Generate new destination address
        destination_address = self.nodes[0].getnewaddress("", "legacy")

        # Transfer out METAv1 tokens
        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.evm_address,
                        "amount": "20@META/v1",
                        "domain": 3,
                    },
                    "dst": {
                        "address": destination_address,
                        "amount": "20@META/v1",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        # Check META balance
        assert_equal(
            self.nodes[0].getaccount(destination_address)[0], "80.00000000@META"
        )

        # Check minted balance
        assert_equal(
            self.nodes[0].gettoken("META")[f"{self.meta_final_id}"]["minted"],
            Decimal(4000.00000000),
        )

    def intrinsic_token_split(self, amount):

        # Rollback
        self.rollback_to(self.block_height)

        # Fund address
        self.fund_address()

        # Split token
        self.split_token(
            self.contract_address_metav1, self.contract_address_metav2, "v1"
        )

        # Create the amount to approve
        amount_to_approve = Web3.to_wei(amount, "ether")
        # Create the amount to approve
        amount_to_receive = Web3.to_wei(amount * self.splitMultiplier, "ether")

        # Construct the approve transaction
        approve_txn = self.meta_contract.functions.approve(
            self.v2_address, amount_to_approve
        ).build_transaction(
            {
                "from": self.evm_address,
                "nonce": self.nodes[0].eth_getTransactionCount(self.evm_address),
            }
        )

        # Sign the transaction
        signed_txn = self.nodes[0].w3.eth.account.sign_transaction(
            approve_txn, self.evm_privkey
        )

        # Send the signed transaction
        self.nodes[0].w3.eth.send_raw_transaction(signed_txn.rawTransaction)
        self.nodes[0].generate(1)

        # Check allowance
        allowance = self.meta_contract.functions.allowance(
            self.evm_address, self.v2_address
        ).call()
        assert_equal(Web3.from_wei(allowance, "ether"), amount)

        # Call depositAndSplitTokens
        deposit_txn = self.intrinsics_contract.functions.depositAndSplitTokens(
            self.contract_address_metav1, amount_to_approve
        ).build_transaction(
            {
                "from": self.evm_address,
                "nonce": self.nodes[0].eth_getTransactionCount(self.evm_address),
            }
        )

        # Sign the transaction
        signed_txn = self.nodes[0].w3.eth.account.sign_transaction(
            deposit_txn, self.evm_privkey
        )

        balanceBefore = self.meta_contract.functions.balanceOf(self.evm_address).call()
        totalSupplyBefore = self.meta_contract.functions.totalSupply().call()

        # Send the signed transaction
        self.nodes[0].w3.eth.send_raw_transaction(signed_txn.rawTransaction)
        self.nodes[0].generate(1)

        # Check METAv1 balance on sender
        # METAv1 balance of sender should be reduced by the approved amount
        assert_equal(
            self.meta_contract.functions.balanceOf(self.evm_address).call(),
            balanceBefore - amount_to_approve,
        )

        # Check METAv1 balance on contract.
        # Funds should not be sent to DFIIntrinsic contract
        # assert_equal(
        #     self.meta_contract.functions.balanceOf(self.v2_address).call(),
        #     Decimal(0),
        # )

        # Check METAv1 totalSupply.
        # Funds should be reduced by the amount splitted
        totalSupplyAfter = self.meta_contract.functions.totalSupply().call()
        assert_equal(totalSupplyAfter, totalSupplyBefore - amount_to_approve)

        # Check META balance on sender
        assert_equal(
            self.meta_contract_new.functions.balanceOf(self.evm_address).call(),
            amount_to_receive,
        )

        # Check META balance on sender
        assert_equal(
            self.meta_contract_new.functions.balanceOf(self.v2_address).call(),
            Decimal(0),
        )

    def split_token(
        self, old_contract_address, new_contract_address, symbol_postfix="v1"
    ):

        # Get META ID
        meta_id = list(self.nodes[0].gettoken("META").keys())[0]

        # Get amount
        balance_after_split = (
            Decimal(self.nodes[0].getaccount(self.address)[1].split("@")[0]) * 2
        )

        # Token split
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    f"v0/oracles/splits/{self.nodes[0].getblockcount() + 2}": f"{meta_id}/{self.splitMultiplier}"
                }
            }
        )
        self.nodes[0].generate(2)

        # Confirm META doubled
        assert_equal(
            self.nodes[0].getaccount(self.address)[1], f"{balance_after_split}@META"
        )

        # Check old contract has been renamed
        self.meta_contract = self.nodes[0].w3.eth.contract(
            address=old_contract_address, abi=self.dst20_abi
        )
        assert_equal(self.meta_contract.functions.name().call(), "Meta")
        assert_equal(
            self.meta_contract.functions.symbol().call(), f"META/{symbol_postfix}"
        )

        # Check new contract has been created
        self.meta_contract_new = self.nodes[0].w3.eth.contract(
            address=new_contract_address, abi=self.dst20_abi
        )
        assert_equal(self.meta_contract_new.functions.name().call(), "Meta")
        assert_equal(self.meta_contract_new.functions.symbol().call(), "META")

        # Check token renamed
        assert_equal(
            self.nodes[0].gettoken(meta_id)[f"{meta_id}"]["symbol"],
            f"META/{symbol_postfix}",
        )

        # Check new token name
        self.meta_final_id = list(self.nodes[0].gettoken("META").keys())[0]
        assert_equal(
            self.nodes[0].gettoken(self.meta_final_id)[f"{self.meta_final_id}"][
                "symbol"
            ],
            "META",
        )


if __name__ == "__main__":
    EVMTokenSplitTest().main()
