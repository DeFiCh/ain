#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify anchors rewards
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, \
    connect_nodes_bi, disconnect_nodes, wait_until

class AnchorRewardsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [
            [ "-dummypos=1", "-spv=1", "-fakespv=1", "-txindex=1", "-anchorquorum=2"],
            [ "-dummypos=1", "-spv=1", "-fakespv=1", "-txindex=1", "-anchorquorum=2"],
            [ "-dummypos=1", "-spv=1", "-fakespv=1", "-txindex=1", "-anchorquorum=2"],
        ]
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        self.sync_all()

    def dumphashes(self, nodes=None, block = None):
        if nodes is None:
            nodes = range(self.num_nodes)
        for i in nodes:
            bl = self.nodes[i].getblockcount() if block is None else block
            print ("Node%d: [%d] %s" % (i, bl, self.nodes[i].getblockhash(bl)))

    def dumpheights(self):
        print ("Heights:", self.nodes[0].getblockcount(), "\t", self.nodes[1].getblockcount(), "\t", self.nodes[2].getblockcount())
        # pass

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

        chain0 = 17+15
        # disconnect_nodes(self.nodes[0], 1)
        self.nodes[0].generate(chain0)
        assert_equal(len(self.nodes[0].spv_listanchors()), 0)

        print ("Node0: Setting anchors")
        self.nodes[0].spv_setlastheight(1)
        rewardAddress0 = self.nodes[0].getnewaddress("", "legacy")
        rewardAddress1 = self.nodes[0].getnewaddress("", "legacy")

        wait_until(lambda: self.authsquorum(15), timeout=10)
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

        # just for triggering activation in regtest
        self.nodes[0].spv_setlastheight(1)
        anchors = self.nodes[0].spv_listanchors()
        assert_equal(len(anchors), 2)
        # print (anchors)
        if anchors[0]['active']:
            activeAnc = anchors[0]
        else:
            activeAnc = anchors[1]

        print ("Confs init:")
        assert_equal(len(self.nodes[0].spv_listanchorconfirms()), 0)
        self.nodes[0].spv_setlastheight(5)
        self.nodes[1].spv_setlastheight(5)
        assert_equal(len(self.nodes[0].spv_listanchorconfirms()), 0)

        print ("Node1: Setting anchors")
        self.nodes[1].spv_setlastheight(1)
        self.nodes[1].spv_sendrawtx(txAnc0['txHex'])
        self.nodes[1].spv_sendrawtx(txAnc1['txHex'])

        # disconnect node2 (BEFORE reward voting!) for future rollback
        disconnect_nodes(self.nodes[1], 2)

        self.nodes[0].spv_setlastheight(6)
        self.nodes[1].spv_setlastheight(6)
        # important to wait here!
        self.sync_blocks(self.nodes[0:2])
        wait_until(lambda: len(self.nodes[0].spv_listanchorconfirms()) == 1 and self.nodes[0].spv_listanchorconfirms()[0]['signers'] == 2, timeout=10)

        conf0 = self.nodes[0].spv_listanchorconfirms()
        print ("Confs created, only active anchor:", conf0)
        assert_equal(len(conf0), 1)
        assert_equal(conf0[0]['anchorHeight'], 15)
        assert_equal(conf0[0]['prevAnchorHeight'], 0)
        assert_equal(conf0[0]['rewardAddress'], activeAnc['rewardAddress'])
        assert_equal(conf0[0]['signers'], 2)

        print ("Generate reward")
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 0)

        self.nodes[0].generate(1)
        # confirms should disappear
        wait_until(lambda: len(self.nodes[0].spv_listanchorconfirms()) == 0, timeout=10)

        # check reward tx
        rew0 = self.nodes[0].spv_listanchorrewards()
        # print ("Rewards0:", rew0)
        assert_equal(len(rew0), 1)
        assert_equal(rew0[0]['AnchorTxHash'], conf0[0]['btcTxHash'])
        rew0tx = self.nodes[0].decoderawtransaction(self.nodes[0].getrawtransaction(rew0[0]['RewardTxHash']))
        # print("Reward tx:", rew0tx)
        assert_equal(rew0tx['vout'][1]['scriptPubKey']['addresses'][0], conf0[0]['rewardAddress'])
        assert_equal(rew0tx['vout'][1]['value'], 0)
        # print(self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount())))

        print ("Rollback!")
        self.nodes[2].generate(2)
        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_all()
        wait_until(lambda: len(self.nodes[0].spv_listanchorconfirms()) == 1, timeout=10) # while rollback, it should appear w/o wait
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 0)
        wait_until(lambda: len(self.nodes[2].spv_listanchorconfirms()) == 1, timeout=10) # but wait here
        assert_equal(len(self.nodes[2].spv_listanchorrewards()), 0)

        print ("Reward again")
        self.nodes[1].generate(1)
        self.sync_all()
        wait_until(lambda: len(self.nodes[0].spv_listanchorconfirms()) == 0, timeout=10)
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 1)

        print ("Generate more (2 unpayed rewards at once)")
        self.nodes[0].spv_setlastheight(6)
        self.nodes[1].spv_setlastheight(6)
        self.nodes[0].generate(60)
        self.sync_all()
        wait_until(lambda: self.authsquorum(75), timeout=10)

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

        self.nodes[0].generate(15)
        self.sync_all()
        wait_until(lambda: self.authsquorum(90), timeout=10)

        rewardAddress3 = self.nodes[0].getnewaddress("", "legacy")
        txAnc3 = self.nodes[0].spv_createanchor([{
            'txid': "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
            'vout': 3,
            'amount': 2262303,
            'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"}],
            rewardAddress3)
        self.nodes[1].spv_sendrawtx(txAnc3['txHex'])

        # for rollback. HERE, to deny cofirmations for node2
        disconnect_nodes(self.nodes[1], 2)

        self.nodes[0].spv_setlastheight(13)
        self.nodes[1].spv_setlastheight(13)
        # important to wait here!
        self.sync_blocks(self.nodes[0:2])
        wait_until(lambda: len(self.nodes[0].spv_listanchorconfirms()) == 2 and self.nodes[0].spv_listanchorconfirms()[0]['signers'] == 2 and self.nodes[0].spv_listanchorconfirms()[1]['signers'] == 2, timeout=10)

        # check confirmations (revoting) after node restart:
        self.stop_node(0)
        self.start_node(0)
        connect_nodes_bi(self.nodes, 0, 1)
        wait_until(lambda: len(self.nodes[0].spv_listanchorconfirms()) == 2 and self.nodes[0].spv_listanchorconfirms()[0]['signers'] == 2 and self.nodes[0].spv_listanchorconfirms()[1]['signers'] == 2, timeout=10)

        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # there is a tricky place here: the rest of confirms should be revoted, but it is very hard to check in regtest due to the same team
        wait_until(lambda: len(self.nodes[0].spv_listanchorconfirms()) == 1 and self.nodes[0].spv_listanchorconfirms()[0]['signers'] == 2, timeout=10)
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 2)
        self.nodes[0].generate(1)
        wait_until(lambda: len(self.nodes[0].spv_listanchorconfirms()) == 0, timeout=10)
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 3)

        # check reward of anc2 value (should be 5)
        rew = self.nodes[0].spv_listanchorrewards()
        for i in rew:
            if i['AnchorTxHash'] == txAnc2['txHash']:
                rew2Hash = i['RewardTxHash']
        rew2tx = self.nodes[0].decoderawtransaction(self.nodes[0].getrawtransaction(rew2Hash))
        assert_equal(rew2tx['vout'][1]['scriptPubKey']['addresses'][0], rewardAddress2)
        assert_equal(rew2tx['vout'][1]['value'], 5)

        print ("Rollback two rewards at once!")
        self.nodes[2].generate(3)
        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_all()
        wait_until(lambda: len(self.nodes[0].spv_listanchorconfirms()) == 2, timeout=10)
        assert_equal(len(self.nodes[0].spv_listanchorrewards()), 1)

if __name__ == '__main__':
    AnchorRewardsTest ().main ()
