// SPDX-License-Identifier: GPL-3.0

pragma solidity >=0.7.0 <0.9.0;

contract TestEvents {
    uint256 number;

    event NumberStored(uint256 indexed _number, address indexed _caller);

    function store(uint256 num) public {
        number = num;
        emit NumberStored(num, msg.sender);
    }

    function retrieve() public view returns (uint256) {
        return number;
    }
}
