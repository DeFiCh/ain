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

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time
import random

def truncate(str, decimal):
    return str if not str.find('.') + 1 else str[:str.find('.') + decimal + 1]

class TokenSplitTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.GREAT_WORLD_HEIGHT = 300
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', f'-greatworldheight={self.GREAT_WORLD_HEIGHT}', '-subsidytest=1', '-jellyfish_regtest=1']]

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

    def gotoGW(self):
        height = self.nodes[0].getblockcount()
        if height < self.GREAT_WORLD_HEIGHT:
            self.nodes[0].generate((self.GREAT_WORLD_HEIGHT - height) + 2)

    def setup(self):
        self.nodes[0].generate(101)
        self.setup_oracles()
        self.setup_tokens()
        self.setup_accounts()
        self.setup_pools()
        self.gotoGW()


    def run_test(self):
        self.setup()

        self.check_tributes_on_split(self.idT1_DUSD, self.idT1, revert=True)
        self.check_tributes_on_split(self.idT2_DUSD, self.idT2, revert=True)
        self.check_tributes_on_split(self.idT1_T2, self.idT1, revert=True)
        self.check_tributes_on_split(self.idT3_DUSD, self.idT3, revert=True)
        self.check_tributes_on_split(self.idT3_DUSD, self.idT3, revert=False)

        self.token_split_T3()
        self.token_split_T1()
        self.setup_test_vaults()
        self.vault_split()

    def setup_test_vaults(self):
        # Create loan scheme
        self.nodes[0].createloanscheme(100, 0.1, 'LOAN0001')
        self.nodes[0].generate(1)

        # Fund address for vault creation
        self.nodes[0].utxostoaccount({self.account1: f'30000@{self.symbolDFI}'})
        self.nodes[0].generate(1)

        for _ in range(100):
            # Create vault
            vault_id = self.nodes[0].createvault(self.account1, '')
            self.nodes[0].generate(1)

            # Take 1 to 3 loans
            for _ in range(1, 4):
                # Deposit random collateral
                collateral = round(random.uniform(1, 100), 8)
                loan = truncate(str(collateral / 3), 8)
                self.nodes[0].deposittovault(vault_id, self.account1, f'{str(collateral)}@DFI')
                self.nodes[0].generate(1)

                # Take loan
                self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': f"{str(loan)}@{self.symbolT3}"
                })
                self.nodes[0].generate(1)

    def check_token_split(self, token_id, token_symbol, token_suffix, multiplier, minted, loan, collateral):

        # Check old token
        result = self.nodes[0].gettoken(token_id)[token_id]
        assert_equal(result['symbol'], f'{token_symbol}{token_suffix}')
        assert_equal(result['minted'], Decimal('0.00000000'))
        assert_equal(result['mintable'], False)
        assert_equal(result['tradeable'], False)
        assert_equal(result['finalized'], True)
        assert_equal(result['destructionTx'], self.nodes[0].getbestblockhash())
        assert_equal(result['destructionHeight'], self.nodes[0].getblockcount())

        # Check old token in Gov vars
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert(f'v0/token/{token_id}/fixed_interval_price_id' not in result)
        if collateral:
            assert(f'v0/token/{token_id}/loan_collateral_enabled' not in result)
            assert(f'v0/token/{token_id}/loan_collateral_factor' not in result)
        if loan:
            assert(f'v0/token/{token_id}/loan_minting_enabled' not in result)
            assert(f'v0/token/{token_id}/loan_minting_interest' not in result)
        assert(f'v0/locks/token/{token_id}' not in result)

        # Save old ID and get new one
        token_idv1 = token_id
        token_id = list(self.nodes[0].gettoken(token_symbol).keys())[0]

        # Check new token in Gov vars
        assert_equal(result[f'v0/token/{token_id}/fixed_interval_price_id'], f'{token_symbol}/USD')
        if collateral:
            assert_equal(result[f'v0/token/{token_id}/loan_collateral_enabled'], 'true')
            assert_equal(result[f'v0/token/{token_id}/loan_collateral_factor'], '1')
        if loan:
            assert_equal(result[f'v0/token/{token_id}/loan_minting_enabled'], 'true')
            assert_equal(result[f'v0/token/{token_id}/loan_minting_interest'], '0')
        assert_equal(result[f'v0/oracles/splits/{self.nodes[0].getblockcount()}'], f'{token_idv1}/{multiplier},')
        assert_equal(result[f'v0/token/{token_idv1}/descendant'], f'{token_id}/{self.nodes[0].getblockcount()}')
        assert_equal(result[f'v0/token/{token_id}/ascendant'], f'{token_idv1}/split')
        assert_equal(result[f'v0/locks/token/{token_id}'], 'true')

        # Check new token
        result = self.nodes[0].gettoken(token_id)[token_id]
        assert_equal(result['symbol'], f'{token_symbol}')
        assert_equal(result['minted'], minted)
        assert_equal(result['mintable'], True)
        assert_equal(result['tradeable'], True)
        assert_equal(result['finalized'], False)
        assert_equal(result['creationTx'], self.nodes[0].getblock(self.nodes[0].getbestblockhash())['tx'][1])
        assert_equal(result['creationHeight'], self.nodes[0].getblockcount())
        assert_equal(result['destructionTx'], '0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(result['destructionHeight'], -1)

        # Make sure no old tokens remain in the account
        result = self.nodes[0].getaccount(self.account1)
        for val in result:
            assert_equal(val.find(f'{token_symbol}{token_suffix}'), -1)

    # Make the split and return split height for revert if needed
    def split(self, tokenId):
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{tokenId}':'true'}})
        self.nodes[0].generate(1)

        # Token split
        splitHeight = self.nodes[0].getblockcount() + 2
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(splitHeight)}':f'{tokenId}/2'}})
        self.nodes[0].generate(2)

        return splitHeight

    def getTokenSymbolFromId(self, tokenId):
        token = self.nodes[0].gettoken(tokenId)
        tokenSymbol = token[tokenId]["symbol"]
        return tokenSymbol

    def revert(self, block):
        blockhash = self.nodes[0].getblockhash(block)
        self.nodes[0].invalidateblock(blockhash)


    def check_tributes_on_split(self, poolId, tokenId, revert=False):
        self.nodes[0].generate(10)
        tokenSymbol = self.getTokenSymbolFromId(tokenId)
        poolSymbol = self.getTokenSymbolFromId(poolId)
        revert_block = self.nodes[0].getblockcount()
        # set LP and Tokens gov vars
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/poolpairs/{poolId}/token_a_fee_pct': '0.01',
                                            f'v0/poolpairs/{poolId}/token_b_fee_pct': '0.03',
                                            f'v0/token/{tokenId}/dex_in_fee_pct': '0.02',
                                            f'v0/token/{tokenId}/dex_out_fee_pct': '0.005'}})

        self.nodes[0].generate(1)

        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert(f'v0/poolpairs/{poolId}/token_a_fee_pct' in result)
        assert(f'v0/poolpairs/{poolId}/token_b_fee_pct' in result)
        assert(f'v0/token/{tokenId}/dex_in_fee_pct' in result)
        assert(f'v0/token/{tokenId}/dex_out_fee_pct' in result)

        splitHeight = self.split(tokenId)
        self.nodes[0].generate(1)

        new_token_id = list(self.nodes[0].gettoken(tokenSymbol).keys())[0]
        new_pool_id = list(self.nodes[0].gettoken(poolSymbol).keys())[0]

        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert(f'v0/poolpairs/{poolId}/token_a_fee_pct' not in result)
        assert(f'v0/poolpairs/{poolId}/token_b_fee_pct' not in result)
        assert(f'v0/token/{tokenId}/dex_in_fee_pct' not in result)
        assert(f'v0/token/{tokenId}/dex_out_fee_pct' not in result)

        assert(f'v0/poolpairs/{new_pool_id}/token_a_fee_pct' in result)
        assert(f'v0/poolpairs/{new_pool_id}/token_b_fee_pct' in result)
        assert(f'v0/token/{new_token_id}/dex_in_fee_pct' in result)
        assert(f'v0/token/{new_token_id}/dex_out_fee_pct' in result)

        if revert:
            self.revert(splitHeight)
            result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
            assert(f'v0/poolpairs/{poolId}/token_a_fee_pct' in result)
            assert(f'v0/poolpairs/{poolId}/token_b_fee_pct' in result)
            assert(f'v0/token/{tokenId}/dex_in_fee_pct' in result)
            assert(f'v0/token/{tokenId}/dex_out_fee_pct' in result)
            assert(f'v0/poolpairs/{new_pool_id}/token_a_fee_pct' not in result)
            assert(f'v0/poolpairs/{new_pool_id}/token_b_fee_pct' not in result)
            assert(f'v0/token/{new_token_id}/dex_in_fee_pct' not in result)
            assert(f'v0/token/{new_token_id}/dex_out_fee_pct' not in result)

            self.revert(revert_block)
            result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
            assert(f'v0/poolpairs/{new_pool_id}/token_a_fee_pct' not in result)
            assert(f'v0/poolpairs/{new_pool_id}/token_b_fee_pct' not in result)
            assert(f'v0/token/{new_token_id}/dex_in_fee_pct' not in result)
            assert(f'v0/token/{new_token_id}/dex_out_fee_pct' not in result)
            assert(f'v0/poolpairs/{poolId}/token_a_fee_pct' not in result)
            assert(f'v0/poolpairs/{poolId}/token_b_fee_pct' not in result)
            assert(f'v0/token/{tokenId}/dex_in_fee_pct' not in result)
            assert(f'v0/token/{tokenId}/dex_out_fee_pct' not in result)

    def check_pool_split(self, pool_id, pool_symbol, token_id, token_symbol, token_suffix, reserve_a, reserve_b, reserve_a_b, reserve_b_a):

        # Check old pool
        result = self.nodes[0].getpoolpair(pool_id)[pool_id]
        assert_equal(result['symbol'], f'{pool_symbol}{token_suffix}')
        assert_equal(result['reserveA'], Decimal('0.00000000'))
        assert_equal(result['reserveB'], Decimal('0.00000000'))
        assert_equal(result['reserveA/reserveB'], '0')
        assert_equal(result['reserveB/reserveA'], '0')
        assert_equal(result['status'], False)
        assert_equal(result['tradeEnabled'], False)
        assert('dexFeePctTokenA' not in result)
        assert('dexFeePctTokenB' not in result)
        assert('dexFeeInPctTokenA' not in result)
        assert('dexFeeOutPctTokenA' not in result)
        assert_equal(result['rewardPct'], Decimal('0.00000000'))
        assert_equal(result['rewardLoanPct'], Decimal('0.00000000'))

        # Validate old Gov vars removed
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert(f'v0/poolpairs/{pool_id}/token_a_fee_pct' not in result)
        assert(f'v0/poolpairs/{pool_id}/token_b_fee_pct' not in result)
        assert(f'v0/token/{token_id}/dex_in_fee_pct' not in result)
        assert(f'v0/token/{token_id}/dex_out_fee_pct' not in result)

        # Swap old for new values
        token_id = list(self.nodes[0].gettoken(token_symbol).keys())[0]
        pool_id = list(self.nodes[0].gettoken(pool_symbol).keys())[0]

        # Validate new Gov vars set
        if isT3:
            assert_equal(result[f'v0/poolpairs/{pool_id}/token_a_fee_pct'], '0.01')
            assert_equal(result[f'v0/poolpairs/{pool_id}/token_b_fee_pct'], '0.03')
            assert_equal(result[f'v0/token/{token_id}/dex_in_fee_pct'], '0.02')
            assert_equal(result[f'v0/token/{token_id}/dex_out_fee_pct'], '0.005')

        # Check new pool
        result = self.nodes[0].getpoolpair(pool_id)[pool_id]
        breakpoint()
        assert_equal(result['symbol'], f'{pool_symbol}')
        assert_equal(result['reserveA'], reserve_a)
        assert_equal(result['reserveB'], reserve_b)
        assert_equal(result['reserveA/reserveB'], reserve_a_b)
        assert_equal(result['reserveB/reserveA'], reserve_b_a)
        assert_equal(result['idTokenA'], str(token_id))
        assert_equal(result['status'], True)
        assert_equal(result['tradeEnabled'], True)
        assert_equal(result['dexFeePctTokenA'], Decimal('0.01000000'))
        assert_equal(result['dexFeePctTokenB'], Decimal('0.03000000'))
        assert_equal(result['dexFeeInPctTokenA'], Decimal('0.01000000'))
        assert_equal(result['dexFeeOutPctTokenA'], Decimal('0.01000000'))
        assert_equal(result['rewardPct'], Decimal('0E-8'))
        assert_equal(result['rewardLoanPct'], Decimal('0E-8'))
        assert_equal(result['creationTx'], self.nodes[0].getblock(self.nodes[0].getbestblockhash())['tx'][2])
        assert_equal(result['creationHeight'], self.nodes[0].getblockcount())

        # Make sure no old pool tokens remain in the account
        result = self.nodes[0].getaccount(self.account1)
        for val in result:
            assert_equal(val.find(f'{pool_symbol}{token_suffix}'), -1)

        # Check that LP_SPLITS and LP_LOAN_TOKEN_SPLITS updated
        breakpoint()
        assert_equal(self.nodes[0].getgov('LP_SPLITS')['LP_SPLITS'], {pool_id: Decimal('1.00000000')})
        assert_equal(self.nodes[0].getgov('LP_LOAN_TOKEN_SPLITS')['LP_LOAN_TOKEN_SPLITS'], {pool_id: Decimal('1.00000000')})

    def token_split(self):

        # Move to GW
        self.nodes[0].generate(151 - self.nodes[0].getblockcount())

        # Make sure we cannot make a token with '/' in its symbol
        assert_raises_rpc_error(-32600, "token symbol should not contain '/'", self.nodes[0].createtoken, {
            'symbol': 'bad/v1',
            "collateralAddress": self.account1
        })

        # Create funded addresses
        funded_addresses = []
        for _ in range(100):
            amount = round(random.uniform(1, 1000), 8)
            self.nodes[0].minttokens([f'{str(amount)}@{self.idT3}'])
            self.nodes[0].generate(1)
            address = self.nodes[0].getnewaddress()
            self.nodes[0].accounttoaccount(self.account1, {address: f'{str(amount)}@{self.idT3}'})
            self.nodes[0].generate(1)
            funded_addresses.append([address, Decimal(str(amount))])

        # Set expected minted amount
        minted = self.nodes[0].gettoken(self.idT3)[self.idT3]['minted'] * 2

        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{self.idT3}':'true'}})
        self.nodes[0].generate(1)

        # Token split
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}':f'{self.idT3}/2'}})
        self.nodes[0].generate(2)

        # Check token split correctly
        self.check_token_split(self.idT3, self.symbolT1, '/v1', 2, minted, True, True)

        # Swap old for new values
        self.idT3 = list(self.nodes[0].gettoken(self.symbolT1).keys())[0]

        # Check new balances
        for [address, amount] in funded_addresses:
            account = self.nodes[0].getaccount(address)
            new_amount = Decimal(account[0].split('@')[0])
            assert_equal(new_amount, amount * 2)

        # Token split
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}':f'{self.idT3}/-3'}})
        self.nodes[0].generate(2)

        # Check new balances
        minted = Decimal('0')
        for [address, amount] in funded_addresses:
            account = self.nodes[0].getaccount(address)
            amount_scaled = Decimal(truncate(str(amount * 2 / 3), 8))
            new_amount = Decimal(account[0].split('@')[0])
            assert_equal(new_amount, amount_scaled)
            minted += new_amount

        # Check token split correctly
        self.check_token_split(self.idT3, self.symbolT1, '/v2', -3, minted, True, True)

        # Swap old for new values
        self.idT3 = list(self.nodes[0].gettoken(self.symbolT1).keys())[0]

        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{self.idT3}':'false'}})
        self.nodes[0].generate(1)

    def token_split_T3(self):
        self.nodes[0].generate(30) # Go to GreatWorldHeight

        # Check pool before split
        result = self.nodes[0].getpoolpair(self.symbolT3_DUSD)[self.idT3_DUSD]
        assert_equal(result['reserveA'], Decimal('100000000.00000000'))
        assert_equal(result['reserveB'], Decimal('1.00000000'))
        assert_equal(result['reserveA/reserveB'], Decimal('100000000.00000000'))
        assert_equal(result['reserveB/reserveA'], Decimal('1E-8'))
        assert_equal(result['status'], True)
        assert_equal(result['tradeEnabled'], True)

        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{self.idT3}':'true'}})
        self.nodes[0].generate(1)
        # Token split
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}':f'{self.idT3}/2'}})
        self.nodes[0].generate(2)

        # Check token split correctly
        self.check_token_split(self.idT3, self.symbolT3, '/v1', 2, Decimal('200000000.00000000'), True, False)

        # Check pool migrated successfully
        self.check_pool_split(self.idT3_DUSD, self.symbolT3_DUSD, self.idT3, self.symbolT3, '/v1', Decimal('200000000.00000000'), Decimal('1.00000000'), Decimal('200000000.00000000'), Decimal('0E-8'))

        # Swap old for new values
        self.idT2 = list(self.nodes[0].gettoken(self.symbolT2).keys())[0]
        self.idT2_DUSD = list(self.nodes[0].gettoken(self.symbolT2_DUSD).keys())[0]

        # Token split
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}':f'{self.idT2}/-3'}})
        self.nodes[0].generate(2)

        # Check token split correctly
        minted = truncate(str(self.poolT2_DUSDTotal * 2 / 3), 8)
        self.check_token_split(self.idT2, self.symbolT2, '/v2', -3, minted, False, True)

        # Check pool migrated successfully
        self.check_pool_split(self.idT2_DUSD, self.symbolT2_DUSD, self.idT2, self.symbolT2, '/v2', minted, Decimal('0.66666666'), Decimal('1.50000000'))

        # Swap old for new values
        self.idT2 = list(self.nodes[0].gettoken(self.symbolT2).keys())[0]

        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{self.idT2}':'false'}})
        self.nodes[0].generate(1)

    def token_split_T1(self):
        self.nodes[0].generate(30) # Go to GreatWorldHeight

        # Check pool before split
        result = self.nodes[0].getpoolpair(self.symbolT1_DUSD)[self.idT1_DUSD]
        assert_equal(result['reserveA'], Decimal('10000.00000000'))
        assert_equal(result['reserveB'], Decimal('100000000.00000000'))
        assert_equal(result['reserveA/reserveB'], Decimal('0.00010000'))
        assert_equal(result['reserveB/reserveA'], Decimal('10000.00000000'))
        assert_equal(result['status'], True)
        assert_equal(result['tradeEnabled'], True)
       # assert_equal(result['dexFeePctTokenA'], Decimal('0.01000000'))
       # assert_equal(result['dexFeePctTokenB'], Decimal('0.03000000'))
       # assert_equal(result['dexFeeInPctTokenA'], Decimal('0.01000000'))
       # assert_equal(result['dexFeeOutPctTokenA'], Decimal('0.01000000'))
       # assert_equal(result['rewardPct'], Decimal('1.00000000'))
       # assert_equal(result['rewardLoanPct'], Decimal('1.00000000'))

        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{self.idT1}':'true'}})
        self.nodes[0].generate(1)
        # Token split
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}':f'{self.idT1}/2'}})
        self.nodes[0].generate(2)

        # Check token split correctly
        self.check_token_split(self.idT1, self.symbolT1, '/v1', 2, Decimal('220000.00000000'), True, True)

        # Check pool migrated successfully
        self.check_pool_split(self.idT2_DUSD, self.symbolT2_DUSD, self.idT2, self.symbolT2, '/v1', self.poolT2_DUSDTotal * 2, Decimal('2.00000000'), Decimal('0.50000000'))

        # Swap old for new values
        self.idT2 = list(self.nodes[0].gettoken(self.symbolT2).keys())[0]
        self.idT2_DUSD = list(self.nodes[0].gettoken(self.symbolT2_DUSD).keys())[0]

        # Token split
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}':f'{self.idT2}/-3'}})
        self.nodes[0].generate(2)

        # Check token split correctly
        minted = truncate(str(self.poolT2_DUSDTotal * 2 / 3), 8)
        self.check_token_split(self.idT2, self.symbolT2, '/v2', -3, minted, False, True)

        # Check pool migrated successfully
        self.check_pool_split(self.idT2_DUSD, self.symbolT2_DUSD, self.idT2, self.symbolT2, '/v2', minted, Decimal('0.66666666'), Decimal('1.50000000'))

        # Swap old for new values
        self.idT2 = list(self.nodes[0].gettoken(self.symbolT2).keys())[0]

        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{self.idT2}':'false'}})
        self.nodes[0].generate(1)

    def execute_vault_split(self, token_id, token_symbol, multiplier, suffix):

        # Get total minted
        if multiplier < 0:
            minted = truncate(str(self.nodes[0].gettoken(token_id)[token_id]['minted'] / abs(multiplier)), 8)
        else:
            minted = self.nodes[0].gettoken(token_id)[token_id]['minted'] * multiplier

        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{token_id}':'true'}})
        self.nodes[0].generate(1)

        # Token split
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}':f'{token_id}/{multiplier}'}})
        self.nodes[0].generate(1)

        # Vault array
        self.vault_balances = []

        # Gather pre-split info to compare later
        pre_get_interest = self.nodes[0].getinterest('LOAN0001', 'NVDA')[0]
        for vault_info in self.nodes[0].listvaults():
            vault = self.nodes[0].getvault(vault_info['vaultId'])
            self.vault_balances.append([vault_info['vaultId'], vault])

        # Move to split block
        self.nodes[0].generate(1)

        # Check token split correctly
        self.check_token_split(token_id, token_symbol, suffix, multiplier, str(minted), True, False)

        # Compare pre-split vaults with post-split vaults
        for [vault_id, pre_vault] in self.vault_balances:
            post_vault = self.nodes[0].getvault(vault_id)
            pre_interest = Decimal(pre_vault['interestAmounts'][0].split('@')[0])
            post_interest = Decimal(post_vault['interestAmounts'][0].split('@')[0])
            pre_loan_amount = Decimal(pre_vault['loanAmounts'][0].split('@')[0]) - pre_interest
            post_loan_amount = Decimal(post_vault['loanAmounts'][0].split('@')[0]) - post_interest
            if multiplier < 0:
                pre_loan_scaled = Decimal(truncate(str(pre_loan_amount / abs(multiplier)), 8))
            else:
                pre_loan_scaled = pre_loan_amount * multiplier
            assert_equal(post_loan_amount, pre_loan_scaled)

        # Check interest has changed as expected
        post_get_interest = self.nodes[0].getinterest('LOAN0001', 'NVDA')[0]
        if multiplier < 0:
            expected_interest = Decimal(truncate(str(pre_get_interest['interestPerBlock'] / abs(multiplier)), 8))
        else:
            expected_interest = pre_get_interest['interestPerBlock'] * multiplier
        current_interest = post_get_interest['interestPerBlock']
        current_interest_round = current_interest + Decimal('0.00000001')
        expected_interest_round = expected_interest + Decimal('0.00000001')
        if current_interest != expected_interest and current_interest_round != expected_interest:
            assert_equal(current_interest, expected_interest_round)

        # Swap old for new values
        token_id = list(self.nodes[0].gettoken(token_symbol).keys())[0]

        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{token_id}':'false'}})
        self.nodes[0].generate(1)

    def vault_split(self):
        # Multiplier 2
        self.execute_vault_split(self.idT3, self.symbolT3, 2, '/v1')

        # Swap old for new values
        self.idT3 = list(self.nodes[0].gettoken(self.symbolT3).keys())[0]

        # Multiplier -3
        self.execute_vault_split(self.idT3, self.symbolT3, -3, '/v2')

if __name__ == '__main__':
    TokenSplitTest().main()

