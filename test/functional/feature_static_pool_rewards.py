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

SATOSHI = Decimal("0.00000001")


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
                "-bayfrontmarinaheight=1",
                "-bayfrontgardensheight=1",
                "-clarkequayheight=1",
                "-dakotaheight=1",
                "-dakotacrescentheight=1",
                "-eunosheight=1",
                "-eunospayaheight=1",
                "-fortcanningheight=1",
                "-fortcanningmuseumheight=1",
                "-fortcanningparkheight=1",
                "-fortcanninghillheight=1",
                "-fortcanningroadheight=1",
                "-fortcanningcrunchheight=1",
                "-fortcanningspringheight=1",
                "-fortcanninggreatworldheight=1",
                "-fortcanningepilogueheight=1",
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

        # Test account update on same block as poolswap
        self.static_update_same_block()

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
        self, token_a, token_b, extra_sat=False, balance_index=0, custom_token=None
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

        # Store reward amount
        reward_amount = Decimal(
            self.nodes[0].getaccount(self.address)[balance_index].split("@")[0]
        )

        # Save first pre-fork reward
        if self.pre_fork_reward is None:
            self.pre_fork_reward = reward_amount

        # Move to before fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount() - 2)

        # Store account balances
        account_balances = self.nodes[0].getaccount(self.address)
        balance_before = Decimal(account_balances[balance_index].split("@")[0])

        # Move to just before fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount() - 1)

        # Check reward is paid
        balance_current = Decimal(
            self.nodes[0].getaccount(self.address)[balance_index].split("@")[0]
        )
        assert_equal(balance_current, balance_before + reward_amount)

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Check reward is paid
        balance_current = Decimal(
            self.nodes[0].getaccount(self.address)[balance_index].split("@")[0]
        )
        assert_equal(balance_current, balance_before + (reward_amount * 2))
        self.nodes[0].generate(1)

        # Check reward is paid
        balance_current = Decimal(
            self.nodes[0].getaccount(self.address)[balance_index].split("@")[0]
        )
        assert_equal(
            balance_current,
            balance_before + (reward_amount * 3) + (SATOSHI if extra_sat else 0),
        )

        # Get current height
        height = self.nodes[0].getblockcount()

        # Loop over and check account history entries
        for result in self.nodes[0].listaccounthistory(
            self.address, {"limit": self.nodes[0].getblockcount()}
        ):
            if result["type"] != "AddPoolLiquidity":
                if custom_token:
                    if result["type"] == "Pool":
                        assert_equal(result["blockHeight"], height)
                        assert_equal(
                            Decimal(result["amounts"][0].split("@")[0]), reward_amount
                        )
                else:
                    assert_equal(result["blockHeight"], height)
                    assert_equal(
                        Decimal(result["amounts"][0].split("@")[0]), reward_amount
                    )
                height -= 1

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
        for x, y in zip(pre_fork_rewards, post_fork_rewards):
            if x != y:
                assert_equal(x + SATOSHI, y)

    def static_reward_single(self):

        # Test single coinbase reward
        self.test_single_liquidity(self.symbolBTC, self.symbolDFI, True)

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
        self.test_single_liquidity(
            self.symbolBTC, self.symbolDFI, False, 1, self.symbolETH
        )

    def static_custom_multiple(self):

        # Test multiple custom reward
        self.test_multiple_liquidity(self.symbolBTC, self.symbolDFI, 1, self.symbolETH)

    def execute_poolswap(self):
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
        dfi_balance = Decimal(self.nodes[0].getaccount(self.address)[0].split("@")[0])
        ltc_balance = Decimal(self.nodes[0].getaccount(self.address)[1].split("@")[0])

        return dfi_balance, ltc_balance

    def static_update_same_block(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Fund pool
        self.nodes[0].addpoolliquidity(
            {self.owner_address: [f"1000@{self.symbolLTC}", f"1000@{self.symbolDFI}"]},
            self.address,
        )
        self.nodes[0].sendtoaddress(self.address, 1)
        self.nodes[0].accounttoaccount(self.owner_address, {self.address: "1@DFI"})
        self.nodes[0].generate(1)

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Execute swaps and update account via accounttoaccount on same block
        self.nodes[0].accounttoaccount(self.address, {self.owner_address: "1@DFI"})
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolLTC,
                "amountFrom": 0.1,
                "to": self.owner_address,
                "tokenTo": self.symbolDFI,
            }
        )
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolLTC,
                "amountFrom": 0.1,
                "to": self.owner_address,
                "tokenTo": self.symbolDFI,
            }
        )
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolDFI,
                "amountFrom": 0.1,
                "to": self.owner_address,
                "tokenTo": self.symbolLTC,
            }
        )
        self.nodes[0].poolswap(
            {
                "from": self.owner_address,
                "tokenFrom": self.symbolDFI,
                "amountFrom": 0.1,
                "to": self.owner_address,
                "tokenTo": self.symbolLTC,
            }
        )
        self.nodes[0].generate(1)

        # Check balance
        assert_equal(self.nodes[0].getaccount(self.address), ["0.00199999@DFI", "0.00199999@LTC", "999.99999000@LTC-DFI"])    

        # Check account history
        results = []
        for result in self.nodes[0].listaccounthistory(self.address, {"depth": 1}):
            if result["type"] == "Commission":
                results.append(result)
        assert_equal(len(results), 2)
        assert_equal(results[0]["amounts"], ["0.00199999@LTC"])
        assert_equal(results[1]["amounts"], ["0.00199999@DFI"])

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

        # Execute swaps
        pre_dfi_balance, pre_ltc_balance = self.execute_poolswap()

        # Get pre fork history
        self.nodes[0].generate(1)  # Add block to get history
        pre_fork_history = self.nodes[0].listaccounthistory(self.address)

        # Rollback swaps
        self.rollback_to(rollback_block)

        # Move to before fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount() - 2)

        # Check results the same before fork
        pre_two_dfi_balance, pre_two_ltc_balance = self.execute_poolswap()
        assert_equal(pre_dfi_balance, pre_two_dfi_balance)
        assert_equal(pre_ltc_balance, pre_two_ltc_balance)

        # Rollback swaps
        self.rollback_to(rollback_block)

        # Move to before fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount() - 1)

        # Check results the same before fork
        pre_two_dfi_balance, pre_two_ltc_balance = self.execute_poolswap()
        assert_equal(pre_dfi_balance, pre_two_dfi_balance)
        assert_equal(pre_ltc_balance, pre_two_ltc_balance)

        # Rollback swaps
        self.rollback_to(rollback_block)

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Store swap heights to check results
        first_swap_height = self.nodes[0].getblockcount() + 1
        second_swap_height = self.nodes[0].getblockcount() + 2

        # Execute swaps
        post_dfi_balance, post_ltc_balance = self.execute_poolswap()

        # Check commission is the same pre and post fork
        assert_equal(pre_dfi_balance, post_dfi_balance)
        assert_equal(pre_ltc_balance, post_ltc_balance)

        # Get post fork history
        post_fork_history = self.nodes[0].listaccounthistory(self.address)
        assert_equal(post_fork_history[0]["blockHeight"], second_swap_height)
        assert_equal(post_fork_history[0]["type"], pre_fork_history[0]["type"])
        assert_equal(post_fork_history[0]["poolID"], pre_fork_history[0]["poolID"])
        assert_equal(post_fork_history[0]["amounts"], pre_fork_history[0]["amounts"])
        assert_equal(post_fork_history[1]["blockHeight"], first_swap_height)
        assert_equal(post_fork_history[1]["type"], pre_fork_history[1]["type"])
        assert_equal(post_fork_history[1]["poolID"], pre_fork_history[1]["poolID"])
        assert_equal(post_fork_history[1]["amounts"], pre_fork_history[1]["amounts"])

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
        self.nodes[0].generate(1)

        # Balance started at zero, new balance is the reward
        new_pool_reward = Decimal(
            self.nodes[0].getaccount(self.address)[0].split("@")[0]
        )

        # Check rewards matches
        assert_equal(self.pre_fork_reward, new_pool_reward)


if __name__ == "__main__":
    TokenFractionalSplitTest().main()
