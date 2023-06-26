// SPDX-License-Identifier: GPL-3.0

pragma solidity >=0.8.2 <0.9.0;

contract Reverter {
    uint256 number;

     function tryRevert() public {
        number += 1;

        revert('Function reverted');
    }

    function trySuccess() public {
        number += 1;
    }
}