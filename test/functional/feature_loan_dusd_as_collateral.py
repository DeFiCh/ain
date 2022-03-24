#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - DUSD as collateral."""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time

class LoanDUSDCollateralTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanninghillheight=200', '-fortcanningroadheight=215', '-jellyfish_regtest=1']]

    def run_test(self):
        self.nodes[0].generate(120)

        mn_address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        symbolDFI = "DFI"
        symbolBTC = "BTC"
        symbolDUSD = "DUSD"

        self.nodes[0].setloantoken({
            'symbol': "DUSD",
            'name': "DUSD",
            'fixedIntervalPriceId': "DUSD/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        self.nodes[0].createtoken({
            "symbol": symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": mn_address
        })
        self.nodes[0].generate(1)

        idDUSD = list(self.nodes[0].gettoken(symbolDUSD).keys())[0]

        # Mint DUSD
        self.nodes[0].minttokens("100000@DUSD")
        self.nodes[0].minttokens("100000@BTC")
        self.nodes[0].generate(1)

        # Create DFI tokens
        self.nodes[0].utxostoaccount({mn_address: "100000@" + symbolDFI})
        self.nodes[0].generate(1)

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": symbolDFI,
            "tokenB": symbolDUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": mn_address
        })
        self.nodes[0].createpoolpair({
            "tokenA": symbolDFI,
            "tokenB": symbolBTC,
            "commission": 0,
            "status": True,
            "ownerAddress": mn_address
        })
        self.nodes[0].generate(1)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity({
            mn_address: [
                '10000@' + symbolDFI,
                '8000@' + symbolDUSD]
            }, mn_address)
        self.nodes[0].addpoolliquidity({
            mn_address: [
                '10000@' + symbolDFI,
                '8000@' + symbolBTC]
            }, mn_address)
        self.nodes[0].generate(1)

        # Set up Oracles
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        price_feed = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"}
        ]

        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        oracle_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
            {"currency": "USD", "tokenAmount": "1@BTC"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': symbolDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"
                                    })

        self.nodes[0].setcollateraltoken({
                                    'token': symbolBTC,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "BTC/USD"
                                    })

        token_factor_dusd = 0.99
        activate = self.nodes[0].getblockcount() + 50
        self.nodes[0].setcollateraltoken({
                                    'token': symbolDUSD,
                                    'factor': token_factor_dusd,
                                    'fixedIntervalPriceId': "DUSD/USD",
                                    'activateAfterBlock': activate
                                    })
        self.nodes[0].generate(1)

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 1, 'LOAN001')
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address with DUSD and DFI
        collateral = 2000
        loan_dusd = 1000
        self.nodes[0].accounttoaccount(mn_address, {vault_address: str(collateral) + "@" + symbolDUSD})
        self.nodes[0].accounttoaccount(mn_address, {vault_address: str(collateral) + "@" + symbolDFI})
        self.nodes[0].accounttoaccount(mn_address, {vault_address: str(collateral) + "@" + symbolBTC})
        self.nodes[0].generate(1)

        # DUSD is not active as a collateral token yet
        assert_raises_rpc_error(-32600, "Collateral token with id (1) does not exist!", self.nodes[0].deposittovault, vault_id, vault_address, str(collateral) + "@" + symbolDUSD)

        # Activates DUSD as collateral token
        self.nodes[0].generate(activate - self.nodes[0].getblockcount())

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, str(collateral) + "@" + symbolDUSD)
        self.nodes[0].deposittovault(vault_id, vault_address, str(collateral) + "@" + symbolDFI)
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(vault_id)
        assert("DUSD" in vault['collateralAmounts'][1])
        assert_equal(vault['collateralValue'], collateral * token_factor_dusd + collateral)

        # Move to FortCanningHill fork
        self.nodes[0].generate(200 - self.nodes[0].getblockcount())

        # Enable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idDUSD + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": str(loan_dusd) + "@" + symbolDUSD })
        self.nodes[0].generate(1)

        # Loan value loan amount + interest
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanValue'], Decimal(loan_dusd) + vault['interestValue'])

        # Swap DUSD from loan to DFI
        self.nodes[0].poolswap({
            "from": vault_address,
            "tokenFrom": symbolDUSD,
            "amountFrom": loan_dusd,
            "to": vault_address,
            "tokenTo": symbolDFI
        })
        self.nodes[0].generate(1)

        # Payback loan with DFI
        [dfi_balance, _] = self.nodes[0].getaccount(vault_address)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': vault_id,
            'from': vault_address,
            'amounts': [dfi_balance + '@' + symbolDFI]})
        self.nodes[0].generate(1)

        # Loan should be paid back in full
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanValue'], Decimal('0'))

        assert_equal(vault['collateralAmounts'], ['2000.00000000@DFI', '2000.00000000@DUSD'])

        # Withdraw DFI and use DUSD as sole collateral
        self.nodes[0].withdrawfromvault(vault_id, vault_address, '2000.00000000@DFI')
        self.nodes[0].generate(1)

        # Try to take DUSD loan with DUSD as sole collateral
        try:
            self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": str(loan_dusd) + "@" + symbolDUSD })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("At least 50% of the minimum required collateral must be in DFI" in errorString)

        self.nodes[0].generate(215 - self.nodes[0].getblockcount()) # move to fortcanningroad height

        # Take DUSD loan with DUSD as sole collateral
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": str(loan_dusd) + "@" + symbolDUSD })
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['collateralAmounts'], ['2000.00000000@DUSD'])

        # Try to take DUSD loan with DUSD less than 50% of total collateralized loan value
        # This tests for collateral factor
        assert_raises_rpc_error(-32600, "Vault does not have enough collateralization ratio defined by loan scheme - 149 < 150", self.nodes[0].takeloan, { "vaultId": vault_id, "amounts": "333@" + symbolDUSD })

        # Set DUSD collateral factor back to 1
        self.nodes[0].setcollateraltoken({
                                    'token': symbolDUSD,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DUSD/USD"
                                    })
        self.nodes[0].generate(10)

        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": "333@" + symbolDUSD })
        self.nodes[0].generate(1)

if __name__ == '__main__':
    LoanDUSDCollateralTest().main()
