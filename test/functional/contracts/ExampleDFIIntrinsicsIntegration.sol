// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "./IDFIIntrinsicsV1.sol";
import "../dfi_intrinsics_registry/IDFIIntrinsicsRegistry.sol";

contract ExampleDFIIntrinsicsV1Integration {

    address _DFIIntrinsicsRegistry;

    function _getDFIIntrinsicsV1() internal view returns(address) {
        return IDFIIntrinsicsRegistry(_DFIIntrinsicsRegistry).getAddressForVersion(1);
    }
    
    function getVersion() external view returns (uint256) {
        return IDFIIntrinsicsV1(_getDFIIntrinsicsV1()).version();
    }

    function getEvmBlockCount() external view returns (uint256) {
        return IDFIIntrinsicsV1(_getDFIIntrinsicsV1()).evmBlockCount();
    }

    function getDvmBlockCount() external view returns (uint256) {
        return IDFIIntrinsicsV1(_getDFIIntrinsicsV1()).dvmBlockCount();
    }
}