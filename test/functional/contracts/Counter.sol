// SPDX-License-Identifier: MIT
pragma solidity >=0.7.0 <0.9.0;

contract Counter {
    string public name = 'Counter';
    address public owner;
    uint256 count = 0;

    event echo(string message);

    constructor() {
        owner = msg.sender;
        emit echo('Hello, Counter');
    }

    modifier onlyOwner() {
        require(msg.sender == owner); // validate whether caller is the address of owner
        _; // if true continue process
    }
    
    // computing
    function mul(uint256 a, uint256 b) public pure returns (uint256) {
        return a * b;
    }

    // validation
    function max10(uint256 a) public pure returns (uint256) {
        if (a > 10) revert('Value must not be greater than 10.');
        return a;
    }

    // get state
    function getCount() public view returns (uint256) {
        return count;
    }

    // set state
    function setCount(uint256 _count) public onlyOwner {
        count = _count;
    }
    
    // update state
    function incr() public {
        count += 1;
    }

    // environmental with global vars
    // https://docs.soliditylang.org/en/v0.8.16/units-and-global-variables.html
    function getBlockHash(uint256 number) public view returns (bytes32) {
        return blockhash(number);
    }

    function getCurrentBlock() public view returns (uint256) {
        return block.number;
    }

    function getGasLimit() public view returns (uint256) {
        return block.gaslimit;
    }
}
