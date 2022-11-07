#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test CFP fee distribution to voting masternodes"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
)

from decimal import ROUND_DOWN, Decimal

class CFPFeeDistributionTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-grandcentralheight=101'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-grandcentralheight=101'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-grandcentralheight=101'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-grandcentralheight=101'],
        ]

    def test_cfp_fee_distribution(self, amount, expectedFee, vote, cycles=2, increaseFee = False):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": amount, "cycles": cycles, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address3, Decimal("1.0"))

        # Generate a block
        self.nodes[0].generate(1)
        self.sync_blocks()
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Check fee burn
        # Half should be burned, the rest distributed among voting masternodes
        assert_equal(self.nodes[0].getburninfo()['feeburn'], Decimal(expectedFee / 2))

        # increase the fee in the middle of CFP and check that refund to MNs didn't change
        if (increaseFee) :
            self.nodes[0].setgov({"ATTRIBUTES":{'v0/governance/proposals/cfp_fee':'0.05'}})
            self.nodes[0].generate(1)

        # Vote on proposal
        self.nodes[0].votegov(propId, self.mn0, vote)
        self.nodes[0].generate(1)
        self.sync_blocks()
        self.nodes[1].votegov(propId, self.mn1, vote)
        self.nodes[1].generate(1)
        self.sync_blocks()
        self.nodes[2].votegov(propId, self.mn2, vote)
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Calculate cycle
        cycle1 = 103 + (103 % 70) + 70
        finalHeight = cycle1 + (cycle1 % 70) + 70

        # Check total votes
        result = self.nodes[0].listgovvotes(propId, "all")
        assert_equal(len(result), 3)

        # Move to final height
        self.nodes[0].generate(finalHeight - self.nodes[0].getblockcount())
        self.sync_blocks()

        expectedAmount = '{}@DFI'.format(Decimal(expectedFee / 2 / 3).quantize(Decimal('1E-8'), rounding=ROUND_DOWN))

        mn0 = self.nodes[0].getmasternode(self.mn0)[self.mn0]
        account0 = self.nodes[0].getaccount(mn0['ownerAuthAddress'])
        assert_equal(account0[0], expectedAmount)
        history = self.nodes[0].listaccounthistory(mn0['ownerAuthAddress'], {"txtype": "ProposalFeeRedistribution"})
        assert_equal(account0[0], history[0]['amounts'][0])

        mn1 = self.nodes[0].getmasternode(self.mn1)[self.mn1]
        account1 = self.nodes[0].getaccount(mn1['ownerAuthAddress'])
        assert_equal(account1[0], expectedAmount)
        history = self.nodes[0].listaccounthistory(mn1['ownerAuthAddress'], {"txtype": "ProposalFeeRedistribution"})
        assert_equal(account0[0], history[0]['amounts'][0])

        # Fee should be redistributed to reward address
        mn2 = self.nodes[0].getmasternode(self.mn2)[self.mn2]
        account2 = self.nodes[0].getaccount(mn2['ownerAuthAddress'])
        assert_equal(account2[0], expectedAmount)
        history = self.nodes[0].listaccounthistory(mn2['ownerAuthAddress'], {"txtype": "ProposalFeeRedistribution"})
        assert_equal(account0[0], history[0]['amounts'][0])

        # mn3 did not vote on proposal
        mn3 = self.nodes[0].getmasternode(self.mn3)[self.mn3]
        account3 = self.nodes[0].getaccount(mn3['ownerAuthAddress'])
        assert_equal(account3, [])
        history = self.nodes[0].listaccounthistory(mn3['ownerAuthAddress'], {"txtype": "ProposalFeeRedistribution"})
        assert_equal(history, [])

        self.rollback_to(height, nodes=[0, 1, 2, 3])

    def setup(self):
        # Get MN addresses
        self.address1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        self.address2 = self.nodes[2].get_genesis_keys().ownerAuthAddress
        self.address3 = self.nodes[3].get_genesis_keys().ownerAuthAddress

        # Get MN IDs
        self.mn0 = self.nodes[0].getmininginfo()['masternodes'][0]['id']
        self.mn1 = self.nodes[1].getmininginfo()['masternodes'][0]['id']
        self.mn2 = self.nodes[2].getmininginfo()['masternodes'][0]['id']
        self.mn3 = self.nodes[3].getmininginfo()['masternodes'][0]['id']

        # Generate chain
        self.nodes[0].generate(100)
        self.sync_blocks()

        # Move to fork block
        self.nodes[1].generate(1)
        self.sync_blocks()

        # grand central
        assert_equal(self.nodes[0].getblockcount(), 101)

        # activate on-chain governance
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/feature/governance_enabled':'true'}})
        self.nodes[0].generate(1)

        # activate fee redistribution
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/governance/proposals/fee_redistribution':'true'}})
        self.nodes[0].generate(1)

        self.sync_blocks()

    def run_test(self):

        self.setup()

        self.test_cfp_fee_distribution(amount=50, expectedFee=10, vote="yes")
        self.test_cfp_fee_distribution(amount=100, expectedFee=10, vote="yes")
        self.test_cfp_fee_distribution(amount=1000, expectedFee=10, vote="yes")
        self.test_cfp_fee_distribution(amount=1000, expectedFee=10, vote="no")
        self.test_cfp_fee_distribution(amount=1000, expectedFee=10, vote="neutral", increaseFee=True)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/governance/proposals/cfp_fee':'2%'}})
        self.nodes[0].generate(1)

        self.test_cfp_fee_distribution(amount=1000, expectedFee=20, vote="yes", cycles=1)
        self.test_cfp_fee_distribution(amount=1000, expectedFee=20, vote="yes", cycles=3)

if __name__ == '__main__':
    CFPFeeDistributionTest().main ()
