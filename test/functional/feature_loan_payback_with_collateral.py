#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - Payback with collateral."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import ROUND_UP, ROUND_DOWN, Decimal
import calendar
import time

BLOCKS_PER_YEAR = Decimal(1051200)

class LoanPaybackWithCollateralTest (DefiTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            '-txnotokens=0',
            '-amkheight=1',
            '-bayfrontheight=1',
            '-eunosheight=1',
            '-fortcanningheight=1',
            '-fortcanningmuseumheight=1',
            '-fortcanninghillheight=1',
            '-fortcanningroadheight=1',
            '-fortcanningspringheight=1',
            '-fortcanningcrunchheight=1',
            '-fortcanninggreatworldheight=1',
            '-fortcanningepilogueheight=1',
            '-regtest-skip-loan-collateral-validation=1',
            '-simulatemainnet=1'
            ]]

    def rollback_to(self, block):
        self.log.info("rollback to: %d", block)
        node = self.nodes[0]
        current_height = node.getblockcount()
        if current_height == block:
            return
        blockhash = node.getblockhash(block + 1)
        node.invalidateblock(blockhash)
        node.clearmempool()
        assert_equal(block, node.getblockcount())

    def createOracles(self):
        self.oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "DUSD"},
                        {"currency": "USD", "token": "TSLA"}]
        self.oracle_id1 = self.nodes[0].appointoracle(self.oracle_address1, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle_prices = [{"currency": "USD", "tokenAmount": "1@TSLA"},
                        {"currency": "USD", "tokenAmount": "1@DUSD"},
                        {"currency": "USD", "tokenAmount": "10@DFI"}]

        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle_prices)

        self.oracle_address2 = self.nodes[0].getnewaddress("", "legacy")
        self.oracle_id2 = self.nodes[0].appointoracle(self.oracle_address2, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, oracle_prices)
        self.nodes[0].generate(120)

    def setup(self):
        self.nodes[0].generate(120)

        self.createOracles()

        self.mn_address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.account0 = self.nodes[0].getnewaddress()

        self.collateralAmount = 2000
        self.loanAmount = 1000

        self.symbolDFI = "DFI"
        self.symbolTSLA = "TSLA"
        self.symbolDUSD = "DUSD"

        self.nodes[0].setloantoken({
            'symbol': "DUSD",
            'name': "DUSD",
            'fixedIntervalPriceId': "DUSD/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(120)

        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]

        # Mint DUSD
        self.nodes[0].minttokens("100000@DUSD")
        self.nodes[0].generate(1)

        # Create DFI tokens
        self.nodes[0].utxostoaccount({self.mn_address: "100000@" + self.symbolDFI})
        self.nodes[0].generate(1)

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDFI,
            "tokenB": self.symbolDUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.mn_address
        })
        self.nodes[0].generate(1)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity({
            self.mn_address: [
                '10000@' + self.symbolDFI,
                '8000@' + self.symbolDUSD]
            }, self.mn_address)
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': "TSLA",
            'name': "TSLA",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 1, 'LOAN001')
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': self.symbolDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"
                                    })
        self.nodes[0].setcollateraltoken({
                                    'token': self.symbolDUSD,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DUSD/USD"
                                    })
        self.nodes[0].generate(120)


        self.nodes[0].accounttoaccount(self.mn_address, {self.account0: str(self.collateralAmount) + "@" + self.symbolDUSD})
        self.nodes[0].accounttoaccount(self.mn_address, {self.account0: str(self.collateralAmount) + "@" + self.symbolDFI})
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.idDUSD + '/loan_payback_collateral':'true'}})
        self.nodes[0].generate(1)

        vault_address = self.nodes[0].getnewaddress()

        self.vaultId = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

    def test_guards(self):
        height = self.nodes[0].getblockcount()

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.idDUSD + '/loan_payback_collateral':'false'}})
        self.nodes[0].generate(1)
        assert_raises_rpc_error(-32600, "Payback of DUSD loan with collateral is not currently active", self.nodes[0].paybackwithcollateral, self.vaultId)
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + self.idDUSD + '/loan_payback_collateral':'true'}})
        self.nodes[0].generate(1)

        assert_raises_rpc_error(-32600, "Vault has no collaterals", self.nodes[0].paybackwithcollateral, self.vaultId)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(self.vaultId, self.account0, str(self.collateralAmount) + "@" + self.symbolDFI)
        self.nodes[0].generate(1)

        assert_raises_rpc_error(-32600, "Vault does not have any DUSD collaterals", self.nodes[0].paybackwithcollateral, self.vaultId)

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(self.collateralAmount) + "@" + self.symbolDUSD)
        self.nodes[0].generate(1)

        assert_raises_rpc_error(-32600, "Vault has no loans", self.nodes[0].paybackwithcollateral, self.vaultId)

        # take TSLA loan
        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts": "1@" + self.symbolTSLA })
        self.nodes[0].generate(1)

        assert_raises_rpc_error(-32600, "Vault does not have any DUSD loans", self.nodes[0].paybackwithcollateral, self.vaultId)

        self.rollback_to(height)

    def test_guard_against_liquidation(self):
        height = self.nodes[0].getblockcount()

        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_collateral_factor': '1.49'}})
        self.nodes[0].generate(1)

        # Check results
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/token/1/loan_collateral_factor'], '1.49')

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(self.vaultId, self.account0, "151@" + self.symbolDFI)
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId, self.account0, "1000@" + self.symbolDUSD)
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts": "1900@" + self.symbolTSLA })
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts": "100@" + self.symbolDUSD })
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'50000000'}})
        self.nodes[0].generate(2) # accrue enough interest to drop below collateralization ratio

        assert_raises_rpc_error(-32600, "Vault does not have enough collateralization ratio defined by loan scheme - 143 < 150", self.nodes[0].paybackwithcollateral, self.vaultId)

        self.rollback_to(height)

    def test_collaterals_greater_than_loans(self, payback_with_collateral):
        height = self.nodes[0].getblockcount()

        collateralDUSDAmount = 2000
        loanDUSDAmount = 1000

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(self.collateralAmount) + "@" + self.symbolDFI)
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(collateralDUSDAmount) + "@" + self.symbolDUSD)
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts": str(loanDUSDAmount) + "@" + self.symbolDUSD })
        self.nodes[0].generate(1)

        mintedAmountBefore = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]

        vaultBefore = self.nodes[0].getvault(self.vaultId)
        [collateralAmountBefore, _] = vaultBefore["collateralAmounts"][1].split("@")
        [loanAmountBefore, _] = vaultBefore["loanAmounts"][0].split("@")

        payback_with_collateral()
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(self.vaultId)
        [collateralAmount, _] = vault["collateralAmounts"][1].split("@")

        assert(not any("DUSD" in loan for loan in vault["loanAmounts"])) # Payback all DUSD loans
        assert(not any("DUSD" in interest for interest in vault["interestAmounts"])) # Payback all DUSD interests
        assert_equal(Decimal(collateralAmount), Decimal(collateralAmountBefore) - Decimal(loanAmountBefore))

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(Decimal(storedInterest["interestPerBlock"]), Decimal(0))
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        mintedAmount = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]
        assert_equal(mintedAmountBefore, mintedAmount + Decimal(loanAmountBefore))

        self.rollback_to(height)

    def test_loans_greater_than_collaterals(self, payback_with_collateral):
        height = self.nodes[0].getblockcount()

        collateralDUSDAmount = 1000
        loanDUSDAmount = 1100

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(self.collateralAmount) + "@" + self.symbolDFI)
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(collateralDUSDAmount) + "@" + self.symbolDUSD)
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts":  str(loanDUSDAmount) + "@" + self.symbolDUSD })
        self.nodes[0].generate(1)

        mintedAmountBefore = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(storedInterest["interestPerBlock"], "0.000010464231354642313546")
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        vaultBefore = self.nodes[0].getvault(self.vaultId)
        [loanAmountBefore, _] = vaultBefore["loanAmounts"][0].split("@")

        payback_with_collateral()
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(self.vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")
        [loanAmount, _] = vault["loanAmounts"][0].split("@")
        assert(not any("DUSD" in collateral for collateral in vault["collateralAmounts"])) # Used all DUSD collateral
        assert_equal(Decimal(loanAmount), Decimal(loanAmountBefore) - Decimal(collateralDUSDAmount) + Decimal(interestAmount))

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(storedInterest["interestPerBlock"], "0.000000951293859113394216")
        assert_equal(Decimal(interestAmount), Decimal(storedInterest["interestPerBlock"]).quantize(Decimal('1E-8'), rounding=ROUND_UP))
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        mintedAmount = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]
        assert_equal(mintedAmountBefore, mintedAmount + collateralDUSDAmount)

        burnInfo = self.nodes[0].getburninfo()
        assert_equal(burnInfo["paybackburn"][0], vaultBefore["interestAmounts"][0])

        self.rollback_to(height)

    def test_loans_equal_to_collaterals(self, payback_with_collateral):
        height = self.nodes[0].getblockcount()

        expected_IPB = 0.00000952
        loanDUSDAmount = 1000
        collateralDUSDAmount = loanDUSDAmount + expected_IPB

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(self.collateralAmount) + "@" + self.symbolDFI)
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(collateralDUSDAmount) + "@" + self.symbolDUSD) # Deposit enough to match amount of loans + interest after one block
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts":  str(loanDUSDAmount) + "@" + self.symbolDUSD })
        self.nodes[0].generate(1)

        mintedAmountBefore = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(storedInterest["interestPerBlock"], "0.000009512937595129375951")
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        vaultBefore = self.nodes[0].getvault(self.vaultId)

        payback_with_collateral()
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(self.vaultId)
        assert(not any("DUSD" in loan for loan in vault["loanAmounts"])) # Payback all DUSD loans
        assert(not any("DUSD" in interest for interest in vault["interestAmounts"])) # Payback all DUSD interests
        assert(not any("DUSD" in collateral for collateral in vault["collateralAmounts"])) # Used all DUSD collateral

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(Decimal(storedInterest["interestPerBlock"]), Decimal(0))
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        mintedAmount = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]
        assert_equal(mintedAmountBefore, Decimal(mintedAmount + Decimal(collateralDUSDAmount)).quantize(Decimal('1E-8'), rounding=ROUND_DOWN))

        burnInfo = self.nodes[0].getburninfo()
        assert_equal(burnInfo["paybackburn"][0], vaultBefore["interestAmounts"][0])

        self.rollback_to(height)

    def test_interest_greater_than_collaterals(self, payback_with_collateral):
        height = self.nodes[0].getblockcount()

        collateralDUSDAmount = 0.000001
        loanDUSDAmount = 1000

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(self.collateralAmount) + "@" + self.symbolDFI)
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(collateralDUSDAmount) + "@" + self.symbolDUSD)
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts":  str(loanDUSDAmount) + "@" + self.symbolDUSD })
        self.nodes[0].generate(1)

        mintedAmountBefore = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(storedInterest["interestPerBlock"], "0.000009512937595129375951")
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        vaultBefore = self.nodes[0].getvault(self.vaultId)

        payback_with_collateral()
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(self.vaultId)
        [interestAmount, _] = vault["interestAmounts"][0].split("@")
        assert(not any("DUSD" in collateral for collateral in vault["collateralAmounts"])) # Used all DUSD collateral

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(storedInterest["interestPerBlock"], "0.000009512937595129375951")
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal("0.000008512937595129375951"))

        assert_equal(Decimal(interestAmount), Decimal(Decimal(storedInterest["interestPerBlock"]) * 2 - Decimal(collateralDUSDAmount)).quantize(Decimal('1E-8'), rounding=ROUND_UP))

        mintedAmount = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]
        assert_equal(mintedAmountBefore, mintedAmount + Decimal(collateralDUSDAmount))

        burnInfo = self.nodes[0].getburninfo()
        assert_equal(burnInfo["paybackburn"][0], vaultBefore["collateralAmounts"][1])

        self.rollback_to(height)

    def test_interest_equal_to_collaterals(self, payback_with_collateral):
        height = self.nodes[0].getblockcount()

        expected_IPB = 0.00000952
        loanDUSDAmount = 1000
        collateralDUSDAmount = expected_IPB

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(self.collateralAmount) + "@" + self.symbolDFI)
        self.nodes[0].generate(1)


        self.nodes[0].deposittovault(self.vaultId, self.account0, str(collateralDUSDAmount) + "@" + self.symbolDUSD) # Deposit enough to match amount of interest after one block
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts":  str(loanDUSDAmount) + "@" + self.symbolDUSD })
        self.nodes[0].generate(1)

        mintedAmountBefore = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(storedInterest["interestPerBlock"], "0.000009512937595129375951")
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        vaultBefore = self.nodes[0].getvault(self.vaultId)

        payback_with_collateral()
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(self.vaultId)
        assert(not any("DUSD" in collateral for collateral in vault["collateralAmounts"])) # Used all DUSD collateral
        assert_equal(vault["interestAmounts"], vaultBefore["interestAmounts"])
        assert_equal(vault["loanAmounts"], vaultBefore["loanAmounts"])
        assert_equal(vault["collateralValue"], float(vaultBefore["collateralValue"]) - expected_IPB)

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(storedInterest["interestPerBlock"], "0.000009512937595129375951")
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        mintedAmount = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]
        assert_equal(mintedAmountBefore, Decimal(mintedAmount + Decimal(collateralDUSDAmount)).quantize(Decimal('1E-8'), rounding=ROUND_DOWN))

        burnInfo = self.nodes[0].getburninfo()
        assert_equal(burnInfo["paybackburn"][0], vaultBefore["interestAmounts"][0])

        self.rollback_to(height)

    def test_negative_interest_collaterals_greater_than_loans(self, payback_with_collateral):
        height = self.nodes[0].getblockcount()

        collateralDUSDAmount = 1000
        loanDUSDAmount = 1000

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(self.collateralAmount) + "@" + self.symbolDFI)
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(collateralDUSDAmount) + "@" + self.symbolDUSD)
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-500'}})
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts":  str(loanDUSDAmount) + "@" + self.symbolDUSD })
        self.nodes[0].generate(1)

        # accrue negative interest
        self.nodes[0].generate(5)

        mintedAmountBefore = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]

        vault = self.nodes[0].getvault(self.vaultId)
        [DUSDInterestAmount, _] = vault["interestAmounts"][0].split("@")
        [DUSDloanAmount, _] = vault["loanAmounts"][0].split("@")
        assert(Decimal(DUSDInterestAmount) < 0)

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(storedInterest["interestPerBlock"], "-0.004746955859969558599695")
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        payback_with_collateral()
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(self.vaultId)
        # collateral amount should be equal to the opposite of DUSDInterestAmount
        [DUSDCollateralAmount, _] = vault["collateralAmounts"][1].split("@")
        assert_equal(Decimal(DUSDCollateralAmount), Decimal(DUSDInterestAmount) * -1)

        assert(not any("DUSD" in loan for loan in vault["loanAmounts"])) # Paid back all DUSD loans
        assert(not any("DUSD" in interest for interest in vault["interestAmounts"])) # Paid back all DUSD interests

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(Decimal(storedInterest["interestPerBlock"]), Decimal("0"))
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal("0"))

        mintedAmount = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]
        assert_equal(mintedAmountBefore, mintedAmount + Decimal(DUSDloanAmount))

        burnInfo = self.nodes[0].getburninfo()
        assert_equal(burnInfo["paybackburn"], []) # no burn on negative interest

        self.rollback_to(height)

    def test_negative_interest_loans_greater_than_collaterals(self, payback_with_collateral):
        height = self.nodes[0].getblockcount()

        collateralDUSDAmount = 1000
        loanDUSDAmount = 1100

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(self.collateralAmount) + "@" + self.symbolDFI)
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vaultId, self.account0, str(collateralDUSDAmount) + "@" + self.symbolDUSD)
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-500'}})
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({ "vaultId": self.vaultId, "amounts":  str(loanDUSDAmount) + "@" + self.symbolDUSD })
        self.nodes[0].generate(1)

        # accrue negative interest
        self.nodes[0].generate(5)

        mintedAmountBefore = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]

        vaultBefore = self.nodes[0].getvault(self.vaultId)
        [DUSDLoanAmountBefore, _] = vaultBefore["loanAmounts"][0].split("@")
        [DUSDInterestAmount, _] = vaultBefore["interestAmounts"][0].split("@")
        assert(Decimal(DUSDInterestAmount) < 0)

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(storedInterest["interestPerBlock"], "-0.005221651445966514459665")
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal(0))

        payback_with_collateral()
        self.nodes[0].generate(1)

        vault = self.nodes[0].getvault(self.vaultId)
        [DUSDLoanAmount, _] = vault["loanAmounts"][0].split("@")
        [DUSDInterestAmount, _] = vault["interestAmounts"][0].split("@")
        assert_equal(Decimal(DUSDLoanAmount), Decimal(DUSDLoanAmountBefore) - Decimal(collateralDUSDAmount) + Decimal(DUSDInterestAmount))
        assert(not any("DUSD" in collateral for collateral in vault["collateralAmounts"])) # Used all DUSD collateral

        storedInterest = self.nodes[0].getstoredinterest(self.vaultId, self.idDUSD)
        assert_equal(Decimal(storedInterest["interestPerBlock"]), Decimal("-0.000474546864344558599695"))
        assert_equal(Decimal(storedInterest["interestToHeight"]), Decimal("0"))

        mintedAmount = self.nodes[0].gettoken(self.symbolDUSD)[self.idDUSD]["minted"]
        assert_equal(mintedAmountBefore, mintedAmount + collateralDUSDAmount)

        burnInfo = self.nodes[0].getburninfo()
        assert_equal(burnInfo["paybackburn"], []) # no burn on negative interest

        self.rollback_to(height)

    def run_test(self):
        self.setup()

        self.test_guards()

        self.test_guard_against_liquidation()

        payback_fns = [
            lambda: self.nodes[0].paybackwithcollateral(self.vaultId),
            lambda: self.nodes[0].paybackloan({
                        'vaultId': self.vaultId,
                        'from': self.account0,
                        'amounts': "9999999999.99999999@" + self.symbolDUSD})
        ]

        for payback_fn in payback_fns:

            self.test_collaterals_greater_than_loans(payback_fn)

            self.test_loans_greater_than_collaterals(payback_fn)

            self.test_loans_equal_to_collaterals(payback_fn)

            self.test_interest_greater_than_collaterals(payback_fn)

            self.test_interest_equal_to_collaterals(payback_fn)

            self.test_negative_interest_collaterals_greater_than_loans(payback_fn)

            self.test_negative_interest_loans_greater_than_collaterals(payback_fn)

if __name__ == '__main__':
    LoanPaybackWithCollateralTest().main()
