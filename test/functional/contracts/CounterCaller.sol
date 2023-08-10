// SPDX-License-Identifier: MIT

pragma solidity >=0.7.0 <0.9.0;

import "test/functional/contracts/Counter.sol";

contract CounterCaller {
    Counter public c;

    constructor(address counterAddress) {
        c = Counter(counterAddress);
    }

    function incr() external {
        c.incr();
    }

    function getCount() external view returns (uint256) {
        return c.getCount();
    }
}
