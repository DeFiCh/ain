// SPDX-License-Identifier: MIT

pragma solidity >=0.7.0 <0.9.0;

import "test/functional/contracts/Counter.sol";

contract CounterCaller {
    Counter private c;

    constructor(address addr) {
        c = Counter(addr);
    }

    function incr() external {
        c.incr();
    }

    function getCount() external view returns (uint256) {
        return c.getCount();
    }

    // function inspectSender() public view returns (address) {
    //     return msg.sender;
    // }

    // function inspectOrigin() public view returns (address) {
    //     return tx.origin;
    // }

    // function inspectCounterSender() public view returns (address) {
    //     return c.inspectSender();
    // }

    // function inspectCounterOrigin() public view returns (address) {
    //     return c.inspectOrigin();
    // }
}
