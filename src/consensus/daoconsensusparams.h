// Copyright (c) 2018-2020 The Stock Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DAOCONSENSUSPARAMS_H
#define DAOCONSENSUSPARAMS_H

namespace Consensus
{
enum ConsensusParamType
{
    TYPE_NUMBER,
    TYPE_PERCENT,
    TYPE_STOCK,
    TYPE_BOOL,
    TYPE_BLOCK,
    TYPE_CYCLE
};

enum ConsensusParamsPos
{
    CONSENSUS_PARAM_VOTING_CYCLE_LENGTH,

    CONSENSUS_PARAM_CONSULTATION_MIN_SUPPORT,
    CONSENSUS_PARAM_CONSULTATION_ANSWER_MIN_SUPPORT,

    CONSENSUS_PARAM_CONSULTATION_MIN_CYCLES,
    CONSENSUS_PARAM_CONSULTATION_MAX_VOTING_CYCLES,
    CONSENSUS_PARAM_CONSULTATION_MAX_SUPPORT_CYCLES,
    CONSENSUS_PARAM_CONSULTATION_REFLECTION_LENGTH,
    CONSENSUS_PARAM_CONSULTATION_MIN_FEE,

    CONSENSUS_PARAM_CONSULTATION_ANSWER_MIN_FEE,

    CONSENSUS_PARAM_PROPOSAL_MIN_QUORUM,
    CONSENSUS_PARAM_PROPOSAL_MIN_ACCEPT,
    CONSENSUS_PARAM_PROPOSAL_MIN_REJECT,
    CONSENSUS_PARAM_PROPOSAL_MIN_FEE,
    CONSENSUS_PARAM_PROPOSAL_MAX_VOTING_CYCLES,

    CONSENSUS_PARAM_PAYMENT_REQUEST_MIN_QUORUM,
    CONSENSUS_PARAM_PAYMENT_REQUEST_MIN_ACCEPT,
    CONSENSUS_PARAM_PAYMENT_REQUEST_MIN_REJECT,
    CONSENSUS_PARAM_PAYMENT_REQUEST_MIN_FEE,
    CONSENSUS_PARAM_PAYMENT_REQUEST_MAX_VOTING_CYCLES,

    CONSENSUS_PARAM_FUND_SPREAD_ACCUMULATION,
    CONSENSUS_PARAM_FUND_PERCENT_PER_BLOCK,

    CONSENSUS_PARAM_GENERATION_PER_BLOCK,
    CONSENSUS_PARAM_STOCKNS_FEE,
    CONSENSUS_PARAMS_DAO_VOTE_LIGHT_MIN_FEE,
    CONSENSUS_PARAMS_CONFIDENTIAL_TOKENS_ENABLED,
    CONSENSUS_PARAMS_DOTSTOCK_LENGTH,
    CONSENSUS_PARAMS_DOTSTOCK_MAXDATA,
    CONSENSUS_PARAMS_DOTSTOCK_FEE_EXTRADATA,
    MAX_CONSENSUS_PARAMS
};

static std::string sConsensusParamsDesc[Consensus::MAX_CONSENSUS_PARAMS] = {
    "Length in blocks of a voting cycle",

    "Minimum of support needed for starting a range consultation",
    "Minimum of support needed for a consultation answer proposal",

    "Earliest cycle when a consultation can get in confirmation phase",
    "Length in cycles for consultation votings",
    "Maximum of voting cycles for a consultation to gain support",
    "Length in cycles for the reflection phase of consultations",
    "Minimum fee to submit a consultation",

    "Minimum fee to submit a consultation answer proposal",

    "Minimum of quorum for fund proposal votings",
    "Minimum of positive votes for a fund proposal to be accepted",
    "Minimum of negative votes for a fund proposal to be rejected",
    "Minimum fee to submit a fund proposal",
    "Maximum of voting cycles for fund proposal votings",

    "Minimum of quorum for payment request votings",
    "Minimum of positive votes for a payment request to be accepted",
    "Minimum of negative votes for a payment request to be rejected",
    "Minimum fee to submit a payment request",
    "Maximum of voting cycles for payment request votings",

    "Frequency of the fund accumulation transaction",
    "Percentage of generated 0DYNS going to the Fund",

    "Amount of 0DYNS generated per block",
    "Fee for registering a name in DotSTOCK",
    "Minimum fee as a fund contribution to submit a DAO vote using a light wallet",
    "Confidential tokens enabled",
    "Length in blocks of a dotSTOCK registration",
    "Max data in bytes attached to a dotSTOCK name without cost",
    "Fee for attaching extra data to a name"
};

static ConsensusParamType vConsensusParamsType[MAX_CONSENSUS_PARAMS] =
{
    TYPE_BLOCK,

    TYPE_PERCENT,
    TYPE_PERCENT,

    TYPE_CYCLE,
    TYPE_CYCLE,
    TYPE_CYCLE,
    TYPE_CYCLE,
    TYPE_STOCK,

    TYPE_STOCK,

    TYPE_PERCENT,
    TYPE_PERCENT,
    TYPE_PERCENT,
    TYPE_STOCK,
    TYPE_CYCLE,

    TYPE_PERCENT,
    TYPE_PERCENT,
    TYPE_PERCENT,
    TYPE_STOCK,
    TYPE_CYCLE,

    TYPE_NUMBER,
    TYPE_PERCENT,

    TYPE_STOCK,
    TYPE_STOCK,
    TYPE_STOCK,
    TYPE_BOOL,
    TYPE_NUMBER,
    TYPE_NUMBER,
    TYPE_STOCK
};
}

#endif // DAOCONSENSUSPARAMS_H
