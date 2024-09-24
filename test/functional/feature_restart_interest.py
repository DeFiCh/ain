#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test interest on dtoken restart"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
import time


class RestartInterestTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.df24height = 110
        self.extra_args = [
            DefiTestFramework.fork_params_till(21)
            + [
                "-txnotokens=0",
                "-subsidytest=1",
                "-regtest-minttoken-simulate-mainnet=1",
                "-jellyfish_regtest=1",
                "-negativeinterest=1",
                "-metachainheight=105",
                "-df23height=110",
                "-df24height={self.df24height}",
            ],
        ]

    def run_test(self):

        # Set up
        self.setup()

        # Test future swap refund on restart
        self.future_swap_refund()

        # Check restart on liquidated vault
        self.vault_liquidation()

        # Check restart skips on locked token
        self.skip_restart_on_lock()

        # Check restart skips on pool disabled
        self.skip_restart_on_pool_disabled()

        # Check minimal balances after restart
        self.minimal_balances_after_restart()

        # Interest paid by collateral.
        self.interest_paid_by_collateral()

        # Interest paid by balance.
        self.interest_paid_by_balance()

        # Interest paid by balance and collateral.
        self.interest_paid_by_balance_and_collateral()

        # Negative interest negated from loan amount.
        self.negative_interest_negated_from_loan()

    def setup(self):

        # Get masternode address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Setup Oracles
        self.setup_test_oracles()

        # Create tokens for tests
        self.setup_test_tokens()

        # Setup pools
        self.setup_test_pools()

        # Store rollback block
        self.start_block = self.nodes[0].getblockcount()

    def setup_test_tokens(self):

        # Create loan token
        self.nodes[0].setloantoken(
            {
                "symbol": self.symbolMETA,
                "name": self.symbolMETA,
                "fixedIntervalPriceId": f"{self.symbolMETA}/USD",
                "isDAT": True,
                "interest": 0,
            }
        )

        self.nodes[0].setloantoken(
            {
                "symbol": self.symbolDUSD,
                "name": self.symbolDUSD,
                "fixedIntervalPriceId": f"{self.symbolDUSD}/USD",
                "mintable": True,
                "interest": 0,
            }
        )
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken(
            {"token": self.symbolDFI, "factor": 1, "fixedIntervalPriceId": "DFI/USD"}
        )
        self.nodes[0].setcollateraltoken(
            {"token": self.symbolDUSD, "factor": 1, "fixedIntervalPriceId": "DUSD/USD"}
        )
        self.nodes[0].generate(1)

        # Store DUSD ID
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]

        # Mint DUSD
        self.nodes[0].minttokens(f"100000@{self.symbolDUSD}")
        self.nodes[0].minttokens(f"100000@{self.symbolMETA}")
        self.nodes[0].generate(1)

        # Create DFI tokens
        self.nodes[0].utxostoaccount({self.address: "100000@" + self.symbolDFI})
        self.nodes[0].generate(1)

    def setup_test_oracles(self):

        # Generate chain
        self.nodes[0].generate(110)

        # Token symbols
        self.symbolDFI = "DFI"
        self.symbolDUSD = "DUSD"
        self.symbolMETA = "META"

        # Create Oracle address
        oracle_address = self.nodes[0].getnewaddress("", "legacy")

        # Define price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolDFI},
            {"currency": "USD", "token": self.symbolMETA},
        ]

        # Appoint Oracle
        self.oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolMETA}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 5, "LOAN001")
        self.nodes[0].generate(1)

    def setup_test_pools(self):

        # Create pool pair
        self.nodes[0].createpoolpair(
            {
                "tokenA": self.symbolDFI,
                "tokenB": self.symbolDUSD,
                "commission": 0,
                "status": True,
                "ownerAddress": self.address,
                "pairSymbol": "DFI-DUSD",
            }
        )
        self.nodes[0].generate(1)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity(
            {self.address: [f"10000@{self.symbolDFI}", f"10000@{self.symbolDUSD}"]},
            self.address,
        )
        self.nodes[0].generate(1)

    def execute_restart(self):

        # Get block height
        block_height = self.nodes[0].getblockcount()

        # Set dToken restart and move to execution block
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f"v0/params/dtoken_restart/{block_height + 2}": "0.9"}}
        )
        self.nodes[0].generate(2)

    def future_swap_refund(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Set all futures attributes
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2203/reward_pct": "0.05",
                    "v0/params/dfip2203/block_period": "25",
                }
            }
        )
        self.nodes[0].generate(1)

        # Fully enable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/params/dfip2203/active": "true"}})
        self.nodes[0].generate(1)

        # Balance before restart
        result = self.nodes[0].getaccount(self.address)
        assert_equal(result[1], f"100000.00000000@{self.symbolMETA}")
        assert_equal(result[2], f"90000.00000000@{self.symbolDUSD}")

        # Create future swaps
        self.nodes[0].futureswap(self.address, f"1@{self.symbolMETA}")
        self.nodes[0].futureswap(self.address, f"1@{self.symbolDUSD}", self.symbolMETA)
        self.nodes[0].generate(1)

        # Execute dtoken restart
        self.execute_restart()

        # Balance after restart. Amount refunded and 90% locked.
        result = self.nodes[0].getaccount(self.address)
        assert_equal(result[1], f"10000.00000000@{self.symbolMETA}")
        assert_equal(result[2], f"9000.00000000@{self.symbolDUSD}")

    def minimal_balances_after_restart(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Create addresses
        address_1_sat = self.nodes[0].getnewaddress("", "legacy")
        address_9_sat = self.nodes[0].getnewaddress("", "legacy")
        address_10_sat = self.nodes[0].getnewaddress("", "legacy")
        address_99_sat = self.nodes[0].getnewaddress("", "legacy")
        address_100_sat = self.nodes[0].getnewaddress("", "legacy")
        address_101_sat = self.nodes[0].getnewaddress("", "legacy")
        address_coin_1_sat = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(
            self.address, {address_1_sat: f"0.00000001@{self.symbolDUSD}"}
        )
        self.nodes[0].accounttoaccount(
            self.address, {address_9_sat: f"0.00000009@{self.symbolDUSD}"}
        )
        self.nodes[0].accounttoaccount(
            self.address, {address_10_sat: f"0.00000010@{self.symbolDUSD}"}
        )
        self.nodes[0].accounttoaccount(
            self.address, {address_99_sat: f"0.00000099@{self.symbolDUSD}"}
        )
        self.nodes[0].accounttoaccount(
            self.address, {address_100_sat: f"0.00000100@{self.symbolDUSD}"}
        )
        self.nodes[0].accounttoaccount(
            self.address, {address_101_sat: f"0.00000101@{self.symbolDUSD}"}
        )
        self.nodes[0].accounttoaccount(
            self.address, {address_coin_1_sat: f"1.00000001@{self.symbolDUSD}"}
        )
        self.nodes[0].generate(1)

        # Execute dtoken restart
        self.execute_restart()

        # Check balances
        assert_equal(self.nodes[0].getaccount(address_1_sat), [])
        assert_equal(self.nodes[0].getaccount(address_9_sat), [])
        assert_equal(
            self.nodes[0].getaccount(address_10_sat), [f"0.00000001@{self.symbolDUSD}"]
        )
        assert_equal(
            self.nodes[0].getaccount(address_99_sat), [f"0.00000009@{self.symbolDUSD}"]
        )
        assert_equal(
            self.nodes[0].getaccount(address_100_sat), [f"0.00000010@{self.symbolDUSD}"]
        )
        assert_equal(
            self.nodes[0].getaccount(address_101_sat), [f"0.00000010@{self.symbolDUSD}"]
        )
        assert_equal(
            self.nodes[0].getaccount(address_coin_1_sat),
            [f"0.10000000@{self.symbolDUSD}"],
        )

    def interest_paid_by_collateral(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Create vault
        vault_address = self.nodes[0].getnewaddress("", "legacy")
        vault_id = self.nodes[0].createvault(vault_address, "LOAN001")
        self.nodes[0].generate(1)

        # Deposit DFI to vault
        self.nodes[0].deposittovault(vault_id, self.address, f"200@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan(
            {"vaultId": vault_id, "amounts": f"100@{self.symbolDUSD}"}
        )
        self.nodes[0].generate(1)

        # Execute dtoken restart
        self.execute_restart()

        # Check vault
        result = self.nodes[0].getvault(vault_id)
        assert_equal(result["loanAmounts"], [])
        assert_equal(  # 200 - Two blocks interest
            result["collateralAmounts"], [f"199.99980974@{self.symbolDFI}"]
        )
        assert_equal(result["interestAmounts"], [])

        # Check balance fully used to  pay back loan
        assert_equal(self.nodes[0].getaccount(vault_address), [])

        # Check interest zeroed
        result = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(result["interestToHeight"], "0.000000000000000000000000")
        assert_equal(result["interestPerBlock"], "0.000000000000000000000000")

    def skip_restart_on_lock(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Set lock
        self.nodes[0].setgov({"ATTRIBUTES": {f"v0/locks/token/{self.idDUSD}": "true"}})
        self.nodes[0].generate(1)

        # Check lock
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes[f"v0/locks/token/{self.idDUSD}"], "true")

        # Calculate restart height
        restart_height = self.nodes[0].getblockcount() + 2

        # Execute dtoken restart
        self.execute_restart()

        # Check we are at restart height
        assert_equal(self.nodes[0].getblockcount(), restart_height)

        # Check restart not executed
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert "v0/live/economy/token_lock_ratio" not in attributes

    def skip_restart_on_pool_disabled(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Disable pool
        self.nodes[0].updatepoolpair({"pool": "DFI-DUSD", "status": False})
        self.nodes[0].generate(1)

        # Calculate restart height
        restart_height = self.nodes[0].getblockcount() + 2

        # Execute dtoken restart
        self.execute_restart()

        # Check we are at restart height
        assert_equal(self.nodes[0].getblockcount(), restart_height)

        # Check restart not executed
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert "v0/live/economy/token_lock_ratio" not in attributes

    def vault_liquidation(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Create vault
        vault_address = self.nodes[0].getnewaddress("", "legacy")
        vault_id = self.nodes[0].createvault(vault_address, "LOAN001")
        self.nodes[0].generate(1)

        # Deposit DFI to vault
        self.nodes[0].deposittovault(vault_id, self.address, f"150@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan(
            {"vaultId": vault_id, "amounts": f"100@{self.symbolDUSD}"}
        )
        self.nodes[0].generate(1)

        # Set Oracle prices to trigger liquidation
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"0.9@{self.symbolDFI}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(7)

        # Check vault in liquidation
        result = self.nodes[0].getvault(vault_id)
        assert_equal(result["state"], "inLiquidation")

        # Place bid on auction
        self.nodes[0].placeauctionbid(
            vault_id, 0, self.address, f"110@{self.symbolDUSD}"
        )
        self.nodes[0].generate(1)

        # check balance before
        assert_equal(
            self.nodes[0].getaccount(self.address)[1],
            f"89890.00000000@{self.symbolDUSD}",
        )
        assert_equal(
            self.nodes[0].getaccount(vault_address),
            [f"100.00000000@{self.symbolDUSD}"],
        )

        # Execute dtoken restart
        self.execute_restart()

        # Verify that auction bid has been refunded and DUSD locked afterwards
        assert_equal(
            self.nodes[0].getaccount(self.address)[1],
            f"9000.00000000@{self.symbolDUSD}",
        )

        # DUSD is used to pay back loan
        assert_equal(self.nodes[0].getaccount(vault_address), [])

        # Check auctions are not cleared
        assert_equal(self.nodes[0].listauctions(), [])

        # Check vault
        result = self.nodes[0].getvault(vault_id)
        assert_equal(result["loanAmounts"], [])
        assert_equal(
            result["collateralAmounts"], [f"149.99933408@{self.symbolDFI}"]
        )  # after paying back with account, interest remains which is paid back with collateral
        assert_equal(result["interestAmounts"], [])

    def interest_paid_by_balance(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Create vault
        vault_address = self.nodes[0].getnewaddress("", "legacy")
        vault_id = self.nodes[0].createvault(vault_address, "LOAN001")
        self.nodes[0].generate(1)

        # Deposit DFI to vault
        self.nodes[0].deposittovault(vault_id, self.address, f"200@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Send funds to cover interest
        self.nodes[0].accounttoaccount(
            self.address, {vault_address: f"0.00019026@{self.symbolDUSD}"}
        )
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan(
            {"vaultId": vault_id, "amounts": f"100@{self.symbolDUSD}"}
        )
        self.nodes[0].generate(1)

        # Execute dtoken restart
        self.execute_restart()

        # Check vault
        result = self.nodes[0].getvault(vault_id)
        assert_equal(result["loanAmounts"], [])
        assert_equal(result["collateralAmounts"], [f"200.00000000@{self.symbolDFI}"])
        assert_equal(result["interestAmounts"], [])

        # Check balance fully used to  pay back loan
        assert_equal(self.nodes[0].getaccount(vault_address), [])

    def interest_paid_by_balance_and_collateral(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Create vault
        vault_address = self.nodes[0].getnewaddress("", "legacy")
        vault_id = self.nodes[0].createvault(vault_address, "LOAN001")
        self.nodes[0].generate(1)

        # Deposit DFI to vault
        self.nodes[0].deposittovault(vault_id, self.address, f"200@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Send funds to cover one block's interest
        self.nodes[0].accounttoaccount(
            self.address, {vault_address: f"0.00009513@{self.symbolDUSD}"}
        )
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan(
            {"vaultId": vault_id, "amounts": f"100@{self.symbolDUSD}"}
        )
        self.nodes[0].generate(1)

        # Execute dtoken restart
        self.execute_restart()

        # Check vault
        result = self.nodes[0].getvault(vault_id)
        assert_equal(result["loanAmounts"], [])
        assert_equal(result["collateralAmounts"], [f"199.99990487@{self.symbolDFI}"])
        assert_equal(result["interestAmounts"], [])

        # Check balance fully used to  pay back loan
        assert_equal(self.nodes[0].getaccount(vault_address), [])

    def negative_interest_negated_from_loan(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Set negative interest rate. Works out to overall -5% after scheme interest.
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f"v0/token/{self.idDUSD}/loan_minting_interest": "-10"}}
        )
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress("", "legacy")
        vault_id = self.nodes[0].createvault(vault_address, "LOAN001")
        self.nodes[0].generate(1)

        # Deposit DFI to vault
        self.nodes[0].deposittovault(vault_id, self.address, f"200@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan(
            {"vaultId": vault_id, "amounts": f"100@{self.symbolDUSD}"}
        )
        self.nodes[0].generate(1)

        # Execute dtoken restart
        self.execute_restart()

        # Check vault
        result = self.nodes[0].getvault(vault_id)
        assert_equal(result["loanAmounts"], [])
        assert_equal(result["collateralAmounts"], [f"200.00000000@{self.symbolDFI}"])
        assert_equal(result["interestAmounts"], [])

        # Check balance leaves amount negated by negative interest
        assert_equal(
            self.nodes[0].getaccount(vault_address), [f"0.00001902@{self.symbolDUSD}"]
        )

        # Check interest zeroed
        result = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(result["interestToHeight"], "0.000000000000000000000000")
        assert_equal(result["interestPerBlock"], "0.000000000000000000000000")


if __name__ == "__main__":
    RestartInterestTest().main()
