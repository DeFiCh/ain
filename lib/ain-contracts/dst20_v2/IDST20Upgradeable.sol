// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

interface IDST20Upgradeable {
    event UpgradeResult(
        address indexed newTokenContractAddress,
        uint256 newAmount
    );

    function upgradeToken(uint256 amount) external returns (address, uint256);
}
