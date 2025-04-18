// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <base58.h>
#include <chain.h>
#include <consensus/dao.h>
#include <core_io.h>
#include <init.h>
#include <main.h>
#include <net.h>
#include <netbase.h>
#include <policy/rbf.h>
#include <pos.h>
#include <rpc/server.h>
#include <txdb.h>
#include <timedata.h>
#include <uint256.h>
#include <util.h>
#include <utils/dns_utils.h>
#include <utilmoneystr.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <stdint.h>

#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include <univalue.h>

int64_t nWalletUnlockTime;
int64_t nWalletFirstStakeTime = -1;
static CCriticalSection cs_nWalletUnlockTime;

std::string HelpRequiringPassphrase()
{
    return pwalletMain && pwalletMain->IsCrypted()
            ? "\nRequires wallet passphrase to be set with walletpassphrase call."
            : "";
}

bool EnsureWalletIsAvailable(bool avoidException)
{
    if (!pwalletMain)
    {
        if (!avoidException)
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");
        else
            return false;
    }
    return true;
}

void EnsureWalletIsUnlocked()
{
    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
    if (fWalletUnlockStakingOnly)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Wallet is unlocked for staking or mixing only.");
}

void WalletTxToJSON(const CWalletTx& wtx, UniValue& entry)
{
    int confirms = wtx.GetDepthInMainChain();
    entry.pushKV("confirmations", confirms);
    if (wtx.IsCoinBase() || wtx.IsCoinStake())
        entry.pushKV("generated", true);
    if (confirms > 0)
    {
        entry.pushKV("blockhash", wtx.hashBlock.GetHex());
        entry.pushKV("blockindex", wtx.nIndex);
        if (mapBlockIndex.count(wtx.hashBlock) > 0)
            entry.pushKV("blocktime", mapBlockIndex[wtx.hashBlock]->GetBlockTime());
        else
            entry.pushKV("blocktime", 0);
    } else {
        entry.pushKV("trusted", wtx.IsTrusted());
    }
    uint256 hash = wtx.GetHash();
    entry.pushKV("txid", hash.GetHex());
    UniValue conflicts(UniValue::VARR);
    for(const uint256& conflict: wtx.GetConflicts())
        conflicts.push_back(conflict.GetHex());
    entry.pushKV("walletconflicts", conflicts);
    entry.pushKV("time", wtx.GetTxTime());
    entry.pushKV("timereceived", (int64_t)wtx.nTimeReceived);
    entry.pushKV("strdzeel", wtx.strDZeel);
    // Add opt-in RBF status
    std::string rbfStatus = "no";
    if (confirms <= 0) {
        LOCK(mempool.cs);
        RBFTransactionState rbfState = IsRBFOptIn(wtx, mempool);
        if (rbfState == RBF_TRANSACTIONSTATE_UNKNOWN)
            rbfStatus = "unknown";
        else if (rbfState == RBF_TRANSACTIONSTATE_REPLACEABLE_BIP125)
            rbfStatus = "yes";
    }
    entry.pushKV("bip125-replaceable", rbfStatus);

    for(const std::pair<std::string, std::string>& item: wtx.mapValue)
        entry.pushKV(item.first, item.second);
}

std::string AccountFromValue(const UniValue& value)
{
    std::string strAccount = value.get_str();
    if (strAccount == "*")
        throw JSONRPCError(RPC_WALLET_INVALID_ACCOUNT_NAME, "Invalid account name");
    return strAccount;
}

UniValue createrawscriptaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "createrawscriptaddress \"hex script\"\n"
                "\nReturns the Stock address for the specified raw hex script.\n"
                "\nArguments:\n"
                "1. \"hex script\"        (string) The hex script to encode in the address.\n"
                "\nResult:\n"
                "\"stockaddress\"    (string) The  stock address\n"
                "\nExamples:\n"
                + HelpExampleCli("createrawscriptaddress", "6ac4c5")
                );

    std::string data = params[0].get_str();

    if (!IsHex(data))
        throw JSONRPCError(RPC_MISC_ERROR, "the script is not expressed in hexadecimal");

    std::vector<unsigned char> vData = ParseHex(data);

    CScript script(vData.begin(), vData.end());

    std::string strAsm = ScriptToAsmStr(script);

    if (strAsm.find("[error]") != std::string::npos || strAsm.find("OP_UNKNOWN") != std::string::npos)
        throw JSONRPCError(RPC_MISC_ERROR, "the script includes invalid or unknown op codes");

    CStockAddress address(script);

    if (!address.IsValid())
        throw JSONRPCError(RPC_MISC_ERROR, "the generated address is not valid");

    return address.ToString();
}

UniValue listprivateunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw std::runtime_error(
                "listprivateunspent ( minconf maxconf )\n"
                "\nReturns array of unspent private transaction outputs\n"
                "with between minconf and maxconf (inclusive) confirmations.\n"
                "\nArguments:\n"
                "1. minconf          (numeric, optional, default=1) The minimum confirmations to filter\n"
                "2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
                "\nResult\n"
                "[                   (array of json object)\n"
                "  {\n"
                "    \"txid\" : \"txid\",          (string) the transaction id \n"
                "    \"vout\" : n,                 (numeric) the vout value\n"
                "    \"amount\" : x.xxx,           (numeric) the transaction amount in " + CURRENCY_UNIT + "\n"
                                                                                                           "    \"confirmations\" : n         (numeric) The number of confirmations\n"
                                                                                                           "  }\n"
                                                                                                           "  ,...\n"
                                                                                                           "]\n"

                                                                                                           "\nExamples\n"
                + HelpExampleCli("listprivateunspent", "")
                + HelpExampleCli("listprivateunspent", "6 9999999")
                );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM)(UniValue::VNUM)(UniValue::VARR));

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 1)
        nMaxDepth = params[1].get_int();

    UniValue results(UniValue::VARR);
    std::vector<COutput> vecOutputs;
    assert(pwalletMain != NULL);
    LOCK2(cs_main, pwalletMain->cs_wallet);
    pwalletMain->AvailablePrivateCoins(vecOutputs, false, NULL, true);
    for(const COutput& out: vecOutputs) {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("txid", out.tx->GetHash().GetHex());
        entry.pushKV("address", out.sAddress);
        entry.pushKV("vout", out.i);
        entry.pushKV("amount", ValueFromAmount(out.tx->vAmounts[out.i]));
        entry.pushKV("confirmations", out.nDepth);
        results.push_back(entry);
    }

    return results;
}


UniValue getnewaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw std::runtime_error(
                "getnewaddress ( \"account\" )\n"
                "\nReturns a new Stock address for receiving payments.\n"
                "If 'account' is specified (DEPRECATED), it is added to the address book \n"
                "so payments received with the address will be credited to 'account'.\n"
                "\nArguments:\n"
                "1. \"account\"        (string, optional) DEPRECATED. The account name for the address to be linked to. If not provided, the default account \"\" is used. It can also be set to the empty string \"\" to represent the default account. The account does not need to exist, it will be created if there is no account by the given name.\n"
                "\nResult:\n"
                "\"stockaddress\"    (string) The new stock address\n"
                "\nExamples:\n"
                + HelpExampleCli("getnewaddress", "")
                + HelpExampleRpc("getnewaddress", "")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    std::string strAccount;
    if (params.size() > 0)
        strAccount = AccountFromValue(params[0]);

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!pwalletMain->GetKeyFromPool(newKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    CKeyID keyID = newKey.GetID();

    pwalletMain->SetAddressBook(keyID, strAccount, "receive");

    return CStockAddress(keyID).ToString();
}

UniValue getcoldstakingaddress(const UniValue& params, bool fHelp)
{

    if (fHelp || params.size() < 2)
        throw std::runtime_error(
                "getcoldstakingaddress \"stakingaddress\" \"spendingaddress\" ( \"votingaddress\" )\n"
            "Returns a coldstaking address based on the address inputs\n"
            "Arguments:\n"
            "1. \"stakingaddress\"  (string, required) The stock staking address.\n"
            "2. \"spendingaddress\" (string, required) The stock spending address.\n\n"
            "3. \"voting\"          (string, optional) The stock voting address.\n\n"
            "\nExamples:\n"
            + HelpExampleCli("getcoldstakingaddress", "\"mqyGZvLYfEH27Zk3z6JkwJgB1zpjaEHfiW\" \"mrfjgazyerYxDQHJAPDdUcC3jpmi8WZ2uv\"") +
                "\nAs a json rpc call\n"
            + HelpExampleRpc("getcoldstakingaddress", "\"mqyGZvLYfEH27Zk3z6JkwJgB1zpjaEHfiW\", \"mrfjgazyerYxDQHJAPDdUcC3jpmi8WZ2uv\"")
                );

    if (!IsColdStakingEnabled(chainActive.Tip(),Params().GetConsensus()))
        throw std::runtime_error(
                "Cold Staking is not active yet.");

    if (!IsColdStakingv2Enabled(chainActive.Tip(), Params().GetConsensus()) && params.size() == 3)
        throw std::runtime_error(
                "Cold Staking v2 is not active yet.");

    if (params[0].get_str() == params[1].get_str())
        throw std::runtime_error(
                "The staking address should be different to the spending address"
                );


    CStockAddress stakingAddress(params[0].get_str());
    CKeyID stakingKeyID;
    if (!stakingAddress.IsValid() || !stakingAddress.GetKeyID(stakingKeyID))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Staking address is not a valid Stock address");

    CStockAddress spendingAddress(params[1].get_str());
    CKeyID spendingKeyID;
    if (!spendingAddress.IsValid() || !spendingAddress.GetKeyID(spendingKeyID))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Spending address is not a valid Stock address");

    spendingAddress.GetKeyID(spendingKeyID);

    if (params.size() == 3)
    {
        CStockAddress votingAddress(params[2].get_str());
        CKeyID votingKeyID;
        if (!votingAddress.IsValid() || !votingAddress.GetKeyID(votingKeyID))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Voting address is not a valid Stock address");

        votingAddress.GetKeyID(votingKeyID);

        return CStockAddress(stakingKeyID, spendingKeyID, votingKeyID).ToString();
    }

    return CStockAddress(stakingKeyID, spendingKeyID).ToString();
}


CStockAddress GetAccountAddress(std::string strAccount, bool bForceNew=false)
{
    CPubKey pubKey;
    if (!pwalletMain->GetAccountPubkey(pubKey, strAccount, bForceNew)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }

    return CStockAddress(pubKey.GetID());
}

UniValue getaccountaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "getaccountaddress \"account\"\n"
                "\nDEPRECATED. Returns the current Stock address for receiving payments to this account.\n"
                "\nArguments:\n"
                "1. \"account\"       (string, required) The account name for the address. It can also be set to the empty string \"\" to represent the default account. The account does not need to exist, it will be created and a new address created  if there is no account by the given name.\n"
                "\nResult:\n"
                "\"stockaddress\"   (string) The account stock address\n"
                "\nExamples:\n"
                + HelpExampleCli("getaccountaddress", "")
                + HelpExampleCli("getaccountaddress", "\"\"")
                + HelpExampleCli("getaccountaddress", "\"myaccount\"")
                + HelpExampleRpc("getaccountaddress", "\"myaccount\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    std::string strAccount = AccountFromValue(params[0]);

    UniValue ret(UniValue::VSTR);

    ret = GetAccountAddress(strAccount).ToString();
    return ret;
}


UniValue getrawchangeaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw std::runtime_error(
                "getrawchangeaddress\n"
                "\nReturns a new Stock address, for receiving change.\n"
                "This is for use with raw transactions, NOT normal use.\n"
                "\nResult:\n"
                "\"address\"    (string) The address\n"
                "\nExamples:\n"
                + HelpExampleCli("getrawchangeaddress", "")
                + HelpExampleRpc("getrawchangeaddress", "")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    CReserveKey reservekey(pwalletMain);
    CPubKey vchPubKey;
    if (!reservekey.GetReservedKey(vchPubKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    reservekey.KeepKey();

    CKeyID keyID = vchPubKey.GetID();

    return CStockAddress(keyID).ToString();
}


UniValue setaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
                "setaccount \"stockaddress\" \"account\"\n"
                "\nDEPRECATED. Sets the account associated with the given address.\n"
                "\nArguments:\n"
                "1. \"stockaddress\"  (string, required) The stock address to be associated with an account.\n"
                "2. \"account\"         (string, required) The account to assign the address to.\n"
                "\nExamples:\n"
                + HelpExampleCli("setaccount", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"tabby\"")
                + HelpExampleRpc("setaccount", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", \"tabby\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CStockAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address");

    std::string strAccount;
    if (params.size() > 1)
        strAccount = AccountFromValue(params[1]);

    // Only add the account if the address is yours.
    if (IsMine(*pwalletMain, address.Get()))
    {
        // Detect when changing the account of an address that is the 'unused current key' of another account:
        if (pwalletMain->mapAddressBook.count(address.Get()))
        {
            std::string strOldAccount = pwalletMain->mapAddressBook[address.Get()].name;
            if (address == GetAccountAddress(strOldAccount))
                GetAccountAddress(strOldAccount, true);
        }
        pwalletMain->SetAddressBook(address.Get(), strAccount, "receive");
    }
    else
        throw JSONRPCError(RPC_MISC_ERROR, "setaccount can only be used with own address");

    return NullUniValue;
}


UniValue getaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "getaccount \"stockaddress\"\n"
                "\nDEPRECATED. Returns the account associated with the given address.\n"
                "\nArguments:\n"
                "1. \"stockaddress\"  (string, required) The stock address for account lookup.\n"
                "\nResult:\n"
                "\"accountname\"        (string) the account address\n"
                "\nExamples:\n"
                + HelpExampleCli("getaccount", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\"")
                + HelpExampleRpc("getaccount", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CStockAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address");

    std::string strAccount;
    std::map<CTxDestination, CAddressBookData>::iterator mi = pwalletMain->mapAddressBook.find(address.Get());
    if (mi != pwalletMain->mapAddressBook.end() && !(*mi).second.name.empty())
        strAccount = (*mi).second.name;
    return strAccount;
}


UniValue getaddressesbyaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "getaddressesbyaccount \"account\"\n"
                "\nDEPRECATED. Returns the list of addresses for the given account.\n"
                "\nArguments:\n"
                "1. \"account\"  (string, required) The account name.\n"
                "\nResult:\n"
                "[                     (json array of string)\n"
                "  \"stockaddress\"  (string) a stock address associated with the given account\n"
                "  ,...\n"
                "]\n"
                "\nExamples:\n"
                + HelpExampleCli("getaddressesbyaccount", "\"tabby\"")
                + HelpExampleRpc("getaddressesbyaccount", "\"tabby\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string strAccount = AccountFromValue(params[0]);

    // Find all addresses that have the given account
    UniValue ret(UniValue::VARR);
    for(const std::pair<CStockAddress, CAddressBookData>& item: pwalletMain->mapAddressBook)
    {
        const CStockAddress& address = item.first;
        const std::string& strName = item.second.name;
        if (strName == strAccount && strName != "blsct receive")
            ret.push_back(address.ToString());
    }
    return ret;
}

UniValue listprivateaddresses(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw std::runtime_error(
                "listprivateaddresses\n"
                "\nList the private addresses of the wallet.\n"
                "\nExample:\n"
                + HelpExampleCli("listprivateaddresses", "")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Find all addresses that have the given account
    UniValue ret(UniValue::VARR);
    for(const std::pair<std::string, CAddressBookData>& item: pwalletMain->mapPrivateAddressBook)
    {
        const std::string& address = item.first;
        const std::string& strName = item.second.name;
        const std::string& index = item.second.purpose;
        if (strName == "blsct receive")
        {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("address", address);
            obj.pushKV("index", index);
            ret.push_back(obj);
        }
    }
    return ret;
}

static void SendMoney(const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, bool fPrivate = false, bool donate = false, bool fDoNotSend = false, const CandidateTransaction* coinsToMix = 0, const std::vector<unsigned char>& vData=std::vector<unsigned char>(), const TokenId &tokenId=TokenId(), const TokenId &tokenIdMint=TokenId())
{
    CAmount curBalance = fPrivate ? pwalletMain->GetPrivateBalance(tokenId) : pwalletMain->GetBalance();

    // Check amount
    if (nValue < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    CAmount toMint = 0;

    TokenId tokenId_;

    if (vData.size() > 0)
    {
        Predicate program(vData);

        if (program.action == MINT)
        {
            if (SerializeHash(program.kParameters[0]) == tokenIdMint.token)
                toMint += program.nParameters[0];
            tokenId_ = tokenIdMint;
        }
    }

    if (tokenId.token != uint256() || tokenId_.token != uint256()) {
        fSubtractFeeFromAmount = false;
    }

    if (nValue > curBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strprintf("Insufficient funds (available %d, trying to send %d)", curBalance, nValue-toMint));

    CScript CFContributionScript;

    // Parse Stock address
    CScript scriptPubKey = GetScriptForDestination(address);

    if(donate)
        SetScriptForCommunityFundContribution(scriptPubKey);

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    bool fBLSCT = address.type() == typeid(blsctDoublePublicKey);
    CRecipient recipient = {scriptPubKey, nValue, fSubtractFeeFromAmount, fBLSCT};
    if (fBLSCT)
    {
        bls::G1Element vk, sk;
        blsctDoublePublicKey dk = boost::get<blsctDoublePublicKey>(address);

        if (!dk.GetSpendKey(sk) || !dk.GetViewKey(vk))
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");
        }
        recipient.sk = sk.Serialize();
        recipient.vk = vk.Serialize();
        recipient.sMemo = wtxNew.mapValue["comment"];
    }
    recipient.vData = vData;
    recipient.tokenId = tokenId_.token == uint256() ? tokenId : tokenId_;
    vecSend.push_back(recipient);

    std::vector<std::shared_ptr<CReserveBLSCTBlindingKey>> reserveBLSCTKey;

    if (fBLSCT || fPrivate)
    {
        for (unsigned int i = 0; i < vecSend.size()+2; i++)
        {
            std::shared_ptr<CReserveBLSCTBlindingKey> rk(new CReserveBLSCTBlindingKey(pwalletMain));
            reserveBLSCTKey.insert(reserveBLSCTKey.begin(), std::move(rk));
        }
    }

    if (!pwalletMain->CreateTransaction(vecSend, wtxNew, reservekey, reserveBLSCTKey, nFeeRequired, nChangePosRet, strError, fPrivate, nullptr, true, coinsToMix, 0, tokenId)) {
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > curBalance)
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if (!fDoNotSend && !pwalletMain->CommitTransaction(wtxNew, reservekey, reserveBLSCTKey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of the wallet and coins were spent in the copy but not marked as spent here.");
}

UniValue generateblsctkeys(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp)
        throw std::runtime_error(
                "generateblsctkeys\n"
                "\nGenerates the BLSCT keys.\n"
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (pwalletMain->HasValidBLSCTKey())
        throw JSONRPCError(RPC_MISC_ERROR, "This wallet already owns BLSCT keys");

    EnsureWalletIsUnlocked();

    CKeyID masterKeyID = pwalletMain->GetHDChain().masterKeyID;
    CKey key;

    if (!pwalletMain->GetKey(masterKeyID, key))
    {
        throw JSONRPCError(RPC_MISC_ERROR, "Could not generate BLSCT parameters. If your wallet is encrypted, you must first unlock your wallet.");
    }

    pwalletMain->GenerateBLSCT();

    LogPrintf("Generated BLSCT parameters.\n");

    return true;
}

UniValue sendtoaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 6)
        throw std::runtime_error(
                "sendtoaddress \"stockaddress\" amount ( \"comment\" \"comment-to\" \"strdzeel\" subtractfeefromamount )\n"
                "\nSend an amount to a given address.\n"
                + HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1. \"stockaddress\"  (string, required) The stock address to send to.\n"
                "2. \"amount\"      (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
                                                                                                    "3. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
                                                                                                    "                             This is not part of the transaction, just kept in your wallet.\n"
                                                                                                    "4. \"comment-to\"  (string, optional) A comment to store the name of the person or organization \n"
                                                                                                    "                             to which you're sending the transaction. This is not part of the \n"
                                                                                                    "                             transaction, just kept in your wallet.\n"
                                                                                                    "5. \"strdzeel\"  (string, optional) Attached string metadata \n"
                                                                                                    "6. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
                                                                                                    "                             The recipient will receive less stocks than you enter in the amount field.\n"
                                                                                                    "\nResult:\n"
                                                                                                    "\"transactionid\"  (string) The transaction id.\n"
                                                                                                    "\nExamples:\n"
                + HelpExampleCli("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1")
                + HelpExampleCli("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"donation\" \"seans outpost\"")
                + HelpExampleCli("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"\" \"\" \"\" true")
                + HelpExampleRpc("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.1, \"donation\", \"seans outpost\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string address_str = params[0].get_str();
    utils::DNSResolver *DNS = nullptr;

    if(DNS->check_address_syntax(params[0].get_str().c_str()))
    {
        bool dnssec_valid; bool dnssec_available;
        std::vector<std::string> addresses = utils::dns_utils::addresses_from_url(params[0].get_str().c_str(), dnssec_available, dnssec_valid);

        if(addresses.empty())
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid OpenAlias address");
        }
        else if (!dnssec_valid && GetBoolArg("-requirednssec",true))
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "OpenAlias Address does not support DNS Sec");
        }
        else
        {
            address_str = addresses.front();
        }
    }
    CStockAddress address(address_str);
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address");

    // Amount
    CAmount nAmount = AmountFromValue(params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 2 && !params[2].isNull() && !params[2].get_str().empty())
        wtx.mapValue["comment"] = params[2].get_str();
    if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
        wtx.mapValue["to"] = params[3].get_str();

    std::string strDZeel;

    if (params.size() > 4 && !params[4].isNull() && !params[4].get_str().empty())
        wtx.strDZeel = params[4].get_str();

    bool fSubtractFeeFromAmount = false;
    if (params.size() > 5)
        fSubtractFeeFromAmount = params[5].get_bool();

    EnsureWalletIsUnlocked();
    SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, false);

    return wtx.GetHash().GetHex();
}

UniValue privatesendtoaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (!IsBLSCTEnabled(chainActive.Tip(),Params().GetConsensus()))
    {
        return JSONRPCError(RPC_MISC_ERROR, "x0DYNS is not active yet");
    }

    if (fHelp || params.size() < 2 || params.size() > 6)
        throw std::runtime_error(
                "privatesendtoaddress \"stockaddress\" amount \"comment\" ( \"comment-to\" \"strdzeel\" subtractfeefromamount )\n"
                "\nSend an amount to a given address using the private balance of coins.\n"
                + HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1. \"stockaddress\"  (string, required) The stock address to send to.\n"
                "2. \"amount\"      (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
                                                                                                    "3. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
                                                                                                    "                             This is part of the transaction and will be seen by the receiver. Max 54 chars.\n"
                                                                                                    "4. \"comment-to\"  (string, optional) A comment to store the name of the person or organization \n"
                                                                                                    "                             to which you're sending the transaction. This is not part of the \n"
                                                                                                    "                             transaction, just kept in your wallet.\n"
                                                                                                    "5. \"strdzeel\"            (string, optional) Attached string metadata \n"
                                                                                                    "6. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
                                                                                                    "                             The recipient will receive less stocks than you enter in the amount field.\n"
                                                                                                    "\nResult:\n"
                                                                                                    "\"transactionid\"  (string) The transaction id.\n"
                                                                                                    "\nExamples:\n"
                + HelpExampleCli("privatesendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1")
                + HelpExampleCli("privatesendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"donation\" \"seans outpost\"")
                + HelpExampleCli("privatesendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"\" \"\" true")
                + HelpExampleRpc("privatesendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.1, \"donation\", \"seans outpost\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string address_str = params[0].get_str();
    utils::DNSResolver *DNS = nullptr;

    if(DNS->check_address_syntax(params[0].get_str().c_str()))
    {
        bool dnssec_valid; bool dnssec_available;
        std::vector<std::string> addresses = utils::dns_utils::addresses_from_url(params[0].get_str().c_str(), dnssec_available, dnssec_valid);

        if(addresses.empty())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid OpenAlias address");
        else if (!dnssec_valid && GetBoolArg("-requirednssec",true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "OpenAlias Address does not support DNS Sec");
        else
        {

            address_str = addresses.front();

        }

    }

    CStockAddress address(address_str);
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address");

    // Amount
    CAmount nAmount = AmountFromValue(params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 2 && !params[2].isNull() && !params[2].get_str().empty())
        wtx.mapValue["comment"] = params[2].get_str();
    if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
        wtx.mapValue["to"] = params[3].get_str();

    std::string strDZeel;

    if (params.size() > 4 && !params[4].isNull() && !params[4].get_str().empty())
        strDZeel = params[4].get_str();

    bool fSubtractFeeFromAmount = false;
    if (params.size() > 5)
        fSubtractFeeFromAmount = params[5].get_bool();

    EnsureWalletIsUnlocked();

    wtx.strDZeel = strDZeel;

    CTxDestination dest = address.Get();

    SendMoney(dest, nAmount, fSubtractFeeFromAmount, wtx, true);


    return wtx.GetHash().GetHex();
}

UniValue privatesendmixtoaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (!IsBLSCTEnabled(chainActive.Tip(),Params().GetConsensus()))
    {
        return JSONRPCError(RPC_MISC_ERROR, "x0DYNS is not active yet");
    }

    if (!GetBoolArg("-blsctmix", DEFAULT_MIX))
    {
        return JSONRPCError(RPC_MISC_ERROR, "-blsctmix is 0");
    }

    if (fHelp || params.size() < 2 || params.size() > 6)
        throw std::runtime_error(
                "privatesendmixtoaddress \"stockaddress\" amount \"comment\" ( \"comment-to\" \"strdzeel\" subtractfeefromamount )\n"
                "\nSend an amount to a given address using the private balance of coins after participating in an aggregation session.\n"
                + HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1. \"stockaddress\"  (string, required) The stock address to send to.\n"
                "2. \"amount\"      (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
                "3. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
                "                             This is part of the transaction and will be seen by the receiver. Max 54 chars.\n"
                "4. \"comment-to\"  (string, optional) A comment to store the name of the person or organization \n"
                "                             to which you're sending the transaction. This is not part of the \n"
                "                             transaction, just kept in your wallet.\n"
                "5. \"strdzeel\"            (string, optional) Attached string metadata \n"
                "6. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
                "                             The recipient will receive less stocks than you enter in the amount field.\n"
                "\nResult:\n"
                "\"transactionid\"  (string) The transaction id.\n"
                "\nExamples:\n"
                + HelpExampleCli("privatesendmixtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1")
                + HelpExampleCli("privatesendmixtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"donation\" \"seans outpost\"")
                + HelpExampleCli("privatesendmixtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1 \"\" \"\" true")
                + HelpExampleRpc("privatesendmixtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.1, \"donation\", \"seans outpost\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string address_str = params[0].get_str();
    utils::DNSResolver *DNS = nullptr;

    if(DNS->check_address_syntax(params[0].get_str().c_str()))
    {
        bool dnssec_valid; bool dnssec_available;
        std::vector<std::string> addresses = utils::dns_utils::addresses_from_url(params[0].get_str().c_str(), dnssec_available, dnssec_valid);

        if(addresses.empty())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid OpenAlias address");
        else if (!dnssec_valid && GetBoolArg("-requirednssec",true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "OpenAlias Address does not support DNS Sec");
        else
        {

            address_str = addresses.front();

        }

    }

    CStockAddress address(address_str);
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address");

    // Amount
    CAmount nAmount = AmountFromValue(params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 2 && !params[2].isNull() && !params[2].get_str().empty())
        wtx.mapValue["comment"] = params[2].get_str();
    if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
        wtx.mapValue["to"] = params[3].get_str();

    std::string strDZeel;

    if (params.size() > 4 && !params[4].isNull() && !params[4].get_str().empty())
        strDZeel = params[4].get_str();

    bool fSubtractFeeFromAmount = false;
    if (params.size() > 5)
        fSubtractFeeFromAmount = params[5].get_bool();

    EnsureWalletIsUnlocked();

    wtx.strDZeel = strDZeel;

    CTxDestination dest = address.Get();

    if (!pwalletMain->aggSession)
        throw JSONRPCError(RPC_WALLET_ERROR, "Could not find an aggregation session");

    CandidateTransaction selectedCoins;

    {
        LOCK(cs_main);
        auto nCount = pwalletMain->aggSession->GetTransactionCandidates().size();

        if (nCount == 0)
            throw JSONRPCError(RPC_WALLET_ERROR, "There are no candidates for mixing.");

        if (!pwalletMain->aggSession->SelectCandidates(selectedCoins))
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "There was an error trying to validate the coins after mixing.");
        }
    }

    SendMoney(dest, nAmount, fSubtractFeeFromAmount, wtx, true, false, false, &selectedCoins);

    return wtx.GetHash().GetHex();
}

UniValue stakervote(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "stakervote vote\n"
                "\nSets the vote to be committed on minted blocks.\n"
                + HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1. \"vote\"               (string, required) The staker vote.\n"
                + HelpExampleCli("stakervote", "yes")
                );

    SoftSetArg("-stakervote",params[0].get_str());
    RemoveConfigFile("stakervote");
    WriteConfigFile("stakervote",params[0].get_str());

    return "";
}

UniValue setexclude(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "setexclude bool\n"
                "\nSets the node blocks to be excluded from votings.\n"
                "\nArguments:\n"
                "1. \"bool\"               (bool, required) Whether to turn on or off.\n"
                + HelpExampleCli("setexclude", "true")
                );

    if (!params[0].isBool())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, argument 1 must be a boolean");

    SoftSetArg("-excludevote", params[0].get_bool() ? "1" : "0", true);
    RemoveConfigFile("excludevote");
    WriteConfigFile("excludevote", params[0].get_bool() ? "1" : "0");

    return "";
}

UniValue createproposal(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 4)
        throw std::runtime_error(
                "createproposal \"stockaddress\" \"amount\" duration \"desc\" ( fee dump_raw )\n"
            "\nCreates a proposal for the community fund. Min fee of " + FormatMoney(GetConsensusParameter(Consensus::CONSENSUS_PARAM_PROPOSAL_MIN_FEE, view)) + "0DYNS is required.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"stockaddress\"       (string, required) The stock address where coins would be sent if proposal is approved.\n"
            "2. \"amount\"               (numeric or string, required) The amount in " + CURRENCY_UNIT + " to request. eg 0.1\n"
            "3. duration               (numeric, required) Number of seconds the proposal will exist after being accepted.\n"
            "4. \"desc\"                 (string, required) Short description of the proposal.\n"
            "5. fee                    (numeric, optional) Contribution to the fund used as fee.\n"
            "6. dump_raw               (bool, optional) Dump the raw transaction instead of sending. Default: false\n"
            "7. \"owneraddress\"         (string, optional) The owner of the proposal who will sign the payment requests. Default: the payment address\n"
            "8. super_proposal         (bool, optional) Creates a super proposal which would print new coins instead of taking from the fund. Default: false\n"
            "\nResult:\n"
            "\"{ hash: proposalid,\"            (string) The proposal id.\n"
            "\"  strDZeel: string }\"            (string) The attached strdzeel property.\n"
            "\nExamples:\n"
            + HelpExampleCli("createproposal", "\"NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ\" 1000 86400 \"Development\"")
                + HelpExampleCli("createproposal", "\"NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ\" 12000 3600 \"Promotional stickers for everyone\" 100")
                );

    if (!Params().GetConsensus().fDaoClientActivated)
        throw JSONRPCError(RPC_WALLET_ERROR, "This command is temporarily disabled");

    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    // Amount
    CAmount nAmount = params.size() == 5 ? AmountFromValue(params[4]) : GetConsensusParameter(Consensus::CONSENSUS_PARAM_PROPOSAL_MIN_FEE, view);
    if (nAmount <= 0 || nAmount < GetConsensusParameter(Consensus::CONSENSUS_PARAM_PROPOSAL_MIN_FEE, view))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for fee");

    bool fDump = params.size() == 6 ? params[5].getBool() : false;
    bool fSuper = params.size() == 8 ? params[7].getBool() : false;

    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    std::string paymentAddress = params[0].get_str();
    std::string ownerAddress = params.size() == 7 ? params[6].get_str() : paymentAddress;

    CStockAddress paddress(paymentAddress);
    if (!paddress.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address for payment");

    CStockAddress oaddress(ownerAddress);
    if (!oaddress.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address for the owner");

    CAmount nReqAmount = AmountFromValue(params[1]);
    int64_t nDeadline = params[2].get_int64();

    if(nDeadline <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Wrong deadline");

    std::string sDesc = params[3].get_str();

    UniValue strDZeel(UniValue::VOBJ);
    uint64_t nVersion = CProposal::BASE_VERSION;

    if (IsReducedCFundQuorumEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CProposal::REDUCED_QUORUM_VERSION;

    if (IsDAOEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CProposal::ABSTAIN_VOTE_VERSION;

    if (IsExcludeEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CProposal::EXCLUDE_VERSION;

    if (IsDaoSuperEnabled(chainActive.Tip(), Params().GetConsensus()) && fSuper)
        nVersion |= CProposal::SUPER_VERSION;

    strDZeel.pushKV("n",nReqAmount);
    strDZeel.pushKV("a",ownerAddress);
    if (ownerAddress != paymentAddress)
        strDZeel.pushKV("p",paymentAddress);
    strDZeel.pushKV("d",nDeadline);
    strDZeel.pushKV("s",sDesc);
    strDZeel.pushKV("v",(uint64_t)nVersion);

    wtx.strDZeel = strDZeel.write();
    wtx.nCustomVersion = CTransaction::PROPOSAL_VERSION;

    if(wtx.strDZeel.length() > 1024)
        throw JSONRPCError(RPC_TYPE_ERROR, "String too long");

    EnsureWalletIsUnlocked();
    SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, false, true, fDump);

    if (!fDump)
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("hash",wtx.GetHash().GetHex());
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
    else
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("raw",EncodeHexTx(wtx));
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
}

UniValue createtoken(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 3)
        throw std::runtime_error(
            "createtoken \"name\" \"token_code\" max_supply\n"
            "\nCreates a confidential token.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"name\"       (string, required) The name of the token\n"
            "2. max_supply     (numeric, optional) The max supply of the token.\n"
            "\nExamples:\n"
            + HelpExampleCli("createtoken", "\"Baby0DYNS\" \"B0DYNS\" 1000")
                + HelpExampleCli("createtoken", "\"Wrapped Bitcoin\" \"wBTC\" 21000000")
                );


    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr() || !params[1].isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Name and token_code must be strings");

    std::string sName = params[0].get_str();
    std::string sDesc = params[1].get_str();

    // Supply
    CAmount nSupply = AmountFromValue(params[2]);
    if (nSupply <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid supply");

    blsctKey sk;

    if (!pwalletMain->GetBLSCTSpendKey(sk))
        throw JSONRPCError(RPC_TYPE_ERROR, "Wallet not available");

    blsctKey pk = sk.PrivateChildHash(SerializeHash("token/"+sName+sDesc));
    bls::G1Element pkg1 = pk.GetG1Element();

    if (view.HaveToken(SerializeHash(pkg1)))
        throw JSONRPCError(RPC_TYPE_ERROR, "You already registered one token with that name and symbol");

    pwalletMain->AddBLSCTTokenKey(pk);

    TokenInfo token(pkg1, sName, sDesc, nSupply);

    EnsureWalletIsUnlocked();
    SendMoney(address.Get(), 0, fSubtractFeeFromAmount, wtx, true, true, false, 0, token.GetCreateProgram());

    return wtx.GetHash().GetHex();
}

UniValue registername(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 1)
        throw std::runtime_error(
            "registername \"name\"\n"
            "\nRegister a dotStock name.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"name\"       (string, required) The name to register\n"
            "\nExamples:\n"
            + HelpExampleCli("registername", "satoshi.0dyns")
                );


    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr() )
        throw JSONRPCError(RPC_TYPE_ERROR, "Name must be string");

    std::string sName = params[0].get_str();

    if (!DotStock::IsValid(sName))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid name");

    blsctKey sk;

    EnsureWalletIsUnlocked();

    if (!pwalletMain->GetBLSCTSpendKey(sk))
        throw JSONRPCError(RPC_TYPE_ERROR, "Wallet not available");

    blsctKey pk = sk.PrivateChildHash(SerializeHash("name/"+DotStock::GetHashName(sName).ToString()));
    bls::G1Element pkg1 = pk.GetG1Element();

    if (view.HaveNameRecord(DotStock::GetHashIdName(sName, pkg1)))
        throw JSONRPCError(RPC_TYPE_ERROR, "You already reserved that name, use updatename instead");
    if (view.HaveNameData(DotStock::GetHashName(sName)))
        throw JSONRPCError(RPC_TYPE_ERROR, "That name is already registered");

    pwalletMain->AddBLSCTTokenKey(pk);

    SendMoney(address.Get(), 0, fSubtractFeeFromAmount, wtx, true, true, false, 0, DotStock::GetRegisterProgram(sName, pkg1));

    return wtx.GetHash().GetHex();
}


UniValue genkeyname(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 1)
        throw std::runtime_error(
            "genkeyname \"name\"\n"
            "\nGenerates a public key for receiving a dotStock name.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"name\"       (string, required) The name \n"
            "\nExamples:\n"
            + HelpExampleCli("genkeyname", "satoshi.0dyns")
                );

    if (!params[0].isStr() )
        throw JSONRPCError(RPC_TYPE_ERROR, "Name must be string");

    std::string sName = params[0].get_str();

    if (!DotStock::IsValid(sName))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid name");

    blsctKey sk;

    EnsureWalletIsUnlocked();

    if (!pwalletMain->GetBLSCTSpendKey(sk))
        throw JSONRPCError(RPC_TYPE_ERROR, "Wallet not available");

    blsctKey pk = sk.PrivateChildHash(SerializeHash("name/"+DotStock::GetHashName(sName).ToString()));
    bls::G1Element pkg1 = pk.GetG1Element();

    pwalletMain->AddBLSCTTokenKey(pk);

    return HexStr(pkg1.Serialize());
}

UniValue renewname(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 1)
        throw std::runtime_error(
            "renewname \"name\"\n"
            "\nRenews a dotStock name.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"name\"       (string, required) The name to renew\n"
            "\nExamples:\n"
            + HelpExampleCli("renewname", "satoshi.0dyns")
                );


    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr() )
        throw JSONRPCError(RPC_TYPE_ERROR, "Name must be string");

    std::string sName = params[0].get_str();

    if (!DotStock::IsValid(sName))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid name");

    if (!view.HaveNameData(DotStock::GetHashName(sName)))
        throw JSONRPCError(RPC_TYPE_ERROR, "That name is not registered");

    CAmount fee = GetConsensusParameter(Consensus::CONSENSUS_PARAM_STOCKNS_FEE, view);

    SendMoney(address.Get(), fee, fSubtractFeeFromAmount, wtx, true, true, false, 0, DotStock::GetRenewProgram(sName));

    return wtx.GetHash().GetHex();
}

UniValue resolvename(const UniValue& params, bool fHelp)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    UniValue ret(UniValue::VOBJ);

    if (fHelp || params.size() < 1)
        throw std::runtime_error(
            "resolvename \"name\"\n"
            "\nResolves a dotStock name.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"name\"       (string, required) The name to resolve\n"
            "2. \"subdomains\" (boolean, optional) Include subdomains\n"
            "\nExamples:\n"
            + HelpExampleCli("resolvename", "satoshi.0dyns")
            + HelpExampleCli("resolvename", "satoshi.0dyns true")
                );

    if (!params[0].isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Name must be string");

    if (params.size() == 2 && !params[1].isBool())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, argument 2 must be a boolean");

    bool getSubdomains = params.size() > 1 && params[1].isBool() ? params[1].getBool() : false;

    std::string sFullName = params[0].get_str();

    std::string delimiter = ".";
    std::string subdomain = "";
    std::string sName = "";

    if (std::count(sFullName.begin(), sFullName.end(), '.') == 2) {
        subdomain = sFullName.substr(0, sFullName.find(delimiter));
        sName = sFullName.substr(sFullName.find(delimiter)+1, sFullName.size());
    } else {
        sName = sFullName;
    }

    if (subdomain != "" && !DotStock::IsValidKey(subdomain))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid sub domain");

    if (!DotStock::IsValid(sName))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid name");

    if (!view.HaveNameData(DotStock::GetHashName(sName)))
        return ret;

    NameDataValues data;

    if (!view.GetNameData(DotStock::GetHashName(sName), data))
    {
        return ret;
    }

    auto mapData = DotStock::Consolidate(data, chainActive.Tip()->nHeight, subdomain);

    for (auto &it: mapData) {
        if (it.first.substr(0,1) == "_") {
            ret.pushKV(it.first, it.second);
        }
    }

    if (getSubdomains && subdomain == "") {
        auto subData = DotStock::ConsolidateSubdomains(data, chainActive.Tip()->nHeight);
        UniValue subUniMain(UniValue::VOBJ);

        for (auto &it: subData) {
            UniValue subUni(UniValue::VOBJ);
            for (auto &sit: it.second) {
                subUni.pushKV(sit.first, sit.second);
            }
            subUniMain.pushKV(it.first, subUni);
        }
        ret.pushKV("_subdomains", subUniMain);
    }

    UniValue dotData(UniValue::VOBJ);
    for (auto &it: mapData) {
        if (it.first.substr(0,1) != "_") {
            dotData.pushKV(it.first, it.second);
        }
    }

    ret.pushKV(".", dotData);

    return ret;
}

UniValue updatename(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 3)
        throw std::runtime_error(
            "updatename \"name\" \"key\" \"value\"\n"
            "\nUpdates a dotStock name.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"name\"       (string, required) The name to update\n"
            "1. \"key\"        (string, required) The parameter to update\n"
            "1. \"value\"      (string, required) The value to set\n"
            "\nExamples:\n"
            + HelpExampleCli("updatename", "satoshi.0dyns 0dyns NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ")
                );


    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr() || !params[1].isStr() || !params[2].isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Name and key and value must be strings");

    std::string sFullName = params[0].get_str();

    std::string delimiter = ".";
    std::string subdomain = "";
    std::string sName = "";

    if (std::count(sFullName.begin(), sFullName.end(), '.') == 2) {
        subdomain = sFullName.substr(0, sFullName.find(delimiter));
        sName = sFullName.substr(sFullName.find(delimiter)+1, sFullName.size());
    } else {
        sName = sFullName;
    }

    if (subdomain != "" && !DotStock::IsValidKey(subdomain))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid sub domain");

    if (!DotStock::IsValid(sName))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid name");

    std::string sKey = params[1].get_str();

    if (!DotStock::IsValidKey(sKey))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid key");

    std::string sValue = params[2].get_str();

    blsctKey sk;

    EnsureWalletIsUnlocked();

    if (!pwalletMain->GetBLSCTSpendKey(sk))
        throw JSONRPCError(RPC_TYPE_ERROR, "Wallet not available");

    blsctKey pk = sk.PrivateChildHash(SerializeHash("name/"+DotStock::GetHashName(sName).ToString()));
    bls::G1Element pkg1 = pk.GetG1Element();

    if (!view.HaveNameRecord(DotStock::GetHashIdName(sName, pkg1)))
        throw JSONRPCError(RPC_TYPE_ERROR, "That name is not registered");

    NameRecordValue recordvalue;

    if (!view.GetNameRecord(DotStock::GetHashIdName(sName, pkg1), recordvalue))
        throw JSONRPCError(RPC_TYPE_ERROR, "Can't get that name");

    if (chainActive.Tip()->nHeight-recordvalue.height < 6)
        throw JSONRPCError(RPC_TYPE_ERROR, "Wait 6 blocks until the name is registered");

    bool first = false;

    NameDataValues data;
    uint64_t dataSize = 0;

    if (!view.HaveNameData(DotStock::GetHashName(sName)))
        first = true;
    else {
        if (!view.GetNameData(DotStock::GetHashName(sName), data))
        {
            throw JSONRPCError(RPC_TYPE_ERROR, "Could not find the name");
        }
        auto mapData = DotStock::Consolidate(data, chainActive.Tip()->nHeight);
        if (!mapData.count("_key"))
        {
            first = true;
        } else {
            mapData[sKey] = sValue;
            dataSize = DotStock::CalculateSize(mapData);
            try {
                if (bls::G1Element::FromByteVector(ParseHex(mapData["_key"])) != pkg1)
                {
                    throw JSONRPCError(RPC_TYPE_ERROR, "You don't own the name");
                }
            } catch(...) {
                throw JSONRPCError(RPC_TYPE_ERROR, "Wrong format key of name");
            }
        }
    }

    uint64_t fee = first ? GetConsensusParameter(Consensus::CONSENSUS_PARAM_STOCKNS_FEE, view) : std::floor(dataSize/GetConsensusParameter(Consensus::CONSENSUS_PARAMS_DOTSTOCK_MAXDATA, view))*GetConsensusParameter(Consensus::CONSENSUS_PARAMS_DOTSTOCK_FEE_EXTRADATA, view);

    auto program = first ? DotStock::GetUpdateFirstProgram(sName, pkg1, sKey, sValue, subdomain) : DotStock::GetUpdateProgram(sName, pkg1, sKey, sValue, subdomain);

    SendMoney(address.Get(), fee, fSubtractFeeFromAmount, wtx, true, true, false, 0, program);

    return wtx.GetHash().GetHex();
}

UniValue createnft(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 3)
        throw std::runtime_error(
            "createnft \"name\" \"scheme\" max_supply\n"
            "\nCreates a nft.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"name\"       (string, required) The name of the NFT\n"
            "1. \"scheme\"     (string, required) The scheme for the NFT metadata\n"
            "2. max_supply     (numeric, optional) The max supply of the NFT.\n"
            "\nExamples:\n"
            + HelpExampleCli("createnft", "\"0DYNS Artwork\" \"{'resource': 'url'}\" 1000")
            + HelpExampleCli("createnft", "\"Game Item\" \"{'type': 'string', 'power': 'int'}\" 100")
                );


    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr() || !params[1].isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Name and token_code must be strings");

    std::string sName = params[0].get_str();
    std::string sDesc = params[1].get_str();

    // Supply
    CAmount nSupply = params[2].get_int64();
    if (nSupply <= 0 || !MoneyRange(nSupply))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid supply");

    blsctKey sk;

    if (!pwalletMain->GetBLSCTSpendKey(sk))
        throw JSONRPCError(RPC_TYPE_ERROR, "Wallet not available");

    blsctKey pk = sk.PrivateChildHash(SerializeHash("nft/"+sName+sDesc));
    bls::G1Element pkg1 = pk.GetG1Element();

    if (view.HaveToken(SerializeHash(pkg1)))
        throw JSONRPCError(RPC_TYPE_ERROR, "You already registered one token with that name and symbol");

    pwalletMain->AddBLSCTTokenKey(pk);

    TokenInfo token(pkg1, sName, sDesc, nSupply, 1);

    EnsureWalletIsUnlocked();
    SendMoney(address.Get(), 0, fSubtractFeeFromAmount, wtx, true, true, false, 0, token.GetCreateProgram());

    return wtx.GetHash().GetHex();
}

UniValue minttoken(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 3)
        throw std::runtime_error(
            "minttoken \"tokenid\" \"destination\" amount\n"
            "\nMints confidential tokens.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"tokenid\"     (string, required) The token id\n"
            "2. \"destination\" (string, required) The x0DYNS destination addressn"
            "3. amount        (string, required) The amount to mint\n"
            "\nExamples:\n"
            + HelpExampleCli("minttoken", "\"a7be93b41e708d21d6c94920401ca5fd93dffe33d2bc197077e3b4fafcc8fe45eebb359b4c8f6bc15a303cc2971a0c48\" \"xNUNs2vtjr6QDL1NiL8TDHgmbuEo5WcY2K2jQ8ATj9pko8wkJ9RutkFQKBCtn6SsBjy6nK5ftofFyLFnAHAynreQCZjuE7dCWVxCX5DCFB2bjx87KvbqVVRCs3KBzdDre7c5FUy7QLo\" 1000")
                );


    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr() || !params[1].isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Token and destination must be strings");

    std::string token = params[0].get_str();

    if (!IsHex(token))
        throw JSONRPCError(RPC_TYPE_ERROR, "Token id is not a hex string");

    uint256 tokenId = uint256S(token);

    if (!view.HaveToken(tokenId))
        throw JSONRPCError(RPC_TYPE_ERROR, "Unknown token");

    TokenInfo tokenInfo;

    if (!view.GetToken(tokenId, tokenInfo))
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find token");

    if (!pwalletMain->HaveBLSCTTokenKey(tokenInfo.key))
    {
        blsctKey sk;

        if (!pwalletMain->GetBLSCTSpendKey(sk))
            throw JSONRPCError(RPC_TYPE_ERROR, "Wallet not available");

        blsctKey pk = sk.PrivateChildHash(SerializeHash("token/"+tokenInfo.sName+tokenInfo.sDesc));

        if (pk.GetG1Element() != tokenInfo.key)
            throw JSONRPCError(RPC_TYPE_ERROR, "Could not find private key for token");

        pwalletMain->AddBLSCTTokenKey(pk);
    }

    std::string address = params[1].get_str();

    CStockAddress dest(address);
    if (!dest.IsValid() || !dest.IsPrivateAddress(Params()))
        throw JSONRPCError(RPC_TYPE_ERROR, "Destination must be an x0DYNS address");

    // Supply
    CAmount amount = AmountFromValue(params[2]);
    if (amount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

    if (!tokenInfo.IncreaseSupply(amount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Not enough supply available");

    tokenInfo.DecreaseSupply(amount);

    EnsureWalletIsUnlocked();

    auto vData = tokenInfo.GetMintProgram(amount, tokenInfo.key);

    if (!vData.size())
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not create program");

    SendMoney(dest.Get(), 0, fSubtractFeeFromAmount, wtx, true, true, false, 0, vData, TokenId(), TokenId(tokenId, -1));

    return wtx.GetHash().GetHex();
}

UniValue mintnft(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 4)
        throw std::runtime_error(
            "mintnft \"tokenid\" \"nftid\" \"destination\" \"metadata\"\n"
            "\nMints nft.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"tokenid\"     (string, required) The token id\n"
            "1. \"nftid\"       (int, required) The ntf id\n"
            "2. \"destination\" (string, required) The x0DYNS destination addressn"
            "3. \"metadata\"    (string, required) The nft metadata\n"
            "\nExamples:\n"
            + HelpExampleCli("mintnft", "\"a7be93b41e708d21d6c94920401ca5fd93dffe33d2bc197077e3b4fafcc8fe45eebb359b4c8f6bc15a303cc2971a0c48\" 1 \"xNUNs2vtjr6QDL1NiL8TDHgmbuEo5WcY2K2jQ8ATj9pko8wkJ9RutkFQKBCtn6SsBjy6nK5ftofFyLFnAHAynreQCZjuE7dCWVxCX5DCFB2bjx87KvbqVVRCs3KBzdDre7c5FUy7QLo\" \"{'resource':'https://stock.org/logo.png'}\"")
                );


    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr() || !params[1].isNum())
        throw JSONRPCError(RPC_TYPE_ERROR, "Token and nftid must be string and number");

    std::string token = params[0].get_str();

    if (!IsHex(token))
        throw JSONRPCError(RPC_TYPE_ERROR, "Token id is not a hex string");

    uint64_t nftid = params[1].get_int64();

    TokenId tokenId(uint256S(token), nftid);

    if (!view.HaveToken(tokenId.token))
        throw JSONRPCError(RPC_TYPE_ERROR, "Unknown token");

    TokenInfo tokenInfo;

    if (!view.GetToken(tokenId.token, tokenInfo))
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find token");

    if (!pwalletMain->HaveBLSCTTokenKey(tokenInfo.key))
    {
        blsctKey sk;

        if (!pwalletMain->GetBLSCTSpendKey(sk))
            throw JSONRPCError(RPC_TYPE_ERROR, "Wallet not available");

        blsctKey pk = sk.PrivateChildHash(SerializeHash("nft/"+tokenInfo.sName+tokenInfo.sDesc));

        if (pk.GetG1Element() != tokenInfo.key)
            throw JSONRPCError(RPC_TYPE_ERROR, "Could not find private key for token");

        pwalletMain->AddBLSCTTokenKey(pk);
    }

    std::string address = params[2].get_str();

    CStockAddress dest(address);
    if (!dest.IsValid() || !dest.IsPrivateAddress(Params()))
        throw JSONRPCError(RPC_TYPE_ERROR, "Destination must be an x0DYNS address");

    // Supply
    std::string metadata = params[3].get_str();

    if (metadata == "")
        throw JSONRPCError(RPC_TYPE_ERROR, "Metadata can't be empty");


    if (tokenInfo.mapMetadata.count(nftid))
        throw JSONRPCError(RPC_TYPE_ERROR, "This NFT has already been minted");

    if (tokenInfo.totalSupply < nftid)
        throw JSONRPCError(RPC_TYPE_ERROR, "Wrong id");

    EnsureWalletIsUnlocked();

    auto vData = tokenInfo.GetMintProgram(nftid, tokenInfo.key, std::vector<unsigned char>(metadata.begin(), metadata.end()));

    if (!vData.size())
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not create program");

    SendMoney(dest.Get(), 0, fSubtractFeeFromAmount, wtx, true, true, false, 0, vData, TokenId(), tokenId);

    return wtx.GetHash().GetHex();
}

UniValue burntoken(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 2)
        throw std::runtime_error(
            "burntoken \"tokenid\" amount\n"
            "\nBurns confidential tokens.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"tokenid\"     (string, required) The token id\n"
            "3. amount        (string, required) The amount to burn\n"
            "\nExamples:\n"
            + HelpExampleCli("burntoken", "\"a7be93b41e708d21d6c94920401ca5fd93dffe33d2bc197077e3b4fafcc8fe45eebb359b4c8f6bc15a303cc2971a0c48\" 1000")
                );


    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Token and destination must be strings");

    std::string token = params[0].get_str();

    if (!IsHex(token))
        throw JSONRPCError(RPC_TYPE_ERROR, "Token id is not a hex string");

    auto tokenId = TokenId(uint256S(token), -1);

    if (!view.HaveToken(tokenId.token))
        throw JSONRPCError(RPC_TYPE_ERROR, "Unknown token");

    TokenInfo tokenInfo;

    if (!view.GetToken(tokenId.token, tokenInfo))
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find token");


    std::string address = "xNVLBgqgCpracwFFqCNibevSZ4fTBcgW2wuWdrnhH5iYmTeZ5RRtCYj4wVPatP7R4g7LQ2Mk9m2pgKXckZJDdhcqE1Kv47tqCdX9g8m2oqECqtGWYfgLvnr6MYJ5a3Sz7apgvssuwrV"; //dummy

    CStockAddress dest(address);
    if (!dest.IsValid() || !dest.IsPrivateAddress(Params()))
        throw JSONRPCError(RPC_TYPE_ERROR, "Destination must be an x0DYNS address");

    // Burn amount
    CAmount amount = AmountFromValue(params[1]);
    if (amount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

    if (!tokenInfo.DecreaseSupply(amount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Not enough supply available");

    tokenInfo.IncreaseSupply(amount);

    EnsureWalletIsUnlocked();

    auto vData = tokenInfo.GetBurnProgram(amount);

    if (!vData.size())
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not create program");

    SendMoney(dest.Get(), amount, fSubtractFeeFromAmount, wtx, true, true, false, 0, vData, tokenId);

    return wtx.GetHash().GetHex();
}

UniValue sendtoken(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 2)
        throw std::runtime_error(
            "sendtoken \"tokenid\" \"destination\" amount\n"
            "\nSends confidential tokens.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"tokenid\"     (string, required) The token id\n"
            "2. \"destination\" (string, required) The x0DYNS destination addressn"
            "3. amount        (string, required) The amount to mint\n"
            "\nExamples:\n"
            + HelpExampleCli("sendtoken", "\"a7be93fd93dffe33d2bc197077e3b4fafcc8fe45eebb359b4c8f6bc15a303cc2971a0c48\" \"xNUNs2vtjr6QDL1NiL8TDHgmbuEo5WcY2K2jQ8ATj9pko8wkJ9RutkFQKBCtn6SsBjy6nK5ftofFyLFnAHAynreQCZjuE7dCWVxCX5DCFB2bjx87KvbqVVRCs3KBzdDre7c5FUy7QLo\" 1000")
                );


    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr() || !params[1].isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Token and destination must be strings");

    std::string token = params[0].get_str();

    if (!IsHex(token))
        throw JSONRPCError(RPC_TYPE_ERROR, "Token id is not a hex string");

    auto tokenId = TokenId(uint256S(token), -1);

    if (!view.HaveToken(tokenId.token))
        throw JSONRPCError(RPC_TYPE_ERROR, "Unknown token");

    TokenInfo tokenInfo;

    if (!view.GetToken(tokenId.token, tokenInfo))
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find token");

    std::string address = params[1].get_str();

    CStockAddress dest(address);
    if (!dest.IsValid() || !dest.IsPrivateAddress(Params()))
        throw JSONRPCError(RPC_TYPE_ERROR, "Destination must be an x0DYNS address");

    // Supply
    CAmount amount = AmountFromValue(params[2]);
    if (amount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

    EnsureWalletIsUnlocked();

    SendMoney(dest.Get(), amount, fSubtractFeeFromAmount, wtx, true, true, false, 0, std::vector<unsigned char>(), tokenId);

    return wtx.GetHash().GetHex();
}

UniValue sendnft(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 2)
        throw std::runtime_error(
            "sendnft \"tokenid\" \"nftid\" \"destination\"\n"
            "\nSends nft.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"tokenid\"     (string, required) The token id\n"
            "2. \"nftid\"       (int, required) The nft id\n"
            "3. \"destination\" (string, required) The x0DYNS destination addressn"
            "\nExamples:\n"
            + HelpExampleCli("sendnft", "\"a7be93fd93dffe33d2bc197077e3b4fafcc8fe45eebb359b4c8f6bc15a303cc2971a0c48\" 1 \"xNUNs2vtjr6QDL1NiL8TDHgmbuEo5WcY2K2jQ8ATj9pko8wkJ9RutkFQKBCtn6SsBjy6nK5ftofFyLFnAHAynreQCZjuE7dCWVxCX5DCFB2bjx87KvbqVVRCs3KBzdDre7c5FUy7QLo\"")
                );


    // Amount
    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    if (!params[0].isStr() || !params[1].isNum())
        throw JSONRPCError(RPC_TYPE_ERROR, "Token and nft id must be string and number");

    std::string token = params[0].get_str();

    if (!IsHex(token))
        throw JSONRPCError(RPC_TYPE_ERROR, "Token id is not a hex string");

    // Supply
    CAmount nftid = params[1].get_int64();
    if (nftid < 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid nftid");

    auto tokenId = TokenId(uint256S(token), nftid);

    if (!view.HaveToken(tokenId.token))
        throw JSONRPCError(RPC_TYPE_ERROR, "Unknown token");

    TokenInfo tokenInfo;

    if (!view.GetToken(tokenId.token, tokenInfo))
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find token");

    std::string address = params[2].get_str();

    CStockAddress dest(address);
    if (!dest.IsValid() || !dest.IsPrivateAddress(Params()))
        throw JSONRPCError(RPC_TYPE_ERROR, "Destination must be an x0DYNS address");

    EnsureWalletIsUnlocked();

    SendMoney(dest.Get(), 1, fSubtractFeeFromAmount, wtx, true, true, false, 0, std::vector<unsigned char>(), tokenId);

    return wtx.GetHash().GetHex();
}


UniValue getconsensusparameters(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
                "getconsensusparameters (extended)\n"
                "\nArguments:\n"
                "1. extended       (bool, optional, default false) Show a more detailed output.\n"
                "\nReturns a list containing the current values of the consensus parameters which can be voted in the DAO.\n"

        );

    LOCK(cs_main);
    CStateViewCache view(pcoinsTip);

    UniValue ret(UniValue::VARR);
    for (unsigned int i = 0; i < Consensus::MAX_CONSENSUS_PARAMS; i++)
    {
        Consensus::ConsensusParamsPos id = (Consensus::ConsensusParamsPos)i;
        int type = (int)Consensus::vConsensusParamsType[id];
        std::string sDesc = Consensus::sConsensusParamsDesc[id];
        if (params.size() > 0)
        {
            UniValue entry(UniValue::VOBJ);
            entry.pushKV("id", (uint64_t)i);
            entry.pushKV("desc", sDesc);
            entry.pushKV("type", type);
            entry.pushKV("value", GetConsensusParameter(id, view));
            ret.push_back(entry);
        }
        else
        {
            ret.push_back(GetConsensusParameter(id, view));
        }
    }
    return ret;
}

UniValue proposeconsensuschange(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    CAmount nMinFee = GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_MIN_FEE, view) + GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_ANSWER_MIN_FEE, view);

    if (fHelp || params.size() < 2 || !params[0].isNum() || !params[1].isNum())
        throw std::runtime_error(
                "proposeconsensuschange parameter value ( fee dump_raw )\n"
            "\nCreates a proposal to the DAO for changing a consensus paremeter. Min fee of " + FormatMoney(nMinFee) + "0DYNS is required.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. parameter        (numeric, required) The parameter id as specified in the output of the getconsensusparameters rpc command.\n"
            "2. value            (numeric, optional) The proposed value.\n"
            "3. fee              (numeric, optional) Contribution to the fund used as fee.\n"
            "4. dump_raw         (bool, optional) Dump the raw transaction instead of sending. Default: false\n"
            "\nResult:\n"
            "\"{ hash: consultation_id,\"            (string) The consultation id.\n"
            "\"  strDZeel: string }\"            (string) The attached strdzeel property.\n"
            "\nExamples:\n"
            + HelpExampleCli("proposeconsensuschange", "1 10")
                );

    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address


    // Amount
    CAmount nAmount = params.size() >= 3 ? AmountFromValue(params[2]) : nMinFee;
    if (nAmount <= 0 || nAmount < nMinFee)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for fee");

    bool fDump = params.size() == 4 ? params[3].getBool() : false;

    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    int64_t nMin = params[0].get_int64();
    int64_t nMax = 1;

    int64_t nValue = params[1].get_int64();

    if (nMin == Consensus::CONSENSUS_PARAM_PROPOSAL_MAX_VOTING_CYCLES || nMin == Consensus::CONSENSUS_PARAM_PAYMENT_REQUEST_MAX_VOTING_CYCLES)
    {
        nValue--;
    }

    std::string sAnswer = std::to_string(nValue);

    if (nMin < Consensus::CONSENSUS_PARAM_VOTING_CYCLE_LENGTH || nMin >= Consensus::MAX_CONSENSUS_PARAMS)
        throw JSONRPCError(RPC_TYPE_ERROR, "Wrong parameter id");

    UniValue strDZeel(UniValue::VOBJ);
    uint64_t nVersion = CConsultation::BASE_VERSION | CConsultation::MORE_ANSWERS_VERSION | CConsultation::CONSENSUS_PARAMETER_VERSION;

    if (IsExcludeEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CConsultation::EXCLUDE_VERSION;

    UniValue answers(UniValue::VARR);
    answers.push_back(sAnswer);

    std::string sQuestion = "Consensus change for: " + Consensus::sConsensusParamsDesc[(Consensus::ConsensusParamsPos)nMin];

    strDZeel.pushKV("q",sQuestion);
    strDZeel.pushKV("a",answers);
    strDZeel.pushKV("m",nMin);
    strDZeel.pushKV("n",nMax);
    strDZeel.pushKV("v",(uint64_t)nVersion);

    wtx.strDZeel = strDZeel.write();
    wtx.nCustomVersion = CTransaction::CONSULTATION_VERSION;

    if(wtx.strDZeel.length() > 1024)
        throw JSONRPCError(RPC_TYPE_ERROR, "String too long");

    EnsureWalletIsUnlocked();
    SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, false, true, fDump);

    if (!fDump)
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("hash",wtx.GetHash().GetHex());
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
    else
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("raw",EncodeHexTx(wtx));
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
}

UniValue proposecombinedconsensuschange(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 2)
        throw std::runtime_error(
                "proposecombinedconsensuschange parameters values ( fee dump_raw )\n"
            "\nCreates a proposal to the DAO for changing various consensus paremeters.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. parameter        (array, required) The parameter ids as specified in the output of the getconsensusparameters rpc command.\n"
            "2. value            (array, optional) The proposed values.\n"
            "3. fee              (numeric, optional) Contribution to the fund used as fee.\n"
            "4. dump_raw         (bool, optional) Dump the raw transaction instead of sending. Default: false\n"
            "\nResult:\n"
            "\"{ hash: consultation_id,\"            (string) The consultation id.\n"
            "\"  strDZeel: string }\"            (string) The attached strdzeel property.\n"
            "\nExamples:\n"
            + HelpExampleCli("proposecombinedconsensuschange", "[1] [10]")
                );

    if (!params[0].isArray() || !params[1].isArray())
        throw JSONRPCError(RPC_TYPE_ERROR, "Parameter and values should be arrays.");

    if (!IsDaoSuperEnabled(chainActive.Tip(), Params().GetConsensus()))
        throw JSONRPCError(RPC_TYPE_ERROR, "Combined voting is not enabled yet.");

    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    UniValue parameters = params[0].get_array();
    UniValue values = params[1].get_array();
    UniValue sValues(UniValue::VARR);

    CAmount nMinFee = GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_MIN_FEE, view) + (GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_ANSWER_MIN_FEE, view) * values.size());

    // Amount
    CAmount nAmount = params.size() >= 3 ? AmountFromValue(params[2]) : nMinFee;
    if (nAmount <= 0 || nAmount < nMinFee)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for fee.");

    if (parameters.size() != values.size())
        throw JSONRPCError(RPC_TYPE_ERROR, "You should specify the same amount of parameters and values.");

    bool fDump = params.size() == 4 ? params[3].getBool() : false;

    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    std::string sQuestion = "Consensus change for: ";
    std::vector<std::string> topics;

    for (size_t i = 0; i < parameters.size(); i++) {
        if (!parameters[i].isNum())
            throw JSONRPCError(RPC_TYPE_ERROR, "Parameters should be numbers");

        int64_t par = parameters[i].get_int64();

        if (par < Consensus::CONSENSUS_PARAM_VOTING_CYCLE_LENGTH || par >= Consensus::MAX_CONSENSUS_PARAMS)
            throw JSONRPCError(RPC_TYPE_ERROR, "Wrong parameter id");

        topics.push_back(Consensus::sConsensusParamsDesc[(Consensus::ConsensusParamsPos)par]);

        uint64_t decrement = (par == Consensus::CONSENSUS_PARAM_PROPOSAL_MAX_VOTING_CYCLES || par == Consensus::CONSENSUS_PARAM_PAYMENT_REQUEST_MAX_VOTING_CYCLES) ? 1 : 0;

        if (!values[i].isNum())
            throw JSONRPCError(RPC_TYPE_ERROR, "Values must be numbers");
        sValues.push_back(std::to_string(values[i].get_int64()-decrement));
    }

    int64_t nMax = 1;

    UniValue strDZeel(UniValue::VOBJ);
    uint64_t nVersion = CConsultation::BASE_VERSION | CConsultation::MORE_ANSWERS_VERSION | CConsultation::CONSENSUS_PARAMETER_VERSION | CConsultation::SUPER_VERSION;

    if (IsExcludeEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CConsultation::EXCLUDE_VERSION;

    strDZeel.pushKV("q",sQuestion + boost::algorithm::join(topics, " + "));
    strDZeel.pushKV("a",sValues);
    strDZeel.pushKV("m",parameters);
    strDZeel.pushKV("n",nMax);
    strDZeel.pushKV("v",(uint64_t)nVersion);

    wtx.strDZeel = strDZeel.write();
    wtx.nCustomVersion = CTransaction::CONSULTATION_VERSION;

    if(wtx.strDZeel.length() > 1024)
        throw JSONRPCError(RPC_TYPE_ERROR, "String too long");

    EnsureWalletIsUnlocked();
    SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, false, true, fDump);

    if (!fDump)
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("hash",wtx.GetHash().GetHex());
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
    else
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("raw",EncodeHexTx(wtx));
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
}

UniValue createconsultation(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 1)
        throw std::runtime_error(
                "createconsultation \"question\" ( min max range fee dump_raw )\n"
            "\nCreates a consultation for the DAO. Min fee of " + FormatMoney(GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_MIN_FEE, view)) + "0DYNS is required.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"question\"       (string, required) The question of the new consultation.\n"
            "2. min              (numeric, optional) The minimum amount for the range. Only used if range equals true.\n"
            "3. max              (numeric, optional) The maximum amount of answers a block can vote for.\n"
            "4. range            (bool, optional) The consultation answers are exclusively in the range min-max.\n"
            "5. fee              (numeric, optional) Contribution to the fund used as fee.\n"
            "6. dump_raw         (bool, optional) Dump the raw transaction instead of sending. Default: false\n"
            "\nResult:\n"
            "\"{ hash: consultation_id,\"            (string) The consultation id.\n"
            "\"  strDZeel: string }\"            (string) The attached strdzeel property.\n"
            "\nExamples:\n"
            + HelpExampleCli("createconsultation", "\"Who should be the CEO of Stock? /s\" 1 1")
                + HelpExampleCli("createconsultation", "\"How much should Stock's CEO earn per month? /s\" 1000 5000 true")
                );

    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    bool fRange = params.size() >= 4 && params[3].isBool() ? params[3].getBool() : false;

    // Amount
    CAmount nAmount = params.size() >= (fRange ? 5 : 4) ? AmountFromValue(params[(fRange ? 4 : 3)]) : GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_MIN_FEE, view);
    if (nAmount <= 0 || nAmount < GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_MIN_FEE, view))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for fee");

    bool fDump = params.size() == (fRange ? 6 : 5) ? params[(fRange ? 5 : 4)].getBool() : false;

    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    int64_t nMin = fRange ? params[1].get_int64() : 0;
    int64_t nMax = params.size() > 1 ? (fRange ? params[2].get_int64() : params[1].get_int64()) : 1;

    if (!fRange && (nMax < 1 ||nMax > 16))
        throw JSONRPCError(RPC_TYPE_ERROR, "Wrong maximum");
    else if(fRange && !(nMin >= 0 && nMax < (uint64_t)VoteFlags::VOTE_ABSTAIN && nMax > nMin))
        throw JSONRPCError(RPC_TYPE_ERROR, "Wrong range");

    std::string sQuestion = params[0].get_str();

    UniValue strDZeel(UniValue::VOBJ);
    uint64_t nVersion = CConsultation::BASE_VERSION | CConsultation::MORE_ANSWERS_VERSION;

    if (fRange)
    {
        nVersion |= CConsultation::ANSWER_IS_A_RANGE_VERSION;
        nVersion &= ~CConsultation::MORE_ANSWERS_VERSION;
    }

    if (IsExcludeEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CConsultation::EXCLUDE_VERSION;

    strDZeel.pushKV("q",sQuestion);
    strDZeel.pushKV("m",nMin);
    strDZeel.pushKV("n",nMax);
    strDZeel.pushKV("v",(uint64_t)nVersion);

    wtx.strDZeel = strDZeel.write();
    wtx.nCustomVersion = CTransaction::CONSULTATION_VERSION;

    if(wtx.strDZeel.length() > 1024)
        throw JSONRPCError(RPC_TYPE_ERROR, "String too long");

    EnsureWalletIsUnlocked();
    SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, false, true, fDump);

    if (!fDump)
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("hash",wtx.GetHash().GetHex());
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
    else
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("raw",EncodeHexTx(wtx));
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
}

UniValue createconsultationwithanswers(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 2)
        throw std::runtime_error(
                "createconsultationwithanswers \"question\" \"[answers]\" ( maxanswers admitsanswerproposals fee dump_raw )\n"
            "\nCreates a consultation for the DAO. Min fee of " + FormatMoney(GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_MIN_FEE, view)) + "0DYNS is required.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"question\"            (string, required) The question of the new consultation.\n"
            "2.  \"[answers]\"          (array of strings, required) An array of strings with the proposed answers.\n"
            "3. maxanswers            (numeric, optional) The maximum amount of answers a block can vote for at the same time.\n"
            "4. admitsanswerproposals (bool, optional) Stakers are allowed to propose new answers.\n"
            "5. fee                   (numeric, optional) Contribution to the fund used as fee.\n"
            "6. dump_raw              (bool, optional) Dump the raw transaction instead of sending. Default: false\n"
            "\nResult:\n"
            "\"{ hash: consultation_id,\"            (string) The consultation id.\n"
            "\"  strDZeel: string }\"            (string) The attached strdzeel property.\n"
            "\nExamples:\n"
            + HelpExampleCli("createconsultationwithanswers", "\"Who should be the CEO of Stock? /s\" \"[\\\"Craig Wright\\\",\\\"Loomdart\\\"]\"")
                );

    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    bool fRange = false;
    UniValue answers = params[1].get_array();
    CAmount nMinFee = answers.size() * GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_ANSWER_MIN_FEE, view) + GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_MIN_FEE, view);
    CAmount nAmount = params.size() >= 5 ? AmountFromValue(params[4]) : nMinFee;

    // Amount
    if (nAmount <= 0 || nAmount < nMinFee)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for fee");

    bool fDump = params.size() >= 6 ? params[5].getBool() : false;

    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    int64_t nMin = 0;
    int64_t nMax = params.size() >= 3 ? params[2].get_int64() : 1;

    if (nMax > 16)
        throw JSONRPCError(RPC_TYPE_ERROR, "Wrong maximum");

    bool fAdmitsAnswers = params.size() >= 4 ? params[3].get_bool() : true;
    std::string sQuestion = params[0].get_str();

    UniValue strDZeel(UniValue::VOBJ);
    uint64_t nVersion = CConsultation::BASE_VERSION;

    if (IsExcludeEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CConsultation::EXCLUDE_VERSION;

    if (fAdmitsAnswers)
        nVersion |= CConsultation::MORE_ANSWERS_VERSION;
    else if (answers.size() == 1)
        throw JSONRPCError(RPC_TYPE_ERROR, "You must add at least 2 answers if no other answers can be proposed");

    strDZeel.pushKV("q",sQuestion);
    strDZeel.pushKV("m",nMin);
    strDZeel.pushKV("a",answers);
    strDZeel.pushKV("n",nMax);
    strDZeel.pushKV("v",(uint64_t)nVersion);

    wtx.strDZeel = strDZeel.write();
    wtx.nCustomVersion = CTransaction::CONSULTATION_VERSION;

    if(wtx.strDZeel.length() > 1024)
        throw JSONRPCError(RPC_TYPE_ERROR, "String too long");

    EnsureWalletIsUnlocked();
    SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, false, true, fDump);

    if (!fDump)
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("hash",wtx.GetHash().GetHex());
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
    else
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("raw",EncodeHexTx(wtx));
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
}

std::string random_string( size_t length )
{
    auto randchar = []() -> char
    {
        const char charset[] =
                "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[ rand() % max_index ];
    };
    std::string str(length,0);
    std::generate_n( str.begin(), length, randchar );
    return str;
}

UniValue createpaymentrequest(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3)
        throw std::runtime_error(
                "createpaymentrequest \"hash\" \"amount\" \"id\" ( fee dump_raw )\n"
            "\nCreates a proposal to withdraw funds from the community fund. Fee: 0.0001 0DYNS\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"hash\"               (string, required) The hash of the proposal from which you want to withdraw funds. It must be approved.\n"
            "2. \"amount\"             (numeric or string, required) The amount in " + CURRENCY_UNIT + " to withdraw. eg 10\n"
            "3. \"id\"                 (string, required) Unique id to identify the payment request\n"
            "4. dump_raw               (bool, optional) Dump the raw transaction instead of sending. Default: false\n"
            "\nResult:\n"
            "\"{ hash: prequestid,\"             (string) The payment request id.\n"
            "\"  strDZeel: string }\"            (string) The attached strdzeel property.\n"
            "\nExamples:\n"
            + HelpExampleCli("createpaymentrequest", "\"196a4c2115d3c1c1dce1156eb2404ad77f3c5e9f668882c60cb98d638313dbd3\" 1000 \"Invoice March 2017\"")
                );

    if (!Params().GetConsensus().fDaoClientActivated)
        throw JSONRPCError(RPC_WALLET_ERROR, "This command is temporarily disabled");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CProposal proposal;
    CStateViewCache view(pcoinsTip);

    if(!view.GetProposal(uint256S(params[0].get_str()), proposal))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid proposal hash.");

    if(proposal.GetLastState() != DAOFlags::ACCEPTED)
        throw JSONRPCError(RPC_TYPE_ERROR, "Proposal has not been accepted.");

    CStockAddress address(proposal.GetOwnerAddress());

    if(!address.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Address of the proposal is not a valid Stock address.");

    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key.");

    EnsureWalletIsUnlocked();

    CKey key;
    if (!pwalletMain->GetKey(keyID, key))
        throw JSONRPCError(RPC_WALLET_ERROR, "You are not the owner of the proposal. Can't find the private key.");

    CAmount nReqAmount = AmountFromValue(params[1]);
    std::string id = params[2].get_str();

    // Amount
    CAmount nAmount = params.size() == 4 ? AmountFromValue(params[3]) : GetConsensusParameter(Consensus::CONSENSUS_PARAM_PAYMENT_REQUEST_MIN_FEE, view);
    if (nAmount < 0 || nAmount < GetConsensusParameter(Consensus::CONSENSUS_PARAM_PAYMENT_REQUEST_MIN_FEE, view))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for fee");

    bool fDump = params.size() == 5 ? params[4].getBool() : false;

    std::string sRandom = random_string(16);

    std::string Secret = sRandom + "I kindly ask to withdraw " +
            std::to_string(nReqAmount) + "0DYNS from the proposal " +
            proposal.hash.ToString() + ". Payment request id: " + id;

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << Secret;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed.");

    std::string Signature = EncodeBase64(&vchSig[0], vchSig.size());

    CStateViewCache coins(pcoinsTip);

    if (nReqAmount <= 0 || nReqAmount > proposal.GetAvailable(coins, true))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount.");

    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    UniValue strDZeel(UniValue::VOBJ);
    uint64_t nVersion = CPaymentRequest::BASE_VERSION;

    if (IsReducedCFundQuorumEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CPaymentRequest::REDUCED_QUORUM_VERSION;

    if (IsDAOEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CPaymentRequest::ABSTAIN_VOTE_VERSION;

    if (IsExcludeEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CPaymentRequest::EXCLUDE_VERSION;

    if (proposal.IsSuper() && IsDaoSuperEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CPaymentRequest::SUPER_VERSION;;

    strDZeel.pushKV("h",params[0].get_str());
    strDZeel.pushKV("n",nReqAmount);
    strDZeel.pushKV("s",Signature);
    strDZeel.pushKV("r",sRandom);
    strDZeel.pushKV("i",id);
    strDZeel.pushKV("v",(uint64_t)nVersion);

    wtx.strDZeel = strDZeel.write();
    wtx.nCustomVersion = CTransaction::PAYMENT_REQUEST_VERSION;

    if(wtx.strDZeel.length() > 1024)
        throw JSONRPCError(RPC_TYPE_ERROR, "String too long");

    SendMoney(address.Get(), 10000, fSubtractFeeFromAmount, wtx, false, true, fDump);

    if (!fDump)
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("hash",wtx.GetHash().GetHex());
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
    else
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("raw",EncodeHexTx(wtx));
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
}

UniValue proposeanswer(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    CStateViewCache view(pcoinsTip);

    if (fHelp || params.size() < 2)
        throw std::runtime_error(
                "proposeanswer \"hash\" \"answer\" ( fee dump_raw )\n"
            "\nProposes an answer for an already existing consultation of the DAO. Min fee of " + FormatMoney(GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_ANSWER_MIN_FEE, view)) + "0DYNS is required.\n"
            + HelpRequiringPassphrase() +
                "\nArguments:\n"
            "1. \"hash\"         (string, required) The hash of the already existing consultation.\n"
            "2. \"answer\"       (string, required) The proposed answer.\n"
            "3. fee              (numeric, optional) Contribution to the fund used as fee.\n"
            "4. dump_raw         (bool, optional) Dump the raw transaction instead of sending. Default: false\n"
            "\nResult:\n"
            "\"{ hash: consultation_id,\"        (string) The consultation id.\n"
            "\"  strDZeel: string }\"            (string) The attached strdzeel property.\n"
            "\nExamples:\n"
            + HelpExampleCli("proposeanswer", "\"196a4c2115d3c1c1dce1156eb2404ad77f3c5e9f668882c60cb98d638313dbd3\" \"Vitalik Buterin\"")
                + HelpExampleCli("proposeanswer", "\"196a4c2115d3c1c1dce1156eb2404ad77f3c5e9f668882c60cb98d638313dbd3\" \"Satoshi Nakamoto\"")
                + HelpExampleCli("proposeanswer", "\"196a4c2115d3c1c1dce1156eb2404ad77f3c5e9f668882c60cb98d638313dbd3\" \"Charlie Lee\"")
                + HelpExampleCli("proposeanswer", "\"196a4c2115d3c1c1dce1156eb2404ad77f3c5e9f668882c60cb98d638313dbd3\" \"Riccardo Fluffypony\"")
                );

    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    // Amount
    CAmount nAmount = params.size() >= 3 ? AmountFromValue(params[2]) : GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_ANSWER_MIN_FEE, view);
    if (nAmount <= 0 || nAmount < GetConsensusParameter(Consensus::CONSENSUS_PARAM_CONSULTATION_ANSWER_MIN_FEE, view))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for fee");

    CConsultation consultation;

    if(!view.GetConsultation(uint256S(params[0].get_str()), consultation))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid consultation.");

    if(!consultation.CanHaveNewAnswers())
        throw JSONRPCError(RPC_TYPE_ERROR, "The consultation does not admit new answers.");

    uint64_t nVersion = CConsultationAnswer::BASE_VERSION;

    if (IsExcludeEnabled(chainActive.Tip(), Params().GetConsensus()))
        nVersion |= CConsultationAnswer::EXCLUDE_VERSION;

    std::string sAnswer = "";
    UniValue vAnswer(UniValue::VARR);
    if (consultation.IsAboutConsensusParameter())
    {
        if (consultation.IsSuper())
        {
            nVersion |= CConsultationAnswer::SUPER_VERSION;
            auto nValue = params[1].get_array();
            auto parameters = consultation.GetParameters();

            for (size_t i = 0; i < nValue.size(); i++)
            {
                auto sub = (parameters[i] == Consensus::CONSENSUS_PARAM_PROPOSAL_MAX_VOTING_CYCLES || parameters[i] == Consensus::CONSENSUS_PARAM_PAYMENT_REQUEST_MAX_VOTING_CYCLES) ? 1 : 0;
                vAnswer.push_back(std::to_string(nValue[i].get_int64()));
            }
        } else {
            int64_t nValue = params[1].get_int64();

            if (consultation.nMin == Consensus::CONSENSUS_PARAM_PROPOSAL_MAX_VOTING_CYCLES || consultation.nMin == Consensus::CONSENSUS_PARAM_PAYMENT_REQUEST_MAX_VOTING_CYCLES)
            {
                nValue--;
            }

            sAnswer = std::to_string(nValue);
        }
    }
    else
    {
        sAnswer = params[1].get_str();
    }

    bool fDump = params.size() == 4 ? params[3].getBool() : false;

    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;

    UniValue strDZeel(UniValue::VOBJ);

    strDZeel.pushKV("h",params[0].get_str());
    strDZeel.pushKV("a",consultation.IsAboutConsensusParameter()&&consultation.IsSuper()?vAnswer:sAnswer);
    strDZeel.pushKV("v",(uint64_t)nVersion);

    wtx.strDZeel = strDZeel.write();
    wtx.nCustomVersion = CTransaction::ANSWER_VERSION;

    if(wtx.strDZeel.length() > 255)
        throw JSONRPCError(RPC_TYPE_ERROR, "String too long");

    SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, false, true, fDump);

    if (!fDump)
    {
        UniValue ret(UniValue::VOBJ);
        CConsultationAnswer answer;

        TxToConsultationAnswer(wtx.strDZeel, wtx.GetHash(), uint256(), answer);

        ret.pushKV("hash", answer.hash.ToString());
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
    else
    {
        UniValue ret(UniValue::VOBJ);

        ret.pushKV("raw",EncodeHexTx(wtx));
        ret.pushKV("strDZeel",wtx.strDZeel);
        return ret;
    }
}


UniValue donatefund(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
                "donatefund \"amount\" ( subtractfeefromamount )\n"
                "\nDonates an amount to the community fund.\n"
                + HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1. \"amount\"      (numeric or string, required) The amount in " + CURRENCY_UNIT + " to donate. eg 0.1\n"
                                                                                                    "2. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
                                                                                                    "                             The fund will receive less stocks than you enter in the amount field.\n"
                                                                                                    "\nResult:\n"
                                                                                                    "\"transactionid\"  (string) The transaction id.\n"
                                                                                                    "\nExamples:\n"
                + HelpExampleCli("donatefund", "0.1")
                + HelpExampleCli("donatefund", "0.1 true")

                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CStockAddress address("NQFqqMUD55ZV3PJEJZtaKCsQmjLT6JkjvJ"); // Dummy address

    // Amount
    CAmount nAmount = AmountFromValue(params[0]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    CWalletTx wtx;
    bool fSubtractFeeFromAmount = false;
    if (params.size() == 2)
        fSubtractFeeFromAmount = params[1].get_bool();

    EnsureWalletIsUnlocked();

    SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx, false, true);

    return wtx.GetHash().GetHex();
}

UniValue listaddressgroupings(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp)
        throw std::runtime_error(
                "listaddressgroupings\n"
                "\nLists groups of addresses which have had their common ownership\n"
                "made public by common use as inputs or as the resulting change\n"
                "in past transactions\n"
                "\nResult:\n"
                "[\n"
                "  [\n"
                "    [\n"
                "      \"stockaddress\",     (string) The stock address\n"
                "      amount,                 (numeric) The amount in " + CURRENCY_UNIT + "\n"
                                                                                           "      \"account\"             (string, optional) The account (DEPRECATED)\n"
                                                                                           "    ]\n"
                                                                                           "    ,...\n"
                                                                                           "  ]\n"
                                                                                           "  ,...\n"
                                                                                           "]\n"
                                                                                           "\nExamples:\n"
                + HelpExampleCli("listaddressgroupings", "")
                + HelpExampleRpc("listaddressgroupings", "")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue jsonGroupings(UniValue::VARR);
    std::map<CTxDestination, CAmount> balances = pwalletMain->GetAddressBalances();
    for(std::set<CTxDestination> grouping: pwalletMain->GetAddressGroupings())
    {
        UniValue jsonGrouping(UniValue::VARR);
        for(CTxDestination address: grouping)
        {
            UniValue addressInfo(UniValue::VARR);
            addressInfo.push_back(CStockAddress(address).ToString());
            addressInfo.push_back(ValueFromAmount(balances[address]));
            {
                if (pwalletMain->mapAddressBook.find(CStockAddress(address).Get()) != pwalletMain->mapAddressBook.end())
                    addressInfo.push_back(pwalletMain->mapAddressBook.find(CStockAddress(address).Get())->second.name);
            }
            jsonGrouping.push_back(addressInfo);
        }
        jsonGroupings.push_back(jsonGrouping);
    }
    return jsonGroupings;
}

UniValue signmessage(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 2)
        throw std::runtime_error(
                "signmessage \"stockaddress\" \"message\"\n"
                "\nSign a message with the private key of an address"
                + HelpRequiringPassphrase() + "\n"
                                              "\nArguments:\n"
                                              "1. \"stockaddress\"  (string, required) The stock address to use for the private key.\n"
                                              "2. \"message\"         (string, required) The message to create a signature of.\n"
                                              "\nResult:\n"
                                              "\"signature\"          (string) The signature of the message encoded in base 64\n"
                                              "\nExamples:\n"
                                              "\nUnlock the wallet for 30 seconds\n"
                + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
                "\nCreate the signature\n"
                + HelpExampleCli("signmessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"my message\"") +
                "\nVerify the signature\n"
                + HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"signature\" \"my message\"") +
                "\nAs json rpc\n"
                + HelpExampleRpc("signmessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", \"my message\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strAddress = params[0].get_str();
    std::string strMessage = params[1].get_str();

    CStockAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    CKey key;
    if (!pwalletMain->GetKey(keyID, key))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key not available");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return EncodeBase64(&vchSig[0], vchSig.size());
}

UniValue getreceivedbyaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
                "getreceivedbyaddress \"stockaddress\" ( minconf )\n"
                "\nReturns the total amount received by the given stockaddress in transactions with at least minconf confirmations.\n"
                "\nArguments:\n"
                "1. \"stockaddress\"  (string, required) The stock address for transactions.\n"
                "2. minconf             (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
                "\nResult:\n"
                "amount   (numeric) The total amount in " + CURRENCY_UNIT + " received at this address.\n"
                                                                            "\nExamples:\n"
                                                                            "\nThe amount from transactions with at least 1 confirmation\n"
                + HelpExampleCli("getreceivedbyaddress", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\"") +
                "\nThe amount including unconfirmed transactions, zero confirmations\n"
                + HelpExampleCli("getreceivedbyaddress", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" 0") +
                "\nThe amount with at least 6 confirmation, very safe\n"
                + HelpExampleCli("getreceivedbyaddress", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" 6") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("getreceivedbyaddress", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", 6")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Stock address
    CStockAddress address = CStockAddress(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address");
    CScript scriptPubKey = GetScriptForDestination(address.Get());
    if (!IsMine(*pwalletMain, scriptPubKey))
        return ValueFromAmount(0);

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Tally
    CAmount nAmount = 0;
    for (std::map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || wtx.IsCoinStake() || !CheckFinalTx(wtx))
            continue;

        for(const CTxOut& txout: wtx.vout)
            if (txout.scriptPubKey == scriptPubKey)
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
    }

    return  ValueFromAmount(nAmount);
}


UniValue getreceivedbyaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
                "getreceivedbyaccount \"account\" ( minconf )\n"
                "\nDEPRECATED. Returns the total amount received by addresses with <account> in transactions with at least [minconf] confirmations.\n"
                "\nArguments:\n"
                "1. \"account\"      (string, required) The selected account, may be the default account using \"\".\n"
                "2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
                "\nResult:\n"
                "amount              (numeric) The total amount in " + CURRENCY_UNIT + " received for this account.\n"
                                                                                       "\nExamples:\n"
                                                                                       "\nAmount received by the default account with at least 1 confirmation\n"
                + HelpExampleCli("getreceivedbyaccount", "\"\"") +
                "\nAmount received at the tabby account including unconfirmed amounts with zero confirmations\n"
                + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 0") +
                "\nThe amount with at least 6 confirmation, very safe\n"
                + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 6") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("getreceivedbyaccount", "\"tabby\", 6")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Get the set of pub keys assigned to account
    std::string strAccount = AccountFromValue(params[0]);
    std::set<CTxDestination> setAddress = pwalletMain->GetAccountAddresses(strAccount);

    // Tally
    CAmount nAmount = 0;
    for (std::map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || wtx.IsCoinStake() || !CheckFinalTx(wtx))
            continue;

        for(const CTxOut& txout: wtx.vout)
        {
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*pwalletMain, address) && setAddress.count(address))
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
        }
    }

    return ValueFromAmount(nAmount);
}


UniValue getbalance(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw std::runtime_error(
                "getbalance ( \"account\" minconf includeWatchonly )\n"
                "\nIf account is not specified, returns the server's total available balance.\n"
                "If account is specified (DEPRECATED), returns the balance in the account.\n"
                "Note that the account \"\" is not the same as leaving the parameter out.\n"
                "The server total may be different to the balance in the default \"\" account.\n"
                "\nArguments:\n"
                "1. \"account\"      (string, optional) DEPRECATED. The selected account, or \"*\" for entire wallet. It may be the default account using \"\".\n"
                "2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
                "3. includeWatchonly (bool, optional, default=false) Also include balance in watchonly addresses (see 'importaddress')\n"
                "\nResult:\n"
                "amount              (numeric) The total amount in " + CURRENCY_UNIT + " received for this account.\n"
                                                                                       "\nExamples:\n"
                                                                                       "\nThe total amount in the wallet\n"
                + HelpExampleCli("getbalance", "") +
                "\nThe total amount in the wallet at least 5 blocks confirmed\n"
                + HelpExampleCli("getbalance", "\"*\" 6") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("getbalance", "\"*\", 6")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (params.size() == 0)
        return  ValueFromAmount(pwalletMain->GetBalance());

    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();
    isminefilter filter = ISMINE_SPENDABLE;
    if(params.size() > 2)
        if(params[2].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    if (params[0].get_str() == "private") {
        return  ValueFromAmount(pwalletMain->GetPrivateBalance());
    } else if (params[0].get_str() == "*") {
        // Calculate total balance a different way from GetBalance()
        // (GetBalance() sums up all unspent TxOuts)
        // getbalance and "getbalance * 1 true" should return the same number
        CAmount nBalance = 0;
        for (std::map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
        {
            const CWalletTx& wtx = (*it).second;
            if (!CheckFinalTx(wtx) || wtx.GetBlocksToMaturity() > 0 || wtx.GetDepthInMainChain() < 0)
                continue;

            CAmount allFee;
            std::string strSentAccount;
            std::list<COutputEntry> listReceived;
            std::list<COutputEntry> listSent;
            wtx.GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);
            if (wtx.GetDepthInMainChain() >= nMinDepth)
            {
                for(const COutputEntry& r: listReceived)
                    nBalance += r.amount;
            }
            for(const COutputEntry& s: listSent)
                nBalance -= s.amount;
            nBalance -= allFee;
        }
        return  ValueFromAmount(nBalance);
    }

    std::string strAccount = AccountFromValue(params[0]);

    CAmount nBalance = pwalletMain->GetAccountBalance(strAccount, nMinDepth, filter);

    return ValueFromAmount(nBalance);
}

UniValue getunconfirmedbalance(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 0)
        throw std::runtime_error(
                "getunconfirmedbalance\n"
                "Returns the server's total unconfirmed balance\n");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    return ValueFromAmount(pwalletMain->GetUnconfirmedBalance());
}


UniValue movecmd(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 5)
        throw std::runtime_error(
                "move \"fromaccount\" \"toaccount\" amount ( minconf \"comment\" )\n"
                "\nDEPRECATED. Move a specified amount from one account in your wallet to another.\n"
                "\nArguments:\n"
                "1. \"fromaccount\"   (string, required) The name of the account to move funds from. May be the default account using \"\".\n"
                "2. \"toaccount\"     (string, required) The name of the account to move funds to. May be the default account using \"\".\n"
                "3. amount            (numeric) Quantity of " + CURRENCY_UNIT + " to move between accounts.\n"
                                                                                "4. minconf           (numeric, optional, default=1) Only use funds with at least this many confirmations.\n"
                                                                                "5. \"comment\"       (string, optional) An optional comment, stored in the wallet only.\n"
                                                                                "\nResult:\n"
                                                                                "true|false           (boolean) true if successful.\n"
                                                                                "\nExamples:\n"
                                                                                "\nMove 0.01 " + CURRENCY_UNIT + " from the default account to the account named tabby\n"
                + HelpExampleCli("move", "\"\" \"tabby\" 0.01") +
                "\nMove 0.01 " + CURRENCY_UNIT + " timotei to akiko with a comment and funds have 6 confirmations\n"
                + HelpExampleCli("move", "\"timotei\" \"akiko\" 0.01 6 \"happy birthday!\"") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("move", "\"timotei\", \"akiko\", 0.01, 6, \"happy birthday!\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string strFrom = AccountFromValue(params[0]);
    std::string strTo = AccountFromValue(params[1]);
    CAmount nAmount = AmountFromValue(params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
    if (params.size() > 3)
        // unused parameter, used to be nMinDepth, keep type-checking it though
        (void)params[3].get_int();
    std::string strComment;
    if (params.size() > 4)
        strComment = params[4].get_str();

    if (!pwalletMain->AccountMove(strFrom, strTo, nAmount, strComment))
        throw JSONRPCError(RPC_DATABASE_ERROR, "database error");

    return true;
}


UniValue sendfrom(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 6)
        throw std::runtime_error(
                "sendfrom \"fromaccount\" \"tostockaddress\" amount ( minconf \"comment\" \"comment-to\" )\n"
                "\nDEPRECATED (use sendtoaddress). Sent an amount from an account to a stock address."
                + HelpRequiringPassphrase() + "\n"
                                              "\nArguments:\n"
                                              "1. \"fromaccount\"       (string, required) The name of the account to send funds from. May be the default account using \"\".\n"
                                              "2. \"tostockaddress\"  (string, required) The stock address to send funds to.\n"
                                              "3. amount                (numeric or string, required) The amount in " + CURRENCY_UNIT + " (transaction fee is added on top).\n"
                                                                                                                                        "4. minconf               (numeric, optional, default=1) Only use funds with at least this many confirmations.\n"
                                                                                                                                        "5. \"comment\"           (string, optional) A comment used to store what the transaction is for. \n"
                                                                                                                                        "                                     This is not part of the transaction, just kept in your wallet.\n"
                                                                                                                                        "6. \"comment-to\"        (string, optional) An optional comment to store the name of the person or organization \n"
                                                                                                                                        "                                     to which you're sending the transaction. This is not part of the transaction, \n"
                                                                                                                                        "                                     it is just kept in your wallet.\n"
                                                                                                                                        "\nResult:\n"
                                                                                                                                        "\"transactionid\"        (string) The transaction id.\n"
                                                                                                                                        "\nExamples:\n"
                                                                                                                                        "\nSend 0.01 " + CURRENCY_UNIT + " from the default account to the address, must have at least 1 confirmation\n"
                + HelpExampleCli("sendfrom", "\"\" \"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.01") +
                "\nSend 0.01 from the tabby account to the given address, funds must have at least 6 confirmations\n"
                + HelpExampleCli("sendfrom", "\"tabby\" \"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.01 6 \"donation\" \"seans outpost\"") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("sendfrom", "\"tabby\", \"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.01, 6, \"donation\", \"seans outpost\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string strAccount = AccountFromValue(params[0]);
    CStockAddress address(params[1].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address");
    CAmount nAmount = AmountFromValue(params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
    int nMinDepth = 1;
    if (params.size() > 3)
        nMinDepth = params[3].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 4 && !params[4].isNull() && !params[4].get_str().empty())
        wtx.mapValue["comment"] = params[4].get_str();
    if (params.size() > 5 && !params[5].isNull() && !params[5].get_str().empty())
        wtx.mapValue["to"]      = params[5].get_str();

    EnsureWalletIsUnlocked();

    // Check funds
    CAmount nBalance = pwalletMain->GetAccountBalance(strAccount, nMinDepth, ISMINE_SPENDABLE);
    if (nAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    SendMoney(address.Get(), nAmount, false, wtx, false);

    return wtx.GetHash().GetHex();
}


UniValue sendmany(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 5)
        throw std::runtime_error(
                "sendmany \"fromaccount\" {\"address\":amount,...} ( minconf \"comment\" [\"address\",...] )\n"
                "\nSend multiple times. Amounts are double-precision floating point numbers."
                + HelpRequiringPassphrase() + "\n"
                                              "\nArguments:\n"
                                              "1. \"fromaccount\"         (string, required) DEPRECATED. The account to send the funds from. Should be \"\" for the default account\n"
                                              "2. \"amounts\"             (string, required) A json object with addresses and amounts\n"
                                              "    {\n"
                                              "      \"address\":amount   (numeric or string) The stock address is the key, the numeric amount (can be string) in " + CURRENCY_UNIT + " is the value\n"
                                                                                                                                                                                        "      ,...\n"
                                                                                                                                                                                        "    }\n"
                                                                                                                                                                                        "3. minconf                 (numeric, optional, default=1) Only use the balance confirmed at least this many times.\n"
                                                                                                                                                                                        "4. \"comment\"             (string, optional) A comment\n"
                                                                                                                                                                                        "5. subtractfeefromamount   (string, optional) A json array with addresses.\n"
                                                                                                                                                                                        "                           The fee will be equally deducted from the amount of each selected address.\n"
                                                                                                                                                                                        "                           Those recipients will receive less stocks than you enter in their corresponding amount field.\n"
                                                                                                                                                                                        "                           If no addresses are specified here, the sender pays the fee.\n"
                                                                                                                                                                                        "    [\n"
                                                                                                                                                                                        "      \"address\"            (string) Subtract fee from this address\n"
                                                                                                                                                                                        "      ,...\n"
                                                                                                                                                                                        "    ]\n"
                                                                                                                                                                                        "\nResult:\n"
                                                                                                                                                                                        "\"transactionid\"          (string) The transaction id for the send. Only 1 transaction is created regardless of \n"
                                                                                                                                                                                        "                                    the number of addresses.\n"
                                                                                                                                                                                        "\nExamples:\n"
                                                                                                                                                                                        "\nSend two amounts to two different addresses:\n"
                + HelpExampleCli("sendmany", "\"\" \"{\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\":0.01,\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\"") +
                "\nSend two amounts to two different addresses setting the confirmation and comment:\n"
                + HelpExampleCli("sendmany", "\"\" \"{\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\":0.01,\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\" 6 \"testing\"") +
                "\nSend two amounts to two different addresses, subtract fee from amount:\n"
                + HelpExampleCli("sendmany", "\"\" \"{\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\":0.01,\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\" 1 \"\" \"[\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\",\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\"]\"") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("sendmany", "\"\", \"{\\\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\\\":0.01,\\\"1353tsE8YMTA4EuV7dgUXGjNFf9KpVvKHz\\\":0.02}\", 6, \"testing\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string strAccount = AccountFromValue(params[0]);
    UniValue sendTo = params[1].get_obj();
    int nMinDepth = 1;
    if (params.size() > 2)
        nMinDepth = params[2].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
        wtx.mapValue["comment"] = params[3].get_str();

    UniValue subtractFeeFromAmount(UniValue::VARR);
    if (params.size() > 4)
        subtractFeeFromAmount = params[4].get_array();

    std::set<CStockAddress> setAddress;
    std::vector<CRecipient> vecSend;

    CAmount totalAmount = 0;
    std::vector<std::string> keys = sendTo.getKeys();

    bool fPrivate = false;
    bool fAnyBLSCT = false;

    for(const std::string& name_: keys)
    {
        CStockAddress address(name_);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Stock address: ")+name_);

        if (setAddress.count(address))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ")+name_);
        setAddress.insert(address);

        CScript scriptPubKey = GetScriptForDestination(address.Get());
        CAmount nAmount = AmountFromValue(sendTo[name_]);
        if (nAmount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
        totalAmount += nAmount;

        bool fSubtractFeeFromAmount = false;
        for (unsigned int idx = 0; idx < subtractFeeFromAmount.size(); idx++) {
            const UniValue& addr = subtractFeeFromAmount[idx];
            if (addr.get_str() == name_)
                fSubtractFeeFromAmount = true;
        }

        bool fBLSCT = address.Get().type() == typeid(blsctDoublePublicKey);
        CRecipient recipient = {scriptPubKey, nAmount, fSubtractFeeFromAmount, fBLSCT};
        if (fBLSCT)
        {
            bls::G1Element vk, sk;
            blsctDoublePublicKey dk = boost::get<blsctDoublePublicKey>(address.Get());

            if (!dk.GetSpendKey(sk) || !dk.GetViewKey(vk))
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");
            }

            recipient.sk = sk.Serialize();
            recipient.vk = vk.Serialize();
            recipient.sMemo = wtx.mapValue["comment"];
            fAnyBLSCT |= fBLSCT;
        }
        vecSend.push_back(recipient);
    }

    EnsureWalletIsUnlocked();

    // Check funds
    CAmount nBalance = pwalletMain->GetAccountBalance(strAccount, nMinDepth, ISMINE_SPENDABLE);
    if (totalAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    // Send
    CReserveKey keyChange(pwalletMain);
    CAmount nFeeRequired = 0;
    int nChangePosRet = -1;
    std::string strFailReason;
    std::vector<std::shared_ptr<CReserveBLSCTBlindingKey>> reserveBLSCTKey;

    if (fAnyBLSCT || fPrivate)
    {
        for (unsigned int i = 0; i < vecSend.size()+2; i++)
        {
            std::shared_ptr<CReserveBLSCTBlindingKey> rk(new CReserveBLSCTBlindingKey(pwalletMain));
            reserveBLSCTKey.insert(reserveBLSCTKey.begin(), std::move(rk));
        }
    }

    bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, reserveBLSCTKey, nFeeRequired, nChangePosRet, strFailReason, false);
    if (!fCreated)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
    if (!pwalletMain->CommitTransaction(wtx, keyChange, reserveBLSCTKey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");

    return wtx.GetHash().GetHex();
}

UniValue getnewprivateaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw std::runtime_error(
                "getnewprivateaddress ( account_id )\n"
                "\nReturns a new Stock private address for receiving payments.\n"
                "If 'account_id' is specified, it is associated with the account \n"
                "with the given index.\n"
                "\nArguments:\n"
                "1. \"account_id\"     (int, optional, default=0) The account id for the address to be linked to. If not provided, the default account 0 is used. It can also be set to 0 to represent the default account. The account does not need to exist before, it will be created if there is no account by the given id.\n"
                "\nResult:\n"
                "\"stockaddress\"    (string) The new stock address\n"
                "\nExamples:\n"
                + HelpExampleCli("getnewprivateaddress", "")
                + HelpExampleRpc("getnewprivateaddress", "")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    uint64_t account = 0;
    if (params.size() > 0)
        account = params[0].get_int64();

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpBLSCTSubAddressKeyPool(account);

    // Generate a new key that is added to wallet
    CKeyID keyID;
    if (!pwalletMain->GetBLSCTSubAddressKeyFromPool(account, keyID))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    blsctDoublePublicKey k;

    if (!pwalletMain->GetBLSCTSubAddressPublicKeys(keyID, k))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Could not calculate public key");

    std::pair<uint64_t, uint64_t> index;

    if (!pwalletMain->GetBLSCTSubAddressIndex(keyID, index))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Could not get subaddress index");

    pwalletMain->SetPrivateAddressBook(CStockAddress(k).ToString(), "blsct receive", std::to_string(index.first) + "/" + std::to_string(index.second));

    return CStockAddress(k).ToString();
}

// Defined in rpc/misc.cpp
extern CScript _createmultisig_redeemScript(const UniValue& params);

UniValue addmultisigaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 3)
    {
        std::string msg = "addmultisigaddress nrequired [\"key\",...] ( \"account\" )\n"
                     "\nAdd a nrequired-to-sign multisignature address to the wallet.\n"
                     "Each key is a Stock address or hex-encoded public key.\n"
                     "If 'account' is specified (DEPRECATED), assign address to that account.\n"

                     "\nArguments:\n"
                     "1. nrequired        (numeric, required) The number of required signatures out of the n keys or addresses.\n"
                     "2. \"keysobject\"   (string, required) A json array of stock addresses or hex-encoded public keys\n"
                     "     [\n"
                     "       \"address\"  (string) stock address or hex-encoded public key\n"
                     "       ...,\n"
                     "     ]\n"
                     "3. \"account\"      (string, optional) DEPRECATED. An account to assign the addresses to.\n"

                     "\nResult:\n"
                     "\"stockaddress\"  (string) A stock address associated with the keys.\n"

                     "\nExamples:\n"
                     "\nAdd a multisig address from 2 addresses\n"
                + HelpExampleCli("addmultisigaddress", "2 \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
                "\nAs json rpc call\n"
                + HelpExampleRpc("addmultisigaddress", "2, \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"")
                ;
        throw std::runtime_error(msg);
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string strAccount;
    if (params.size() > 2)
        strAccount = AccountFromValue(params[2]);

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);
    pwalletMain->AddCScript(inner);

    pwalletMain->SetAddressBook(innerID, strAccount, "send");
    return CStockAddress(innerID).ToString();
}

class Witnessifier : public boost::static_visitor<bool>
{
public:
    CScriptID result;

    bool operator()(const CNoDestination &dest) const { return false; }

    bool operator()(const blsctDoublePublicKey &dest) const { return false; }

    bool operator()(const std::pair<CKeyID, CKeyID> &dest) const { return false; }

    bool operator()(const std::pair<CKeyID, std::pair<CKeyID, CKeyID>> &dest) const { return false; }

    bool operator()(const CKeyID &keyID) {
        CPubKey pubkey;
        if (pwalletMain && pwalletMain->GetPubKey(keyID, pubkey)) {
            CScript basescript;
            basescript << ToByteVector(pubkey) << OP_CHECKSIG;
            CScript witscript = GetScriptForWitness(basescript);
            pwalletMain->AddCScript(witscript);
            result = CScriptID(witscript);
            return true;
        }
        return false;
    }

    bool operator()(const CScriptID &scriptID) {
        CScript subscript;
        if (pwalletMain && pwalletMain->GetCScript(scriptID, subscript)) {
            int witnessversion;
            std::vector<unsigned char> witprog;
            if (subscript.IsWitnessProgram(witnessversion, witprog)) {
                result = scriptID;
                return true;
            }
            CScript witscript = GetScriptForWitness(subscript);
            pwalletMain->AddCScript(witscript);
            result = CScriptID(witscript);
            return true;
        }
        return false;
    }
};

UniValue addwitnessaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 1)
    {
        std::string msg = "addwitnessaddress \"address\"\n"
                     "\nAdd a witness address for a script (with pubkey or redeemscript known).\n"
                     "It returns the witness script.\n"

                     "\nArguments:\n"
                     "1. \"address\"       (string, required) An address known to the wallet\n"

                     "\nResult:\n"
                     "\"witnessaddress\",  (string) The value of the new address (P2SH of witness script).\n"
                     "}\n"
                ;
        throw std::runtime_error(msg);
    }

    {
        LOCK(cs_main);
        if (!IsWitnessEnabled(chainActive.Tip(), Params().GetConsensus()) && !GetBoolArg("-walletprematurewitness", false)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Segregated witness not enabled on network");
        }
    }

    CStockAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Stock address");

    Witnessifier w;
    CTxDestination dest = address.Get();
    bool ret = boost::apply_visitor(w, dest);
    if (!ret) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Public key or redeemscript not known to wallet");
    }

    return CStockAddress(w.result).ToString();
}

struct tallyitem
{
    CAmount nAmount;
    int nConf;
    std::vector<uint256> txids;
    bool fIsWatchonly;
    tallyitem()
    {
        nAmount = 0;
        nConf = std::numeric_limits<int>::max();
        fIsWatchonly = false;
    }
};

UniValue ListReceived(const UniValue& params, bool fByAccounts)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    // Whether to include empty accounts
    bool fIncludeEmpty = false;
    if (params.size() > 1)
        fIncludeEmpty = params[1].get_bool();

    isminefilter filter = ISMINE_SPENDABLE;
    if(params.size() > 2)
        if(params[2].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    // Tally
    std::map<CStockAddress, tallyitem> mapTally;
    for (std::map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;

        if (wtx.IsCoinBase() || wtx.IsCoinStake() || !CheckFinalTx(wtx))
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < nMinDepth)
            continue;

        size_t i = -1;

        for(const CTxOut& txout: wtx.vout)
        {
            i++;

            CTxDestination address;
            if (!ExtractDestination(txout.scriptPubKey, address))
                continue;

            isminefilter mine = IsMine(*pwalletMain, address);
            if(!(mine & filter))
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue + (wtx.vAmounts.size() > i ? wtx.vAmounts[i] : 0);
            item.nConf = std::min(item.nConf, nDepth);
            item.txids.push_back(wtx.GetHash());
            if (mine & ISMINE_WATCH_ONLY)
                item.fIsWatchonly = true;
        }
    }

    // Reply
    UniValue ret(UniValue::VARR);
    std::map<std::string, tallyitem> mapAccountTally;
    for(const std::pair<CStockAddress, CAddressBookData>& item: pwalletMain->mapAddressBook)
    {
        const CStockAddress& address = item.first;
        const std::string& strAccount = item.second.name;
        std::map<CStockAddress, tallyitem>::iterator it = mapTally.find(address);
        if (it == mapTally.end() && !fIncludeEmpty)
            continue;

        if (strAccount == "blsct receive")
            continue;

        CAmount nAmount = 0;
        int nConf = std::numeric_limits<int>::max();
        bool fIsWatchonly = false;
        if (it != mapTally.end())
        {
            nAmount = (*it).second.nAmount;
            nConf = (*it).second.nConf;
            fIsWatchonly = (*it).second.fIsWatchonly;
        }

        if (fByAccounts)
        {
            tallyitem& item = mapAccountTally[strAccount];
            item.nAmount += nAmount;
            item.nConf = std::min(item.nConf, nConf);
            item.fIsWatchonly = fIsWatchonly;
        }
        else
        {
            UniValue obj(UniValue::VOBJ);
            if(fIsWatchonly)
                obj.pushKV("involvesWatchonly", true);
            obj.pushKV("address",       address.ToString());
            obj.pushKV("account",       strAccount);
            obj.pushKV("amount",        ValueFromAmount(nAmount));
            obj.pushKV("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf));
            if (!fByAccounts)
                obj.pushKV("label", strAccount);
            UniValue transactions(UniValue::VARR);
            if (it != mapTally.end())
            {
                for(const uint256& item: (*it).second.txids)
                {
                    transactions.push_back(item.GetHex());
                }
            }
            obj.pushKV("txids", transactions);
            ret.push_back(obj);
        }
    }

    if (fByAccounts)
    {
        for (std::map<std::string, tallyitem>::iterator it = mapAccountTally.begin(); it != mapAccountTally.end(); ++it)
        {
            CAmount nAmount = (*it).second.nAmount;
            int nConf = (*it).second.nConf;
            UniValue obj(UniValue::VOBJ);
            if((*it).second.fIsWatchonly)
                obj.pushKV("involvesWatchonly", true);
            obj.pushKV("account",       (*it).first);
            obj.pushKV("amount",        ValueFromAmount(nAmount));
            obj.pushKV("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf));
            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue listreceivedbyaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw std::runtime_error(
                "listreceivedbyaddress ( minconf includeempty includeWatchonly)\n"
                "\nList balances by receiving address.\n"
                "\nArguments:\n"
                "1. minconf       (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
                "2. includeempty  (bool, optional, default=false) Whether to include addresses that haven't received any payments.\n"
                "3. includeWatchonly (bool, optional, default=false) Whether to include watchonly addresses (see 'importaddress').\n"

                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"involvesWatchonly\" : true,        (bool) Only returned if imported addresses were involved in transaction\n"
                "    \"address\" : \"receivingaddress\",  (string) The receiving address\n"
                "    \"account\" : \"accountname\",       (string) DEPRECATED. The account of the receiving address. The default account is \"\".\n"
                "    \"amount\" : x.xxx,                  (numeric) The total amount in " + CURRENCY_UNIT + " received by the address\n"
                                                                                                            "    \"confirmations\" : n,               (numeric) The number of confirmations of the most recent transaction included\n"
                                                                                                            "    \"label\" : \"label\"                (string) A comment for the address/transaction, if any\n"
                                                                                                            "  }\n"
                                                                                                            "  ,...\n"
                                                                                                            "]\n"

                                                                                                            "\nExamples:\n"
                + HelpExampleCli("listreceivedbyaddress", "")
                + HelpExampleCli("listreceivedbyaddress", "6 true")
                + HelpExampleRpc("listreceivedbyaddress", "6, true, true")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    return ListReceived(params, false);
}

UniValue listreceivedbyaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw std::runtime_error(
                "listreceivedbyaccount ( minconf includeempty includeWatchonly)\n"
                "\nDEPRECATED. List balances by account.\n"
                "\nArguments:\n"
                "1. minconf      (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
                "2. includeempty (bool, optional, default=false) Whether to include accounts that haven't received any payments.\n"
                "3. includeWatchonly (bool, optional, default=false) Whether to include watchonly addresses (see 'importaddress').\n"

                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"involvesWatchonly\" : true,   (bool) Only returned if imported addresses were involved in transaction\n"
                "    \"account\" : \"accountname\",  (string) The account name of the receiving account\n"
                "    \"amount\" : x.xxx,             (numeric) The total amount received by addresses with this account\n"
                "    \"confirmations\" : n,          (numeric) The number of confirmations of the most recent transaction included\n"
                "    \"label\" : \"label\"           (string) A comment for the address/transaction, if any\n"
                "  }\n"
                "  ,...\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("listreceivedbyaccount", "")
                + HelpExampleCli("listreceivedbyaccount", "6 true")
                + HelpExampleRpc("listreceivedbyaccount", "6, true, true")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    return ListReceived(params, true);
}

static void MaybePushAddress(UniValue & entry, const CTxDestination &dest)
{
    CStockAddress addr;
    if (addr.Set(dest)) {
        entry.pushKV("address", addr.ToString());
    }
}

void GetReceived(const COutputEntry& r, const CWalletTx& wtx, const std::string& strAccount, bool fLong, UniValue& ret, CAmount nFee, bool fAllAccounts, bool involvesWatchonly)
{
    std::string account;
    if (pwalletMain->mapAddressBook.count(r.destination))
        account = pwalletMain->mapAddressBook[r.destination].name;
    if (fAllAccounts || (account == strAccount))
    {
        UniValue entry(UniValue::VOBJ);
        if(involvesWatchonly || (::IsMine(*pwalletMain, r.destination) & ISMINE_WATCH_ONLY))
            entry.pushKV("involvesWatchonly", true);
        entry.pushKV("account", account);
        MaybePushAddress(entry, r.destination);
        if (wtx.IsCoinBase() || wtx.IsCoinStake())
        {
            if (wtx.GetDepthInMainChain() < 1)
                entry.pushKV("category", "orphan");
            else if (wtx.GetBlocksToMaturity() > 0)
                entry.pushKV("category", "immature");
            else
                entry.pushKV("category", "generate");
        }
        else
        {
            entry.pushKV("category", "receive");
        }
        entry.pushKV("amount", ValueFromAmount(r.amount));

        entry.pushKV("canStake", (::IsMine(*pwalletMain, r.destination) & ISMINE_STAKABLE ||
                                  (::IsMine(*pwalletMain, r.destination) & ISMINE_SPENDABLE &&
                                   !CStockAddress(r.destination).IsColdStakingAddress(Params()) &&
                                   !CStockAddress(r.destination).IsColdStakingv2Address(Params()))) ? true : false);
        entry.pushKV("canSpend", ((::IsMine(*pwalletMain, r.destination) & ISMINE_SPENDABLE) || (pwalletMain->IsMine(wtx.vout[r.vout]) & ISMINE_SPENDABLE_PRIVATE)) ? true : false);
        if (pwalletMain->mapAddressBook.count(r.destination))
            entry.pushKV("label", account);
        entry.pushKV("vout", r.vout);
        if (fLong)
            WalletTxToJSON(wtx, entry);
        ret.push_back(entry);
    }
}

void ListTransactions(const CWalletTx& wtx, const std::string& strAccount, int nMinDepth, bool fLong, UniValue& ret, const isminefilter& filter)
{
    CAmount nFee;
    std::string strSentAccount;
    std::list<COutputEntry> listReceived;
    std::list<COutputEntry> listSent;

    wtx.GetAmounts(listReceived, listSent, nFee, strSentAccount, filter);

    bool fAllAccounts = (strAccount == std::string("*"));
    bool involvesWatchonly = wtx.IsFromMe(ISMINE_WATCH_ONLY);

    // Sent
    if ((!wtx.IsCoinStake())  && (!listSent.empty() || nFee != 0) && (fAllAccounts || strAccount == strSentAccount))
    {
        for(const COutputEntry& s: listSent)
        {
            UniValue entry(UniValue::VOBJ);
            if(involvesWatchonly || (::IsMine(*pwalletMain, s.destination) & ISMINE_WATCH_ONLY))
                entry.pushKV("involvesWatchonly", true);
            entry.pushKV("account", strSentAccount);
            MaybePushAddress(entry, s.destination);
            bool fCFund = false;
            for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];
                if (txout.scriptPubKey.IsCommunityFundContribution()) fCFund = true;
            }

            entry.pushKV("category", s.amount == nFee ? "fee" : (fCFund ? "cfund contribution" : "send"));
            entry.pushKV("memo", wtx.vMemos.size() > s.vout ? wtx.vMemos[s.vout] : "");
            entry.pushKV("amount", ValueFromAmount(-s.amount));
            if (pwalletMain->mapAddressBook.count(s.destination))
                entry.pushKV("label", pwalletMain->mapAddressBook[s.destination].name);
            entry.pushKV("vout", s.vout);
            entry.pushKV("fee", ValueFromAmount(-nFee));
            if (fLong)
                WalletTxToJSON(wtx, entry);
            entry.pushKV("abandoned", wtx.isAbandoned());
            ret.push_back(entry);
        }
    }

    // Received
    if (listReceived.size() > 0 && wtx.GetDepthInMainChain() >= nMinDepth)
    {
        for(const COutputEntry& r: listReceived)
        {
            GetReceived(r, wtx, strAccount, fLong, ret, nFee, fAllAccounts, involvesWatchonly);
        }
    }
}

void AcentryToJSON(const CAccountingEntry& acentry, const std::string& strAccount, UniValue& ret)
{
    bool fAllAccounts = (strAccount == std::string("*"));

    if (fAllAccounts || acentry.strAccount == strAccount)
    {
        UniValue entry(UniValue::VOBJ);
        entry.pushKV("account", acentry.strAccount);
        entry.pushKV("category", "move");
        entry.pushKV("time", acentry.nTime);
        entry.pushKV("amount", ValueFromAmount(acentry.nCreditDebit));
        entry.pushKV("otheraccount", acentry.strOtherAccount);
        entry.pushKV("comment", acentry.strComment);
        ret.push_back(entry);
    }
}

UniValue listtransactions(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 4)
        throw std::runtime_error(
                "listtransactions ( \"account\" count from includeWatchonly)\n"
                "\nReturns up to 'count' most recent transactions skipping the first 'from' transactions for account 'account'.\n"
                "\nArguments:\n"
                "1. \"account\"    (string, optional) DEPRECATED. The account name. Should be \"*\".\n"
                "2. count          (numeric, optional, default=10) The number of transactions to return\n"
                "3. from           (numeric, optional, default=0) The number of transactions to skip\n"
                "4. includeWatchonly (bool, optional, default=false) Include transactions to watchonly addresses (see 'importaddress')\n"
                "5. includeColdStaking (bool, optional, default=true) Include transactions to cold staking addresses\n"
                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"account\":\"accountname\",       (string) DEPRECATED. The account name associated with the transaction. \n"
                "                                                It will be \"\" for the default account.\n"
                "    \"address\":\"stockaddress\",    (string) The stock address of the transaction. Not present for \n"
                "                                                move transactions (category = move).\n"
                "    \"category\":\"send|receive|move\", (string) The transaction category. 'move' is a local (off blockchain)\n"
                "                                                transaction between accounts, and not associated with an address,\n"
                "                                                transaction id or block. 'send' and 'receive' transactions are \n"
                "                                                associated with an address, transaction id and block details\n"
                "    \"amount\": x.xxx,          (numeric) The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and for the\n"
                                                                                             "                                         'move' category for moves outbound. It is positive for the 'receive' category,\n"
                                                                                             "                                         and for the 'move' category for inbound funds.\n"
                                                                                             "    \"vout\": n,                (numeric) the vout value\n"
                                                                                             "    \"fee\": x.xxx,             (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
                                                                                                                                                                                     "                                         'send' category of transactions.\n"
                                                                                                                                                                                     "    \"abandoned\": xxx          (bool) 'true' if the transaction has been abandoned (inputs are respendable).\n"
                                                                                                                                                                                     "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and \n"
                                                                                                                                                                                     "                                         'receive' category of transactions. Negative confirmations indicate the\n"
                                                                                                                                                                                     "                                         transaction conflicts with the block chain\n"
                                                                                                                                                                                     "    \"trusted\": xxx            (bool) Whether we consider the outputs of this unconfirmed transaction safe to spend.\n"
                                                                                                                                                                                     "    \"blockhash\": \"hashvalue\", (string) The block hash containing the transaction. Available for 'send' and 'receive'\n"
                                                                                                                                                                                     "                                          category of transactions.\n"
                                                                                                                                                                                     "    \"blockindex\": n,          (numeric) The index of the transaction in the block that includes it. Available for 'send' and 'receive'\n"
                                                                                                                                                                                     "                                          category of transactions.\n"
                                                                                                                                                                                     "    \"blocktime\": xxx,         (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
                                                                                                                                                                                     "    \"txid\": \"transactionid\", (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
                                                                                                                                                                                     "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (midnight Jan 1 1970 GMT).\n"
                                                                                                                                                                                     "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (midnight Jan 1 1970 GMT). Available \n"
                                                                                                                                                                                     "                                          for 'send' and 'receive' category of transactions.\n"
                                                                                                                                                                                     "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
                                                                                                                                                                                     "    \"label\": \"label\"        (string) A comment for the address/transaction, if any\n"
                                                                                                                                                                                     "    \"otheraccount\": \"accountname\",  (string) For the 'move' category of transactions, the account the funds came \n"
                                                                                                                                                                                     "                                          from (for receiving funds, positive amounts), or went to (for sending funds,\n"
                                                                                                                                                                                     "                                          negative amounts).\n"
                                                                                                                                                                                     "    \"bip125-replaceable\": \"yes|no|unknown\"  (string) Whether this transaction could be replaced due to BIP125 (replace-by-fee);\n"
                                                                                                                                                                                     "                                                     may be unknown for unconfirmed transactions not in the mempool\n"
                                                                                                                                                                                     "  }\n"
                                                                                                                                                                                     "]\n"

                                                                                                                                                                                     "\nExamples:\n"
                                                                                                                                                                                     "\nList the most recent 10 transactions in the systems\n"
                + HelpExampleCli("listtransactions", "") +
                "\nList transactions 100 to 120\n"
                + HelpExampleCli("listtransactions", "\"*\" 20 100") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("listtransactions", "\"*\", 20, 100")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string strAccount = "*";
    if (params.size() > 0)
        strAccount = params[0].get_str();
    int nCount = 10;
    if (params.size() > 1)
        nCount = params[1].get_int();
    int nFrom = 0;
    if (params.size() > 2)
        nFrom = params[2].get_int();
    isminefilter filter = ISMINE_SPENDABLE | ISMINE_STAKABLE | ISMINE_SPENDABLE_PRIVATE;
    if(params.size() > 3)
        if(params[3].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;
    if(params.size() > 4)
        if(!params[4].get_bool())
            filter &= ~ISMINE_WATCH_ONLY;

    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    UniValue ret(UniValue::VARR);

    const CWallet::TxItems & txOrdered = pwalletMain->wtxOrdered;

    // iterate backwards until we have nCount items to return:
    for (CWallet::TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
    {
        CWalletTx *const pwtx = (*it).second.first;
        if (pwtx != 0)
            ListTransactions(*pwtx, strAccount, 0, true, ret, filter);
        CAccountingEntry *const pacentry = (*it).second.second;
        if (pacentry != 0)
            AcentryToJSON(*pacentry, strAccount, ret);

        if ((int)ret.size() >= (nCount+nFrom)) break;
    }
    // ret is newest to oldest

    if (nFrom > (int)ret.size())
        nFrom = ret.size();
    if ((nFrom + nCount) > (int)ret.size())
        nCount = ret.size() - nFrom;

    std::vector<UniValue> arrTmp = ret.getValues();

    std::vector<UniValue>::iterator first = arrTmp.begin();
    std::advance(first, nFrom);
    std::vector<UniValue>::iterator last = arrTmp.begin();
    std::advance(last, nFrom+nCount);

    if (last != arrTmp.end()) arrTmp.erase(last, arrTmp.end());
    if (first != arrTmp.begin()) arrTmp.erase(arrTmp.begin(), first);

    std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;
}

UniValue listaccounts(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 2)
        throw std::runtime_error(
                "listaccounts ( minconf includeWatchonly)\n"
                "\nDEPRECATED. Returns Object that has account names as keys, account balances as values.\n"
                "\nArguments:\n"
                "1. minconf          (numeric, optional, default=1) Only include transactions with at least this many confirmations\n"
                "2. includeWatchonly (bool, optional, default=false) Include balances in watchonly addresses (see 'importaddress')\n"
                "\nResult:\n"
                "{                      (json object where keys are account names, and values are numeric balances\n"
                "  \"account\": x.xxx,  (numeric) The property name is the account name, and the value is the total balance for the account.\n"
                "  ...\n"
                "}\n"
                "\nExamples:\n"
                "\nList account balances where there at least 1 confirmation\n"
                + HelpExampleCli("listaccounts", "") +
                "\nList account balances including zero confirmation transactions\n"
                + HelpExampleCli("listaccounts", "0") +
                "\nList account balances for 6 or more confirmations\n"
                + HelpExampleCli("listaccounts", "6") +
                "\nAs json rpc call\n"
                + HelpExampleRpc("listaccounts", "6")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();
    isminefilter includeWatchonly = ISMINE_SPENDABLE;
    if(params.size() > 1)
        if(params[1].get_bool())
            includeWatchonly = includeWatchonly | ISMINE_WATCH_ONLY;

    std::map<std::string, CAmount> mapAccountBalances;
    for(const std::pair<CTxDestination, CAddressBookData>& entry: pwalletMain->mapAddressBook) {
        if (IsMine(*pwalletMain, entry.first) & includeWatchonly) // This address belongs to me
            mapAccountBalances[entry.second.name] = 0;
    }

    for (std::map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        CAmount nFee;
        std::string strSentAccount;
        std::list<COutputEntry> listReceived;
        std::list<COutputEntry> listSent;
        int nDepth = wtx.GetDepthInMainChain();
        if (wtx.GetBlocksToMaturity() > 0 || nDepth < 0)
            continue;
        wtx.GetAmounts(listReceived, listSent, nFee, strSentAccount, includeWatchonly);
        mapAccountBalances[strSentAccount] -= nFee;
        for(const COutputEntry& s: listSent)
            mapAccountBalances[strSentAccount] -= s.amount;
        if (nDepth >= nMinDepth)
        {
            for(const COutputEntry& r: listReceived)
                if (pwalletMain->mapAddressBook.count(r.destination))
                    mapAccountBalances[pwalletMain->mapAddressBook[r.destination].name] += r.amount;
                else
                    mapAccountBalances[""] += r.amount;
        }
    }

    const std::list<CAccountingEntry> & acentries = pwalletMain->laccentries;
    for(const CAccountingEntry& entry: acentries)
        mapAccountBalances[entry.strAccount] += entry.nCreditDebit;

    UniValue ret(UniValue::VOBJ);
    for(const std::pair<std::string, CAmount>& accountBalance: mapAccountBalances) {
        ret.pushKV(accountBalance.first, ValueFromAmount(accountBalance.second));
    }
    return ret;
}

UniValue listsinceblock(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp)
        throw std::runtime_error(
                "listsinceblock ( \"blockhash\" target-confirmations includeWatchonly)\n"
                "\nGet all transactions in blocks since block [blockhash], or all transactions if omitted\n"
                "\nArguments:\n"
                "1. \"blockhash\"   (string, optional) The block hash to list transactions since\n"
                "2. target-confirmations:    (numeric, optional) The confirmations required, must be 1 or more\n"
                "3. includeWatchonly:        (bool, optional, default=false) Include transactions to watchonly addresses (see 'importaddress')"
                "\nResult:\n"
                "{\n"
                "  \"transactions\": [\n"
                "    \"account\":\"accountname\",       (string) DEPRECATED. The account name associated with the transaction. Will be \"\" for the default account.\n"
                "    \"address\":\"stockaddress\",    (string) The stock address of the transaction. Not present for move transactions (category = move).\n"
                "    \"category\":\"send|receive\",     (string) The transaction category. 'send' has negative amounts, 'receive' has positive amounts.\n"
                "    \"amount\": x.xxx,          (numeric) The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and for the 'move' category for moves \n"
                                                                                             "                                          outbound. It is positive for the 'receive' category, and for the 'move' category for inbound funds.\n"
                                                                                             "    \"vout\" : n,               (numeric) the vout value\n"
                                                                                             "    \"fee\": x.xxx,             (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the 'send' category of transactions.\n"
                                                                                                                                                                                     "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and 'receive' category of transactions.\n"
                                                                                                                                                                                     "    \"blockhash\": \"hashvalue\",     (string) The block hash containing the transaction. Available for 'send' and 'receive' category of transactions.\n"
                                                                                                                                                                                     "    \"blockindex\": n,          (numeric) The index of the transaction in the block that includes it. Available for 'send' and 'receive' category of transactions.\n"
                                                                                                                                                                                     "    \"blocktime\": xxx,         (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
                                                                                                                                                                                     "    \"txid\": \"transactionid\",  (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
                                                                                                                                                                                     "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT).\n"
                                                                                                                                                                                     "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (Jan 1 1970 GMT). Available for 'send' and 'receive' category of transactions.\n"
                                                                                                                                                                                     "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
                                                                                                                                                                                     "    \"label\" : \"label\"       (string) A comment for the address/transaction, if any\n"
                                                                                                                                                                                     "    \"to\": \"...\",            (string) If a comment to is associated with the transaction.\n"
                                                                                                                                                                                     "  ],\n"
                                                                                                                                                                                     "  \"lastblock\": \"lastblockhash\"     (string) The hash of the last block\n"
                                                                                                                                                                                     "}\n"
                                                                                                                                                                                     "\nExamples:\n"
                + HelpExampleCli("listsinceblock", "")
                + HelpExampleCli("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\" 6")
                + HelpExampleRpc("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\", 6")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CBlockIndex *pindex = nullptr;
    int target_confirms = 1;
    isminefilter filter = ISMINE_SPENDABLE;

    if (params.size() > 0)
    {
        uint256 blockId;

        blockId.SetHex(params[0].get_str());
        BlockMap::iterator it = mapBlockIndex.find(blockId);
        if (it != mapBlockIndex.end())
            pindex = it->second;
    }

    if (params.size() > 1)
    {
        target_confirms = params[1].get_int();

        if (target_confirms < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
    }

    if(params.size() > 2)
        if(params[2].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    int depth = pindex ? (1 + chainActive.Height() - pindex->nHeight) : -1;

    UniValue transactions(UniValue::VARR);

    for (std::map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); it++)
    {
        CWalletTx tx = (*it).second;

        if (depth == -1 || tx.GetDepthInMainChain() < depth)
            ListTransactions(tx, "*", 0, true, transactions, filter);
    }

    CBlockIndex *pblockLast = chainActive[chainActive.Height() + 1 - target_confirms];
    uint256 lastblock = pblockLast ? pblockLast->GetBlockHash() : uint256();

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("transactions", transactions);
    ret.pushKV("lastblock", lastblock.GetHex());

    return ret;
}

UniValue gettransaction(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
                "gettransaction \"txid\" ( includeWatchonly )\n"
                "\nGet detailed information about in-wallet transaction <txid>\n"
                "\nArguments:\n"
                "1. \"txid\"    (string, required) The transaction id\n"
                "2. \"includeWatchonly\"    (bool, optional, default=false) Whether to include watchonly addresses in balance calculation and details[]\n"
                "\nResult:\n"
                "{\n"
                "  \"amount\" : x.xxx,        (numeric) The transaction amount in " + CURRENCY_UNIT + "\n"
                "  \"confirmations\" : n,     (numeric) The number of confirmations\n"
                "  \"blockhash\" : \"hash\",  (string) The block hash\n"
                "  \"blockindex\" : xx,       (numeric) The index of the transaction in the block that includes it\n"
                "  \"blocktime\" : ttt,       (numeric) The time in seconds since epoch (1 Jan 1970 GMT)\n"
                "  \"txid\" : \"transactionid\",   (string) The transaction id.\n"
                "  \"time\" : ttt,            (numeric) The transaction time in seconds since epoch (1 Jan 1970 GMT)\n"
                "  \"timereceived\" : ttt,    (numeric) The time received in seconds since epoch (1 Jan 1970 GMT)\n"
                "  \"bip125-replaceable\": \"yes|no|unknown\"  (string) Whether this transaction could be replaced due to BIP125 (replace-by-fee);\n"
                "                                                   may be unknown for unconfirmed transactions not in the mempool\n"
                "  \"details\" : [\n"
                "    {\n"
                "      \"account\" : \"accountname\",  (string) DEPRECATED. The account name involved in the transaction, can be \"\" for the default account.\n"
                "      \"address\" : \"stockaddress\",   (string) The stock address involved in the transaction\n"
                "      \"category\" : \"send|receive\",    (string) The category, either 'send' or 'receive'\n"
                "      \"amount\" : x.xxx,                 (numeric) The amount in " + CURRENCY_UNIT + "\n"
                "      \"label\" : \"label\",              (string) A comment for the address/transaction, if any\n"
                "      \"vout\" : n,                       (numeric) the vout value\n"
                "    }\n"
                "    ,...\n"
                "  ],\n"
                "  \"hex\" : \"data\"         (string) Raw data for transaction\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
                + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" true")
                + HelpExampleRpc("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 hash;
    hash.SetHex(params[0].get_str());

    isminefilter filter = ISMINE_SPENDABLE;
    if(params.size() > 1)
        if(params[1].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    UniValue entry(UniValue::VOBJ);
    if (!pwalletMain->mapWallet.count(hash))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    const CWalletTx& wtx = pwalletMain->mapWallet[hash];

    if (wtx.IsCTOutput())
        filter = filter | ISMINE_SPENDABLE_PRIVATE;

    CAmount nCredit = wtx.GetCredit(filter, false);
    CAmount nDebit = wtx.GetDebit(filter);
    CAmount nNet = nCredit - nDebit;
    CAmount nFee = (wtx.IsFromMe(filter) ? (wtx.IsBLSCT() ? wtx.GetFee() : wtx.GetValueOut() - nDebit) : 0);

    entry.pushKV("amount", ValueFromAmount(nNet - (wtx.IsCoinStake() ? 0 : nFee)));
    if (wtx.IsFromMe(filter))
        entry.pushKV("fee", ValueFromAmount(nFee - (wtx.IsCoinStake() ? nNet : 0)));

    WalletTxToJSON(wtx, entry);

    UniValue details(UniValue::VARR);
    ListTransactions(wtx, "*", 0, false, details, filter);
    entry.pushKV("details", details);

    std::string strHex = EncodeHexTx(static_cast<CTransaction>(wtx));
    entry.pushKV("hex", strHex);

    return entry;
}

UniValue abandontransaction(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "abandontransaction \"txid\"\n"
                "\nMark in-wallet transaction <txid> as abandoned\n"
                "This will mark this transaction and all its in-wallet descendants as abandoned which will allow\n"
                "for their inputs to be respent.  It can be used to replace \"stuck\" or evicted transactions.\n"
                "It only works on transactions which are not included in a block and are not currently in the mempool.\n"
                "It has no effect on transactions which are already conflicted or abandoned.\n"
                "\nArguments:\n"
                "1. \"txid\"    (string, required) The transaction id\n"
                "\nResult:\n"
                "\nExamples:\n"
                + HelpExampleCli("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
                + HelpExampleRpc("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 hash;
    hash.SetHex(params[0].get_str());

    if (!pwalletMain->mapWallet.count(hash))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    if (!pwalletMain->AbandonTransaction(hash))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");

    return NullUniValue;
}


UniValue backupwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "backupwallet \"destination\"\n"
                "\nSafely copies current wallet file to destination, which can be a directory or a path with filename.\n"
                "\nArguments:\n"
                "1. \"destination\"   (string) The destination directory or file\n"
                "\nExamples:\n"
                + HelpExampleCli("backupwallet", "\"backup.dat\"")
                + HelpExampleRpc("backupwallet", "\"backup.dat\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string strDest = params[0].get_str();
    if (!pwalletMain->BackupWallet(strDest))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet backup failed!");

    return NullUniValue;
}


UniValue keypoolrefill(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw std::runtime_error(
                "keypoolrefill ( newsize )\n"
                "\nFills the keypool."
                + HelpRequiringPassphrase() + "\n"
                                              "\nArguments\n"
                                              "1. newsize     (numeric, optional, default=100) The new keypool size\n"
                                              "\nExamples:\n"
                + HelpExampleCli("keypoolrefill", "")
                + HelpExampleRpc("keypoolrefill", "")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // 0 is interpreted by TopUpKeyPool() as the default keypool size given by -keypool
    unsigned int kpSize = 0;
    if (params.size() > 0) {
        if (params[0].get_int() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size.");
        kpSize = (unsigned int)params[0].get_int();
    }

    EnsureWalletIsUnlocked();
    pwalletMain->TopUpKeyPool(kpSize);
    pwalletMain->TopUpBLSCTBlindingKeyPool(kpSize);
    for (auto&it: pwalletMain->mapBLSCTSubAddressKeyPool)
        pwalletMain->TopUpBLSCTSubAddressKeyPool(it.first, kpSize);

    if (pwalletMain->GetKeyPoolSize() < kpSize)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");

    return NullUniValue;
}


static void LockWallet(CWallet* pWallet)
{
    LOCK(cs_nWalletUnlockTime);
    nWalletUnlockTime = 0;
    pWallet->Lock();
}

UniValue walletpassphrase(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (pwalletMain->IsCrypted() && (fHelp || params.size() < 2))
        throw std::runtime_error(
                "walletpassphrase \"passphrase\" timeout [stakingonly]\n"
                "\nStores the wallet decryption key in memory for 'timeout' seconds.\n"
                "This is needed prior to performing transactions related to private keys such as sending stocks\n"
                "\nArguments:\n"
                "1. \"passphrase\"     (string, required) The wallet passphrase\n"
                "2. timeout            (numeric, required) The time to keep the decryption key in seconds.\n"
                "3. stakingonly        (bool, optional) If it is true sending functions are disabled.\n"
                "\nNote:\n"
                "Issuing the walletpassphrase command while the wallet is already unlocked will set a new unlock\n"
                "time that overrides the old one.\n"
                "\nExamples:\n"
                "\nunlock the wallet for 60 seconds\n"
                + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 60") +
                "\nLock the wallet again (before 60 seconds)\n"
                + HelpExampleCli("walletlock", "") +
                "\nAs json rpc call\n"
                + HelpExampleRpc("walletpassphrase", "\"my pass phrase\", 60")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");

    // Note that the walletpassphrase is stored in params[0] which is not mlock()ed
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() > 0)
    {
        if (!pwalletMain->Unlock(strWalletPass))
            throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
    }
    else
        throw std::runtime_error(
                "walletpassphrase <passphrase> <timeout>\n"
                "Stores the wallet decryption key in memory for <timeout> seconds.");

    pwalletMain->TopUpKeyPool();
    pwalletMain->TopUpBLSCTBlindingKeyPool();
    for (auto&it: pwalletMain->mapBLSCTSubAddressKeyPool)
        pwalletMain->TopUpBLSCTSubAddressKeyPool(it.first);

    int64_t nSleepTime = params[1].get_int64();
    LOCK(cs_nWalletUnlockTime);
    nWalletUnlockTime = GetTime() + nSleepTime;
    RPCRunLater("lockwallet", boost::bind(LockWallet, pwalletMain), nSleepTime);

    if (params.size() > 2)
        fWalletUnlockStakingOnly = params[2].get_bool();
    else
        fWalletUnlockStakingOnly = false;

    return NullUniValue;
}


UniValue walletpassphrasechange(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw std::runtime_error(
                "walletpassphrasechange \"oldpassphrase\" \"newpassphrase\"\n"
                "\nChanges the wallet passphrase from 'oldpassphrase' to 'newpassphrase'.\n"
                "\nArguments:\n"
                "1. \"oldpassphrase\"      (string) The current passphrase\n"
                "2. \"newpassphrase\"      (string) The new passphrase\n"
                "\nExamples:\n"
                + HelpExampleCli("walletpassphrasechange", "\"old one\" \"new one\"")
                + HelpExampleRpc("walletpassphrasechange", "\"old one\", \"new one\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");

    // TODO: get rid of these .c_str() calls by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strOldWalletPass;
    strOldWalletPass.reserve(100);
    strOldWalletPass = params[0].get_str().c_str();

    SecureString strNewWalletPass;
    strNewWalletPass.reserve(100);
    strNewWalletPass = params[1].get_str().c_str();

    if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
        throw std::runtime_error(
                "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
                "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");

    if (!pwalletMain->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass))
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");

    return NullUniValue;
}


UniValue walletlock(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 0))
        throw std::runtime_error(
                "walletlock\n"
                "\nRemoves the wallet encryption key from memory, locking the wallet.\n"
                "After calling this method, you will need to call walletpassphrase again\n"
                "before being able to call any methods which require the wallet to be unlocked.\n"
                "\nExamples:\n"
                "\nSet the passphrase for 2 minutes to perform a transaction\n"
                + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 120") +
                "\nPerform a send (requires passphrase set)\n"
                + HelpExampleCli("sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 1.0") +
                "\nClear the passphrase since we are done before 2 minutes is up\n"
                + HelpExampleCli("walletlock", "") +
                "\nAs json rpc call\n"
                + HelpExampleRpc("walletlock", "")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletlock was called.");

    {
        LOCK(cs_nWalletUnlockTime);
        pwalletMain->Lock();
        nWalletUnlockTime = 0;
    }

    return NullUniValue;
}


UniValue encryptwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (!pwalletMain->IsCrypted() && (fHelp || params.size() != 1))
        throw std::runtime_error(
                "encryptwallet \"passphrase\"\n"
                "\nEncrypts the wallet with 'passphrase'. This is for first time encryption.\n"
                "After this, any calls that interact with private keys such as sending or signing \n"
                "will require the passphrase to be set prior the making these calls.\n"
                "Use the walletpassphrase call for this, and then walletlock call.\n"
                "If the wallet is already encrypted, use the walletpassphrasechange call.\n"
                "Note that this will shutdown the server.\n"
                "\nArguments:\n"
                "1. \"passphrase\"    (string) The pass phrase to encrypt the wallet with. It must be at least 1 character, but should be long.\n"
                "\nExamples:\n"
                "\nEncrypt you wallet\n"
                + HelpExampleCli("encryptwallet", "\"my pass phrase\"") +
                "\nNow set the passphrase to use the wallet, such as for signing or sending stock\n"
                + HelpExampleCli("walletpassphrase", "\"my pass phrase\"") +
                "\nNow we can so something like sign\n"
                + HelpExampleCli("signmessage", "\"stockaddress\" \"test message\"") +
                "\nNow lock the wallet again by removing the passphrase\n"
                + HelpExampleCli("walletlock", "") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("encryptwallet", "\"my pass phrase\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an encrypted wallet, but encryptwallet was called.");

    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() < 1)
        throw std::runtime_error(
                "encryptwallet <passphrase>\n"
                "Encrypts the wallet with <passphrase>.");

    if (!pwalletMain->EncryptWallet(strWalletPass))
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: Failed to encrypt the wallet.");

    // BDB seems to have a bad habit of writing old data into
    // slack space in .dat files; that is bad if the old data is
    // unencrypted private keys. So:
    StartShutdown();
    return _("wallet encrypted; Stock server stopping, restart to run with encrypted wallet.");
}

UniValue encrypttxdata(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "encrypttxdata \"passphrase\"\n"
            "\nEncrypts the wallet database using \"passphrase\", effectively encrypting your\n"
            "transaction data and addressbook, you can also use this rpc command to change the\n"
            "encryption \"passphrase\" of an already encrypted wallet database.\n"
            "Note that this will shutdown the server.\n"
            "\nArguments:\n"
            "1. \"passphrase\"    (string) The pass phrase to encrypt the wallet database with. It must be at least 1 character, but should be long.\n"
            "\nExamples:\n"
            "\nEncrypt you wallet\n"
            + HelpExampleCli("encrypttxdata", "\"my pass phrase\"") +
                "\nAs a json rpc call\n"
            + HelpExampleRpc("encrypttxdata", "\"my pass phrase\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;

    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() < 1)
        throw std::runtime_error(
                "encrypttxdata <passphrase>\n"
            "Encrypts the txdata with <passphrase>.");

    if (!pwalletMain->EncryptTx(strWalletPass))
        throw JSONRPCError(RPC_TXDATA_ENCRYPTION_FAILED, "Error: Failed to encrypt the txdata.");

    // Shutdown the wallet so we don't accidentally write unencrypted data
    // to the wallet.dat file...
    StartShutdown();
    return _("txdata encrypted; Stock server stopping, restart to run with encrypted txdata.");
}

UniValue lockunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
                "lockunspent unlock ([{\"txid\":\"txid\",\"vout\":n},...])\n"
                "\nUpdates list of temporarily unspendable outputs.\n"
                "Temporarily lock (unlock=false) or unlock (unlock=true) specified transaction outputs.\n"
                "If no transaction outputs are specified when unlocking then all current locked transaction outputs are unlocked.\n"
                "A locked transaction output will not be chosen by automatic coin selection, when spending stocks.\n"
                "Locks are stored in memory only. Nodes start with zero locked outputs, and the locked output list\n"
                "is always cleared (by virtue of process exit) when a node stops or fails.\n"
                "Also see the listunspent call\n"
                "\nArguments:\n"
                "1. unlock            (boolean, required) Whether to unlock (true) or lock (false) the specified transactions\n"
                "2. \"transactions\"  (string, optional) A json array of objects. Each object the txid (string) vout (numeric)\n"
                "     [           (json array of json objects)\n"
                "       {\n"
                "         \"txid\":\"id\",    (string) The transaction id\n"
                "         \"vout\": n         (numeric) The output number\n"
                "       }\n"
                "       ,...\n"
                "     ]\n"

                "\nResult:\n"
                "true|false    (boolean) Whether the command was successful or not\n"

                "\nExamples:\n"
                "\nList the unspent transactions\n"
                + HelpExampleCli("listunspent", "") +
                "\nLock an unspent transaction\n"
                + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
                "\nList the locked transactions\n"
                + HelpExampleCli("listlockunspent", "") +
                "\nUnlock the transaction again\n"
                + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("lockunspent", "false, \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (params.size() == 1)
        RPCTypeCheck(params, boost::assign::list_of(UniValue::VBOOL));
    else
        RPCTypeCheck(params, boost::assign::list_of(UniValue::VBOOL)(UniValue::VARR));

    bool fUnlock = params[0].get_bool();

    if (params.size() == 1) {
        if (fUnlock)
            pwalletMain->UnlockAllCoins();
        return true;
    }

    UniValue outputs = params[1].get_array();
    for (unsigned int idx = 0; idx < outputs.size(); idx++) {
        const UniValue& output = outputs[idx];
        if (!output.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
        const UniValue& o = output.get_obj();

        RPCTypeCheckObj(o,
                        {
                            {"txid", UniValueType(UniValue::VSTR)},
                            {"vout", UniValueType(UniValue::VNUM)},
                        });

        std::string txid = find_value(o, "txid").get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        int nOutput = find_value(o, "vout").get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        COutPoint outpt(uint256S(txid), nOutput);

        if (fUnlock)
            pwalletMain->UnlockCoin(outpt);
        else
            pwalletMain->LockCoin(outpt);
    }

    return true;
}

UniValue listlockunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 0)
        throw std::runtime_error(
                "listlockunspent\n"
                "\nReturns list of temporarily unspendable outputs.\n"
                "See the lockunspent call to lock and unlock transactions for spending.\n"
                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"txid\" : \"transactionid\",     (string) The transaction id locked\n"
                "    \"vout\" : n                      (numeric) The vout value\n"
                "  }\n"
                "  ,...\n"
                "]\n"
                "\nExamples:\n"
                "\nList the unspent transactions\n"
                + HelpExampleCli("listunspent", "") +
                "\nLock an unspent transaction\n"
                + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
                "\nList the locked transactions\n"
                + HelpExampleCli("listlockunspent", "") +
                "\nUnlock the transaction again\n"
                + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("listlockunspent", "")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::vector<COutPoint> vOutpts;
    pwalletMain->ListLockedCoins(vOutpts);

    UniValue ret(UniValue::VARR);

    for(COutPoint &outpt: vOutpts) {
        UniValue o(UniValue::VOBJ);

        o.pushKV("txid", outpt.hash.GetHex());
        o.pushKV("vout", (int)outpt.n);
        ret.push_back(o);
    }

    return ret;
}

UniValue settxfee(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 1)
        throw std::runtime_error(
                "settxfee amount\n"
                "\nSet the transaction fee per kB. Overwrites the paytxfee parameter.\n"
                "\nArguments:\n"
                "1. amount         (numeric or string, required) The transaction fee in " + CURRENCY_UNIT + "/kB\n"
                                                                                                            "\nResult\n"
                                                                                                            "true|false        (boolean) Returns true if successful\n"
                                                                                                            "\nExamples:\n"
                + HelpExampleCli("settxfee", "0.00001")
                + HelpExampleRpc("settxfee", "0.00001")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Amount
    CAmount nAmount = AmountFromValue(params[0]);

    payTxFee = CFeeRate(nAmount, 1000);
    return true;
}

UniValue getwalletinfo(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw std::runtime_error(
                "getwalletinfo\n"
                "Returns an object containing various wallet state info.\n"
                "\nResult:\n"
                "{\n"
                "  \"walletversion\": xxxxx,       (numeric) the wallet version\n"
                "  \"balance\": xxxxxxx,           (numeric) the total confirmed balance of the wallet in " + CURRENCY_UNIT + "\n"
                "  \"unconfirmed_balance\": xxx,   (numeric) the total unconfirmed balance of the wallet in " + CURRENCY_UNIT + "\n"		             "  \"private_balance\": xxx,       (numeric) the total confirmed private balance of the wallet in " + CURRENCY_UNIT + "\n"
                "  \"unconfirmed_balance\": xxx,   (numeric) the total unconfirmed balance of the wallet in " + CURRENCY_UNIT + "\n"
                "  \"immature_balance\": xxxxxx,   (numeric) the total immature balance of the wallet in " + CURRENCY_UNIT + "\n"
                "  \"txcount\": xxxxxxx,           (numeric) the total number of transactions in the wallet\n"
                "  \"keypoololdest\": xxxxxx,      (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
                "  \"keypoolsize\": xxxx,          (numeric) how many new keys are pre-generated\n"
                "  \"unlocked_until\": ttt,        (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
                "  \"unlocked_for_staking\": b,    (boolean) whether the wallet is unlocked just for staking and mixing or not\n"
                "  \"paytxfee\": x.xxxx,           (numeric) the transaction fee configuration, set in " + CURRENCY_UNIT + "/kB\n"
                "  \"hdmasterkeyid\": \"<hash160>\", (string) the Hash160 of the HD master pubkey\n"
                "}\n"
                "\nExamples:\n"
                + HelpExampleCli("getwalletinfo", "")
                + HelpExampleRpc("getwalletinfo", "")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("walletversion",           pwalletMain->GetVersion());
    obj.pushKV("balance",                 ValueFromAmount(pwalletMain->GetBalance()));
    obj.pushKV("private_balance",         ValueFromAmount(pwalletMain->GetPrivateBalance()));
    obj.pushKV("coldstaking_balance",     ValueFromAmount(pwalletMain->GetColdStakingBalance()));
    obj.pushKV("unconfirmed_balance",     ValueFromAmount(pwalletMain->GetUnconfirmedBalance()));
    obj.pushKV("private_balance_pending", ValueFromAmount(pwalletMain->GetPrivateBalancePending()));
    obj.pushKV("immature_balance",        ValueFromAmount(pwalletMain->GetImmatureBalance()));
    obj.pushKV("txcount",                 (int)pwalletMain->mapWallet.size());
    obj.pushKV("keypoololdest",           pwalletMain->GetOldestKeyPoolTime());
    obj.pushKV("keypoolsize",             (int)pwalletMain->GetKeyPoolSize());
    if (pwalletMain->IsCrypted()) {
        obj.pushKV("unlocked_until", nWalletUnlockTime);
        obj.pushKV("unlocked_for_staking", fWalletUnlockStakingOnly);
    }
    obj.pushKV("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK()));
    CKeyID masterKeyID = pwalletMain->GetHDChain().masterKeyID;
    if (!masterKeyID.IsNull())
        obj.pushKV("hdmasterkeyid", masterKeyID.GetHex());
    return obj;
}

UniValue resendwallettransactions(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw std::runtime_error(
                "resendwallettransactions\n"
                "Immediately re-broadcast unconfirmed wallet transactions to all peers.\n"
                "Intended only for testing; the wallet code periodically re-broadcasts\n"
                "automatically.\n"
                "Returns array of transaction ids that were re-broadcast.\n"
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::vector<uint256> txids = pwalletMain->ResendWalletTransactionsBefore(GetTime());
    UniValue result(UniValue::VARR);
    for(const uint256& txid: txids)
    {
        result.push_back(txid.ToString());
    }
    return result;
}

UniValue listunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw std::runtime_error(
                "listunspent ( minconf maxconf  [\"address\",...] )\n"
                "\nReturns array of unspent transaction outputs\n"
                "with between minconf and maxconf (inclusive) confirmations.\n"
                "Optionally filter to only include txouts paid to specified addresses.\n"
                "\nArguments:\n"
                "1. minconf          (numeric, optional, default=1) The minimum confirmations to filter\n"
                "2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
                "3. \"addresses\"    (string) A json array of stock addresses to filter\n"
                "    [\n"
                "      \"address\"   (string) stock address\n"
                "      ,...\n"
                "    ]\n"
                "\nResult\n"
                "[                   (array of json object)\n"
                "  {\n"
                "    \"txid\" : \"txid\",          (string) the transaction id \n"
                "    \"vout\" : n,               (numeric) the vout value\n"
                "    \"address\" : \"address\",    (string) the stock address\n"
                "    \"account\" : \"account\",    (string) DEPRECATED. The associated account, or \"\" for the default account\n"
                "    \"scriptPubKey\" : \"key\",   (string) the script key\n"
                "    \"amount\" : x.xxx,         (numeric) the transaction amount in " + CURRENCY_UNIT + "\n"
                                                                                                         "    \"confirmations\" : n,      (numeric) The number of confirmations\n"
                                                                                                         "    \"redeemScript\" : n        (string) The redeemScript if scriptPubKey is P2SH\n"
                                                                                                         "    \"spendable\" : xxx,        (bool) Whether we have the private keys to spend this output\n"
                                                                                                         "    \"solvable\" : xxx          (bool) Whether we know how to spend this output, ignoring the lack of keys\n"
                                                                                                         "  }\n"
                                                                                                         "  ,...\n"
                                                                                                         "]\n"

                                                                                                         "\nExamples\n"
                + HelpExampleCli("listunspent", "")
                + HelpExampleCli("listunspent", "6 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
                + HelpExampleRpc("listunspent", "6, 9999999 \"[\\\"1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
                );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM)(UniValue::VNUM)(UniValue::VARR));

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 1)
        nMaxDepth = params[1].get_int();

    std::set<CStockAddress> setAddress;
    if (params.size() > 2) {
        UniValue inputs = params[2].get_array();
        for (unsigned int idx = 0; idx < inputs.size(); idx++) {
            const UniValue& input = inputs[idx];
            CStockAddress address(input.get_str());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Stock address: ")+input.get_str());
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ")+input.get_str());
            setAddress.insert(address);
        }
    }

    UniValue results(UniValue::VARR);
    std::vector<COutput> vecOutputs;
    assert(pwalletMain != nullptr);
    LOCK2(cs_main, pwalletMain->cs_wallet);
    pwalletMain->AvailableCoins(vecOutputs, false, nullptr, true);
    for(const COutput& out: vecOutputs) {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;

        CTxDestination address;
        const CScript& scriptPubKey = out.tx->vout[out.i].scriptPubKey;
        bool fValidAddress = ExtractDestination(scriptPubKey, address);

        if (setAddress.size() && (!fValidAddress || !setAddress.count(address)))
            continue;

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("txid", out.tx->GetHash().GetHex());
        entry.pushKV("vout", out.i);

        if (fValidAddress) {
            entry.pushKV("address", CStockAddress(address).ToString());

            if (pwalletMain->mapAddressBook.count(address))
                entry.pushKV("account", pwalletMain->mapAddressBook[address].name);

            if (scriptPubKey.IsPayToScriptHash()) {
                const CScriptID& hash = boost::get<CScriptID>(address);
                CScript redeemScript;
                if (pwalletMain->GetCScript(hash, redeemScript))
                    entry.pushKV("redeemScript", HexStr(redeemScript.begin(), redeemScript.end()));
            }
        }

        entry.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));
        entry.pushKV("amount", ValueFromAmount(out.tx->vout[out.i].nValue));
        entry.pushKV("confirmations", out.nDepth);
        entry.pushKV("spendable", out.fSpendable);
        entry.pushKV("solvable", out.fSolvable);
        results.push_back(entry);
    }

    return results;
}

UniValue fundrawtransaction(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
                "fundrawtransaction \"hexstring\" ( options )\n"
                "\nAdd inputs to a transaction until it has enough in value to meet its out value.\n"
                "This will not modify existing inputs, and will add one change output to the outputs.\n"
                "Note that inputs which were signed may need to be resigned after completion since in/outputs have been added.\n"
                "The inputs added will not be signed, use signrawtransaction for that.\n"
                "Note that all existing inputs must have their previous output transaction be in the wallet.\n"
                "Note that all inputs selected must be of standard form and P2SH scripts must be\n"
                "in the wallet using importaddress or addmultisigaddress (to calculate fees).\n"
                "You can see whether this is the case by checking the \"solvable\" field in the listunspent output.\n"
                "Only pay-to-pubkey, multisig, and P2SH versions thereof are currently supported for watch-only\n"
                "\nArguments:\n"
                "1. \"hexstring\"           (string, required) The hex string of the raw transaction\n"
                "2. options               (object, optional)\n"
                "   {\n"
                "     \"changeAddress\"     (string, optional, default pool address) The stock address to receive the change\n"
                "     \"changePosition\"    (numeric, optional, default random) The index of the change output\n"
                "     \"private\"           (boolean, optional, default false) Try to spend private coin outputs\n"
                "     \"includeWatching\"   (boolean, optional, default false) Also select inputs which are watch only\n"
                "     \"lockUnspents\"      (boolean, optional, default false) Lock selected unspent outputs\n"
                "     \"feeRate\"           (numeric, optional, default not set: makes wallet determine the fee) Set a specific feerate (" + CURRENCY_UNIT + " per KB)\n"
                                                                                                                                                             "   }\n"
                                                                                                                                                             "                         for backward compatibility: passing in a true instead of an object will result in {\"includeWatching\":true}\n"
                                                                                                                                                             "\nResult:\n"
                                                                                                                                                             "{\n"
                                                                                                                                                             "  \"hex\":       \"value\", (string)  The resulting raw transaction (hex-encoded string)\n"
                                                                                                                                                             "  \"fee\":       n,         (numeric) Fee in " + CURRENCY_UNIT + " the resulting transaction pays\n"
                                                                                                                                                                                                                               "  \"changepos\": n          (numeric) The position of the added change output, or -1\n"
                                                                                                                                                                                                                               "}\n"
                                                                                                                                                                                                                               "\"hex\"             \n"
                                                                                                                                                                                                                               "\nExamples:\n"
                                                                                                                                                                                                                               "\nCreate a transaction with no inputs\n"
                + HelpExampleCli("createrawtransaction", "\"[]\" \"{\\\"myaddress\\\":0.01}\"") +
                "\nAdd sufficient unsigned inputs to meet the output value\n"
                + HelpExampleCli("fundrawtransaction", "\"rawtransactionhex\"") +
                "\nSign the transaction\n"
                + HelpExampleCli("signrawtransaction", "\"fundedtransactionhex\"") +
                "\nSend the transaction\n"
                + HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\"")
                );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    CTxDestination changeAddress = CNoDestination();
    int changePosition = -1;
    bool includeWatching = false;
    bool lockUnspents = false;
    bool fPrivate = false;
    CFeeRate feeRate = CFeeRate(0);
    bool overrideEstimatedFeerate = false;

    if (params.size() > 1) {
        if (params[1].type() == UniValue::VBOOL) {
            // backward compatibility bool only fallback
            includeWatching = params[1].get_bool();
        }
        else {
            RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VOBJ));

            UniValue options = params[1];

            RPCTypeCheckObj(options,
                            {
                                {"changeAddress", UniValueType(UniValue::VSTR)},
                                {"changePosition", UniValueType(UniValue::VNUM)},
                                {"includeWatching", UniValueType(UniValue::VBOOL)},
                                {"lockUnspents", UniValueType(UniValue::VBOOL)},
                                {"private", UniValueType(UniValue::VBOOL)},
                                {"feeRate", UniValueType()}, // will be checked below
                            },
                            true, true);

            if (options.exists("changeAddress")) {
                CStockAddress address(options["changeAddress"].get_str());

                if (!address.IsValid())
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "changeAddress must be a valid stock address");

                changeAddress = address.Get();
            }

            if (options.exists("private"))
                fPrivate = options["private"].get_bool();

            if (options.exists("changePosition"))
                changePosition = options["changePosition"].get_int();

            if (options.exists("includeWatching"))
                includeWatching = options["includeWatching"].get_bool();

            if (options.exists("lockUnspents"))
                lockUnspents = options["lockUnspents"].get_bool();

            if (options.exists("feeRate"))
            {
                feeRate = CFeeRate(AmountFromValue(options["feeRate"]));
                overrideEstimatedFeerate = true;
            }
        }
    }

    // parse hex string from parameter
    CTransaction origTx;
    if (!DecodeHexTx(origTx, params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    if (origTx.vout.size() == 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "TX must have at least one output");

    if (changePosition != -1 && (changePosition < 0 || (unsigned int)changePosition > origTx.vout.size()))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "changePosition out of bounds");

    CMutableTransaction tx(origTx);
    CAmount nFeeOut;
    std::string strFailReason;

    if(!pwalletMain->FundTransaction(tx, nFeeOut, overrideEstimatedFeerate, feeRate, changePosition, strFailReason, includeWatching, lockUnspents, changeAddress, fPrivate))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(tx));
    result.pushKV("changepos", changePosition);
    result.pushKV("fee", ValueFromAmount(nFeeOut));

    return result;
}

// ///////////////////////////////////////////////////////////////////// ** em52
//  new rpc added by Remy5

struct StakeRange {
    int64_t Start;
    int64_t End;
    int64_t Total;
    int Count;
    std::string Name;
};

typedef std::vector<StakeRange> vStakeRange;

// Check if we have a Tx that can be counted in staking report
bool IsTxCountedAsStaked(const CWalletTx* tx)
{
    // Make sure we have a lock
    LOCK(cs_main);

    // orphan block or immature
    if ((!tx->GetDepthInMainChain()) || (tx->GetBlocksToMaturity() > 0) || !tx->IsInMainChain())
        return false;

    // abandoned transactions
    if (tx->isAbandoned())
        return false;

    // transaction other than POS block
    return tx->IsCoinStake();
}

// Get the amount for a staked tx used in staking report
CAmount GetTxStakeAmount(const CWalletTx* tx)
{
    // use the cached amount if available
    if ((tx->fCreditCached || tx->fColdStakingCreditCached) && (tx->fDebitCached || tx->fColdStakingDebitCached))
        return tx->nCreditCached + tx->nColdStakingCreditCached - tx->nDebitCached - tx->nColdStakingDebitCached;
    // Check for cold staking
    else if (tx->vout[1].scriptPubKey.IsColdStaking() || tx->vout[1].scriptPubKey.IsColdStakingv2())
        return tx->GetCredit(pwalletMain->IsMine(tx->vout[1])) - tx->GetDebit(pwalletMain->IsMine(tx->vout[1]));

    return tx->GetCredit(ISMINE_SPENDABLE) + tx->GetCredit(ISMINE_STAKABLE) - tx->GetDebit(ISMINE_SPENDABLE) - tx->GetDebit(ISMINE_STAKABLE);
}

// Gets timestamp for first stake
// Returns -1 (Zero) if has not staked yet
int64_t GetFirstStakeTime()
{
    // Check if we already know when
    if (nWalletFirstStakeTime > 0)
        return nWalletFirstStakeTime;

    // Need a pointer for the tx
    const CWalletTx* tx;

    // scan the entire wallet transactions
    for(auto& it: pwalletMain->wtxOrdered)
    {
        tx = it.second.first;

        // Check if we have a useable tx
        if (IsTxCountedAsStaked(tx)) {
            nWalletFirstStakeTime = tx->nTime; // Save it for later use
            return nWalletFirstStakeTime;
        }
    }

    // Did not find the first stake
    return nWalletFirstStakeTime;
}

// **em52: Get total coins staked on given period
// inspired from CWallet::GetStake()
// Parameter vRange = Vector with given limit date, and result
// return int =  Number of Wallet's elements analyzed
int GetsStakeSubTotal(vStakeRange& vRange)
{
    // Lock cs_main before we try to call GetTxStakeAmount
    LOCK(cs_main);

    int nElement = 0;
    int64_t nAmount = 0;

    const CWalletTx* pcoin;

    vStakeRange::iterator vIt;

    // scan the entire wallet transactions
    for (std::map<uint256, CWalletTx>::const_iterator it = pwalletMain->mapWallet.begin();
         it != pwalletMain->mapWallet.end();
         ++it)
    {
        pcoin = &(*it).second;

        // Check if we have a useable tx
        if (!IsTxCountedAsStaked(pcoin))
            continue;

        nElement++;

        // Get the stake tx amount from pcoin
        nAmount = GetTxStakeAmount(pcoin);

        // scan the range
        for(vIt=vRange.begin(); vIt != vRange.end(); vIt++)
        {
            if (pcoin->nTime >= vIt->Start)
            {
                if (! vIt->End)
                {   // Manage Special case
                    vIt->Start = pcoin->nTime;
                    vIt->Total = nAmount;
                }
                else if (pcoin->nTime <= vIt->End)
                {
                    vIt->Count++;
                    vIt->Total += nAmount;
                }
            }
        }
    }

    return nElement;
}

// prepare range for stake report
vStakeRange PrepareRangeForStakeReport(bool fNoDaily = false)
{
    vStakeRange vRange;
    StakeRange x;

    int64_t n1Hour = 60*60;
    int64_t n1Day = 24 * n1Hour;

    int64_t nToday = GetTime();
    time_t CurTime = nToday;
    auto localTime = boost::posix_time::second_clock::local_time();
    struct tm Loc_MidNight = boost::posix_time::to_tm(localTime);

    Loc_MidNight.tm_hour = 0;
    Loc_MidNight.tm_min = 0;
    Loc_MidNight.tm_sec = 0;  // set midnight

    x.Start = mktime(&Loc_MidNight);
    x.End   = nToday;
    x.Count = 0;
    x.Total = 0;

    if (!fNoDaily) {
        // prepare last single 30 day Range
        for(int i=0; i<30; i++)
        {
            x.Name = DateTimeStrFormat("%Y-%m-%d %H:%M:%S",x.Start);

            vRange.push_back(x);

            x.End    = x.Start - 1;
            x.Start -= n1Day;
        }
    }

    // prepare subtotal range of last 24H, 1 week, 30 days, 1 years
    int GroupDays[5][2] = { {1, 0}, {7, 0}, {30, 0}, {365, 0}, {99999999, 0}};
    std::string sGroupName[] = {"24H", "7 Days", "30 Days", "365 Days", "All" };

    nToday = GetTime();

    for(int i=0; i<5; i++)
    {
        x.Start = nToday - GroupDays[i][0] * n1Day;
        x.End   = nToday - GroupDays[i][1] * n1Day;
        x.Name = "Last " + sGroupName[i];

        vRange.push_back(x);
    }

    // Special case. not a subtotal, but last stake
    x.End  = 0;
    x.Start = 0;
    x.Name = "Latest Stake";
    vRange.push_back(x);

    return vRange;
}


// getstakereport: return SubTotal of the staked coin in last 24H, 7 days, etc.. of all owns address
UniValue getstakereport(const UniValue& params, bool fHelp)
{
    if ((params.size()>0) || (fHelp))
        throw std::runtime_error(
                "getstakereport\n"
                "List last single 30 day stake subtotal and last 24h, 7, 30, 365 day subtotal.\n");

    vStakeRange vRange = PrepareRangeForStakeReport();

    LOCK(cs_main);

    // get subtotal calc
    int64_t nTook = GetTimeMillis();
    int nItemCounted = GetsStakeSubTotal(vRange);

    UniValue result(UniValue::VOBJ);

    vStakeRange::iterator vIt;

    // Span of days to compute average over
    int nDays = 0;

    // Get the wallet's staking age in days
    int nWalletDays = 0;

    // Check if we have a stake already
    if (GetFirstStakeTime() != -1)
        nWalletDays = (GetTime() - GetFirstStakeTime()) / 86400;

    // report it
    for(vIt = vRange.begin(); vIt != vRange.end(); vIt++)
    {
        // Add it to results
        result.pushKV(vIt->Name, FormatMoney(vIt->Total).c_str());

        // Get the nDays value
        nDays = 0;
        if (vIt->Name == "Last 7 Days")
            nDays = 7;
        else if (vIt->Name == "Last 30 Days")
            nDays = 30;
        else if (vIt->Name == "Last 365 Days")
            nDays = 365;

        // Check if we need to add the average
        if (nDays > 0) {
            // Check if nDays is larger than the wallet's staking age in days
            if (nDays > nWalletDays && nWalletDays > 0)
                nDays = nWalletDays;

            // Add the Average
            result.pushKV(vIt->Name + " Avg", FormatMoney(vIt->Total / nDays).c_str());
        }
    }

    vIt--;
    result.pushKV("Latest Time",
                  vIt->Start ? DateTimeStrFormat("%Y-%m-%d %H:%M:%S",vIt->Start).c_str() :
                               "Never");

    // Moved nTook call down here to be more accurate
    nTook = GetTimeMillis() - nTook;

    // report element counted / time took
    result.pushKV("Stake counted", nItemCounted);
    result.pushKV("time took (ms)",  nTook);

    return  result;
}

UniValue resolveopenalias(const UniValue& params, bool fHelp)
{
    bool dnssec_available; bool dnssec_valid;
    UniValue result(UniValue::VOBJ);

    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if ((fHelp || params.size() != 1))
        throw std::runtime_error(
                "resolveopenallias \"openAlias\"\n"
                "\nResolves the given OpenAlias address to a Stock address.\n"
                "\nArguments:\n"
                "1. \"address\"    (string) The OpenAlias address.\n"
                "\nExamples:\n"
                "\nGet information about an OpenAlias address\n"
                + HelpExampleCli("resolveopenalias", "\"donate@stock.org\"")
                );

    std::string address = params[0].get_str();

    std::vector<std::string> addresses = utils::dns_utils::addresses_from_url(address, dnssec_available, dnssec_valid);

    result.pushKV("dnssec_available",dnssec_available);
    result.pushKV("dnssec_valid",dnssec_valid);

    if (addresses.empty())
        result.pushKV("address","");
    else
        result.pushKV("address",addresses.front());

    return result;
}

UniValue proposalvotelist(const UniValue& params, bool fHelp)
{

    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw std::runtime_error(
                "proposalvotelist\n"

                "\nReturns a list containing the wallet's current voting status for all active proposals.\n"

                "\nResult:\n"
                "{\n"
                "      \"yes\":   List of proposals this wallet is casting a 'yes' vote for.\n"
                "      \"no\":    List of proposals this wallet is casting a 'no' vote for.\n"
                "      \"abs\":   List of proposals this wallet is casting an 'abstain' vote for.\n"
                "      \"null\":  List of proposals this wallet has NOT yet cast a vote for.\n"
                "}\n"
                );

    LOCK(cs_main);

    CStateViewCache coins(pcoinsTip);

    UniValue ret(UniValue::VOBJ);
    UniValue yesvotes(UniValue::VARR);
    UniValue novotes(UniValue::VARR);
    UniValue absvotes(UniValue::VARR);
    UniValue nullvotes(UniValue::VARR);

    CProposalMap mapProposals;
    CStateViewCache view(pcoinsTip);

    if(view.GetAllProposals(mapProposals))
    {
        for (CProposalMap::iterator it_ = mapProposals.begin(); it_ != mapProposals.end(); it_++)
        {
            CProposal proposal;

            if (!view.GetProposal(it_->first, proposal))
                continue;

            if (proposal.GetLastState() != DAOFlags::NIL)
                continue;

            auto it = mapAddedVotes.find(proposal.hash);

            UniValue p(UniValue::VOBJ);
            proposal.ToJson(p, view);
            if (it != mapAddedVotes.end()) {
                if (it->second == 1)
                    yesvotes.push_back(p);
                else if (it->second == -1)
                    absvotes.push_back(p);
                else if (it->second == 0)
                    novotes.push_back(p);
            } else
                nullvotes.push_back(p);

        }
    }

    ret.pushKV("yes",yesvotes);
    ret.pushKV("no",novotes);
    ret.pushKV("abs",absvotes);
    ret.pushKV("null",nullvotes);

    return ret;
}

UniValue support(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1)
        throw std::runtime_error(
                "support \"hash\" ( add )\n"
            "\nShows support for the consultation or consultation answer identified by \"hash\".\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The hash\n"
            "2. \"add\"           (bool, optional) Set to false to remove support (Default: true)\n"
        );

    LOCK(cs_main);

    bool fRemove = params.size() > 1 ? !params[1].getBool() : false;

    std::string strHash = params[0].get_str();
    uint256 hash = uint256S(strHash);
    bool duplicate = false;

    CStateViewCache coins(pcoinsTip);
    CConsultation consultation;
    CConsultationAnswer answer;

    if (!((coins.GetConsultation(hash, consultation) && consultation.CanBeSupported()) || (coins.GetConsultationAnswer(hash, answer) && answer.CanBeSupported(coins))))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not find a valid entry with hash ")+strHash);
    }

    if (fRemove)
    {
        bool ret = RemoveSupport(strHash);
        if (!ret)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The hash is not on the list: ")+strHash);
        }
        else
        {
            return NullUniValue;
        }
    }
    else
    {
        bool ret = Support(hash, duplicate);
        if (duplicate)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The hash is already on the list: ")+strHash);
        }
        else if (ret)
        {
            return NullUniValue;
        }
    }

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not find ")+strHash);

}

UniValue consultationvote(const UniValue& params, bool fHelp)
{
    std::string strCommand;

    if (params.size() >= 2)
        strCommand = params[1].get_str();

    if (fHelp || params.size() < 2 ||
            (strCommand != "yes" && strCommand != "value"  && strCommand != "abs" && strCommand != "remove"))
        throw std::runtime_error(
                "consultationvote \"hash\" \"yes|value|abs|remove\" ( value )\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The consultation/answer hash\n"
            "2. \"command\"       (string, required) 'yes' to vote yes, 'value' to vote for a range,\n"
            "                      'abs' to abstain, 'remove' to remove a vote from the list\n"
            "3. \"value\"         (integer, required) For consultations where the answer is a range,\n"
            "                      this sets the value to vote for\n"
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash = uint256S(strHash);
    bool duplicate = false;

    CStateViewCache coins(pcoinsTip);
    CConsultation consultation;
    CConsultationAnswer answer;

    int64_t nVote;

    bool fConsultation = coins.HaveConsultation(hash);
    bool fConsultationAnswer = coins.HaveConsultationAnswer(hash);

    if (!fConsultation && !fConsultationAnswer)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not find ")+hash.ToString());

    if (fConsultation)
    {
        if (!coins.GetConsultation(hash, consultation))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not read consultation ")+hash.ToString());

        if (!consultation.CanBeVoted() && strCommand != "abs")
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The consultation can not be voted."));

        if (strCommand == "yes")
            throw JSONRPCError(RPC_INVALID_PARAMS, std::string("This consultation does not admit a yes vote."));
        else if (strCommand != "remove" && strCommand != "abs")
        {
            nVote = params[2].get_int64();
            if (!consultation.IsValidVote(nVote))
                throw JSONRPCError(RPC_INVALID_PARAMS, std::string("The vote is out of range"));
        }
    }
    else if (fConsultationAnswer)
    {
        if (!coins.GetConsultationAnswer(hash, answer))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not read answer ")+hash.ToString());

        if (!coins.GetConsultation(answer.parent, consultation))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not read parent consultation ")+answer.parent.ToString());

        if (!answer.CanBeVoted(coins))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The answer can not be voted."));

        if (strCommand == "value")
            throw JSONRPCError(RPC_INVALID_PARAMS, std::string("This consultation's answer is not a value range."));
    }

    if (strCommand == "yes" || strCommand == "abs")
    {
        if (strCommand == "yes")
            nVote = 1;

        if (strCommand == "abs")
            nVote = -1;

        bool ret = Vote(hash,nVote,duplicate);
        if (duplicate)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The hash is already in the list: ")+strHash);
        }
        else if (ret)
        {
            return NullUniValue;
        }
    }
    else if (strCommand == "value")
    {
        bool ret = VoteValue(hash,nVote,duplicate);
        if (duplicate)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The hash is already in the list: ")+strHash);
        }
        else if (ret)
        {
            return NullUniValue;
        }
    }
    else if(strCommand == "remove")
    {
        bool ret = fConsultation ? RemoveVoteValue(strHash) : RemoveVote(strHash);
        if (ret)
        {
            return NullUniValue;
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The hash is not in the list: ")+strHash);
        }
    }
    return NullUniValue;
}

UniValue supportlist(const UniValue& params, bool fHelp)
{

    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw std::runtime_error(
                "supportlist\n"

                "\nReturns a list containing the wallet's current support status for all active consultations.\n"

        );

    LOCK(cs_main);

    CStateViewCache coins(pcoinsTip);
    UniValue ret(UniValue::VARR);
    CConsultation consultation;
    CConsultationAnswer answer;

    for (auto& it: mapSupported)
    {
        bool fConsultation = coins.HaveConsultation(it.first) && coins.GetConsultation(it.first, consultation) && consultation.CanBeSupported();
        bool fConsultationAnswer = coins.HaveConsultationAnswer(it.first) && coins.GetConsultationAnswer(it.first, answer) && answer.CanBeSupported(coins);

        if (fConsultation || fConsultationAnswer)
        {
            ret.push_back(it.first.ToString());
        }
    }

    return ret;
}

UniValue consultationvotelist(const UniValue& params, bool fHelp)
{

    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw std::runtime_error(
                "consultationvotelist\n"

                "\nReturns a list containing the wallet's current voting status for all active consultations.\n"

        );

    LOCK(cs_main);

    CStateViewCache coins(pcoinsTip);
    UniValue ret(UniValue::VARR);
    CConsultation consultation;
    CConsultationAnswer answer;

    for (auto& it: mapAddedVotes)
    {
        bool fConsultation = coins.HaveConsultation(it.first) && coins.GetConsultation(it.first, consultation) && consultation.CanBeVoted();
        bool fConsultationAnswer = coins.HaveConsultationAnswer(it.first) && coins.GetConsultationAnswer(it.first, answer) && answer.CanBeVoted(coins);

        if (fConsultation)
        {
            UniValue entry(UniValue::VOBJ);
            entry.pushKV(it.first.ToString(), (it.second == -1) ? "abstain" : std::to_string(it.second));
            ret.push_back(entry);
        }
        else if (fConsultationAnswer)
        {
            UniValue entry(UniValue::VOBJ);
            entry.pushKV(it.first.ToString(), (it.second == -1) ? "abstain" : (it.second == 1 ? "yes" : "unknown") );
            ret.push_back(entry);
        }
    }

    return ret;
}

UniValue proposalvote(const UniValue& params, bool fHelp)
{
    std::string strCommand;

    if (params.size() >= 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() > 3 ||
            (strCommand != "yes" && strCommand != "no"  && strCommand != "abs" && strCommand != "remove"))
        throw std::runtime_error(
                "proposalvote \"proposal_hash\" \"yes|no|abs|remove\"\n"
            "\nAdds a proposal to the list of votes.\n"
            "\nArguments:\n"
            "1. \"proposal_hash\" (string, required) The proposal hash\n"
            "2. \"command\"       (string, required) 'yes' to vote yes, 'no' to vote no,\n"
            "                      'abs' to abstain, 'remove' to remove a proposal from the list\n"
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    bool duplicate = false;

    CStateViewCache coins(pcoinsTip);
    CProposal proposal;

    if (!coins.GetProposal(uint256S(strHash), proposal))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not find proposal ")+strHash);
    }

    if (strCommand == "yes" || strCommand == "no" || strCommand == "abs")
    {
        int vote = 0;

        if (strCommand == "yes")
            vote = 1;

        if (strCommand == "abs")
            vote = -1;

        bool ret = Vote(proposal.hash,vote,duplicate);
        if (duplicate)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The proposal is already in the list: ")+strHash);
        }
        else if (ret)
        {
            return NullUniValue;
        }
    }
    else if(strCommand == "remove")
    {
        bool ret = RemoveVote(strHash);
        if (ret)
        {
            return NullUniValue;
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The proposal is not in the list: ")+strHash);
        }
    }

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not find proposal ")+strHash);
}

UniValue getstakervote(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "getstakervote <stakerscript>\n"
                "\nReturns a list of all the votes stored for a staker script.\n"
                "\nResult:\n"
                "[\n"
                "     {\n"
                "          \"height\":   height of the vote,\n"
                "          \"hash\":     hash of the item,\n"
                "          \"val\":      value of the vote\n"
                "     }\n"
                "]\n"
                );

    std::string data = params[0].get_str();

    if (!IsHex(data))
        throw JSONRPCError(RPC_MISC_ERROR, "the script is not expressed in hexadecimal");

    LOCK(cs_main);

    std::vector<unsigned char> stakerScript = ParseHex(data);
    CStateViewCache view(pcoinsTip);
    UniValue ret(UniValue::VARR);
    CVoteList pVoteList;

    if (!view.GetCachedVoter(stakerScript, pVoteList))
    {
        return ret;
    }

    std::map<int, std::map<uint256, int64_t>>* list= pVoteList.GetFullList();

    for (auto& it: *list)
    {
        for (auto& it2: it.second)
        {
            int64_t val = it2.second;

            UniValue entry(UniValue::VOBJ);
            entry.pushKV("height", it.first);
            entry.pushKV("hash", it2.first.ToString());
            entry.pushKV("val", val);
            ret.push_back(entry);
        }
    }

    return ret;
}

UniValue paymentrequestvotelist(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
                "paymentrequestvotelist\n"

                "\nReturns a list containing the wallet's current voting status for all active payment requests.\n"

                "\nResult:\n"
                "{\n"
                "      \"yes\":   List of proposals this wallet is casting a 'yes' vote for.\n"
                "      \"no\":    List of proposals this wallet is casting a 'no' vote for.\n"
                "      \"abs\":   List of proposals this wallet is casting an 'abstain' vote for.\n"
                "      \"null\":  List of proposals this wallet has NOT yet cast a vote for.\n"
                "}\n"
                );

    UniValue ret(UniValue::VOBJ);
    UniValue yesvotes(UniValue::VARR);
    UniValue novotes(UniValue::VARR);
    UniValue absvotes(UniValue::VARR);
    UniValue nullvotes(UniValue::VARR);

    CPaymentRequestMap mapPaymentRequests;
    CStateViewCache view(pcoinsTip);

    if(view.GetAllPaymentRequests(mapPaymentRequests))
    {
        for (CPaymentRequestMap::iterator it_ = mapPaymentRequests.begin(); it_ != mapPaymentRequests.end(); it_++)
        {
            CPaymentRequest prequest;

            if (!view.GetPaymentRequest(it_->first, prequest))
                continue;

            if (prequest.GetLastState() != DAOFlags::NIL)
                continue;

            auto it = mapAddedVotes.find(prequest.hash);

            UniValue p(UniValue::VOBJ);
            prequest.ToJson(p, view);

            if (it != mapAddedVotes.end())
            {
                if (it->second == 1)
                    yesvotes.push_back(p);
                else if (it->second == -1)
                    absvotes.push_back(p);
                else if (it->second == 0)
                    novotes.push_back(p);
            } else
                nullvotes.push_back(p);
        }
    }

    ret.pushKV("yes",yesvotes);
    ret.pushKV("no",novotes);
    ret.pushKV("abs",absvotes);
    ret.pushKV("null",nullvotes);

    return ret;
}

UniValue paymentrequestvote(const UniValue& params, bool fHelp)
{
    std::string strCommand;

    if (params.size() >= 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() > 3 ||
            (strCommand != "yes" && strCommand != "no" && strCommand != "abs" && strCommand != "remove"))
        throw std::runtime_error(
                "paymentrequestvote \"request_hash\" \"yes|no|abs|remove\"\n"
            "\nAdds/removes a proposal to the list of votes.\n"
            "\nArguments:\n"
            "1. \"request_hash\" (string, required) The payment request hash\n"
            "2. \"command\"       (string, required) 'yes' to vote yes, 'no' to vote no,\n"
            "                      'abs' to abstain, 'remove' to remove a proposal from the list\n"
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    bool duplicate = false;

    CPaymentRequest prequest;
    CStateViewCache view(pcoinsTip);

    if (!view.GetPaymentRequest(uint256S(strHash), prequest))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not find payment request: ")+strHash);
    }

    if (strCommand == "yes" || strCommand == "no" || strCommand == "abs")
    {
        int vote = 0;

        if (strCommand == "yes")
            vote = 1;

        if (strCommand == "abs")
            vote = -1;

        bool ret = Vote(prequest.hash,vote,duplicate);
        if (duplicate) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The payment request is already in the list: ")+strHash);
        } else if (ret) {
            return NullUniValue;
        }
    }
    else if(strCommand == "remove")
    {
        bool ret = RemoveVote(strHash);
        if (ret) {
            return NullUniValue;
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("The payment request is not in the list: ")+strHash);
        }
    }

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Could not find payment request: ")+strHash);

}

UniValue listproposals(const UniValue& params, bool fHelp)
{
    if (fHelp)
        throw std::runtime_error(
                "listproposals \"filter\"\n"
                "\nList the proposals and all the relating data including payment requests and status.\n"
                "\nNote passing no argument returns all proposals regardless of state.\n"
                "\nArguments:\n"
                "\n1. \"filter\" (string, optional)   \"accepted\" | \"rejected\" | \"expired\" | \"pending\" | \"mine\" | \"accepted_expired\"\n"
                "\nExamples:\n"
                + HelpExampleCli("listproposal", "mine accepted")
                + HelpExampleCli("listproposal", "accepted")
                + HelpExampleRpc("listproposal", "")
                );

    LOCK(cs_main);

    UniValue ret(UniValue::VARR);

    bool showAll = true;
    bool showAccepted = false;
    bool showRejected = false;
    bool showExpired = false;
    bool showAcceptedExpired = false;
    bool showPending = false;
    bool showMine = false;
    for(unsigned int i = 0; i < params.size(); i++) {
        if(params[i].get_str() == "accepted") {
            showAccepted = true;
            showAll = false;
        }
        else if(params[i].get_str() == "rejected") {
            showRejected = true;
            showAll = false;
        }
        else if(params[i].get_str() == "expired") {
            showAll = false;
            showExpired = true;
        }
        else if(params[i].get_str() == "pending") {
            showAll = false;
            showPending = true;
        }
        else if(params[i].get_str() == "mine") {
            showAll = false;
            showMine = true;
        }
        else if(params[i].get_str() == "accepted_expired") {
            showAll = false;
            showAcceptedExpired = true;
        }
    }

    CProposalMap mapProposals;
    CStateViewCache view(pcoinsTip);

    if(view.GetAllProposals(mapProposals))
    {
        for (CProposalMap::iterator it = mapProposals.begin(); it != mapProposals.end(); it++)
        {
            CProposal proposal;
            if (!view.GetProposal(it->first, proposal))
                continue;

            flags fLastState = proposal.GetLastState();

            bool fIsMine = false;

            if (showMine)
            {
                CTxDestination address(CStockAddress(proposal.GetOwnerAddress()).Get());
                isminefilter mine = IsMine(*pwalletMain, address);
                if(mine & ISMINE_SPENDABLE)
                    fIsMine = true;
            }


            if(showAll
                    || (showMine && fIsMine)
                    || (showPending  && (fLastState == DAOFlags::NIL || fLastState == DAOFlags::PENDING_VOTING_PREQ
                                         || fLastState == DAOFlags::PENDING_FUNDS))
                    || (showAccepted && (fLastState == DAOFlags::ACCEPTED))
                    || (showAcceptedExpired && (fLastState == DAOFlags::ACCEPTED_EXPIRED))
                    || (showRejected && (fLastState == DAOFlags::REJECTED))
                    || (showExpired  &&  proposal.IsExpired(pindexBestHeader->GetBlockTime(), view))) {
                UniValue o(UniValue::VOBJ);
                proposal.ToJson(o, view);
                ret.push_back(o);
            }
        }
    }
    return ret;
}

UniValue listtokens(const UniValue& params, bool fHelp)
{
    if (fHelp)
        throw std::runtime_error(
                "listtokens (mine)\n"
                "\nList the confidential tokens.\n"

                "\nArguments:\n"
                "1. mine          (boolean, optional, default=false) Show only owned tokens\n"

                "\nExamples:\n"
                + HelpExampleCli("listtokens", "")
                + HelpExampleCli("listtokens", "true")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    bool fMine = params[0].getBool();

    UniValue ret(UniValue::VARR);
    TokenMap mapTokens;
    CStateViewCache view(pcoinsTip);

    if(view.GetAllTokens(mapTokens))
    {
        for (TokenMap::iterator it = mapTokens.begin(); it != mapTokens.end(); it++)
        {
            TokenInfo token;
            if (!view.GetToken(it->first, token))
                continue;

            // Check for regular tokens
            if (it->second.nVersion != 0)
                continue;

            UniValue o(UniValue::VOBJ);
            o.pushKV("version", it->second.nVersion);
            o.pushKV("id", it->first.ToString());
            o.pushKV("pubkey", HexStr(it->second.key.Serialize()));
            o.pushKV("name", it->second.sName);
            o.pushKV(it->second.nVersion == 0 ? "token_code" : "scheme", it->second.sDesc);
            o.pushKV("current_supply", it->second.nVersion == 0 ? FormatMoney(it->second.currentSupply) : std::to_string(it->second.mapMetadata.size()));
            o.pushKV("max_supply", it->second.nVersion == 0 ? FormatMoney(it->second.totalSupply) : std::to_string(it->second.totalSupply));
            o.pushKV("balance", FormatMoney(pwalletMain->GetPrivateBalance(TokenId(it->first, -1))));

            // Is this token ours?
            bool fTokenIsMine = false;

            blsctKey pk;
            if (!pwalletMain->GetBLSCTTokenKey(it->second.key, pk))
            {
                blsctKey sk;

                if (!pwalletMain->GetBLSCTSpendKey(sk))
                    throw JSONRPCError(RPC_TYPE_ERROR, "Wallet not available");

                pk = sk.PrivateChildHash(SerializeHash("nft/"+it->second.sName+it->second.sDesc));

                pwalletMain->AddBLSCTTokenKey(pk);
            }

            if (pk.GetG1Element() == it->second.key)
                fTokenIsMine = true;

            o.pushKV("is_mine", fTokenIsMine);
            if (!fMine || (fMine && fTokenIsMine))
                ret.push_back(o);
        }
    }
    return ret;
}

UniValue listnfts(const UniValue& params, bool fHelp)
{
    if (fHelp)
        throw std::runtime_error(
                "listnfts (mine) (with_utxo)\n"
                "\nList the confidential tokens.\n"

                "\nArguments:\n"
                "1. mine          (boolean, optional, default=false) Show only owned tokens\n"
                "2. with_utxo     (boolean, optional, default=false) Show last utxo for nfts\n"

                "\nExamples:\n"
                + HelpExampleCli("listnfts", "")
                + HelpExampleCli("listnfts", "true")
                + HelpExampleCli("listnfts", "true true")
                );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    bool fMine = params[0].getBool();
    bool fWithUtxo = params.size() > 1 ? params[1].getBool() : false;

    UniValue ret(UniValue::VARR);
    TokenMap mapTokens;
    CStateViewCache view(pcoinsTip);

    if(view.GetAllTokens(mapTokens))
    {
        for (TokenMap::iterator it = mapTokens.begin(); it != mapTokens.end(); it++)
        {
            TokenInfo token;
            if (!view.GetToken(it->first, token))
                continue;

            // Check for nft
            if (it->second.nVersion != 1)
                continue;

            UniValue o(UniValue::VOBJ);
            o.pushKV("version", it->second.nVersion);
            o.pushKV("id", it->first.ToString());
            o.pushKV("pubkey", HexStr(it->second.key.Serialize()));
            o.pushKV("name", it->second.sName);
            o.pushKV(it->second.nVersion == 0 ? "token_code" : "scheme", it->second.sDesc);
            o.pushKV("current_supply", it->second.nVersion == 0 ? FormatMoney(it->second.currentSupply) : std::to_string(it->second.mapMetadata.size()));
            o.pushKV("max_supply", it->second.nVersion == 0 ? FormatMoney(it->second.totalSupply) : std::to_string(it->second.totalSupply));

            UniValue a(UniValue::VARR);

            for (auto& it_: it->second.mapMetadata) {
                UniValue n(UniValue::VOBJ);
                n.pushKV("index", it_.first);
                n.pushKV("metadata", it_.second);
                n.pushKV("balance", pwalletMain->GetPrivateBalance(TokenId(it->first, it_.first)));

                TokenUtxoValues utxos;
                if (fWithUtxo && view.GetTokenUtxos(SerializeHash(TokenId(it->first, it_.first)), utxos)) {
                    if (utxos.size() > 0) {
                        TokenUtxoValue txout;
                        auto i = utxos.end();
                        while (i != utxos.begin())
                        {
                            --i;
                            if (!i->second.IsNull()) {
                                txout = i->second;
                                break;
                            }
                        }
                        if (!txout.IsNull()) {
                            UniValue utxo(UniValue::VOBJ);
                            utxo.pushKV("n", std::to_string(txout.n));
                            utxo.pushKV("hash", txout.hash.ToString());
                            utxo.pushKV("spendingKey", HexStr(txout.spendingKey));
                            n.pushKV("utxo", utxo);
                        }
                    }
                }

                a.push_back(n);
            }

            o.pushKV("nfts", a);

            // Is this token ours?
            bool fTokenIsMine = false;

            blsctKey pk;
            if (!pwalletMain->GetBLSCTTokenKey(it->second.key, pk))
            {
                blsctKey sk;

                if (!pwalletMain->GetBLSCTSpendKey(sk))
                    throw JSONRPCError(RPC_TYPE_ERROR, "Wallet not available");

                pk = sk.PrivateChildHash(SerializeHash("nft/"+it->second.sName+it->second.sDesc));

                pwalletMain->AddBLSCTTokenKey(pk);
            }

            if (pk.GetG1Element() == it->second.key)
                fTokenIsMine = true;

            o.pushKV("is_mine", fTokenIsMine);
            if (!fMine || (fMine && fTokenIsMine))
                ret.push_back(o);
        }
    }

    return ret;
}

UniValue gettoken(const UniValue& params, bool fHelp)
{
    if (fHelp)
        throw std::runtime_error(
                "gettoken hash (with_utxo)\n"
                "\nShows information about a token.\n"

                + HelpExampleCli("gettoken", "90fc7410164a466b78096967ec948fcc13142b0f5fb4397462304c517840d74f")
                + HelpExampleCli("gettoken", "90fc7410164a466b78096967ec948fcc13142b0f5fb4397462304c517840d74f true")
                );

    LOCK(cs_main);

    bool fWithUtxo = params.size() > 1 ? params[1].getBool() : false;

    UniValue ret(UniValue::VOBJ);

    CStateViewCache view(pcoinsTip);

    TokenInfo token;
    if (!view.GetToken(uint256S(params[0].get_str()), token))
        return ret;

    ret.pushKV("version", token.nVersion);
    ret.pushKV("id", params[0].get_str());
    ret.pushKV("pubkey", HexStr(token.key.Serialize()));
    ret.pushKV("name", token.sName);
    ret.pushKV(token.nVersion == 0 ? "token_code" : "scheme", token.sDesc);
    ret.pushKV("current_supply", token.nVersion == 0 ? FormatMoney(token.currentSupply) : std::to_string(token.mapMetadata.size()));
    ret.pushKV("max_supply", token.nVersion == 0 ? FormatMoney(token.totalSupply) : std::to_string(token.totalSupply));
    ret.pushKV("balance", FormatMoney(pwalletMain->GetPrivateBalance(TokenId(uint256S(params[0].get_str()), -1))));

    return ret;
}

UniValue getnft(const UniValue& params, bool fHelp)
{
    if (fHelp)
        throw std::runtime_error(
                "getnft hash (subid) (with_utxo)\n"
                "\nShows information about a token.\n"

                + HelpExampleCli("getnft", "90fc7410164a466b78096967ec948fcc13142b0f5fb4397462304c517840d74f")
                + HelpExampleCli("getnft", "90fc7410164a466b78096967ec948fcc13142b0f5fb4397462304c517840d74f 1")
                + HelpExampleCli("getnft", "90fc7410164a466b78096967ec948fcc13142b0f5fb4397462304c517840d74f 1 true")
                );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    CStateViewCache view(pcoinsTip);

    TokenInfo token;
    if (!view.GetToken(uint256S(params[0].get_str()), token))
        return ret;

    if (token.nVersion != 1)
        return ret;

    int64_t nSubid = params.size() > 1 ? params[1].get_int() : -1;

    bool fWithUtxo = params.size() > 2 ? params[2].getBool() : false;

    ret.pushKV("version", token.nVersion);
    ret.pushKV("id", params[0].get_str());
    ret.pushKV("pubkey", HexStr(token.key.Serialize()));
    ret.pushKV("name", token.sName);
    ret.pushKV(token.nVersion == 0 ? "token_code" : "scheme", token.sDesc);
    ret.pushKV("current_supply", token.nVersion == 0 ? FormatMoney(token.currentSupply) : std::to_string(token.mapMetadata.size()));
    ret.pushKV("max_supply", token.nVersion == 0 ? FormatMoney(token.totalSupply) : std::to_string(token.totalSupply));
    UniValue a(UniValue::VARR);
    for (auto& it_: token.mapMetadata) {
        if (nSubid > -1 && it_.first != nSubid)
            continue;

        UniValue n(UniValue::VOBJ);
        n.pushKV("index", it_.first);
        n.pushKV("metadata", it_.second);
        n.pushKV("balance", pwalletMain->GetPrivateBalance(TokenId(uint256S(params[0].get_str()), it_.first)));

        TokenUtxoValues utxos;
        if (fWithUtxo && view.GetTokenUtxos(SerializeHash(TokenId(uint256S(params[0].get_str()), it_.first)), utxos)) {
            if (utxos.size() > 0) {
                TokenUtxoValue txout;
                auto i = utxos.end();
                while (i != utxos.begin())
                {
                    --i;
                    if (!i->second.IsNull()) {
                        txout = i->second;
                        break;
                    }
                }
                if (!txout.IsNull()) {
                    UniValue utxo(UniValue::VOBJ);
                    utxo.pushKV("n", std::to_string(txout.n));
                    utxo.pushKV("hash", txout.hash.ToString());
                    utxo.pushKV("spendingKey", HexStr(txout.spendingKey));
                    n.pushKV("utxo", utxo);
                }
            }
        }

        a.push_back(n);
    }

    ret.pushKV("nfts", a);

    return ret;
}


extern UniValue dumpprivkey(const UniValue& params, bool fHelp); // in rpcdump.cpp
extern UniValue dumpmasterprivkey(const UniValue& params, bool fHelp);
extern UniValue dumpmnemonic(const UniValue& params, bool fHelp);
extern UniValue importprivkey(const UniValue& params, bool fHelp);
extern UniValue importaddress(const UniValue& params, bool fHelp);
extern UniValue importpubkey(const UniValue& params, bool fHelp);
extern UniValue dumpwallet(const UniValue& params, bool fHelp);
extern UniValue importwallet(const UniValue& params, bool fHelp);
extern UniValue importprunedfunds(const UniValue& params, bool fHelp);
extern UniValue removeprunedfunds(const UniValue& params, bool fHelp);

static const CRPCCommand commands[] =
{ //  category              name                        actor (function)           okSafeMode
  //  --------------------- ------------------------    -----------------------    ----------
  { "wallet",             "getnewprivateaddress",     &getnewprivateaddress,     true  },
  { "wallet",             "listprivateunspent",       &listprivateunspent,       false },
  { "wallet",             "privatesendtoaddress",     &privatesendtoaddress,     false },
  { "wallet",             "privatesendmixtoaddress",  &privatesendmixtoaddress,  false },
  { "rawtransactions",    "fundrawtransaction",       &fundrawtransaction,       false },
  { "hidden",             "resendwallettransactions", &resendwallettransactions, true  },
  { "wallet",             "abandontransaction",       &abandontransaction,       false },
  { "wallet",             "addmultisigaddress",       &addmultisigaddress,       true  },
  { "wallet",             "addwitnessaddress",        &addwitnessaddress,        true  },
  { "wallet",             "backupwallet",             &backupwallet,             true  },
  { "wallet",             "createrawscriptaddress",   &createrawscriptaddress,   true  },
  { "wallet",             "dumpprivkey",              &dumpprivkey,              true  },
  { "wallet",             "dumpmasterprivkey",        &dumpmasterprivkey,        true  },
  { "wallet",             "dumpmnemonic",             &dumpmnemonic,             true  },
  { "wallet",             "dumpwallet",               &dumpwallet,               true  },
  { "wallet",             "burntoken",                &burntoken,                true  },
  { "wallet",             "minttoken",                &minttoken,                true  },
  { "wallet",             "mintnft",                  &mintnft,                  true  },
  { "wallet",             "sendtoken",                &sendtoken,                true  },
  { "wallet",             "sendnft",                  &sendnft,                  true  },
  { "wallet",             "createtoken",              &createtoken,              true  },
  { "wallet",             "createnft",                &createnft,                true  },
  { "wallet",             "encryptwallet",            &encryptwallet,            true  },
  { "wallet",             "encrypttxdata",            &encrypttxdata,            true  },
  { "wallet",             "getaccountaddress",        &getaccountaddress,        true  },
  { "wallet",             "getaccount",               &getaccount,               true  },
  { "wallet",             "getaddressesbyaccount",    &getaddressesbyaccount,    true  },
  { "wallet",             "listprivateaddresses",     &listprivateaddresses,     true  },
  { "wallet",             "listtokens",               &listtokens,               true  },
  { "wallet",             "listnfts",                 &listnfts,                 true  },
  { "wallet",             "gettoken",                 &gettoken,                 true  },
  { "wallet",             "getnft",                   &getnft,                   true  },
  { "wallet",             "getbalance",               &getbalance,               false },
  { "wallet",             "getnewaddress",            &getnewaddress,            true  },
  { "wallet",             "getcoldstakingaddress",    &getcoldstakingaddress,    true  },
  { "wallet",             "getrawchangeaddress",      &getrawchangeaddress,      true  },
  { "wallet",             "getreceivedbyaccount",     &getreceivedbyaccount,     false },
  { "wallet",             "getreceivedbyaddress",     &getreceivedbyaddress,     false },
  { "wallet",             "getstakereport",           &getstakereport,           false },
  { "wallet",             "gettransaction",           &gettransaction,           false },
  { "wallet",             "getunconfirmedbalance",    &getunconfirmedbalance,    false },
  { "wallet",             "getwalletinfo",            &getwalletinfo,            false },
  { "wallet",             "importprivkey",            &importprivkey,            true  },
  { "wallet",             "importwallet",             &importwallet,             true  },
  { "wallet",             "importaddress",            &importaddress,            true  },
  { "wallet",             "importprunedfunds",        &importprunedfunds,        true  },
  { "wallet",             "importpubkey",             &importpubkey,             true  },
  { "wallet",             "keypoolrefill",            &keypoolrefill,            true  },
  { "wallet",             "listaccounts",             &listaccounts,             false },
  { "wallet",             "listaddressgroupings",     &listaddressgroupings,     false },
  { "wallet",             "listlockunspent",          &listlockunspent,          false },
  { "wallet",             "listreceivedbyaccount",    &listreceivedbyaccount,    false },
  { "wallet",             "listreceivedbyaddress",    &listreceivedbyaddress,    false },
  { "wallet",             "listsinceblock",           &listsinceblock,           false },
  { "wallet",             "listtransactions",         &listtransactions,         false },
  { "wallet",             "listunspent",              &listunspent,              false },
  { "wallet",             "lockunspent",              &lockunspent,              true  },
  { "wallet",             "move",                     &movecmd,                  false },
  { "wallet",             "sendfrom",                 &sendfrom,                 false },
  { "wallet",             "sendmany",                 &sendmany,                 false },
  { "wallet",             "sendtoaddress",            &sendtoaddress,            false },
  { "communityfund",      "donatefund",               &donatefund,               false },
  { "communityfund",      "createpaymentrequest",     &createpaymentrequest,     false },
  { "communityfund",      "createproposal",           &createproposal,           false },
  { "dao",                "createconsultation",       &createconsultation,       false },
  { "dao",                "createconsultationwithanswers",
    &createconsultationwithanswers,
    false },
  { "dao",                "getstakervote",            &getstakervote,            false },
  { "dao",                "proposeanswer",            &proposeanswer,            false },
  { "dao",                "proposeconsensuschange",   &proposeconsensuschange,   false },
  { "dao",                "proposecombinedconsensuschange",   &proposecombinedconsensuschange,   false },
  { "dao",                "getconsensusparameters",   &getconsensusparameters,   false },
  { "dao",                "setexclude",               &setexclude,               false },
  { "wallet",             "stakervote",               &stakervote,               false },
  { "dao",                "support",                  &support,                  false },
  { "dao",                "supportlist",              &supportlist,              false },
  { "dao",                "consultationvote",         &consultationvote,         false },
  { "dao",                "consultationvotelist",     &consultationvotelist,     false },
  { "communityfund",      "proposalvote",             &proposalvote,             false },
  { "communityfund",      "proposalvotelist",         &proposalvotelist,         false },
  { "communityfund",      "listproposals",            &listproposals,            true  },
  { "communityfund",      "paymentrequestvote",       &paymentrequestvote,       false },
  { "communityfund",      "paymentrequestvotelist",   &paymentrequestvotelist,   false },
  { "communityfund",      "proposalvote",             &proposalvote,             false },
  { "communityfund",      "proposalvotelist",         &proposalvotelist,         false },
  { "wallet",             "generateblsctkeys",        &generateblsctkeys,        true  },
  { "wallet",             "setaccount",               &setaccount,               true  },
  { "wallet",             "settxfee",                 &settxfee,                 true  },
  { "wallet",             "signmessage",              &signmessage,              true  },
  { "wallet",             "walletlock",               &walletlock,               true  },
  { "wallet",             "walletpassphrasechange",   &walletpassphrasechange,   true  },
  { "wallet",             "walletpassphrase",         &walletpassphrase,         true  },
  { "wallet",             "removeprunedfunds",        &removeprunedfunds,        true  },
  { "wallet",             "resolveopenalias",         &resolveopenalias,         true  },
  { "dotstock",             "registername",             &registername,             true  },
  { "dotstock",             "renewname",                &renewname,                true  },
  { "dotstock",             "updatename",               &updatename,               true  },
  { "dotstock",             "resolvename",              &resolvename,              true  },
  { "dotstock",             "genkeyname",               &genkeyname,               true  },
};

void RegisterWalletRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
