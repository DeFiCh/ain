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

class DUSDLoanTests(DefiTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.fortcanninghillheight = 1000
        self.fortcanningroadheight = 2000
        self.fortcanninggreatworldheight = 3000
        self.fortcanningepilogueheight = 4000
        self.extra_args = [
            ['-txnotokens=0',
            '-amkheight=1',
            '-bayfrontheight=1',
            '-eunosheight=1',
            '-fortcanningheight=1',
            '-fortcanningmuseumheight=1',
            '-fortcanningparkheight=1',
            f'-fortcanninghillheight={self.fortcanninghillheight}',
            f'-fortcanningroadheight={self.fortcanningroadheight}',
            f'-fortcanninggreatworldheight={self.fortcanninggreatworldheight}',
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

    def goto_fch_height(self):
        blockHeight = self.nodes[0].getblockcount()
        if self.fortcanninghillheight > blockHeight:
            self.nodes[0].generate((self.fortcanninghillheight - blockHeight) + 2)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["fortcanninghill"]["active"], True)

    def goto_fcr_height(self):
        blockHeight = self.nodes[0].getblockcount()
        if self.fortcanningroadheight > blockHeight:
            self.nodes[0].generate((self.fortcanningroadheight - blockHeight) + 2)
        blockchainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchainInfo["softforks"]["fortcanningroad"]["active"], True)

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

    def update_oracle_price(self, add_time=3000):
        oracle_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"},
                         {"currency": "USD", "tokenAmount": "1@DUSD"},
                         {"currency": "USD", "tokenAmount": "1@BTC"},
                         {"currency": "USD", "tokenAmount": "10@DFI"}]

        mock_time = int(time.time()+add_time)
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
    def pre_FCH_DFI_minimum_check_withdraw(self):
        block_height = self.nodes[0].getblockcount()

        vault_id = self.new_vault('LOAN1', ["10.00000000@DFI", "100.00000000@BTC"])
        self.takeloan_withdraw(vault_id, "1.00000000@DUSD", 'takeloan')
        self.takeloan_withdraw(vault_id, "1.00000000@BTC", 'withdraw')

        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id, "1.00000000@DFI", 'withdraw')

        self.nodes[0].deposittovault(vault_id, self.account0, '100@DFI')
        self.nodes[0].generate(1)
        self.takeloan_withdraw(vault_id, "100.00000000@DFI", 'withdraw')

        self.rollback_to(block_height)
        self.rollback_checks([vault_id])

    def pre_FCR_DFI_minimum_check_withdraw(self):
        block_height = self.nodes[0].getblockcount()

        self.goto_fch_height()

        vault_id = self.new_vault('LOAN1', ["3.00000000@DFI", "200.00000000@BTC", "10.00000000@DUSD"])
        self.takeloan_withdraw(vault_id, "1.00000000@TSLA", 'takeloan')
        self.takeloan_withdraw(vault_id, "2.00000000@DFI", 'withdraw')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id, "0.26000000@DFI", 'withdraw')

        # Check DUSD addition as collateral is ignored
        self.nodes[0].deposittovault(vault_id, self.account0, '1000@DUSD')
        self.nodes[0].generate(1)
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id, "0.26000000@DFI", 'withdraw')

        self.rollback_to(block_height)
        self.rollback_checks([vault_id])

    def post_FCR_DFI_minimum_check_withdraw(self):
        block_height = self.nodes[0].getblockcount()

        self.goto_fcr_height()

        vault_id = self.new_vault('LOAN1', ["3.00000000@DFI", "200.00000000@BTC"])
        self.takeloan_withdraw(vault_id, "1.00000000@TSLA", 'takeloan')
        self.takeloan_withdraw(vault_id, "2.00000000@DFI", 'withdraw')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT,
                                self.takeloan_withdraw, vault_id, "0.26000000@DFI", 'withdraw')

        # Check DUSD addition as collateral is added to collateral
        self.nodes[0].deposittovault(vault_id, self.account0, '2.7@DUSD')
        self.nodes[0].generate(1)
        self.takeloan_withdraw(vault_id, "0.26000000@DFI", 'withdraw')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT,
                                self.takeloan_withdraw, vault_id, "0.50000000@DFI", 'withdraw')

        vault_id_1 = self.new_vault('LOAN1', ["200.00000000@BTC","30.00000000@DUSD"])
        self.takeloan_withdraw(vault_id_1, "1.00000000@TSLA", 'takeloan')
        self.takeloan_withdraw(vault_id_1, "20.00000000@DUSD", 'withdraw')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT,
                                self.takeloan_withdraw, vault_id_1, "2.60000000@DUSD", 'withdraw')

        # Check DFI addition as collateral is added to collateral
        self.nodes[0].deposittovault(vault_id_1, self.account0, '0.27@DFI')
        self.nodes[0].generate(1)
        self.takeloan_withdraw(vault_id_1, "2.60000000@DUSD", 'withdraw')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT,
                                self.takeloan_withdraw, vault_id_1, "5.00000000@DUSD", 'withdraw')

        self.rollback_to(block_height)
        self.rollback_checks([vault_id, vault_id_1])

    def post_FCE_DFI_minimum_check_withdraw(self):
        block_height = self.nodes[0].getblockcount()

        self.goto_fce_height()
        self.update_oracle_price()

        vault_id = self.new_vault('LOAN1', ["200.00000000@BTC", "37.50000000@DUSD"])
        self.takeloan_withdraw(vault_id, "5.00000000@TSLA", 'takeloan')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id, "0.10000000@DUSD", 'withdraw')
        self.nodes[0].deposittovault(vault_id, self.account0, '0.02@DFI')
        self.nodes[0].generate(1)

        self.takeloan_withdraw(vault_id, "0.10000000@DUSD", 'withdraw')
        # Try to take a DUSD loan without 50% DFI
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id, "10.00000000@DUSD", 'takeloan')

        # Deposit DFI and try again to take a DUSD loan
        self.nodes[0].deposittovault(vault_id, self.account0, '3.9@DFI')
        self.nodes[0].generate(1)
        self.takeloan_withdraw(vault_id, "1.00000000@DUSD", 'takeloan')

        # Try withdraw to break 50% DFI
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id, "0.10000000@DFI", 'withdraw')
        # Try withdraw DUSD
        self.takeloan_withdraw(vault_id, "0.10000000@DUSD", 'withdraw')

        self.rollback_to(block_height)
        self.rollback_checks([vault_id])

    def pre_FCH_DFI_minimum_check_takeloan(self):
        block_height = self.nodes[0].getblockcount()

        vault_id = self.new_vault('LOAN1', ["10.00000000@DFI", "100.00000000@BTC"])
        self.takeloan_withdraw(vault_id, "1.00000000@TSLA", 'takeloan')

        vault_id_1 = self.new_vault('LOAN1', ["9.00000000@DFI", "100.00000000@BTC"])
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id_1, "1.00000000@TSLA", 'takeloan')

        # Check after depositting DFI check passess
        self.nodes[0].deposittovault(vault_id_1, self.account0, '100@DFI')
        self.nodes[0].generate(1)

        self.takeloan_withdraw(vault_id_1, "1.00000000@TSLA", 'takeloan')

        self.rollback_to(block_height)
        self.rollback_checks([vault_id, vault_id_1])

    def pre_FCR_DFI_minimum_check_takeloan(self):
        block_height = self.nodes[0].getblockcount()

        self.goto_fch_height()

        vault_id = self.new_vault('LOAN1', ["3.74000000@DFI", "200.00000000@BTC"])
        self.takeloan_withdraw(vault_id, "1.00000000@TSLA", 'takeloan')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id, "4.00000000@TSLA", 'takeloan')

        # Check after depositting DFI check passess
        self.nodes[0].deposittovault(vault_id, self.account0, '0.011@DFI')
        self.nodes[0].generate(1)

        self.takeloan_withdraw(vault_id, "4.00000000@TSLA", 'takeloan')

        # Check DUSD does not add to collateral
        vault_id_1 = self.new_vault('LOAN1', ["3.00000000@DFI", "200.00000000@BTC", "7.50000000@DUSD"])
        self.takeloan_withdraw(vault_id_1, "1.00000000@TSLA", 'takeloan')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id_1, "4.00000000@TSLA", 'takeloan')

        # Check after depositting DFI check passess
        self.nodes[0].deposittovault(vault_id_1, self.account0, '5000.00000000@DUSD')
        self.nodes[0].generate(1)

        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id_1, "4.00000000@TSLA", 'takeloan')

        self.nodes[0].deposittovault(vault_id_1, self.account0, '100@DFI')
        self.nodes[0].generate(1)

        self.takeloan_withdraw(vault_id_1, "1.00000000@TSLA", 'takeloan')

        self.rollback_to(block_height)
        self.rollback_checks([vault_id, vault_id_1])


    def post_FCR_DFI_minimum_check_takeloan(self):
        block_height = self.nodes[0].getblockcount()

        self.goto_fcr_height()

        vault_id = self.new_vault('LOAN1', ["3.74000000@DFI", "200.00000000@BTC"])
        self.takeloan_withdraw(vault_id, "1.00000000@TSLA", 'takeloan')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT,
                                self.takeloan_withdraw, vault_id, "4.00000000@TSLA", 'takeloan')

        # Check after depositting DFI check passess
        self.nodes[0].deposittovault(vault_id, self.account0, '0.011@DFI')
        self.nodes[0].generate(1)

        self.takeloan_withdraw(vault_id, "4.00000000@TSLA", 'takeloan')

        # Check DUSD also adds to collateral
        vault_id_1 = self.new_vault('LOAN1', ["3.00000000@DFI", "200.00000000@BTC", "7.50000000@DUSD"])
        self.takeloan_withdraw(vault_id_1, "1.00000000@TSLA", 'takeloan')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT,
                                self.takeloan_withdraw, vault_id_1, "4.00000000@TSLA", 'takeloan')

        # Check after depositting DFI check passess
        self.nodes[0].deposittovault(vault_id_1, self.account0, '0.11@DUSD')
        self.nodes[0].generate(1)

        self.takeloan_withdraw(vault_id_1, "1.00000000@TSLA", 'takeloan')

        self.rollback_to(block_height)
        self.rollback_checks([vault_id, vault_id_1])

    def post_FCE_DFI_minimum_check_takeloan(self):
        block_height = self.nodes[0].getblockcount()

        self.goto_fce_height()

        vault_id = self.new_vault('LOAN1', ["200.00000000@BTC", "37.40000000@DUSD"])
        self.takeloan_withdraw(vault_id, "1.00000000@TSLA", 'takeloan')
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id, "4.00000000@TSLA", 'takeloan')

        # Check after depositting DFI check passess
        self.nodes[0].deposittovault(vault_id, self.account0, '0.11@DUSD')
        self.nodes[0].generate(1)

        self.takeloan_withdraw(vault_id, "4.00000000@TSLA", 'takeloan')

        vault_id_1 = self.new_vault('LOAN1', ["200.00000000@BTC", "37.40000000@DUSD"])
        assert_raises_rpc_error(-32600,
                                ERR_STRING_MIN_COLLATERAL_DFI_PCT,
                                self.takeloan_withdraw, vault_id_1, "1.00000000@DUSD", 'takeloan')

        self.nodes[0].deposittovault(vault_id_1, self.account0, '1@DFI')
        self.nodes[0].generate(1)
        self.takeloan_withdraw(vault_id_1, "4.00000000@DUSD", 'takeloan')

        self.rollback_to(block_height)
        self.rollback_checks([vault_id, vault_id_1])



    def run_test(self):
        # Initial set up
        self.setup()
        self.update_oracle_price()

        self.pre_FCH_DFI_minimum_check_takeloan()
        self.pre_FCH_DFI_minimum_check_withdraw()
        self.update_oracle_price()
        self.pre_FCR_DFI_minimum_check_takeloan()
        self.pre_FCR_DFI_minimum_check_withdraw()
        self.update_oracle_price()
        self.post_FCR_DFI_minimum_check_takeloan()
        self.post_FCR_DFI_minimum_check_withdraw()
        self.update_oracle_price()
        self.post_FCE_DFI_minimum_check_takeloan()

        # self.post_FCE_DFI_minimum_check_withdraw()

        # TODO
        # test passes, update_oracle_price() makes it fail
        # File "/Users/diegodelcorral/workspace/fce-no-dusd-loop/test/functional/test_framework/test_framework.py", line 421, in rollback_to
        # self._rollback_to(block)
        # File "/Users/diegodelcorral/workspace/fce-no-dusd-loop/test/functional/test_framework/test_framework.py", line 414, in _rollback_to
        # node.invalidateblock(blockhash)

if __name__ == '__main__':
    DUSDLoanTests().main()
