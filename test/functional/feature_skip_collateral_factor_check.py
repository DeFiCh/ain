#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test - Skip Collateral Factor """

from test_framework.test_framework import DefiTestFramework


class SkipCollateralFactorTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-eunosheight=1',
             '-txindex=1', '-fortcanningheight=1', '-fortcanningroadheight=1',
             '-fortcanninghillheight=1', '-fortcanningcrunchheight=1', '-fortcanninggreatworldheight=1',
             '-fortcanningepilogueheight=200', '-regtest-skip-loan-collateral-validation', '-jellyfish_regtest=1']]

    def run_test(self):
        # Generate chain
        self.nodes[0].generate(120)

        # Create loan tokens
        self.symbolDUSD = "DUSD"
        self.nodes[0].setloantoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            'fixedIntervalPriceId': f"{self.symbolDUSD}/USD",
            'mintable': True,
            'interest': -1
        })
        self.nodes[0].generate(1)

        # Store DUSD ID
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]

        # Move to fork
        self.nodes[0].generate(200 - self.nodes[0].getblockcount())

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 1, 'LOAN001')
        self.nodes[0].generate(1)

        # Should not throw error
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/token/{self.idDUSD}/loan_collateral_factor': '1.50'}})


if __name__ == '__main__':
    SkipCollateralFactorTest().main()
