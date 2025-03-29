// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STOCK_ADDRESSINDEX_H
#define STOCK_ADDRESSINDEX_H

#include <uint256.h>
#include <amount.h>
#include <script/script.h>

struct CAddressUnspentKey {
    unsigned int type;
    uint160 hashBytes;
    uint256 txhash;
    size_t index;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 57;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
        txhash.Serialize(s, nType, nVersion);
        ser_writedata32(s, index);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
        txhash.Unserialize(s, nType, nVersion);
        index = ser_readdata32(s);
    }

    CAddressUnspentKey(unsigned int addressType, uint160 addressHash, uint256 txid, size_t indexValue) {
        type = addressType;
        hashBytes = addressHash;
        txhash = txid;
        index = indexValue;
    }

    CAddressUnspentKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        txhash.SetNull();
        index = 0;
    }
};

struct CAddressUnspentValue {
    CAmount satoshis;
    CScript script;
    int blockHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(satoshis);
        READWRITE(*(CScriptBase*)(&script));
        READWRITE(blockHeight);
    }

    CAddressUnspentValue(CAmount sats, CScript scriptPubKey, int height) {
        satoshis = sats;
        script = scriptPubKey;
        blockHeight = height;
    }

    CAddressUnspentValue() {
        SetNull();
    }

    void SetNull() {
        satoshis = -1;
        script.clear();
        blockHeight = 0;
    }

    bool IsNull() const {
        return (satoshis == -1);
    }
};

enum AddressHistoryFlag {
    GENERATED_FLAG = 1,
};

enum AddressHistoryFilter {
    SPENDABLE = 1,
    STAKABLE = 2,
    VOTING_WEIGHT = 4,
    STAKABLE_VOTING_WEIGHT = STAKABLE|VOTING_WEIGHT,
    ALL = SPENDABLE|STAKABLE|VOTING_WEIGHT,
    GENERATED_FILTER = 8,
};

struct CAddressIndexKey {
    unsigned int type;
    uint160 hashBytes;
    int blockHeight;
    unsigned int txindex;
    uint256 txhash;
    size_t index;
    bool spending;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 66;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
        // Heights are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, blockHeight);
        ser_writedata32be(s, txindex);
        txhash.Serialize(s, nType, nVersion);
        ser_writedata32(s, index);
        char f = spending;
        ser_writedata8(s, f);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
        blockHeight = ser_readdata32be(s);
        txindex = ser_readdata32be(s);
        txhash.Unserialize(s, nType, nVersion);
        index = ser_readdata32(s);
        char f = ser_readdata8(s);
        spending = f;
    }

    CAddressIndexKey(unsigned int addressType, uint160 addressHash, int height, int blockindex,
                     uint256 txid, size_t indexValue, bool isSpending) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
        txindex = blockindex;
        txhash = txid;
        index = indexValue;
        spending = isSpending;
    }

    CAddressIndexKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
        txindex = 0;
        txhash.SetNull();
        index = 0;
        spending = false;
    }

};

struct CAddressHistoryKey {
    uint160 hashBytes;
    uint160 hashBytes2;
    int blockHeight;
    unsigned int txindex;
    uint256 txhash;
    uint32_t time;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 84;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        hashBytes.Serialize(s, nType, nVersion);
        hashBytes2.Serialize(s, nType, nVersion);
        // Heights are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, blockHeight);
        ser_writedata32be(s, txindex);
        txhash.Serialize(s, nType, nVersion);
        ser_writedata32be(s, time);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        hashBytes.Unserialize(s, nType, nVersion);
        hashBytes2.Unserialize(s, nType, nVersion);
        blockHeight = ser_readdata32be(s);
        txindex = ser_readdata32be(s);
        txhash.Unserialize(s, nType, nVersion);
        time = ser_readdata32be(s);
    }

    bool operator<(const CAddressHistoryKey& b) const {
        if (hashBytes == b.hashBytes) {
            if (hashBytes2 == b.hashBytes2) {
                if (blockHeight == b.blockHeight) {
                    if (txhash == b.txhash) {
                        return time < b.time;
                    } else {
                        return txhash < b.txhash;
                    }
                } else {
                    return blockHeight < b.blockHeight;
                }
            } else {
                return hashBytes2 < b.hashBytes2;
            }
        } else {
            return hashBytes < b.hashBytes;
        }
    }

    CAddressHistoryKey(uint160 addressHash, uint160 addressHash2, int height, int blockindex,
                     uint256 txid, uint32_t time_) {
        hashBytes = addressHash;
        hashBytes2 = addressHash2;
        blockHeight = height;
        txindex = blockindex;
        txhash = txid;
        time = time_;
    }

    CAddressHistoryKey() {
        SetNull();
    }

    void SetNull() {
        hashBytes.SetNull();
        hashBytes2.SetNull();
        blockHeight = 0;
        txindex = 0;
        txhash.SetNull();
        time = 0;
    }
};

struct CAddressHistoryValue {
    CAmount spendable;
    CAmount stakable;
    CAmount voting_weight;
    char flags;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 25;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata64(s, spendable);
        ser_writedata64(s, stakable);
        ser_writedata64(s, voting_weight);
        ser_writedata8(s, flags);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        spendable = ser_readdata64(s);
        stakable = ser_readdata64(s);
        voting_weight = ser_readdata64(s);
        flags = ser_readdata8(s);
    }

    bool operator<(const CAddressHistoryValue& b) const {
        return spendable < b.spendable;
    }

    CAddressHistoryValue(const CAmount &spendable_, const CAmount &stakable_, const CAmount &voting_weight_,
                         const char& flags_) {
        spendable = spendable_;
        stakable = stakable_;
        voting_weight = voting_weight_;
        flags = flags_;
    }

    CAddressHistoryValue() {
        SetNull();
    }

    void SetNull() {
        spendable = 0;
        stakable = 0;
        voting_weight = 0;
        flags = 0;
    }
};

struct CAddressIndexIteratorKey {
    unsigned int type;
    uint160 hashBytes;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 21;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
    }

    CAddressIndexIteratorKey(unsigned int addressType, uint160 addressHash) {
        type = addressType;
        hashBytes = addressHash;
    }

    CAddressIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
    }
};

struct CAddressIndexIteratorHeightKey {
    unsigned int type;
    uint160 hashBytes;
    int blockHeight;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 25;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s, nType, nVersion);
        ser_writedata32be(s, blockHeight);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s, nType, nVersion);
        blockHeight = ser_readdata32be(s);
    }

    CAddressIndexIteratorHeightKey(unsigned int addressType, uint160 addressHash, int height) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
    }

    CAddressIndexIteratorHeightKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
    }
};

struct CAddressHistoryIteratorKey {
    uint160 hashBytes;
    uint160 hashBytes2;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 40;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        hashBytes.Serialize(s, nType, nVersion);
        hashBytes2.Serialize(s, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        hashBytes.Unserialize(s, nType, nVersion);
        hashBytes2.Unserialize(s, nType, nVersion);
    }

    CAddressHistoryIteratorKey(uint160 addressHash, uint160 addressHash2) {
        hashBytes = addressHash;
        hashBytes2 = addressHash2;
    }

    CAddressHistoryIteratorKey() {
        SetNull();
    }

    void SetNull() {
        hashBytes.SetNull();
        hashBytes2.SetNull();
    }
};

struct CAddressHistoryIteratorHeightKey {
    uint160 hashBytes;
    uint160 hashBytes2;
    int blockHeight;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 44;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        hashBytes.Serialize(s, nType, nVersion);
        hashBytes2.Serialize(s, nType, nVersion);
        ser_writedata32be(s, blockHeight);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        hashBytes.Unserialize(s, nType, nVersion);
        hashBytes2.Unserialize(s, nType, nVersion);
        blockHeight = ser_readdata32be(s);
    }

    CAddressHistoryIteratorHeightKey(uint160 addressHash, uint160 addressHash2, int height) {
        hashBytes = addressHash;
        hashBytes2 = addressHash2;
        blockHeight = height;
    }

    CAddressHistoryIteratorHeightKey() {
        SetNull();
    }

    void SetNull() {
        hashBytes.SetNull();
        hashBytes2.SetNull();
        blockHeight = 0;
    }
};

struct CMempoolAddressDelta
{
    int64_t time;
    CAmount amount;
    uint256 prevhash;
    unsigned int prevout;

    CMempoolAddressDelta(int64_t t, CAmount a, uint256 hash, unsigned int out) {
        time = t;
        amount = a;
        prevhash = hash;
        prevout = out;
    }

    CMempoolAddressDelta(int64_t t, CAmount a) {
        time = t;
        amount = a;
        prevhash.SetNull();
        prevout = 0;
    }
};

struct CMempoolAddressDeltaKey
{
    int type;
    uint160 addressBytes;
    uint256 txhash;
    unsigned int index;
    int spending;

    CMempoolAddressDeltaKey(int addressType, uint160 addressHash, uint256 hash, unsigned int i, int s) {
        type = addressType;
        addressBytes = addressHash;
        txhash = hash;
        index = i;
        spending = s;
    }

    CMempoolAddressDeltaKey(int addressType, uint160 addressHash) {
        type = addressType;
        addressBytes = addressHash;
        txhash.SetNull();
        index = 0;
        spending = 0;
    }
};

struct CMempoolAddressDeltaKeyCompare
{
    bool operator()(const CMempoolAddressDeltaKey& a, const CMempoolAddressDeltaKey& b) const {
        if (a.type == b.type) {
            if (a.addressBytes == b.addressBytes) {
                if (a.txhash == b.txhash) {
                    if (a.index == b.index) {
                        return a.spending < b.spending;
                    } else {
                        return a.index < b.index;
                    }
                } else {
                    return a.txhash < b.txhash;
                }
            } else {
                return a.addressBytes < b.addressBytes;
            }
        } else {
            return a.type < b.type;
        }
    }
};

#endif // STOCK_ADDRESSINDEX_H
