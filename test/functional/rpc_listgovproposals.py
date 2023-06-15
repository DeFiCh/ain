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


class ListGovProposalsTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1',
             '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1',
             '-fortcanningcrunchheight=1', '-fortcanningspringheight=1', '-fortcanninggreatworldheight=1',
             '-fortcanningepilogueheight=1', '-grandcentralheight=1', '-subsidytest=1', '-txindex=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1',
             '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1',
             '-fortcanningcrunchheight=1', '-fortcanningspringheight=1', '-fortcanninggreatworldheight=1',
             '-fortcanningepilogueheight=1', '-grandcentralheight=1', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1',
             '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1',
             '-fortcanningcrunchheight=1', '-fortcanningspringheight=1', '-fortcanninggreatworldheight=1',
             '-fortcanningepilogueheight=1', '-grandcentralheight=1', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1',
             '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1',
             '-fortcanningcrunchheight=1', '-fortcanningspringheight=1', '-fortcanninggreatworldheight=1',
             '-fortcanningepilogueheight=1', '-grandcentralheight=1', '-subsidytest=1']
        ]

    def setup(self):
        self.address1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        self.address2 = self.nodes[2].get_genesis_keys().ownerAuthAddress
        self.address3 = self.nodes[3].get_genesis_keys().ownerAuthAddress

        # Get MN IDs
        self.mn0 = self.nodes[0].getmininginfo()['masternodes'][0]['id']
        self.mn1 = self.nodes[1].getmininginfo()['masternodes'][0]['id']
        self.mn2 = self.nodes[2].getmininginfo()['masternodes'][0]['id']
        self.mn3 = self.nodes[3].getmininginfo()['masternodes'][0]['id']

        # Generate chain
        self.nodes[0].generate(150)
        self.sync_blocks()

        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address3, Decimal("1.0"))
        self.nodes[0].generate(1)

        # mine at least one block with each mn to be able to vote
        self.nodes[1].generate(1)
        self.nodes[2].generate(1)
        self.nodes[3].generate(1)
        self.sync_blocks()

    def create_proposal(self, title="default title", context="default", amount=100, cycles=1):
        payoutAddress = self.nodes[0].getnewaddress()
        tx = self.nodes[0].creategovcfp(
            {"title": title, "context": context, "amount": amount, "cycles": cycles, "payoutAddress": payoutAddress})
        self.nodes[0].generate(1)
        self.sync_blocks()

        proposal = self.nodes[0].getgovproposal(tx)
        assert_equal(proposal["proposalId"], tx)
        return proposal

    def activate_onchain_gov_attributes(self):
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/gov': 'true'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/gov/proposals/fee_redistribution': 'true'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/gov-payout': 'true'}})
        # VOC emergency Attributes
        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/params/feature/gov-payout': 'true',
            'v0/gov/proposals/voc_emergency_period': '25',
            'v0/gov/proposals/voc_emergency_fee': '20.00000000',
            'v0/gov/proposals/voc_approval_threshold': '50.00%'
        }})
        self.nodes[0].generate(1)
        self.sync_blocks()

    def create_proposals(self, num_props=1, cycles=1):
        tmp_proposals = []
        for _ in range(num_props):
            tmp_proposal = self.create_proposal(cycles=cycles)
            tmp_proposals.append(tmp_proposal)

        return tmp_proposals

    def vote_on_proposals(self, proposals, vote="yes"):
        for proposal in proposals:
            self.nodes[0].votegov(proposal["proposalId"], self.mn0, vote)
            self.nodes[0].generate(1)
            self.nodes[1].votegov(proposal["proposalId"], self.mn1, vote)
            self.nodes[1].generate(1)
            self.nodes[2].votegov(proposal["proposalId"], self.mn2, vote)
            self.nodes[2].generate(1)
            self.sync_blocks()

    def create_100_proposals_and_revert(self):
        revertHeight = self.nodes[0].getblockcount()
        self.create_proposals(100)

        self.rollback_to(revertHeight)
        prop_list = self.nodes[0].listgovproposals()
        assert_equal(len(prop_list), 0)

    def create_20_proposals_go_to_end_cycle_1(self):
        tmp_proposals = self.create_proposals(num_props=20)

        # check all inside same cycle
        tmp_end_cycle_height = tmp_proposals[0]["cycleEndHeight"]
        for prop in tmp_proposals:
            assert_equal(prop["cycleEndHeight"], tmp_end_cycle_height)

        self.nodes[0].generate((tmp_end_cycle_height - self.nodes[0].getblockcount()) + 1)
        self.sync_blocks()

        prop_list = self.nodes[0].listgovproposals()

        assert_equal(len(prop_list), 20)

    def create_10_proposals_go_to_end_cycle_2(self):
        tmp_proposals = self.create_proposals(num_props=10, cycles=2)

        # check all inside same cycle
        tmp_end_cycle_height = tmp_proposals[0]["cycleEndHeight"]
        for prop in tmp_proposals:
            assert_equal(prop["cycleEndHeight"], tmp_end_cycle_height)

        prop_list = self.nodes[0].listgovproposals()
        assert_equal(len(prop_list), 30)
        prop_list = self.nodes[0].listgovproposals({})
        assert_equal(len(prop_list), 30)
        prop_list = self.nodes[0].listgovproposals("all", "all", 0)
        assert_equal(len(prop_list), 30)
        prop_list = self.nodes[0].listgovproposals("all", "all", -1)
        assert_equal(len(prop_list), 20)
        prop_list = self.nodes[0].listgovproposals("all", "all", 1)
        assert_equal(len(prop_list), 20)
        prop_list = self.nodes[0].listgovproposals("all", "all", 2)
        assert_equal(len(prop_list), 10)
        prop_list = self.nodes[0].listgovproposals("all", "rejected")
        assert_equal(len(prop_list), 20)
        prop_list = self.nodes[0].listgovproposals("all", "rejected", -1)
        assert_equal(len(prop_list), 20)
        prop_list = self.nodes[0].listgovproposals("all", "rejected", 1)
        assert_equal(len(prop_list), 20)
        prop_list = self.nodes[0].listgovproposals("all", "rejected", 2)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "completed")
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "completed", -1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "completed", 1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "completed", 2)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "voting")
        assert_equal(len(prop_list), 10)
        prop_list = self.nodes[0].listgovproposals("all", "voting", -1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "voting", 1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "voting", 2)
        assert_equal(len(prop_list), 10)
        prop_list = self.nodes[0].listgovproposals("voc", "all", -1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("voc", "all", 1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("voc")
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("cfp", "all")
        assert_equal(len(prop_list), 30)

        # Go to endCycleHeight and check rejected props
        self.nodes[0].generate((tmp_end_cycle_height - self.nodes[0].getblockcount()) + 3)
        self.sync_blocks()

        prop_list = self.nodes[0].listgovproposals("all", "rejected", 0)
        assert_equal(len(prop_list), 30)
        prop_list = self.nodes[0].listgovproposals("all", "rejected")
        assert_equal(len(prop_list), 30)
        prop_list = self.nodes[0].listgovproposals("all", "all")
        assert_equal(len(prop_list), 30)
        prop_list = self.nodes[0].listgovproposals("all")
        assert_equal(len(prop_list), 30)
        prop_list = self.nodes[0].listgovproposals("cfp")
        assert_equal(len(prop_list), 30)
        prop_list = self.nodes[0].listgovproposals("cfp", "voting")
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("cfp", "rejected", 2)
        assert_equal(len(prop_list), 10)

    def create_10_proposals_and_aprove_half(self):
        tmp_proposals = self.create_proposals(num_props=10, cycles=2)

        # check all inside same cycle
        tmp_end_cycle_height = tmp_proposals[0]["cycleEndHeight"]
        for prop in tmp_proposals:
            assert_equal(prop["cycleEndHeight"], tmp_end_cycle_height)

        self.vote_on_proposals(tmp_proposals[0: int(len(tmp_proposals) / 2)], vote="yes")

        prop_list = self.nodes[0].listgovproposals()
        assert_equal(len(prop_list), 40)
        prop_list = self.nodes[0].listgovproposals("all", "all", 0)
        assert_equal(len(prop_list), 40)
        prop_list = self.nodes[0].listgovproposals("all", "all", -1)
        assert_equal(len(prop_list), 10)
        prop_list = self.nodes[0].listgovproposals("all", "all", 1)
        assert_equal(len(prop_list), 20)
        prop_list = self.nodes[0].listgovproposals("all", "all", 2)
        assert_equal(len(prop_list), 10)
        prop_list = self.nodes[0].listgovproposals("all", "rejected")
        assert_equal(len(prop_list), 30)
        prop_list = self.nodes[0].listgovproposals("all", "rejected", -1)
        assert_equal(len(prop_list), 10)
        prop_list = self.nodes[0].listgovproposals("all", "rejected", 1)
        assert_equal(len(prop_list), 20)
        prop_list = self.nodes[0].listgovproposals("all", "rejected", 2)
        assert_equal(len(prop_list), 10)
        prop_list = self.nodes[0].listgovproposals("all", "completed")
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "completed", -1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "completed", 1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "completed", 2)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "voting")
        assert_equal(len(prop_list), 10)
        prop_list = self.nodes[0].listgovproposals("all", "voting", -1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "voting", 1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "voting", 2)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "voting", 3)
        assert_equal(len(prop_list), 10)
        prop_list = self.nodes[0].listgovproposals("voc", "all", -1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("voc", "all", 1)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("voc")
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("cfp", "all")
        assert_equal(len(prop_list), 40)

        # Go to endCycleHeight and check rejected props
        self.nodes[0].generate((tmp_end_cycle_height - self.nodes[0].getblockcount()) + 3)
        self.sync_blocks()

        prop_list = self.nodes[0].listgovproposals("all", "rejected", 0)
        assert_equal(len(prop_list), 35)
        prop_list = self.nodes[0].listgovproposals("all", "rejected")
        assert_equal(len(prop_list), 35)
        prop_list = self.nodes[0].listgovproposals("all", "all")
        assert_equal(len(prop_list), 40)
        prop_list = self.nodes[0].listgovproposals("all")
        assert_equal(len(prop_list), 40)
        prop_list = self.nodes[0].listgovproposals("cfp")
        assert_equal(len(prop_list), 40)
        prop_list = self.nodes[0].listgovproposals("cfp", "voting")
        assert_equal(len(prop_list), 5)
        prop_list = self.nodes[0].listgovproposals("cfp", "rejected", 3)
        assert_equal(len(prop_list), 5)
        prop_list = self.nodes[0].listgovproposals("cfp", "completed", 3)
        assert_equal(len(prop_list), 0)

        # check voting CFPs are in second cycle
        prop_list = self.nodes[0].listgovproposals("all", "all", 4)
        for prop in prop_list:
            assert_equal(prop["currentCycle"], 2)

        # approve CFPs and go to next cycle, checl completed state
        self.vote_on_proposals(prop_list, "yes")
        next_end_height = prop_list[0]["cycleEndHeight"]
        self.nodes[0].generate((next_end_height - self.nodes[0].getblockcount()) + 3)
        self.sync_blocks()
        prop_list = self.nodes[0].listgovproposals("all", "all", 4)
        assert_equal(len(prop_list), 5)
        prop_list = self.nodes[0].listgovproposals("all", "voting", 4)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "rejected", 4)
        assert_equal(len(prop_list), 0)
        prop_list = self.nodes[0].listgovproposals("all", "completed", 4)
        assert_equal(len(prop_list), 5)

    def run_test(self):
        self.setup()

        # check that on-chain governance is disabled
        assert_raises_rpc_error(-32600, "Cannot create tx, on-chain governance is not enabled", self.create_proposal)

        # activate onchain gov
        self.activate_onchain_gov_attributes()

        # self.create_100_proposals_and_revert()
        self.create_20_proposals_go_to_end_cycle_1()
        self.create_10_proposals_go_to_end_cycle_2()
        self.create_10_proposals_and_aprove_half()


if __name__ == '__main__':
    ListGovProposalsTest().main()
