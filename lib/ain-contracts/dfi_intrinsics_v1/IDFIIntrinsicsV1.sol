// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

interface IDFIIntrinsicsV1 { 
    function version() external view returns (uint256);
    function evmBlockCount() external view returns(uint256);
    function dvmBlockCount() external view returns(uint256);
}
