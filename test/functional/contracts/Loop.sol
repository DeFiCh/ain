// SPDX-License-Identifier: GPL-3.0

pragma solidity >=0.7.0 <0.9.0;

contract Loop {
    function loop(uint256 num) public {
        uint256 number = 0;
        while (number < num) {
            number++;
        }
    }
}
