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
        self.FCR_HEIGHT = 800
        self.num_nodes = 1
        self.FINISHED_SETUP_BLOCK = 0
        self.setup_clean_chain = True
        self.extra_args = [
            ['-walletbroadcast=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-eunosheight=50','-fortcanningheight=50', '-fortcanninghillheight=50', f'-fortcanningroadheight={self.FCR_HEIGHT}', '-simulatemainnet', '-txindex=1', '-jellyfish_regtest=1' ],
        ]
        self.symbolDFI = "DFI"
        self.symbolBTC = "BTC"
        self.symboldUSD = "DUSD"
        self.symbolTSLA = "TSLA"

    def go_post_FCR(self):
        self.nodes[0].generate(self.FCR_HEIGHT+1)

    def reset_chain(self):
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(1))

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
        self.nodes[0].utxostoaccount({self.account0: "6000000@" + self.symbolDFI})
        self.nodes[0].generate(1)


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
            {"currency": "USD", "tokenAmount": "0.005@TSLA"},
            {"currency": "USD", "tokenAmount": "4@DFI"},
            {"currency": "USD", "tokenAmount": "50000@BTC"}
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
        self.nodes[0].minttokens("70000100@DUSD")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("500@BTC")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("5000000000@TSLA")
        self.nodes[0].generate(1)
        self.iddUSD = list(self.nodes[0].gettoken(self.symboldUSD).keys())[0]
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

    def create_fill_addresses(self):
        # Fill LM account DFI-DUSD
        # 5714285@DFI
        # 20000000@DUSD
        # 1 DFI = 3.5 DUSD
        self.addr_pool_DFI_DUSD = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {self.addr_pool_DFI_DUSD: ["5000000@DFI", "20000000@DUSD"]}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)

        # Fill LM account BTC-DUSD
        # 400@BTC
        # 20000000@DUSD
        # 1 BTC = 50000 DUSD
        self.addr_pool_BTC_DUSD = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {self.addr_pool_BTC_DUSD: ["400@BTC", "20000000@DUSD"]}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)

        # Fill LM account TSLA-DUSD
        # 4000000000@TSLA
        # 20000000@DUSD
        # 1 TSLA = 0.005 DUSD
        self.addr_pool_TSLA_DUSD = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {self.addr_pool_TSLA_DUSD: ["4000000000@TSLA", "20000000@DUSD"]}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)

        # Fill account DUSD
        self.addr_DUSD = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {self.addr_DUSD: "10000000@DUSD"}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)

        # Fill account DFI
        self.addr_DFI = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {self.addr_DFI: "1000000@DFI"}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)

        # Fill account BTC
        self.addr_BTC = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {self.addr_BTC: "100@BTC"}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)

        # Fill account TSLA
        self.addr_TSLA = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {self.addr_TSLA: "1000000000@TSLA"}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)


        # Check balances on each account
        account = self.nodes[0].getaccount(self.account0)
        assert_equal(account, ["100.00000000@DUSD"])
        account = self.nodes[0].getaccount(self.addr_pool_DFI_DUSD)
        assert_equal(account, ['5000000.00000000@DFI', '20000000.00000000@DUSD'])
        account = self.nodes[0].getaccount(self.addr_pool_BTC_DUSD)
        assert_equal(account, ['400.00000000@BTC', '20000000.00000000@DUSD'])
        account = self.nodes[0].getaccount(self.addr_pool_TSLA_DUSD)
        assert_equal(account, ['4000000000.00000000@TSLA', '20000000.00000000@DUSD'])
        account = self.nodes[0].getaccount(self.addr_DFI)
        assert_equal(account, ['1000000.00000000@DFI'])
        account = self.nodes[0].getaccount(self.addr_BTC)
        assert_equal(account, ['100.00000000@BTC'])
        account = self.nodes[0].getaccount(self.addr_TSLA)
        assert_equal(account, ['1000000000.00000000@TSLA'])

    def setup_poolpairs(self):
        poolOwner = self.nodes[0].getnewaddress("", "legacy")
        # DFI-DUSD
        self.nodes[0].createpoolpair({
            "tokenA": self.iddUSD,
            "tokenB": self.idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DFI-DUSD",
        })
        self.nodes[0].generate(1)
        self.nodes[0].addpoolliquidity(
            {self.addr_pool_DFI_DUSD: ["5000000@" + self.symbolDFI, "20000000@" + self.symboldUSD]}, self.account0)
        self.nodes[0].generate(1)

        # BTC-DUSD
        self.nodes[0].createpoolpair({
            "tokenA": self.iddUSD,
            "tokenB": self.idBTC,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "BTC-DUSD",
        })
        self.nodes[0].generate(1)
        self.nodes[0].addpoolliquidity(
            {self.addr_pool_BTC_DUSD: ["400@" + self.symbolBTC, "20000000@" + self.symboldUSD]}, self.account0)
        self.nodes[0].generate(1)

        # TSLA-DUSD
        self.nodes[0].createpoolpair({
            "tokenA": self.iddUSD,
            "tokenB": self.idTSLA,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "TSLA-DUSD",
        })
        self.nodes[0].generate(1)
        self.nodes[0].addpoolliquidity(
            {self.addr_pool_TSLA_DUSD: ["4000000000@" + self.symbolTSLA, "20000000@" + self.symboldUSD]}, self.account0)
        self.nodes[0].generate(1)

        # check poolpairs and addresses
        pool = self.nodes[0].getpoolpair("DFI-DUSD")['4']
        assert_equal(pool['reserveA'], Decimal('20000000'))
        assert_equal(pool['reserveB'], Decimal('5000000'))
        account = self.nodes[0].getaccount(self.addr_pool_DFI_DUSD)
        assert_equal(account, [])

        pool = self.nodes[0].getpoolpair("BTC-DUSD")['5']
        assert_equal(pool['reserveA'], Decimal('20000000'))
        assert_equal(pool['reserveB'], Decimal('400'))
        account = self.nodes[0].getaccount(self.addr_pool_BTC_DUSD)
        assert_equal(account, [])

        pool = self.nodes[0].getpoolpair("TSLA-DUSD")['6']
        assert_equal(pool['reserveA'], Decimal('20000000'))
        assert_equal(pool['reserveB'], Decimal('4000000000'))
        account = self.nodes[0].getaccount(self.addr_pool_TSLA_DUSD)
        assert_equal(account, [])

    def setup_loanschemes(self):
        self.nodes[0].createloanscheme(200, 1, 'LOAN200')
        self.nodes[0].generate(1)

    def setup(self, FCR=False):
        self.nodes[0].generate(100)
        if(FCR):
            self.go_post_FCR()

        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.create_tokens()
        self.setup_oracles()
        self.setup_loan_tokens()
        self.create_fill_addresses()
        self.setup_poolpairs()
        self.setup_loanschemes()
        self.FINISHED_SETUP_BLOCK = self.nodes[0].getblockcount()

    def payback_DUSD_with_BTC(self):
        self.vaultId1 = self.nodes[0].createvault(self.account0, 'LOAN200')
        self.nodes[0].generate(120)

        self.nodes[0].deposittovault(self.vaultId1, self.addr_DFI, "50@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': self.vaultId1,
            'amounts': "100@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        # Should not be able to payback loan with BTC
        errorString = ''
        try:
            self.nodes[0].paybackloan({
                    'vaultId': self.vaultId1,
                    'from': self.account0,
                    'amounts': "1@BTC"
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Loan token with id (1) does not exist!" in errorString)

    def payback_with_DFI_prior_to_atribute_activation(self):
        # Should not be able to payback loan before DFI payback enabled
        errorString = ''
        try:
            self.nodes[0].paybackloan({
                    'vaultId': self.vaultId1,
                    'from': self.account0,
                    'amounts': "1@DFI"
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Payback of loan via DFI token is not currently active" in errorString)

    def setgov_attribute_to_false_and_payback(self):
        assert_raises_rpc_error(-5, 'Unrecognised type argument provided, valid types are: consortium, governance, locks, oracles, params, poolpairs, token,',
                                self.nodes[0].setgov, {"ATTRIBUTES":{'v0/live/economy/dfi_payback_tokens':'1'}})

        # Disable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.iddUSD + '/payback_dfi':'false'}})
        self.nodes[0].generate(1)

        # Should not be able to payback loan before DFI payback enabled
        assert_raises_rpc_error(-32600, "Payback of loan via DFI token is not currently active", self.nodes[0].paybackloan, {
            'vaultId': self.vaultId1,
            'from': self.account0,
            'amounts': "1@DFI"
        })

    def setgov_attribute_to_true_and_payback_with_dfi(self):
        # Enable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.iddUSD + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        vaultBefore = self.nodes[0].getvault(self.vaultId1)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        # Partial loan payback in DFI
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId1,
            'from': self.addr_DFI,
            'amounts': "1@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('0.01000000')) # paybackfee defaults to 1% of total payback -> 0.01 DFI
        assert_equal(info['dfipaybacktokens'], ['3.96000000@DUSD']) # 4 - penalty (0.01DFI->0.04USD)
        [new_amount, _] = info['paybackburn'][0].split('@')
        assert_equal(Decimal(new_amount), Decimal('1'))

        vaultAfter = self.nodes[0].getvault(self.vaultId1)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')

        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), Decimal(amountBefore) - Decimal('3.96')) # no payback fee

    def test_5pct_penalty(self):
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.iddUSD + '/payback_dfi_fee_pct':'0.05'}})
        self.nodes[0].generate(1)

        vaultBefore = self.nodes[0].getvault(self.vaultId1)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        [old_amount, _] = self.nodes[0].getburninfo()['paybackburn'][0].split('@')

        # Partial loan payback in DFI
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId1,
            'from': self.addr_DFI,
            'amounts': "1@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('0.06000000'))
        assert_equal(info['dfipaybacktokens'], ['7.76000000@DUSD'])
        [new_amount, _] = info['paybackburn'][0].split('@')
        assert_equal(Decimal(new_amount), Decimal(old_amount) + Decimal('1'))

        vaultAfter = self.nodes[0].getvault(self.vaultId1)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')

        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - (Decimal('3.8')))) # 4$ in DFI - 0.05fee = 3.8$ paid back

    def overpay_loan_in_DFI(self):
        vaultBefore = self.nodes[0].getvault(self.vaultId1)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')

        [old_amount, _] = self.nodes[0].getburninfo()['paybackburn'][0].split('@')

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId1,
            'from': self.addr_DFI,
            'amounts': "250@DFI"
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'],  Decimal('1.27368435'))
        assert_equal(info['dfipaybacktokens'], ['100.00001113@DUSD']) # Total loan in vault1 + previous dfipaybacktokens
        [new_amount, _] = info['paybackburn'][0].split('@')
        assert_equal(Decimal(new_amount), Decimal(old_amount) + Decimal('24.27368714'))

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/dfi_payback_tokens'], ['1.27368435@DFI', '100.00001113@DUSD'])

        vaultAfter = self.nodes[0].getvault(self.vaultId1)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')

        assert_equal(len(vaultAfter['loanAmounts']), 0)
        assert_equal(len(vaultAfter['interestAmounts']), 0)
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), (Decimal(amountBefore) / Decimal('3.8')).quantize(Decimal('1E-8'), rounding=ROUND_UP))

    def take_new_loan_payback_exact_amount_in_DFI(self):
        self.nodes[0].takeloan({
            'vaultId': self.vaultId1,
            'amounts': "100@" + self.symboldUSD
        })
        self.nodes[0].generate(10)

        [old_amount, _] = self.nodes[0].getburninfo()['paybackburn'][0].split('@')

        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId1,
            'from': self.addr_DFI,
            'amounts': "26.31579449@DFI" # 25 DFI + 0.05 penalty + interests
        })
        self.nodes[0].generate(1)

        info = self.nodes[0].getburninfo()
        assert_equal(info['dfipaybackfee'], Decimal('2.58947407'))
        assert_equal(info['dfipaybacktokens'], ['200.00003016@DUSD'])
        [new_amount, _] = info['paybackburn'][0].split('@')
        assert_equal(Decimal(new_amount), Decimal(old_amount) + Decimal('26.31579449'))

        vaultAfter = self.nodes[0].getvault(self.vaultId1)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')

        assert_equal(len(vaultAfter['loanAmounts']), 0)
        assert_equal(len(vaultAfter['interestAmounts']), 0)
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('26.31579449'))

    def new_vault_payback_TSLA_loan_with_DFI(self):
        # Payback of loan token other than DUSD
        self.vaultId2 = self.nodes[0].createvault(self.account0, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId2, self.addr_DFI, "100@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': self.vaultId2,
            'amounts': "10@" + self.symbolTSLA
        })
        self.nodes[0].generate(1)

        #Should not be able to payback loan token other than DUSD with DFI
        assert_raises_rpc_error(-32600, "There is no loan on token (DUSD) in this vault!", self.nodes[0].paybackloan, {
            'vaultId': self.vaultId2,
            'from': self.addr_DFI,
            'amounts': "10@DFI"
        })
        # Should not be able to payback loan before DFI payback enabled
        assert_raises_rpc_error(-32600, "Payback of loan via DFI token is not currently active", self.nodes[0].paybackloan, {
            'vaultId': self.vaultId2,
            'from': self.addr_DFI,
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

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.idTSLA + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.idTSLA + '/payback_dfi_fee_pct':'0.01'}})
        self.nodes[0].generate(1)

    def setgov_enable_dTSLA_to_dBTC_payback(self):
        self.nodes[0].setgov({
            "ATTRIBUTES":{
                'v0/token/'+self.idTSLA+'/loan_payback/'+self.idBTC: 'true',
                'v0/token/'+self.idTSLA+'/loan_payback_fee_pct/'+self.idBTC: '0.25'
            }
        })
        self.nodes[0].generate(1)
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/token/'+self.idTSLA+'/loan_payback/'+self.idBTC], 'true')
        assert_equal(attributes['v0/token/'+self.idTSLA+'/loan_payback_fee_pct/'+self.idBTC], '0.25')


    def payback_TSLA_with_1_dfi(self):
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.addr_DFI,
            'loans': [{
                'dToken': self.idTSLA,
                'amounts': "0.01@DFI"
            }]
        })
        self.nodes[0].generate(1)

        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('0.01'))

    def take_dUSD_loan_and_payback_with_1_dfi(self):
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')
        self.nodes[0].takeloan({
            'vaultId': self.vaultId2,
            'amounts': "100@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        vaultBefore = self.nodes[0].getvault(self.vaultId2)
        [amountBefore, _] = vaultBefore['loanAmounts'][1].split('@')

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.addr_DFI,
            'loans': [{
                'dToken': self.iddUSD,
                'amounts': "1@DFI"
            }]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId2)
        [amountAfter, _] = vaultAfter['loanAmounts'][1].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][1].split('@')
        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - Decimal('3.8')))

        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('1'))

    def payback_TSLA_and_dUSD_with_1_dfi(self):
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')
        vaultBefore = self.nodes[0].getvault(self.vaultId2)
        [amountTSLABefore, _] = vaultBefore['loanAmounts'][0].split('@')
        [amountDUSDBefore, _] = vaultBefore['loanAmounts'][1].split('@')

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.addr_DFI,
            'loans': [
                {
                    'dToken': self.idTSLA,
                    'amounts': "0.001@DFI"
                },
                {
                    'dToken': self.iddUSD,
                    'amounts': "1@DFI"
                },
            ]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId2)
        [amountTSLAAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestTSLAAfter, _] = vaultAfter['interestAmounts'][0].split('@')
        [amountDUSDAfter, _] = vaultAfter['loanAmounts'][1].split('@')
        [interestDUSDAfter, _] = vaultAfter['interestAmounts'][1].split('@')
        assert_equal(Decimal(amountTSLAAfter) - Decimal(interestTSLAAfter), (Decimal(amountTSLABefore) - Decimal('0.792')))
        assert_equal(Decimal(amountDUSDAfter) - Decimal(interestDUSDAfter), (Decimal(amountDUSDBefore) - Decimal('3.8')))

        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('1.001'))

    def payback_TSLA_with_10_dfi(self):
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.addr_DFI,
            'loans': [{
                'dToken': self.idTSLA,
                'amounts': "10@DFI"
            }]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId2)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')

        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('0.00162627'))
        assert_equal(len(vaultAfter['loanAmounts']), 1)
        assert_equal(len(vaultAfter['interestAmounts']), 1)

    def payback_dUSD_with_dfi(self):
        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')
        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.addr_DFI,
            'loans': [{
                'dToken': self.iddUSD,
                'amounts': "25@DFI"
            }]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId2)
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.addr_DFI)[0].split('@')

        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('24.31579139'))
        assert_equal(len(vaultAfter['loanAmounts']), 0)
        assert_equal(len(vaultAfter['interestAmounts']), 0)

    def payback_TSLA_with_1_dBTC(self):
        self.vaultId3 = self.nodes[0].createvault(self.account0, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId3, self.addr_DFI, "100000@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': self.vaultId3,
            'amounts': "10000000@" + self.symbolTSLA
        })
        self.nodes[0].generate(1)
        [balanceBTCBefore, _] = self.nodes[0].getaccount(self.addr_BTC)[0].split('@')
        vaultBefore = self.nodes[0].getvault(self.vaultId3)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        [old_amount, _] = self.nodes[0].getburninfo()['paybackburn'][0].split('@')

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId3,
            'from': self.addr_BTC,
            'loans': [{
                'dToken': self.idTSLA,
                'amounts': "0.5@BTC"
            }]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId3)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')
        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - Decimal('3750000'))) # add 25% fee for payback with BTC

        [balanceBTCAfter, _] = self.nodes[0].getaccount(self.addr_BTC)[0].split('@')
        assert_equal(Decimal(balanceBTCBefore) - Decimal(balanceBTCAfter), Decimal('0.5'))

        info = self.nodes[0].getburninfo()
        assert_equal(info['paybackfees'], ['0.12500000@BTC'])
        assert_equal(info['paybacktokens'], ['3750000.00000000@TSLA'])
        [new_amount, _] = info['paybackburn'][0].split('@')
        assert_equal(Decimal(new_amount), Decimal(old_amount) + Decimal('6209.54767138'))

    def payback_dUSD_with_dUSD(self):
        self.nodes[0].takeloan({
            'vaultId': self.vaultId2,
            'amounts': "100@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        [balanceDUSDBefore, _] = self.nodes[0].getaccount(self.addr_DUSD)[0].split('@')
        vaultBefore = self.nodes[0].getvault(self.vaultId2)
        [amountBefore, _] = vaultBefore['loanAmounts'][0].split('@')

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId2,
            'from': self.addr_DUSD,
            'loans': [{
                'dToken': self.iddUSD,
                'amounts': "100@DUSD"
            }]
        })
        self.nodes[0].generate(1)


        vaultAfter = self.nodes[0].getvault(self.vaultId2)
        [amountAfter, _] = vaultAfter['loanAmounts'][0].split('@')
        [interestAfter, _] = vaultAfter['interestAmounts'][0].split('@')
        assert_equal(Decimal(amountAfter) - Decimal(interestAfter), (Decimal(amountBefore) - Decimal('100')))

        [balanceDUSDAfter, _] = self.nodes[0].getaccount(self.addr_DUSD)[0].split('@')
        assert_equal(Decimal(balanceDUSDBefore) - Decimal(balanceDUSDAfter), Decimal('100'))

    def payback_TSLA_with_1sat_dBTC(self):
        self.vaultId4 = self.nodes[0].createvault(self.account0, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId4, self.addr_DFI, "0.00000001@DFI")
        self.nodes[0].generate(1)

        [balanceBTCBefore, _] = self.nodes[0].getaccount(self.addr_BTC)[0].split('@')

        self.nodes[0].takeloan({
            'vaultId': self.vaultId4,
            'amounts': "0.00000001@" + self.symbolTSLA
        })
        self.nodes[0].generate(1)

        [old_amount, _] = self.nodes[0].getburninfo()['paybackburn'][0].split('@')

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId4,
            'from': self.addr_BTC,
            'loans': [{
                'dToken': self.idTSLA,
                'amounts': "0.00000001@BTC"
            }]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId4)
        assert_equal(vaultAfter["interestAmounts"], [])
        assert_equal(vaultAfter["loanAmounts"], [])

        [balanceBTCAfter, _] = self.nodes[0].getaccount(self.addr_BTC)[0].split('@')
        assert_equal(Decimal(balanceBTCBefore) - Decimal(balanceBTCAfter), Decimal('0.00000001'))

        info = self.nodes[0].getburninfo()
        assert_equal(info['paybackfees'], ['0.12500000@BTC'])
        assert_equal(info['paybacktokens'], ['3750000.00000002@TSLA'])
        [new_amount, _] = info['paybackburn'][0].split('@')
        assert_equal(Decimal(new_amount), Decimal(old_amount) + Decimal('0.00012413'))

    def multipayback_DUSD_with_DFI_and_DUSD(self):
        self.vaultId5 = self.nodes[0].createvault(self.account0, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId5, self.addr_DFI, "100@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].generate(1)
        # Create and fill addres with 10DFI + 70DUSD
        self.nodes[0].utxostoaccount({self.account0: "11@" + self.symbolDFI})
        self.nodes[0].generate(1)
        self.addr_DFI_DUSD = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {self.addr_DFI_DUSD: ["11@DFI", "71@DUSD"]}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': self.vaultId5,
            'amounts': "100@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.addr_DFI_DUSD)[0].split('@')
        [balanceDUSDBefore, _] = self.nodes[0].getaccount(self.addr_DFI_DUSD)[1].split('@')
        assert_equal(balanceDUSDBefore, '71.00000000')
        assert_equal(balanceDFIBefore, '11.00000000')

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId5,
            'from': self.addr_DFI_DUSD,
            'loans': [
                {
                    'dToken': self.iddUSD,
                    'amounts': ["70@DUSD", "10@DFI"]
                }
            ]
        })
        #self.nodes[0].paybackloan({
        #    'vaultId': self.vaultId5,
        #    'from': self.addr_DFI_DUSD,
        #    'loans': [
        #        {
        #            'dToken': self.iddUSD,
        #            'amounts': "70@DUSD"
        #        },
        #        {
        #            'dToken': self.iddUSD,
        #            'amounts': "10@DFI"
        #        }
        #    ]
        #})
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId5)
        assert_equal(vaultAfter["loanAmounts"], [])
        [balanceDUSDAfter, _] = self.nodes[0].getaccount(self.addr_DFI_DUSD)[1].split('@')
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.addr_DFI_DUSD)[0].split('@')
        assert_equal(Decimal(balanceDUSDBefore) - Decimal(balanceDUSDAfter), Decimal('62.00000191')) # balanceAfter = 71DUSD - (100.00000191DUSD loan+interests - 38DUSD (40DFI - 5% fee)
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('10'))

    def multipayback_DUSD_with_DFI_and_DUSD_Pre_FCR(self):
        self.vaultId6 = self.nodes[0].createvault(self.account0, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId6, self.addr_DFI, "100@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].generate(1)
        # Create and fill addres with 10DFI + 70DUSD
        self.nodes[0].utxostoaccount({self.account0: "11@" + self.symbolDFI})
        self.nodes[0].generate(1)
        self.addr_DFI_DUSD = self.nodes[0].getnewaddress("", "legacy")
        toAmounts = {self.addr_DFI_DUSD: ["11@DFI", "71@DUSD"]}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': self.vaultId6,
            'amounts': "100@" + self.symboldUSD
        })
        self.nodes[0].generate(1)

        [balanceDFIBefore, _] = self.nodes[0].getaccount(self.addr_DFI_DUSD)[0].split('@')
        [balanceDUSDBefore, _] = self.nodes[0].getaccount(self.addr_DFI_DUSD)[1].split('@')
        assert_equal(balanceDUSDBefore, '71.00000000')
        assert_equal(balanceDFIBefore, '11.00000000')

        errorString = ''
        try:
            self.nodes[0].paybackloan({
                'vaultId': self.vaultId6,
                'from': self.addr_DFI_DUSD,
                'amounts': ["70@DUSD", "10@DFI"]
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Payback of loan via DFI token is not currently active" in errorString)
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.iddUSD + '/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        self.nodes[0].paybackloan({
            'vaultId': self.vaultId6,
            'from': self.addr_DFI_DUSD,
            'amounts': ["70@DUSD", "10@DFI"]
        })
        self.nodes[0].generate(1)

        vaultAfter = self.nodes[0].getvault(self.vaultId6)
        assert_equal(vaultAfter["loanAmounts"], [])
        [balanceDUSDAfter, _] = self.nodes[0].getaccount(self.addr_DFI_DUSD)[1].split('@')
        [balanceDFIAfter, _] = self.nodes[0].getaccount(self.addr_DFI_DUSD)[0].split('@')
        assert_equal(Decimal(balanceDUSDBefore) - Decimal(balanceDUSDAfter), Decimal('60.40000571'))
        assert_equal(Decimal(balanceDFIBefore) - Decimal(balanceDFIAfter), Decimal('10'))


    def run_test(self):
        self.setup(FCR=True)
        self.payback_DUSD_with_BTC()
        self.payback_with_DFI_prior_to_atribute_activation()
        self.setgov_attribute_to_false_and_payback()
        self.setgov_attribute_to_true_and_payback_with_dfi()
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

        self.setgov_enable_dTSLA_to_dBTC_payback()

        self.payback_TSLA_with_1_dBTC()
        self.payback_dUSD_with_dUSD()
        self.payback_TSLA_with_1sat_dBTC()

        self.multipayback_DUSD_with_DFI_and_DUSD()

        self.reset_chain()
        self.setup(FCR=False)
        self.multipayback_DUSD_with_DFI_and_DUSD_Pre_FCR()

if __name__ == '__main__':
    PaybackDFILoanTest().main()
