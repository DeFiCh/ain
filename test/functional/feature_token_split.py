#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
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
        self.setup_clean_chain = True
        self.fort_canning_crunch = 600
        self.extra_args = [
            ['-vaultindex=1', '-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1',
             '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1',
             '-fortcanningroadheight=1', f'-fortcanningcrunchheight={self.fort_canning_crunch}', '-subsidytest=1']]

    def run_test(self):
        self.setup_test_tokens()
        self.setup_test_pools()
        self.token_split()
        self.pool_split()
        self.setup_test_vaults()
        self.vault_split()
        self.check_govvar_deletion()
        self.migrate_auction_batches()
        self.check_future_swap_refund()

    def setup_test_tokens(self):
        self.nodes[0].generate(101)

        # Symbols
        self.symbolDFI = 'DFI'
        self.symbolBTC = 'BTC'
        self.symbolDUSD = 'DUSD'
        self.symbolTSLA = 'TSLA'
        self.symbolGOOGL = 'GOOGL'
        self.symbolNVDA = 'NVDA'
        self.symbolMSFT = 'MSFT'
        self.symbolTWTR = 'TWTR'
        self.symbolGD = 'GOOGL-DUSD'

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolDFI},
            {"currency": "USD", "token": self.symbolBTC},
            {"currency": "USD", "token": self.symbolDUSD},
            {"currency": "USD", "token": self.symbolGOOGL},
            {"currency": "USD", "token": self.symbolTSLA},
            {"currency": "USD", "token": self.symbolNVDA},
            {"currency": "USD", "token": self.symbolMSFT},
            {"currency": "USD", "token": self.symbolTWTR},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolBTC}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDUSD}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolGOOGL}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTSLA}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolNVDA}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolMSFT}"},
            {"currency": "USD", "tokenAmount": f"2@{self.symbolTWTR}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Create token
        self.nodes[0].createtoken({
            "symbol": self.symbolBTC,
            "name": self.symbolBTC,
            "isDAT": True,
            "collateralAddress": self.address
        })
        self.nodes[0].generate(1)

        # Set loan tokens
        self.nodes[0].setloantoken({
            'symbol': self.symbolGOOGL,
            'name': self.symbolGOOGL,
            'fixedIntervalPriceId': f"{self.symbolGOOGL}/USD",
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
            'symbol': self.symbolTSLA,
            'name': self.symbolTSLA,
            'fixedIntervalPriceId': f"{self.symbolTSLA}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolNVDA,
            'name': self.symbolNVDA,
            'fixedIntervalPriceId': f"{self.symbolNVDA}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolMSFT,
            'name': self.symbolMSFT,
            'fixedIntervalPriceId': f"{self.symbolMSFT}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolTWTR,
            'name': self.symbolTWTR,
            'fixedIntervalPriceId': f"{self.symbolTWTR}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
            'token': self.symbolBTC,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolBTC}/USD"
        })
        self.nodes[0].generate(1)

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
            'token': self.symbolGOOGL,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolGOOGL}/USD"
        })
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': self.symbolTSLA,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolTSLA}/USD"
        })
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': self.symbolTWTR,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolTWTR}/USD"
        })
        self.nodes[0].generate(1)

        # Store token IDs
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]
        self.idNVDA = list(self.nodes[0].gettoken(self.symbolNVDA).keys())[0]
        self.idMSFT = list(self.nodes[0].gettoken(self.symbolMSFT).keys())[0]
        self.idTWTR = list(self.nodes[0].gettoken(self.symbolTWTR).keys())[0]

        # Mint some loan tokens
        self.nodes[0].minttokens([
            f'1000000@{self.symbolMSFT}',
            f'1000000@{self.symbolDUSD}',
            f'1000000@{self.symbolTWTR}',
            f'1000000@{self.symbolBTC}',
        ])
        self.nodes[0].generate(1)

        # Create funded addresses
        self.funded_addresses = []
        for _ in range(100):
            amount = round(random.uniform(1, 1000), 8)
            self.nodes[0].minttokens([f'{str(amount)}@{self.idTSLA}'])
            self.nodes[0].generate(1)
            address = self.nodes[0].getnewaddress()
            self.nodes[0].accounttoaccount(self.address, {address: f'{str(amount)}@{self.idTSLA}'})
            self.nodes[0].generate(1)
            self.funded_addresses.append([address, Decimal(str(amount))])

    def setup_test_pools(self):

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolGOOGL,
            "tokenB": self.symbolDUSD,
            "commission": 0.001,
            "status": True,
            "ownerAddress": self.address,
            "symbol": self.symbolGD
        })
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDUSD,
            "tokenB": self.symbolDFI,
            "commission": Decimal('0.001'),
            "status": True,
            "ownerAddress": self.address
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDUSD,
            "tokenB": self.symbolNVDA,
            "commission": Decimal('0.001'),
            "status": True,
            "ownerAddress": self.address
        }, [])
        self.nodes[0].generate(1)

        # Fund address for pool
        self.nodes[0].utxostoaccount({self.address: f'100@{self.symbolDFI}'})
        self.nodes[0].minttokens([f'200@{self.idDUSD}', f'100@{self.idNVDA}'])
        self.nodes[0].generate(1)

        # Fund pools
        self.nodes[0].addpoolliquidity({
            self.address: [f'100@{self.symbolDUSD}', f'100@{self.symbolDFI}']
        }, self.address)
        self.nodes[0].addpoolliquidity({
            self.address: [f'100@{self.symbolDUSD}', f'100@{self.symbolNVDA}']
        }, self.address)
        self.nodes[0].generate(1)

        # Store pool ID
        self.idGD = list(self.nodes[0].gettoken(self.symbolGD).keys())[0]

        # Set pool gov vars
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/poolpairs/{self.idGD}/token_a_fee_pct': '0.01',
                                             f'v0/poolpairs/{self.idGD}/token_b_fee_pct': '0.03',
                                             f'v0/token/{self.idGOOGL}/dex_in_fee_pct': '0.02',
                                             f'v0/token/{self.idGOOGL}/dex_out_fee_pct': '0.005'}})
        self.nodes[0].setgov({"LP_SPLITS": {str(self.idGD): 1}})
        self.nodes[0].setgov({"LP_LOAN_TOKEN_SPLITS": {str(self.idGD): 1}})
        self.nodes[0].generate(1)

        # Randomly populate pool
        self.poolGDTotal = Decimal('0')
        for _ in range(100):
            amount = round(random.uniform(1, 1000), 8)
            self.nodes[0].minttokens([f'{str(amount)}@{self.idDUSD}'])
            self.nodes[0].minttokens([f'{str(amount)}@{self.idGOOGL}'])
            self.nodes[0].generate(1)
            self.nodes[0].addpoolliquidity({
                self.address: [f'{str(amount)}@{self.idDUSD}', f'{str(amount)}@{self.idGOOGL}']
            }, self.address)
            self.nodes[0].generate(1)
            self.poolGDTotal += Decimal(str(amount))

    def setup_test_vaults(self):

        # Create loan scheme
        self.nodes[0].createloanscheme(100, 0.1, 'LOAN0001')
        self.nodes[0].generate(1)

        # Fund address for vault creation
        self.nodes[0].utxostoaccount({self.address: f'30000@{self.symbolDFI}'})
        self.nodes[0].generate(1)

        for vault in range(100):
            # Create vault
            vault_id = self.nodes[0].createvault(self.address, '')
            self.nodes[0].generate(1)

            # Store single vault to check later
            if vault == 0:
                self.vault_id = vault_id
                self.vault_loan = 0

            # Take 1 to 3 loans
            for _ in range(1, 4):
                # Deposit random collateral
                collateral = round(random.uniform(1, 100), 8)
                loan = truncate(str(collateral / 3), 8)
                self.nodes[0].deposittovault(vault_id, self.address, f'{collateral}@{self.symbolDFI}')
                self.nodes[0].generate(1)

                # Take loan
                self.nodes[0].takeloan({
                    'vaultId': vault_id,
                    'amounts': f"{loan}@{self.symbolNVDA}"
                })
                self.nodes[0].generate(1)

                # Store loan amounts
                if vault == 0:
                    self.vault_loan += Decimal(loan)

    def check_token_split(self, token_id, token_symbol, token_suffix, multiplier, minted, loan, collateral):

        # Check old token
        result = self.nodes[0].gettoken(token_id)[token_id]
        assert_equal(result['symbol'], f'{token_symbol}{token_suffix}')
        assert_equal(result['minted'], Decimal('0.00000000'))
        assert_equal(result['mintable'], False)
        assert_equal(result['tradeable'], False)
        assert_equal(result['finalized'], True)
        assert_equal(result['isLoanToken'], False)
        assert_equal(result['destructionTx'], self.nodes[0].getbestblockhash())
        assert_equal(result['destructionHeight'], self.nodes[0].getblockcount())

        # Check old token in Gov vars
        result = self.nodes[0].listgovs("attrs")[0][0]['ATTRIBUTES']
        assert (f'v0/token/{token_id}/fixed_interval_price_id' not in result)
        if collateral:
            assert (f'v0/token/{token_id}/loan_collateral_enabled' not in result)
            assert (f'v0/token/{token_id}/loan_collateral_factor' not in result)
        if loan:
            assert (f'v0/token/{token_id}/loan_minting_enabled' not in result)
            assert (f'v0/token/{token_id}/loan_minting_interest' not in result)
        assert (f'v0/locks/token/{token_id}' not in result)

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
        assert (f'v0/oracles/splits/{self.nodes[0].getblockcount()}' not in result)
        assert_equal(result[f'v0/token/{token_idv1}/descendant'], f'{token_id}/{self.nodes[0].getblockcount()}')
        assert_equal(result[f'v0/token/{token_id}/ascendant'], f'{token_idv1}/split')
        assert_equal(result[f'v0/locks/token/{token_id}'], 'true')

        # Check new token
        result = self.nodes[0].gettoken(token_id)[token_id]
        assert_equal(result['symbol'], f'{token_symbol}')
        assert_equal(result['minted'], Decimal(f'{minted}'))
        assert_equal(result['mintable'], True)
        assert_equal(result['tradeable'], True)
        assert_equal(result['finalized'], False)
        assert_equal(result['isLoanToken'], True)
        assert_equal(result['creationTx'], self.nodes[0].getblock(self.nodes[0].getbestblockhash())['tx'][1])
        assert_equal(result['creationHeight'], self.nodes[0].getblockcount())
        assert_equal(result['destructionTx'], '0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(result['destructionHeight'], -1)

        # Make sure no old tokens remain in the account
        result = self.nodes[0].getaccount(self.address)
        for val in result:
            assert_equal(val.find(f'{token_symbol}{token_suffix}'), -1)

    def check_pool_split(self, pool_id, pool_symbol, token_id, token_symbol, token_suffix, minted, reserve_a,
                         reserve_b):

        # Check old pool
        result = self.nodes[0].getpoolpair(pool_id)[pool_id]
        assert_equal(result['symbol'], f'{pool_symbol}{token_suffix}')
        assert_equal(result['reserveA'], Decimal('0.00000000'))
        assert_equal(result['reserveB'], Decimal('0.00000000'))
        assert_equal(result['reserveA/reserveB'], '0')
        assert_equal(result['reserveB/reserveA'], '0')
        assert_equal(result['status'], False)
        assert_equal(result['tradeEnabled'], False)
        assert ('dexFeePctTokenA' not in result)
        assert ('dexFeeInPctTokenA' not in result)
        assert ('dexFeeOutPctTokenA' not in result)
        assert ('dexFeePctTokenB' not in result)
        assert ('dexFeeInPctTokenB' not in result)
        assert ('dexFeeOutPctTokenB' not in result)
        assert_equal(result['rewardPct'], Decimal('0.00000000'))
        assert_equal(result['rewardLoanPct'], Decimal('0.00000000'))

        # Validate old Gov vars removed
        result = self.nodes[0].listgovs("attrs")[0][0]['ATTRIBUTES']
        assert (f'v0/poolpairs/{pool_id}/token_a_fee_pct' not in result)
        assert (f'v0/poolpairs/{pool_id}/token_b_fee_pct' not in result)
        assert (f'v0/token/{token_id}/dex_in_fee_pct' not in result)
        assert (f'v0/token/{token_id}/dex_out_fee_pct' not in result)

        # Swap old for new values
        token_id = list(self.nodes[0].gettoken(token_symbol).keys())[0]
        pool_id = list(self.nodes[0].gettoken(pool_symbol).keys())[0]

        # Validate new Gov vars set
        assert_equal(result[f'v0/poolpairs/{pool_id}/token_a_fee_pct'], '0.01')
        assert_equal(result[f'v0/poolpairs/{pool_id}/token_b_fee_pct'], '0.03')
        assert_equal(result[f'v0/token/{token_id}/dex_in_fee_pct'], '0.02')
        assert_equal(result[f'v0/token/{token_id}/dex_out_fee_pct'], '0.005')

        # Check new pool
        result = self.nodes[0].getpoolpair(pool_id)[pool_id]
        assert_equal(result['symbol'], f'{pool_symbol}')
        assert_equal(result['reserveA'], Decimal(f'{minted}'))
        assert_equal(result['reserveB'], self.poolGDTotal)
        assert_equal(result['reserveA/reserveB'], reserve_a)
        assert_equal(result['reserveB/reserveA'], reserve_b)
        assert_equal(result['idTokenA'], str(token_id))
        assert_equal(result['status'], True)
        assert_equal(result['tradeEnabled'], True)
        assert_equal(result['dexFeePctTokenA'], Decimal('0.01000000'))
        assert_equal(result['dexFeeInPctTokenA'], Decimal('0.01000000'))
        assert_equal(result['dexFeeOutPctTokenA'], Decimal('0.01000000'))
        assert_equal(result['dexFeePctTokenB'], Decimal('0.03000000'))
        assert_equal(result['dexFeeInPctTokenB'], Decimal('0.03000000'))
        assert_equal(result['dexFeeOutPctTokenB'], Decimal('0.03000000'))
        assert_equal(result['rewardPct'], Decimal('1.00000000'))
        assert_equal(result['rewardLoanPct'], Decimal('1.00000000'))
        assert_equal(result['creationTx'], self.nodes[0].getblock(self.nodes[0].getbestblockhash())['tx'][2])
        assert_equal(result['creationHeight'], self.nodes[0].getblockcount())

        # Make sure no old pool tokens remain in the account
        result = self.nodes[0].getaccount(self.address)
        for val in result:
            assert_equal(val.find(f'{pool_symbol}{token_suffix}'), -1)

        # Check that LP_SPLITS and LP_LOAN_TOKEN_SPLITS updated
        assert_equal(self.nodes[0].getgov('LP_SPLITS')['LP_SPLITS'], {pool_id: Decimal('1.00000000')})
        assert_equal(self.nodes[0].getgov('LP_LOAN_TOKEN_SPLITS')['LP_LOAN_TOKEN_SPLITS'],
                     {pool_id: Decimal('1.00000000')})

    def token_split(self):

        # Move to FCC
        self.nodes[0].generate(self.fort_canning_crunch - 1 - self.nodes[0].getblockcount())

        # Mined in fork height, still using old code
        self.nodes[0].updateloantoken(self.idGOOGL, {
            'name': 'AAAA',
            'interest': 1
        })
        self.nodes[0].generate(1)

        # Check name change
        token = self.nodes[0].getloantoken(self.idGOOGL)
        assert_equal(token['token'][f'{self.idGOOGL}']['name'], 'AAAA')
        assert_equal(token['interest'], Decimal(1))

        # Gov vars should not have loan or collateral tokens in yet
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result, {f'v0/poolpairs/{self.idGD}/token_a_fee_pct': '0.01',
                              f'v0/poolpairs/{self.idGD}/token_b_fee_pct': '0.03',
                              f'v0/token/{self.idGOOGL}/dex_in_fee_pct': '0.02',
                              f'v0/token/{self.idGOOGL}/dex_out_fee_pct': '0.005'})

        # Mining fork height - migration of token to gov var
        self.nodes[0].generate(1)

        # Check Gov var present
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result[f'v0/token/{self.idGOOGL}/loan_minting_enabled'], 'true')
        assert_equal(result[f'v0/token/{self.idGOOGL}/loan_minting_interest'], '1')
        assert_equal(result[f'v0/token/{self.idGOOGL}/fixed_interval_price_id'], f"{self.symbolGOOGL}/USD")

        # Make sure name updated
        token = self.nodes[0].getloantoken(self.idGOOGL)
        assert_equal(token['token'][f'{self.idGOOGL}']['name'], 'AAAA')
        assert_equal(token['interest'], Decimal(1))

        # Mined in past fork height, using new code
        self.nodes[0].updateloantoken(self.idGOOGL, {
            'name': self.symbolGOOGL,
            'interest': 0
        })

        # Set extra Gov vars for token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/token/{self.idTSLA}/dfip2203': 'true',
                                             f'v0/token/{self.idTSLA}/loan_payback/{self.idDUSD}': 'true',
                                             f'v0/token/{self.idGOOGL}/loan_payback/{self.idTSLA}': 'true',
                                             f'v0/token/{self.idTSLA}/loan_payback/{self.idTSLA}': 'true',
                                             f'v0/token/{self.idGOOGL}/loan_payback_fee_pct/{self.idTSLA}': '0.25',
                                             f'v0/token/{self.idTSLA}/loan_payback_fee_pct/{self.idTSLA}': '0.25',
                                             f'v0/token/{self.idTSLA}/loan_payback_fee_pct/{self.idDUSD}': '0.25'}})
        self.nodes[0].generate(1)

        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result[f'v0/token/{self.idGOOGL}/loan_minting_enabled'], 'true')
        assert_equal(result[f'v0/token/{self.idGOOGL}/loan_minting_interest'], '0')
        assert_equal(result[f'v0/token/{self.idGOOGL}/fixed_interval_price_id'], f"{self.symbolGOOGL}/USD")

        # Check name changed back
        token = self.nodes[0].getloantoken(self.idGOOGL)
        assert_equal(token['token'][f'{self.idGOOGL}']['name'], self.symbolGOOGL)
        assert_equal(token['interest'], Decimal(0))

        # Make sure we cannot make a token with '/' in its symbol
        assert_raises_rpc_error(-32600, "token symbol should not contain '/'", self.nodes[0].createtoken, {
            'symbol': 'bad/v1',
            "collateralAddress": self.address
        })

        # Set expected minted amount
        minted = self.nodes[0].gettoken(self.idTSLA)[self.idTSLA]['minted'] * 2

        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'true'}})
        self.nodes[0].generate(1)

        # Token split
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}': f'{self.idTSLA}/2'}})
        self.nodes[0].generate(2)

        # Check token split correctly
        self.check_token_split(self.idTSLA, self.symbolTSLA, '/v1', 2, minted, True, True)

        # Swap old for new values
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Verify extra Gov vars copied
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result[f'v0/token/{self.idTSLA}/dfip2203'], 'true')
        assert_equal(result[f'v0/token/{self.idTSLA}/loan_payback/{self.idDUSD}'], 'true')
        assert_equal(result[f'v0/token/{self.idGOOGL}/loan_payback/{self.idTSLA}'], 'true')
        assert_equal(result[f'v0/token/{self.idTSLA}/loan_payback/{self.idTSLA}'], 'true')
        assert_equal(result[f'v0/token/{self.idTSLA}/loan_payback_fee_pct/{self.idDUSD}'], '0.25')
        assert_equal(result[f'v0/token/{self.idGOOGL}/loan_payback_fee_pct/{self.idTSLA}'], '0.25')
        assert_equal(result[f'v0/token/{self.idTSLA}/loan_payback_fee_pct/{self.idTSLA}'], '0.25')

        # Check new balances and history
        for [address, amount] in self.funded_addresses:
            account = self.nodes[0].getaccount(address)
            new_amount = Decimal(account[0].split('@')[0])
            assert_equal(new_amount, amount * 2)
            history = self.nodes[0].listaccounthistory(address, {'txtype': 'TokenSplit'})
            assert_equal(len(history), 2)
            assert_equal(history[0]['amounts'][0], f'{-amount:.8f}' + f'@{self.symbolTSLA}/v1')
            assert_equal(history[1]['amounts'][0], account[0])

        # Token split
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}': f'{self.idTSLA}/-3'}})
        self.nodes[0].generate(2)

        # Check new balances
        minted = Decimal('0')
        for [address, amount] in self.funded_addresses:
            account = self.nodes[0].getaccount(address)
            amount_scaled = Decimal(truncate(str(amount * 2 / 3), 8))
            new_amount = Decimal(account[0].split('@')[0])
            assert_equal(new_amount, amount_scaled)
            minted += new_amount

        # Check token split correctly
        self.check_token_split(self.idTSLA, self.symbolTSLA, '/v2', -3, minted, True, True)

        # Swap old for new values
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'false'}})
        self.nodes[0].generate(1)

    def pool_split(self):

        # Check pool before split
        result = self.nodes[0].getpoolpair(self.idGD)[self.idGD]
        assert_equal(result['reserveA'], self.poolGDTotal)
        assert_equal(result['reserveB'], self.poolGDTotal)
        assert_equal(result['reserveA/reserveB'], Decimal('1.00000000'))
        assert_equal(result['reserveB/reserveA'], Decimal('1.00000000'))
        assert_equal(result['status'], True)
        assert_equal(result['tradeEnabled'], True)
        assert_equal(result['dexFeePctTokenA'], Decimal('0.01000000'))
        assert_equal(result['dexFeeInPctTokenA'], Decimal('0.01000000'))
        assert_equal(result['dexFeeOutPctTokenA'], Decimal('0.01000000'))
        assert_equal(result['dexFeePctTokenB'], Decimal('0.03000000'))
        assert_equal(result['dexFeeInPctTokenB'], Decimal('0.03000000'))
        assert_equal(result['dexFeeOutPctTokenB'], Decimal('0.03000000'))
        assert_equal(result['rewardPct'], Decimal('1.00000000'))
        assert_equal(result['rewardLoanPct'], Decimal('1.00000000'))

        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idGOOGL}': 'true'}})
        self.nodes[0].generate(1)

        # Token split
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}': f'{self.idGOOGL}/2'}})
        self.nodes[0].generate(2)

        # Check token split correctly
        self.check_token_split(self.idGOOGL, self.symbolGOOGL, '/v1', 2, str(self.poolGDTotal * 2), False, True)

        # Check pool migrated successfully
        self.check_pool_split(self.idGD, self.symbolGD, self.idGOOGL, self.symbolGOOGL, '/v1', self.poolGDTotal * 2,
                              Decimal('2.00000000'), Decimal('0.50000000'))

        # Swap old for new values
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]
        self.idGD = list(self.nodes[0].gettoken(self.symbolGD).keys())[0]

        # Check token split records in account history
        balances = self.nodes[0].gettokenbalances({'start': int(self.idGD), 'including_start': True, 'limit': 1}, False,
                                                  True)
        result = self.nodes[0].listaccounthistory(self.address, {"maxBlockHeight": self.nodes[0].getblockcount(),
                                                                 'txtype': 'TokenSplit', 'depth': 0})
        assert_equal(result[0]['owner'], self.address)
        assert_equal(result[0]['type'], 'TokenSplit')
        assert_equal(result[0]['amounts'], [f'-{self.poolGDTotal - Decimal("0.00001")}@{self.symbolGD}/v1'])
        assert_equal(result[1]['owner'], self.address)
        assert_equal(result[1]['type'], 'TokenSplit')
        assert_equal(result[1]['amounts'], balances)

        # Token split
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}': f'{self.idGOOGL}/-3'}})
        self.nodes[0].generate(2)

        # Check token split correctly
        minted = truncate(str(self.poolGDTotal * 2 / 3), 8)
        self.check_token_split(self.idGOOGL, self.symbolGOOGL, '/v2', -3, minted, False, True)

        # Check pool migrated successfully
        self.check_pool_split(self.idGD, self.symbolGD, self.idGOOGL, self.symbolGOOGL, '/v2', minted,
                              Decimal('0.66666666'), Decimal('1.50000000'))

        # Swap old for new values
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]

        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idGOOGL}': 'false'}})
        self.nodes[0].generate(1)

    def execute_vault_split(self, token_id, token_symbol, multiplier, suffix):

        # Get total minted
        if multiplier < 0:
            minted = truncate(str(self.nodes[0].gettoken(token_id)[token_id]['minted'] / abs(multiplier)), 8)
        else:
            minted = self.nodes[0].gettoken(token_id)[token_id]['minted'] * multiplier

        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{token_id}': 'true'}})
        self.nodes[0].generate(1)

        # Token split
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}': f'{token_id}/{multiplier}'}})
        self.nodes[0].generate(1)

        # Vault array
        self.vault_balances = []

        # Gather pre-split info to compare later
        pre_get_interest = self.nodes[0].getinterest('LOAN0001', 'NVDA')[0]
        for vault_info in self.nodes[0].listvaults():
            if self.skip_vault != vault_info['vaultId']:
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
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{token_id}': 'false'}})
        self.nodes[0].generate(1)

    def vault_split(self):

        # Create vault to test split after payback
        self.skip_vault = self.nodes[0].createvault(self.address, '')
        self.nodes[0].generate(1)

        # Deposit to vault
        self.nodes[0].deposittovault(self.skip_vault, self.address, f'100@{self.symbolDFI}')
        self.nodes[0].generate(1)

        # Take loan
        self.nodes[0].takeloan({
            'vaultId': self.skip_vault,
            'amounts': f'100@{self.symbolNVDA}'
        })
        self.nodes[0].generate(1)

        # Payback loan
        result = self.nodes[0].getvault(self.skip_vault)
        self.nodes[0].paybackloan({
            'vaultId': self.skip_vault,
            'from': self.address,
            'amounts': result['loanAmounts'][0]
        })
        self.nodes[0].generate(1)

        # Multiplier 2
        self.execute_vault_split(self.idNVDA, self.symbolNVDA, 2, '/v1')

        # Check split history
        result = self.nodes[0].listvaulthistory(self.vault_id,
                                                {'maxBlockHeight': self.nodes[0].getblockcount(), 'depth': 1})
        assert_equal(result[0]['address'], self.address)
        assert_equal(result[0]['type'], 'TokenSplit')
        assert_equal(result[0]['amounts'], [f'-{self.vault_loan}@{self.symbolNVDA}/v1'])
        assert_equal(result[1]['address'], self.address)
        assert_equal(result[1]['type'], 'TokenSplit')
        assert_equal(result[1]['amounts'], [f'{self.vault_loan * 2}@{self.symbolNVDA}'])

        # Swap old for new values
        self.idNVDA = list(self.nodes[0].gettoken(self.symbolNVDA).keys())[0]

        # Multiplier -3
        self.execute_vault_split(self.idNVDA, self.symbolNVDA, -3, '/v2')

        # Swap old for new values
        self.idNVDA = list(self.nodes[0].gettoken(self.symbolNVDA).keys())[0]

    def check_govvar_deletion(self):
        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'true'}})
        self.nodes[0].generate(1)

        # Token split
        split_height = self.nodes[0].getblockcount() + 2
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/oracles/splits/{split_height}': f'{self.idTSLA}/2'}})
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/oracles/splits/500000': f'{self.idTSLA}/2'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/oracles/splits/1000000': f'{self.idTSLA}/2'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/oracles/splits/1000001': f'{self.idNVDA}/2'}})
        self.nodes[0].generate(1)

        # Check splits
        result = self.nodes[0].listgovs("attrs")[0][0]['ATTRIBUTES']
        assert_equal(result[f'v0/oracles/splits/{split_height}'], f'{self.idTSLA}/2')
        assert_equal(result[f'v0/oracles/splits/500000'], f'{self.idTSLA}/2')
        assert_equal(result[f'v0/oracles/splits/1000000'], f'{self.idTSLA}/2')
        assert_equal(result[f'v0/oracles/splits/1000001'], f'{self.idNVDA}/2')

        # Split
        self.nodes[0].generate(1)

        # Check TSLA entries removed
        result = self.nodes[0].listgovs("attrs")[0][0]['ATTRIBUTES']
        assert (f'v0/oracles/splits/{split_height}' not in result)
        assert (f'v0/oracles/splits/500000' not in result)
        assert_equal(result[f'v0/oracles/splits/1000001'], f'{self.idNVDA}/2')

        # Swap old for new values
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'false'}})
        self.nodes[0].generate(1)

    def check_future_swap_refund(self):

        # Set all futures attributes
        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/params/dfip2203/reward_pct': '0.05',
            'v0/params/dfip2203/block_period': '2880'
        }})
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/params/dfip2203/active': 'true'
        }})
        self.nodes[0].generate(1)

        # Create addresses for futures
        address_msft = self.nodes[0].getnewaddress("", "legacy")
        address_locked = self.nodes[0].getnewaddress("", "legacy")

        # Get block to revert to
        revert_block = self.nodes[0].getblockcount()

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address, {address_msft: [f'1@{self.symbolMSFT}', f'1@{self.symbolDUSD}']})
        self.nodes[0].accounttoaccount(self.address, {address_locked: [f'1@{self.symbolMSFT}', f'1@{self.symbolDUSD}']})
        self.nodes[0].generate(1)

        # Create user futures contracts
        self.nodes[0].futureswap(address_msft, f'1@{self.symbolDUSD}', int(self.idMSFT))
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_msft, f'1@{self.symbolMSFT}')
        self.nodes[0].generate(1)

        # Check pending swaps
        result = self.nodes[0].listpendingfutureswaps()
        assert_equal(result[0]['owner'], address_msft)
        assert_equal(result[0]['source'], f'1.00000000@{self.symbolMSFT}')
        assert_equal(result[0]['destination'], self.symbolDUSD)
        assert_equal(result[1]['owner'], address_msft)
        assert_equal(result[1]['source'], f'1.00000000@{self.symbolDUSD}')
        assert_equal(result[1]['destination'], self.symbolMSFT)

        # Check balance empty
        result = self.nodes[0].getaccount(address_msft)
        assert_equal(result, [])

        # Token split
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}': f'{self.idMSFT}/2'}})
        self.nodes[0].generate(1)

        # Test creating future swaps with locked tokens
        assert_raises_rpc_error(-32600, 'Cannot create future swap for locked token', self.nodes[0].futureswap,
                                address_locked, f'1@{self.symbolDUSD}', int(self.idMSFT))
        assert_raises_rpc_error(-32600, 'Cannot create future swap for locked token', self.nodes[0].futureswap,
                                address_locked, f'1@{self.symbolMSFT}')

        # Move to split block
        self.nodes[0].generate(1)

        # Check balance returned with multiplier applied
        result = self.nodes[0].getaccount(address_msft)
        assert_equal(result, [f'1.00000000@{self.symbolDUSD}', f'2.00000000@{self.symbolMSFT}'])

        # Check future swap and token split account history
        result = self.nodes[0].listaccounthistory(address_msft,
                                                  {"maxBlockHeight": self.nodes[0].getblockcount(), 'depth': 0})
        assert_equal(result[0]['owner'], address_msft)
        assert_equal(result[0]['type'], 'FutureSwapRefund')
        assert_equal(result[0]['amounts'], [f'1.00000000@{self.symbolDUSD}'])
        assert_equal(result[1]['owner'], address_msft)
        assert_equal(result[1]['type'], 'FutureSwapRefund')
        assert_equal(result[1]['amounts'], [f'1.00000000@{self.symbolMSFT}/v2'])
        assert_equal(result[2]['owner'], address_msft)
        assert_equal(result[2]['type'], 'TokenSplit')
        assert_equal(result[2]['amounts'], [f'-1.00000000@{self.symbolMSFT}/v2'])
        assert_equal(result[3]['owner'], address_msft)
        assert_equal(result[3]['type'], 'TokenSplit')
        assert_equal(result[3]['amounts'], [f'2.00000000@{self.symbolMSFT}'])

        # Get block to move forward to after revert
        future_block = self.nodes[0].getblockcount()

        # Revert chain to before address funding
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(revert_block))
        self.nodes[0].clearmempool()
        self.nodes[0].generate(1)

        # Move forward again without split
        self.nodes[0].generate(future_block - self.nodes[0].getblockcount())
        assert_equal(self.nodes[0].getblockcount(), future_block)

        # Account history should now be empty
        result = self.nodes[0].listaccounthistory(address_msft,
                                                  {"maxBlockHeight": self.nodes[0].getblockcount(), 'depth': 0})
        assert_equal(result, [])

    def migrate_auction_batches(self):

        # Create vaults
        vault1 = self.nodes[0].createvault(self.address, 'LOAN0001')
        vault2 = self.nodes[0].createvault(self.address, 'LOAN0001')
        vault3 = self.nodes[0].createvault(self.address, 'LOAN0001')
        vault4 = self.nodes[0].createvault(self.address, 'LOAN0001')
        self.nodes[0].generate(1)

        # Fund vaults
        self.nodes[0].deposittovault(vault1, self.address, f'1@{self.symbolDFI}')
        self.nodes[0].deposittovault(vault2, self.address, f'2@{self.symbolDFI}')
        self.nodes[0].deposittovault(vault3, self.address, f'1@{self.symbolDUSD}')
        self.nodes[0].deposittovault(vault3, self.address, f'0.5@{self.symbolTWTR}')
        self.nodes[0].deposittovault(vault4, self.address, f'0.75@{self.symbolDFI}')
        self.nodes[0].deposittovault(vault4, self.address, f'0.75@{self.symbolBTC}')
        self.nodes[0].generate(1)

        # Take loans
        self.nodes[0].takeloan({
            'vaultId': vault1,
            'amounts': "1@MSFT"})
        self.nodes[0].takeloan({
            'vaultId': vault2,
            'amounts': "1@MSFT"})
        self.nodes[0].takeloan({
            'vaultId': vault2,
            'amounts': "1@MSFT"})
        self.nodes[0].takeloan({
            'vaultId': vault3,
            'amounts': "2@GOOGL"})
        self.nodes[0].generate(1)
        self.nodes[0].takeloan({
            'vaultId': vault4,
            'amounts': "1@NVDA"})
        self.nodes[0].generate(1)

        # Check collateral
        result = self.nodes[0].getvault(vault4)
        assert_equal(result['collateralRatio'], 150)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDUSD}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolGOOGL}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTSLA}"},
            {"currency": "USD", "tokenAmount": f"0.5@{self.symbolNVDA}"},
            {"currency": "USD", "tokenAmount": f"2@{self.symbolMSFT}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTWTR}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Check liquidation
        result = self.nodes[0].getvault(vault1)
        assert_equal(result['state'], 'inLiquidation')
        result = self.nodes[0].getvault(vault2)
        assert_equal(result['state'], 'inLiquidation')
        result = self.nodes[0].getvault(vault3)
        assert_equal(result['state'], 'inLiquidation')

        # Create bids
        self.nodes[0].placeauctionbid(vault1, 0, self.address, f"1.1@{self.symbolMSFT}")
        self.nodes[0].generate(1)

        # Token split
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}': f'{self.idMSFT}/2'}})
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 3)}': f'{self.idTWTR}/2'}})
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 4)}': f'{self.idNVDA}/2'}})
        self.nodes[0].generate(4)

        # Swap old for new values
        self.idMSFT = list(self.nodes[0].gettoken(self.symbolMSFT).keys())[0]
        self.idTWTR = list(self.nodes[0].gettoken(self.symbolTWTR).keys())[0]
        self.idNVDA = list(self.nodes[0].gettoken(self.symbolNVDA).keys())[0]

        # Assign results to auctions
        result = self.nodes[0].listauctions()
        vault1_result, vault2_result, vault3_result = {}, {}, {}
        for auctions in result:
            if auctions['vaultId'] == vault1:
                vault1_result = auctions
            elif auctions['vaultId'] == vault2:
                vault2_result = auctions
            elif auctions['vaultId'] == vault3:
                vault3_result = auctions

        # Check loan, bid and collateral amounts have multiplied
        assert_equal(vault1_result['batches'][0]['loan'], f'2.00000036@{self.symbolMSFT}')
        assert_equal(vault1_result['batches'][0]['highestBid']['amount'], f'2.20000000@{self.symbolMSFT}')
        assert_equal(vault1_result['batches'][0]['collaterals'], [f'1.00000000@{self.symbolDFI}'])
        assert_equal(vault2_result['batches'][0]['loan'], f'4.00000070@{self.symbolMSFT}')
        assert_equal(vault2_result['batches'][0]['collaterals'], [f'2.00000000@{self.symbolDFI}'])
        assert_equal(vault3_result['batches'][0]['loan'], f'2.00000035@{self.symbolGOOGL}')
        assert_equal(vault3_result['batches'][0]['collaterals'],
                     [f'1.00000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTWTR}'])

        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idMSFT}': 'false'}})
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTWTR}': 'false'}})
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idNVDA}': 'false'}})
        self.nodes[0].generate(1)

        # Check collateral ratio still the same
        result = self.nodes[0].getvault(vault4)
        assert_equal(result['collateralRatio'], 150)


if __name__ == '__main__':
    TokenSplitTest().main()
