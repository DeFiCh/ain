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

VOTING_PERIOD = 70


class CFPFeeDistributionTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-rpc-governance-accept-neutral', '-amkheight=50', '-bayfrontheight=51',
             '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-grandcentralheight=101'],
            ['-dummypos=0', '-txnotokens=0', '-rpc-governance-accept-neutral', '-amkheight=50', '-bayfrontheight=51',
             '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-grandcentralheight=101'],
            ['-dummypos=0', '-txnotokens=0', '-rpc-governance-accept-neutral', '-amkheight=50', '-bayfrontheight=51',
             '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-grandcentralheight=101'],
            ['-dummypos=0', '-txnotokens=0', '-rpc-governance-accept-neutral', '-amkheight=50', '-bayfrontheight=51',
             '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-grandcentralheight=101'],
        ]

    def test_cfp_fee_distribution(self, amount, expectedFee, burnPct, vote, cycles=2, changeFeeAndBurnPCT=False):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        # Create CFP
        propId = self.nodes[0].creategovcfp(
            {"title": title, "context": context, "amount": amount, "cycles": cycles, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address3, Decimal("1.0"))

        # Generate a block
        self.nodes[0].generate(1)
        self.sync_blocks()
        proposalCreationHeight = self.nodes[0].getblockcount()

        self.nodes[2].generate(1)
        self.sync_blocks()

        # Check fee burn
        # Half should be burned, the rest distributed among voting masternodes
        assert_equal(self.nodes[0].getburninfo()['feeburn'], Decimal(expectedFee * burnPct / 100))

        # increase the fee in the middle of CFP and check that refund to MNs didn't change
        if changeFeeAndBurnPCT:
            self.nodes[0].setgov({"ATTRIBUTES": {'v0/gov/proposals/cfp_fee': '0.05'}})
            self.nodes[0].setgov({"ATTRIBUTES": {'v0/gov/proposals/fee_burn_pct': '40%'}})
            self.nodes[0].generate(1)

        expectedAmount = Decimal(expectedFee * (100 - burnPct) / 100 / 3).quantize(Decimal('1E-8'), rounding=ROUND_DOWN)

        # Calculate cycle
        cycleAlignment = proposalCreationHeight + (VOTING_PERIOD - proposalCreationHeight % VOTING_PERIOD)
        votingCycles = 0
        for cycle in range(cycles):
            result = self.nodes[0].listgovproposals()
            if result[0]['status'] == "Voting":
                votingCycles += 1
                # Vote on proposal
                self.nodes[0].votegov(propId, self.mn0, vote)
                self.nodes[1].votegov(propId, self.mn1, vote)
                self.nodes[2].votegov(propId, self.mn2, vote)
                self.sync_mempools()
                self.nodes[2].generate(1)
                self.sync_blocks()

                # Check total votes
                result = self.nodes[0].listgovproposalvotes(propId, "all")
                assert_equal(len(result), 3)

            cycleEnd = cycleAlignment + (cycle + 1) * VOTING_PERIOD
            # Move to cycle end height
            self.nodes[0].generate(cycleEnd - self.nodes[0].getblockcount())
            self.sync_blocks()

            mn0 = self.nodes[0].getmasternode(self.mn0)[self.mn0]
            account0 = self.nodes[0].getaccount(mn0['ownerAuthAddress'])
            assert_equal(account0[0], '{}@DFI'.format(expectedAmount * votingCycles))
            history = self.nodes[0].listaccounthistory(mn0['ownerAuthAddress'], {"txtype": "ProposalFeeRedistribution"})
            assert_equal(len(history), votingCycles)
            for i in range(votingCycles):
                assert_equal(history[i]['amounts'][0], '{}@DFI'.format(expectedAmount))

            mn1 = self.nodes[0].getmasternode(self.mn1)[self.mn1]
            account1 = self.nodes[0].getaccount(mn1['ownerAuthAddress'])
            assert_equal(account1[0], '{}@DFI'.format(expectedAmount * votingCycles))
            history = self.nodes[0].listaccounthistory(mn1['ownerAuthAddress'], {"txtype": "ProposalFeeRedistribution"})
            assert_equal(len(history), votingCycles)
            for i in range(votingCycles):
                assert_equal(history[i]['amounts'][0], '{}@DFI'.format(expectedAmount))

            # Fee should be redistributed to reward address
            mn2 = self.nodes[0].getmasternode(self.mn2)[self.mn2]
            account2 = self.nodes[0].getaccount(mn2['ownerAuthAddress'])
            assert_equal(account2[0], '{}@DFI'.format(expectedAmount * votingCycles))
            history = self.nodes[0].listaccounthistory(mn2['ownerAuthAddress'], {"txtype": "ProposalFeeRedistribution"})
            assert_equal(len(history), votingCycles)
            for i in range(votingCycles):
                assert_equal(history[i]['amounts'][0], '{}@DFI'.format(expectedAmount))

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
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/gov': 'true'}})
        self.nodes[0].generate(1)

        # activate fee redistribution
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/gov/proposals/fee_redistribution': 'true'}})
        self.nodes[0].generate(1)

        self.sync_blocks()

    def run_test(self):

        self.setup()

        self.test_cfp_fee_distribution(amount=50, expectedFee=10, burnPct=50, vote="yes")
        self.test_cfp_fee_distribution(amount=100, expectedFee=10, burnPct=50, vote="yes")
        self.test_cfp_fee_distribution(amount=1000, expectedFee=10, burnPct=50, vote="yes", changeFeeAndBurnPCT=True)
        self.test_cfp_fee_distribution(amount=1000, expectedFee=10, burnPct=50, vote="no", changeFeeAndBurnPCT=True)

        self.nodes[0].setgov({"ATTRIBUTES": {'v0/gov/proposals/fee_burn_pct': '30%'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.test_cfp_fee_distribution(amount=1000, expectedFee=10, burnPct=30, vote="neutral")

        self.nodes[0].setgov({"ATTRIBUTES": {'v0/gov/proposals/cfp_fee': '2%'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.test_cfp_fee_distribution(amount=1000, expectedFee=20, burnPct=30, vote="yes", cycles=1)
        self.test_cfp_fee_distribution(amount=1000, expectedFee=20, burnPct=30, vote="yes", cycles=3,
                                       changeFeeAndBurnPCT=True)


if __name__ == '__main__':
    CFPFeeDistributionTest().main()
