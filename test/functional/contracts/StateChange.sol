// SPDX-License-Identifier: GPL-3.0

pragma solidity >=0.7.0 <0.9.0;

contract StateChange {
    mapping(address => bool) public state;
    function changeState(bool a) public {
        state[msg.sender] = a;
    }

    function loop(uint256 num) public {
        uint number = 0;
        require(state[msg.sender]);
        while (number < num) {
            number++;
        }
    }
}