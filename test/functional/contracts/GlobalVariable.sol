// SPDX-License-Identifier: GPL-3.0
// https://docs.soliditylang.org/en/v0.8.19/units-and-global-variables.html#block-and-transaction-properties

pragma solidity >=0.7.0 <0.9.0;

contract GlobalVariable {
    address owner;
    int public count = 45;

    constructor() {
        owner = msg.sender;
    }

    function mul(uint a, uint b) public pure returns (uint) {
        return a * b;
    }

    // hash of the given block - only works for 256 most recent, excluding current, blocks
    function blockHash(uint number) public view returns (bytes32) {
        return blockhash(number);
    }

    // current block base fee (EIP-3198 and EIP-1559)
    function baseFee() public view returns (uint) {
        return block.basefee;
    }

    // current chain id
    function chainId() public view returns (uint) {
        return block.chainid;
    }

    // current block miner's address
    function coinbase() public payable returns (address) {
        return block.coinbase;
    }

    // current block difficulty
    function difficulty() public view returns (uint) {
        return block.difficulty; // replaced by prevrandao (EIP-4399)
    }

    // current block gaslimit
    function gasLimit() public view returns (uint) {
        return block.gaslimit;
    }

    // current block number
    function blockNumber() public view returns (uint) {
        return block.number;
    }

    // Current block timestamp as seconds since unix epoch
    function timestamp() public view returns (uint) {
        return block.timestamp;
    }

    // Remaining gas
    function gasLeft() public view returns (uint256) {
        return gasleft();
    }

    // sender of the message (current call)
    function getSender() public view returns (address) {
        return owner;
    }

    // number of wei sent with the message
    function getValue() public payable returns (uint256) {
        return uint256(msg.value);
    }

    // complete calldata
    function getData() public pure returns (bytes memory) {
        return msg.data;
    }

    // first four bytes of the calldata (i.e. function identifier)
    function getSig() public pure returns (bytes4) {
        return msg.sig;
    }

    // gas price of the transaction
    function getTxGasPrice() public view returns (uint256) {
        return tx.gasprice;
    }

    // sender of the transaction (full call chain)
    function getTxOrigin() public view returns (address) {
        return tx.origin;
    }
}
