#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - DUSD as collateral."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time

class LoanDUSDCollateralTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanninghillheight=200', '-jellyfish_regtest=1']]

    def run_test(self):
        self.nodes[0].generate(120)

        mn_address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        symbol_dfi = "DFI"
        symbol_dusd = "DUSD"

        self.nodes[0].setloantoken({
            'symbol': "DUSD",
            'name': "DUSD",
            'fixedIntervalPriceId': "DUSD/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        id_usd = list(self.nodes[0].gettoken(symbol_dusd).keys())[0]

        # Mint DUSD
        self.nodes[0].minttokens("100000@DUSD")
        self.nodes[0].generate(1)

        # Create DFI tokens
        self.nodes[0].utxostoaccount({mn_address: "100000@" + symbol_dfi})
        self.nodes[0].generate(1)

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": symbol_dfi,
            "tokenB": symbol_dusd,
            "commission": 0,
            "status": True,
            "ownerAddress": mn_address
        })
        self.nodes[0].generate(1)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity({
            mn_address: [
                '10000@' + symbol_dfi,
                '8000@' + symbol_dusd]
            }, mn_address)
        self.nodes[0].generate(1)

        # Set up Oracles
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        price_feed = [
            {"currency": "USD", "token": "DFI"}
        ]

        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        oracle_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': symbol_dfi,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"
                                    })

        token_factor_dusd = 0.99
        activate = self.nodes[0].getblockcount() + 50
        self.nodes[0].setcollateraltoken({
                                    'token': symbol_dusd,
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
        self.nodes[0].accounttoaccount(mn_address, {vault_address: str(collateral) + "@" + symbol_dusd})
        self.nodes[0].accounttoaccount(mn_address, {vault_address: str(collateral) + "@" + symbol_dfi})
        self.nodes[0].generate(1)

        # DUSD is not active as a collateral token yet
        assert_raises_rpc_error(-32600, "Collateral token with id (1) does not exist!", self.nodes[0].deposittovault, vault_id, vault_address, str(collateral) + "@" + symbol_dusd)

        # Activates DUSD as collateral token
        self.nodes[0].generate(activate - self.nodes[0].getblockcount())

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, str(collateral) + "@" + symbol_dusd)
        self.nodes[0].deposittovault(vault_id, vault_address, str(collateral) + "@" + symbol_dfi)
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(vault_id)
        assert("DUSD" in vault['collateralAmounts'][1])
        assert_equal(vault['collateralValue'], collateral * token_factor_dusd + collateral)

        # Move to FortCanningHill fork
        self.nodes[0].generate(200 - self.nodes[0].getblockcount())

        # Enable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + id_usd + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": str(loan_dusd) + "@" + symbol_dusd })
        self.nodes[0].generate(1)

        # Loan value loan amount + interest
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanValue'], Decimal(loan_dusd) + vault['interestValue'])

        # Swap DUSD from loan to DFI
        self.nodes[0].poolswap({
            "from": vault_address,
            "tokenFrom": symbol_dusd,
            "amountFrom": loan_dusd,
            "to": vault_address,
            "tokenTo": symbol_dfi
        })
        self.nodes[0].generate(1)

        # Payback loan with DFI
        [dfi_balance, _] = self.nodes[0].getaccount(vault_address)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': vault_id,
            'from': vault_address,
            'amounts': [dfi_balance + '@' + symbol_dfi]})
        self.nodes[0].generate(1)

        # Loan should be paid back in full
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanValue'], Decimal('0'))

if __name__ == '__main__':
    LoanDUSDCollateralTest().main()
