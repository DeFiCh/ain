#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - interest test."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

import calendar
import time
from decimal import Decimal, getcontext, ROUND_UP

class LowInterestTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=2', '-fortcanningheight=3', '-fortcanningmuseumheight=4', '-fortcanningparkheight=5', '-fortcanninghillheight=132', '-jellyfish_regtest=1', '-txindex=1']
        ]

    def run_test(self):
        blocksPerDay = Decimal('144')
        n0 = self.nodes[0]
        # n1 = self.nodes[1]

        print("Generating initial chain...")
        n0.generate(100) # get initial UTXO balance from immature to trusted -> check getbalances()
        self.sync_blocks()

        account0 = n0.get_genesis_keys().ownerAuthAddress

        symbolDFI = "DFI"
        symbolDOGE = "DOGE"
        symboldUSD = "DUSD"

        n0.utxostoaccount({account0: "10000000@" + symbolDFI})
        n0.generate(1)
        self.sync_blocks()

        n0.generate(1)
        self.sync_blocks()

        oracle_address1 = n0.getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "DOGE"}]
        oracle_id1 = n0.appointoracle(oracle_address1, price_feeds1, 10)
        n0.generate(1)
        self.sync_blocks()

        # feed oracle
        oracle1_prices = [{"currency": "USD", "tokenAmount": "0.01@DOGE"},
                          {"currency": "USD", "tokenAmount": "10@DFI"}]
        timestamp = calendar.timegm(time.gmtime())
        n0.setoracledata(oracle_id1, timestamp, oracle1_prices)
        n0.generate(1)
        self.sync_blocks()

        n0.setloantoken({
                    'symbol': symboldUSD,
                    'name': "DUSD stable token",
                    'fixedIntervalPriceId': "DUSD/USD",
                    'mintable': True,
                    'interest': 0})

        tokenInterestPercentage = 1

        n0.setloantoken({
                    'symbol': symbolDOGE,
                    'name': "DOGE token",
                    'fixedIntervalPriceId': "DOGE/USD",
                    'mintable': True,
                    'interest': tokenInterestPercentage})
        n0.generate(1)
        self.sync_blocks()

        n0.minttokens("100000@DOGE")
        n0.generate(1)
        self.sync_blocks()
        iddUSD = list(n0.gettoken(symboldUSD).keys())[0]
        idDFI = list(n0.gettoken(symbolDFI).keys())[0]
        idDOGE = list(n0.gettoken(symbolDOGE).keys())[0]

        n0.setcollateraltoken({
                    'token': idDFI,
                    'factor': 1,
                    'fixedIntervalPriceId': "DFI/USD"})

        n0.generate(1)
        self.sync_blocks()


        poolOwner = n0.getnewaddress("", "legacy")

        # create pool DUSD-DFI
        n0.createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        }, [])

        n0.createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDOGE,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DOGE",
        }, [])

        n0.minttokens("2000000@" + symboldUSD)
        n0.generate(1)
        self.sync_blocks()

        n0.addpoolliquidity({
            account0: ["1000000@" + symboldUSD, "100000@" + symbolDFI]
        }, account0, [])
        n0.generate(1)
        self.sync_blocks()

        n0.addpoolliquidity({
            account0: ["1000000@" + symboldUSD, "1000@" + symbolDOGE]
        }, account0, [])
        n0.generate(1)
        self.sync_blocks()


        tokenInterest = Decimal(tokenInterestPercentage/100)
        n0.generate(1)
        self.sync_blocks()


        loanInterestPercentage = 1;
        n0.createloanscheme(150, loanInterestPercentage, 'LOAN150')

        loanInterest = Decimal(loanInterestPercentage/100)

        n0.generate(5)
        self.sync_blocks()

        # create vaults
        vaultIds = [n0.createvault(account0, 'LOAN150') for _ in range(4)]
        n0.generate(1)
        self.sync_blocks()
        # deposit 1DFI to vault list
        for vaultId in vaultIds:
            n0.deposittovault(vaultId, account0, "1@DFI")
        n0.generate(1)
        self.sync_blocks()


        # Fort Canning Park

        getcontext().prec = 8
        # Loan 0 vault0
        # Interest amount per block is under 1Sat so it is rounded to 1Sat and added in each block
        loanAmount0 = Decimal('0.00657')
        n0.takeloan({
                    'vaultId': vaultIds[0],
                    'amounts': str(loanAmount0) + "@" + symbolDOGE}) # 0.000657*(( 0.01 + 0.01 ) / ( 365 * 144 )) = 0.25 Sat/block
                                                                     # loan    *((tokenInterest% + LoanInterest%) /(daysYear * blocksDay))
        n0.generate(1)
        self.sync_blocks()
        # save height in which loan was taken to calculate interest per block
        loanHeight0 = n0.getblockcount()

        n0.generate(5)
        vault0 = n0.getvault(vaultIds[0])

        blocksSinceLoan0 = (n0.getblockcount() - loanHeight0) + 1 # +1 as interest shown is calculated with next height
        interestAmountLoan0 = Decimal(blocksSinceLoan0) * Decimal('0.00000001') # 1 sat per block
        returnedInterest0 = Decimal(vault0["interestAmounts"][0].split('@')[0])
        assert_equal(returnedInterest0, interestAmountLoan0)



        # Loan 1 vault 1
        # interest generated by loan is higher than 1Sat so calculations must be the same as post-fork
        loanAmount1 = Decimal('0.657')
        n0.takeloan({
                    'vaultId': vaultIds[1],
                    'amounts': str(loanAmount1) + "@" + symbolDOGE}) # 0.657*(( 0.01 + 0.01 ) / ( 365 * 144 )) = 25 Sat/block
        n0.generate(1)
        self.sync_blocks()
        loanHeight1 = n0.getblockcount()

        interestBlockLoan1 = loanAmount1 * ((loanInterest + tokenInterest) / (blocksPerDay * 365))

        n0.generate(5)
        vault1 = n0.getvault(vaultIds[1])
        blocksSinceLoan1 = (n0.getblockcount() - loanHeight1) + 1
        interestAmountLoan1 = Decimal(blocksSinceLoan1)*Decimal(interestBlockLoan1)
        returnedInterest1 = Decimal(vault1["interestAmounts"][0].split('@')[0])
        assert_equal(returnedInterest1, interestAmountLoan1)


        # Fort Canning Hill with high precision interest

        # Height 132
        n0.generate(5)

        blockChainInfo = n0.getblockchaininfo()
        assert_equal(blockChainInfo["softforks"]["fortcanninghill"]["active"], True)



        # Loan 2 vault2
        # Interest amount per block is under 1Sat interest is kept to 0.25 Sat/block and only total interest is ceiled
        loanAmount2 = Decimal('0.00657')
        n0.takeloan({
                    'vaultId': vaultIds[2],
                    'amounts': str(loanAmount2) + "@" + symbolDOGE})

        interestBlockLoan2 = loanAmount2 * ((loanInterest + tokenInterest) / (blocksPerDay * 365))
        n0.generate(1)
        self.sync_blocks()

        loanHeight2 = n0.getblockcount()

        n0.generate(4)
        vault2 = n0.getvault(vaultIds[2])

        blocksSinceLoan2 = (n0.getblockcount() - loanHeight2) + 1 # +1 as interest shown is calculated with next height
        interestAmountLoan2 = Decimal(blocksSinceLoan2) * Decimal(interestBlockLoan2)
        returnedInterest2 = Decimal(vault2["interestAmounts"][0].split('@')[0])
        # ceiling
        interestAmountLoanCeil2 = interestAmountLoan2.quantize(Decimal('1E-8'), rounding=ROUND_UP)
        assert_equal(returnedInterest2, interestAmountLoanCeil2)


        # Loan 3 vault 3
        # interest generated by loan is higher than 1Sat so calculations must be the same as pre-fork
        loanAmount3 = Decimal('0.657')
        n0.takeloan({
                    'vaultId': vaultIds[3],
                    'amounts': str(loanAmount3) + "@" + symbolDOGE}) # 0.657*(( 0.01 + 0.01 ) / ( 365 * 144 )) = 25 Sat/block
        n0.generate(1)
        self.sync_blocks()
        loanHeight3 = n0.getblockcount()

        interestBlockLoan3 = loanAmount3 * ((loanInterest + tokenInterest) / (blocksPerDay * 365))

        n0.generate(5)
        vault3 = n0.getvault(vaultIds[3])

        blocksSinceLoan3 = (n0.getblockcount() - loanHeight3) + 1 # +1 as interest shown is calculated with next height
        interestAmountLoan3 = Decimal(blocksSinceLoan3) * Decimal(interestBlockLoan3)
        returnedInterest3 = Decimal(vault3["interestAmounts"][0].split('@')[0])
        # ceiling
        interestAmountLoanCeil3 = interestAmountLoan3.quantize(Decimal('1E-8'), rounding=ROUND_UP)
        assert_equal(returnedInterest3, interestAmountLoanCeil3)

        # compare results of pre/post fork calculations when interest per block is bigger than 1Sat
        assert_equal(returnedInterest1, returnedInterest3)


        # Loan 4 vault 0
        # Take another loan in vault 0 post fork and see how interests add up
        # (1Sat + Loan4 interest) will be added each block
        loanAmount4 = Decimal('0.00657')
        n0.takeloan({
                    'vaultId': vaultIds[0],
                    'amounts': str(loanAmount4) + "@" + symbolDOGE})
        n0.generate(1)
        self.sync_blocks()
        loanHeight4 = n0.getblockcount()

        interestBlockLoan4 = loanAmount4 * ((loanInterest + tokenInterest) / (blocksPerDay * 365))

        n0.generate(5)
        vault0 = n0.getvault(vaultIds[0])

        blocksSinceLoan0 = (n0.getblockcount() - loanHeight0) + 1
        blocksSinceLoan4 = (n0.getblockcount() - loanHeight4) + 1
        interestAmountLoan0 = Decimal(blocksSinceLoan0) * Decimal('0.00000001') # 1 sat per block
        interestAmountLoan4 = Decimal(blocksSinceLoan4) * Decimal(interestBlockLoan4)
        returnedInterest40 = Decimal(vault0["interestAmounts"][0].split('@')[0])
        # ceiling
        interestAmountLoanCeil4 = interestAmountLoan4.quantize(Decimal('1E-8'), rounding=ROUND_UP)

        totalInterestLoan40 = interestAmountLoan0 + interestAmountLoanCeil4
        assert_equal(returnedInterest40, totalInterestLoan40)

        # Payback

        paybackAmount0 = loanAmount0 + interestAmountLoan0 + loanAmount4 + totalInterestLoan40
        n0.paybackloan({
                        'vaultId': vaultIds[0],
                        'from': account0,
                        'amounts': [str(paybackAmount0) +"@" + symbolDOGE]})
        n0.generate(1)
        vault0 = n0.getvault(vaultIds[0])

        loanAmount5 = Decimal('0.657')
        n0.takeloan({
                    'vaultId': vaultIds[0],
                    'amounts': str(loanAmount5) + "@" + symbolDOGE}) # 0.657*(( 0.01 + 0.01 ) / ( 365 * 144 )) = 25 Sat/block
        n0.generate(1)
        self.sync_blocks()
        loanHeight5 = n0.getblockcount()

        interestBlockLoan5 = loanAmount5 * ((loanInterest + tokenInterest) / (blocksPerDay * 365))

        n0.generate(5)
        vault0 = n0.getvault(vaultIds[0])

        blocksSinceLoan5 = (n0.getblockcount() - loanHeight5) + 1 # +1 as interest shown is calculated with next height
        interestAmountLoan5 = Decimal(blocksSinceLoan5) * Decimal(interestBlockLoan5)
        returnedInterest5 = Decimal(vault0["interestAmounts"][0].split('@')[0])
        # ceiling
        interestAmountLoanCeil5 = interestAmountLoan5.quantize(Decimal('1E-8'), rounding=ROUND_UP)
        assert_equal(returnedInterest5, interestAmountLoanCeil5)





if __name__ == '__main__':
    LowInterestTest().main()
