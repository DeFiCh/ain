#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test on chain government behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException
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
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-subsidytest=1', '-txindex=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-subsidytest=1'],
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
        mn3 = self.nodes[3].getmininginfo()['masternodes'][0]['id']

        # Generate chain
        self.nodes[0].generate(100)
        self.sync_blocks()

        # Check foundation output in coinbase TX
        result = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        raw_tx = self.nodes[0].getrawtransaction(result['tx'][0], 1)
        assert_equal(len(raw_tx['vout']), 3)
        assert_equal(raw_tx['vout'][0]['value'], Decimal('134.99983200'))
        assert_equal(raw_tx['vout'][1]['value'], Decimal('19.88746400'))
        assert_equal(raw_tx['vout'][2]['value'], Decimal('0'))

        # Move to fork block
        self.nodes[1].generate(1)
        self.sync_blocks()

        # grand central
        assert_equal(self.nodes[0].getblockcount(), 101)

        # Check foundation output no longer in coinbase TX
        result = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        raw_tx = self.nodes[0].getrawtransaction(result['tx'][0], 1)
        assert_equal(len(raw_tx['vout']), 2)
        assert_equal(raw_tx['vout'][0]['value'], Decimal('134.99983200'))
        assert_equal(raw_tx['vout'][1]['value'], Decimal('0'))

        # Create address for CFP
        address = self.nodes[0].getnewaddress()
        context = "<Git issue url>"

        # check that on-chain governance is disabled
        assert_raises_rpc_error(-32600, "Cannot create tx, on-chain governance is not enabled", self.nodes[0].creategovcfp, {"title": "test", "context": context, "amount": 100, "cycles": 4, "payoutAddress": address})

        # activate on-chain governance
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/feature/gov':'true'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check community dev fund present
        result = self.nodes[0].listcommunitybalances()
        assert_equal(result['CommunityDevelopmentFunds'], 2 * Decimal('19.88746400'))
        result = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        assert_equal(result['nonutxo'][0]['CommunityDevelopmentFunds'], Decimal('19.88746400'))

        # Check errors
        assert_raises_rpc_error(-32600, "proposal cycles can be between 1 and 100", self.nodes[0].creategovcfp, {"title": "test", "context": context, "amount": 100, "cycles": 101, "payoutAddress": address})
        assert_raises_rpc_error(-32600, "proposal cycles can be between 1 and 100", self.nodes[0].creategovcfp, {"title": "test", "context": context, "amount": 100, "cycles": 0, "payoutAddress": address})

        # Check burn empty
        assert_equal(self.nodes[0].getburninfo()['feeburn'], 0)

        title = "Create test community fund request proposal"
        # Test invalid title
        try:
            self.nodes[0].creategovcfp({"title":"a" * 129, "context": context, "amount":100, "cycles":2, "payoutAddress":address})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("proposal title cannot be more than 128 bytes" in errorString)

        # Test invalid context
        try:
            self.nodes[0].creategovcfp(
                {"title": title, "context": "a" * 513, "amount": 100, "cycles": 2, "payoutAddress": address})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("proposal context cannot be more than 512 bytes" in errorString)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/fee_redistribution':'true'}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/feature/gov-payout':'true'}})

        # Create CFP
        tx = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": 100, "cycles": 2, "payoutAddress": address})
        # Fund addresses
        self.nodes[0].sendtoaddress(address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(address3, Decimal("1.0"))

        # Generate a block
        self.nodes[0].generate(1)

        creationHeight = self.nodes[0].getblockcount()
        self.sync_blocks()
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Check fee burn
        assert_equal(self.nodes[0].getburninfo()['feeburn'], Decimal('5.00000000'))

        # cannot vote by non owning masternode
        assert_raises_rpc_error(-5, "Incorrect authorization", self.nodes[0].votegov, tx, mn1, "yes")

        # Vote on proposal
        self.nodes[0].votegov(tx, mn0, "yes")
        self.nodes[0].generate(1)
        self.nodes[1].votegov(tx, mn1, "no")
        self.nodes[1].generate(1)
        self.nodes[2].votegov(tx, mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Try and vote with non-staked MN
        assert_raises_rpc_error(None, "does not mine at least one block", self.nodes[3].votegov, tx, mn3, "neutral")

        # voting period
        votingPeriod = 70

        # Calculate cycle
        cycle1 = creationHeight + (votingPeriod - creationHeight % votingPeriod) + votingPeriod
        proposalEndHeight = cycle1 + votingPeriod

        # Check proposal and votes
        result = self.nodes[0].listgovproposals()
        assert_equal(len(result), 1)
        assert_equal(result[0]["proposalId"], tx)
        assert_equal(result[0]["title"], title)
        assert_equal(result[0]["context"], context)
        assert_equal(result[0]["type"], "CommunityFundProposal")
        assert_equal(result[0]["status"], "Voting")
        assert_equal(result[0]["amount"], Decimal("100"))
        assert_equal(result[0]["currentCycle"], 1)
        assert_equal(result[0]["totalCycles"], 2)
        assert_equal(result[0]["payoutAddress"], address)
        assert_equal(result[0]["proposalEndHeight"], proposalEndHeight)

        # Check individual MN votes
        results = self.nodes[1].listgovvotes(tx, mn0)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        results = self.nodes[1].listgovvotes(tx, mn1)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'NO')

        results = self.nodes[1].listgovvotes(tx, mn2)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        # Check total votes
        result = self.nodes[1].listgovvotes(tx, "all")
        assert_equal(len(result), 3)

        # Move to just before cycle payout
        self.nodes[0].generate(cycle1 - self.nodes[0].getblockcount() - 1)
        self.sync_blocks()
        bal = self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds']
        assert_equal(self.nodes[1].getaccount(address), [])

        # Check first cycle length
        result = self.nodes[0].listgovproposals()
        assert_equal(result[0]['currentCycle'], 1)

        # Move to cycle payout
        self.nodes[0].generate(1)
        self.sync_blocks()

        result = self.nodes[0].listgovproposals()
        blockcount = self.nodes[0].getblockcount()

        # Actually moved to next cycle at cycle1
        assert_equal(result[0]['currentCycle'], 2)
        assert_equal(blockcount, cycle1)

        # First cycle should last for at least a votingPeriod
        assert(cycle1 - creationHeight >= votingPeriod)

        # CommunityDevelopmentFunds is charged by proposal
        assert_equal(self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds'], bal + Decimal("19.887464") - Decimal(100))

        # payout address
        assert_equal(self.nodes[1].getaccount(address), ['100.00000000@DFI'])
        result = self.nodes[0].listgovproposals()[0]
        assert_equal(result["status"], "Voting")
        assert_equal(result["currentCycle"], 2)

        # Move to just before final height
        self.nodes[0].generate(proposalEndHeight - self.nodes[0].getblockcount() - 1)
        bal = self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds']

        # Move to final height
        self.nodes[0].generate(1)
        self.sync_blocks()

        # payout address isn't changed
        assert_equal(self.nodes[1].getaccount(address), ['100.00000000@DFI'])

        # proposal fails, CommunityDevelopmentFunds does not charged
        assert_equal(self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds'], bal + Decimal("19.55772984"))

        # not votes on 2nd cycle makes proposal to rejected
        result = self.nodes[0].listgovproposals()[0]
        assert_equal(result["status"], "Rejected")

        # No proposals pending
        assert_equal(self.nodes[0].listgovproposals("all", "voting"), [])
        assert_equal(self.nodes[0].listgovproposals("all", "completed"), [])
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected")), 1)

        # Test Vote of Confidence
        title = "Create vote of confidence"
        context = "Test context"
        tx = self.nodes[0].creategovvoc({"title": title, "context": context})
        raw_tx = self.nodes[0].getrawtransaction(tx)
        self.nodes[3].sendrawtransaction(raw_tx)
        self.nodes[3].generate(1)
        self.sync_blocks()

        # Check burn fee increment
        assert_equal(self.nodes[0].getburninfo()['feeburn'], Decimal('7.50000000'))

        # Cast votes
        self.nodes[0].votegov(tx, mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[1].votegov(tx, mn1, "no")
        self.nodes[1].generate(1)
        self.sync_blocks()

        self.nodes[2].votegov(tx, mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks()

        self.nodes[3].votegov(tx, mn3, "yes")
        self.nodes[3].generate(1)
        self.sync_blocks()

        # Check results
        result = self.nodes[0].getgovproposal(tx)
        assert_equal(result["proposalId"], tx)
        assert_equal(result["title"], title)
        assert_equal(result["context"], context)
        assert_equal(result["type"], "VoteOfConfidence")
        assert_equal(result["status"], "Approved")
        assert_equal(result["approval"], "75.00 of 66.67%")
        assert_equal(result["ends"], "1 days")

        assert_equal(len(self.nodes[0].listgovproposals("all", "voting")), 1)
        assert_equal(self.nodes[0].listgovproposals("all", "completed"), [])
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected")), 1)

        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/params/feature/gov-payout':'false',
            'v0/gov/proposals/cfp_fee':'0.25',
            'v0/gov/proposals/voting_period':'100',
        }})

        self.nodes[0].generate(1)
        self.sync_blocks()

        title = "Create test community fund request proposal without automatic payout"

        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": 50, "cycles": 2, "payoutAddress": address})
        creationHeight = self.nodes[0].getblockcount()

        # Fund addresses
        self.nodes[0].sendtoaddress(address1, Decimal("1.0"))
        self.nodes[0].sendtoaddress(address2, Decimal("1.0"))
        self.nodes[0].sendtoaddress(address3, Decimal("1.0"))

        # Generate a block
        self.nodes[0].generate(1)
        self.sync_blocks()
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Check fee burn
        assert_equal(self.nodes[0].getburninfo()['feeburn'], Decimal('13.75000000'))

        # cannot vote by non owning masternode
        assert_raises_rpc_error(-5, "Incorrect authorization", self.nodes[0].votegov, propId, mn1, "yes")

        # Vote on proposal
        self.nodes[0].votegov(propId, mn0, "yes")
        self.nodes[0].generate(1)
        self.nodes[1].votegov(propId, mn1, "no")
        self.nodes[1].generate(1)
        self.nodes[2].votegov(propId, mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Calculate cycle
        votingPeriod = 100
        cycle1 = creationHeight + (votingPeriod - creationHeight % votingPeriod) + votingPeriod
        proposalEndHeight = cycle1 + votingPeriod

        # Check proposal and votes
        result = self.nodes[0].listgovproposals("cfp","voting")
        assert_equal(result[0]["proposalId"], propId)
        assert_equal(result[0]["title"], title)
        assert_equal(result[0]["context"], context)
        assert_equal(result[0]["type"], "CommunityFundProposal")
        assert_equal(result[0]["status"], "Voting")
        assert_equal(result[0]["amount"], Decimal("50"))
        assert_equal(result[0]["currentCycle"], 1)
        assert_equal(result[0]["totalCycles"], 2)
        assert_equal(result[0]["payoutAddress"], address)
        assert_equal(result[0]["proposalEndHeight"], proposalEndHeight)

        # Check individual MN votes
        results = self.nodes[1].listgovvotes(propId, mn0)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        results = self.nodes[1].listgovvotes(propId, mn1)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'NO')

        results = self.nodes[1].listgovvotes(propId, mn2)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        # Check total votes
        result = self.nodes[1].listgovvotes(propId, "all")
        assert_equal(len(result), 3)

        # Move to just before cycle payout
        self.nodes[0].generate(cycle1 - self.nodes[0].getblockcount() - 1)
        self.sync_blocks()
        bal = self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds']
        assert_equal(self.nodes[1].getaccount(address), ['100.00000000@DFI'])

        # Move to cycle payout
        self.nodes[0].generate(1)
        self.sync_blocks()

        # CommunityDevelopmentFunds is not charged by proposal as automatic payout is disabled
        assert_equal(self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds'], bal + Decimal("19.23346268"))

        # payout address
        assert_equal(self.nodes[1].getaccount(address), ['100.00000000@DFI'])
        result = self.nodes[0].getgovproposal(propId)
        assert_equal(result["status"], "Voting")
        assert_equal(result["currentCycle"], 2)

        # Move to just before final height
        self.nodes[0].generate(proposalEndHeight - self.nodes[0].getblockcount() - 1)
        bal = self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds']

        # Move to final height
        self.nodes[0].generate(1)
        self.sync_blocks()

        # payout address isn't changed
        assert_equal(self.nodes[1].getaccount(address), ['100.00000000@DFI'])

        # proposal fails, CommunityDevelopmentFunds is not charged
        assert_equal(self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds'], bal + Decimal("19.23346268"))

        # not votes on 2nd cycle makes proposal to rejected
        result = self.nodes[0].listgovproposals()[0]
        assert_equal(result["status"], "Rejected")

        # No proposals pending
        assert_equal(self.nodes[0].listgovproposals("all", "voting"), [])
        assert_equal(self.nodes[0].listgovproposals("all", "completed"), [])
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected")), 3)

        emergencyPeriod = 25
        title = 'Emergency VOC'

        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/params/feature/gov-payout':'true',
            'v0/gov/proposals/voc_emergency_period': f'{emergencyPeriod}',
            'v0/gov/proposals/voc_emergency_fee':'20.00000000',
            'v0/gov/proposals/voc_required_votes':'0.4999'
        }})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Test emergency Vote of Confidence
        title = "Create vote of confidence custom fee custom majority"
        context = "Test context"
        tx = self.nodes[0].creategovvoc({"title": title, "context": context, "emergency": True})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check burn fee increment
        assert_equal(self.nodes[0].getburninfo()['feeburn'], Decimal('23.750000000'))

        # Cast votes
        self.nodes[0].votegov(tx, mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[1].votegov(tx, mn1, "no")
        self.nodes[1].generate(1)
        self.sync_blocks()

        self.nodes[2].votegov(tx, mn2, "no")
        self.nodes[2].generate(1)
        self.sync_blocks()

        self.nodes[3].votegov(tx, mn3, "yes")
        self.nodes[3].generate(1)
        self.sync_blocks()

        # Check results
        result = self.nodes[0].getgovproposal(tx)
        assert_equal(result["proposalId"], tx)
        assert_equal(result["title"], title)
        assert_equal(result["context"], context)
        assert_equal(result["type"], "VoteOfConfidence")
        assert_equal(result["status"], "Approved")
        assert_equal(result["approval"], "50.00 of 49.99%")
        assert_equal(result["ends"], "3 hours")

        assert_equal(len(self.nodes[0].listgovproposals("all", "voting")), 1)
        assert_equal(len(self.nodes[0].listgovproposals("all", "completed")), 0)
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected")), 3)

        # Test emergency quorum
        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/gov/proposals/voc_emergency_quorum':'80.01%'
        }})
        self.nodes[0].generate(1)
        self.sync_blocks()

        tx = self.nodes[0].creategovvoc({"title": "Create vote of confidence custom fee custom majority", "context": "test context", "emergency": True})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Cast votes
        self.nodes[0].votegov(tx, mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[1].votegov(tx, mn1, "no")
        self.nodes[1].generate(1)
        self.sync_blocks()

        self.nodes[3].votegov(tx, mn3, "yes")
        self.nodes[3].generate(1)
        self.sync_blocks()

        self.nodes[0].generate(votingPeriod)
        self.sync_blocks()

        # Check results, proposal should be rejected as only 75% of masternodes voted
        result = self.nodes[0].getgovproposal(tx)
        assert_equal(result["status"], "Rejected")

        assert_equal(len(self.nodes[0].listgovproposals("all", "voting")), 0)
        assert_equal(len(self.nodes[0].listgovproposals("all", "completed")), 1)
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected")), 4)

if __name__ == '__main__':
    ChainGornmentTest().main ()
