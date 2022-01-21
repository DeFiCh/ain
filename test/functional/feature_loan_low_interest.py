#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - interest test."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from test_framework.authproxy import JSONRPCException

import calendar
import time
from decimal import Decimal, getcontext, ROUND_UP

FCH_HEIGHT = 1200
BLOCKS_PER_DAY = Decimal('2880')
class Amount():
    def __init__(self, *args):
        if len(args) == 2:
            self._quantity = Decimal(args[0])
            self._symbol =args[1]
        elif len(args) == 1:
            split_amount = args[0].split('@')
            self._quantity = Decimal(split_amount[0])
            self._symbol = split_amount[1]
        else:
            raise Exception("Wrong number of arguments")

    @property
    def quantity(self):
        return self._quantity

    @property
    def symbol(self):
        return self._symbol

    def is_same_token(self, other):
        return self.symbol == other.symbol

    def __add__(self, other):
        if self.symbol != other.symbol:
            raise Exception("Cant add Amounts of different symbol")
        total = self._quantity + other.quantity
        return Amount(total, self.symbol)

    def __iadd__(self, other):
        if self.symbol != other.symbol:
            raise Exception("Cant add Amounts of different symbol")
        self._quantity += other.quantity
        return self

    def __eq__(self, other):
        return self.quantity == other.quantity and self.symbol == other.symbol

    def __str__(self):
        return str(self.quantity) + "@" + self.symbol
    def __repr__(self):
        return str(self.quantity) + "@" + self.symbol

class Loan():
    def __init__(self, node, amount: Amount, vault_id):
        self._node = node
        self._amount: Amount = amount
        self._vault_id = vault_id
        self._height = self.node.getblockcount()+1
        self.__fetch_total_interest()
        self.__takeLoan()
        self.__calculate_ipb()

    @property
    def amount(self):
        return self._amount

    @property
    def node(self):
        return self._node

    @property
    def vault_id(self):
        return self._vault_id

    @property
    def height(self):
        return self._height

    @property
    def total_interest(self):
        return self._total_interest

    @property
    def ipb_pre_fork(self):
        return self._ipb_pre_fork

    @property
    def ipb_post_fork(self):
        return self._ipb_post_fork

    def __calculate_ipb(self):
        self._ipb_post_fork = self.amount.quantity * ((self.total_interest) / (BLOCKS_PER_DAY * 365))
        self._ipb_pre_fork = self.ipb_post_fork.quantize(Decimal('1E-8'), rounding=ROUND_UP)


    def __fetch_total_interest(self):
        vault = self.node.getvault(self.vault_id)
        scheme_id = vault["loanSchemeId"]
        loan_scheme = self.node.getloanscheme(scheme_id)
        loan_interest = loan_scheme["interestrate"]
        token = self.node.getloantoken(self.amount.symbol)
        token_interest = token["interest"]
        self._total_interest = (loan_interest + token_interest) / 100

    def __takeLoan(self):
        amount_str = str(self.amount)
        self.node.takeloan({
                    'vaultId': self.vault_id,
                    'amounts': amount_str})

    def get_total_interest(self):
        current_height = self.node.getblockcount()
        if current_height < FCH_HEIGHT:
            loan_blocks = current_height - self.height + 1
            return Amount(loan_blocks * self.ipb_pre_fork, self.amount.symbol)
        else:
            loan_blocks_pre = 0
            total_interest_pre = 0
            loan_blocks_post = current_height - self.height + 1
            if self.height < FCH_HEIGHT:
                loan_blocks_pre = FCH_HEIGHT - self.height + 1
                total_interest_pre = loan_blocks_pre * self.ipb_pre_fork
                loan_blocks_post = current_height - FCH_HEIGHT
            total_interest_post = loan_blocks_post * self.ipb_post_fork
            total = total_interest_pre + total_interest_post
            return Amount(total, self.amount.symbol)

    def payback(self):
        total_payback = self.get_total_interest() + self.amount
        self._amount = Amount(0, self._amount.symbol)
        self.__calculate_ipb()
        return total_payback



class LowInterestTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=2', '-fortcanningheight=3', '-fortcanningmuseumheight=4', '-fortcanningparkheight=5', f'-fortcanninghillheight={FCH_HEIGHT}', '-jellyfish_regtest=1', '-txindex=1', '-simulatemainnet']
        ]

    account0 = ''
    symbolDFI = "DFI"
    symbolDOGE = "DOGE"
    symboldUSD = "DUSD"
    idDFI = 0
    iddUSD = 0
    idDOGE = 0
    tokenInterest = 0
    loanInterest = 0
    vault_loans = {}

    getcontext().prec = 8

    def setup(self):
        print('Generating initial chain...')
        self.nodes[0].generate(100) # get initial UTXO balance from immature to trusted -> check getbalances()
        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # UTXO -> token
        self.nodes[0].utxostoaccount({self.account0: "199999900@" + self.symbolDFI})
        self.nodes[0].generate(1)

        # Setup oracles
        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "DOGE"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)
        oracle1_prices = [{"currency": "USD", "tokenAmount": "0.0001@DOGE"},
                          {"currency": "USD", "tokenAmount": "10@DFI"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        # Setup loan tokens
        self.nodes[0].setloantoken({
                    'symbol': self.symboldUSD,
                    'name': "DUSD stable token",
                    'fixedIntervalPriceId': "DUSD/USD",
                    'mintable': True,
                    'interest': 0})

        self.tokenInterest = Decimal(1)
        self.nodes[0].setloantoken({
                    'symbol': self.symbolDOGE,
                    'name': "DOGE token",
                    'fixedIntervalPriceId': "DOGE/USD",
                    'mintable': True,
                    'interest': Decimal(self.tokenInterest*100)})
        self.nodes[0].generate(1)

        # Set token ids
        self.iddUSD = list(self.nodes[0].gettoken(self.symboldUSD).keys())[0]
        self.idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]
        self.idDOGE = list(self.nodes[0].gettoken(self.symbolDOGE).keys())[0]

        # Mint tokens
        self.nodes[0].minttokens("1000000@DOGE")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("2000000@" + self.symboldUSD) # necessary for pools
        self.nodes[0].generate(1)

        # Setup collateral tokens
        self.nodes[0].setcollateraltoken({
                    'token': self.idDFI,
                    'factor': 1,
                    'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(300)

        # Setup pools
        poolOwner = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].createpoolpair({
            "tokenA": self.iddUSD,
            "tokenB": self.idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        }, [])

        self.nodes[0].createpoolpair({
            "tokenA": self.iddUSD,
            "tokenB": self.idDOGE,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DOGE",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["1000000@" + self.symboldUSD, "1000000@" + self.symbolDFI]
        }, self.account0, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.account0: ["1000000@" + self.symboldUSD, "1000@" + self.symbolDOGE]
        }, self.account0, [])
        self.nodes[0].generate(1)
        self.loanInterest = Decimal(1)
        # Create loan schemes and vaults
        self.nodes[0].createloanscheme(150, Decimal(self.loanInterest*100), 'LOAN150')
        self.nodes[0].generate(1)

    def get_new_vault(self, loan_scheme=None, amount=10000):
        vault_id = self.nodes[0].createvault(self.account0, loan_scheme)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vault_id, self.account0, str(amount)+"@DFI") # enough collateral to take loans
        self.nodes[0].generate(1)
        return vault_id

    def test_loan(self, amount, loan_token, vault_id, blocks=100):
        is_post_FCH = self.nodes[0].getblockcount() > FCH_HEIGHT
        loan = Loan(node=self.nodes[0],
                    amount=Amount(Decimal(amount), loan_token),
                    vault_id=vault_id)
        if vault_id not in self.vault_loans:
            self.vault_loans[vault_id] = []

        self.vault_loans[vault_id].append(loan)

        # Generate interest over n blocks
        self.nodes[0].generate(blocks)

        vault = self.nodes[0].getvault(loan.vault_id)

        interest_amounts_dict = {}
        for loan in self.vault_loans[vault_id]:
            if loan.amount.symbol not in interest_amounts_dict:
                interest_amounts_dict[loan.amount.symbol] = loan.get_total_interest()
            else:
                interest_amounts_dict[loan.amount.symbol] += loan.get_total_interest()

        if is_post_FCH:
            for key in interest_amounts_dict:
                interest_amounts_dict[key]= Amount(interest_amounts_dict[key].quantity.quantize(Decimal('1E-8'), rounding=ROUND_UP), key)

        returnedInterest = None
        for interest_amount in vault["interestAmounts"]:
            if loan_token in interest_amount:
                returnedInterest = Amount(interest_amount)
        assert_equal(returnedInterest, interest_amounts_dict[loan_token])

        return loan.get_total_interest()

    def go_to_FCH(self):
        blocksUntilFCH = FCH_HEIGHT - self.nodes[0].getblockcount() + 2
        self.nodes[0].generate(blocksUntilFCH)
        blockChainInfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockChainInfo["softforks"]["fortcanninghill"]["active"], True)

    # Payback full loan and test vault keeps empty loan and interest amounts
    def payback_vault(self, vault_id, loan_token_symbol='DOGE'):
        is_post_FCH = self.nodes[0].getblockcount() > FCH_HEIGHT
        payback_amount = Amount(Decimal(0), loan_token_symbol)
        for loan in self.vault_loans[vault_id]:
            if loan.amount.symbol == loan_token_symbol:
                payback_amount += loan.payback()
        if is_post_FCH:
            payback_amount = Amount(payback_amount.quantity.quantize(Decimal('1E-8'), rounding=ROUND_UP), loan_token_symbol)

        vault = self.nodes[0].getvault(vault_id)
        vault_token_amount = None
        for amount in vault["loanAmounts"]:
            if loan_token_symbol in amount:
                vault_token_amount = Amount(amount)
        assert_equal(payback_amount, vault_token_amount)

        self.nodes[0].paybackloan({
                        'vaultId': vault_id,
                        'from': self.account0,
                        'amounts': str(payback_amount)})
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(vault_id)
        # Check loan is fully payed back
        for amount in vault["loanAmounts"]:
            assert(loan_token_symbol not in amount)
        # Generate blocks and check again
        self.nodes[0].generate(100)
        vault = self.nodes[0].getvault(vault_id)
        # Check loan is fully payed back
        for amount in vault["loanAmounts"]:
            assert(loan_token_symbol not in amount)

    def run_test(self):
        self.setup()

        # PRE FCH
        # LOAN0: IPB(Interest Per Block) is 0.25Sat so it is rounded to 1Sat and added in each block
        vault_id_0 = self.get_new_vault()
        interest0 = self.test_loan(Decimal('0.001314'), self.symbolDOGE, vault_id_0)

        # LOAN1: IPB is 25Sat, rounding DOES NOT affect calculations they should be the same as post-fork
        vault_id_1 = self.get_new_vault()
        interest1 = self.test_loan(Decimal('0.1314'), self.symbolDOGE, vault_id_1)

        self.payback_vault(vault_id_1)

        # LOAN2: 100@DOGE loan, IPB rounding DOES affect calculations they should be DIFFERENT as post-fork
        vault_id_2 = self.get_new_vault()
        interest2 = self.test_loan(Decimal('100'), self.symbolDOGE, vault_id_2)

        # limit tests pre
        vault_id_a = self.get_new_vault()
        self.test_loan(Decimal('0.00000001'), self.symbolDOGE, vault_id_a)
        vault_id_b = self.get_new_vault()
        self.test_loan(Decimal('0.00000001'), self.symboldUSD, vault_id_b)
        vault_id_c = self.get_new_vault()
        self.test_loan(Decimal('2345.225'), self.symbolDOGE, vault_id_c)

        self.go_to_FCH()

        # POST FCH
        # LOAN3: IPB is 0.25Sat only TOTAL interest is ceiled
        vault_id_3 = self.get_new_vault()
        interest3 = self.test_loan(Decimal('0.001314'), self.symbolDOGE, vault_id_3)
        assert(interest3 != interest0)

        # LOAN4: IPB is 25Sat, rounding does NOT affect calculations they should be the same as pre FCH
        vault_id_4 = self.get_new_vault()
        interest4 = self.test_loan(Decimal('0.1314'), self.symbolDOGE, vault_id_4)
        assert_equal(interest4, interest1)

        # LOAN5: 100@DOGE loan, IPB rounding DOES affect calculations they should be DIFFERENT from pre-fork in LOAN2
        vault_id_5 = self.get_new_vault()
        interest5 = self.test_loan(Decimal('100'), self.symbolDOGE, vault_id_5)
        assert(interest5 != interest2)

        # LOAN6: take loan on existing vault with one loan already taken pre fork
        self.test_loan(Decimal('0.001314'), self.symbolDOGE, vault_id_0)

        self.payback_vault(vault_id_0)

        # Test if after paying loan interest keep being added
        self.nodes[0].generate(100)
        vault_id_6 = self.get_new_vault()
        interest7 = self.test_loan(Decimal('0.001314'), self.symbolDOGE, vault_id_0)
        interest8 = self.test_loan(Decimal('0.001314'), self.symbolDOGE, vault_id_6)
        assert_equal(interest7, interest8)

        getcontext().prec = 20
        # limit tests post
        # low ammount of loan
        vault_id_x = self.get_new_vault(amount=198000000)
        self.test_loan(Decimal('0.00000001'), self.symbolDOGE, vault_id_x, 241)
        try:
            self.test_loan(Decimal('0.000000009'), self.symbolDOGE, vault_id_x)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Invalid amount' in errorString)

        self.payback_vault(vault_id_x)

        # hig ammount of loan
        self.test_loan(Decimal('999999999'), self.symbolDOGE, vault_id_x, 314)
        self.nodes[0].generate(100)
        self.payback_vault(vault_id_x)
        try:
            self.test_loan(Decimal('9999999999'), self.symbolDOGE, vault_id_x)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Invalid amount' in errorString)

        self.test_loan(Decimal('0.00000001'), self.symboldUSD, vault_id_x, 241)
        self.test_loan(Decimal('123.34518'), self.symboldUSD, vault_id_2, 241)

        self.payback_vault(vault_id_2)


if __name__ == '__main__':
    LowInterestTest().main()
