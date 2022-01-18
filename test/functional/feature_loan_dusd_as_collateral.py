#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - DUSD as collateral."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
import time

class LoanDUSDCollateralTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fortcanningheight=50', '-fortcanninghillheight=500', '-eunosheight=50', '-txindex=1']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI
        print("Generating initial chain...")
        self.nodes[0].generate(120)

        symbolDFI = "DFI"
        symbolDUSD = "DUSD"

        self.nodes[0].setloantoken({
            'symbol': "DUSD",
            'name': "DUSD",
            'fixedIntervalPriceId': "DUSD/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        oracleAddress = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [
            {"currency": "USD", "token": "DFI"}
        ]
        oracle_id1 = self.nodes[0].appointoracle(oracleAddress, price_feeds1, 10)
        self.nodes[0].generate(1)

        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
        ]
        mock_time = int(time.time())
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(oracle_id1, mock_time, oracle1_prices)

        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
                                    'token': symbolDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"
                                    })

        tokenFactorDUSD = 0.9
        activateAfterBlock = self.nodes[0].getblockcount() + 50
        self.nodes[0].setcollateraltoken({
                                    'token': "DUSD",
                                    'factor': tokenFactorDUSD,
                                    'fixedIntervalPriceId': "DUSD/USD",
                                    'activateAfterBlock': activateAfterBlock
                                    })
        self.nodes[0].generate(1)

        self.nodes[0].createloanscheme(200, 1, 'LOAN001')
        self.nodes[0].generate(1)

        vaultAddress = self.nodes[0].getnewaddress('', 'legacy')
        vaultId = self.nodes[0].createvault(vaultAddress, 'LOAN001')
        self.nodes[0].generate(1)

        amountDFI = 500
        amountDUSD = 250

        self.nodes[0].utxostoaccount({vaultAddress: str(amountDFI) + "@" + symbolDFI})
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId, vaultAddress, str(amountDFI) + "@" + symbolDFI)
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({ "vaultId": vaultId, "amounts": str(amountDUSD) + "@" + symbolDUSD })
        self.nodes[0].generate(1)

        # DUSD is not active as a collateral token yet
        assert_raises_rpc_error(-32600, "Collateral token with id (1) does not exist!", self.nodes[0].deposittovault, vaultId, vaultAddress, str(amountDUSD) + "@" + symbolDUSD)

        self.nodes[0].generate(50) # Activates DUSD as collateral token

        # Should be able to deposit DUSD to vault
        self.nodes[0].deposittovault(vaultId, vaultAddress, str(amountDUSD) + "@" + symbolDUSD)
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(vaultId)

        assert("DUSD" in vault['collateralAmounts'][1])
        assert_equal(vault['collateralValue'], amountDUSD * tokenFactorDUSD + amountDFI)

if __name__ == '__main__':
    LoanDUSDCollateralTest().main()
