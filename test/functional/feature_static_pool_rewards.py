#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test static pool rewards"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal

from decimal import Decimal
import time


class TokenFractionalSplitTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.df24height = 150
        self.extra_args = [
            [
                "-jellyfish_regtest=1",
                "-subsidytest=1",
                "-txnotokens=0",
                "-amkheight=1",
                "-bayfrontheight=1",
                "-bayfrontgardensheight=1",
                "-eunosheight=1",
                "-fortcanningheight=1",
                "-fortcanningmuseumheight=1",
                "-fortcanninghillheight=1",
                "-fortcanningroadheight=1",
                "-fortcanningcrunchheight=1",
                "-fortcanningspringheight=1",
                "-fortcanninggreatworldheight=1",
                "-grandcentralheight=1",
                "-grandcentralepilogueheight=1",
                "-metachainheight=105",
                "-df23height=110",
                f"-df24height={self.df24height}",
            ]
        ]

    def run_test(self):

        # Setup test
        self.setup_tests()

        # Test new pool reward
        self.static_reward_calculation()

        # Test reward when LP_SPLITS zeroed
        self.lp_split_zero_reward()

        # Add liquidity before LP_SPLITS
        self.liquidity_before_setting_splits()

        # Add liquidity after LP_SPLITS
        self.liquidity_after_setting_splits()

        # Check loan token reward
        self.static_loan_reward_calculation()

    def setup_tests(self):

        # Set up test tokens
        self.setup_test_tokens()

        # Set up pool pair
        self.setup_poolpair()

        # Save start block
        self.start_block = self.nodes[0].getblockcount()

    def setup_poolpair(self):

        # Enable mint tokens to address
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/feature/mint-tokens-to-address": "true"}}
        )
        self.nodes[0].generate(1)

        # Fund address for pool
        self.nodes[0].utxostoaccount({self.address: f"1000@{self.symbolDFI}"})
        self.nodes[0].utxostoaccount({self.alt_address: f"1000@{self.symbolDFI}"})
        self.nodes[0].minttokens([f"1000@{self.idBTC}"])
        self.nodes[0].minttokens([f"1000@{self.idLTC}"])
        self.nodes[0].minttokens([f"1000@{self.idTSLA}"], [], self.alt_address)
        self.nodes[0].generate(1)

        # Create pool symbol
        btc_pool_symbol = self.symbolBTC + "-" + self.symbolDFI
        ltc_pool_symbol = self.symbolLTC + "-" + self.symbolDFI
        tsla_pool_symbol = self.symbolTSLA + "-" + self.symbolDFI

        # Create pool pairs
        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolBTC,
                "tokenB": self.symbolDFI,
                "status": True,
                "ownerAddress": self.address,
                "symbol": btc_pool_symbol,
            }
        )
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolLTC,
                "tokenB": self.symbolDFI,
                "status": True,
                "ownerAddress": self.address,
                "symbol": ltc_pool_symbol,
            }
        )
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolTSLA,
                "tokenB": self.symbolDFI,
                "status": True,
                "ownerAddress": self.address,
                "symbol": self.symbolTSLA + "-" + self.symbolDFI,
            }
        )
        self.nodes[0].generate(1)

        # Get pool pair IDs
        btc_pool_id = list(self.nodes[0].getpoolpair(btc_pool_symbol).keys())[0]
        self.ltc_pool_id = list(self.nodes[0].getpoolpair(ltc_pool_symbol).keys())[0]
        tsla_pool_id = list(self.nodes[0].getpoolpair(tsla_pool_symbol).keys())[0]

        # Set pool pair splits
        self.nodes[0].setgov({"LP_SPLITS": {btc_pool_id: 1}})
        self.nodes[0].setgov({"LP_LOAN_TOKEN_SPLITS": {tsla_pool_id: 1}})
        self.nodes[0].generate(1)

        # Fund pools
        self.nodes[0].addpoolliquidity(
            {self.address: [f"1000@{self.symbolBTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].addpoolliquidity(
            {self.alt_address: [f"1000@{self.symbolTSLA}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].generate(1)

    def setup_test_tokens(self):
        self.nodes[0].generate(110)

        # Symbols
        self.symbolBTC = "BTC"
        self.symbolLTC = "LTC"
        self.symbolDFI = "DFI"
        self.symbolTSLA = "TSLA"

        # Store addresses
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.alt_address = self.nodes[0].getnewaddress("", "legacy")

        # Create tokens
        self.nodes[0].createtoken(
            {
                "symbol": self.symbolBTC,
                "name": self.symbolBTC,
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)

        self.nodes[0].createtoken(
            {
                "symbol": self.symbolLTC,
                "name": self.symbolLTC,
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolTSLA},
            {"currency": "USD", "token": self.symbolDFI},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTSLA}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(11)

        # Create loan token
        self.nodes[0].setloantoken(
            {
                "symbol": self.symbolTSLA,
                "name": self.symbolTSLA,
                "fixedIntervalPriceId": f"{self.symbolTSLA}/USD",
                "mintable": True,
                "interest": 0,
            }
        )
        self.nodes[0].generate(1)

        # Store token IDs
        self.idBTC = list(self.nodes[0].gettoken(self.symbolBTC).keys())[0]
        self.idLTC = list(self.nodes[0].gettoken(self.symbolLTC).keys())[0]
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

    def static_reward_calculation(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Get initial balance
        start_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])

        # Calculate pre-fork reward
        self.pre_fork_reward = end_balance - start_balance

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Get initial balance
        start_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])

        # Calculate post-fork reward
        post_fork_reward = end_balance - start_balance

        # Check rewards are the same
        assert_equal(self.pre_fork_reward, post_fork_reward)

    def lp_split_zero_reward(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Set pool pair splits to different pool
        self.nodes[0].setgov({"LP_SPLITS": {self.ltc_pool_id: 1}})
        self.nodes[0].generate(1)

        # Get initial balance
        start_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])

        # Check balances are the same
        assert_equal(start_balance, end_balance)

    def liquidity_before_setting_splits(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Fund pool
        self.nodes[0].addpoolliquidity(
            {self.address: [f"1000@{self.symbolLTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].generate(1)

        # Set pool pair splits to different pool and calculate owner rewards
        self.nodes[0].setgov({"LP_SPLITS": {self.ltc_pool_id: 1}})
        self.nodes[0].accounttoaccount(
            self.address, {self.alt_address: f"1@{self.symbolDFI}"}
        )
        self.nodes[0].generate(1)

        # Get initial balance
        start_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])

        # Calculate new pool reward
        new_pool_reward = end_balance - start_balance

        # Check rewards matches previous pre-fork reward. Different pool but same liquidty and reward share
        assert_equal(self.pre_fork_reward, new_pool_reward)

    def liquidity_after_setting_splits(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Set pool pair splits to different pool
        self.nodes[0].setgov({"LP_SPLITS": {self.ltc_pool_id: 1}})
        self.nodes[0].generate(10)

        # Fund pool and calculate owner rewards
        self.nodes[0].addpoolliquidity(
            {self.address: [f"1000@{self.symbolLTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].accounttoaccount(
            self.address, {self.alt_address: f"1@{self.symbolDFI}"}
        )
        self.nodes[0].generate(1)

        # Get initial balance
        start_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])

        # Calculate new pool reward
        new_pool_reward = end_balance - start_balance

        # Check rewards matches previous pre-fork reward. Different pool but same liquidty and reward share
        assert_equal(self.pre_fork_reward, new_pool_reward)

    def static_loan_reward_calculation(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Get initial balance
        start_balance = Decimal(
            self.nodes[0].getaccount(self.alt_address)[0].split("@")[0]
        )
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(
            self.nodes[0].getaccount(self.alt_address)[0].split("@")[0]
        )

        # Calculate pre-fork reward
        pre_fork_reward = end_balance - start_balance

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Get initial balance
        start_balance = Decimal(
            self.nodes[0].getaccount(self.alt_address)[0].split("@")[0]
        )
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(
            self.nodes[0].getaccount(self.alt_address)[0].split("@")[0]
        )

        # Calculate post-fork reward
        post_fork_reward = end_balance - start_balance

        # Check rewards are the same
        assert_equal(pre_fork_reward, post_fork_reward)


if __name__ == "__main__":
    TokenFractionalSplitTest().main()
