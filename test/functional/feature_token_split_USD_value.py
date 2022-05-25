#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

# TODO
# Multiple pools with split stock OK
# Both sides with split token
# Two splits same height
# MINIMUM_LIQUIDITY 1000sats
# Merge with one side bigger in value max_limit
"""Test token split"""

from re import split
from sys import breakpointhook
from test_framework.test_framework import DefiTestFramework

from decimal import Decimal
import time
import random

def truncate(str, decimal):
    return str if not str.find('.') + 1 else str[:str.find('.') + decimal + 1]

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

    def generateAndFillAccounts(self, nAccounts=20):
        self.accounts = []
        for _ in range(nAccounts):
            self.accounts.append(self.nodes[0].getnewaddress())
        totalDUSD = 10000000
        totalT1 = 10000000
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
        self.generateAndFillAccounts()

    def addTotalAccountToLiquidityPool(self):
        print(f'Adding liquidity with {len(self.accounts)} accounts...')
        size = 1000000
        for account in self.accounts:
            totalAmount = Decimal(self.getAmountFromAccount(account, self.symbolDUSD))
            while size >= 10:
                while Decimal(totalAmount) >= size:
                    tmpAmount = Decimal(random.randint(int(size/10), int(size-1)))
                    self.nodes[0].addpoolliquidity({account: [str(tmpAmount)+"@T1", str(tmpAmount)+"@DUSD"]}, account)
                    self.nodes[0].generate(1)
                    totalAmount -= tmpAmount
                size /= 10
            finalAmount = Decimal(self.getAmountFromAccount(account, self.symbolDUSD))
            self.nodes[0].addpoolliquidity({account: [str(finalAmount)+"@T1", str(finalAmount)+"@DUSD"]}, account)
            self.nodes[0].generate(1)
            totalAmount -= finalAmount
            print(f'account {account} finished')

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

        self.addTotalAccountToLiquidityPool()

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
    def oracleSplit(self):
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDUSD}"},
            {"currency": "USD", "tokenAmount": f"0.05@{self.symbolT1}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

    # Make the split and return split height for revert if needed
    def split(self, tokenId, keepLocked=False, oracleSplit=False, multiplier=2):
        tokenSymbol = self.getTokenSymbolFromId(tokenId)
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{tokenId}':'true'}})
        self.nodes[0].generate(1)

        if oracleSplit:
            self.oracleSplit()

        # Token split
        splitHeight = self.nodes[0].getblockcount() + 2
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(splitHeight)}':f'{tokenId}/{multiplier}'}})
        self.nodes[0].generate(2)

        tokenId = list(self.nodes[0].gettoken(tokenSymbol).keys())[0]


        if not keepLocked:
            self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{tokenId}':'false'}})
            self.nodes[0].generate(1)

        return splitHeight

    def remove_from_pool(self, account):
        amountLP = self.getAmountFromAccount(account, "T1-DUSD")
        self.nodes[0].removepoolliquidity(account, amountLP+"@T1-DUSD", [])
        self.nodes[0].generate(1)


    def save_current_usd_value(self):
        self.value_accounts_pre_split = []
        activePriceT1 = self.nodes[0].getfixedintervalprice(f"{self.symbolT1}/USD")["activePrice"]
        activePriceDUSD = self.nodes[0].getfixedintervalprice(f"{self.symbolT1}/USD")["activePrice"]
        for account in self.accounts:
            amounts = {}
            self.remove_from_pool(account)
            accountInfo = self.nodes[0].getaccount(account)
            amounts["DUSD"] = Decimal(self.getAmountFromAccount(account, "DUSD")) * Decimal(activePriceDUSD)
            amounts["T1"] = Decimal(self.getAmountFromAccount(account, "T1")) *Decimal(activePriceT1)
            self.value_accounts_pre_split.append(amounts)

    def getTokenSymbolFromId(self, tokenId):
        token = self.nodes[0].gettoken(tokenId)
        tokenSymbol = token[str(tokenId)]["symbol"].split('/')[0]
        return tokenSymbol

    # Returns a list of pool token ids in which token is present
    def getTokenPools(self, tokenId):
        tokenSymbol = self.getTokenSymbolFromId(tokenId)
        tokenPools = {}
        currentPools = self.nodes[0].listpoolpairs()
        for pool in currentPools:
            if tokenSymbol in currentPools[pool]["symbol"] and currentPools[pool]["status"]:
                tokenPools[pool] = currentPools[pool]
        assert(len(tokenPools) > 0)
        return tokenPools

    def getAmountFromAccount(self, account, symbol):
        amounts = self.nodes[0].getaccount(account)
        amountStr = '0'
        for amount in amounts:
            amountSplit = amount.split('@')
            if symbol == amountSplit[1]:
                amountStr = amountSplit[0]
        return amountStr


    def run_test(self):
        self.setup()
        initialStateBlock = self.nodes[0].getblockcount()
        self.save_current_usd_value()
        self.split(self.idT1, oracleSplit=True, multiplier=20)
        # stop here to see activePrice = 0
        activePriceT1 = self.nodes[0].getfixedintervalprice(f"{self.symbolT1}/USD")["activePrice"]
        print(activePriceT1)
        breakpoint()


if __name__ == '__main__':
    TokenSplitUSDValueTest().main()

