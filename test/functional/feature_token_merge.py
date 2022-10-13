#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

"""Test token merge"""

from decimal import Decimal
import time

from test_framework.test_framework import DefiTestFramework

def truncate(str, decimal):
    return str if not str.find('.') + 1 else str[:str.find('.') + decimal + 1]

class TokenMergeTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningcrunchheight=1', '-greatworldheight=1', '-regtest-skip-loan-collateral-validation=1', '-subsidytest=1']]

    def setup_oracles(self):
        # Symbols
        self.symbolDUSD = 'DUSD'
        self.symbolDFI = 'DFI'
        self.symbolT1 = 'T1'
        self.symbolT2 = 'T2'
        self.symbolT3 = 'T3'

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolDFI},
            {"currency": "USD", "token": self.symbolDUSD},
            {"currency": "USD", "token": self.symbolT2},
            {"currency": "USD", "token": self.symbolT1},
            {"currency": "USD", "token": self.symbolT3},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDUSD}"},
            {"currency": "USD", "tokenAmount": f"3@{self.symbolDFI}"},
            {"currency": "USD", "tokenAmount": f"10000@{self.symbolT1}"},
            {"currency": "USD", "tokenAmount": f"100@{self.symbolT2}"},
            {"currency": "USD", "tokenAmount": f"0.00000001@{self.symbolT3}"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

    def setup_tokens(self):
        # Set loan tokens
        self.nodes[0].setloantoken({
            'symbol': self.symbolT2,
            'name': self.symbolT2,
            'fixedIntervalPriceId': f"{self.symbolT2}/USD",
            "isDAT": True,
            'interest': 0
        })
        self.nodes[0].generate(1)

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

        self.nodes[0].setloantoken({
            'symbol': self.symbolT3,
            'name': self.symbolT3,
            'fixedIntervalPriceId': f"{self.symbolT3}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
            'token': self.symbolDFI,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolDFI}/USD"
        })
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': self.symbolDUSD,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolDUSD}/USD"
        })
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': self.symbolT2,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolT2}/USD"
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
        self.idT2 = list(self.nodes[0].gettoken(self.symbolT2).keys())[0]
        self.idT3 = list(self.nodes[0].gettoken(self.symbolT3).keys())[0]

    def setup_accounts(self):
        self.account1 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.account2 = self.nodes[0].getnewaddress()
        self.account3 = self.nodes[0].getnewaddress()

        self.nodes[0].utxostoaccount({self.account1: "100000@DFI"})
        self.nodes[0].generate(1)

        self.nodes[0].minttokens("110300001@DUSD")
        self.nodes[0].minttokens("110000@T1")
        self.nodes[0].minttokens("205000@T2")
        self.nodes[0].minttokens("100000000@T3")
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].accounttoaccount(self.account1, {self.account2: ["55039700.499@DUSD", "49900@DFI", "54890@T1", "102295@T2", "49900000@T3"]})
        self.nodes[0].accounttoaccount(self.account1, {self.account3: ["110300.0010@DUSD", "100@DFI", "110@T1", "205@T2", "100000@T3"]})
        self.nodes[0].generate(1)
        self.sync_blocks()

    def setup_pools(self):
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDFI,
            "tokenB": self.symbolDUSD,
            "commission": 0.01,
            "status": True,
            "ownerAddress": self.account1,
        })
        self.nodes[0].generate(1)
        self.symbolDFI_DUSD = "DFI-DUSD"
        self.idDFI_DUSD = list(self.nodes[0].gettoken(self.symbolDFI_DUSD).keys())[0]

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolT1,
            "tokenB": self.symbolDUSD,
            "commission": 0.01,
            "status": True,
            "ownerAddress": self.account1,
        })
        self.nodes[0].generate(1)
        self.symbolT1_DUSD = "T1-DUSD"
        self.idT1_DUSD = list(self.nodes[0].gettoken(self.symbolT1_DUSD).keys())[0]

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolT2,
            "tokenB": self.symbolDUSD,
            "commission": 0.05,
            "status": True,
            "ownerAddress": self.account1,
        })
        self.nodes[0].generate(1)
        self.symbolT2_DUSD = "T2-DUSD"
        self.idT2_DUSD = list(self.nodes[0].gettoken(self.symbolT2_DUSD).keys())[0]

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolT3,
            "tokenB": self.symbolDUSD,
            "commission": 0.01,
            "status": True,
            "ownerAddress": self.account1,
        })
        self.nodes[0].generate(1)
        self.symbolT3_DUSD = "T3-DUSD"
        self.idT3_DUSD = list(self.nodes[0].gettoken(self.symbolT3_DUSD).keys())[0]

        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolT1,
            "tokenB": self.symbolT2,
            "commission": 0.001,
            "status": True,
            "ownerAddress": self.account1,
        })
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.symbolT1_T2 = "T1-T2"
        self.idT1_T2 = list(self.nodes[0].gettoken(self.symbolT1_T2).keys())[0]


        # Add liquidity
        for _ in range(10):
            self.nodes[0].addpoolliquidity({self.account1: ["5000@DFI", "15000@DUSD"]}, self.account1)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account2: ["4990@DFI", "14970@DUSD"]}, self.account2)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account3: ["10@DFI", "30@DUSD"]}, self.account3)
            self.nodes[0].generate(1)

        for _ in range(10):
            self.nodes[0].addpoolliquidity({self.account1: ["500@T1", "5000000@DUSD"]}, self.account1)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account2: ["499@T1", "4990000@DUSD"]}, self.account2)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account3: ["1@T1", "10000@DUSD"]}, self.account3)
            self.nodes[0].generate(1)

        for _ in range(10):
            self.nodes[0].addpoolliquidity({self.account1: ["10000@T2", "500000@DUSD"]}, self.account1)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account2: ["9980@T2", "499000@DUSD"]}, self.account2)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account3: ["20@T2", "1000@DUSD"]}, self.account3)
            self.nodes[0].generate(1)

        for _ in range(10):
            self.nodes[0].addpoolliquidity({self.account1: ["5000000@T3", "0.05@DUSD"]}, self.account1)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account2: ["4990000@T3", "0.0499@DUSD"]}, self.account2)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account3: ["10000@T3", "0.0001@DUSD"]}, self.account3)
            self.nodes[0].generate(1)

        for _ in range(10):
            self.nodes[0].addpoolliquidity({self.account1: ["5000@T1", "250@T2"]}, self.account1)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account2: ["4990@T1", "249.5@T2"]}, self.account2)
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({self.account3: ["10@T1", "0.5@T2"]}, self.account3)
            self.nodes[0].generate(1)

    def setup(self):
        self.nodes[0].generate(101)
        self.setup_oracles()
        self.setup_tokens()
        self.setup_accounts()
        self.setup_pools()

    # Make the split and return split height for revert if needed
    def merge(self, tokenId, keepLocked=False):
        tokenSymbol = self.getTokenSymbolFromId(tokenId)
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{tokenId}':'true'}})
        self.nodes[0].generate(1)

        # Token split
        splitHeight = self.nodes[0].getblockcount() + 2
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(splitHeight)}':f'{tokenId}/-2'}})
        self.nodes[0].generate(2)

        tokenId = list(self.nodes[0].gettoken(tokenSymbol).keys())[0]
        if not keepLocked:
            self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{tokenId}':'false'}})
            self.nodes[0].generate(1)

        return splitHeight

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

    def check_attributes_on_merge(self, tokenId, revert=False):
        tokenSymbol = self.getTokenSymbolFromId(tokenId)
        revert_block = self.nodes[0].getblockcount()
        pools = self.getTokenPools(tokenId)
        poolsSymbol = []
        for pool in pools:
            poolsSymbol.append(self.getTokenSymbolFromId(pool))

        # set LP and Tokens gov vars
        for poolId in pools:
            self.nodes[0].setgov({"ATTRIBUTES":{f'v0/poolpairs/{poolId}/token_a_fee_pct': '0.01',
                                                f'v0/poolpairs/{poolId}/token_b_fee_pct': '0.03'}})

        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{tokenId}/dex_in_fee_pct': '0.02',
                                            f'v0/token/{tokenId}/dex_out_fee_pct': '0.005'}})
        self.nodes[0].generate(1)

        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert(f'v0/token/{tokenId}/dex_in_fee_pct' in result)
        assert(f'v0/token/{tokenId}/dex_out_fee_pct' in result)
        for poolId in pools:
            assert(f'v0/poolpairs/{poolId}/token_a_fee_pct' in result)
            assert(f'v0/poolpairs/{poolId}/token_b_fee_pct' in result)

        splitHeight = self.merge(tokenId) - 2
        self.nodes[0].generate(1)

        new_token_id = list(self.nodes[0].gettoken(tokenSymbol).keys())[0]
        new_pools = []
        for poolSymbol in poolsSymbol:
            new_pools.append(list(self.nodes[0].gettoken(poolSymbol).keys())[0])

        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']

        for poolId in pools:
            assert(f'v0/poolpairs/{poolId}/token_a_fee_pct' not in result)
            assert(f'v0/poolpairs/{poolId}/token_b_fee_pct' not in result)
        assert(f'v0/token/{tokenId}/dex_in_fee_pct' not in result)
        assert(f'v0/token/{tokenId}/dex_out_fee_pct' not in result)

        for new_pool_id in new_pools:
            assert(f'v0/poolpairs/{new_pool_id}/token_a_fee_pct' in result)
            assert(f'v0/poolpairs/{new_pool_id}/token_b_fee_pct' in result)
        assert(f'v0/token/{new_token_id}/dex_in_fee_pct' in result)
        assert(f'v0/token/{new_token_id}/dex_out_fee_pct' in result)

        if not revert:
            return new_token_id
        else:
            self.rollback_to(splitHeight)
            result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
            for poolId in pools:
                assert(f'v0/poolpairs/{poolId}/token_a_fee_pct' in result)
                assert(f'v0/poolpairs/{poolId}/token_b_fee_pct' in result)
            assert(f'v0/token/{tokenId}/dex_in_fee_pct' in result)
            assert(f'v0/token/{tokenId}/dex_out_fee_pct' in result)
            for new_pool_id in new_pools:
                assert(f'v0/poolpairs/{new_pool_id}/token_a_fee_pct' not in result)
                assert(f'v0/poolpairs/{new_pool_id}/token_b_fee_pct' not in result)
            assert(f'v0/token/{new_token_id}/dex_in_fee_pct' not in result)
            assert(f'v0/token/{new_token_id}/dex_out_fee_pct' not in result)

            self.rollback_to(revert_block)
            result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
            for new_pool_id in new_pools:
                assert(f'v0/poolpairs/{new_pool_id}/token_a_fee_pct' not in result)
                assert(f'v0/poolpairs/{new_pool_id}/token_b_fee_pct' not in result)
            assert(f'v0/token/{new_token_id}/dex_in_fee_pct' not in result)
            assert(f'v0/token/{new_token_id}/dex_out_fee_pct' not in result)
            return 0

    def getAmountFromAccount(self, account, symbol):
        amounts = self.nodes[0].getaccount(account)
        amountStr = '0'
        for amount in amounts:
            amountSplit = amount.split('@')
            if symbol == amountSplit[1]:
                amountStr = amountSplit[0]
        return amountStr

    def check_amounts_on_merge(self, poolId, tokenId, revert=False):
        self.nodes[0].generate(10)
        tokenSymbol = self.getTokenSymbolFromId(tokenId)
        poolSymbol = self.getTokenSymbolFromId(poolId)
        tokenBSymbol = poolSymbol.split('-')[0]
        if tokenBSymbol == tokenSymbol:
            tokenBSymbol = poolSymbol.split('-')[1]
        revertHeight = self.nodes[0].getblockcount()

        amountLPBeforeAcc1 = self.getAmountFromAccount(self.account1, poolSymbol)
        amountLPBeforeAcc2 = self.getAmountFromAccount(self.account2, poolSymbol)
        amountLPBeforeAcc3 = self.getAmountFromAccount(self.account3, poolSymbol)
        if amountLPBeforeAcc1 != '0':
            self.nodes[0].removepoolliquidity(self.account1, amountLPBeforeAcc1+"@"+poolSymbol, [])
            self.nodes[0].generate(1)
        if amountLPBeforeAcc2 != '0':
            self.nodes[0].removepoolliquidity(self.account2, amountLPBeforeAcc2+"@"+poolSymbol, [])
            self.nodes[0].generate(1)
        if amountLPBeforeAcc3 != '0':
            self.nodes[0].removepoolliquidity(self.account3, amountLPBeforeAcc3+"@"+poolSymbol, [])
            self.nodes[0].generate(1)

        amountTokenBeforeAcc1 = self.getAmountFromAccount(self.account1, tokenSymbol)
        amountTokenB_BeforeAcc1 = self.getAmountFromAccount(self.account1, tokenBSymbol)
        amountTokenBeforeAcc2 = self.getAmountFromAccount(self.account2, tokenSymbol)
        amountTokenB_BeforeAcc2 = self.getAmountFromAccount(self.account2, tokenBSymbol)
        amountTokenBeforeAcc3 = self.getAmountFromAccount(self.account3, tokenSymbol)
        amountTokenB_BeforeAcc3 = self.getAmountFromAccount(self.account3, tokenBSymbol)

        self.rollback_to(revertHeight)

        self.merge(tokenId)
        new_token_id = list(self.nodes[0].gettoken(tokenSymbol).keys())[0]

        amountLPAfterAcc1 = self.getAmountFromAccount(self.account1, poolSymbol)
        amountLPAfterAcc2 = self.getAmountFromAccount(self.account2, poolSymbol)
        amountLPAfterAcc3 = self.getAmountFromAccount(self.account3, poolSymbol)
        self.nodes[0].removepoolliquidity(self.account1, amountLPAfterAcc1+"@"+poolSymbol, [])
        self.nodes[0].generate(1)
        self.nodes[0].removepoolliquidity(self.account2, amountLPAfterAcc2+"@"+poolSymbol, [])
        self.nodes[0].generate(1)
        self.nodes[0].removepoolliquidity(self.account3, amountLPAfterAcc3+"@"+poolSymbol, [])
        self.nodes[0].generate(1)
        amountTokenAfterAcc1 = self.getAmountFromAccount(self.account1, tokenSymbol)
        amountTokenB_AfterAcc1 = self.getAmountFromAccount(self.account1, tokenBSymbol)
        amountTokenAfterAcc2 = self.getAmountFromAccount(self.account2, tokenSymbol)
        amountTokenB_AfterAcc2 = self.getAmountFromAccount(self.account2, tokenBSymbol)
        amountTokenAfterAcc3 = self.getAmountFromAccount(self.account3, tokenSymbol)
        amountTokenB_AfterAcc3 = self.getAmountFromAccount(self.account3, tokenBSymbol)
        # Check difference is not grater than 0,001% rounding difference
        assert((Decimal(amountTokenB_BeforeAcc1) - Decimal(amountTokenB_AfterAcc1)).copy_abs() <= (Decimal(0.00001)*Decimal(amountTokenB_BeforeAcc1)))
        assert((Decimal(amountTokenB_BeforeAcc2) - Decimal(amountTokenB_AfterAcc2)).copy_abs() <= (Decimal(0.00001)*Decimal(amountTokenB_BeforeAcc2)))
        assert((Decimal(amountTokenB_BeforeAcc3) - Decimal(amountTokenB_AfterAcc3)).copy_abs() <= (Decimal(0.00001)*Decimal(amountTokenB_BeforeAcc3)))
        assert(((Decimal(amountTokenBeforeAcc1)/2) - Decimal(amountTokenAfterAcc1)).copy_abs() <= Decimal(0.00001)*Decimal(amountTokenBeforeAcc1))
        assert(((Decimal(amountTokenBeforeAcc2)/2) - Decimal(amountTokenAfterAcc2)).copy_abs() <= Decimal(0.00001)*Decimal(amountTokenBeforeAcc2))
        assert(((Decimal(amountTokenBeforeAcc3)/2) - Decimal(amountTokenAfterAcc3)).copy_abs() <= Decimal(0.00001)*Decimal(amountTokenBeforeAcc3))

        if revert:
            self.rollback_to(revertHeight)
        return new_token_id

    def run_test(self):
        self.setup()
        self.check_attributes_on_merge(self.idT1, revert=True)
        self.check_attributes_on_merge(self.idT2, revert=True)
        self.check_attributes_on_merge(self.idT1, revert=True)
        self.check_attributes_on_merge(self.idT3, revert=True)
        self.idT3 = self.check_attributes_on_merge(self.idT3, revert=False)
        self.idT1 = self.check_attributes_on_merge(self.idT1, revert=False)
        self.idT2 = self.check_attributes_on_merge(self.idT2, revert=False)
        self.idT3 = self.check_attributes_on_merge(self.idT3, revert=False)

        # Second round split
        self.check_attributes_on_merge(self.idT1, revert=True)
        self.check_attributes_on_merge(self.idT2, revert=True)
        self.check_attributes_on_merge(self.idT1, revert=True)
        self.check_attributes_on_merge(self.idT3, revert=True)
        self.idT3 = self.check_attributes_on_merge(self.idT3, revert=False)
        self.idT1 = self.check_attributes_on_merge(self.idT1, revert=False)
        self.idT2 = self.check_attributes_on_merge(self.idT2, revert=False)
        self.idT3 = self.check_attributes_on_merge(self.idT3, revert=False)

        # Check amounts pre an dpost merge
        self.check_amounts_on_merge(self.idT1_DUSD, self.idT1, revert=True)
        self.check_amounts_on_merge(self.idT2_DUSD, self.idT2, revert=True)
        self.check_amounts_on_merge(self.idT3_DUSD, self.idT3, revert=True)
        self.check_amounts_on_merge(self.idT1_T2, self.idT2, revert=True)
        self.check_amounts_on_merge(self.idT1_T2, self.idT1, revert=True)
        self.idT1 = self.check_amounts_on_merge(self.idT1_DUSD, self.idT1, revert=False)
        self.idT2 = self.check_amounts_on_merge(self.idT2_DUSD, self.idT2, revert=False)
        self.idT3 = self.check_amounts_on_merge(self.idT3_DUSD, self.idT3, revert=False)
        self.idT2 = self.check_amounts_on_merge(self.idT1_T2, self.idT2, revert=False)

if __name__ == '__main__':
    TokenMergeTest().main()

