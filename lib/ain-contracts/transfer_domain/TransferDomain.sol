// File: @openzeppelin/contracts@4.9.2/token/ERC20/IERC20.sol

// OpenZeppelin Contracts (last updated v4.9.0) (token/ERC20/IERC20.sol)

pragma solidity ^0.8.0;

interface IERC20 {
    function transferFrom(
        address from,
        address to,
        uint256 amount
    ) external returns (bool);
}

// SPDX-License-Identifier: MIT

pragma solidity ^0.8.0;

/**
 * @title TransferDomain
 */
contract TransferDomain {
    event Transfer(address indexed from, address indexed to, uint256 amount);
    event VMTransfer(string vmAddress);

    function transfer(
        address from,
        address payable to,
        uint256 amount,
        string memory vmAddress
    ) external {
        address transferFrom = from;

        if (to != address(this)) {
            require(
                address(this).balance >= amount,
                "Insufficient contract balance"
            );
            to.transfer(amount);

            transferFrom = address(this);
        }

        emit Transfer(transferFrom, to, amount);
        emit VMTransfer(vmAddress);
    }

    /**
     * @dev Returns the name of the token.
     */
    function name() public view virtual returns (string memory) {
        return "TransferDomain";
    }

    /**
     * @dev Returns the symbol of the token, usually a shorter version of the
     * name.
     */
    function symbol() public view virtual returns (string memory) {
        return "XVM";
    }

    /**
     * @dev Returns the number of decimals used to get its user representation.
     * For example, if `decimals` equals `2`, a balance of `505` tokens should
     * be displayed to a user as `5.05` (`505 / 10 ** 2`).
     *
     * Tokens usually opt for a value of 18, imitating the relationship between
     * Ether and Wei. This is the default value returned by this function, unless
     * it's overridden.
     *
     * NOTE: This information is only used for _display_ purposes: it in
     * no way affects any of the arithmetic of the contract, including
     * {IERC20-balanceOf} and {IERC20-transfer}.
     */
    function decimals() public view virtual returns (uint8) {
        return 18;
    }

    function transferDST20(
        address contractAddress,
        address from,
        address payable to,
        uint256 amount,
        string memory vmAddress
    ) external {
        if (to != address(this)) {
            IERC20(contractAddress).transferFrom(from, to, amount);
        }
        emit VMTransfer(vmAddress);
    }
}
