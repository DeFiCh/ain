// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract StressTest {
    function burnGas(uint256 loopCount) external pure {
        uint256 sum = 0;
        for (uint256 i = 0; i < loopCount; i++) {
            sum += i;
        }
    }
}
