#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test negative interest."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
import time

class NegativeInterestTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningcrunchheight=1', '-greatworldheight=1', '-jellyfish_regtest=1']]

    def run_test(self):
        # Create tokens for tests
        self.setup_test_tokens()

        # Setup pools
        self.setup_test_pools()

        # Setup Oracles
        self.setup_test_oracles()

        # Test negative interest
        self.test_negative_interest()

    def setup_test_tokens(self):
        # Generate chain
        self.nodes[0].generate(120)

        # Get MN address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Token symbols
        self.symbolDFI = "DFI"
        self.symbolDUSD = "DUSD"

        # Create loan token
        self.nodes[0].setloantoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            'fixedIntervalPriceId': f"{self.symbolDUSD}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        # Store DUSD ID
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]

        # Mint DUSD
        self.nodes[0].minttokens("100000@DUSD")
        self.nodes[0].generate(1)

        # Create DFI tokens
        self.nodes[0].utxostoaccount({self.address: "100000@" + self.symbolDFI})
        self.nodes[0].generate(1)

    def setup_test_pools(self):

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDFI,
            "tokenB": self.symbolDUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.address
        })
        self.nodes[0].generate(1)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity({
            self.address: [
                '10000@' + self.symbolDFI,
                '10000@' + self.symbolDUSD]
            }, self.address)
        self.nodes[0].generate(1)

    def setup_test_oracles(self):

        # Create Oracle address
        oracle_address = self.nodes[0].getnewaddress("", "legacy")

        # Define price feeds
        price_feed = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"}
        ]

        # Appoint Oracle
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': self.symbolDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"
                                    })
        self.nodes[0].generate(1)

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 5, 'LOAN001')
        self.nodes[0].generate(1)

    def test_negative_interest(self):

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"1000@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"10@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Set negative interest rate to cancel out scheme interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-5'}})
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check loan interest
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['interestAmounts'], [f'0.00000000@{self.symbolDUSD}'])

        # Set negative interest rate to go below 0 when combined with scheme interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-10'}})
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check loan interest
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['interestAmounts'], [f'0.00000000@{self.symbolDUSD}'])

if __name__ == '__main__':
    NegativeInterestTest().main()
