#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test OCG voting scenarios"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)

APPROVAL_THRESHOLD = 50
QUORUM = 50
VOTING_PERIOD = 10
NEXT_NETWORK_UPGRADE_HEIGHT = 200


class OCGVotingScenarionTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-jellyfish_regtest=1', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51',
             '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-grandcentralheight=101', f'-nextnetworkupgradeheight={NEXT_NETWORK_UPGRADE_HEIGHT}',
             '-rpc-governance-accept-neutral=1', '-simulatemainnet=1'],
        ]

    def setup_masternodes(self, nMasternodes=19):
        self.nodes[0].mns = []
        self.operatorAddresses = []

        for _ in range(nMasternodes):
            address = self.nodes[0].getnewaddress('', 'legacy')
            self.nodes[0].mns.append(self.nodes[0].createmasternode(address))
            self.operatorAddresses.append(address)
            self.nodes[0].generate(1)

        self.nodes[0].generate(20)  # Enables all MNs
        self.sync_blocks(timeout=120)

        # restart node with masternode_operator addresses to be able to mint with every MNs
        self.restart_node(0, self.nodes[0].extra_args + ['-masternode_operator={}'.format(address) for address in
                                                         self.operatorAddresses])

        # Mint with every MNs to meet voting eligibility criteria
        for address in self.operatorAddresses:
            self.nodes[0].generatetoaddress(1, address)

    def setup(self):
        # Generate chain
        self.nodes[0].generate(100)
        self.sync_blocks(timeout=120)

        self.setup_masternodes()

        # activate on-chain governance
        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/params/feature/gov': 'true',
            'v0/gov/proposals/voting_period': '{}'.format(VOTING_PERIOD),
        }})
        self.nodes[0].generate(1)

    def test_vote_on_cfp(self, yesVote, noVote, neutralVote, expectedStatus):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund proposal"
        amount = 100

        # Create CFP
        propId = self.nodes[0].creategovcfp(
            {"title": title, "context": context, "amount": amount, "cycles": 1, "payoutAddress": address})
        self.nodes[0].generate(1)
        mnIterator = iter(self.nodes[0].mns)

        for _ in range(yesVote):
            mnId = next(mnIterator)
            self.nodes[0].votegov(propId, mnId, 'yes')

        for _ in range(noVote):
            mnId = next(mnIterator)
            self.nodes[0].votegov(propId, mnId, 'no')

        for _ in range(neutralVote):
            mnId = next(mnIterator)
            self.nodes[0].votegov(propId, mnId, 'neutral')

        self.nodes[0].generate(1)

        self.nodes[0].generate(VOTING_PERIOD * 2)
        proposal = self.nodes[0].getgovproposal(propId)

        assert_equal(proposal['status'], expectedStatus)

        self.rollback_to(height)

    def test_vote_on_cfp_with_address(self, yesVote, noVote, neutralVote, expectedStatus):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund proposal"
        amount = 100

        # Create CFP
        propId = self.nodes[0].creategovcfp(
            {"title": title, "context": context, "amount": amount, "cycles": 1, "payoutAddress": address})
        self.nodes[0].generate(1)

        addressIterator = iter(self.operatorAddresses)

        for _ in range(yesVote):
            mnId = next(addressIterator)
            self.nodes[0].votegov(propId, mnId, 'yes')

        for _ in range(noVote):
            mnId = next(addressIterator)
            self.nodes[0].votegov(propId, mnId, 'no')

        for _ in range(neutralVote):
            mnId = next(addressIterator)
            self.nodes[0].votegov(propId, mnId, 'neutral')

        self.nodes[0].generate(1)

        self.nodes[0].generate(VOTING_PERIOD * 2)
        proposal = self.nodes[0].getgovproposal(propId)

        assert_equal(proposal['status'], expectedStatus)

        self.rollback_to(height)

    def test_vote_with_address_without_masternode(self):
        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund proposal"
        amount = 100

        # Create CFP
        propId = self.nodes[0].creategovcfp(
            {"title": title, "context": context, "amount": amount, "cycles": 1, "payoutAddress": address})
        self.nodes[0].generate(1)

        address = self.nodes[0].getnewaddress('', 'legacy')

        assert_raises_rpc_error(-8, "The masternode does not exist or the address doesn't own a masternode: {}".format(
            address), self.nodes[0].votegov, propId, address, 'yes')

    def test_vote_with_invalid_address(self):
        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund proposal"
        amount = 100

        # Create CFP
        propId = self.nodes[0].creategovcfp(
            {"title": title, "context": context, "amount": amount, "cycles": 1, "payoutAddress": address})
        self.nodes[0].generate(1)

        address = "fake_address"
        assert_raises_rpc_error(-8, "The masternode id or address is not valid: {}".format(address),
                                self.nodes[0].votegov, propId, address, 'yes')

    def test_scenario_below_approval_threshold(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=4, noVote=6, neutralVote=2, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=4, noVote=6, neutralVote=2, expectedStatus=expectedStatus)

    def test_scenario_at_approval_threshold(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=8, noVote=8, neutralVote=0, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=8, noVote=8, neutralVote=0, expectedStatus=expectedStatus)

    def test_scenario_above_approval_threshold(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=10, noVote=6, neutralVote=2, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=10, noVote=6, neutralVote=2, expectedStatus=expectedStatus)

    def test_scenario_below_quorum(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=6, noVote=2, neutralVote=1, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=6, noVote=2, neutralVote=1, expectedStatus=expectedStatus)

    def test_scenario_at_quorum(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=6, noVote=2, neutralVote=2, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=6, noVote=2, neutralVote=2, expectedStatus=expectedStatus)

    def test_scenario_above_quorum(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=6, noVote=3, neutralVote=2, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=6, noVote=3, neutralVote=2, expectedStatus=expectedStatus)

    def test_scenario_high_neutral_vote(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=8, noVote=3, neutralVote=5, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=8, noVote=3, neutralVote=5, expectedStatus=expectedStatus)

    def test_scenario_only_yes_and_neutral(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=8, noVote=0, neutralVote=8, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=8, noVote=0, neutralVote=8, expectedStatus=expectedStatus)

    def test_scenario_66_6_percent_approval_full_yes_votes(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=len(self.nodes[0].mns), noVote=0, neutralVote=0, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=len(self.nodes[0].mns), noVote=0, neutralVote=0,
                                           expectedStatus=expectedStatus)

    def test_scenario_66_6_percent_approval_full_no_votes(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=0, noVote=len(self.nodes[0].mns), neutralVote=0, expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=0, noVote=len(self.nodes[0].mns), neutralVote=0,
                                           expectedStatus=expectedStatus)

    def test_scenario_66_6_percent_approval_full_neutral_votes(self, expectedStatus):
        self.test_vote_on_cfp(yesVote=0, noVote=0, neutralVote=len(self.nodes[0].mns), expectedStatus=expectedStatus)
        self.test_vote_on_cfp_with_address(yesVote=0, noVote=0, neutralVote=len(self.nodes[0].mns),
                                           expectedStatus=expectedStatus)

    def scenarios_test(self):
        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/gov/proposals/cfp_approval_threshold': '{}%'.format(APPROVAL_THRESHOLD),
        }})
        self.nodes[0].generate(1)

        self.test_scenario_below_approval_threshold(expectedStatus='Rejected')
        self.test_scenario_at_approval_threshold(expectedStatus='Rejected')
        self.test_scenario_above_approval_threshold(expectedStatus='Completed')

        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/gov/proposals/quorum': '{}%'.format(QUORUM),
        }})
        self.nodes[0].generate(1)

        self.test_scenario_below_quorum(expectedStatus='Rejected')
        self.test_scenario_at_quorum(expectedStatus='Rejected')
        self.test_scenario_above_quorum(expectedStatus='Completed')

        # Currently marked as Rejected as neutral votes are incorrectly counted as no
        # Should assert that it's Completed once https://github.com/DeFiCh/ain/issues/1704 is fixed
        self.test_scenario_high_neutral_vote(expectedStatus='Rejected')

        # Currently marked as Rejected as neutral votes are incorrectly counted as no
        # Should assert that it's Completed once https://github.com/DeFiCh/ain/issues/1704 is fixed
        self.test_scenario_only_yes_and_neutral(expectedStatus='Rejected')

        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/gov/proposals/cfp_approval_threshold': '{}%'.format(66.6),
        }})
        self.nodes[0].generate(1)

        self.test_scenario_66_6_percent_approval_full_yes_votes(expectedStatus="Completed")
        self.test_scenario_66_6_percent_approval_full_no_votes(expectedStatus="Rejected")
        self.test_scenario_66_6_percent_approval_full_neutral_votes(expectedStatus="Rejected")

    def scenarios_neutral_votes_not_counted_test(self):
        self.nodes[0].generate(NEXT_NETWORK_UPGRADE_HEIGHT - self.nodes[0].getblockcount())

        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/gov/proposals/cfp_approval_threshold': '{}%'.format(APPROVAL_THRESHOLD),
        }})
        self.nodes[0].generate(1)

        self.test_scenario_below_approval_threshold(expectedStatus='Rejected')
        self.test_scenario_at_approval_threshold(expectedStatus='Rejected')
        self.test_scenario_above_approval_threshold(expectedStatus='Completed')

        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/gov/proposals/quorum': '{}%'.format(QUORUM),
        }})
        self.nodes[0].generate(1)

        self.test_scenario_below_quorum(expectedStatus='Rejected')
        self.test_scenario_at_quorum(expectedStatus='Rejected')
        self.test_scenario_above_quorum(expectedStatus='Completed')

        # Now it should be Completed after neutral votes fix
        self.test_scenario_high_neutral_vote(expectedStatus='Completed')

        # Now it should be Completed after neutral votes fix
        self.test_scenario_only_yes_and_neutral(expectedStatus='Completed')

        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/gov/proposals/cfp_approval_threshold': '{}%'.format(66.6),
        }})
        self.nodes[0].generate(1)

        self.test_scenario_66_6_percent_approval_full_yes_votes(expectedStatus="Completed")
        self.test_scenario_66_6_percent_approval_full_no_votes(expectedStatus="Rejected")
        self.test_scenario_66_6_percent_approval_full_neutral_votes(expectedStatus="Rejected")

    def run_test(self):

        self.setup()

        self.scenarios_test()
        self.test_vote_with_address_without_masternode()
        self.test_vote_with_invalid_address()

        self.scenarios_neutral_votes_not_counted_test()


if __name__ == '__main__':
    OCGVotingScenarionTest().main()
