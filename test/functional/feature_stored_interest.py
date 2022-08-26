#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test stored interest."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from decimal import Decimal
import time

class StoredInterestTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningcrunchheight=1', '-greatworldheight=1', '-jellyfish_regtest=1']]

    def run_test(self):
        # Create tokens for tests
        self.setup_test_tokens()

        # Setup pools
        self.setup_test_pools()

        # Setup Oracles
        self.setup_test_oracles()

        # Test token interest changes
        self.test_token_interest_change()

        # Test scheme changes
        self.test_scheme_change()

        # Test take loan negative IPB and positive ITH
        self.test_take_loan_neg_ipb_pos_ith()

        # Test take loan positive IPB and negative ITH
        self.test_take_loan_pos_ipb_neg_ith()

        # Test payback loan
        self.test_payback_loan()

        # Test negative interest does not apply to new loan if previous loan fully negated
        self.test_takeloan_close_loan()

        # Test payback on fully negated loan does not remove funds from users account
        self.test_payback_close_loan()

        # Test payback on half negated loan, payback of full amount should only have half subbed
        self.test_payback_partial_close_loan()

        # Test minimum negative interest take loan
        self.check_minimum_interest_takeloan()

        # Test minimum negative interest payback
        self.check_minimum_interest_payback()

    def setup_test_tokens(self):
        # Generate chain
        self.nodes[0].generate(120)

        # Get MN address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Token symbols
        self.symbolDFI = "DFI"
        self.symbolDUSD = "DUSD"

        # Create loan token
        self.nodes[0].setloantoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            'fixedIntervalPriceId': f"{self.symbolDUSD}/USD",
            'mintable': True,
            'interest': 1
        })
        self.nodes[0].generate(1)

        # Store DUSD ID
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]

        # Mint DUSD
        self.nodes[0].minttokens("100000@DUSD")
        self.nodes[0].generate(1)

        # Create DFI tokens
        self.nodes[0].utxostoaccount({self.address: "100000@" + self.symbolDFI})
        self.nodes[0].generate(1)

    def setup_test_pools(self):

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDFI,
            "tokenB": self.symbolDUSD,
            "commission": 0,
            "status": True,
            "ownerAddress": self.address
        })
        self.nodes[0].generate(1)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity({
            self.address: [
                '10000@' + self.symbolDFI,
                '10000@' + self.symbolDUSD]
            }, self.address)
        self.nodes[0].generate(1)

    def setup_test_oracles(self):

        # Create Oracle address
        oracle_address = self.nodes[0].getnewaddress("", "legacy")

        # Define price feeds
        price_feed = [
            {"currency": "USD", "token": "DFI"}
        ]

        # Appoint Oracle
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
                                    'token': self.symbolDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"
                                    })
        self.nodes[0].generate(1)

        # Create loan scheme
        self.nodes[0].createloanscheme(100, 1, 'LOAN001')
        self.nodes[0].generate(1)

        # Create loan scheme
        self.nodes[0].createloanscheme(150, 1, 'LOAN002')
        self.nodes[0].generate(1)

    def test_token_interest_change(self):

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"10@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"10@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(10)

        # Change token interest to create positive interestToHeight value
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        # Check stored interest increased as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517503805175038')
        positive_stored_ipb = Decimal(stored_interest['interestPerBlock'])
        assert_equal(Decimal(stored_interest['interestToHeight']), positive_stored_ipb * 10)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Set negative interest to be inverse of positive interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(6)

        # Apply again to update stored interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(10)

        # Check interest is now set to be negative and that interest to height has reduced
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), positive_stored_ipb * 5)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 9)

        # Apply again to update stored interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(5)

        # Check interest to height is now negative
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        negative_stored_ipb = Decimal(stored_interest['interestPerBlock'])
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), negative_stored_ipb * 5)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 4)

        # Apply again to update stored interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(1)

        # Check interest to height has additional negative interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), negative_stored_ipb * 10)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Set positive token interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(6)

        # Apply again to update stored interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(10)

        # Check interest to height has additional negative interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), negative_stored_ipb * 5)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 9)

        # Apply again to update stored interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        # Check interest to height has additional negative interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), positive_stored_ipb * 5)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

    def test_scheme_change(self):

        # Reset token interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"10@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"10@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(10)

        # Change vault scheme to create positive interest to height
        self.nodes[0].updatevault(vault_id, {'loanSchemeId': 'LOAN002'})
        self.nodes[0].generate(1)

        # Check vault scheme has actually changed
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanSchemeId'], 'LOAN002')

        # Check stored interest increased as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517503805175038')
        positive_stored_ipb = Decimal(stored_interest['interestPerBlock'])
        assert_equal(Decimal(stored_interest['interestToHeight']), positive_stored_ipb * 10)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Set negative interest to be inverse of positive interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(6)

        # Apply scheme change to update stored interest
        self.nodes[0].updatevault(vault_id, {'loanSchemeId': 'LOAN001'})
        self.nodes[0].generate(10)

        # Check vault scheme has actually changed
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanSchemeId'], 'LOAN001')

        # Check interest is now set to be negative and that interest to height has reduced
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), positive_stored_ipb * 5)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 9)

        # Apply scheme change to update stored interest
        self.nodes[0].updatevault(vault_id, {'loanSchemeId': 'LOAN002'})
        self.nodes[0].generate(5)

        # Check vault scheme has actually changed
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanSchemeId'], 'LOAN002')

        # Check interest to height is now negative
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        negative_stored_ipb = Decimal(stored_interest['interestPerBlock'])
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), negative_stored_ipb * 5)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 4)

        # Apply scheme change to update stored interest
        self.nodes[0].updatevault(vault_id, {'loanSchemeId': 'LOAN001'})
        self.nodes[0].generate(1)

        # Check vault scheme has actually changed
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanSchemeId'], 'LOAN001')

        # Check interest to height has additional negative interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), negative_stored_ipb * 10)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Set positive token interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(6)

        # Apply scheme change to update stored interest
        self.nodes[0].updatevault(vault_id, {'loanSchemeId': 'LOAN002'})
        self.nodes[0].generate(10)

        # Check vault scheme has actually changed
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanSchemeId'], 'LOAN002')

        # Check interest to height has additional negative interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), negative_stored_ipb * 5)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 9)

        # Apply scheme change to update stored interest
        self.nodes[0].updatevault(vault_id, {'loanSchemeId': 'LOAN001'})
        self.nodes[0].generate(1)

        # Check vault scheme has actually changed
        vault = self.nodes[0].getvault(vault_id)
        assert_equal(vault['loanSchemeId'], 'LOAN001')

        # Check interest to height has additional negative interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517503805175038')
        assert_equal(Decimal(stored_interest['interestToHeight']), positive_stored_ipb * 5)
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

    def test_take_loan_neg_ipb_pos_ith(self):

        # Reset token interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"10@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"10@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(10)

        # Check interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)['interestPerBlock']
        assert_equal(stored_interest, '0.000000380517503805175038')

        # Take another loan to update stored interest
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check interest has updated as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517507610350076')
        assert_equal(stored_interest['interestToHeight'], '0.000003805175038051750380')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Set negative interest to be inverse of positive interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(6)

        # Check negative rate
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517507610350076')

        # Take another loan to update stored interest
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(6)

        # Check interest has reduced as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517511415525114')
        assert_equal(stored_interest['interestToHeight'], '0.000001902587500000000000') # Reduced 6 * IPB
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 5)

        # Check loan amount before taking new loan
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'1.00000002@{self.symbolDUSD}'])

        # Take another loan to update stored interest
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000040@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check ITH is wiped and IPB increased slightly
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517519025875190')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Check amount has increased for the new loan amount not negated
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'1.00000004@{self.symbolDUSD}'])

        # Revert and restore positive ITH
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        # Take another loan to update stored interest
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check ITH is wiped and IPB decreased slightly
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517370624048706')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Check amount has decreased for amount negated by interest
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'0.99999965@{self.symbolDUSD}'])

        # Revert and restore positive ITH
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount() - 1))
        self.nodes[0].clearmempool()

        # Set mega negative interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-1000000'}})
        self.nodes[0].generate(10)

        # Update token rate to negative
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(1)

        # Check ITH is more than loan amount
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517511415525114')
        assert_equal(stored_interest['interestToHeight'], '-1.902585654490125570776250')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Take new loan. Old loan amount should be paid off only.
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.50000000@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check loan amount only shows new loan
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'0.50000000@{self.symbolDUSD}'])

        # User's account has increased by the expected amount
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [f'1.50000002@{self.symbolDUSD}'])

        # Check stored interest is now nil
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000190258751902587519')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

    def test_payback_loan(self):

        # Reset token interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"10@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"10@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(10)

        # Check interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)['interestPerBlock']
        assert_equal(stored_interest, '0.000000380517503805175038')

        # Payback DUSD loan to update stored interest
        self.nodes[0].paybackloan({ "vaultId": vault_id, "from": self.address, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check interest has updated as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517503805175038')
        assert_equal(stored_interest['interestToHeight'], '0.000003795175038051750380') # Less 1 Sat payback
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Set negative interest to be inverse of positive interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(6)

        # Check negative rate
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517503805175038')

        # Payback DUSD loan to update stored interest
        self.nodes[0].paybackloan({ "vaultId": vault_id, "from": self.address, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(6)

        # Check interest has reduced as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517503805175038')
        assert_equal(stored_interest['interestToHeight'], '0.000001882587519025875190')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 5)

        # Payback DUSD loan to update stored interest
        self.nodes[0].paybackloan({ "vaultId": vault_id, "from": self.address, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(6)

        # Check interest has wiped as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517347792998477')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 5)

        # Check loan amount is reduced as expected by one negative interest block
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, ['0.99999959@DUSD'])

        # Apply token interest to update interest to height
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(1)

        # Check interest increased as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000380517347792998477')
        assert_equal(stored_interest['interestToHeight'], '-0.000002283104086757990862')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Payback loan in full
        self.nodes[0].paybackloan({ "vaultId": vault_id, "from": self.address, "amounts": self.nodes[0].getloantokens(vault_id)[0]})
        self.nodes[0].generate(6)

        # Check interest has wiped as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000000000000000000000')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 5)

        # Apply token interest to update interest to height
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(1)

        # Make sure that interest is still wiped and not updated
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000000000000000000000')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 6)

    def test_takeloan_close_loan(self):

        # Set mega negative interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-200000'}})
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"10@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"10@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(60)

        # Check interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)['interestPerBlock']
        assert_equal(stored_interest, '-0.000000000380515601217656')

        # Set resonable negative interest. Ends up at 0% in vault.
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-1'}})
        self.nodes[0].generate(1)

        # Check interest fully negates loan amount of 1 Sat
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000000000000000000000')
        assert_equal(stored_interest['interestToHeight'], '-0.000000022830936073059360')

        # Take loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check interest and interest to height are both zero
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000000000000000000000')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')

        # Check that new loan amount is in full and does not have previous negative interest applied
        assert_equal(self.nodes[0].getloantokens(vault_id), ['1.00000000@DUSD'])

    def test_payback_close_loan(self):

        # Set mega negative interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-200000'}})
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"10@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"10@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(60)

        # Check interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)['interestPerBlock']
        assert_equal(stored_interest, '-0.000000000380515601217656')

        # Set resonable negative interest. Ends up at 0% in vault.
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-1'}})
        self.nodes[0].generate(1)

        # Check interest fully negates loan amount of 1 Sat
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000000000000000000000')
        assert_equal(stored_interest['interestToHeight'], '-0.000000022830936073059360')

        # Payback loan
        self.nodes[0].paybackloan({ "vaultId": vault_id, "from": vault_address, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check interest and interest to height are both zero
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000000000000000000000')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')

        # Check that the DUSD balance has not been used to payback the loan
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [f'0.00000001@{self.symbolDUSD}'])

        # Check loan amount is nil
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [])

    def test_payback_partial_close_loan(self):

        # Set mega negative interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-100000'}})
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"10@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"10@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(27)

        # Check interest
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)['interestPerBlock']
        assert_equal(stored_interest, '-0.019025684931506849315068')

        # Set resonable negative interest. Ends up at 0% in vault.
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-1'}})
        self.nodes[0].generate(1)

        # Check interest fully negates loan amount of 1 Sat
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000000000000000000000')
        assert_equal(stored_interest['interestToHeight'], '-0.513693493150684931506836')

        # Payback loan
        self.nodes[0].paybackloan({ "vaultId": vault_id, "from": vault_address, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check interest and interest to height are both zero
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000000000000000000000')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')

        # Check that only part of the user's balaance was used to payback the loan
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [f'0.51369349@{self.symbolDUSD}'])

        # Check loan amount is nil
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [])

    def test_take_loan_pos_ipb_neg_ith(self):

        # Set negative interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"10@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"10@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"1@{self.symbolDUSD}"})
        self.nodes[0].generate(10)

        # Set interest to for positive IPB
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(20)

        # Check interest has updated as expected
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517503805175038')
        assert_equal(stored_interest['interestToHeight'], '-0.000003805175038051750380')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount() - 19)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Accrued interes should have wiped negative interest and left positive ITH
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517507610350076')
        assert_equal(stored_interest['interestToHeight'], '0.000003805175038051750380')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Account has increased by new loan amount
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [f'1.00000001@{self.symbolDUSD}'])

        # Check loan amount is for all loan amounts taken
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'1.00000001@{self.symbolDUSD}'])

        # Set negative interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-3'}})
        self.nodes[0].generate(20)

        # Set positive interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000400@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Accrued interes should have wiped negative interest and left positive ITH
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517872907153729')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Account has increased by new loan amount
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [f'1.00000401@{self.symbolDUSD}'])

        # Check loan amount is increased for the new loan not negated by interest
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'1.00000097@{self.symbolDUSD}'])

        # Revert and restore negative ITH
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        # Take DUSD loan smaller than negative interest
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000100@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Accrued interes should have wiped negative interest and left positive ITH
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380516731354642313')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Account has increased by new loan amount
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [f'1.00000101@{self.symbolDUSD}'])

        # Check loan amount is decreased for the excess negative interest
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'0.99999797@{self.symbolDUSD}'])

        # Revert and restore negative ITH
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        # Set mega negative interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-1000000'}})
        self.nodes[0].generate(10)

        # Update token interest to positive
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        # Check ITH is more than loan amount
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000380517507610350076')
        assert_equal(stored_interest['interestToHeight'], '-1.902588679604311263318108')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Take new loan. Old loan amount should be paid off only.
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.50000000@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check loan amount only shows new loan
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'0.50000000@{self.symbolDUSD}'])

        # User's account has increased by the expected amount
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [f'1.50000001@{self.symbolDUSD}'])

        # Check stored interest is now nil
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '0.000000190258751902587519')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

    def check_minimum_interest_takeloan(self):

        # Set negative interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-2'}})
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"1@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"1@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check IPB is sub satoshi
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000000000001902587519')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Take new Sat loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check IPB is doubled and ITH updated
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000000000003805175038')
        assert_equal(stored_interest['interestToHeight'], '-0.000000000000001902587519')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Check loan amount shows all loan amounts
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'0.00000002@{self.symbolDUSD}'])

        # User's account is the expected amount
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [f'0.00000002@{self.symbolDUSD}'])


    def check_minimum_interest_payback(self):

        # Set negative interest
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{self.idDUSD}/loan_minting_interest':'-2'}})
        self.nodes[0].generate(1)

        # Create vault
        vault_address = self.nodes[0].getnewaddress('', 'legacy')
        vault_id = self.nodes[0].createvault(vault_address, 'LOAN001')
        self.nodes[0].generate(1)

        # Fund vault address
        self.nodes[0].accounttoaccount(self.address, {vault_address: f"1@{self.symbolDFI}"})
        self.nodes[0].generate(1)

        # Deposit DUSD and DFI to vault
        self.nodes[0].deposittovault(vault_id, vault_address, f"1@{self.symbolDFI}")
        self.nodes[0].generate(1)

        # Take DUSD loan
        self.nodes[0].takeloan({ "vaultId": vault_id, "amounts": f"0.00000002@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check IPB is sub satoshi
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000000000003805175038')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Payback part of the DUSD loan
        self.nodes[0].paybackloan({ "vaultId": vault_id, "from": vault_address, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check IPB is doubled and ITH updated
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000000000001902587519')
        assert_equal(stored_interest['interestToHeight'], '-0.000000000000003805175038')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Check loan amount reduced
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [f'0.00000001@{self.symbolDUSD}'])

        # User's was used to payback the amount
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [f'0.00000001@{self.symbolDUSD}'])

        # Payback the rest of the DUSD loan
        self.nodes[0].paybackloan({ "vaultId": vault_id, "from": vault_address, "amounts": f"0.00000001@{self.symbolDUSD}"})
        self.nodes[0].generate(1)

        # Check IPB is doubled and ITH updated
        stored_interest = self.nodes[0].getstoredinterest(vault_id, self.symbolDUSD)
        assert_equal(stored_interest['interestPerBlock'], '-0.000000000000000000000000')
        assert_equal(stored_interest['interestToHeight'], '0.000000000000000000000000')
        assert_equal(stored_interest['height'], self.nodes[0].getblockcount())

        # Check loan amount now removed
        loan_tokens = self.nodes[0].getloantokens(vault_id)
        assert_equal(loan_tokens, [])

        # User's was used to payback the amount
        account = self.nodes[0].getaccount(vault_address)
        assert_equal(account, [])

if __name__ == '__main__':
    StoredInterestTest().main()
