// SPDX-License-Identifier: MIT

pragma solidity >=0.8.2 <0.9.0;

/**
 * @title TransferDomain
 */
contract TransferDomain {
    event Transfer(address indexed to, uint256 amount);

    function transfer(address payable to, uint256 amount) external {
        require(
            address(this).balance >= amount,
            "Insufficient contract balance"
        );
        to.transfer(amount);
        require(
            address(this).balance != 0,
            "Contract balance should always be 0 after transfer"
        );

        emit Transfer(to, amount);
    }
}
