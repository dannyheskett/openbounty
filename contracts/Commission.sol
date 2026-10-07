// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract Commission {
    mapping(address => bool) public isOwned;
    mapping(address => bool) public isGarrisoned;
    mapping(address => uint256) public weeklyCommission;

    function calculateWeeklyCommission(address castle) external {
        if (isOwned[castle]) {
            uint256 commission = 100; // base commission for owning a castle
            if (isGarrisoned[castle]) {
                commission += 50; // additional commission for garrisoned castle
            }
            weeklyCommission[castle] = commission;
        }
    }
}
