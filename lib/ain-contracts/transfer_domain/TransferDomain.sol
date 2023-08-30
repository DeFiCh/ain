// SPDX-License-Identifier: MIT

pragma solidity >=0.8.2 <0.9.0;

/**
 * @title TransferDomain
 */
contract TransferDomain {
    event Transfer(address indexed to, uint256 amount);

    function transfer(address payable to, uint256 amount) external {
        if (to != address(this)) {
            require(
                address(this).balance >= amount,
                "Insufficient contract balance"
            );
            to.transfer(amount);
        }

        emit Transfer(to, amount);
    }
}
