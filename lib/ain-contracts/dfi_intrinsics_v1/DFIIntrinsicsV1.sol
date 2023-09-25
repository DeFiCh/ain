// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "./IDFIIntrinsicsV1.sol";

contract DFIIntrinsicsV1 is IDFIIntrinsicsV1 {
    uint256 private _version;
    uint256 private _evmBlockCount;
    uint256 private _dvmBlockCount;

    function version() public view returns(uint256) {
        return _version;
    }

    function evmBlockCount() public view returns(uint256) {
        return _evmBlockCount;
    }

    function dvmBlockCount() public view returns(uint256) {
        return _dvmBlockCount;
    }
}
