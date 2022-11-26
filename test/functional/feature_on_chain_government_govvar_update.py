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
EMERGENCY_PERIOD=100
EMERGENCY_FEE=0.1

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

    def test_cfp_update_automatic_payout(self):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        amount = 100
        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": amount, "cycles": 2, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address3, Decimal("1.0"))
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        # Vote during first cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[2].votegov(propId, self.mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[3].votegov(propId, self.mn3, "yes")
        self.nodes[3].generate(1)
        self.sync_blocks(timeout=120)

        # No automatic payout before its activation via govvar
        account = self.nodes[0].getaccount(address)
        assert_equal(account, [])

        # Move to next cycle
        self.nodes[0].generate(VOTING_PERIOD * 2)
        self.sync_blocks(timeout=120)

        # Activate payout on second cycle
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/feature/gov-payout':'true'}})
        self.nodes[0].generate(1)

        # Vote during second cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[2].votegov(propId, self.mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[3].votegov(propId, self.mn3, "yes")
        self.nodes[3].generate(1)
        self.sync_blocks(timeout=120)

        # End proposal
        self.nodes[0].generate(VOTING_PERIOD)
        self.sync_blocks(timeout=120)

        # Automatic payout only for last cycle
        account = self.nodes[0].getaccount(address)
        assert_equal(account, ['100.00000000@DFI'])

        self.rollback_to(height, nodes=[0, 1, 2, 3])

    def test_cfp_update_quorum(self):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        amount = 100
        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": amount, "cycles": 2, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address3, Decimal("1.0"))
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        # Update quorum during first cycle.
        # 80% of masternodes should vote
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/quorum':'80%'}})
        self.nodes[0].generate(1)

        # Vote during first cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[2].votegov(propId, self.mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks(timeout=120)

        # Vote and move to next cycle
        self.nodes[3].votegov(propId, self.mn3, "yes")
        self.nodes[3].generate(VOTING_PERIOD * 2)
        self.sync_blocks(timeout=120)

        # First cycle should be approved
        proposal = self.nodes[0].getgovproposal(propId)
        assert_equal(proposal['status'], 'Voting')

        # Vote during second cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[2].votegov(propId, self.mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks(timeout=120)

        self.nodes[0].generate(VOTING_PERIOD)
        self.sync_blocks(timeout=120)

        # Quorum should be updated between cycles
        # Proposal should be rejected as only 75% of masternodes voted
        proposal = self.nodes[0].getgovproposal(propId)
        assert_equal(proposal['status'], 'Rejected')

        self.rollback_to(height, nodes=[0, 1, 2, 3])

    def test_cfp_update_majority_threshold(self):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        amount = 100
        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": amount, "cycles": 2, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address3, Decimal("1.0"))
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        # Update majority threshold during first cycle
        # 80% of masternodes should approve a CFP
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/cfp_required_votes':'80%'}})
        self.nodes[0].generate(1)

        # Vote during first cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[2].votegov(propId, self.mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[3].votegov(propId, self.mn3, "yes")
        self.nodes[3].generate(1)
        self.sync_blocks(timeout=120)

        # Move to next cycle
        self.nodes[0].generate(VOTING_PERIOD * 2)
        self.sync_blocks(timeout=120)

        # First cycle should be approved
        proposal = self.nodes[0].getgovproposal(propId)
        assert_equal(proposal['status'], 'Voting')

        # Vote during second cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[2].votegov(propId, self.mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[3].votegov(propId, self.mn3, "no")
        self.nodes[3].generate(1)
        self.sync_blocks(timeout=120)

        self.nodes[0].generate(VOTING_PERIOD)
        self.sync_blocks(timeout=120)

        # Majority threshold should be updated between cycles
        # Proposal should be rejected as only 75% of masternodes voted yes
        proposal = self.nodes[0].getgovproposal(propId)
        assert_equal(proposal['status'], 'Rejected')

        self.rollback_to(height, nodes=[0, 1, 2, 3])

    def test_cfp_update_fee_redistribution(self):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        amount = 100
        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": amount, "cycles": 2, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address3, Decimal("1.0"))
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        # Vote during first cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)

        # Move to next cycle
        self.nodes[0].generate(VOTING_PERIOD * 2)
        self.sync_blocks(timeout=120)

        # Fee should not be redistributed before activation
        mn0 = self.nodes[0].getmasternode(self.mn0)[self.mn0]
        account0 = self.nodes[0].getaccount(mn0['ownerAuthAddress'])
        assert_equal(account0, [])

        mn1 = self.nodes[0].getmasternode(self.mn1)[self.mn1]
        account1 = self.nodes[0].getaccount(mn1['ownerAuthAddress'])
        assert_equal(account1, [])

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/fee_redistribution':'true'}})
        self.nodes[0].generate(1)

        # Vote during second cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)

        self.nodes[0].generate(VOTING_PERIOD)
        self.sync_blocks(timeout=120)

        fee = 10
        numVoters = 2
        expectedAmount = '{}@DFI'.format(Decimal(fee / 2 / numVoters).quantize(Decimal('1E-8'), rounding=ROUND_DOWN))

        mn0 = self.nodes[0].getmasternode(self.mn0)[self.mn0]
        account0 = self.nodes[0].getaccount(mn0['ownerAuthAddress'])
        assert_equal(account0[0], expectedAmount)

        mn1 = self.nodes[0].getmasternode(self.mn1)[self.mn1]
        account1 = self.nodes[0].getaccount(mn1['ownerAuthAddress'])
        assert_equal(account1[0], expectedAmount)

        self.rollback_to(height, nodes=[0, 1, 2, 3])

    def test_cfp_update_cfp_fee(self):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        amount = 100
        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": amount, "cycles": 2, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address3, Decimal("1.0"))
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        # Vote during first cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)

        # Move to next cycle
        self.nodes[0].generate(VOTING_PERIOD * 2)
        self.sync_blocks(timeout=120)

        # Set higher fee
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/cfp_fee':'50%'}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/fee_redistribution':'true'}})
        self.nodes[0].generate(1)

        # Vote during second cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)

        self.nodes[0].generate(VOTING_PERIOD)
        self.sync_blocks(timeout=120)

        # Check that fee set at creation is used for redistribution
        fee = 10
        numVoters = 2
        expectedAmount = '{}@DFI'.format(Decimal(fee / 2 / numVoters).quantize(Decimal('1E-8'), rounding=ROUND_DOWN))

        mn0 = self.nodes[0].getmasternode(self.mn0)[self.mn0]
        account0 = self.nodes[0].getaccount(mn0['ownerAuthAddress'])
        assert_equal(account0[0], expectedAmount)

        mn1 = self.nodes[0].getmasternode(self.mn1)[self.mn1]
        account1 = self.nodes[0].getaccount(mn1['ownerAuthAddress'])
        assert_equal(account1[0], expectedAmount)

        self.rollback_to(height, nodes=[0, 1, 2, 3])

    def test_cfp_update_voting_period(self):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        amount = 100
        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": amount, "cycles": 2, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        # Vote during first cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)

        # Move to next cycle
        self.nodes[0].generate(VOTING_PERIOD * 2)
        self.sync_blocks(timeout=120)

        # Set higher fee
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/voting_period': '200'}})
        self.nodes[0].generate(1)

        # Vote during second cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)

        self.nodes[0].generate(VOTING_PERIOD)
        self.sync_blocks(timeout=120)

        # Voting period should is saved on creation
        proposal = self.nodes[0].getgovproposal(propId)
        assert_equal(proposal['status'], 'Completed')

        self.rollback_to(height, nodes=[0, 1, 2, 3])

    def test_cfp_update_voc_emergency_period(self):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        amount = 100
        # Create CFP
        propId = self.nodes[0].creategovvoc({"title": title, "context": context, "amount": amount, "payoutAddress": address, "emergency": True})
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        # Set longer emergency period
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/voc_emergency_period': str(EMERGENCY_PERIOD * 2)}})
        self.nodes[0].generate(1)

        # Move to next cycle
        self.nodes[0].generate(EMERGENCY_PERIOD)
        self.sync_blocks(timeout=120)

        # Emergency voting period should is saved on creation
        proposal = self.nodes[0].getgovproposal(propId)
        assert_equal(proposal['status'], 'Rejected')

        self.rollback_to(height, nodes=[0, 1, 2, 3])

    def test_cfp_update_voc_emergency_fee(self):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        amount = 100
        # Create CFP
        propId = self.nodes[0].creategovvoc({"title": title, "context": context, "amount": amount, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        # Set higher fee
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/voc_emergency_fee': str(EMERGENCY_FEE * 2)}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/fee_redistribution': 'true'}})
        self.nodes[0].generate(1)

        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)

        self.nodes[0].generate(EMERGENCY_PERIOD)
        self.sync_blocks(timeout=120)

        # Check that fee set at creation is used for redistribution
        fee = 5
        numVoters = 2
        expectedAmount = '{}@DFI'.format(Decimal(fee / 2 / numVoters).quantize(Decimal('1E-8'), rounding=ROUND_DOWN))

        mn0 = self.nodes[0].getmasternode(self.mn0)[self.mn0]
        account0 = self.nodes[0].getaccount(mn0['ownerAuthAddress'])
        assert_equal(account0[0], expectedAmount)

        mn1 = self.nodes[0].getmasternode(self.mn1)[self.mn1]
        account1 = self.nodes[0].getaccount(mn1['ownerAuthAddress'])
        assert_equal(account1[0], expectedAmount)

        self.rollback_to(height, nodes=[0, 1, 2, 3])

    def test_cfp_state_after_update(self):
        height = self.nodes[0].getblockcount()

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"
        title = "Create test community fund request proposal without automatic payout"
        amount = 100
        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": amount, "cycles": 1, "payoutAddress": address})

        # Fund addresses
        self.nodes[0].sendtoaddress(self.address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(self.address3, Decimal("1.0"))
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        # Vote during first cycle
        self.nodes[0].votegov(propId, self.mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].votegov(propId, self.mn1, "yes")
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[2].votegov(propId, self.mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks(timeout=120)

        # Vote and move to next cycle
        self.nodes[3].votegov(propId, self.mn3, "no")
        self.nodes[3].generate(VOTING_PERIOD * 2)
        self.sync_blocks(timeout=120)

        # First cycle should be approved
        proposal = self.nodes[0].getgovproposal(propId)
        assert_equal(proposal['status'], 'Completed')

        # Update quorum after end of proposal.
        # 80% of masternodes should vote
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/cfp_required_votes':'80%'}})
        self.nodes[0].generate(1)

        # Attributes change should not impact state of resolved proposals
        proposal = self.nodes[0].getgovproposal(propId)
        assert_equal(proposal['status'], 'Completed')

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
        self.sync_blocks(timeout=120)

        # Move to fork block
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)

        # grand central
        assert_equal(self.nodes[0].getblockcount(), 101)

        # Every masternode mine at least one block
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[1].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[2].generate(1)
        self.sync_blocks(timeout=120)
        self.nodes[3].generate(1)
        self.sync_blocks(timeout=120)

        # activate on-chain governance
        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/params/feature/gov':'true',
            'v0/gov/proposals/voting_period': str(VOTING_PERIOD),
        }})
        self.nodes[0].generate(1)

        self.sync_blocks(timeout=120)

    def run_test(self):

        self.setup()

        self.test_cfp_update_automatic_payout()
        self.test_cfp_update_quorum()
        self.test_cfp_update_majority_threshold()
        self.test_cfp_update_fee_redistribution()
        self.test_cfp_update_cfp_fee()
        self.test_cfp_update_voting_period()

        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/gov/proposals/voc_emergency_period': str(EMERGENCY_PERIOD),
            'v0/gov/proposals/voc_emergency_fee': str(EMERGENCY_FEE),
        }})
        self.nodes[0].generate(1)
        self.sync_blocks(timeout=120)

        self.test_cfp_update_voc_emergency_period()
        self.test_cfp_update_voc_emergency_fee()

        self.test_cfp_state_after_update()

if __name__ == '__main__':
    CFPFeeDistributionTest().main ()
