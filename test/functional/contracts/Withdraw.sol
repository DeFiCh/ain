// SPDX-License-Identifier: GPL-3.0

pragma solidity >=0.7.0 <0.9.0;

contract Withdraw {

    address public owner;

    constructor () payable {
        owner = msg.sender;
    }

    function withdraw() public {
        // get the amount of DFI stored in this contract
        uint amount = address(this).balance;

        // send all DFI to owner
        (bool success, ) = owner.call{value: amount}("");
        require(success, "Failed to send DFI");
    }

    receive() external payable {}

}   