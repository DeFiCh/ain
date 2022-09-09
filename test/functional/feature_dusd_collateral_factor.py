#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test DUSD collateral factor."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time

class DUSDCollateralFactorTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1', '-fortcanningroadheight=1', '-fortcanninghillheight=1', '-fortcanningcrunchheight=1', '-fortcanninggreatworldheight=1', '-fortcanningepilogueheight=200', '-jellyfish_regtest=1']]

    def run_test(self):
        # Setup
        self.setup()

        # Test setting of DUSD collateral factor
        self.set_collateral_factor()

        # Test new factor when taking DUSD loan
        self.take_dusd_loan()

        # Test with multiple loan tokens
        self.take_multiple_loans()

    def setup(self):
        # Generate chain
        self.nodes[0].generate(120)

        # Get MN address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Token symbols
        self.symbolDFI = "DFI"
        self.symbolDUSD = "DUSD"
        self.symbolTSLA = "TSLA"

        # Create Oracle address
        oracle_address = self.nodes[0].getnewaddress("", "legacy")

        # Define price feeds
        price_feed = [
            {"currency": "USD", "token": f"{self.symbolDFI}"},
            {"currency": "USD", "token": f"{self.symbolTSLA}"},
        ]

        # Appoint Oracle
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTSLA}"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Create loan tokens
        self.nodes[0].setloantoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            'fixedIntervalPriceId': f"{self.symbolDUSD}/USD",
            'mintable': True,
            'interest': -1
        })
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolTSLA,
            'name': self.symbolTSLA,
            'fixedIntervalPriceId': f"{self.symbolTSLA}/USD",
            'mintable': True,
            'interest': -1
        })
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
            'token': self.symbolDFI,
            'factor': 1,
            'fixedIntervalPriceId': f'{self.symbolDFI}/USD'
        })
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': self.symbolDUSD,
            'factor': 1,
            'fixedIntervalPriceId': f'{self.symbolDFI}/USD'
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

    def set_collateral_factor(self):

        # Test setting new token factor before fork
        assert_raises_rpc_error(-32600, "Cannot be set before FortCanningEpilogue", self.nodes[0].setgov, {"ATTRIBUTES":{f'v0/token/{self.idDUSD}/dusd_collateral_factor': '1.49'}})

        # Move to fork
        self.nodes[0].generate(200 - self.nodes[0].getblockcount())

        # Test setting before scheme set
        assert_raises_rpc_error(-32600, "Set loan scheme before setting DUSD collateral factor", self.nodes[0].setgov, {"ATTRIBUTES":{f'v0/token/{self.idDUSD}/dusd_collateral_factor': '1.50'}})

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 1, 'LOAN001')
        self.nodes[0].generate(1)

        # Test setting higher than the lowest scheme rate
        assert_raises_rpc_error(-32600, "Factor cannot be more than or equal to the lowest scheme rate of 1.50000000", self.nodes[0].setgov, {"ATTRIBUTES":{f'v0/token/{self.idDUSD}/dusd_collateral_factor': '1.50'}})

        # Now set new token factor
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/dusd_collateral_factor': '1.49'}})
        self.nodes[0].generate(1)

        # Check results
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/token/1/dusd_collateral_factor'], '1.49')

    def take_dusd_loan(self):

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, self.address, f"1.5@{self.symbolDFI}")
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vault_id, self.address, f"1.5@{self.symbolDUSD}")
        self.nodes[0].generate(1)

        # Take DUSD loan greater than collateral amount
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"2.49@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check that we are on 150% collateral ratio
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['collateralRatio'], 150)
        assert_equal(vault['informativeRatio'], Decimal('150.00000000'))
        assert_equal(vault['collateralValue'], Decimal('3.73500000'))
        assert_equal(vault['loanValue'], Decimal('2.49000000'))

    def take_multiple_loans(self):

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, self.address, f"1.5@{self.symbolDFI}")
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vault_id, self.address, f"1.5@{self.symbolDUSD}")
        self.nodes[0].generate(1)

        # Take TSLA loan for half the available ratio
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolTSLA}"})
        self.nodes[0].generate(1)

        # Check that we are on 300% collateral ratio
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['collateralRatio'], 300)
        assert_equal(vault['informativeRatio'], Decimal('300.00000000'))
        assert_equal(vault['collateralValue'], Decimal('3.00000000'))
        assert_equal(vault['loanValue'], Decimal('1.00000000'))

        # Take a DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check that vault still has capacity to take more loans
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanValue'], Decimal('2.00000000'))

        # Collateral value
        #
        # TSLA backed by 50% DUSD at 1x and 50% DFI at 1x
        # (0.5 * 1.5 DUSD * 1) + (0.5 * 1.5 DFI * 1) = 1.5
        #
        # DUSD backed by 50% DUSD at 1.49x and 50% DFI at 1x
        # (0.5 * 1.5 DUSD * 1.49) + (0.5 * 1.5 DFI * 1) = 1.8675
        #
        # Added together this amounts to:
        # 1.5 + 1.8675 = 3.3675
        assert_equal(vault['collateralValue'], Decimal('3.36750000'))

        # Collateral ratio
        #
        # Collateral divided into loan amount
        # 3.3675 / 2 * 100 = 168.375
        assert_equal(vault['informativeRatio'], Decimal('168.37500000'))
        assert_equal(vault['collateralRatio'], 168)

if __name__ == '__main__':
    DUSDCollateralFactorTest().main()

