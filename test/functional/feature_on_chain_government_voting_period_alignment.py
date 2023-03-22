#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test on chain government behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
)

from decimal import Decimal


class OnChainGovernanceTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80',
             '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-subsidytest=1', '-txindex=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80',
             '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80',
             '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80',
             '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-subsidytest=1'],
        ]

    def run_test(self):
        # Get MN addresses
        address1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        address2 = self.nodes[2].get_genesis_keys().ownerAuthAddress
        address3 = self.nodes[3].get_genesis_keys().ownerAuthAddress

        # Get MN IDs
        mn0 = self.nodes[0].getmininginfo()['masternodes'][0]['id']
        mn1 = self.nodes[1].getmininginfo()['masternodes'][0]['id']
        mn2 = self.nodes[2].getmininginfo()['masternodes'][0]['id']

        # Generate chain
        self.nodes[0].generate(100)
        self.sync_blocks()

        # Move to fork block
        self.nodes[1].generate(1)
        self.sync_blocks()

        # Fund addresses
        self.nodes[0].sendtoaddress(address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(address2, Decimal("11.0"))
        self.nodes[0].sendtoaddress(address3, Decimal("1.0"))

        # grand central
        assert_equal(self.nodes[0].getblockcount(), 101)

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"

        # activate on-chain governance
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/gov': 'true',
                                             'v0/gov/proposals/voting_period': '40'}}, )
        self.nodes[0].generate(1)
        self.sync_blocks()
        # voting period
        votingPeriod = 40

        # Create CFP
        tx = self.nodes[0].creategovcfp(
            {"title": "Single cycle CFP (70)", "context": context, "amount": 100, "cycles": 1,
             "payoutAddress": address})

        # Generate a block
        self.nodes[0].generate(1)
        self.sync_blocks()
        creationHeight = self.nodes[0].getblockcount()

        # Create CFP with 2 cycles
        tx1 = self.nodes[2].creategovcfp(
            {"title": "Multi cycle CFP (70)", "context": context, "amount": 100, "cycles": 2,
             "payoutAddress": address2})

        self.nodes[2].generate(1)
        self.sync_blocks()

        # Vote on proposal
        self.nodes[0].votegov(tx, mn0, "yes")
        self.nodes[0].votegov(tx1, mn0, "yes")
        self.nodes[0].generate(1)
        self.nodes[1].votegov(tx, mn1, "no")
        self.nodes[1].votegov(tx1, mn1, "no")
        self.nodes[1].generate(1)
        self.nodes[2].votegov(tx, mn2, "yes")
        self.nodes[2].votegov(tx1, mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Calculate cycle
        cycleAlignemnt = creationHeight + (votingPeriod - creationHeight % votingPeriod)
        proposalEndHeight = cycleAlignemnt + votingPeriod
        multiProposalEndHeight = proposalEndHeight + votingPeriod

        # Check proposal and votes
        result = self.nodes[0].getgovproposal(tx)
        assert_equal(result["creationHeight"], creationHeight)
        assert_equal(result["context"], context)
        assert_equal(result["contextHash"], "")
        assert_equal(result["type"], "CommunityFundProposal")
        assert_equal(result["amount"], Decimal("100"))
        assert_equal(result["currentCycle"], 1)
        assert_equal(result["cycleEndHeight"], proposalEndHeight)
        assert_equal(result["proposalEndHeight"], proposalEndHeight)
        assert_equal(result["votingPeriod"], votingPeriod)
        assert_equal(result["quorum"], "1.00%")
        assert_equal(result["approvalThreshold"], "50.00%")
        assert_equal(result["fee"], Decimal("10"))

        result = self.nodes[0].getgovproposal(tx1)
        assert_equal(result["creationHeight"], creationHeight + 1)
        assert_equal(result["context"], context)
        assert_equal(result["contextHash"], "")
        assert_equal(result["type"], "CommunityFundProposal")
        assert_equal(result["amount"], Decimal("100"))
        assert_equal(result["currentCycle"], 1)
        assert_equal(result["cycleEndHeight"], proposalEndHeight)
        assert_equal(result["proposalEndHeight"], multiProposalEndHeight)
        assert_equal(result["votingPeriod"], votingPeriod)
        assert_equal(result["quorum"], "1.00%")
        assert_equal(result["approvalThreshold"], "50.00%")
        assert_equal(result["fee"], Decimal("10"))

        # Change voting period at height - 1 when voting period ends
        self.nodes[0].setgovheight({"ATTRIBUTES": {'v0/gov/proposals/voting_period': '130'}}, cycleAlignemnt - 1)
        self.nodes[0].generate(1)
        self.sync_blocks()

        votingPeriod1 = 130

        # Move to voting period end
        self.nodes[0].generate(cycleAlignemnt - self.nodes[0].getblockcount())
        self.sync_blocks()

        # Create CFP in new voting period
        tx2 = self.nodes[0].creategovcfp(
            {"title": "Single cycle CFP (200)", "context": context, "amount": 100, "cycles": 1,
             "payoutAddress": address})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Calculate new cycle
        creationHeight = self.nodes[0].getblockcount()
        cycleAlignemnt1 = creationHeight + (votingPeriod1 - creationHeight % votingPeriod1)
        proposalEndHeight1 = cycleAlignemnt1 + votingPeriod1

        # Check all proposals cycle and proposal end height
        result = self.nodes[0].getgovproposal(tx2)
        assert_equal(result['currentCycle'], 1)
        assert_equal(result["cycleEndHeight"], proposalEndHeight1)
        assert_equal(result["proposalEndHeight"], proposalEndHeight1)
        result = self.nodes[0].getgovproposal(tx1)
        assert_equal(result['currentCycle'], 1)
        assert_equal(result["cycleEndHeight"], proposalEndHeight)
        assert_equal(result["proposalEndHeight"], multiProposalEndHeight)
        result = self.nodes[0].getgovproposal(tx)
        assert_equal(result['currentCycle'], 1)
        assert_equal(result["cycleEndHeight"], proposalEndHeight)
        assert_equal(result["proposalEndHeight"], proposalEndHeight)

        # Move to 2nd cycle for multi cycle proposal in old voting period
        self.nodes[0].generate(proposalEndHeight - self.nodes[0].getblockcount() + 1)
        self.sync_blocks()

        # Check all proposals cycle and proposal end height
        result = self.nodes[0].getgovproposal(tx2)
        assert_equal(result['currentCycle'], 1)
        assert_equal(result["cycleEndHeight"], proposalEndHeight1)
        assert_equal(result["proposalEndHeight"], proposalEndHeight1)
        result = self.nodes[0].getgovproposal(tx1)
        assert_equal(result['currentCycle'], 2)
        assert_equal(result["cycleEndHeight"], multiProposalEndHeight)
        assert_equal(result["proposalEndHeight"], multiProposalEndHeight)
        result = self.nodes[0].getgovproposal(tx)
        assert_equal(result['currentCycle'], 1)
        assert_equal(result["cycleEndHeight"], proposalEndHeight)
        assert_equal(result["proposalEndHeight"], proposalEndHeight)


if __name__ == '__main__':
    OnChainGovernanceTest().main()
