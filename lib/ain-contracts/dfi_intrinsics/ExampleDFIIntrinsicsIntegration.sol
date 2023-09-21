// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "./IDFIIntrinsicsV1.sol";

contract ExampleDFIIntrinsicsV1Integration {

    address DFIIntrinsicsV1Address;
    
    function getVersion() external view returns (uint256) {
        return IDFIIntrinsicsV1(DFIIntrinsicsV1Address).version();
    }

    function getEvmBlockCount() external view returns (uint256) {
        return IDFIIntrinsicsV1(DFIIntrinsicsV1Address).evmBlockCount();
    }

    function getDvmBlockCount() external view returns (uint256) {
        return IDFIIntrinsicsV1(DFIIntrinsicsV1Address).evmBlockCount();
    }
}