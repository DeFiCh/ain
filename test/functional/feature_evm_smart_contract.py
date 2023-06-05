#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)

# Utility function to assert value of U256 output
def int_to_eth_u256(value):
    """
    Convert a non-negative integer to an Ethereum U256-compatible format.

    The input value is multiplied by a fixed factor of 10^18 (1 ether in wei)
    and represented as a hexadecimal string. This function validates that the
    input is a non-negative integer and checks if the converted value is within
    the range of U256 values (0 to 2^256 - 1). If the input is valid and within
    range, it returns the corresponding U256-compatible hexadecimal representation.

    Args:
        value (int): The non-negative integer to convert.

    Returns:
        str: The U256-compatible hexadecimal representation of the input value.

    Raises:
        ValueError: If the input is not a non-negative integer or if the
                    converted value is outside the U256 range.
    """
    if not isinstance(value, int) or value < 0:
        raise ValueError("Value must be a non-negative integer")

    max_u256_value = 2**256 - 1
    factor = 10**18

    converted_value = value * factor
    if converted_value > max_u256_value:
        raise ValueError(f"Value must be less than or equal to {max_u256_value}")

    return hex(converted_value)

rawTx = "f9044e808502540be400832dc6c08080b903fb608060405234801561001057600080fd5b506103db806100206000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c8063131a06801461003b5780632e64cec114610050575b600080fd5b61004e61004936600461015d565b61006e565b005b6100586100b5565b604051610065919061020e565b60405180910390f35b600061007a82826102e5565b507ffe3101cc3119e1fe29a9c3464a3ff7e98501e65122abab6937026311367dc516816040516100aa919061020e565b60405180910390a150565b6060600080546100c49061025c565b80601f01602080910402602001604051908101604052809291908181526020018280546100f09061025c565b801561013d5780601f106101125761010080835404028352916020019161013d565b820191906000526020600020905b81548152906001019060200180831161012057829003601f168201915b5050505050905090565b634e487b7160e01b600052604160045260246000fd5b60006020828403121561016f57600080fd5b813567ffffffffffffffff8082111561018757600080fd5b818401915084601f83011261019b57600080fd5b8135818111156101ad576101ad610147565b604051601f8201601f19908116603f011681019083821181831017156101d5576101d5610147565b816040528281528760208487010111156101ee57600080fd5b826020860160208301376000928101602001929092525095945050505050565b600060208083528351808285015260005b8181101561023b5785810183015185820160400152820161021f565b506000604082860101526040601f19601f8301168501019250505092915050565b600181811c9082168061027057607f821691505b60208210810361029057634e487b7160e01b600052602260045260246000fd5b50919050565b601f8211156102e057600081815260208120601f850160051c810160208610156102bd5750805b601f850160051c820191505b818110156102dc578281556001016102c9565b5050505b505050565b815167ffffffffffffffff8111156102ff576102ff610147565b6103138161030d845461025c565b84610296565b602080601f83116001811461034857600084156103305750858301515b600019600386901b1c1916600185901b1785556102dc565b600085815260208120601f198616915b8281101561037757888601518255948401946001909101908401610358565b50858210156103955787850151600019600388901b60f8161c191681555b5050505050600190811b0190555056fea2646970667358221220f5c9bb4feb3fa563cfe06a38d411044d98edf92f98726288036607edd71587b564736f6c634300081100332aa04ba71a6cfe81e1fc346a9720140e01bfff57a8712fbdd57394306dadf1668ac6a02407389762aa0e9c5dd4c79dc6c4fcf550eb6309fe3aaa776d45d5a5874b892c"

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1'],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.ethAddress = self.nodes[0].getnewaddress("","eth")
        self.to_address = self.nodes[0].getnewaddress("","eth")

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(-32600, "called before NextNetworkUpgrade height", self.nodes[0].evmtx, self.ethAddress, 0, 21, 21000, self.to_address, 0.1)

        # Move to fork height
        self.nodes[0].generate(4)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

        self.creationAddress = "0xe61a3a6eb316d773c773f4ce757a542f673023c6"
        self.nodes[0].importprivkey("957ac3be2a08afe1fafb55bd3e1d479c4ae6d7bf1c9b2a0dcc5caad6929e6617")

    def test_deploy_smart_contract(self):
        # deploy smart contract
        self.nodes[0].evmrawtx(rawTx)
        self.nodes[0].generate(1)

        # get smart contract address
        blockNumber = self.nodes[0].eth_blockNumber()
        block = self.nodes[0].eth_getBlockByNumber(blockNumber, False)
        smartContractInfoReceipt = self.nodes[0].eth_getTransactionReceipt(block['transactions'][0])
        self.smartContractAddress = smartContractInfoReceipt["contractAddress"]

        code = self.nodes[0].eth_getCode(self.smartContractAddress)
        assert_equal(code, "0x608060405234801561001057600080fd5b50600436106100365760003560e01c8063131a06801461003b5780632e64cec114610050575b600080fd5b61004e61004936600461015d565b61006e565b005b6100586100b5565b604051610065919061020e565b60405180910390f35b600061007a82826102e5565b507ffe3101cc3119e1fe29a9c3464a3ff7e98501e65122abab6937026311367dc516816040516100aa919061020e565b60405180910390a150565b6060600080546100c49061025c565b80601f01602080910402602001604051908101604052809291908181526020018280546100f09061025c565b801561013d5780601f106101125761010080835404028352916020019161013d565b820191906000526020600020905b81548152906001019060200180831161012057829003601f168201915b5050505050905090565b634e487b7160e01b600052604160045260246000fd5b60006020828403121561016f57600080fd5b813567ffffffffffffffff8082111561018757600080fd5b818401915084601f83011261019b57600080fd5b8135818111156101ad576101ad610147565b604051601f8201601f19908116603f011681019083821181831017156101d5576101d5610147565b816040528281528760208487010111156101ee57600080fd5b826020860160208301376000928101602001929092525095945050505050565b600060208083528351808285015260005b8181101561023b5785810183015185820160400152820161021f565b506000604082860101526040601f19601f8301168501019250505092915050565b600181811c9082168061027057607f821691505b60208210810361029057634e487b7160e01b600052602260045260246000fd5b50919050565b601f8211156102e057600081815260208120601f850160051c810160208610156102bd5750805b601f850160051c820191505b818110156102dc578281556001016102c9565b5050505b505050565b815167ffffffffffffffff8111156102ff576102ff610147565b6103138161030d845461025c565b84610296565b602080601f83116001811461034857600084156103305750858301515b600019600386901b1c1916600185901b1785556102dc565b600085815260208120601f198616915b8281101561037757888601518255948401946001909101908401610358565b50858210156103955787850151600019600388901b60f8161c191681555b5050505050600190811b0190555056fea2646970667358221220f5c9bb4feb3fa563cfe06a38d411044d98edf92f98726288036607edd71587b564736f6c63430008110033")

    def test_smart_contract_address_state_balance(self):
        initialBlockNumber = self.nodes[0].eth_blockNumber()

        balance = self.nodes[0].eth_getBalance(self.smartContractAddress)
        assert_equal(balance, int_to_eth_u256(0))

        self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"50@DFI", "domain": 1}, "dst":{"address":self.smartContractAddress, "amount":"50@DFI", "domain": 2}}])
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(self.smartContractAddress, "latest")
        assert_equal(balance, int_to_eth_u256(50))

        balance = self.nodes[0].eth_getBalance(self.smartContractAddress, initialBlockNumber) # Test querying previous block
        assert_equal(balance, int_to_eth_u256(0))

    def test_smart_contract_address_state_storage(self):
        initialBlockNumber = self.nodes[0].eth_blockNumber()

        storage = self.nodes[0].eth_getStorageAt(self.smartContractAddress, "0x0", "latest")
        assert_equal(storage, "0x0000000000000000000000000000000000000000000000000000000000000000")

        # Call smart contract
        callRawTx = "f8ca018502540be400832dc6c094e27a95f0d6fafa131927ac50861a4190f5a9c60b80b864131a06800000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000000d48656c6c6f2c20576f726c6421000000000000000000000000000000000000002aa06e2e1dc55471cbd7ec4b0f38822152b2dfb1c31f9f7bb17f0581e81b7ab1d978a05fef62334fec720c614c138d11b78f855cd0f53ad44b2d86ff1ee68c04cfbb65"
        self.nodes[0].evmrawtx(callRawTx)
        self.nodes[0].generate(1)

        storage = self.nodes[0].eth_getStorageAt(self.smartContractAddress, "0x0", "latest")
        assert_equal(storage, "0x48656c6c6f2c20576f726c64210000000000000000000000000000000000001a")

        storage = self.nodes[0].eth_getStorageAt(self.smartContractAddress, "0x0", initialBlockNumber) # Test querying previous block
        assert_equal(storage, "0x0000000000000000000000000000000000000000000000000000000000000000")

    def run_test(self):
        self.setup()

        self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"100@DFI", "domain": 1}, "dst":{"address":self.creationAddress, "amount":"100@DFI", "domain": 2}}])
        self.nodes[0].generate(1)

        self.test_deploy_smart_contract()

        self.test_smart_contract_address_state_balance()

        self.test_smart_contract_address_state_storage()

if __name__ == '__main__':
    EVMTest().main()
