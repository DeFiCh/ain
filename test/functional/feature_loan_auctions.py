#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Auctions."""

from decimal import Decimal
from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_greater_than

import calendar
import time

class AuctionsTest (DefiTestFramework):
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

        self.nodes[0].deposittovault(vaultId1, account, '5@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId1, account, '5@BTC')
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
                'vaultId': vaultId1,
                'amounts': "5@TSLA"})
        self.nodes[0].generate(1)
        # trigger liquidation
        oracle1_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12) # let price update and trigger liquidation of vault

        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist), 1)
        assert_equal(auctionlist[0]["liquidationHeight"], 570)

        self.nodes[0].generate(36) # let auction end without bids
        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist), 1)
        assert_equal(auctionlist[0]["liquidationHeight"], 607)

        # Case 2
        # reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12) # let price update and trigger liquidation of vault

        vaultId2 = self.nodes[0].createvault(account, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId2, account, '60@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId2, account, '60@BTC')
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
                'vaultId': vaultId2,
                'amounts': "60@TSLA"})
        self.nodes[0].generate(1)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12) # let price update and trigger liquidation of vault
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2["state"], "inLiquidation")
        assert_equal(vault2["batches"][0]["collaterals"], ['49.99999980@DFI', '49.99999980@BTC'])
        assert_equal(vault2["batches"][1]["collaterals"], ['10.00000020@DFI', '10.00000020@BTC'])

        self.nodes[0].placeauctionbid(vaultId2, 0, account, "59.41@TSLA")

        self.nodes[0].generate(34) # let auction end

        interest = self.nodes[0].getinterest('LOAN200', "TSLA")
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2["state"], "active")
        assert_equal(interest[0]["interestPerBlock"], Decimal(vault2["interestAmounts"][0].split('@')[0]))
        assert_greater_than(Decimal(vault2["collateralAmounts"][0].split('@')[0]), Decimal(10.00000020))
        assert_equal(vault2["informativeRatio"], Decimal("264.70111158"))
        self.nodes[0].paybackloan({
                    'vaultId': vaultId2,
                    'from': account,
                    'amounts': vault2["loanAmounts"]})
        self.nodes[0].generate(1)
        self.nodes[0].closevault(vaultId2, account)

        # Case 3
        # reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12) # let price update

        vaultId3 = self.nodes[0].createvault(account, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId3, account, '60@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId3, account, '60@BTC')
        self.nodes[0].generate(1)
        vault3 = self.nodes[0].getvault(vaultId3)

        self.nodes[0].takeloan({
                'vaultId': vaultId3,
                'amounts': "60@TSLA"})
        self.nodes[0].generate(1)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12) # let price update and trigger liquidation of vault

        self.nodes[0].placeauctionbid(vaultId3, 0, account, "54.46@TSLA")
        self.nodes[0].generate(31) # let auction end
        vault3 = self.nodes[0].getvault(vaultId3)
        assert_equal(vault3["state"], "active")
        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist), 1)
        assert_greater_than(Decimal(vault3["collateralAmounts"][0].split('@')[0]), Decimal(10.00000020))

        self.nodes[0].paybackloan({
                    'vaultId': vaultId3,
                    'from': account,
                    'amounts': vault3["loanAmounts"]})
        self.nodes[0].generate(1)
        self.nodes[0].closevault(vaultId3, account)

        # Case 4
        # reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12) # let price update

        vaultId4 = self.nodes[0].createvault(account, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId4, account, '5@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId4, account, '5@BTC')
        self.nodes[0].generate(1)
        self.nodes[0].takeloan({
                'vaultId': vaultId4,
                'amounts': "5@TSLA"})
        self.nodes[0].generate(1)
        oracle1_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12) # let price update and trigger liquidation of vault

        self.nodes[0].placeauctionbid(vaultId4, 0, account, "7.92@TSLA")
        self.nodes[0].generate(31) # let auction end

        vault4 = self.nodes[0].getvault(vaultId4)
        assert_equal(len(vault4["loanAmounts"]), 0)
        self.nodes[0].generate(3)
        assert_equal(len(vault4["loanAmounts"]), 0)
        assert_equal(len(vault4["interestAmounts"]), 0)
        collateralAmount = Decimal(vault4["collateralAmounts"][0].split("@")[0])
        accountDFIBalance = Decimal(self.nodes[0].getaccount(account)[0].split("@")[0])

        self.nodes[0].withdrawfromvault(vaultId4, account, "2.50710426@DFI")
        self.nodes[0].generate(1)
        assert_equal(Decimal(self.nodes[0].getaccount(account)[0].split("@")[0]), collateralAmount+accountDFIBalance)

        vault4 = self.nodes[0].getvault(vaultId4)
        assert_equal(len(vault4["collateralAmounts"]), 0)

        self.nodes[0].closevault(vaultId4, account)
        self.nodes[0].generate(1)

        assert_equal(Decimal(self.nodes[0].getaccount(account)[0].split("@")[0]), Decimal(0.5) + collateralAmount + accountDFIBalance)

        # Case 5
        # reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12) # let price update

        vaultId5 = self.nodes[0].createvault(account, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId5, account, '100@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId5, account, '100@BTC')
        self.nodes[0].generate(1)
        self.nodes[0].takeloan({
                'vaultId': vaultId5,
                'amounts': "100@TSLA"})
        self.nodes[0].generate(1)
        oracle1_prices = [{"currency": "USD", "tokenAmount": "330@TSLA"}, {"currency": "USD", "tokenAmount": "220@DFI"}, {"currency": "USD", "tokenAmount": "220@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12) # let price update and trigger liquidation of vault
        vault5 = self.nodes[0].getvault(vaultId5)
        assert_equal(len(vault5["batches"]), 5)

        self.nodes[0].placeauctionbid(vaultId5, 0, account, "29.70@TSLA")
        self.nodes[0].generate(1)
        self.nodes[0].placeauctionbid(vaultId5, 4, account, "10@TSLA")
        self.nodes[0].generate(1)

        self.nodes[0].generate(32) # let auction end

        vault5 = self.nodes[0].getvault(vaultId5)
        assert_equal(len(vault5["batches"]), 4)

if __name__ == '__main__':
    AuctionsTest().main()
