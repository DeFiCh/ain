#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
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
             '-fortcanningheight=50', '-fortcanninghillheight=50', '-fortcanningroadheight=196', '-fortcanningspringheight=200', '-debug=loan', '-txindex=1']
        ]

    def run_test(self):
        self.nodes[0].generate(150)

        account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress

        symbolDFI = "DFI"
        symbolBTC = "BTC"
        symboldUSD = "DUSD"
        symbolTSLA = "TSLA"

        self.nodes[0].createtoken({
            "symbol": symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": account0
        })

        self.nodes[0].generate(1)

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]

        self.nodes[0].utxostoaccount({account0: "1000@" + symbolDFI})
        self.nodes[0].minttokens("500@BTC")

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"},
            {"currency": "USD", "token": "TSLA"}
        ]
        oracle_id1 = self.nodes[0].appointoracle(
            oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "10@TSLA"},
            {"currency": "USD", "tokenAmount": "10@DFI"},
            {"currency": "USD", "tokenAmount": "10@BTC"}
        ]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': idDFI,
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"
        })

        self.nodes[0].setcollateraltoken({
            'token': idBTC,
            'factor': 1,
            'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': symbolTSLA,
            'name': "Tesla stock token",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 1
        })

        self.nodes[0].setloantoken({
            'symbol': symboldUSD,
            'name': "DUSD stable token",
            'fixedIntervalPriceId': "DUSD/USD",
                                    'mintable': True,
                                    'interest': 1
        })
        self.nodes[0].generate(1)

        self.nodes[0].createloanscheme(150, 5, 'LOAN150')

        self.nodes[0].generate(5)

        iddUSD = list(self.nodes[0].gettoken(symboldUSD).keys())[0]

        vaultId = self.nodes[0].createvault(account0, 'LOAN150')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId, account0, "400@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "2000@" + symboldUSD
        })
        self.nodes[0].generate(1)

        # Check total DUSD loan amount
        attributes = self.nodes[0].getgov("ATTRIBUTES")['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/loans'], [f'2000.00000000@{symboldUSD}'])

        poolOwner = self.nodes[0].getnewaddress("", "legacy")
        # create pool DUSD-DFI
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        })
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity(
            {account0: ["30@" + symbolDFI, "300@" + symboldUSD]}, account0)
        self.nodes[0].generate(1)

        # Should not be able to payback loan with BTC
        assert_raises_rpc_error(-32600, "Loan token with id (1) does not exist!", self.nodes[0].paybackloan, {
            'vaultId': vaultId,
            'from': account0,
            'amounts': "1@BTC"
        })

        # Should not be able to payback loan before DFI payback enabled
        assert_raises_rpc_error(-32600, "Payback of loan via DFI token is not currently active", self.nodes[0].paybackloan, {
            'vaultId': vaultId,
            'from': account0,
            'amounts': "1@DFI"
        })

        assert_raises_rpc_error(-5, 'Unrecognised type argument provided, valid types are: gov, locks, oracles, params, poolpairs, token,',
                                self.nodes[0].setgov, {"ATTRIBUTES":{'v0/live/economy/dfi_payback_tokens':'1'}})

        # Disable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + iddUSD + '/payback_dfi':'false'}})
        self.nodes[0].generate(1)

        # Should not be able to payback loan before DFI payback enabled
        assert_raises_rpc_error(-32600, "Payback of loan via DFI token is not currently active", self.nodes[0].paybackloan, {
            'vaultId': vaultId,
            'from': account0,
            'amounts': "1@DFI"
        })

        # Enable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + iddUSD + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        vaultBefore = self.nodes[0].getvault(vaultId)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        # Partial loan payback in DFI
        self.nodes[0].paybackloan({
            'vaultId': vaultId,
            'from': account0,
            'amounts': "1@DFI"
        })
        self.nodes[0].generate(1)

        # Check total DUSD reduced by 10 DUSD - interest
        attributes = self.nodes[0].getgov("ATTRIBUTES")['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/loans'], [f'1990.11141553@{symboldUSD}'])

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('0.01000000'))
        assert_equal(info['dfipaybacktokens'], ['9.90000000@DUSD'])

        vaultAfter = self.nodes[0].getvault(vaultId)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')

        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - (10 * Decimal('0.99'))))

        # Test 5% penalty
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + iddUSD + '/payback_dfi_fee_pct':'0.05'}})
        self.nodes[0].generate(1)

        vaultBefore = self.nodes[0].getvault(vaultId)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        # Partial loan payback in DFI
        self.nodes[0].paybackloan({
            'vaultId': vaultId,
            'from': account0,
            'amounts': "1@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('0.06000000'))
        assert_equal(info['dfipaybacktokens'], ['19.40000000@DUSD'])

        vaultAfter = self.nodes[0].getvault(vaultId)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')

        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - (10 * Decimal('0.95'))))

        vaultBefore = vaultAfter
        [balanceDFIBefore, _] = self.nodes[0].getaccount(account0)[0].split('@')
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        # Overpay loan payback in DFI
        self.nodes[0].paybackloan({
            'vaultId': vaultId,
            'from': account0,
            'amounts': "250@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('10.48430642'))
        assert_equal(info['dfipaybacktokens'], ['2000.01822015@DUSD'])

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/dfi_payback_tokens'], ['10.48430642@DFI', '2000.01822015@DUSD'])

        vaultAfter = self.nodes[0].getvault(vaultId)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(account0)[0].split('@')

        assert_equal(len(vaultAfter['loanAmounts']), 0)
        assert_equal(len(vaultAfter['interestAmounts']), 0)
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), (Decimal(amountBefore) / Decimal('9.5')).quantize(Decimal('1E-8'), rounding=ROUND_UP))

        # Exact amount loan payback in DFI

        # take new loan of 2000DUSD
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "2000@" + symboldUSD
        })
        self.nodes[0].generate(10)

        # Check total DUSD loan amount is just the new loan amount
        attributes = self.nodes[0].getgov("ATTRIBUTES")['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/loans'], [f'2000.00000000@{symboldUSD}'])

        vaultBefore = self.nodes[0].getvault(vaultId)
        [balanceDFIBefore, _] = self.nodes[0].getaccount(account0)[0].split('@')

        self.nodes[0].paybackloan({
            'vaultId': vaultId,
            'from': account0,
            'amounts': "210.52871906@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('21.01074237'))
        assert_equal(info['dfipaybacktokens'], ['4000.04105121@DUSD'])

        vaultAfter = self.nodes[0].getvault(vaultId)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(account0)[0].split('@')

        assert_equal(len(vaultAfter['loanAmounts']), 0)
        assert_equal(len(vaultAfter['interestAmounts']), 0)
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('210.52871906'))

        # Payback of loan token other than DUSD
        vaultId2 = self.nodes[0].createvault(account0, 'LOAN150')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId2, account0, "100@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': vaultId2,
            'amounts': "10@" + symbolTSLA
        })
        self.nodes[0].generate(1)

        # Should not be able to payback loan token other than DUSD with DFI
        assert_raises_rpc_error(-32600, "There is no loan on token (DUSD) in this vault!", self.nodes[0].paybackloan, {
            'vaultId': vaultId2,
            'from': account0,
            'amounts': "10@DFI"
        })

        # Multiple token payback pre FCR
        # Disable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + iddUSD + '/payback_dfi':'false'}})
        self.nodes[0].generate(1)
        vaultId6 = self.nodes[0].createvault(account0, 'LOAN150')
        self.nodes[0].generate(1)

        self.nodes[0].utxostoaccount({account0: "100@DFI"})
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId6, account0, "100@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].generate(1)
        # Create and fill addres with 10DFI + 70DUSD
        self.nodes[0].utxostoaccount({account0: "11@DFI"})
        self.nodes[0].generate(1)
        addr_DFI_DUSD = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {addr_DFI_DUSD: ["11@DFI", "71@DUSD"]}
        self.nodes[0].accounttoaccount(account0, toAmounts)
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': vaultId6,
            'amounts': "100@DUSD"
        })
        self.nodes[0].generate(1)

        # Check total DUSD loan amount is just the new loan amount
        attributes = self.nodes[0].getgov("ATTRIBUTES")['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/loans'], [f'100.00000000@{symboldUSD}'])

        [balanceDFIBefore, _] = self.nodes[0].getaccount(addr_DFI_DUSD)[0].split('@')
        [balanceDUSDBefore, _] = self.nodes[0].getaccount(addr_DFI_DUSD)[1].split('@')
        assert_equal(balanceDUSDBefore, '71.00000000')
        assert_equal(balanceDFIBefore, '11.00000000')

        try:
            self.nodes[0].paybackloan({
                'vaultId': vaultId6,
                'from': addr_DFI_DUSD,
                'amounts': ["70@DUSD", "10@DFI"]
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Payback of loan via DFI token is not currently active" in errorString)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + iddUSD + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        self.nodes[0].paybackloan({
            'vaultId': vaultId6,
            'from': addr_DFI_DUSD,
            'amounts': ["70@DUSD", "10@DFI"]
        })
        self.nodes[0].generate(1)

        # Check total DUSD loan amount is now empty
        attributes = self.nodes[0].getgov("ATTRIBUTES")['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/loans'], [])

        vaultAfter = self.nodes[0].getvault(vaultId6)
        assert_equal(vaultAfter["loanAmounts"], [])
        [balanceDUSDAfter, _] = self.nodes[0].getaccount(addr_DFI_DUSD)[1].split('@')
        [balanceDFIAfter, _] = self.nodes[0].getaccount(addr_DFI_DUSD)[0].split('@')
        assert_equal(Decimal(balanceDUSDBefore) - Decimal(balanceDUSDAfter), Decimal('5.00022832'))
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('10'))

        # Move to FCG fork
        self.nodes[0].generate(200 - self.nodes[0].getblockcount())

        idTSLA = list(self.nodes[0].gettoken(symbolTSLA).keys())[0]

        # create pool DUSD-DFI
        self.nodes[0].createpoolpair({
            "tokenA": idTSLA,
            "tokenB": iddUSD,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "TSLA-DUSD",
        })
        self.nodes[0].createpoolpair({
            "tokenA": idBTC,
            "tokenB": iddUSD,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "BTC-DUSD",
        })
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity(
            {account0: ["1@" + symbolTSLA, "10@" + symboldUSD]}, account0)
        self.nodes[0].addpoolliquidity(
            {account0: ["100@" + symbolBTC, "1000@" + symboldUSD]}, account0)
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2206a/dusd_interest_burn':'true', 'v0/token/'+idTSLA+'/loan_payback/'+idBTC: 'true', 'v0/token/'+idTSLA+'/loan_payback/'+iddUSD: 'true'}})
        self.nodes[0].generate(1)

        burnAddress = "mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG"

        [balanceDFIBefore, _] = self.nodes[0].getaccount(burnAddress)[0].split('@')
        assert_equal(len(self.nodes[0].getaccount(burnAddress)), 1)

        burn_before = self.nodes[0].getburninfo()['paybackburn']

        self.nodes[0].paybackloan({
            'vaultId': vaultId2,
            'from': account0,
            'amounts': ["1@TSLA"]
        })
        self.nodes[0].generate(1)

        burn_after = self.nodes[0].getburninfo()['paybackburn']

        dusd_amount = '0.00216423'

        assert_equal(burn_before[0], burn_after[0])
        assert_equal(burn_after[1], f'{dusd_amount}@{symboldUSD}')

        [balanceDFIAfter, _] = self.nodes[0].getaccount(burnAddress)[0].split('@')
        [balanceDUSDAfter, _] = self.nodes[0].getaccount(burnAddress)[1].split('@')
        assert_equal(Decimal(balanceDUSDAfter), Decimal(dusd_amount))
        assert_equal(Decimal(balanceDFIAfter) - Decimal(balanceDFIBefore), Decimal('0'))

        balanceDUSDBefore = balanceDUSDAfter
        balanceDFIBefore = balanceDFIAfter
        burn_before = burn_after

        self.nodes[0].paybackloan({
            'vaultId': vaultId2,
            'from': account0,
            'loans': [
                {
                    'dToken': idTSLA,
                    'amounts': "1@BTC"
                },
            ]
        })
        self.nodes[0].generate(1)

        burn_after = self.nodes[0].getburninfo()['paybackburn']

        assert_equal(burn_after[0], f'431.96962411@{symbolDFI}')
        assert_equal(burn_after[1], burn_before[1])

        [balanceDFIAfter, _] = self.nodes[0].getaccount(burnAddress)[0].split('@')
        [balanceDUSDAfter, _] = self.nodes[0].getaccount(burnAddress)[1].split('@')
        assert_equal(Decimal(balanceDUSDAfter) - Decimal(balanceDUSDBefore), Decimal('0'))
        assert_equal(Decimal(balanceDFIAfter) - Decimal(balanceDFIBefore), Decimal('0.95477661'))

        balanceDUSDBefore = balanceDUSDAfter
        balanceDFIBefore = balanceDFIAfter
        burn_before = burn_after

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2206a/dusd_loan_burn':'true'}})
        self.nodes[0].generate(1)

        self.nodes[0].paybackloan({
            'vaultId': vaultId2,
            'from': account0,
            'loans': [
                {
                    'dToken': idTSLA,
                    'amounts': "1@BTC"
                },
            ]
        })
        self.nodes[0].generate(1)

        burn_after = self.nodes[0].getburninfo()['paybackburn']

        assert_equal(burn_after[0], burn_before[0])
        assert_equal(burn_after[1], f'9.69017531@{symboldUSD}')

        [balanceDFIAfter, _] = self.nodes[0].getaccount(burnAddress)[0].split('@')
        [balanceDUSDAfter, _] = self.nodes[0].getaccount(burnAddress)[1].split('@')
        assert_equal(Decimal(balanceDUSDAfter) - Decimal(balanceDUSDBefore), Decimal('9.68801108'))
        assert_equal(Decimal(balanceDFIAfter) - Decimal(balanceDFIBefore), Decimal('0'))

        balanceDUSDBefore = balanceDUSDAfter
        balanceDFIBefore = balanceDFIAfter
        burn_before = burn_after

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idTSLA + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        self.nodes[0].paybackloan({
            'vaultId': vaultId2,
            'from': account0,
            'loans': [
                {
                    'dToken': idTSLA,
                    'amounts': "1@DFI"
                },
            ]
        })
        self.nodes[0].generate(1)

        burn_after = self.nodes[0].getburninfo()['paybackburn']

        assert_equal(burn_after[0], f'432.96962411@{symbolDFI}')
        assert_equal(burn_after[1], burn_before[1])

        [balanceDFIAfter, _] = self.nodes[0].getaccount(burnAddress)[0].split('@')
        [balanceDUSDAfter, _] = self.nodes[0].getaccount(burnAddress)[1].split('@')
        assert_equal(Decimal(balanceDUSDAfter) - Decimal(balanceDUSDBefore), Decimal('0'))
        assert_equal(Decimal(balanceDFIAfter) - Decimal(balanceDFIBefore), Decimal('1.00000000'))

        balanceDUSDBefore = balanceDUSDAfter
        balanceDFIBefore = balanceDFIAfter
        burn_before = burn_after

        self.nodes[0].takeloan({
            'vaultId': vaultId2,
            'amounts': "100@DUSD"
        })
        self.nodes[0].generate(10)

        # Check total DUSD loan amount is just the new loan amount
        attributes = self.nodes[0].getgov("ATTRIBUTES")['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/loans'], [f'100.00000000@{symboldUSD}'])

        self.nodes[0].paybackloan({
            'vaultId': vaultId2,
            'from': account0,
            'amounts': ["10@DUSD"]
        })
        self.nodes[0].generate(1)

        # Check total DUSD loan amount is less 10 DUSD less interest
        attributes = self.nodes[0].getgov("ATTRIBUTES")['ATTRIBUTES']
        assert_equal(attributes['v0/live/economy/loans'], [f'90.00114156@{symboldUSD}'])

        burn_after = self.nodes[0].getburninfo()['paybackburn']

        assert_equal(burn_after[0], burn_before[0])
        assert_equal(burn_after[1], f'9.69131687@{symboldUSD}')

        [balanceDFIAfter, _] = self.nodes[0].getaccount(burnAddress)[0].split('@')
        [balanceDUSDAfter, _] = self.nodes[0].getaccount(burnAddress)[1].split('@')

        assert_equal(Decimal(balanceDUSDAfter) - Decimal(balanceDUSDBefore), Decimal('0.00114156'))
        assert_equal(Decimal(balanceDFIAfter) - Decimal(balanceDFIBefore), Decimal('0'))

        balanceDUSDBefore = balanceDUSDAfter
        balanceDFIBefore = balanceDFIAfter
        burn_before = burn_after

        self.nodes[0].paybackloan({
            'vaultId': vaultId2,
            'from': account0,
            'loans': [
                {
                    'dToken': idTSLA,
                    'amounts': "10@DUSD"
                },
            ]
        })
        self.nodes[0].generate(1)

        burn_after = self.nodes[0].getburninfo()['paybackburn']

        assert_equal(burn_after[0], burn_before[0])
        assert_equal(burn_after[1], f'19.69131687@{symboldUSD}')

        [balanceDFIAfter, _] = self.nodes[0].getaccount(burnAddress)[0].split('@')
        [balanceDUSDAfter, _] = self.nodes[0].getaccount(burnAddress)[1].split('@')

        assert_equal(Decimal(balanceDUSDAfter) - Decimal(balanceDUSDBefore), Decimal('10'))
        assert_equal(Decimal(balanceDFIAfter) - Decimal(balanceDFIBefore), Decimal('0'))

if __name__ == '__main__':
    PaybackDFILoanTest().main()
