// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

interface IDFIIntrinsicsRegistry {
    function get(uint256 _version) external view returns (address);
}
