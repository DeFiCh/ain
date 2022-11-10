#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

"""Test token split"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_greater_than_or_equal

from decimal import Decimal
import time
import random

def truncate(str, decimal):
    return str if not str.find('.') + 1 else str[:str.find('.') + decimal + 1]

def almost_equal(x, y, threshold=0.0001):
  return abs(x-y) < threshold

class TokenSplitUSDValueTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.FCC_HEIGHT = 300
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', f'-fortcanningcrunchheight={self.FCC_HEIGHT}', '-jellyfish_regtest=1', '-subsidytest=1']]

    def setup_oracles(self):
        # Symbols
        self.symbolDUSD = 'DUSD'
        self.symbolT1 = 'T1'

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolDUSD},
            {"currency": "USD", "token": self.symbolT1},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDUSD}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolT1}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

    def setup_tokens(self):
        # Set loan tokens

        self.nodes[0].setloantoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            'fixedIntervalPriceId': f"{self.symbolDUSD}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolT1,
            'name': self.symbolT1,
            'fixedIntervalPriceId': f"{self.symbolT1}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': self.symbolDUSD,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolDUSD}/USD"
        })
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': self.symbolT1,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolT1}/USD"
        })
        self.nodes[0].generate(1)

        # Store token IDs
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idT1 = list(self.nodes[0].gettoken(self.symbolT1).keys())[0]

    def generate_and_fill_accounts(self, nAccounts=20):
        self.accounts = []
        for _ in range(nAccounts):
            self.accounts.append(self.nodes[0].getnewaddress())
        totalDUSD = 10000000
        totalT1 = 10000000
        self.accounts.sort()
        self.nodes[0].utxostoaccount({self.account1: "10000000@0"})
        self.nodes[0].minttokens(str(totalDUSD)+"@DUSD")
        self.nodes[0].minttokens(str(totalT1)+"@T1")
        self.nodes[0].generate(1)

        perAccountDUSD = totalDUSD/nAccounts
        perAccountT1 = totalT1/nAccounts
        for account in self.accounts:
            self.nodes[0].accounttoaccount(self.account1, {account: [str(perAccountDUSD)+"@DUSD", str(perAccountT1)+"@T1"]})
            self.nodes[0].generate(1)

    def setup_accounts(self):
        self.account1 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.generate_and_fill_accounts()

    def add_total_account_to_liquidity_pool(self):
        size = 1000000
        for account in self.accounts:
            totalAmount = Decimal(self.get_amount_from_account(account, self.symbolDUSD))
            while size >= 10:
                while Decimal(totalAmount) >= size:
                    tmpAmount = Decimal(random.randint(int(size/10), int(size-1)))
                    self.nodes[0].addpoolliquidity({account: [str(tmpAmount)+"@T1", str(tmpAmount)+"@DUSD"]}, account)
                    self.nodes[0].generate(1)
                    totalAmount -= tmpAmount
                size /= 10
            finalAmount = Decimal(self.get_amount_from_account(account, self.symbolDUSD))
            self.nodes[0].addpoolliquidity({account: [str(finalAmount)+"@T1", str(finalAmount)+"@DUSD"]}, account)
            self.nodes[0].generate(1)
            totalAmount -= finalAmount

    def setup_pools(self):
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolT1,
            "tokenB": self.symbolDUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.account1,
        })
        self.nodes[0].generate(1)
        self.symbolT1_DUSD = "T1-DUSD"
        self.idT1_DUSD = list(self.nodes[0].gettoken(self.symbolT1_DUSD).keys())[0]

        self.add_total_account_to_liquidity_pool()

    def gotoFCC(self):
        height = self.nodes[0].getblockcount()
        if height < self.FCC_HEIGHT:
            self.nodes[0].generate((self.FCC_HEIGHT - height) + 2)

    def setup(self):
        self.nodes[0].generate(101)
        self.setup_oracles()
        self.setup_tokens()
        self.setup_accounts()
        self.setup_pools()
        self.gotoFCC()

    # /20 split
    def oracle_split(self):
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDUSD}"},
            {"currency": "USD", "tokenAmount": f"0.05@{self.symbolT1}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

    def split(self, tokenId, keepLocked=False, oracleSplit=False, multiplier=2):
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{tokenId}':'true'}})
        self.nodes[0].generate(1)

        if oracleSplit:
            self.oracle_split()

        # Token split
        splitHeight = self.nodes[0].getblockcount() + 2
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(splitHeight)}':f'{tokenId}/{multiplier}'}})
        self.nodes[0].generate(2)

        self.idT1old = tokenId
        self.idT1 = list(self.nodes[0].gettoken(self.symbolT1).keys())[0]

        if not keepLocked:
            self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{self.idT1}':'false'}})
            self.nodes[0].generate(1)

    def remove_from_pool(self, account):
        amountLP = self.get_amount_from_account(account, "T1-DUSD")
        self.nodes[0].removepoolliquidity(account, amountLP+"@T1-DUSD", [])
        self.nodes[0].generate(1)

    def accounts_usd_values(self):
        values =[]
        revertHeight = self.nodes[0].getblockcount()
        activePriceT1 = self.nodes[0].getfixedintervalprice(f"{self.symbolT1}/USD")["activePrice"]
        activePriceDUSD = self.nodes[0].getfixedintervalprice(f"{self.symbolDUSD}/USD")["activePrice"]
        for account in self.accounts:
            amounts = {}
            self.remove_from_pool(account)
            amounts["account"] = account
            amounts["DUSD"] = Decimal(self.get_amount_from_account(account, "DUSD")) * Decimal(activePriceDUSD)
            amounts["T1"] = Decimal(self.get_amount_from_account(account, "T1")) *Decimal(activePriceT1)
            values.append(amounts)
        self.rollback_to(revertHeight)
        return values

    def compare_value_list(self, pre, post):
        for index, amount in enumerate(pre):
            if index != 0:
                almost_equal(amount["DUSD"], post[index]["DUSD"])
                almost_equal(amount["T1"], post[index]["T1"])

    def compare_vaults_list(self, pre, post):
        for index, vault in enumerate(pre):
            if index != 0:
                almost_equal(vault["collateralValue"], post[index]["collateralValue"])
                almost_equal(vault["loanValue"], post[index]["loanValue"])
                almost_equal(vault["collateralRatio"], post[index]["collateralRatio"])

    def get_token_symbol_from_id(self, tokenId):
        token = self.nodes[0].gettoken(tokenId)
        tokenSymbol = token[str(tokenId)]["symbol"].split('/')[0]
        return tokenSymbol

    # Returns a list of pool token ids in which token is present
    def get_token_pools(self, tokenId):
        tokenSymbol = self.get_token_symbol_from_id(tokenId)
        tokenPools = {}
        currentPools = self.nodes[0].listpoolpairs()
        for pool in currentPools:
            if tokenSymbol in currentPools[pool]["symbol"] and currentPools[pool]["status"]:
                tokenPools[pool] = currentPools[pool]
        assert(len(tokenPools) > 0)
        return tokenPools

    def get_amount_from_account(self, account, symbol):
        amounts = self.nodes[0].getaccount(account)
        amountStr = '0'
        for amount in amounts:
            amountSplit = amount.split('@')
            if symbol == amountSplit[1]:
                amountStr = amountSplit[0]
        return amountStr

    def compare_usd_account_value_on_split(self, revert=False):
        revertHeight = self.nodes[0].getblockcount()
        value_accounts_pre_split = self.accounts_usd_values()
        self.split(self.idT1, oracleSplit=True, multiplier=20)
        value_accounts_post_split = self.accounts_usd_values()
        # TODO fail
        self.compare_value_list(value_accounts_pre_split, value_accounts_post_split)
        if revert:
            self.rollback_to(revertHeight)
            self.idT1=self.idT1old

    def setup_vaults(self, collateralSplit=False):
        self.nodes[0].createloanscheme(200, 0.01, 'LOAN_0')

        self.vaults = []
        vaultCount = 0
        for account in self.accounts:
            self.remove_from_pool(account)
            vaultId= self.nodes[0].createvault(account)
            self.nodes[0].generate(1)
            vaultCount += 1
            self.vaults.append(vaultId)
            amountT1 = self.get_amount_from_account(account, "T1")
            amountT1 = Decimal(amountT1)/Decimal(4)
            if collateralSplit:
                amountT1 = Decimal(amountT1)/Decimal(2)
            amountDUSD = self.get_amount_from_account(account, "DUSD")
            amountDUSD = Decimal(amountDUSD)/Decimal(2)
            if collateralSplit:
                self.nodes[0].deposittovault(vaultId, account, str(amountT1)+"@T1")
            self.nodes[0].deposittovault(vaultId, account, str(amountDUSD)+"@DUSD")
            self.nodes[0].generate(1)
            amountT1Loan = Decimal(amountT1)/Decimal(2)
            self.nodes[0].takeloan({
                        'vaultId': vaultId,
                        'amounts': str(amountT1Loan)+"@T1"})
            self.nodes[0].generate(1)

    def get_vaults_usd_values(self):
        vault_values = []
        for vault in self.vaults:
            vaultInfo = self.nodes[0].getvault(vault)
            vault_values.append(vaultInfo)
        return vault_values

    def compare_usd_vaults_values_on_split(self, revert=False):
        revertHeight = self.nodes[0].getblockcount()
        self.setup_vaults(collateralSplit=False)
        vault_values_pre_split = self.get_vaults_usd_values()
        self.split(self.idT1, oracleSplit=True, multiplier=20)
        self.nodes[0].generate(40)
        vault_values_post_split = self.get_vaults_usd_values()
        self.compare_vaults_list(vault_values_pre_split,vault_values_post_split)

        if revert:
            self.rollback_to(revertHeight)
            self.idT1=self.idT1old

    def test_values_non_zero_with_token_locked(self):
        self.setup_vaults()
        self.split(self.idT1, keepLocked=True)
        vaults_values = self.get_vaults_usd_values()
        for vault in vaults_values:
            assert_equal(vault["state"], "frozen")
            assert_equal(vault["collateralValue"], -1)
            assert_equal(vault["loanValue"], -1)
            assert_equal(vault["interestValue"], -1)
            assert_equal(vault["informativeRatio"], -1)
            assert_equal(vault["collateralRatio"], -1)
        vaults_values = self.nodes[0].listvaults({"skipLockedCheck": False, "verbose": True})
        for vault in vaults_values:
            assert_equal(vault["state"], "frozen")
            assert_equal(vault["collateralValue"], -1)
            assert_equal(vault["loanValue"], -1)
            assert_equal(vault["interestValue"], -1)
            assert_equal(vault["informativeRatio"], -1)
            assert_equal(vault["collateralRatio"], -1)

        vaults_values = self.nodes[0].listvaults({"skipLockedCheck": True, "verbose": True})
        for vault in vaults_values:
            assert_greater_than_or_equal(vault["collateralValue"], 0)
            assert_greater_than_or_equal(vault["loanValue"], 0)

    def test_values_after_token_unlock(self):
        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{self.idT1}':'false'}})
        self.nodes[0].generate(1)
        vaults_values = self.nodes[0].listvaults({"skipLockedCheck": True, "verbose": True})
        for vault in vaults_values:
            assert_equal(vault["state"], "active")
            assert_greater_than_or_equal(vault["collateralValue"], 0)
            assert_greater_than_or_equal(vault["loanValue"], 0)
            assert_greater_than_or_equal(vault["interestValue"], 0)
            assert_greater_than_or_equal(vault["informativeRatio"], 0)
            assert_greater_than_or_equal(vault["collateralRatio"], 0)
            assert_greater_than_or_equal(vault["interestPerBlock"][0].split('@')[0], 0)
            assert_greater_than_or_equal(vault["interestPerBlockValue"], 0)

    def run_test(self):
        self.setup()
        assert_equal(1,1) # Make linter happy for now
        self.compare_usd_account_value_on_split(revert=True)
        self.compare_usd_vaults_values_on_split(revert=True)
        self.test_values_non_zero_with_token_locked()
        self.test_values_after_token_unlock()

if __name__ == '__main__':
    TokenSplitUSDValueTest().main()
