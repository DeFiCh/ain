#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test no_dusd_loop"""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_raises_rpc_error
import calendar
import time
from decimal import Decimal

def get_decimal_amount(amount):
    account_tmp = amount.split('@')[0]
    return Decimal(account_tmp)

def token_index_in_account(accounts, symbol):
    for id in range(len(accounts)):
        if symbol in accounts[id]:
            return id
    return -1

ERR_STRING_MIN_COLLATERAL_DFI_PCT = "At least 50% of the minimum required collateral must be in DFI"
ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT = "At least 50% of the minimum required collateral must be in DFI or DUSD"

class DUSDFactorPctTests(DefiTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.fortcanningepilogueheight = 1000
        self.extra_args = [
            ['-txnotokens=0',
            '-amkheight=1',
            '-bayfrontheight=1',
            '-eunosheight=1',
            '-fortcanningheight=1',
            '-fortcanningmuseumheight=1',
            '-fortcanningparkheight=1',
            f'-fortcanninghillheight=1',
            f'-fortcanningcrunchheight=1',
            f'-fortcanningroadheight=1',
            f'-fortcanninggreatworldheight=1',
            f'-fortcanningepilogueheight={self.fortcanningepilogueheight}',
            '-jellyfish_regtest=1', '-txindex=1', '-simulatemainnet=1']
        ]

    def takeloan_withdraw(self, vaultId, amount, type='takeloan'):
        account = self.nodes[0].getaccount(self.account0)
        id = token_index_in_account(account, amount.split("@")[1])
        balance_before = Decimal(0)
        if id != -1:
            balance_before = get_decimal_amount(account[id])

        if type == 'takeloan':
            self.nodes[0].takeloan({
                'vaultId': vaultId,
                'amounts': amount})

        if type == 'withdraw':
            self.nodes[0].withdrawfromvault(vaultId, self.account0, amount)

        self.nodes[0].generate(1)
        account = self.nodes[0].getaccount(self.account0)
        if id == -1:
            id = token_index_in_account(account, amount.split("@")[1])
        balance_after = get_decimal_amount(account[id])
        assert_equal(balance_before + Decimal(amount.split("@")[0]), balance_after)

    # Utils

    def new_vault(self, loan_scheme, amounts = None):
        if amounts is None:
            amounts = []
        vaultId = self.nodes[0].createvault(self.account0, loan_scheme)
        self.nodes[0].generate(1)
        for amount in amounts:
            self.nodes[0].deposittovault(vaultId, self.account0, amount)
            self.nodes[0].generate(1)
        vault = self.nodes[0].getvault(vaultId)
        assert_equal(vault["collateralAmounts"], amounts)
        return vaultId

    def goto_fce_height(self):
        blockHeight = self.nodes[0].getblockcount()
        if self.fortcanningepilogueheight > blockHeight:
            self.nodes[0].generate((self.fortcanningepilogueheight - blockHeight) + 2)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["fortcanningepilogue"]["active"], True)


    def create_tokens(self):
        self.symboldUSD = "DUSD"
        self.symbolWMT = "WMT"
        self.symbolBTC = "BTC"

        self.nodes[0].createtoken({
            "symbol": self.symbolBTC,
            "name": "Token " + self.symbolBTC,
            "isDAT": True,
            "collateralAddress": self.account0
        }, [])
        self.nodes[0].generate(1)

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
            'factor': 1,
            'fixedIntervalPriceId': "DUSD/USD"})
        self.nodes[0].generate(120)

        self.nodes[0].setloantoken({
            'symbol': self.symbolWMT,
            'name': "WMT token",
            'fixedIntervalPriceId': "WMT/USD",
            'mintable': True,
            'interest': 0})
        self.nodes[0].generate(1)

        self.idWMT = list(self.nodes[0].gettoken(self.symbolWMT).keys())[0]
        self.nodes[0].minttokens("1000@WMT")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("1000000@DUSD")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("10@BTC")
        self.nodes[0].generate(1)
        toAmounts = {self.account0: ["1000000@DUSD", "1000@WMT", "10@BTC"]}
        self.nodes[0].accounttoaccount(self.account0, toAmounts)
        self.nodes[0].generate(1)

    def create_oracles(self):
        self.oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [{"currency": "USD", "token": "DUSD"},
                       {"currency": "USD", "token": "BTC"},
                       {"currency": "USD", "token": "WMT"}]
        self.oracle_id1 = self.nodes[0].appointoracle(self.oracle_address1, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle_prices = [{"currency": "USD", "tokenAmount": "132.65070089@WMT"},
                         {"currency": "USD", "tokenAmount": "1@DUSD"},
                         {"currency": "USD", "tokenAmount": "19349.2222248@BTC"}]

        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle_prices)

        self.oracle_address2 = self.nodes[0].getnewaddress("", "legacy")
        self.oracle_id2 = self.nodes[0].appointoracle(self.oracle_address2, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, oracle_prices)
        self.nodes[0].generate(120)


        mock_time = int(time.time()+3000)
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(self.oracle_id1, mock_time, oracle_prices)
        self.nodes[0].generate(120)

    def create_pool_pairs(self):

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolWMT,
            "tokenB": self.symboldUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.account0,
            "pairSymbol": "WMT-DUSD",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["100@WMT", "1000@DUSD"]
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

    def rollback_checks(self, vaults = None):
        if vaults is None:
            vaults = []
        for vault in vaults:
            try:
                self.nodes[0].getvault(vault)
            except JSONRPCException as e:
                errorString = e.error['message']
            assert(f"Vault <{vault}> not found" in errorString)

    # TESTS
    def post_FCE_DFI_minimum_check_takeloan(self):
        block_height = self.nodes[0].getblockcount()

        self.goto_fce_height()

        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.iddUSD}/loan_collateral_factor': '1.2'}})
        self.nodes[0].generate(1)
        vault_id = self.new_vault('LOAN1', ["0.90000000@BTC", "20931.30417782@DUSD"])
        self.takeloan_withdraw(vault_id, "204.81447327@WMT", 'takeloan')

        # Test case #1511
        self.takeloan_withdraw(vault_id, "360.1562000@DUSD", 'withdraw')
        self.takeloan_withdraw(vault_id, "2.61850000@WMT", 'takeloan')

        self.rollback_to(block_height)
        self.rollback_checks([vault_id])

    def run_test(self):
        self.setup()
        self.post_FCE_DFI_minimum_check_takeloan()

if __name__ == '__main__':
    DUSDFactorPctTests().main()
