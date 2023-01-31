#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify anchors rewards
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, connect_nodes_bi, \
    disconnect_nodes, wait_until, assert_raises_rpc_error

from decimal import Decimal
import time

class AnchorRewardsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [
            [ "-dummypos=1", "-spv=1", '-amkheight=0', "-anchorquorum=2", "-dakotaheight=1", "-fortcanningheight=1"],
            [ "-dummypos=1", "-spv=1", '-amkheight=0', "-anchorquorum=2", "-dakotaheight=1", "-fortcanningheight=1"],
            [ "-dummypos=1", "-spv=1", '-amkheight=0', "-anchorquorum=2", "-dakotaheight=1", "-fortcanningheight=1"],
        ]
        self.setup_clean_chain = True

    def mock_time(self, offset_hours, hours = 0):
        for i in range(0, self.num_nodes):
            self.nodes[i % self.num_nodes].set_mocktime(int((time.time() - offset_hours * 60 * 60) + (hours * 60 * 60)))
            self.nodes[i % self.num_nodes].setmocktime(int((time.time() - offset_hours * 60 * 60)  + (hours * 60 * 60)))

    # Masternodes have to mint blocks in the last 2 weeks to be valid for
    # anchor teams, function here mines on all available nodes in turn.
    def initmasternodesforanchors(self, offset_hours, blocks):
        # Change node time time first.
        self.mock_time(offset_hours)

        for i in range(0, blocks):
            block_count = self.nodes[i % self.num_nodes].getblockcount()

            # Make sure that generate successfully ioncrements the chain by one.
            while self.nodes[i % self.num_nodes].getblockcount() < block_count + 1:
                self.nodes[i % self.num_nodes].generate(1)

            # Make sure all nodes agree before creating the next block.
            self.sync_blocks()

    # Mine up to height.
    def mine_diff(self, height):
        if self.nodes[0].getblockcount() < height:
            self.nodes[0].generate(height - self.nodes[0].getblockcount())
            self.sync_blocks()

    # Tiem is hours to move node time forward and blocks the number of blocks
    # to mine in each hour. Offset allows us to put check clock back.
    def rotateandgenerate(self, hours, offset_hours, blocks):
        for increment in range(1, hours + 1):
            # Change node time time first.
            self.mock_time(offset_hours, increment)

            block_count = self.nodes[0].getblockcount()

            # Make sure that generate successfully ioncrements the chain by blocks
            while self.nodes[0].getblockcount() < block_count + blocks:
                self.nodes[0].generate(1)

            self.sync_blocks()

    def setlastheight(self, height):
        for i in range(0, self.num_nodes):
            self.nodes[int(i % self.num_nodes)].spv_setlastheight(height)

    def authsquorum(self, height, node=None):
        QUORUM = 2
        if node is None:
            node = 0
        auths = self.nodes[node].spv_listanchorauths()
        for auth in auths:
            if auth['blockHeight'] == height and auth['signers'] >= QUORUM:
                return True
        return False

    def run_test(self):
        assert_equal(len(self.nodes[0].listmasternodes()), 8)

        anchorFrequency = 15

        # Create multiple active MNs
        self.initmasternodesforanchors(13, 1 * anchorFrequency)

        wait_until(lambda: len(self.nodes[0].getanchorteams()['auth']) == 3 and len(self.nodes[0].getanchorteams()['confirm']) == 3, timeout=10)

        # Mo anchors created yet as we need three hours depth in chain
        assert_equal(len(self.nodes[0].spv_listanchorauths()), 0)

        # Mine forward 4 hours, from 12 hours ago, 5 blocks an hour
        self.rotateandgenerate(3, 12, 5)

        # Mine up to block 60
        self.mine_diff(60)

        # Anchor data
        print(self.nodes[0].spv_listanchorauths())
        print(self.nodes[0].getblockcount())
        wait_until(lambda: len(self.nodes[0].spv_listanchorauths()) > 0 and self.nodes[0].spv_listanchorauths()[0]['signers'] == 3, timeout=10)

        auth = self.nodes[0].spv_listanchorauths()
        creation_height = auth[0]['creationHeight']
        assert_equal(auth[0]['blockHeight'], 15)

        hash15 = self.nodes[0].getblockhash(15)
        hash_creation = self.nodes[0].getblockhash(creation_height)
        block15 = self.nodes[0].getblock(hash15)
        block_creation = self.nodes[0].getblock(hash_creation)

        # Check the time
        time_diff = block_creation['time'] - block15['time']
        assert(time_diff > 3 * 60 * 60)

        self.nodes[0].spv_setlastheight(1)
        self.nodes[1].spv_setlastheight(1)

        # Check errors
        assert_raises_rpc_error(None, "Not enough money", self.nodes[1].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 1000,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        # Check some params:
        assert_raises_rpc_error(None, "Expected type array, got object", self.nodes[1].spv_createanchor,
            {
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }, "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "txid must be of length 64", self.nodes[1].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963aa",
                'vout': 3,
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "value is not an integer", self.nodes[1].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': "aa",
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "Can't parse WIF privkey", self.nodes[1].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 2262303,
                'privkey': "1_cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "does not refer to a P2PKH or P2WPKH address", self.nodes[1].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "__mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "does not refer to a P2PKH or P2WPKH address", self.nodes[1].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "")

        # Test anchor creations
        rewardAddress0 = self.nodes[0].getnewaddress("", "legacy")
        rewardAddress1 = self.nodes[0].getnewaddress("", "legacy")

        txAnc0 = self.nodes[0].spv_createanchor([{
            'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
            'vout': 3,
            'amount': 2262303,
            'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"}],
            rewardAddress0)
        txAnc1 = self.nodes[0].spv_createanchor([{
            'txid': "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            'vout': 3,
            'amount': 2262303,
            'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"}],
            rewardAddress1)
        self.nodes[1].spv_sendrawtx(txAnc0['txHex'])
        self.nodes[1].spv_sendrawtx(txAnc1['txHex'])

        # just for triggering activation in regtest
        self.nodes[0].spv_setlastheight(1)
        self.nodes[1].spv_setlastheight(1)
        pending = self.nodes[0].spv_listanchorspending()
        assert_equal(len(pending), 2)
        assert_equal(pending[0]['btcBlockHeight'], 1)
        assert_equal(pending[0]['defiBlockHeight'], 15)
        if pending[0]['rewardAddress'] == rewardAddress0:
            assert_equal(pending[1]['rewardAddress'], rewardAddress1)
        else:
            assert_equal(pending[0]['rewardAddress'], rewardAddress1)
        assert_equal(pending[0]['confirmations'], 1) # Bitcoin confirmations
        assert_equal(pending[0]['signatures'], 2)
        assert_equal(pending[0]['anchorCreationHeight'], creation_height)

        # Check these are consistent across anchors life
        btcHash0 = pending[0]['btcTxHash']
        btcHash1 = pending[1]['btcTxHash']
        dfiHash = pending[0]['defiBlockHash']

        # Trigger anchor check
        self.nodes[0].generate(1)

        # Get anchors
        anchors = self.nodes[0].spv_listanchors()
        assert_equal(len(anchors), 2)
        assert_equal(anchors[0]['btcBlockHeight'], 1)
        if anchors[0]['btcTxHash'] == btcHash0:
            assert_equal(anchors[1]['btcTxHash'], btcHash1)
        else:
            assert_equal(anchors[0]['btcTxHash'], btcHash1)
        assert_equal(anchors[0]['previousAnchor'], '0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(anchors[0]['defiBlockHeight'], 15)
        assert_equal(anchors[0]['defiBlockHash'], dfiHash)
        if anchors[0]['rewardAddress'] == rewardAddress0:
            assert_equal(anchors[1]['rewardAddress'], rewardAddress1)
        else:
            assert_equal(anchors[0]['rewardAddress'], rewardAddress1)
        assert_equal(anchors[0]['confirmations'], 1) # Bitcoin confirmations
        assert_equal(anchors[0]['signatures'], 2)
        assert_equal(anchors[0]['anchorCreationHeight'], creation_height)
        assert_equal(anchors[0]['active'], False)

        print ("Confs init:")
        assert_equal(len(self.nodes[0].spv_listanchorrewardconfirms()), 0)
        self.nodes[0].spv_setlastheight(5)
        self.nodes[1].spv_setlastheight(5)
        assert_equal(len(self.nodes[0].spv_listanchorrewardconfirms()), 0)

        # Still no active anchor
        anchors = self.nodes[0].spv_listanchors()
        assert_equal(anchors[0]['confirmations'], 5) # Bitcoin confirmations
        assert_equal(anchors[0]['active'], False)

        # important (!) to be synced before disconnection
        # disconnect node2 (BEFORE reward voting!) for future rollback
        disconnect_nodes(self.nodes[1], 2)

        self.nodes[0].spv_setlastheight(6)
        self.nodes[1].spv_setlastheight(6)

        anchors = self.nodes[0].spv_listanchors()
        print(anchors)
        assert_equal(anchors[0]['confirmations'], 6) # Bitcoin confirmations
        if anchors[0]['active']:
            activeAnc = anchors[0]
        else:
            # Make sure this actually is active
            assert_equal(anchors[1]['active'], True)
            activeAnc = anchors[1]

        unrewarded = self.nodes[0].spv_listanchorsunrewarded()
        assert_equal(len(unrewarded), 1)
        assert_equal(unrewarded[0]['btcBlockHeight'], 1)
        if unrewarded[0]['btcTxHash'] != btcHash0:
            assert_equal(unrewarded[0]['btcTxHash'], btcHash1)
        assert_equal(unrewarded[0]['defiBlockHeight'], 15)
        assert_equal(unrewarded[0]['defiBlockHash'], dfiHash)

        # important to wait here!
        self.sync_blocks(self.nodes[0:2])
        wait_until(lambda: len(self.nodes[0].spv_listanchorrewardconfirms()) == 1 and self.nodes[0].spv_listanchorrewardconfirms()[0]['signers'] == 2, timeout=10)

        conf0 = self.nodes[0].spv_listanchorrewardconfirms()
        print ("Confs created, only active anchor")
        assert_equal(len(conf0), 1)
        assert_equal(conf0[0]['anchorHeight'], 15)
        assert_equal(conf0[0]['prevAnchorHeight'], 0)
        assert_equal(conf0[0]['rewardAddress'], activeAnc['rewardAddress'])
        assert_equal(conf0[0]['signers'], 2)

        print ("Generate reward")
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 0)

        # Reward before
        assert_equal(self.nodes[0].listcommunitybalances()['AnchorReward'], Decimal('6.10000000'))

        self.nodes[0].generate(1)

        # Reward after
        assert_equal(self.nodes[0].listcommunitybalances()['AnchorReward'], Decimal('0.10000000'))

        # confirms should disappear
        wait_until(lambda: len(self.nodes[0].spv_listanchorrewardconfirms()) == 0, timeout=10)

        # check reward tx
        rew0 = self.nodes[0].spv_listanchorrewards()

        assert_equal(len(rew0), 1)
        assert_equal(rew0[0]['AnchorTxHash'], conf0[0]['btcTxHash'])
        blockcount = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockcount)
        rew0tx = self.nodes[0].getrawtransaction(rew0[0]['RewardTxHash'], 1, blockhash)

        assert_equal(rew0tx['vout'][1]['scriptPubKey']['addresses'][0], conf0[0]['rewardAddress'])
        assert_equal(rew0tx['vout'][1]['value'], Decimal('6.10000000'))

        # Check data from list transactions
        anchors = self.nodes[0].listanchors()
        assert_equal(anchors[0]['anchorHeight'], 15)
        assert_equal(anchors[0]['anchorHash'], dfiHash)
        assert_equal(anchors[0]['rewardAddress'], conf0[0]['rewardAddress'])
        assert_equal(anchors[0]['dfiRewardHash'], rew0tx['txid'])
        assert_equal(anchors[0]['btcAnchorHeight'], 1)
        if anchors[0]['btcAnchorHash'] != btcHash0:
            assert_equal(anchors[0]['btcAnchorHash'], btcHash1)

        print ("Rollback!")
        self.nodes[2].generate(2)
        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()

        wait_until(lambda: len(self.nodes[0].spv_listanchorrewardconfirms()) == 1, timeout=10) # while rollback, it should appear w/o wait
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 0)
        wait_until(lambda: len(self.nodes[2].spv_listanchorrewardconfirms()) == 1, timeout=10) # but wait here
        assert_equal(len(self.nodes[2].spv_listanchorrewards()), 0)

        print ("Reward again")

        # Reward before
        assert_equal(self.nodes[0].listcommunitybalances()['AnchorReward'], Decimal('6.30000000')) # 2 more blocks on this chain

        self.nodes[1].generate(1)
        self.sync_blocks()

        # Reward after
        assert_equal(self.nodes[0].listcommunitybalances()['AnchorReward'], Decimal('0.10000000'))

        wait_until(lambda: len(self.nodes[0].spv_listanchorrewardconfirms()) == 0, timeout=10)
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 1)

        print ("Generate another reward")
        self.setlastheight(6)

        # Mine forward 6 hours, from 9 hours ago, 5 blocks an hour
        self.rotateandgenerate(6, 9, 5)

        wait_until(lambda: self.authsquorum(60), timeout=10)

        rewardAddress2 = self.nodes[0].getnewaddress("", "legacy")
        txAnc2 = self.nodes[0].spv_createanchor([{
            'txid': "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
            'vout': 3,
            'amount': 2262303,
            'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"}],
            rewardAddress2)
        self.nodes[1].spv_sendrawtx(txAnc2['txHex'])

        self.nodes[0].spv_setlastheight(7)
        self.nodes[1].spv_setlastheight(7)

        # Mine forward 3 hours, from 9 hours ago, 5 blocks an hour
        self.rotateandgenerate(3, 3, 15)

        wait_until(lambda: self.authsquorum(60), timeout=10)

        # for rollback. HERE, to deny cofirmations for node2
        disconnect_nodes(self.nodes[1], 2)

        self.nodes[0].spv_setlastheight(13)
        self.nodes[1].spv_setlastheight(13)

        # important to wait here!
        self.sync_blocks(self.nodes[0:2])

        wait_until(lambda: len(self.nodes[0].spv_listanchorrewardconfirms()) == 1 and self.nodes[0].spv_listanchorrewardconfirms()[0]['signers'] == 2, timeout=10)

        # check confirmations (revoting) after node restart:
        self.stop_node(0)
        self.start_node(0, ['-txindex=1', '-amkheight=0', "-dakotaheight=1"])
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks(self.nodes[0:2])
        wait_until(lambda: len(self.nodes[0].spv_listanchorrewardconfirms()) == 1 and self.nodes[0].spv_listanchorrewardconfirms()[0]['signers'] == 2, timeout=10)

        # Reward before
        assert_equal(self.nodes[1].listcommunitybalances()['AnchorReward'], Decimal('7.60000000')) # 2 more blocks on this chain

        self.nodes[1].generate(1)

        block_height = self.nodes[1].getblockcount()
        block_hash = self.nodes[1].getblockhash(block_height)

        # Reward after
        assert_equal(self.nodes[1].listcommunitybalances()['AnchorReward'], Decimal('0.10000000')) # Subsidy halving!

        self.sync_blocks(self.nodes[0:2])
        wait_until(lambda: len(self.nodes[0].spv_listanchorrewardconfirms()) == 0, timeout=10)

        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 2)

        rew = self.nodes[0].spv_listanchorrewards()
        for i in rew:
            if i['AnchorTxHash'] == txAnc2['txHash']:
                rew2Hash = i['RewardTxHash']
        rew2tx = self.nodes[0].getrawtransaction(rew2Hash, 1, block_hash)
        assert_equal(rew2tx['vout'][1]['scriptPubKey']['addresses'][0], rewardAddress2)
        assert_equal(rew2tx['vout'][1]['value'], Decimal('7.60000000'))

        print ("Rollback a rewards")
        self.nodes[2].generate(3)
        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()
        wait_until(lambda: len(self.nodes[0].spv_listanchorrewardconfirms()) == 1, timeout=10)
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 1)

if __name__ == '__main__':
    AnchorRewardsTest().main()
