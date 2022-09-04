#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test no_dusd_loop"""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException

from test_framework.util import assert_equal

import calendar
import time

from decimal import Decimal

def get_decimal_amount(amount):
    account_tmp = amount.split('@')[0]
    return Decimal(account_tmp)

class DUSDLoanTests(DefiTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.fortcanninggreatworldheight = 1000
        self.fortcanningroadheight = 1500
        self.fortcanningepilogueheight = 2000
        self.extra_args = [
            ['-txnotokens=0',
            '-amkheight=1',
            '-bayfrontheight=1',
            '-eunosheight=1',
            '-fortcanningheight=1',
            '-fortcanningmuseumheight=1',
            '-fortcanningparkheight=1',
            '-fortcanninghillheight=1',
            '-fortcanningroadheight=1',
            '-fortcanningcrunchheight=1',
            '-fortcanningspringheight=1',
            f'-fortcanninggreatworldheight={self.fortcanninggreatworldheight}',
            f'-fortcanningepilogueheight={self.fortcanningepilogueheight}',
            '-jellyfish_regtest=1', '-txindex=1', '-simulatemainnet=1']
        ]

    def vault_without_dusd_loan_test_withdraw_before_and_after_fce(self):

        blockHeight = self.nodes[0].getblockcount()
        self.update_oracle_price()
        # BEFORE FCE
        self.goto_gw_height()


        loanScheme = 'LOAN1'
        vaultId = self.new_vault_with_dusd_and_btc(loanScheme, deposit=10)
        tsla_balance_before_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[3])
        loanAmount = 1

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        tsla_balance_after_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[3])
        assert_equal(tsla_balance_after_loan, tsla_balance_before_loan + loanAmount)
        vault = self.nodes[0].getvault(vaultId)

        # Withdraw
        dusd_balance_before_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        withdraw_amount = Decimal('2.4')
        self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+'@'+self.symboldUSD)
        self.nodes[0].generate(1)
        dusd_balance_after_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        assert_equal(Decimal(dusd_balance_before_withdraw), Decimal(dusd_balance_after_withdraw) - Decimal(withdraw_amount))

        btc_balance_before_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[1])
        withdraw_amount = Decimal('0.1')
        self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+'@'+self.symbolBTC)
        self.nodes[0].generate(1)
        btc_balance_after_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[1])
        assert_equal(Decimal(btc_balance_before_withdraw), Decimal(btc_balance_after_withdraw) - Decimal(withdraw_amount))

        withdraw_amount = Decimal('0.1')
        err_string = "At least 50% of the minimum required collateral must be in DFI or DUSD"
        try:
            self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+"@"+self.symboldUSD)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

        self.goto_fce_height()

        withdraw_amount = Decimal('0.1')
        err_string = "At least 50% of the minimum required collateral must be in DFI or DUSD"
        try:
            self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+"@"+self.symboldUSD)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

        btc_balance_before_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[1])
        withdraw_amount = Decimal('0.1')
        self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+'@'+self.symbolBTC)
        self.nodes[0].generate(1)
        btc_balance_after_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[1])
        assert_equal(Decimal(btc_balance_before_withdraw), Decimal(btc_balance_after_withdraw) - Decimal(withdraw_amount))

        self.nodes[0].deposittovault(vaultId, self.account0, '100@DUSD')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, '5@DFI')
        self.nodes[0].generate(1)


        loanAmount = 1
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        dfi_balance_before_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[0])
        withdraw_amount = Decimal('5')
        self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+'@'+self.symbolDFI)
        self.nodes[0].generate(1)
        dfi_balance_after_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[0])
        assert_equal(Decimal(dfi_balance_before_withdraw), Decimal(dfi_balance_after_withdraw) - Decimal(withdraw_amount))
        vault = self.nodes[0].getvault(vaultId)
        assert_equal(vault["collateralAmounts"], ['9.80000000@BTC', '107.60000000@DUSD'])

        btc_balance_before_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[1])
        withdraw_amount = Decimal('9.8')
        self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+'@'+self.symbolBTC)
        self.nodes[0].generate(1)
        btc_balance_after_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[1])
        assert_equal(Decimal(btc_balance_before_withdraw), Decimal(btc_balance_after_withdraw) - Decimal(withdraw_amount))
        vault = self.nodes[0].getvault(vaultId)
        assert_equal(vault["collateralAmounts"], ['107.60000000@DUSD'])

        self.rollback_to(blockHeight)
        err_string = f"Vault <{vaultId}> not found"
        try:
            self.nodes[0].getvault(vaultId)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

    def vault_with_dusd_loan_test_withdraw_before_and_after_fce(self):
        blockHeight = self.nodes[0].getblockcount()
        self.update_oracle_price()
        # BEFORE FCE
        self.goto_gw_height()

        loanScheme = 'LOAN1'
        vaultId = self.new_vault_with_dusd_only(loanScheme, deposit=100)
        dusd_balance_before_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        loanAmount = 1
        vault = self.nodes[0].getvault(vaultId)
        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        dusd_balance_after_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        assert_equal(dusd_balance_after_loan, dusd_balance_before_loan + 1)

        # Withdraw
        dusd_balance_before_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        withdraw_amount = 8
        self.nodes[0].withdrawfromvault(vaultId, self.account0, f'{str(withdraw_amount)}@{self.symboldUSD}')
        self.nodes[0].generate(1)
        dusd_balance_after_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        assert_equal(dusd_balance_before_withdraw, dusd_balance_after_withdraw - Decimal(withdraw_amount))

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        dusd_balance_before_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        withdraw_amount = 8
        self.nodes[0].withdrawfromvault(vaultId, self.account0, f'{str(withdraw_amount)}@{self.symboldUSD}')
        self.nodes[0].generate(1)
        dusd_balance_after_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        assert_equal(dusd_balance_before_withdraw, dusd_balance_after_withdraw - Decimal(withdraw_amount))

        # AFTER CFE
        self.goto_fce_height()
        withdraw_amount = 8

        err_string = "At least 50% of the minimum required collateral must be in DFI"
        try:
            self.nodes[0].withdrawfromvault(vaultId, self.account0, f'{str(withdraw_amount)}@{self.symboldUSD}')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

        # Deposit DFI so it does not fail on withdraw
        self.nodes[0].deposittovault(vaultId, self.account0, '0.9@DFI')
        self.nodes[0].generate(1)

        dfi_balance_before_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[0])
        withdraw_amount = Decimal('0.07')
        self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+'@'+self.symbolDFI)
        self.nodes[0].generate(1)
        dfi_balance_after_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[0])
        assert_equal(Decimal(dfi_balance_before_withdraw), Decimal(dfi_balance_after_withdraw) - Decimal(withdraw_amount))

        withdraw_amount = Decimal('0.01')
        err_string = "At least 50% of the minimum required collateral must be in DFI"
        try:
            self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+'@'+self.symbolDFI)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

        dusd_balance_before_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        withdraw_amount = Decimal('75.75')
        self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+'@'+self.symboldUSD)
        self.nodes[0].generate(1)
        dusd_balance_after_withdraw = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        assert_equal(Decimal(dusd_balance_before_withdraw), Decimal(dusd_balance_after_withdraw) - Decimal(withdraw_amount))

        vault = self.nodes[0].getvault(vaultId)
        assert_equal(vault["collateralRatio"], 150)
        assert_equal(vault["collateralAmounts"], ['0.83000000@DFI', '8.25000000@DUSD'])
        assert_equal(vault["loanAmounts"], ['1.00000953@DUSD', '1.00000951@TSLA'])

        self.rollback_to(blockHeight)
        err_string = f"Vault <{vaultId}> not found"
        try:
            self.nodes[0].getvault(vaultId)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

    def dusd_loans_before_and_after_fce(self):

        blockHeight = self.nodes[0].getblockcount()
        self.update_oracle_price()
        # BEFORE FCE
        self.goto_gw_height()

        loanScheme = 'LOAN1'
        vaultId = self.new_vault_with_dusd_only(loanScheme)
        dusd_balance_before_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        loanAmount = 1

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        dusd_balance_after_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        assert_equal(dusd_balance_after_loan, dusd_balance_before_loan + loanAmount)

        self.nodes[0].deposittovault(vaultId, self.account0, '100' + "@BTC")
        self.nodes[0].generate(1)

        loanAmount = 15
        err_string = "At least 50% of the minimum required collateral must be in DFI or DUSD when taking a loan."
        try:
            self.nodes[0].takeloan({
                'vaultId': vaultId,
                'amounts': f"{loanAmount}@" + self.symboldUSD})
            self.nodes[0].generate(1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)
        dusd_balance_before_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])

        loanAmount = 1

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(1)

        dusd_balance_after_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        assert_equal(dusd_balance_after_loan, dusd_balance_before_loan + loanAmount)

        self.goto_fce_height()
        err_string = "At least 50% of the minimum required collateral must be in DFI"
        try:
            self.nodes[0].takeloan({
                'vaultId': vaultId,
                'amounts': f"{loanAmount}@" + self.symboldUSD})
            self.nodes[0].generate(1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

        tsla_balance_before_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[3])

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symbolTSLA})
        self.nodes[0].generate(1)

        tsla_balance_after_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[3])
        assert_equal(tsla_balance_after_loan, tsla_balance_before_loan + loanAmount)

        err_string = "At least 50% of the minimum required collateral must be in DFI"
        try:
            withdraw_amount = Decimal('50')
            self.nodes[0].withdrawfromvault(vaultId, self.account0, str(withdraw_amount)+'@'+self.symbolBTC)
            self.nodes[0].generate(1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

        self.nodes[0].deposittovault(vaultId, self.account0, '1' + "@DFI")
        self.nodes[0].generate(1)

        loanAmount = 15
        err_string = "At least 50% of the minimum required collateral must be in DFI"
        try:
            self.nodes[0].takeloan({
                'vaultId': vaultId,
                'amounts': f"{loanAmount}@" + self.symboldUSD})
            self.nodes[0].generate(1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

        self.nodes[0].deposittovault(vaultId, self.account0, '20' + "@DFI")
        self.nodes[0].generate(1)

        dusd_balance_before_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': f"{loanAmount}@" + self.symboldUSD})
        self.nodes[0].generate(10)

        dusd_balance_after_loan = get_decimal_amount(self.nodes[0].getaccount(self.account0)[2])
        assert_equal(dusd_balance_after_loan, dusd_balance_before_loan + loanAmount)



        vault = self.nodes[0].getvault(vaultId)
        assert_equal(vault["collateralRatio"], 1185)
        assert_equal(vault["collateralAmounts"],  ['21.00000000@DFI','100.00000000@BTC','10.00000000@DUSD'])
        assert_equal(vault["loanAmounts"], ['17.00002065@DUSD', '1.00000013@TSLA'])

        self.rollback_to(blockHeight)
        err_string = f"Vault <{vaultId}> not found"
        try:
            self.nodes[0].getvault(vaultId)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert(err_string in errorString)

    # Utils

    def new_vault_with_dfi_only(self, loan_scheme, deposit=10):
        vaultId = self.nodes[0].createvault(self.account0, loan_scheme)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, str(deposit) + "@DFI")
        self.nodes[0].generate(1)
        return vaultId

    def new_vault_with_dusd_only(self, loan_scheme, deposit=10):
        vaultId = self.nodes[0].createvault(self.account0, loan_scheme)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, str(deposit) + "@DUSD")
        self.nodes[0].generate(1)
        return vaultId

    def new_vault_with_dusd_and_btc(self, loan_scheme, deposit=10):
        vaultId = self.nodes[0].createvault(self.account0, loan_scheme)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, str(deposit) + "@DUSD")
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, str(deposit) + "@BTC")
        self.nodes[0].generate(1)
        return vaultId

    def new_vault_with_dfi_and_dusd(self, loan_scheme, deposit=10):
        vaultId = self.nodes[0].createvault(self.account0, loan_scheme)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId, self.account0, str(deposit) + "@DFI")
        self.nodes[0].deposittovault(vaultId, self.account0, str(deposit) + "@DUSD")
        self.nodes[0].generate(1)
        return vaultId

    def goto_gw_height(self):
        blockHeight = self.nodes[0].getblockcount()
        if self.fortcanninggreatworldheight > blockHeight:
            self.nodes[0].generate((self.fortcanninggreatworldheight - blockHeight) + 2)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["fortcanninggreatworld"]["active"], True)

    def goto_fce_height(self):
        blockHeight = self.nodes[0].getblockcount()
        if self.fortcanningepilogueheight > blockHeight:
            self.nodes[0].generate((self.fortcanningepilogueheight - blockHeight) + 2)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["fortcanningepilogue"]["active"], True)

    def goto_fcr_height(self):
        blockHeight = self.nodes[0].getblockcount()
        if self.fortcanningepilogueheight > blockHeight:
            self.nodes[0].generate((self.fortcanningroadheight - blockHeight) + 2)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["fortcanningroad"]["active"], True)


    # Default token = 1 = dUSD
    def set_token_interest(self, token='1', interest=0):
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/token/{token}/loan_minting_interest': str(interest)}})
        self.nodes[0].generate(1)

    def create_tokens(self):
        self.symbolDFI = "DFI"
        self.symboldUSD = "DUSD"
        self.symbolTSLA = "TSLA"
        self.symbolBTC = "BTC"

        self.idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]

        self.nodes[0].createtoken({
            "symbol": self.symbolBTC,
            "name": "Token " + self.symbolBTC,
            "isDAT": True,
            "collateralAddress": self.account0
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': self.idDFI,
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(120)

        self.nodes[0].setcollateraltoken({
            'token': self.symbolBTC,
            'factor': 1,
            'fixedIntervalPriceId': "BTC/USD"})
        self.nodes[0].generate(120)
        self.idBTC = list(self.nodes[0].gettoken(self.symbolBTC).keys())[0]

        self.nodes[0].setloantoken({
            'symbol': self.symboldUSD,
            'name': "DUSD stable token",
            'fixedIntervalPriceId': "DUSD/USD",
            'mintable': True,
            'interest': 0})
        self.nodes[0].generate(120)
        self.iddUSD = list(self.nodes[0].gettoken(self.symboldUSD).keys())[0]

        self.nodes[0].setcollateraltoken({
            'token': self.iddUSD,
            'factor': 0.99,
            'fixedIntervalPriceId': "DUSD/USD"})
        self.nodes[0].generate(120)

        self.nodes[0].setloantoken({
            'symbol': self.symbolTSLA,
            'name': "TSLA token",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 0})
        self.nodes[0].generate(1)
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]
        self.nodes[0].minttokens("1000@TSLA")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("10000@DUSD")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("10000@BTC")
        self.nodes[0].generate(1)
        toAmounts = {self.account0: ["10000@DUSD", "1000@TSLA", "1000@BTC"]}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)
        self.nodes[0].utxostoaccount({self.account0: "10000@0"})
        self.nodes[0].generate(1)

    def create_oracles(self):
        self.oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [{"currency": "USD", "token": "DFI"},
                       {"currency": "USD", "token": "DUSD"},
                       {"currency": "USD", "token": "BTC"},
                       {"currency": "USD", "token": "TSLA"}]
        self.oracle_id1 = self.nodes[0].appointoracle(self.oracle_address1, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"},
                         {"currency": "USD", "tokenAmount": "1@DUSD"},
                         {"currency": "USD", "tokenAmount": "1@BTC"},
                         {"currency": "USD", "tokenAmount": "10@DFI"}]

        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle_prices)

        self.oracle_address2 = self.nodes[0].getnewaddress("", "legacy")
        self.oracle_id2 = self.nodes[0].appointoracle(self.oracle_address2, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, oracle_prices)
        self.nodes[0].generate(120)

    def update_oracle_price(self):
        oracle_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"},
                         {"currency": "USD", "tokenAmount": "1@DUSD"},
                         {"currency": "USD", "tokenAmount": "1@BTC"},
                         {"currency": "USD", "tokenAmount": "10@DFI"}]

        mock_time = int(time.time() + 3000)
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(self.oracle_id1, mock_time, oracle_prices)
        self.nodes[0].generate(120)

    def create_pool_pairs(self):
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDFI,
            "tokenB": self.symboldUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.account0,
            "pairSymbol": "DFI-DUSD",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolTSLA,
            "tokenB": self.symboldUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.account0,
            "pairSymbol": "TSLA-DUSD",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["100@TSLA", "1000@DUSD"]
        }, self.account0)
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["100@DFI", "1000@DUSD"]
        }, self.account0)
        self.nodes[0].generate(1)

    def setup(self):
        assert_equal(len(self.nodes[0].listtokens()), 1)  # only one token == DFI
        self.nodes[0].generate(100)
        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.create_oracles()
        self.create_tokens()
        self.create_pool_pairs()
        self.nodes[0].createloanscheme(150, 1, 'LOAN1')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(200, 2, 'LOAN2')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(300, 3, 'LOAN3')
        self.nodes[0].generate(10)
        self.setup_height = self.nodes[0].getblockcount()

    def run_test(self):
        # Initial set up
        self.setup()
        self.update_oracle_price()
        # Withdraw
        self.vault_without_dusd_loan_test_withdraw_before_and_after_fce()
        self.vault_with_dusd_loan_test_withdraw_before_and_after_fce()
        # Take loan
        self.dusd_loans_before_and_after_fce()

if __name__ == '__main__':
    DUSDLoanTests().main()
