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
        self.df24height = 250
        self.extra_args = [
            [
                "-jellyfish_regtest=1",
                "-subsidytest=1",
                "-txnotokens=0",
                "-amkheight=1",
                "-bayfrontheight=1",
                "-bayfrontgardensheight=1",
                "-clarkequayheight=1",
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

        # Test new pool reward with single share
        self.static_reward_single()

        # Test new pool reward with multiple share
        self.static_reward_multiple()

        # Test loan token reward
        self.static_loan_single()

        # Test loan token reward
        self.static_loan_multiple()

        # Test custom token reward
        self.static_custom_single()

        # Test custom token reward
        self.static_custom_multiple()

        # Test commission
        self.static_commission_single()

        # Test commission
        self.static_commission_multiple()

        # Test reward when LP_SPLITS zeroed
        self.lp_split_zero_reward()

        # Add liquidity before LP_SPLITS
        self.liquidity_before_setting_splits()

        # Add liquidity after LP_SPLITS
        self.liquidity_after_setting_splits()

    def setup_tests(self):

        # Set up test tokens
        self.setup_test_tokens()

        # Set up pool pair
        self.setup_poolpair()

        # Save start block
        self.start_block = self.nodes[0].getblockcount()

        # Create variable for pre-fork coinbase reward
        self.pre_fork_reward = None

    def setup_poolpair(self):

        # Fund address for pool
        self.nodes[0].utxostoaccount({self.owner_address: f"100000@{self.symbolDFI}"})
        self.nodes[0].minttokens([f"100000@{self.idBTC}"])
        self.nodes[0].minttokens([f"100000@{self.idLTC}"])
        self.nodes[0].minttokens([f"100000@{self.idTSLA}"])
        self.nodes[0].minttokens([f"100000@{self.idETH}"])
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
                "ownerAddress": self.owner_address,
                "commission": 0,
                "symbol": btc_pool_symbol,
            }
        )

        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolLTC,
                "tokenB": self.symbolDFI,
                "status": True,
                "ownerAddress": self.owner_address,
                "commission": 0.01,
                "symbol": ltc_pool_symbol,
            }
        )

        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolTSLA,
                "tokenB": self.symbolDFI,
                "status": True,
                "ownerAddress": self.owner_address,
                "commission": 0,
                "symbol": self.symbolTSLA + "-" + self.symbolDFI,
            }
        )
        self.nodes[0].generate(1)

        # Get pool pair IDs
        self.btc_pool_id = list(self.nodes[0].getpoolpair(btc_pool_symbol).keys())[0]
        self.ltc_pool_id = list(self.nodes[0].getpoolpair(ltc_pool_symbol).keys())[0]
        tsla_pool_id = list(self.nodes[0].getpoolpair(tsla_pool_symbol).keys())[0]

        # Set pool pair splits
        self.nodes[0].setgov({"LP_SPLITS": {self.btc_pool_id: 1}})
        self.nodes[0].setgov({"LP_LOAN_TOKEN_SPLITS": {tsla_pool_id: 1}})
        self.nodes[0].generate(1)

    def setup_test_tokens(self):
        self.nodes[0].generate(110)

        # Symbols
        self.symbolBTC = "BTC"
        self.symbolLTC = "LTC"
        self.symbolDFI = "DFI"
        self.symbolTSLA = "TSLA"
        self.symbolETH = "ETH"

        # Store addresses
        self.owner_address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address = self.nodes[0].getnewaddress("", "legacy")

        # Create tokens
        self.nodes[0].createtoken(
            {
                "symbol": self.symbolBTC,
                "name": self.symbolBTC,
                "isDAT": True,
                "collateralAddress": self.owner_address,
            }
        )

        self.nodes[0].createtoken(
            {
                "symbol": self.symbolLTC,
                "name": self.symbolLTC,
                "isDAT": True,
                "collateralAddress": self.owner_address,
            }
        )

        self.nodes[0].createtoken(
            {
                "symbol": self.symbolETH,
                "name": self.symbolETH,
                "isDAT": True,
                "collateralAddress": self.owner_address,
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
        self.idETH = list(self.nodes[0].gettoken(self.symbolETH).keys())[0]

    def fund_pool_multiple(self, token_a, token_b):

        # Fund pool with multiple addresses
        liquidity_addresses = []
        for i in range(100):
            address = self.nodes[0].getnewaddress()
            amount = 10 + (i * 10)
            self.nodes[0].addpoolliquidity(
                {self.owner_address: [f"{amount}@{token_a}", f"{amount}@{token_b}"]},
                address,
            )
            self.nodes[0].generate(1)
            liquidity_addresses.append(address)

        return liquidity_addresses

    def test_single_liquidity(
        self, token_a, token_b, balance_index=0, custom_token=None
    ):

        # Rollback block
        self.rollback_to(self.start_block)

        # Fund pool
        self.nodes[0].addpoolliquidity(
            {self.owner_address: [f"1000@{token_a}", f"1000@{token_b}"]},
            self.address,
        )
        self.nodes[0].generate(1)

        # Add custom reward
        if custom_token:
            self.nodes[0].updatepoolpair(
                {"pool": self.btc_pool_id, "customRewards": [f"1@{custom_token}"]}
            )
            self.nodes[0].generate(1)

        # Calculate pre-fork reward
        pre_fork_reward = Decimal(
            self.nodes[0].getaccount(self.address)[balance_index].split("@")[0]
        )

        # Save first pre-fork reward
        if self.pre_fork_reward is None:
            self.pre_fork_reward = pre_fork_reward

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Get initial balance
        start_balance = Decimal(
            self.nodes[0].getaccount(self.address)[balance_index].split("@")[0]
        )
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(
            self.nodes[0].getaccount(self.address)[balance_index].split("@")[0]
        )

        # Calculate post-fork reward
        post_fork_reward = end_balance - start_balance

        # Check rewards are the same
        assert_equal(pre_fork_reward, post_fork_reward)

    def test_multiple_liquidity(
        self, token_a, token_b, balance_index=0, custom_token=None
    ):

        # Rollback block
        self.rollback_to(self.start_block)

        # Fund pool with multiple addresses
        liquidity_addresses = self.fund_pool_multiple(token_a, token_b)

        # Add custom reward
        if custom_token:
            self.nodes[0].updatepoolpair(
                {"pool": self.btc_pool_id, "customRewards": [f"1@{custom_token}"]}
            )
            self.nodes[0].generate(1)

        # Get initial balances
        intitial_balances = []
        for address in liquidity_addresses:
            intitial_balances.append(
                Decimal(self.nodes[0].getaccount(address)[balance_index].split("@")[0])
            )
        self.nodes[0].generate(1)

        # Get balances after a block
        end_balances = []
        for address in liquidity_addresses:
            end_balances.append(
                Decimal(self.nodes[0].getaccount(address)[balance_index].split("@")[0])
            )

        # Calculate pre-fork rewards
        pre_fork_rewards = [
            end - start for start, end in zip(intitial_balances, end_balances)
        ]

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Get initial balances
        intitial_balances = []
        for address in liquidity_addresses:
            intitial_balances.append(
                Decimal(self.nodes[0].getaccount(address)[balance_index].split("@")[0])
            )
        self.nodes[0].generate(1)

        # Get balances after a block
        end_balances = []
        for address in liquidity_addresses:
            end_balances.append(
                Decimal(self.nodes[0].getaccount(address)[balance_index].split("@")[0])
            )

        # Calculate post-fork reward
        post_fork_rewards = [
            end - start for start, end in zip(intitial_balances, end_balances)
        ]

        # Check rewards are the same
        assert_equal(pre_fork_rewards, post_fork_rewards)

    def static_reward_single(self):

        # Test single coinbase reward
        self.test_single_liquidity(self.symbolBTC, self.symbolDFI)

    def static_reward_multiple(self):

        # Test multiple coinbase reward
        self.test_multiple_liquidity(self.symbolTSLA, self.symbolDFI)

    def static_loan_single(self):

        # Test single loan reward
        self.test_single_liquidity(self.symbolTSLA, self.symbolDFI)

    def static_loan_multiple(self):

        # Test multiple loan reward
        self.test_multiple_liquidity(self.symbolBTC, self.symbolDFI)

    def static_custom_single(self):

        # Test single custom reward
        self.test_single_liquidity(self.symbolBTC, self.symbolDFI, 1, self.symbolETH)

        # Rollback block
        self.rollback_to(self.start_block)

        # Fund pool
        self.nodes[0].addpoolliquidity(
            {self.owner_address: [f"1000@{self.symbolBTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].generate(1)

        # Add custom reward
        self.nodes[0].updatepoolpair(
            {"pool": self.btc_pool_id, "customRewards": [f"1@{self.symbolETH}"]}
        )
        self.nodes[0].generate(1)

        # Calculate pre-fork reward
        pre_fork_reward = Decimal(
            self.nodes[0].getaccount(self.address)[1].split("@")[0]
        )

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Get initial balance
        start_balance = Decimal(self.nodes[0].getaccount(self.address)[1].split("@")[0])
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(self.nodes[0].getaccount(self.address)[1].split("@")[0])

        # Calculate post-fork reward
        post_fork_reward = end_balance - start_balance

        # Check rewards are the same
        assert_equal(pre_fork_reward, post_fork_reward)

    def static_custom_multiple(self):

        # Test multiple custom reward
        self.test_multiple_liquidity(self.symbolBTC, self.symbolDFI, 1, self.symbolETH)

    def static_commission_single(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Fund pool
        self.nodes[0].addpoolliquidity(
            {self.owner_address: [f"1000@{self.symbolLTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].generate(1)

        # Store rollback block
        rollback_block = self.nodes[0].getblockcount()

        # Swap LTC to DFI
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolLTC,
                "amountFrom": 1,
                "to": self.owner_address,
                "tokenTo": self.symbolDFI,
            }
        )
        self.nodes[0].generate(1)

        # Swap DFI to LTC
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolDFI,
                "amountFrom": 1,
                "to": self.owner_address,
                "tokenTo": self.symbolLTC,
            }
        )
        self.nodes[0].generate(1)

        # Get commission balances
        pre_dfi_balance = Decimal(
            self.nodes[0].getaccount(self.address)[0].split("@")[0]
        )
        pre_ltc_balance = Decimal(
            self.nodes[0].getaccount(self.address)[1].split("@")[0]
        )

        # Rollback swaps
        self.rollback_to(rollback_block)

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Swap LTC to DFI
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolLTC,
                "amountFrom": 1,
                "to": self.owner_address,
                "tokenTo": self.symbolDFI,
            }
        )
        self.nodes[0].generate(1)

        # Swap DFI to LTC
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolDFI,
                "amountFrom": 1,
                "to": self.owner_address,
                "tokenTo": self.symbolLTC,
            }
        )
        self.nodes[0].generate(1)

        # Get commission balances
        post_dfi_balance = Decimal(
            self.nodes[0].getaccount(self.address)[0].split("@")[0]
        )
        post_ltc_balance = Decimal(
            self.nodes[0].getaccount(self.address)[1].split("@")[0]
        )

        # Check commission is the same pre and post fork
        assert_equal(pre_dfi_balance, post_dfi_balance)
        assert_equal(pre_ltc_balance, post_ltc_balance)

    def static_commission_multiple(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Fund pool with multiple addresses
        liquidity_addresses = self.fund_pool_multiple(self.symbolLTC, self.symbolDFI)

        # Store rollback block
        rollback_block = self.nodes[0].getblockcount()

        # Swap LTC to DFI
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolLTC,
                "amountFrom": 1,
                "to": self.owner_address,
                "tokenTo": self.symbolDFI,
            }
        )
        self.nodes[0].generate(1)

        # Swap DFI to LTC
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolDFI,
                "amountFrom": 1,
                "to": self.owner_address,
                "tokenTo": self.symbolLTC,
            }
        )
        self.nodes[0].generate(1)

        # Get commission balances
        pre_dfi_balances = []
        for address in liquidity_addresses:
            pre_dfi_balances.append(
                Decimal(self.nodes[0].getaccount(address)[0].split("@")[0])
            )
        pre_ltc_balances = []
        for address in liquidity_addresses:
            pre_ltc_balances.append(
                Decimal(self.nodes[0].getaccount(address)[1].split("@")[0])
            )

        # Rollback swaps
        self.rollback_to(rollback_block)

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Swap LTC to DFI
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolLTC,
                "amountFrom": 1,
                "to": self.owner_address,
                "tokenTo": self.symbolDFI,
            }
        )
        self.nodes[0].generate(1)

        # Swap DFI to LTC
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolDFI,
                "amountFrom": 1,
                "to": self.owner_address,
                "tokenTo": self.symbolLTC,
            }
        )
        self.nodes[0].generate(1)

        # Get commission balances
        post_dfi_balances = []
        for address in liquidity_addresses:
            post_dfi_balances.append(
                Decimal(self.nodes[0].getaccount(address)[0].split("@")[0])
            )
        post_ltc_balances = []
        for address in liquidity_addresses:
            post_ltc_balances.append(
                Decimal(self.nodes[0].getaccount(address)[1].split("@")[0])
            )

        # Check commission is the same pre and post fork
        assert_equal(pre_dfi_balances, post_dfi_balances)
        assert_equal(pre_ltc_balances, post_ltc_balances)

    def lp_split_zero_reward(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Fund pool
        self.nodes[0].addpoolliquidity(
            {self.owner_address: [f"1000@{self.symbolBTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].generate(1)

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
            {self.owner_address: [f"1000@{self.symbolLTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].generate(1)

        # Set pool pair splits to different pool and calculate owner rewards
        self.nodes[0].setgov({"LP_SPLITS": {self.ltc_pool_id: 1}})
        self.nodes[0].generate(1)

        # Get initial balance
        start_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])
        self.nodes[0].generate(1)

        # Get balance after a block
        end_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])

        # Calculate new pool reward
        new_pool_reward = end_balance - start_balance

        # Add 1 Sat to start balance as new reward is higher precsision over multiple blocks
        old_reward = self.pre_fork_reward + Decimal("0.00000001")

        # Check rewards matches
        assert_equal(old_reward, new_pool_reward)

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
            {self.owner_address: [f"1000@{self.symbolLTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].generate(2)

        # Balance started at zero, new balance is the reward
        new_pool_reward = Decimal(
            self.nodes[0].getaccount(self.address)[0].split("@")[0]
        )

        # Check rewards matches
        assert_equal(self.pre_fork_reward, new_pool_reward)


if __name__ == "__main__":
    TokenFractionalSplitTest().main()
