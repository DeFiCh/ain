#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test on chain government behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)

from decimal import Decimal

class ChainGornmentTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=101', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=101', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=101', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=101', '-subsidytest=1'],
        ]

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        node2 = self.nodes[2]
        node3 = self.nodes[3]
        address0 = node0.get_genesis_keys().ownerAuthAddress
        address1 = node1.get_genesis_keys().ownerAuthAddress
        address2 = node2.get_genesis_keys().ownerAuthAddress
        address3 = node3.get_genesis_keys().ownerAuthAddress

        masternodes = node0.listmasternodes()
        for node in masternodes:
            address = masternodes[node]['ownerAuthAddress']
            if address == address0:
                mn1 = node
            elif address == address1:
                mn2 = node
            elif address == address2:
                mn3 = node
            elif address == address3:
                mn4 = node

        node0.generate(100)
        node1.generate(1)
        self.sync_all()

        assert_equal(node0.getblockcount(), 101) # great world

        # Get addresses
        address = node0.getnewaddress()
        assert_equal(node0.getburninfo()['feeburn'], 0)
        title = "Create test community fund request proposal"
        tx = node0.createcfp({"title":title, "amount":100, "cycles":2, "payoutAddress":address})

        node0.sendtoaddress(address1, Decimal("1.0"))
        node0.sendtoaddress(address2, Decimal("1.0"))
        node0.sendtoaddress(address3, Decimal("1.0"))
        # Generate a block
        node0.generate(1)
        self.sync_all()
        node2.generate(1)
        self.sync_all()

        assert_equal(node0.getburninfo()['feeburn'], Decimal('1.00000000'))
        # cannot vote by non owning masternode
        assert_raises_rpc_error(-5, "Incorrect authorization", node0.vote, tx, mn2, "yes")

        node0.vote(tx, mn1, "yes")
        node0.generate(1)
        node1.vote(tx, mn2, "no")
        node1.generate(1)
        node2.vote(tx, mn3, "yes")
        node2.generate(1)
        self.sync_all()

        assert_raises_rpc_error(None, "does not mine at least one block", node3.vote, tx, mn4, "neutral")

        cycle1 = 102 + (102 % 70) + 70
        finalHeight = cycle1 + (cycle1 % 70) + 70

        results = node0.listproposals()
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result["proposalId"], tx)
        assert_equal(result["title"], title)
        assert_equal(result["type"], "CommunityFundProposal")
        assert_equal(result["status"], "Voting")
        assert_equal(result["amount"], Decimal("100"))
        assert_equal(result["cyclesPaid"], 1)
        assert_equal(result["totalCycles"], 2)
        assert_equal(result["payoutAddress"], address)
        assert_equal(result["finalizeAfter"], finalHeight)

        results = node1.listvotes(tx, mn1)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        results = node1.listvotes(tx, mn2)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'NO')

        results = node1.listvotes(tx, mn3)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        result = node1.listvotes(tx, "all")
        assert_equal(len(result), 3)

        node0.generate(cycle1 - node0.getblockcount() - 1)
        self.sync_all()
        bal = node0.listcommunitybalances()['CommunityDevelopmentFunds']
        assert_equal(node1.getaccount(address), [])
        node0.generate(1)
        self.sync_all()
        # CommunityDevelopmentFunds is charged by proposal
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], bal + Decimal("19.887464") - Decimal(100))
        # payout address
        assert_equal(node1.getaccount(address), ['100.00000000@DFI'])
        results = node0.listproposals()
        result = results[0]
        assert_equal(result["status"], "Voting")
        assert_equal(result["cyclesPaid"], 2)

        node0.generate(finalHeight - node0.getblockcount() - 1)
        self.sync_all()
        bal = node0.listcommunitybalances()['CommunityDevelopmentFunds']
        node0.generate(1)
        self.sync_all()
        # payout address isn't changed
        assert_equal(node1.getaccount(address), ['100.00000000@DFI'])
        # proposal fails, CommunityDevelopmentFunds does not charged
        assert_equal(node0.listcommunitybalances()['CommunityDevelopmentFunds'], bal + Decimal("19.55772984"))
        results = node0.listproposals()
        result = results[0]
        # not votes on 2nd cycle makes proposal to rejected
        assert_equal(result["status"], "Rejected")

        assert_equal(node0.listproposals("all", "voting"), [])
        assert_equal(node0.listproposals("all", "completed"), [])

        # Test Vote of Confidence
        assert_equal(node0.getburninfo()['feeburn'], Decimal('1.00000000'))
        title = "Create vote of confidence"
        tx = node0.createvoc(title)
        self.sync_mempools()
        node3.generate(1)
        self.sync_all()
        assert_equal(node0.getburninfo()['feeburn'], Decimal('6.00000000'))

        node0.vote(tx, mn1, "yes")
        node0.generate(1)
        node1.vote(tx, mn2, "no")
        node1.generate(1)
        node2.vote(tx, mn3, "yes")
        node2.generate(1)
        node3.vote(tx, mn4, "yes")
        node3.generate(1)
        self.sync_all()

        result = node0.getproposal(tx)
        assert_equal(result["proposalId"], tx)
        assert_equal(result["title"], title)
        assert_equal(result["type"], "VoteOfConfidence")
        assert_equal(result["status"], "Approved")
        assert_equal(result["approval"], "75.00 of 66.67%")

if __name__ == '__main__':
    ChainGornmentTest().main ()
