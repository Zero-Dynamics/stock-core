// Copyright (c) 2018-2020 The Stock Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STOCK_POS_H
#define STOCK_POS_H

#define COMMUNITY_FUND_AMOUNT 25000000

static const int STAKE_TIMESTAMP_MASK = 15;

double GetDifficulty(const CBlockIndex* blockindex);

double GetPoWMHashPS();

double GetPoSKernelPS();

std::pair<CAmount, std::pair<CAmount, CAmount>> GetStakingCoins();

extern uint64_t nLastCoinStakeSearchInterval;

#endif // STOCK_POS_H
