#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test negative interest."""

from test_framework.test_framework import DefiTestFramework
from decimal import Decimal

from test_framework.util import assert_equal
import time
import calendar

class NegativeInterestTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningcrunchheight=1', '-fortcanninggreatworldheight=1', '-fortcanningepilogueheight=1','-regtest-skip-loan-collateral-validation=1', '-simulatemainnet=1', '-negativeinterest=1']]

    def run_test(self):
        # Create tokens for tests
        self.setup_test_tokens()

        # Setup Oracles
        self.setup_test_oracles()

        # Setup pools
        self.setup_test_pools()

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
            {"currency": "USD", "tokenAmount": f"10@{self.symbolDFI}"},
        ]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle, timestamp, oracle_prices)
        self.nodes[0].generate(1)

        self.oracle_address2 = self.nodes[0].getnewaddress("", "legacy")
        self.oracle_id2 = self.nodes[0].appointoracle(self.oracle_address2, price_feed, 10)
        self.nodes[0].generate(1)

        # feed oracle
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, oracle_prices)
        self.nodes[0].generate(120)

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 5, 'LOAN001')
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': self.symbolDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"
                                    })
        self.nodes[0].generate(120)

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

        # Check stored interest is nil
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['interestPerBlock'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Set overall negative interest rate to -5
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-10'}})
        self.nodes[0].generate(1)

        # Check stored interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['interestPerBlock'], '-0.000000047564687975646879')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Check loan interest
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['interestAmounts'], [f'-0.00000004@{self.symbolDUSD}'])

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check IPB doubled and ITH wiped
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['interestPerBlock'], '-0.000000095129374048706240')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Check loan interest
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['interestAmounts'], [f'-0.00000009@{self.symbolDUSD}'])

        [balanceDUSDbefore, _] = self.nodes[0].getaccount(vault_address)[1].split('@')
        vaultBefore = self.nodes[0].getvault(vault_id, True)

        # Payback almost all of the loan amount
        self.nodes[0].paybackloan({
            'vaultId': vault_id,
            'from': vault_address,
            'amounts': f'1.9999@{self.symbolDUSD}'
        })
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(vault_id, True)
        [loanAmountBefore, _] = vaultBefore["loanAmounts"][0].split('@')
        [loanAmount, _] = vault["loanAmounts"][0].split('@')
        [interestAmount, _] = vault["interestAmounts"][0].split('@')
        assert_equal(Decimal(loanAmount), Decimal(loanAmountBefore) - Decimal('1.9999') + Decimal(interestAmount))

        [balanceDUSDafter, _] = self.nodes[0].getaccount(vault_address)[1].split('@')
        assert_equal(Decimal(balanceDUSDafter), Decimal(balanceDUSDbefore) - Decimal('1.9999'))

        # Set negative interest rate very high to speed up negating vault amount
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-8000000'}})
        self.nodes[0].generate(20)

        # Check loan amount and interest fully negated
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanAmounts'], [])
        assert_equal(vault['interestAmounts'], [])

        # Close now empty vault
        self.nodes[0].closevault(vault_id, vault_address)
        self.nodes[0].generate(1)

        # Check attributes. Amount was 0.00000013 before, diff of remaining 9987 Sat loan amount.
        attrs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attrs['v0/live/economy/negative_interest'], [f'0.00010000@{self.symbolDUSD}'])

if __name__ == '__main__':
    NegativeInterestTest().main()
