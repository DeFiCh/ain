// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract NonPayableFallback {
    uint public count;

    constructor() {
        count = 0;
    }

    fallback() external {
        count += 1;
    }

    function getCount() external view returns (uint) {
        return count;
    }
}
