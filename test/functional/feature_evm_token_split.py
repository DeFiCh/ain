#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test DFI intrinsics contract"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, get_solc_artifact_path, int_to_eth_u256

from decimal import Decimal, ROUND_DOWN
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

        # Store block height for rollback
        self.block_height = self.nodes[0].getblockcount()

        # Split token via transfer domain
        self.transfer_domain_split()

        # Split token multiple times via transfer domain
        self.transfer_domain_multiple_split()

        # Split tokens via intrinsics contract
        self.intrinsic_token_split(20, 2)

        # Partial split tokens via intrinsics contract
        self.intrinsic_token_split(10, 2)

        # Partial split tokens via intrinsics contract
        self.multiple_intrinsic_token_split()

        # Merge tokens via intrinsics contract
        self.intrinsic_token_merge(20, -2)

        # Merge small amount via intrinsics contract
        self.intrinsic_token_merge(0.00000002, -2)

        # Merge Satoshi amount via intrinsics contract
        self.intrinsic_token_merge(0.00000001, -2)

        # Merge tokens via intrinsics contract
        self.intrinsic_token_merge(20, -3)

        # Merge small amount via intrinsics contract
        self.intrinsic_token_merge(0.00000003, -3)

        # Merge small amount via intrinsics contract
        self.intrinsic_token_merge(0.00000002, -3)

        # Merge Satoshi amount via intrinsics contract
        self.intrinsic_token_merge(0.00000001, -3)

        # Merge tokens via transfer domain
        self.transfer_domain_merge(20, -2)

        # Merge small amount via transfer domain
        self.transfer_domain_merge(0.00000002, -2)

        # Merge Satoshi amount via transfer domain
        self.transfer_domain_merge(0.00000001, -2)

        # Merge tokens via transfer domain
        self.transfer_domain_merge(20, -3)

        # Merge small amount via transfer domain
        self.transfer_domain_merge(0.00000003, -3)

        # Merge small amount via transfer domain
        self.transfer_domain_merge(0.00000002, -3)

        # Merge Satoshi amount via transfer domain
        self.transfer_domain_merge(0.00000001, -3)

        # Fractional split via transferdomain
        self.transfer_domain_split(1.5)

        # Fractional split via intrinsics contract
        self.intrinsic_token_split(20, 1.5)

    def satoshi_limit(self, amount):
        return amount.quantize(Decimal("0.00000001"), rounding=ROUND_DOWN)

    def setup(self):

        # Define address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

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

        self.burn_address = self.nodes[0].w3.to_checksum_address(
            "0x0000000000000000000000000000000000000000"
        )

        self.contract_address_metav1 = self.nodes[0].w3.to_checksum_address(
            "0xff00000000000000000000000000000000000001"
        )

        self.contract_address_metav2 = self.nodes[0].w3.to_checksum_address(
            "0xff00000000000000000000000000000000000002"
        )

        self.contract_address_metav3 = self.nodes[0].w3.to_checksum_address(
            "0xff00000000000000000000000000000000000003"
        )

        # DST20 ABI
        self.dst20_abi = open(
            get_solc_artifact_path("dst20_v1", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        self.dst20_v2_abi = open(
            get_solc_artifact_path("dst20_v2", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        # Check META variables
        self.meta_contract = self.nodes[0].w3.eth.contract(
            address=self.contract_address_metav1, abi=self.dst20_v2_abi
        )
        assert_equal(self.meta_contract.functions.name().call(), "Meta")
        assert_equal(self.meta_contract.functions.symbol().call(), "META")

        # Activate fractional split
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/oracles/splits/fractional_enabled": "true",
                }
            }
        )
        self.nodes[0].generate(1)

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

    def fund_address(self, source, destination, amount=20):

        # Fund address with META
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": source, "amount": f"{amount}@META", "domain": 2},
                    "dst": {
                        "address": destination,
                        "amount": f"{amount}@META",
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
                    "src": {"address": source, "amount": "1@DFI", "domain": 2},
                    "dst": {
                        "address": destination,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        # Check META balance
        meta_contract = self.meta_contract
        assert_equal(
            meta_contract.functions.balanceOf(destination).call()
            / math.pow(10, meta_contract.functions.decimals().call()),
            Decimal(amount),
        )

        # Check DFI balance
        balance = self.nodes[0].eth_getBalance(destination)
        assert_equal(balance, int_to_eth_u256(1))

    def transfer_domain_split(self, multiplier=2):

        # Rollback
        self.rollback_to(self.block_height)

        # Fund address
        self.fund_address(self.address, self.evm_address)

        # Split token
        self.split_token(
            self.contract_address_metav1, self.contract_address_metav2, "v1", multiplier
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

        # Create Decimal multiplier
        decimal_multiplier = Decimal(str(multiplier))

        # Check META balance
        assert_equal(
            Decimal(self.nodes[0].getaccount(destination_address)[0].split("@")[0]),
            Decimal("20.00000000") * decimal_multiplier,
        )

        # Check minted balance
        assert_equal(
            self.nodes[0].gettoken("META")[f"{self.meta_final_id}"]["minted"],
            Decimal("1000.00000000") * decimal_multiplier,
        )

    def transfer_domain_merge(self, amount, split_multiplier):

        # Rollback
        self.rollback_to(self.block_height)

        # Fund address
        self.fund_address(self.address, self.evm_address, amount)

        # Split token
        self.split_token(
            self.contract_address_metav1,
            self.contract_address_metav2,
            "v1",
            split_multiplier,
        )

        # Generate new destination address
        destination_address = self.nodes[0].getnewaddress("", "legacy")

        # Transfer out METAv1 tokens
        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.evm_address,
                        "amount": f"{amount}@META/v1",
                        "domain": 3,
                    },
                    "dst": {
                        "address": destination_address,
                        "amount": f"{amount}@META/v1",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        # Work out expected balance
        expected_balance = self.satoshi_limit(
            Decimal(str(amount)) / abs(split_multiplier)
        )

        # Check META balance
        if expected_balance > 0:
            assert_equal(
                Decimal(self.nodes[0].getaccount(destination_address)[0].split("@")[0]),
                expected_balance,
            )
        else:
            assert_equal(self.nodes[0].getaccount(destination_address), [])

        # Calculate minted balance
        minted_balance = (Decimal(1000.00000000) - Decimal(str(amount))) / abs(
            split_multiplier
        )

        # Limit to 8 decimal places
        balance_after_split = self.satoshi_limit(minted_balance)

        # Check minted balance
        assert_equal(
            self.nodes[0].gettoken("META")[f"{self.meta_final_id}"]["minted"],
            balance_after_split + expected_balance,
        )

    def transfer_domain_multiple_split(self):

        # Rollback
        self.rollback_to(self.block_height)

        # Fund address
        self.fund_address(self.address, self.evm_address)

        # Split token
        self.split_token(self.contract_address_metav1, self.contract_address_metav2)

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

    def intrinsic_token_split(self, amount, split_multiplier):

        # Rollback
        self.rollback_to(self.block_height)

        # Fund address
        fund_amount = 20
        self.fund_address(self.address, self.evm_address, fund_amount)

        # Split token
        self.split_token(
            self.contract_address_metav1,
            self.contract_address_metav2,
            "v1",
            split_multiplier,
        )

        # Get values from before transfer out
        result = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        dvm_current = result["v0/live/economy/transferdomain/dvm/1/current"]
        evm_current = result["v0/live/economy/transferdomain/evm/1/current"]

        # Execute and test split transaction
        self.execute_split_transaction(
            self.contract_address_metav1,
            self.contract_address_metav2,
            amount,
            split_multiplier,
        )

        # Get values from after transfer out
        result = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]

        # Verify transfer out stats
        assert_equal(result["v0/live/economy/transferdomain/evm-dvm/1/total"], amount)
        assert_equal(
            result["v0/live/economy/transferdomain/dvm/1/current"],
            Decimal(dvm_current) + amount,
        )
        assert_equal(result["v0/live/economy/transferdomain/dvm/1/in"], amount)
        assert_equal(
            result["v0/live/economy/transferdomain/evm/1/current"],
            Decimal(evm_current) - amount,
        )
        assert_equal(result["v0/live/economy/transferdomain/evm/1/out"], amount)

        # Verify transfer in stats
        in_amount = amount * abs(split_multiplier)
        assert_equal(
            result["v0/live/economy/transferdomain/dvm-evm/2/total"], in_amount
        )
        assert_equal(
            result["v0/live/economy/transferdomain/dvm/2/current"], Decimal(-in_amount)
        )
        assert_equal(result["v0/live/economy/transferdomain/dvm/2/out"], in_amount)
        assert_equal(result["v0/live/economy/transferdomain/evm/2/current"], in_amount)
        assert_equal(result["v0/live/economy/transferdomain/evm/2/in"], in_amount)

        # Check minted balance
        decimal_multiplier = Decimal(str(split_multiplier))
        assert_equal(
            self.nodes[0].gettoken("META")[f"{self.meta_final_id}"]["minted"],
            ((Decimal(1000.00000000) - Decimal(fund_amount)) * decimal_multiplier)
            + (amount * decimal_multiplier),
        )

        self.execute_split_transaction_at_highest_level(
            self.contract_address_metav2, amount
        )

    def intrinsic_token_merge(self, amount, split_multiplier):

        # Rollback
        self.rollback_to(self.block_height)

        # Fund address
        self.fund_address(self.address, self.evm_address, amount)

        # Split token
        self.split_token(
            self.contract_address_metav1,
            self.contract_address_metav2,
            "v1",
            split_multiplier,
        )

        # Execute and test split transaction
        split_amount = self.execute_split_transaction(
            self.contract_address_metav1,
            self.contract_address_metav2,
            amount,
            split_multiplier,
        )

        # Calculate minted balance
        minted_balance = (Decimal(1000.00000000) - Decimal(str(amount))) / abs(
            split_multiplier
        )

        # Limit to 8 decimal places
        balance_after_split = self.satoshi_limit(minted_balance)

        # Check minted balance
        assert_equal(
            self.nodes[0].gettoken("META")[f"{self.meta_final_id}"]["minted"],
            balance_after_split + Web3.from_wei(split_amount, "ether"),
        )

    def multiple_intrinsic_token_split(self):

        # Set multiplier
        split_multiplier = 2

        # Rollback
        self.rollback_to(self.block_height)

        # Fund address
        self.fund_address(self.address, self.evm_address)

        # Split token
        self.split_token(self.contract_address_metav1, self.contract_address_metav2)

        # Split token twice
        self.split_token(
            self.contract_address_metav2, self.contract_address_metav3, "v2"
        )

        # Set amount to split
        amount = 20

        # Execute first split transaction
        self.execute_split_transaction(
            self.contract_address_metav1,
            self.contract_address_metav2,
            amount,
            split_multiplier,
        )

        # Set amount to split
        amount = 40

        # Execute second split transaction
        self.execute_split_transaction(
            self.contract_address_metav2,
            self.contract_address_metav3,
            amount,
            split_multiplier,
        )

        # Check minted balances
        assert_equal(
            self.nodes[0].gettoken("META/v1")["1"]["minted"],
            Decimal(0.00000000),
        )
        assert_equal(
            self.nodes[0].gettoken("META/v2")["2"]["minted"],
            Decimal(0.00000000),
        )
        assert_equal(
            self.nodes[0].gettoken("META")["3"]["minted"],
            Decimal("3920.00000000") + (amount * split_multiplier),
        )

    def split_token(
        self,
        old_contract_address,
        new_contract_address,
        symbol_postfix="v1",
        split_multiplier=2,
    ):

        # Get META ID
        meta_id = list(self.nodes[0].gettoken("META").keys())[0]

        # Get amount
        if split_multiplier > 0:
            balance_after_split = Decimal(
                self.nodes[0].getaccount(self.address)[1].split("@")[0]
            ) * Decimal(str(split_multiplier))
        else:
            balance_after_split = Decimal(
                self.nodes[0].getaccount(self.address)[1].split("@")[0]
            ) / abs(split_multiplier)

        # Limit to 8 decimal places
        balance_after_split = self.satoshi_limit(balance_after_split)

        # Token split
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    f"v0/oracles/splits/{self.nodes[0].getblockcount() + 2}": f"{meta_id}/{split_multiplier}"
                }
            }
        )
        self.nodes[0].generate(2)

        # Confirm META doubled
        assert_equal(
            self.nodes[0].getaccount(self.address)[1], f"{balance_after_split}@META"
        )

        # Check old contract has been renamed
        meta_contract = self.nodes[0].w3.eth.contract(
            address=old_contract_address, abi=self.dst20_abi
        )
        assert_equal(meta_contract.functions.name().call(), "Meta")
        assert_equal(meta_contract.functions.symbol().call(), f"META/{symbol_postfix}")

        # Check new contract has been created
        meta_contract_new = self.nodes[0].w3.eth.contract(
            address=new_contract_address, abi=self.dst20_abi
        )
        assert_equal(meta_contract_new.functions.name().call(), "Meta")
        assert_equal(meta_contract_new.functions.symbol().call(), "META")

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

    def execute_split_transaction(
        self, source_contract, destination_contract, amount=20, split_multiplier=2
    ):

        # Create the amount to approve
        amount_to_send = Web3.to_wei(amount, "ether")

        # Create the amount to approve
        if split_multiplier > 0:
            amount_to_receive = Web3.to_wei(amount * split_multiplier, "ether")
        else:
            amount_to_receive = Decimal(str(amount)) / abs(split_multiplier)
            amount_to_receive = self.satoshi_limit(Decimal(str(amount_to_receive)))
            amount_to_receive = Web3.to_wei(amount_to_receive, "ether")

        # If less than 1 Sat then amount will be 0
        if amount_to_receive < Web3.to_wei(10, "gwei"):
            amount_to_receive = 0

        # Get old contract
        meta_contract = self.nodes[0].w3.eth.contract(
            address=source_contract, abi=self.dst20_v2_abi
        )

        totalSupplyBefore = meta_contract.functions.totalSupply().call()
        balance_before = meta_contract.functions.balanceOf(self.evm_address).call()

        # Call upgradeToken
        deposit_txn = meta_contract.functions.upgradeToken(
            amount_to_send
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
        self.nodes[0].w3.eth.send_raw_transaction(signed_txn.rawTransaction)
        self.nodes[0].generate(1)

        tx_receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(signed_txn.hash)

        events = meta_contract.events.UpgradeResult().process_log(
            list(tx_receipt["logs"])[0]
        )

        assert_equal(events["event"], "UpgradeResult")
        assert_equal(events["args"]["newTokenContractAddress"], destination_contract)
        assert_equal(events["args"]["newAmount"], amount_to_receive)

        # Check source contract balance on sender
        # Source contract balance of sender should be reduced by the approved amount
        assert_equal(
            meta_contract.functions.balanceOf(self.evm_address).call(),
            balance_before - amount_to_send,
        )

        # Check source contract totalSupply.
        # Funds should be reduced by the amount splitted
        totalSupplyAfter = meta_contract.functions.totalSupply().call()
        assert_equal(totalSupplyAfter, totalSupplyBefore - amount_to_send)

        # Get new contract
        meta_contract_new = self.nodes[0].w3.eth.contract(
            address=destination_contract, abi=self.dst20_v2_abi
        )

        # Check transfer from sender to burn address
        events = meta_contract_new.events.Transfer().process_log(
            list(tx_receipt["logs"])[1]
        )

        assert_equal(events["event"], "Transfer")
        assert_equal(events["args"]["to"], self.burn_address)
        assert_equal(events["args"]["value"], amount_to_send)

        # Check transfer token logs on new contract
        events = meta_contract_new.events.Transfer().process_log(
            list(tx_receipt["logs"])[2]
        )
        assert_equal(events["event"], "Transfer")
        assert_equal(events["args"]["to"], self.evm_address)
        assert_equal(events["args"]["value"], amount_to_receive)

        # Check META balance on sender
        assert_equal(
            meta_contract_new.functions.balanceOf(self.evm_address).call(),
            amount_to_receive,
        )

        return amount_to_receive

    def execute_split_transaction_at_highest_level(self, source_contract, amount=20):
        meta_contract = self.nodes[0].w3.eth.contract(
            address=source_contract, abi=self.dst20_v2_abi
        )

        amount_to_send = Web3.to_wei(amount, "ether")

        # Check that new contract split does not work
        deposit_txn = meta_contract.functions.upgradeToken(
            amount_to_send
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
        self.nodes[0].w3.eth.send_raw_transaction(signed_txn.rawTransaction)
        self.nodes[0].generate(1)

        tx_receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(signed_txn.hash)

        events = meta_contract.events.UpgradeResult().process_log(
            list(tx_receipt["logs"])[0]
        )

        assert_equal(events["event"], "UpgradeResult")
        assert_equal(events["args"]["newTokenContractAddress"], source_contract)
        assert_equal(events["args"]["newAmount"], amount_to_send)


if __name__ == "__main__":
    EVMTokenSplitTest().main()
