#!/usr/bin/env python3
# Copyright (c) 2021 The DeFi Blockchain developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
from decimal import Decimal

class TestForcedRewardAddress(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-greatworldheight=110'],
            ['-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-greatworldheight=110'],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    @staticmethod
    def list_unspent_tx(node, address):
        result = []
        vals = node.listunspent()
        for i in range(0, len(vals)):
            if vals[i]['address'] == address:
                result.append(vals[i])
        return result

    @staticmethod
    def unspent_amount(node, address):
        result = 0
        vals = node.listunspent()
        for i in range(0, len(vals)):
            if vals[i]['address'] == address:
                result += vals[i]['amount']
        return result

    def fund_tx(self, address, amount):
        missing_auth_tx = self.nodes[0].sendtoaddress(address, amount)
        count, missing_input_vout = 0, 0
        for vout in self.nodes[0].getrawtransaction(missing_auth_tx, 1)['vout']:
            if vout['scriptPubKey']['addresses'][0] == address:
                missing_input_vout = count
                break
            count += 1
        self.nodes[0].generate(1)
        return missing_auth_tx, missing_input_vout

    def transfer_owner(self, mn_id):
        # Get current collateral
        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        if (result['collateralTx'] == "0000000000000000000000000000000000000000000000000000000000000000"):
            collateral = mn_id
        else:
            collateral = result['collateralTx']
        owner = result['ownerAuthAddress']

        # Create new owner address
        new_owner = self.nodes[0].getnewaddress("", "legacy")

        # Test update of owner address
        mn_transfer_tx = self.nodes[0].updatemasternode(mn_id, {'ownerAddress':new_owner})
        mn_transfer_rawtx = self.nodes[0].getrawtransaction(mn_transfer_tx, 1)
        self.nodes[0].generate(1)

        # Make sure new collateral is present
        assert_equal(mn_transfer_rawtx['vout'][1]['value'], Decimal('10.00000000'))
        assert_equal(mn_transfer_rawtx['vout'][1]['scriptPubKey']['hex'], self.nodes[0].getaddressinfo(new_owner)['scriptPubKey'])

        # Test spending of new collateral
        rawtx = self.nodes[0].createrawtransaction([{"txid":mn_transfer_tx, "vout":1}], [{new_owner:9.9999}])
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(rawtx)
        assert_raises_rpc_error(-26, "tried to spend locked collateral for {}".format(mn_transfer_tx), self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

        # Make sure old collateral is set as input
        found = False
        for vin in mn_transfer_rawtx['vin']:
            if vin['txid'] == collateral and vin['vout'] == 1:
                found = True
        assert(found)

        # Check new state is TRANSFERRING and owner is still the same
        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['state'], 'TRANSFERRING')
        assert_equal(result['collateralTx'], mn_transfer_tx)
        assert_equal(result['ownerAuthAddress'], owner)

        # Test update while masternode is in TRANSFERRING state
        assert_raises_rpc_error(-32600, "Masternode {} is not in 'ENABLED' state".format(mn_id), self.nodes[0].updatemasternode, mn_id, {'ownerAddress':new_owner})

        # Test PRE_ENABLED state and owner change
        self.nodes[0].generate(10)
        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['state'], 'PRE_ENABLED')
        assert_equal(result['collateralTx'], mn_transfer_tx)
        assert_equal(result['ownerAuthAddress'], new_owner)

        # Try another transfer during pre-enabled
        assert_raises_rpc_error(-32600, "Masternode {} is not in 'ENABLED' state".format(mn_id), self.nodes[0].updatemasternode, mn_id, {'ownerAddress':new_owner})

        # Test ENABLED state and owner change
        self.nodes[0].generate(10)
        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['state'], 'ENABLED')
        assert_equal(result['collateralTx'], mn_transfer_tx)
        assert_equal(result['ownerAuthAddress'], new_owner)

    def run_test(self):
        self.nodes[0].generate(105)
        self.sync_all([self.nodes[0], self.nodes[1]])

        num_mns = len(self.nodes[0].listmasternodes())
        mn_owner = self.nodes[0].getnewaddress("", "legacy")
        mn_owner2 = self.nodes[0].getnewaddress("", "legacy")

        mn_id = self.nodes[0].createmasternode(mn_owner)
        mn_id2 = self.nodes[0].createmasternode(mn_owner2)
        self.nodes[0].generate(1)

        assert_equal(len(self.nodes[0].listmasternodes()), num_mns + 2)
        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['collateralTx'], "0000000000000000000000000000000000000000000000000000000000000000")
        assert_equal(result['rewardAddress'], '')
        assert_equal(result['ownerAuthAddress'], mn_owner)
        assert_equal(result['operatorAuthAddress'], mn_owner)

        # Test call before for height
        operator_address = self.nodes[0].getnewaddress("", "legacy")
        assert_raises_rpc_error(-32600, "called before GreatWorld height".format(mn_id), self.nodes[0].updatemasternode, mn_id, {'operatorAddress':operator_address})

        self.nodes[0].generate(4)

        # Test call before masternode active
        operator_address = self.nodes[0].getnewaddress("", "legacy")
        assert_raises_rpc_error(-32600, "Masternode {} is not in 'ENABLED' state".format(mn_id), self.nodes[0].updatemasternode, mn_id, {'operatorAddress':operator_address})

        self.nodes[0].generate(5)

        assert_raises_rpc_error(-32600, "Masternode with that operator address already exists", self.nodes[0].updatemasternode, mn_id, {'operatorAddress':mn_owner})

        # node 1 try to update node 0 which should be rejected.
        assert_raises_rpc_error(-5, "Incorrect authorization for {}".format(mn_owner), self.nodes[1].updatemasternode, mn_id, {'operatorAddress':operator_address})

        # Update operator address
        self.nodes[0].updatemasternode(mn_id, {'operatorAddress':operator_address})

        # Test updating another node to the same address
        assert_raises_rpc_error(-26, "Masternode with that operator address already exists", self.nodes[0].updatemasternode, mn_id2, {'operatorAddress':operator_address})
        self.nodes[0].resignmasternode(mn_id2)
        self.nodes[0].generate(1)
        self.sync_all()

        assert_equal(self.nodes[1].listmasternodes()[mn_id]["operatorAuthAddress"], operator_address)

        # Set forced address
        forced_reward_address = self.nodes[0].getnewaddress("", "legacy")
        assert_raises_rpc_error(-8,
            "The masternode {} does not exist".format("some_bad_mn_id"),
            self.nodes[0].updatemasternode, "some_bad_mn_id", {'rewardAddress':forced_reward_address}
        )
        assert_raises_rpc_error(-8,
            "rewardAddress ({}) does not refer to a P2PKH or P2WPKH address".format("some_bad_address"),
            self.nodes[0].updatemasternode, mn_id, {'rewardAddress':'some_bad_address'}
        )

        self.nodes[0].updatemasternode(mn_id, {'rewardAddress':forced_reward_address})
        self.nodes[0].generate(1)

        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['rewardAddress'], forced_reward_address)
        assert_equal(result['ownerAuthAddress'], mn_owner)
        assert_equal(result['operatorAuthAddress'], operator_address)

        self.nodes[0].updatemasternode(mn_id, {'rewardAddress':''})
        self.nodes[0].generate(1)

        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['rewardAddress'], '')
        assert_equal(result['ownerAuthAddress'], mn_owner)
        assert_equal(result['operatorAuthAddress'], operator_address)

        self.nodes[0].updatemasternode(mn_id, {'rewardAddress':forced_reward_address})
        self.nodes[0].generate(1)

        fra_amount = self.unspent_amount(self.nodes[0], forced_reward_address)
        fra_unspent = self.list_unspent_tx(self.nodes[0], forced_reward_address)
        assert_equal(len(fra_unspent), 0)
        assert_equal(fra_amount, 0)

        self.stop_node(1)
        self.restart_node(0, ['-gen', '-masternode_operator='+operator_address, '-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-greatworldheight=1'])

        # Mine blocks
        self.nodes[0].generate(300)

        self.nodes[0].updatemasternode(mn_id, {'rewardAddress':''})
        self.nodes[0].generate(1)

        assert(len(self.list_unspent_tx(self.nodes[0], forced_reward_address)) > len(fra_unspent))
        assert(self.unspent_amount(self.nodes[0], forced_reward_address) > fra_amount)

        # CLI Reward address for test -rewardaddress
        cli_reward_address = self.nodes[0].getnewaddress("", "legacy")

        self.restart_node(0, ['-gen', '-masternode_operator='+operator_address, '-rewardaddress='+cli_reward_address, '-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-greatworldheight=1'])

        cra_unspent = self.list_unspent_tx(self.nodes[0], cli_reward_address)
        cra_amount = self.unspent_amount(self.nodes[0], cli_reward_address)
        assert_equal(len(cra_unspent), 0)
        assert_equal(cra_amount, 0)

        # Mine blocks
        self.nodes[0].generate(400)

        assert(len(self.list_unspent_tx(self.nodes[0], cli_reward_address)) > len(fra_unspent))
        assert(self.unspent_amount(self.nodes[0], cli_reward_address) > fra_amount)

        # Test updating operator and reward address simultaniously
        new_operator_address = self.nodes[0].getnewaddress("", "legacy")
        new_reward_address = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].updatemasternode(mn_id, {'operatorAddress':new_operator_address,'rewardAddress':new_reward_address})
        self.nodes[0].generate(1)

        # Check results
        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['rewardAddress'], new_reward_address)
        assert_equal(result['ownerAuthAddress'], mn_owner)
        assert_equal(result['operatorAuthAddress'], new_operator_address)

        # Test empty argument
        try:
            self.nodes[0].updatemasternode(mn_id, {})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No update arguments provided" in errorString)

        # Test unknown update type
        while True:
            address = self.nodes[0].getnewaddress("", "legacy")
            unknown_tx = self.nodes[0].updatemasternode(mn_id, {'rewardAddress':address})
            unknown_rawtx = self.nodes[0].getrawtransaction(unknown_tx)
            self.nodes[0].clearmempool()
            if unknown_rawtx.count('01030114') == 1:
                break

        updated_tx = unknown_rawtx.replace('01030114', '01ff0114')
        self.nodes[0].signrawtransactionwithwallet(updated_tx)

        try:
            self.nodes[0].sendrawtransaction(updated_tx)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Unknown update type provided" in errorString)

        # Test incorrect owner address
        assert_raises_rpc_error(-8,
            "ownerAddress ({}) does not refer to a P2PKH or P2WPKH address".format("some_bad_address"),
            self.nodes[0].updatemasternode, mn_id, {'ownerAddress':'some_bad_address'}
        )

        # Test update of owner address with existing address
        assert_raises_rpc_error(-32600, "Masternode with that owner address already exists", self.nodes[0].updatemasternode, mn_id, {'ownerAddress':mn_owner})

        # Set up input / output tests
        not_collateral = self.nodes[0].getnewaddress("", "legacy")
        owner_address = self.nodes[0].getnewaddress("", "legacy")
        [not_collateral_tx, not_collateral_vout] = self.fund_tx(not_collateral, 10)
        [missing_auth_tx, missing_input_vout] = self.fund_tx(mn_owner, 0.1)
        [owner_auth_tx, owner_auth_vout] = self.fund_tx(owner_address, 0.1)

        # Get TX to use OP_RETURN output
        missing_tx = self.nodes[0].updatemasternode(mn_id, {'ownerAddress':owner_address})
        missing_rawtx = self.nodes[0].getrawtransaction(missing_tx, 1)
        self.nodes[0].clearmempool()

        # Test owner update without collateral input
        rawtx = self.nodes[0].createrawtransaction([{"txid":missing_auth_tx, "vout":missing_input_vout},{"txid":not_collateral_tx, "vout":not_collateral_vout}], [{"data":missing_rawtx['vout'][0]['scriptPubKey']['hex'][4:]},{owner_address:10}])
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(rawtx)
        assert_raises_rpc_error(-26, "Missing previous collateral from transaction inputs", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

        # Test incorrect new collateral amount
        rawtx = self.nodes[0].createrawtransaction([{"txid":mn_id, "vout":1},{"txid":missing_auth_tx, "vout":missing_input_vout},{"txid":owner_auth_tx, "vout":owner_auth_vout}], [{"data":missing_rawtx['vout'][0]['scriptPubKey']['hex'][4:]},{owner_address:10.1}])
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(rawtx)
        assert_raises_rpc_error(-26, "Incorrect collateral amount", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

        # Test missing new owner auth
        rawtx = self.nodes[0].createrawtransaction([{"txid":mn_id, "vout":1},{"txid":missing_auth_tx, "vout":missing_input_vout}], [{"data":missing_rawtx['vout'][0]['scriptPubKey']['hex'][4:]},{owner_address:10}])
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(rawtx)
        assert_raises_rpc_error(-26, "Missing auth input for new masternode owner", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

        # Test transfer of owner
        self.transfer_owner(mn_id)

        # Test second transfer of MN owner
        self.transfer_owner(mn_id)

        # Test resigning MN with transferred collateral
        self.nodes[0].resignmasternode(mn_id)
        self.nodes[0].generate(1)
        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['state'], 'PRE_RESIGNED')

        # Roll back resignation
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['state'], 'ENABLED')

        # Check MN resigned
        self.nodes[0].generate(11)
        result = self.nodes[0].getmasternode(mn_id)[mn_id]
        assert_equal(result['state'], 'RESIGNED')

        # Test spending of transferred collateral after resignation
        rawtx = self.nodes[0].createrawtransaction([{"txid":result['collateralTx'], "vout":1}], [{owner_address:9.9999}])
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(rawtx)
        self.nodes[0].sendrawtransaction(signed_rawtx['hex'])

        # Set up for multiple MN owner transfer
        mn1_owner = self.nodes[0].getnewaddress("", "legacy")
        mn2_owner = self.nodes[0].getnewaddress("", "legacy")
        mn3_owner = self.nodes[0].getnewaddress("", "legacy")
        mn4_owner = self.nodes[0].getnewaddress("", "legacy")
        mn5_owner = self.nodes[0].getnewaddress("", "legacy")
        mn6_owner = self.nodes[0].getnewaddress("", "legacy")

        mn1 = self.nodes[0].createmasternode(mn1_owner)
        self.nodes[0].generate(1)
        mn2 = self.nodes[0].createmasternode(mn2_owner)
        self.nodes[0].generate(1)
        mn3 = self.nodes[0].createmasternode(mn3_owner)
        self.nodes[0].generate(1)
        mn4 = self.nodes[0].createmasternode(mn4_owner)
        self.nodes[0].generate(1)
        mn5 = self.nodes[0].createmasternode(mn5_owner)
        self.nodes[0].generate(1)
        mn6 = self.nodes[0].createmasternode(mn6_owner)
        self.nodes[0].generate(11)

        result = self.nodes[0].listmasternodes()
        assert_equal(result[mn1]['state'], 'ENABLED')
        assert_equal(result[mn2]['state'], 'ENABLED')
        assert_equal(result[mn3]['state'], 'ENABLED')
        assert_equal(result[mn4]['state'], 'ENABLED')
        assert_equal(result[mn5]['state'], 'ENABLED')
        assert_equal(result[mn6]['state'], 'ENABLED')

        new_mn1_owner = self.nodes[0].getnewaddress("", "legacy")
        new_mn2_owner = self.nodes[0].getnewaddress("", "legacy")
        new_mn3_owner = self.nodes[0].getnewaddress("", "legacy")
        new_mn4_owner = self.nodes[0].getnewaddress("", "legacy")
        new_mn5_owner = self.nodes[0].getnewaddress("", "legacy")
        new_mn6_owner = self.nodes[0].getnewaddress("", "legacy")

        # Try updating two nodes to the same address
        self.nodes[0].updatemasternode(mn1, {'ownerAddress':new_mn1_owner})
        self.nodes[0].generate(1)
        assert_raises_rpc_error(-32600, "Masternode exist with that owner address pending already", self.nodes[0].updatemasternode, mn2, {'ownerAddress':new_mn1_owner})

        # Test updating several MNs owners in the same block
        self.nodes[0].updatemasternode(mn2, {'ownerAddress':new_mn2_owner})
        self.nodes[0].updatemasternode(mn3, {'ownerAddress':new_mn3_owner})
        self.nodes[0].updatemasternode(mn4, {'ownerAddress':new_mn4_owner})
        self.nodes[0].updatemasternode(mn5, {'ownerAddress':new_mn5_owner})
        self.nodes[0].updatemasternode(mn6, {'ownerAddress':new_mn6_owner})
        self.nodes[0].generate(1)

        result = self.nodes[0].listmasternodes()
        assert_equal(result[mn1]['state'], 'TRANSFERRING')
        assert_equal(result[mn1]['ownerAuthAddress'], mn1_owner)
        assert_equal(result[mn2]['state'], 'TRANSFERRING')
        assert_equal(result[mn2]['ownerAuthAddress'], mn2_owner)
        assert_equal(result[mn3]['state'], 'TRANSFERRING')
        assert_equal(result[mn3]['ownerAuthAddress'], mn3_owner)
        assert_equal(result[mn4]['state'], 'TRANSFERRING')
        assert_equal(result[mn4]['ownerAuthAddress'], mn4_owner)
        assert_equal(result[mn5]['state'], 'TRANSFERRING')
        assert_equal(result[mn5]['ownerAuthAddress'], mn5_owner)
        assert_equal(result[mn6]['state'], 'TRANSFERRING')
        assert_equal(result[mn6]['ownerAuthAddress'], mn6_owner)

        self.nodes[0].generate(10)
        result = self.nodes[0].listmasternodes()
        assert_equal(result[mn1]['state'], 'PRE_ENABLED')
        assert_equal(result[mn1]['ownerAuthAddress'], new_mn1_owner)
        assert_equal(result[mn2]['state'], 'PRE_ENABLED')
        assert_equal(result[mn2]['ownerAuthAddress'], new_mn2_owner)
        assert_equal(result[mn3]['state'], 'PRE_ENABLED')
        assert_equal(result[mn3]['ownerAuthAddress'], new_mn3_owner)
        assert_equal(result[mn4]['state'], 'PRE_ENABLED')
        assert_equal(result[mn4]['ownerAuthAddress'], new_mn4_owner)
        assert_equal(result[mn5]['state'], 'PRE_ENABLED')
        assert_equal(result[mn5]['ownerAuthAddress'], new_mn5_owner)
        assert_equal(result[mn6]['state'], 'PRE_ENABLED')
        assert_equal(result[mn6]['ownerAuthAddress'], new_mn6_owner)

        self.nodes[0].generate(10)
        result = self.nodes[0].listmasternodes()
        assert_equal(result[mn1]['state'], 'ENABLED')
        assert_equal(result[mn1]['ownerAuthAddress'], new_mn1_owner)
        assert_equal(result[mn2]['state'], 'ENABLED')
        assert_equal(result[mn2]['ownerAuthAddress'], new_mn2_owner)
        assert_equal(result[mn3]['state'], 'ENABLED')
        assert_equal(result[mn3]['ownerAuthAddress'], new_mn3_owner)
        assert_equal(result[mn4]['state'], 'ENABLED')
        assert_equal(result[mn4]['ownerAuthAddress'], new_mn4_owner)
        assert_equal(result[mn5]['state'], 'ENABLED')
        assert_equal(result[mn5]['ownerAuthAddress'], new_mn5_owner)
        assert_equal(result[mn6]['state'], 'ENABLED')
        assert_equal(result[mn6]['ownerAuthAddress'], new_mn6_owner)

if __name__ == '__main__':
    TestForcedRewardAddress().main()
