#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test automation of stabilisation fee."""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal
from decimal import Decimal

import time
import calendar

def truncate(str, decimal):
    return str if not str.find('.') + 1 else str[:str.find('.') + decimal + 1]

class AutomationStabilisationFeeTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningspringheight=1', '-fortcanningroadheight=1', '-fortcanningcrunchheight=1', '-fortcanninggreatworldheight=1', '-fortcanningepilogueheight=1', '-grandcentralheight=1', '-jellyfish_regtest=1']]

    def run_test(self):

        # Setup Oracles
        self.setup_test_oracles()

        # Create tokens for tests
        self.setup_test_tokens()

        # Setup pools
        self.setup_test_pools()

        # Setup vault, take loan and fund DFI-DUSD pool
        self.setup_vault_fund_pool()

        # Set up future swap
        self.setup_future_swap()

        # Create DEx fee tokens and FutureSwap burn amounts (removed from minted to get circulating supply)
        self.setup_burn_amounts()

        # Create unbacked DUSD with DFI payback
        self.setup_unbacked_dusd()

        # Test automation of stabilisation fee
        self.test_stabilisation_fee()

    def setup_test_oracles(self):

        # Generate chain
        self.nodes[0].generate(120)

        # Get MN address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Token symbols
        self.symbolDFI = "DFI"
        self.symbolDUSD = "DUSD"
        self.symbolTSLA = "TSLA"
        self.symbolDD = 'DUSD-DFI'

        # Create Oracle address
        oracle_address = self.nodes[0].getnewaddress("", "legacy")

        # Define price feeds
        price_feed = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "TSLA"}
        ]

        # Appoint Oracle
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTSLA}"},
        ]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle, timestamp, oracle_prices)
        self.nodes[0].generate(1)

    def setup_test_tokens(self):

        # Create loan tokens
        self.nodes[0].setloantoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            'fixedIntervalPriceId': f"{self.symbolDUSD}/USD",
            'mintable': True,
            'interest': -1
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

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
            'token': self.symbolDFI,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolDFI}/USD"
        })
        self.nodes[0].generate(120)

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 1, 'LOAN001')
        self.nodes[0].generate(1)

        # Store DUSD ID
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]

        # Create DFI tokens
        self.nodes[0].utxostoaccount({self.address: "10000000@" + self.symbolDFI})
        self.nodes[0].generate(1)

    def setup_test_pools(self):

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDUSD,
            "tokenB": self.symbolDFI,
            "commission": 0,
            "status": True,
            "ownerAddress": self.address,
            "symbol" : self.symbolDD
        })
        self.nodes[0].generate(1)

        # Store pool ID
        self.idDD = list(self.nodes[0].gettoken(self.symbolDD).keys())[0]

        # Set 99% stability fee on DUSD
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.idDD}/token_a_fee_pct': '0.99',
            f'v0/poolpairs/{self.idDD}/token_a_fee_direction': 'in'
        }})
        self.nodes[0].generate(1)

    def setup_vault_fund_pool(self):

        # Create vault
        self.vault_address = self.nodes[0].getnewaddress('', 'legacy')
        self.vault_id = self.nodes[0].createvault(self.vault_address)
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {self.vault_address: f"10000000@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(self.vault_id, self.vault_address, f"4000000@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": self.vault_id, "amounts": f"2000000@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Fund pool
        self.nodes[0].addpoolliquidity({
            self.vault_address: [f"1000000@{self.symbolDUSD}", f"1000000@{self.symbolDFI}"]
        }, self.vault_address)
        self.nodes[0].generate(1)

    def setup_future_swap(self):

        # Set all futures swap attributes
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2203/reward_pct':'0.05','v0/params/dfip2203/block_period':'10'}})
        self.nodes[0].generate(1)

        # Set future swap to active
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2203/active':'true'}})
        self.nodes[0].generate(1)

    def setup_burn_amounts(self):

        # Trade on pool to generate DEx fee token
        self.nodes[0].poolswap({
            "from": self.vault_address,
            "tokenFrom": self.symbolDUSD,
            "amountFrom": 101010.10101011, # 100,000 DEx fee
            "to": self.vault_address,
            "tokenTo": self.symbolDFI,
        })
        self.nodes[0].generate(1)

        # Burn DUSD for TSLA in FutureSwap
        self.nodes[0].futureswap(self.vault_address, f'100000@{self.symbolDUSD}', self.symbolTSLA)
        self.nodes[0].generate(10)

    def setup_unbacked_dusd(self):

        # Enable loan payback
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/payback_dfi':'true'}})
        self.nodes[0].generate(1)

        # Disable any fee
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/payback_dfi_fee_pct':'0'}})
        self.nodes[0].generate(1)

        # Payback DUSD loan with DFI to create unbacked DUSD
        self.nodes[0].paybackloan({
            'vaultId': self.vault_id,
            'from': self.vault_address,
            'amounts': f"1500000@{self.symbolDFI}"
        })
        self.nodes[0].generate(1)

    def test_stabilisation_fee(self):
        # Minted DUSD 2,000,000
        # DEx Fee Tokens 100,000
        # Future Swap Burn 100,000
        # Circulating DUSD 1,800,800 (Minted - DEx Fee Tokens - Future Swap Burn)
        # Loan DUSD 500,000
        # Algo DUSD 1,300,000 (Circulating DUSD - Loan DUSD)
        # Ratio 72.222222%
        # DEx fee 4.416116%

        # Get values required to verify auto stabilisation fee
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        minted_dusd = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]['minted']
        burned = self.nodes[0].getaccount('mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')[1].split('@')[0]
        future_swap = attributes['v0/live/economy/dfip2203_burned'][0].split('@')[0]
        circulating_dusd = minted_dusd - Decimal(burned) - Decimal(future_swap)
        loan_dusd = attributes['v0/live/economy/loans'][0].split('@')[0]
        algo_dusd = circulating_dusd - Decimal(loan_dusd)
        ratio = truncate(str(algo_dusd / circulating_dusd), 8)
        auto_fee = truncate(str(((2**((Decimal(ratio) * 100 - 30)/10)) - 1 ) / 400), 8)

        # Enable stability fee on pool
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/poolpairs/{self.idDD}/auto_dusd_fee':'true'}})
        self.nodes[0].generate(1)

        # Check auto DUSD fee enabled and set
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes[f'v0/poolpairs/{self.idDD}/auto_dusd_fee'], 'true')
        assert_equal(attributes[f'v0/poolpairs/{self.idDD}/token_a_fee_pct'], auto_fee)

if __name__ == '__main__':
    AutomationStabilisationFeeTest().main()
