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


class AuctionsTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1',
             '-fortcanningheight=1']
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
        price_feeds1 = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"},
                        {"currency": "USD", "token": "TSLA"}, {"currency": "USD", "token": "GOOGL"},
                        {"currency": "USD", "token": "TWTR"}, {"currency": "USD", "token": "MSFT"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"},
                          {"currency": "USD", "tokenAmount": "100@GOOGL"},
                          {"currency": "USD", "tokenAmount": "100@TWTR"},
                          {"currency": "USD", "tokenAmount": "100@MSFT"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
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
        self.nodes[0].setloantoken({
            'symbol': "GOOGL",
            'name': "Google",
            'fixedIntervalPriceId': "GOOGL/USD",
            'mintable': True,
            'interest': 1})
        self.nodes[0].setloantoken({
            'symbol': "TWTR",
            'name': "Twitter",
            'fixedIntervalPriceId': "TWTR/USD",
            'mintable': True,
            'interest': 1})
        self.nodes[0].setloantoken({
            'symbol': "MSFT",
            'name': "Microsoft",
            'fixedIntervalPriceId': "MSFT/USD",
            'mintable': True,
            'interest': 1})
        self.nodes[0].generate(6)  # let active price update

        idDFI = list(self.nodes[0].gettoken("DFI").keys())[0]
        iddUSD = list(self.nodes[0].gettoken("DUSD").keys())[0]
        idTSLA = list(self.nodes[0].gettoken("TSLA").keys())[0]
        idGOOGL = list(self.nodes[0].gettoken("GOOGL").keys())[0]
        idTWTR = list(self.nodes[0].gettoken("TWTR").keys())[0]
        idMSFT = list(self.nodes[0].gettoken("MSFT").keys())[0]
        poolOwner = self.nodes[0].getnewaddress("", "legacy")

        # create pool DUSD-TSLA
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idTSLA,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-TSLA",
        }, [])
        self.nodes[0].generate(1)

        # create pool DUSD-GOOGL
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idGOOGL,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-GOOGL",
        }, [])

        # create pool DUSD-TWTR
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idTWTR,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-TWTR",
        }, [])

        # create pool DUSD-MSFT
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idMSFT,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-MSFT",
        }, [])

        # create pool DUSD-DFI
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        }, [])
        self.nodes[0].generate(1)

        # Mint tokens
        self.nodes[0].minttokens("3000@TSLA")
        self.nodes[0].minttokens("3000@GOOGL")
        self.nodes[0].minttokens("3000@TWTR")
        self.nodes[0].minttokens("3000@MSFT")
        self.nodes[0].minttokens("6500@DUSD")
        self.nodes[0].generate(1)

        # Add liquidity to DUSD-TSLA
        self.nodes[0].addpoolliquidity({
            account: ["1000@DUSD", "1000@TSLA"]
        }, account, [])

        # Add liquidity to DUSD-GOOGL
        self.nodes[0].addpoolliquidity({
            account: ["1000@DUSD", "1000@GOOGL"]
        }, account, [])

        # Add liquidity to DUSD-TWTR
        self.nodes[0].addpoolliquidity({
            account: ["1000@DUSD", "1000@TWTR"]
        }, account, [])

        # Add liquidity to DUSD-MSFT
        self.nodes[0].addpoolliquidity({
            account: ["1000@DUSD", "1000@MSFT"]
        }, account, [])

        # Add liquidity to DUSD-DFI
        self.nodes[0].addpoolliquidity({
            account: ["1000@DUSD", "1000@DFI"]
        }, account, [])
        self.nodes[0].generate(1)

        # Case 1
        # Create loan schemes
        self.nodes[0].createloanscheme(200, 1, 'LOAN200')
        self.nodes[0].createloanscheme(120, 5, 'LOAN120')
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
        oracle1_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault

        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist), 1)
        assert_equal(auctionlist[0]["liquidationHeight"], 570)

        self.nodes[0].generate(36)  # let auction end without bids
        auctionlist = self.nodes[0].listauctions()
        assert_equal(len(auctionlist), 1)
        assert_equal(auctionlist[0]["liquidationHeight"], 607)

        # Case 2
        # reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault

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

        oracle1_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2["state"], "inLiquidation")
        assert_equal(vault2["batches"][0]["collaterals"], ['49.99999980@DFI', '49.99999980@BTC'])
        assert_equal(vault2["batches"][1]["collaterals"], ['10.00000020@DFI', '10.00000020@BTC'])

        self.nodes[0].placeauctionbid(vaultId2, 0, account, "59.41@TSLA")

        self.nodes[0].generate(35)  # let auction end

        interest = self.nodes[0].getinterest('LOAN200', "TSLA")
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2["state"], "active")
        assert_equal(interest[0]["interestPerBlock"], Decimal(vault2["interestAmounts"][0].split('@')[0]))
        assert_greater_than(Decimal(vault2["collateralAmounts"][0].split('@')[0]), Decimal(10.00000020))
        assert_equal(vault2["informativeRatio"], Decimal("264.70081509"))
        self.nodes[0].paybackloan({
            'vaultId': vaultId2,
            'from': account,
            'amounts': vault2["loanAmounts"]})
        self.nodes[0].generate(1)
        self.nodes[0].closevault(vaultId2, account)

        # Case 3
        # reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update

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

        oracle1_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault

        self.nodes[0].placeauctionbid(vaultId3, 0, account, "54.46@TSLA")
        self.nodes[0].generate(31)  # let auction end
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
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update

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
        oracle1_prices = [{"currency": "USD", "tokenAmount": "101@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault

        self.nodes[0].placeauctionbid(vaultId4, 0, account, "7.92@TSLA")
        self.nodes[0].generate(31)  # let auction end

        vault4 = self.nodes[0].getvault(vaultId4)
        assert_equal(len(vault4["loanAmounts"]), 0)
        self.nodes[0].generate(3)
        assert_equal(len(vault4["loanAmounts"]), 0)
        assert_equal(len(vault4["interestAmounts"]), 0)
        collateralAmount = Decimal(vault4["collateralAmounts"][0].split("@")[0])
        accountDFIBalance = Decimal(self.nodes[0].getaccount(account)[0].split("@")[0])

        self.nodes[0].withdrawfromvault(vaultId4, account, "2.50710426@DFI")
        self.nodes[0].generate(1)
        assert_equal(Decimal(self.nodes[0].getaccount(account)[0].split("@")[0]), collateralAmount + accountDFIBalance)

        vault4 = self.nodes[0].getvault(vaultId4)
        assert_equal(len(vault4["collateralAmounts"]), 0)

        self.nodes[0].closevault(vaultId4, account)
        self.nodes[0].generate(1)

        assert_equal(Decimal(self.nodes[0].getaccount(account)[0].split("@")[0]),
                     Decimal(0.5) + collateralAmount + accountDFIBalance)

        # Case 5
        # reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update

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
        oracle1_prices = [{"currency": "USD", "tokenAmount": "330@TSLA"}, {"currency": "USD", "tokenAmount": "220@DFI"},
                          {"currency": "USD", "tokenAmount": "220@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault
        vault5 = self.nodes[0].getvault(vaultId5)
        assert_equal(len(vault5["batches"]), 5)

        self.nodes[0].placeauctionbid(vaultId5, 0, account, "29.70@TSLA")
        self.nodes[0].generate(1)
        self.nodes[0].placeauctionbid(vaultId5, 4, account, "10@TSLA")
        self.nodes[0].generate(1)

        self.nodes[0].generate(32)  # let auction end

        vault5 = self.nodes[0].getvault(vaultId5)
        assert_equal(len(vault5["batches"]), 4)

        # Case 6 Test two loan token
        # Reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"},
                          {"currency": "USD", "tokenAmount": "100@GOOGL"},
                          {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update
        vaultId6 = self.nodes[0].createvault(account, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId6, account, '200@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId6, account, '200@BTC')
        self.nodes[0].generate(1)

        # Take TSLA loan
        self.nodes[0].takeloan({
            'vaultId': vaultId6,
            'amounts': "172@TSLA"})

        # Take GOOGL loan
        self.nodes[0].takeloan({
            'vaultId': vaultId6,
            'amounts': "18@GOOGL"})
        self.nodes[0].generate(1)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "330@TSLA"},
                          {"currency": "USD", "tokenAmount": "330@GOOGL"},
                          {"currency": "USD", "tokenAmount": "220@DFI"}, {"currency": "USD", "tokenAmount": "220@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault
        vault6 = self.nodes[0].getvault(vaultId6)

        batches = vault6['batches']
        assert_equal(len(batches), 9)

        collateralDFI = Decimal('0')
        collateralBTC = Decimal('0')
        for batch in batches:
            collaterals = batch['collaterals']
            assert_equal(len(collaterals), 2)
            DFI = Decimal(collaterals[0].replace('@DFI', ''))
            BTC = Decimal(collaterals[1].replace('@BTC', ''))
            assert (DFI * Decimal('220') + BTC * Decimal('220') < 10000)
            collateralDFI += DFI
            collateralBTC += BTC

        assert_equal(collateralDFI, Decimal('200.00000000'))
        assert_equal(collateralBTC, Decimal('200.00000000'))

        # Case 7 With max possible oracle deviation. Loantoken value 100 -> 129 && collateral value 100 -> 71
        # Loan value should end up greater than collateral value
        # Reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"}, {"currency": "USD", "tokenAmount": "100@DFI"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update

        vaultId7 = self.nodes[0].createvault(account, 'LOAN120')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId7, account, '100@DFI')
        self.nodes[0].generate(1)

        # Take TSLA loan
        self.nodes[0].takeloan({
            'vaultId': vaultId7,
            'amounts': "80@TSLA"})
        self.nodes[0].generate(1)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "129@TSLA"}, {"currency": "USD", "tokenAmount": "71@DFI"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault
        vault7 = self.nodes[0].getvault(vaultId7)

        batches = vault7['batches']
        assert_equal(len(batches), 1)

        loan_amount = batches[0]["loan"].split("@")[0]
        loan_value = float(loan_amount) * 129  # TSLA USD value
        collateral_amount = batches[0]["collaterals"][0].split("@")[0]
        collateral_value = float(collateral_amount) * 71  # DFI USD value
        assert (loan_value > collateral_value)

        # Case 8 With max possible oracle deviation. Loantoken value 100 -> 129 && collateral value 100 -> 71. Multi tokens
        # Reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"},
                          {"currency": "USD", "tokenAmount": "100@GOOGL"},
                          {"currency": "USD", "tokenAmount": "100@DFI"}, {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update

        vaultId7 = self.nodes[0].createvault(account, 'LOAN120')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId7, account, '100@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId7, account, '100@BTC')
        self.nodes[0].generate(1)

        # Take TSLA loan
        self.nodes[0].takeloan({
            'vaultId': vaultId7,
            'amounts': "100@TSLA"})
        # Take GOOGL loan
        self.nodes[0].takeloan({
            'vaultId': vaultId7,
            'amounts': "60@GOOGL"})
        self.nodes[0].generate(1)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "129@TSLA"},
                          {"currency": "USD", "tokenAmount": "129@GOOGL"}, {"currency": "USD", "tokenAmount": "71@DFI"},
                          {"currency": "USD", "tokenAmount": "71@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault
        vault7 = self.nodes[0].getvault(vaultId7)

        batches = vault7['batches']
        assert_equal(len(batches), 2)
        for batch in batches:
            assert_equal(len(batch['collaterals']), 2)
        loan_amount_TSLA = batches[0]["loan"].split("@")[0]
        loan_value_TSLA = float(loan_amount_TSLA) * 129  # TSLA USD value
        collateral_amount = float(batches[0]["collaterals"][0].split("@")[0]) + float(
            batches[0]["collaterals"][1].split("@")[0])
        collateral_value = collateral_amount * 71  # collaterals USD value
        assert (loan_value_TSLA > collateral_value)

        loan_amount_GOOGL = batches[1]["loan"].split("@")[0]
        loan_value_GOOGL = float(loan_amount_GOOGL) * 129  # GOOGL USD value
        collateral_amount = float(batches[1]["collaterals"][0].split("@")[0]) + float(
            batches[1]["collaterals"][1].split("@")[0])
        collateral_value = float(collateral_amount) * 71  # collaterals USD value
        assert (loan_value_GOOGL > collateral_value)

        # Case 9 Auction with dust amount
        # Reset prices
        oracle1_prices = [{"currency": "USD", "tokenAmount": "100@TSLA"},
                          {"currency": "USD", "tokenAmount": "100@GOOGL"},
                          {"currency": "USD", "tokenAmount": "100@TWTR"},
                          {"currency": "USD", "tokenAmount": "100@MSFT"}, {"currency": "USD", "tokenAmount": "100@DFI"},
                          {"currency": "USD", "tokenAmount": "100@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update

        vaultId8 = self.nodes[0].createvault(account, 'LOAN200')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId8, account, '100@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId8, account, '100@BTC')
        self.nodes[0].generate(1)

        # Take TSLA loan
        self.nodes[0].takeloan({
            'vaultId': vaultId8,
            'amounts': "25@TSLA"})

        # Take GOOGL loan
        self.nodes[0].takeloan({
            'vaultId': vaultId8,
            'amounts': "25@GOOGL"})

        # Take TWTR loan
        self.nodes[0].takeloan({
            'vaultId': vaultId8,
            'amounts': "25@TWTR"})

        # Take MSFT loan
        self.nodes[0].takeloan({
            'vaultId': vaultId8,
            'amounts': "25@MSFT"})
        self.nodes[0].generate(1)

        vault8 = self.nodes[0].getvault(vaultId8)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "300@TSLA"},
                          {"currency": "USD", "tokenAmount": "300@GOOGL"},
                          {"currency": "USD", "tokenAmount": "300@TWTR"},
                          {"currency": "USD", "tokenAmount": "300@MSFT"},
                          {"currency": "USD", "tokenAmount": "200.0001@DFI"},
                          {"currency": "USD", "tokenAmount": "200.0001@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(12)  # let price update and trigger liquidation of vault

        vault8 = self.nodes[0].getvault(vaultId8)
        batches = vault8['batches']
        assert_equal(len(batches), 8)
        batches = sorted(batches, key=lambda k: k['loan'])
        assert_equal(batches[0]["loan"], '0.00001251@GOOGL')
        assert_equal(batches[1]["loan"], '0.00001251@MSFT')
        assert_equal(batches[2]["loan"], '0.00001251@TSLA')
        assert_equal(batches[3]["loan"], '0.00001251@TWTR')


if __name__ == '__main__':
    AuctionsTest().main()
