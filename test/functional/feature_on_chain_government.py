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
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=101', '-subsidytest=1', '-txindex=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=101', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=101', '-subsidytest=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-eunosheight=80', '-greatworldheight=101', '-subsidytest=1'],
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

        # great world
        assert_equal(self.nodes[0].getblockcount(), 101)

        # Check community dev fund present
        result = self.nodes[0].listcommunitybalances()
        assert_equal(result['CommunityDevelopmentFunds'], Decimal('19.88746400'))
        result = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        assert_equal(result['nonutxo'][0]['CommunityDevelopmentFunds'], Decimal('19.88746400'))

        # Check foundation output no longer in coinbase TX
        raw_tx = self.nodes[0].getrawtransaction(result['tx'][0], 1)
        assert_equal(len(raw_tx['vout']), 2)
        assert_equal(raw_tx['vout'][0]['value'], Decimal('134.99983200'))
        assert_equal(raw_tx['vout'][1]['value'], Decimal('0'))

        # Create address for CFP
        address = self.nodes[0].getnewaddress()

        # Check errors
        assert_raises_rpc_error(-32600, "proposal cycles can be between 1 and 3", self.nodes[0].createcfp, {"title": "test", "amount": 100, "cycles": 4, "payoutAddress": address})
        assert_raises_rpc_error(-32600, "proposal cycles can be between 1 and 3", self.nodes[0].createcfp, {"title": "test", "amount": 100, "cycles": 0, "payoutAddress": address})

        # Check burn empty
        assert_equal(self.nodes[0].getburninfo()['feeburn'], 0)

        # Create CFP
        title = "Create test community fund request proposal"
        tx = self.nodes[0].createcfp({"title": title, "amount": 100, "cycles": 2, "payoutAddress": address})

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
        assert_equal(self.nodes[0].getburninfo()['feeburn'], Decimal('1.00000000'))

        # cannot vote by non owning masternode
        assert_raises_rpc_error(-5, "Incorrect authorization", self.nodes[0].vote, tx, mn1, "yes")

        # Vote on proposal
        self.nodes[0].vote(tx, mn0, "yes")
        self.nodes[0].generate(1)
        self.nodes[1].vote(tx, mn1, "no")
        self.nodes[1].generate(1)
        self.nodes[2].vote(tx, mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Try and vote with non-staked MN
        assert_raises_rpc_error(None, "does not mine at least one block", self.nodes[3].vote, tx, mn3, "neutral")

        # Calculate cycle
        cycle1 = 102 + (102 % 70) + 70
        finalHeight = cycle1 + (cycle1 % 70) + 70

        # Check proposal and votes
        result = self.nodes[0].listproposals()
        assert_equal(len(result), 1)
        assert_equal(result[0]["proposalId"], tx)
        assert_equal(result[0]["title"], title)
        assert_equal(result[0]["type"], "CommunityFundProposal")
        assert_equal(result[0]["status"], "Voting")
        assert_equal(result[0]["amount"], Decimal("100"))
        assert_equal(result[0]["nextCycle"], 1)
        assert_equal(result[0]["totalCycles"], 2)
        assert_equal(result[0]["payoutAddress"], address)
        assert_equal(result[0]["finalizeAfter"], finalHeight)

        # Check individual MN votes
        results = self.nodes[1].listvotes(tx, mn0)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        results = self.nodes[1].listvotes(tx, mn1)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'NO')

        results = self.nodes[1].listvotes(tx, mn2)
        assert_equal(len(results), 1)
        result = results[0]
        assert_equal(result['vote'], 'YES')

        # Check total votes
        result = self.nodes[1].listvotes(tx, "all")
        assert_equal(len(result), 3)

        # Move to just before cycle payout
        self.nodes[0].generate(cycle1 - self.nodes[0].getblockcount() - 1)
        self.sync_blocks()
        bal = self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds']
        assert_equal(self.nodes[1].getaccount(address), [])

        # Move to cycle payout
        self.nodes[0].generate(1)
        self.sync_blocks()

        # CommunityDevelopmentFunds is charged by proposal
        assert_equal(self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds'], bal + Decimal("19.887464") - Decimal(100))

        # payout address
        assert_equal(self.nodes[1].getaccount(address), ['100.00000000@DFI'])
        result = self.nodes[0].listproposals()[0]
        assert_equal(result["status"], "Voting")
        assert_equal(result["nextCycle"], 2)

        # Move to just before final height
        self.nodes[0].generate(finalHeight - self.nodes[0].getblockcount() - 1)
        bal = self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds']

        # Move to final height
        self.nodes[0].generate(1)
        self.sync_blocks()

        # payout address isn't changed
        assert_equal(self.nodes[1].getaccount(address), ['100.00000000@DFI'])

        # proposal fails, CommunityDevelopmentFunds does not charged
        assert_equal(self.nodes[0].listcommunitybalances()['CommunityDevelopmentFunds'], bal + Decimal("19.55772984"))

        # not votes on 2nd cycle makes proposal to rejected
        result = self.nodes[0].listproposals()[0]
        assert_equal(result["status"], "Rejected")

        # No proposals pending
        assert_equal(self.nodes[0].listproposals("all", "voting"), [])
        assert_equal(self.nodes[0].listproposals("all", "completed"), [])

        # Test Vote of Confidence
        title = "Create vote of confidence"
        tx = self.nodes[0].createvoc(title)
        raw_tx = self.nodes[0].getrawtransaction(tx)
        self.nodes[3].sendrawtransaction(raw_tx)
        self.nodes[3].generate(1)
        self.sync_blocks()

        # Check burn fee increment
        assert_equal(self.nodes[0].getburninfo()['feeburn'], Decimal('6.00000000'))

        # Cast votes
        self.nodes[0].vote(tx, mn0, "yes")
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[1].vote(tx, mn1, "no")
        self.nodes[1].generate(1)
        self.sync_blocks()

        self.nodes[2].vote(tx, mn2, "yes")
        self.nodes[2].generate(1)
        self.sync_blocks()

        self.nodes[3].vote(tx, mn3, "yes")
        self.nodes[3].generate(1)
        self.sync_blocks()

        # Check results
        result = self.nodes[0].getproposal(tx)
        assert_equal(result["proposalId"], tx)
        assert_equal(result["title"], title)
        assert_equal(result["type"], "VoteOfConfidence")
        assert_equal(result["status"], "Approved")
        assert_equal(result["approval"], "75.00 of 66.67%")

if __name__ == '__main__':
    ChainGornmentTest().main ()
