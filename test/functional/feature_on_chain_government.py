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

class OnChainGovernanceTest(DefiTestFramework):
    mns = None
    proposalId = ""

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
        self.mns = [mn0, mn1, mn2, mn3]

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

        # Test invalid keys
        try:
            self.nodes[0].creategovcfp(
                {"title": title, "context": "a", "amount": 100, "cycle": 2, "payoutAddress": address})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Unexpected key cycle" in errorString)
        try:
            self.nodes[0].creategovvoc(
                {"title": title, "context": "a", "amounts": 100})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Unexpected key amounts" in errorString)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/gov/proposals/fee_redistribution':'true'}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/feature/gov-payout':'true'}})

        # Create CFP
        cfp1 = self.nodes[0].creategovcfp({"title": title, "context": context, "amount": 100, "cycles": 2, "payoutAddress": address})
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
        assert_raises_rpc_error(-5, "Incorrect authorization", self.nodes[0].votegov, cfp1, mn1, "yes")

        assert_raises_rpc_error(-8, "Decision supports yes or no. Neutral is currently disabled because of issue https://github.com/DeFiCh/ain/issues/1704", self.nodes[0].votegov, cfp1, mn0, "neutral")

        # Vote on proposal
        self.nodes[0].votegov(cfp1, mn0, "yes")
        self.nodes[0].generate(1)
        self.nodes[1].votegov(cfp1, mn1, "no")
        self.nodes[1].generate(1)
        self.nodes[2].votegov(cfp1, mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Try and vote with non-staked MN
        assert_raises_rpc_error(None, "does not mine at least one block", self.nodes[3].votegov, cfp1, mn3, "yes")

        # voting period
        votingPeriod = 70

        # Calculate cycle
        cycle1 = creationHeight + (votingPeriod - creationHeight % votingPeriod) + votingPeriod
        proposalEndHeight = cycle1 + votingPeriod

        # Check proposal and votes
        result = self.nodes[0].listgovproposals()
        assert_equal(len(result), 1)
        assert_equal(result[0]["proposalId"], cfp1)
        assert_equal(result[0]["creationHeight"], creationHeight)
        assert_equal(result[0]["title"], title)
        assert_equal(result[0]["context"], context)
        assert_equal(result[0]["contextHash"], "")
        assert_equal(result[0]["status"], "Voting")
        assert_equal(result[0]["type"], "CommunityFundProposal")
        assert_equal(result[0]["amount"], Decimal("100"))
        assert_equal(result[0]["payoutAddress"], address)
        assert_equal(result[0]["currentCycle"], 1)
        assert_equal(result[0]["totalCycles"], 2)
        assert_equal(result[0]["cycleEndHeight"], cycle1)
        assert_equal(result[0]["proposalEndHeight"], proposalEndHeight)
        assert_equal(result[0]["votingPeriod"], votingPeriod)
        assert_equal(result[0]["quorum"], "1.00%")
        assert_equal(result[0]["approvalThreshold"], "50.00%")
        assert_equal(result[0]["fee"], Decimal("10"))

        # Check individual MN votes
        results = self.nodes[1].listgovproposalvotes(cfp1, mn0)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        results = self.nodes[1].listgovproposalvotes(cfp1, mn1)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'NO')

        results = self.nodes[1].listgovproposalvotes(cfp1, mn2)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        # Check total votes
        result = self.nodes[1].listgovproposalvotes(cfp1, "all")
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

        # Check VoC in mempool
        result = self.nodes[0].getcustomtx(tx)
        assert_equal(result['type'], 'CreateVoc')
        assert_equal(result['valid'], True)
        assert_equal(result['results']['proposalId'], tx)
        assert_equal(result['results']['type'], 'VoteOfConfidence')
        assert_equal(result['results']['title'], title)
        assert_equal(result['results']['context'], context)
        assert_equal(result['results']['amount'], Decimal('0E-8'))
        assert_equal(result['results']['cycles'], 1)
        assert_equal(result['results']['proposalEndHeight'], 420)
        assert_equal(result['results']['payoutAddress'], '')

        # Send transaction through a different node
        self.nodes[3].sendrawtransaction(raw_tx)
        self.nodes[3].generate(1)
        self.sync_blocks()
        creationHeight = self.nodes[0].getblockcount()

        # Check VoC on-chain
        result = self.nodes[0].getcustomtx(tx)
        assert_equal(result['type'], 'CreateVoc')
        assert_equal(result['valid'], True)
        assert_equal(result['results']['proposalId'], tx)
        assert_equal(result['results']['type'], 'VoteOfConfidence')
        assert_equal(result['results']['title'], title)
        assert_equal(result['results']['context'], context)
        assert_equal(result['results']['amount'], Decimal('0E-8'))
        assert_equal(result['results']['cycles'], 1)
        assert_equal(result['results']['proposalEndHeight'], 420)
        assert_equal(result['results']['payoutAddress'], '')

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

        cycle1 = creationHeight + (votingPeriod - creationHeight % votingPeriod) + votingPeriod
        proposalEndHeight = cycle1

        # Check results
        result = self.nodes[0].getgovproposal(tx)
        assert_equal(result["proposalId"], tx)
        assert_equal(result["creationHeight"], creationHeight)
        assert_equal(result["title"], title)
        assert_equal(result["context"], context)
        assert_equal(result["contextHash"], "")
        assert_equal(result["status"], "Voting")
        assert_equal(result["type"], "VoteOfConfidence")
        assert_equal(result["currentCycle"], 1)
        assert_equal(result["totalCycles"], 1)
        assert_equal(result["cycleEndHeight"], cycle1)
        assert_equal(result["proposalEndHeight"], proposalEndHeight)
        assert_equal(result["votingPeriod"], votingPeriod)
        assert_equal(result["quorum"], "1.00%")
        assert_equal(result["votesPossible"], Decimal("4"))
        assert_equal(result["votesPresent"], Decimal("4"))
        assert_equal(result["votesPresentPct"], "100.00%")
        assert_equal(result["votesYes"], Decimal("3"))
        assert_equal(result["votesYesPct"], "75.00%")
        assert_equal(result["votesNo"], Decimal("1"))
        assert_equal(result["votesNeutral"], Decimal("0"))
        assert_equal(result["votesInvalid"], Decimal("0"))
        assert_equal(result["feeRedistributionPerVote"], Decimal("0.625"))
        assert_equal(result["feeRedistributionTotal"], Decimal("2.5"))
        assert_equal(result["approvalThreshold"], "66.67%")
        assert_equal(result["fee"], Decimal("5"))

        assert_equal(len(self.nodes[0].listgovproposals("all", "voting")), 1)
        assert_equal(self.nodes[0].listgovproposals("all", "completed"), [])
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected")), 1)

        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/params/feature/gov-payout':'false',
            'v0/gov/proposals/cfp_fee':'0.25',
            'v0/gov/proposals/voting_period':'100',
        }})
        votingPeriod = 100

        self.nodes[0].generate(1)
        self.sync_blocks()

        title = "Create test community fund request proposal without automatic payout"

        # Create CFP
        propId = self.nodes[0].creategovcfp({"title": title,
            "context": context,
            "amount": 50,
            "cycles": 2,
            "payoutAddress": address})
        self.nodes[0].generate(1)
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
        results = self.nodes[0].listgovproposals("cfp","voting")
        result = results[0]
        assert_equal(result["proposalId"], propId)
        assert_equal(result["creationHeight"], creationHeight)
        assert_equal(result["title"], title)
        assert_equal(result["context"], context)
        assert_equal(result["contextHash"], "")
        assert_equal(result["status"], "Voting")
        assert_equal(result["type"], "CommunityFundProposal")
        assert_equal(result["amount"], Decimal("50"))
        assert_equal(result["payoutAddress"], address)
        assert_equal(result["currentCycle"], 1)
        assert_equal(result["totalCycles"], 2)
        assert_equal(result["cycleEndHeight"], cycle1)
        assert_equal(result["proposalEndHeight"], proposalEndHeight)
        assert_equal(result["votingPeriod"], votingPeriod)
        assert_equal(result["quorum"], "1.00%")
        assert_equal(result["approvalThreshold"], "50.00%")
        assert_equal(result["fee"], Decimal("12.5"))

        # Check individual MN votes
        results = self.nodes[1].listgovproposalvotes(propId, mn0)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        results = self.nodes[1].listgovproposalvotes(propId, mn1)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'NO')

        results = self.nodes[1].listgovproposalvotes(propId, mn2)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        # Check total votes
        result = self.nodes[1].listgovproposalvotes(propId, "all")
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
        cycle2 = cycle1 + votingPeriod
        assert_equal(self.nodes[1].getaccount(address), ['100.00000000@DFI'])
        result = self.nodes[0].getgovproposal(propId)
        assert_equal(result["status"], "Voting")
        assert_equal(result["currentCycle"], 2)
        assert_equal(result["cycleEndHeight"], cycle2)

        # vote cycle 2
        self.nodes[0].votegov(propId, mn0, "no")
        self.nodes[0].generate(1)

        listvotes = self.nodes[0].listgovproposalvotes(propId)
        assert_equal(len(listvotes), 1)
        listvotes = self.nodes[0].listgovproposalvotes(propId, 'all', 0)
        assert_equal(len(listvotes), 1)
        listvotes = self.nodes[0].listgovproposalvotes(propId, 'all', -1)
        assert_equal(len(listvotes), 4)
        listvotes = self.nodes[0].listgovproposalvotes(propId, 'all', 1)
        assert_equal(len(listvotes), 3)
        listvotes = self.nodes[0].listgovproposalvotes(propId, mn0, -1)
        assert_equal(len(listvotes), 2)
        listvotes = self.nodes[0].listgovproposalvotes(propId, mn0, 2)
        assert_equal(len(listvotes), 1)
        listvotes = self.nodes[0].listgovproposalvotes(propId, 'all', 2)
        assert_equal(len(listvotes), 1)


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
        result = self.nodes[0].listgovproposals({"pagination":{"start": propId, "including_start":True}})[0]
        assert_equal(result["status"], "Rejected")

        # No proposals pending
        assert_equal(self.nodes[0].listgovproposals("all", "voting"), [])
        assert_equal(len(self.nodes[0].listgovproposals("all", "completed")), 1)
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected")), 2)

        emergencyPeriod = 25
        title = 'Emergency VOC'

        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/params/feature/gov-payout':'true',
            'v0/gov/proposals/voc_emergency_period': f'{emergencyPeriod}',
            'v0/gov/proposals/voc_emergency_fee':'20.00000000',
            'v0/gov/proposals/voc_approval_threshold':'50.00%'
        }})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Test emergency Vote of Confidence
        title = "Create vote of confidence custom fee custom majority"
        context = "Test context"
        tx = self.nodes[0].creategovvoc({"title": title, "context": context, "emergency": True})
        self.nodes[0].generate(1)
        creationHeight = self.nodes[0].getblockcount()
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

        cycle1 = creationHeight + emergencyPeriod
        proposalEndHeight = cycle1

        # Check results
        result = self.nodes[0].getgovproposal(tx)
        assert_equal(result["proposalId"], tx)
        assert_equal(result["creationHeight"], creationHeight)
        assert_equal(result["title"], title)
        assert_equal(result["context"], context)
        assert_equal(result["contextHash"], "")
        assert_equal(result["status"], "Voting")
        assert_equal(result["type"], "VoteOfConfidence")
        assert_equal(result["currentCycle"], 1)
        assert_equal(result["totalCycles"], 1)
        assert_equal(result["cycleEndHeight"], cycle1)
        assert_equal(result["proposalEndHeight"], proposalEndHeight)
        assert_equal(result["votingPeriod"], emergencyPeriod)
        assert_equal(result["quorum"], "10.00%")
        assert_equal(result["votesPossible"], Decimal("4"))
        assert_equal(result["votesPresent"], Decimal("4"))
        assert_equal(result["votesPresentPct"], "100.00%")
        assert_equal(result["votesYes"], Decimal("2"))
        assert_equal(result["votesYesPct"], "50.00%")
        assert_equal(result["votesNo"], Decimal("2"))
        assert_equal(result["votesNeutral"], Decimal("0"))
        assert_equal(result["votesInvalid"], Decimal("0"))
        assert_equal(result["feeRedistributionPerVote"], Decimal("2.5"))
        assert_equal(result["feeRedistributionTotal"], Decimal("10"))
        assert_equal(result["approvalThreshold"], "50.00%")
        assert_equal(result["fee"], Decimal("20"))
        assert_equal(result["options"], ["emergency"])

        assert_equal(len(self.nodes[0].listgovproposals("all", "voting")), 1)
        assert_equal(len(self.nodes[0].listgovproposals("all", "completed")), 1)
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected")), 2)

        # Test emergency quorum
        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/gov/proposals/voc_emergency_quorum':'80.01%'
        }})
        self.nodes[0].generate(1)
        self.sync_blocks()

        tx = self.nodes[0].creategovvoc({"title": "Create vote of confidence custom fee custom majority", "context": "test context", "special": True})
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

        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected", 0, {"limit":1})), 1)
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected", 0, {"limit":0})), 4)
        assert_equal(len(self.nodes[0].listgovproposals("all", "rejected", 0, {"limit":10})), 4)
        assert_equal(self.nodes[0].listgovproposals("all", "rejected", 0, {"start": tx, "including_start": True})[0]["proposalId"], tx)

        assert_equal(len(self.nodes[0].listgovproposals({"type":"all", "status":"voting"})), 0)
        assert_equal(len(self.nodes[0].listgovproposals({"type":"all", "status":"completed"})), 1)
        assert_equal(len(self.nodes[0].listgovproposals({"type":"all", "status":"rejected"})), 4)

        assert_equal(len(self.nodes[0].listgovproposals({"type":"all", "status":"rejected", "pagination": {"limit":1}})), 1)
        assert_equal(len(self.nodes[0].listgovproposals({"type":"all", "status":"rejected", "pagination": {"limit":0}})), 4)
        assert_equal(len(self.nodes[0].listgovproposals({"type":"all", "status":"rejected", "pagination": {"limit":10}})), 4)
        assert_equal(self.nodes[0].listgovproposals({"type":"all", "status":"rejected", "pagination": {"start": tx, "including_start": True}})[0]["proposalId"], tx)

        # Test pagination, total number of votes is 3
        assert_equal(len(self.nodes[1].listgovproposalvotes(
            {"proposalId": tx, "masternode": "all", "cycle": -1, "pagination": {"start": 0}})), 2)
        assert_equal(len(self.nodes[1].listgovproposalvotes(
            {"proposalId": tx, "masternode": "all", "cycle": -1, "pagination": {"start": 0, "including_start": True}})),
                     3)
        assert_equal(
            len(self.nodes[1].listgovproposalvotes({"proposalId": tx,  "masternode": "all", "cycle": -1, "pagination": {"start": 0, "including_start": True, "limit": 2}})),
            2)
        assert_equal(
            len(self.nodes[1].listgovproposalvotes({"proposalId": tx,  "masternode": "all", "cycle": -1, "pagination": {"start": 0, "including_start": True, "limit": 1}})),
            1)

        # should be empty if start > number of entries
        assert_equal(
            len(self.nodes[1].listgovproposalvotes({"proposalId": tx,  "masternode": "all", "cycle": -1, "pagination": {"start": 10, "including_start": True, "limit": 1}})),
            0)

        # should return all entries if limit is 0
        assert_equal(
            len(self.nodes[1].listgovproposalvotes({"proposalId": tx,  "masternode": "all", "cycle": -1, "pagination": {"start": 0, "including_start": True, "limit": 0}})),
            3)

        # should return all entries if limit is 0
        assert_equal(
            len(self.nodes[1].listgovproposalvotes({"proposalId": tx,  "masternode": "all", "cycle": -1, "pagination": {"start": 0, "including_start": False, "limit": 0}})),
            2)

        # should respect filters
        assert_equal(len(self.nodes[1].listgovproposalvotes({"proposalId": tx,  "masternode": mn1, "cycle": -1, "pagination": {"start": 0}})), 0)

        # test non-object RPC arguments
        assert_equal(len(self.nodes[0].listgovproposalvotes(propId, 'all', -1, {"limit": 2})), 2)

        tx1 = self.nodes[0].creategovcfp({"title": "1111",
            "context": context,
            "amount": 50,
            "cycles": 1,
            "payoutAddress": address})
        self.nodes[0].generate(1)
        self.sync_blocks()

        tx2 = self.nodes[0].creategovcfp({"title": "2222",
            "context": context,
            "amount": 50,
            "cycles": 1,
            "payoutAddress": address})
        self.nodes[0].generate(1)
        self.sync_blocks()

        tx3 = self.nodes[0].creategovcfp({"title": "3333",
            "context": context,
            "amount": 50,
            "cycles": 1,
            "payoutAddress": address})
        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].listgovproposals({"cycle":1, "pagination": {"start": cfp1, "including_start": True, "limit": 1}})[0]["proposalId"], cfp1)
        assert_equal(len(self.nodes[0].listgovproposals({"cycle":6})), 3)
        assert_equal(self.nodes[0].listgovproposals({"cycle":6, "pagination": {"start": tx2, "including_start": True, "limit": 1}})[0]["proposalId"], tx2)
        assert_equal(self.nodes[0].listgovproposals({"cycle":6, "pagination": {"start": tx3, "including_start": True, "limit": 1}})[0]["proposalId"], tx3)

        assert_equal(len(self.nodes[0].listgovproposals({"type": "cfp"})), 5)
        assert_equal(self.nodes[0].listgovproposals({"type": "cfp", "pagination": {"start": cfp1, "including_start": True, "limit": 1}})[0]["proposalId"], cfp1)
        assert_equal(self.nodes[0].listgovproposals({"type": "cfp", "pagination": {"start": tx2, "including_start": True, "limit": 1}})[0]["proposalId"], tx2)

        assert_equal(len(self.nodes[0].listgovproposals({"status": "voting"})), 3)
        assert_equal(self.nodes[0].listgovproposals({"status": "voting", "pagination": {"start": tx1, "including_start": True, "limit": 1}})[0]["proposalId"], tx1)
        assert_equal(self.nodes[0].listgovproposals({"status": "voting", "pagination": {"start": tx3, "including_start": True, "limit": 1}})[0]["proposalId"], tx3)

        allProposals = self.nodes[0].listgovproposals({"status": "voting"})
        nextProposal = []
        for i in range(len(allProposals)):
            if allProposals[i]["proposalId"] == tx1:
                if i < len(allProposals) - 1:
                    nextProposal = [allProposals[i + 1]]
                # otherwise tx1 is the last proposal
                break

        assert_equal(self.nodes[0].listgovproposals(
            {"status": "voting", "pagination": {"start": tx1, "including_start": False, "limit": 1}}), nextProposal)

        self.test_aggregation(propId)
        self.test_default_cycles_fix()
        self.aggregate_all_votes()
        self.test_valid_votes()
        self.test_empty_object()

    def test_aggregation(self, propId):
        """
        Tests vote aggregation for a specific proposal. It should respect all provided filters.
        """
        votes = self.nodes[0].listgovproposalvotes(propId, 'all', -1, {})
        totalVotes = len(votes)
        yesVotes = len([x for x in votes if x["vote"] == "YES"])
        noVotes = len([x for x in votes if x["vote"] == "NO"])
        neutralVotes = len([x for x in votes if x["vote"] == "NEUTRAL"])

        votes_aggregate = self.nodes[0].listgovproposalvotes(propId, 'all', -1, {}, True)[0]
        assert_equal(votes_aggregate["proposalId"], propId)
        assert_equal(votes_aggregate["total"], totalVotes)
        assert_equal(votes_aggregate["yes"], yesVotes)
        assert_equal(votes_aggregate["neutral"], neutralVotes)
        assert_equal(votes_aggregate["no"], noVotes)

    def test_default_cycles_fix(self):
        """
        Tests fix for an issue for when the cycles argument is not provided, the
        votes for cycle 1 are returned instead of the latest cycle.
        https://github.com/DeFiCh/ain/pull/1701
        """
        tx1 = self.nodes[0].creategovcfp({"title": "1111",
                                          "context": "<Git issue url>",
                                          "amount": 50,
                                          "cycles": 2,
                                          "payoutAddress": self.nodes[0].getnewaddress()})
        self.nodes[0].generate(1)
        self.sync_blocks()

        endHeight = self.nodes[0].getgovproposal(tx1)["cycleEndHeight"]
        self.proposalId = self.nodes[0].getgovproposal(tx1)["proposalId"]

        # cycle 1 votes
        for mn in range(len(self.mns)):
            self.nodes[mn].votegov(self.proposalId, self.mns[mn], "yes")
            self.nodes[mn].generate(1)
            self.sync_blocks()

        # should show cycle 1 votes
        votes = self.nodes[0].listgovproposalvotes(self.proposalId, 'all')
        for vote in votes:
            assert_equal(vote["vote"], "YES")  # there are only YES votes in cycle 1

        # move to next cycle
        self.nodes[0].generate(endHeight + 1 - self.nodes[0].getblockcount())

        # cycle 2 votes
        for mn in range(len(self.mns)):
            self.nodes[mn].votegov(self.proposalId, self.mns[mn], "no")
            self.nodes[mn].generate(1)
            self.sync_blocks()

        votes = self.nodes[0].listgovproposalvotes(self.proposalId, 'all')
        for vote in votes:
            # there are only NO votes in cycle 2, this should fail if cycle defaults to 1
            assert_equal(vote["vote"], "NO")

    def aggregate_all_votes(self):
        """
        Tests aggregation of all latest cycle votes for all proposals
        when no arguments are provided in listgovproposalvotes.
        """
        votes = self.nodes[0].listgovproposalvotes({})
        proposalVotes = self.nodes[0].listgovproposalvotes(self.proposalId, "all", 0, {}, True)
        filteredVotes = list(filter(lambda vote: vote["proposalId"] == self.proposalId, votes))
        assert_equal(filteredVotes, proposalVotes)

        props = self.nodes[0].listgovproposals()
        missing = []
        for prop in props:
            if prop["proposalId"] not in [x["proposalId"] for x in votes]:
                missing.append(prop["proposalId"])

        for miss in missing:
            # proposals missing from entry must have 0 votes in the latest cycle
            assert_equal(len(self.nodes[0].listgovproposalvotes(miss, "all", 0)), 0)

    def test_empty_object(self):
        """
        Tests fix for an issue where providing an empty object would
        cause the node to incorrectly throw an error
        """
        votes = self.nodes[0].listgovproposalvotes()
        votesObj = self.nodes[0].listgovproposalvotes({})
        assert_equal(votes, votesObj)

    def test_valid_votes(self):
        """
        Tests valid votes filter.
        """
        tx1 = self.nodes[0].creategovcfp({"title": "1111",
                                          "context": "<Git issue url>",
                                          "amount": 50,
                                          "cycles": 2,
                                          "payoutAddress": self.nodes[0].getnewaddress()})
        self.nodes[0].generate(1)
        self.sync_blocks()

        endHeight = self.nodes[0].getgovproposal(tx1)["cycleEndHeight"]
        propId = self.nodes[0].getgovproposal(tx1)["proposalId"]

        for mn in range(len(self.mns)):
            self.nodes[mn].votegov(propId, self.mns[mn], "yes")
            self.nodes[mn].generate(1)
            self.sync_blocks()

        self.nodes[2].resignmasternode(self.mns[2])
        self.sync_mempools()
        self.nodes[0].generate(5)
        self.sync_blocks()

        # move to next cycle
        self.nodes[0].generate(endHeight + 1 - self.nodes[0].getblockcount())

        validVotes = self.nodes[0].listgovproposalvotes(propId, "all", 1, {}, False, True)
        invalidVotes = self.nodes[0].listgovproposalvotes(propId, "all", 1, {}, False, False)

        assert(self.mns[2] not in [x["masternodeId"] for x in validVotes])
        assert_equal(self.mns[2], invalidVotes[0]["masternodeId"])

if __name__ == '__main__':
    OnChainGovernanceTest().main()
