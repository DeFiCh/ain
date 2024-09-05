#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test interest on dtoken restart"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
import time


class RestartInterestTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.df24height = 110
        self.extra_args = [
            [
                "-txnotokens=0",
                "-subsidytest=1",
                "-regtest-minttoken-simulate-mainnet=1",
                "-jellyfish_regtest=1",
                "-negativeinterest=1",
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
                "-df24height={self.df24height}",
            ],
        ]

    def run_test(self):

        # Set up
        self.setup()

        # Basic test. Single vault with no interest.
        self.basic_restart()

    def setup(self):

        # Get masternode address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Create tokens for tests
        self.setup_test_tokens()

        # Setup Oracles
        self.setup_test_oracles()

        # Setup pools
        self.setup_test_pools()

        # Store rollback block
        self.rollback_block = self.nodes[0].getblockhash(self.nodes[0].getblockcount())

    def setup_test_tokens(self):

        # Generate chain
        self.nodes[0].generate(110)

        # Token symbols
        self.symbolDFI = "DFI"
        self.symbolDUSD = "DUSD"

        # Create loan token
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

        # Store DUSD ID
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]

        # Mint DUSD
        self.nodes[0].minttokens("100000@DUSD")
        self.nodes[0].generate(1)

        # Create DFI tokens
        self.nodes[0].utxostoaccount({self.address: "100000@" + self.symbolDFI})
        self.nodes[0].generate(1)

    def setup_test_oracles(self):

        # Create Oracle address
        oracle_address = self.nodes[0].getnewaddress("", "legacy")

        # Define price feeds
        price_feed = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"},
        ]

        # Appoint Oracle
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"10@{self.symbolDFI}"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 5, "LOAN001")
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken(
            {"token": self.symbolDFI, "factor": 1, "fixedIntervalPriceId": "DFI/USD"}
        )
        self.nodes[0].setcollateraltoken(
            {"token": self.symbolDUSD, "factor": 1, "fixedIntervalPriceId": "DUSD/USD"}
        )
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
            }
        )
        self.nodes[0].generate(1)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity(
            {self.address: ["10000@" + self.symbolDFI, "10000@" + self.symbolDUSD]},
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

    def basic_restart(self):

        # Rollback
        self.nodes[0].invalidateblock(self.rollback_block)

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

        print(self.nodes[0].getvault(vault_id))
        print(self.nodes[0].getaccount(vault_address))

        # Execute dtoken restart
        self.execute_restart()

        print(self.nodes[0].getvault(vault_id))
        print(self.nodes[0].getaccount(vault_address))


if __name__ == "__main__":
    RestartInterestTest().main()
