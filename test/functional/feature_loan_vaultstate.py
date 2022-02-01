#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Vault State."""

from decimal import Decimal
from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

import calendar
import time

class VaultStateTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1']
            ]

    def run_test(self):
        self.nodes[0].generate(500)
        self.nodes[0].createtoken({
            "symbol": "DUSD",
            "name": "DUSD token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].createtoken({
            "symbol": "USD",
            "name": "USD token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[0].generate(1)

        # mint BTC
        self.nodes[0].minttokens("3000@BTC")
        self.nodes[0].generate(1)

        account = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # transfer DFI
        self.nodes[0].utxostoaccount({account: "4000@DFI"})
        self.nodes[0].generate(1)

        # setup oracle
        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"}, {"currency": "USD", "token": "TSLA"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(6)

        # set DFI an BTC as collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': "DFI",
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].setcollateraltoken({
                                    'token': "BTC",
                                    'factor': 1,
                                    'fixedIntervalPriceId': "BTC/USD"})
        self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla Token",
                            'fixedIntervalPriceId': "TSLA/USD",
                            'mintable': True,
                            'interest': 1})
        self.nodes[0].generate(6) # let active price update

        idDFI = list(self.nodes[0].gettoken("DFI").keys())[0]
        iddUSD = list(self.nodes[0].gettoken("DUSD").keys())[0]
        idTSLA = list(self.nodes[0].gettoken("TSLA").keys())[0]
        poolOwner = self.nodes[0].getnewaddress("", "legacy")

        # create pool USDT-DFI
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idTSLA,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-TSLA",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].minttokens("3000@TSLA")
        self.nodes[0].minttokens("3500@DUSD")
        self.nodes[0].generate(1)
        self.nodes[0].addpoolliquidity({
            account: ["1000@DUSD", "1000@TSLA"]
        }, account, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        }, [])

        self.nodes[0].minttokens("3000@DUSD")
        self.nodes[0].generate(1)
        self.nodes[0].addpoolliquidity({
            account: ["1000@DUSD", "1000@DFI"]
        }, account, [])
        self.nodes[0].generate(1)
        # Case 1
        # Create loan schemes
        self.nodes[0].createloanscheme(200, 1, 'LOAN200')
        self.nodes[0].generate(1)

        # Create vault
        vaultId1 = self.nodes[0].createvault(account, 'LOAN200')
        self.nodes[0].generate(1)
        vault1 = self.nodes[0].getvault(vaultId1)
        listvaults = self.nodes[0].listvaults()
        assert_equal(vault1["state"], "active")
        assert_equal(listvaults[0]["state"], "active")

        self.nodes[0].deposittovault(vaultId1, account, '5@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId1, account, '5@BTC')
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
                'vaultId': vaultId1,
                'amounts': "5@TSLA"})
        self.nodes[0].generate(1)
        # trigger liquidation
        oracle1_prices = [{"currency": "USD", "tokenAmount": "134@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(6) # let price enter invalid state

        vault1 = self.nodes[0].getvault(vaultId1)
        listvaults = self.nodes[0].listvaults()
        assert_equal(vault1["state"], "frozen")
        assert_equal(listvaults[0]["state"], "frozen")
        # test options
        listvaults = self.nodes[0].listvaults({"state": "frozen"})
        assert_equal(len(listvaults), 1)
        listvaults = self.nodes[0].listvaults({"state": "active"})
        assert_equal(len(listvaults), 0)
        self.nodes[0].generate(6) # let price update and trigger liquidation of vault

        vault1 = self.nodes[0].getvault(vaultId1)
        listvaults = self.nodes[0].listvaults()
        assert_equal(vault1["state"], "inLiquidation")
        assert_equal(listvaults[0]["state"], "inLiquidation")

        auctionlist = self.nodes[0].listauctions()
        assert_equal(auctionlist[0]["liquidationHeight"], 570)

        self.nodes[0].generate(36) # let auction end without bids
        auctionlist = self.nodes[0].listauctions()
        assert_equal(auctionlist[0]["liquidationHeight"], 607)
        assert_equal(len(auctionlist), 1)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(6) # update price
        self.nodes[0].generate(30) # let auction end without bids and open vault again

        vault1 = self.nodes[0].getvault(vaultId1)
        listvaults = self.nodes[0].listvaults()
        assert_equal(vault1["state"], "active")
        assert_equal(listvaults[0]["state"], "active")

        oracle1_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(6) # let price update

        vault1 = self.nodes[0].getvault(vaultId1)
        listvaults = self.nodes[0].listvaults()
        assert_equal(vault1["state"], "mayLiquidate")
        assert_equal(listvaults[0]["state"], "mayLiquidate")

        self.nodes[0].generate(6) # let vault enter liquidation state

        oracle1_prices = [{"currency": "USD", "tokenAmount": "136@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(6) # let price update

        vault1 = self.nodes[0].getvault(vaultId1)
        listvaults = self.nodes[0].listvaults()
        assert_equal(vault1["state"], "inLiquidation")
        assert_equal(listvaults[0]["state"], "inLiquidation")

if __name__ == '__main__':
    VaultStateTest().main()

