// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STOCK_TOKENUTXOS_H
#define STOCK_TOKENUTXOS_H

#include <uint256.h>

struct TokenUtxoKey {
    uint256 id;
    uint64_t blockHeight;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 32 + 8;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        id.Serialize(s, nType, nVersion);
        ser_writedata32be(s, blockHeight);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        id.Unserialize(s, nType, nVersion);
        blockHeight = ser_readdata32be(s);
    }

    TokenUtxoKey(uint256 t, int h) {
        id = t;
        blockHeight = h;
    }

    TokenUtxoKey() {
        SetNull();
    }

    void SetNull() {
        id.SetNull();
        blockHeight = 0;
    }

    bool IsNull() const {
        return id.IsNull();
    }

    bool operator==(const TokenUtxoKey& other) const {
        return (id == other.id && blockHeight == other.blockHeight);
    }

    bool operator<(const TokenUtxoKey& b) const {
        if (id == b.id) {
            return blockHeight < b.blockHeight;
        } else {
            return id < b.id;
        }
    }

    void swap(TokenUtxoKey &to) {
        std::swap(to.id, id);
        std::swap(to.blockHeight, blockHeight);
    }
};

struct TokenUtxoValue {
    uint256 hash;
    std::vector<uint8_t> spendingKey;
    uint32_t n;

    TokenUtxoValue(uint256 _hash, std::vector<uint8_t> _spendingKey, uint32_t _n) {
        hash = _hash;
        spendingKey = _spendingKey;
        n = _n;
    }

    TokenUtxoValue() {
        SetNull();
    }

    void SetNull() {
        hash.SetNull();
        spendingKey.clear();
        n = (uint32_t) -1;
    }

    bool IsNull() const {
        return hash.IsNull();
    }

    bool operator==(const TokenUtxoValue& other) const {
        return (hash == other.hash && spendingKey == other.spendingKey && n == other.n);
    }

    void swap(TokenUtxoValue &to) {
        std::swap(to.hash, hash);
        std::swap(to.spendingKey, spendingKey);
        std::swap(to.n, n);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(hash);
        READWRITE(spendingKey);
        READWRITE(n);
    }
};

typedef std::pair<uint64_t, TokenUtxoValue> TokenUtxoEntry;
typedef std::vector<TokenUtxoEntry> TokenUtxoValues;
typedef std::map<uint256, TokenUtxoValues> TokenUtxoMap;

#endif // STOCK_TOKENUTXOS_H
