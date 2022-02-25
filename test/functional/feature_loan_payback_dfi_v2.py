#!/usr/bin/env python3
# Copyright (c) 2016-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - payback loan dfi."""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.authproxy import JSONRPCException

import calendar
import time
from decimal import Decimal, ROUND_UP


class PaybackDFILoanTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-eunosheight=50',
                '-fortcanningheight=50', '-fortcanninghillheight=50', '-fortcanningroadheight=50', '-simulatemainnet', '-txindex=1', '-jellyfish_regtest=1']
        ]
        self.symbolDFI = "DFI"
        self.symbolBTC = "BTC"
        self.symboldUSD = "DUSD"
        self.symbolTSLA = "TSLA"

    def create_tokens(self):
        self.nodes[0].createtoken({
            "symbol": self.symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.account0
        })
        self.nodes[0].generate(1)
        self.idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]
        self.idBTC = list(self.nodes[0].gettoken(self.symbolBTC).keys())[0]
        self.nodes[0].utxostoaccount({self.account0: "2000000@" + self.symbolDFI})


    def setup_oracles(self):
        self.oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"},
            {"currency": "USD", "token": "TSLA"}
        ]
        self.oracle_id1 = self.nodes[0].appointoracle(
            self.oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "10@TSLA"},
            {"currency": "USD", "tokenAmount": "10@DFI"},
            {"currency": "USD", "tokenAmount": "10@BTC"}
        ]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(120) # make prices active

    def setup_loan_tokens(self):
        self.nodes[0].setcollateraltoken({
            'token': self.idDFI,
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"
        })
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': self.idBTC,
            'factor': 1,
            'fixedIntervalPriceId': "BTC/USD"})
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolTSLA,
            'name': "Tesla stock token",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 1
        })
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symboldUSD,
            'name': "DUSD stable token",
            'fixedIntervalPriceId': "DUSD/USD",
                                    'mintable': True,
                                    'interest': 1
        })
        self.nodes[0].generate(1)

        self.nodes[0].minttokens("1000000000@DUSD")
        self.iddUSD = list(self.nodes[0].gettoken(self.symboldUSD).keys())[0]
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

    def setup_poolpairs(self):
        poolOwner = self.nodes[0].getnewaddress("", "legacy")
        # create pool DUSD-DFI
        self.nodes[0].createpoolpair({
            "tokenA": self.iddUSD,
            "tokenB": self.idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        })
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity(
            {self.account0: ["30@" + self.symbolDFI, "300@" + self.symboldUSD]}, self.account0)
        self.nodes[0].generate(1)

    def setup_loanschemes(self):
        self.nodes[0].createloanscheme(150, 5, 'LOAN150')
        self.nodes[0].generate(1)

    def setup(self):
        self.nodes[0].generate(150)
        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.create_tokens()
        self.setup_oracles()
        self.setup_loan_tokens()
        self.setup_poolpairs()
        self.setup_loanschemes()

    def payback_with_BTC(self):
        self.vaultId = self.nodes[0].createvault(self.account0, 'LOAN150')
        self.nodes[0].generate(90)

        self.nodes[0].deposittovault(self.vaultId, self.account0, "400@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': self.vaultId,
            'amounts': "2000@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        # Should not be able to payback loan with BTC
        try:
            self.nodes[0].paybackloan({
                    'vaultId': self.vaultId,
                    'from': self.account0,
                    'amounts': "1@BTC"
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan token with id (1) does not exist!" in errorString)

    def payback_with_DFI_prior_to_atribute_activation(self):
        # Should not be able to payback loan before DFI payback enabled
        try:
            self.nodes[0].paybackloan({
                    'vaultId': self.vaultId,
                    'from': self.account0,
                    'amounts': "1@DFI"
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Payback of DUSD loans with DFI not currently active" in errorString)

    def setgov_attribute_to_false_and_payback(self):
        assert_raises_rpc_error(-5, 'Unrecognised type argument provided, valid types are: params, poolpairs, token,',
                                self.nodes[0].setgov, {"ATTRIBUTES":{'v0/live/economy/dfi_payback_tokens':'1'}})

        # Disable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.iddUSD + '/payback_dfi':'false'}})
        self.nodes[0].generate(1)

        # Should not be able to payback loan before DFI payback enabled
        assert_raises_rpc_error(-32600, "Payback of DUSD loans with DFI not currently active", self.nodes[0].paybackloan, {
            'vaultId': self.vaultId,
            'from': self.account0,
            'amounts': "1@DFI"
        })

    def setgov_attribute_to_true_and_payback(self):
        # Enable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.iddUSD + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        vaultBefore = self.nodes[0].getvault(self.vaultId)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        # Partial loan payback in DFI
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId,
            'from': self.account0,
            'amounts': "1@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('0.01000000'))
        assert_equal(info['dfipaybacktokens'], ['9.90000000@DUSD'])

        vaultAfter = self.nodes[0].getvault(self.vaultId)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')

        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - (10 * Decimal('0.99'))))

    def test_5pct_penalty(self):
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.iddUSD + '/payback_dfi_fee_pct':'0.05'}})
        self.nodes[0].generate(1)

        vaultBefore = self.nodes[0].getvault(self.vaultId)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        # Partial loan payback in DFI
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId,
            'from': self.account0,
            'amounts': "1@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('0.06000000'))
        assert_equal(info['dfipaybacktokens'], ['19.40000000@DUSD'])

        vaultAfter = self.nodes[0].getvault(self.vaultId)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')

        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - (10 * Decimal('0.95'))))

    def overpay_loan_in_DFI(self):
        vaultBefore = self.nodes[0].getvault(self.vaultId)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.account0)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId,
            'from': self.account0,
            'amounts': "250@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('10.48421412'))
        assert_equal(info['dfipaybacktokens'], ['2000.00068271@DUSD'])

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/dfi_payback_tokens'], ['10.48421412@DFI', '2000.00068271@DUSD'])

        vaultAfter = self.nodes[0].getvault(self.vaultId)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.account0)[0].split('@')

        assert_equal(len(vaultAfter['loanAmounts']), 0)
        assert_equal(len(vaultAfter['interestAmounts']), 0)
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), (Decimal(amountBefore) / Decimal('9.5')).quantize(Decimal('1E-8'), rounding=ROUND_UP))

    def take_new_loan_payback_exact_amount_in_DFI(self):
        self.nodes[0].takeloan({
            'vaultId': self.vaultId,
            'amounts': "2000@" + self.symboldUSD
        })
        self.nodes[0].generate(10)

        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.account0)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId,
            'from': self.account0,
            'amounts': "210.52871906@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('21.01053591'))
        assert_equal(info['dfipaybacktokens'], ['4000.00182427@DUSD'])

        vaultAfter = self.nodes[0].getvault(self.vaultId)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.account0)[0].split('@')

        assert_equal(len(vaultAfter['loanAmounts']), 0)
        assert_equal(len(vaultAfter['interestAmounts']), 0)
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('210.52643596'))

    def new_vault_payback_TSLA_loan_with_DFI(self):
        # Payback of loan token other than DUSD
        self.vaultId2 = self.nodes[0].createvault(self.account0, 'LOAN150')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId2, self.account0, "100@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': self.vaultId2,
            'amounts': "10@" + self.symbolTSLA
        })
        self.nodes[0].generate(1)

        #Should not be able to payback loan token other than DUSD with DFI
        assert_raises_rpc_error(-32600, "There is no loan on token (DUSD) in this vault!", self.nodes[0].paybackloan, {
            'vaultId': self.vaultId2,
            'from': self.account0,
            'amounts': "10@DFI"
        })
        # Should not be able to payback loan before DFI payback enabled
        assert_raises_rpc_error(-32600, "Payback of TSLA loans with DFI not currently active", self.nodes[0].paybackloan, {
            'vaultId': self.vaultId2,
            'from': self.account0,
            'loans': [{
                'dToken': self.idTSLA,
                'amounts': "1@DFI"
            }]
        })

    def setgov_enable_dfi_payback_and_dfi_fee_pct(self):
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.idTSLA + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.idTSLA + '/payback_dfi_fee_pct':'0.01'}})
        self.nodes[0].generate(1)

    def payback_TSLA_with_1_dfi(self):
        vaultBefore = self.nodes[0].getvault(self.vaultId2)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.account0)[0].split('@')

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.account0,
            'loans': [{
                'dToken': self.idTSLA,
                'amounts': "1@DFI"
            }]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId2)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.account0)[0].split('@')

        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - Decimal('0.99')))
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('1'))

    def take_dUSD_loan_and_payback_with_1_dfi(self):
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.account0)[0].split('@')
        self.nodes[0].takeloan({
            'vaultId': self.vaultId2,
            'amounts': "100@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.account0,
            'loans': [{
                'dToken': self.iddUSD,
                'amounts': "1@DFI"
            }]
        })
        self.nodes[0].generate(1)

        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.account0)[0].split('@')
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('1'))

    def payback_TSLA_and_dUSD_with_1_dfi(self):
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.account0)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.account0,
            'loans': [
                {
                    'dToken': self.idTSLA,
                    'amounts': "1@DFI"
                },
                {
                    'dToken': self.iddUSD,
                    'amounts': "1@DFI"
                },
            ]
        })
        self.nodes[0].generate(1)

        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.account0)[0].split('@')
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('2'))

    def payback_TSLA_with_10_dfi(self):
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.account0)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.account0,
            'loans': [{
                'dToken': self.idTSLA,
                'amounts': "10@DFI"
            }]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId2)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.account0)[0].split('@')

        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('8.10101387'))
        assert_equal(len(vaultAfter['loanAmounts']), 1)
        assert_equal(len(vaultAfter['interestAmounts']), 1)

    def payback_dUSD_with_dfi(self):
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.account0)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.account0,
            'loans': [{
                'dToken': self.iddUSD,
                'amounts': "8.53684423@DFI"
            }]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId2)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.account0)[0].split('@')

        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('8.52631791'))
        assert_equal(len(vaultAfter['loanAmounts']), 0)
        assert_equal(len(vaultAfter['interestAmounts']), 0)

    def run_test(self):
        self.setup()

        self.payback_with_BTC()
        self.payback_with_DFI_prior_to_atribute_activation()
        self.setgov_attribute_to_false_and_payback()
        self.setgov_attribute_to_true_and_payback()
        self.test_5pct_penalty()
        self.overpay_loan_in_DFI()
        self.take_new_loan_payback_exact_amount_in_DFI()
        self.new_vault_payback_TSLA_loan_with_DFI()

        self.setgov_enable_dfi_payback_and_dfi_fee_pct()

        self.payback_TSLA_with_1_dfi()
        self.take_dUSD_loan_and_payback_with_1_dfi()
        self.payback_TSLA_and_dUSD_with_1_dfi()
        self.payback_TSLA_with_10_dfi()
        self.payback_dUSD_with_dfi()

if __name__ == '__main__':
    PaybackDFILoanTest().main()
