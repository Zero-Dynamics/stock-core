#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Exercise the listreceivedbyaddress API

from test_framework.test_framework import StockTestFramework
from test_framework.util import *


def get_sub_array_from_array(object_array, to_match):
    '''
        Finds and returns a sub array from an array of arrays.
        to_match should be a unique idetifier of a sub array
    '''
    num_matched = 0
    for item in object_array:
        all_match = True
        for key,value in to_match.items():
            if item[key] != value:
                all_match = False
        if not all_match:
            continue
        return item
    return []

class ReceivedByTest(StockTestFramework):

    def __init__(self):
        super().__init__()
        self.num_nodes = 4
        self.setup_clean_chain = True

    def setup_nodes(self):
        return start_nodes(self.num_nodes, self.options.tmpdir)

    def run_test(self):
        self.nodes[0].staking(False)
        self.nodes[1].staking(False)
        self.nodes[2].staking(False)
        self.nodes[3].staking(False)
        self.is_network_split=False
        self.sync_all()
        '''
        listreceivedbyaddress Test
        '''
        # Generate for genesis block reward
        slow_gen(self.nodes[0], 51)
        self.sync_all()
        # Send from node 0 to 1
        addr = self.nodes[1].getnewaddress()
        txid = self.nodes[0].sendtoaddress(addr, 0.1)
        self.sync_all()

        #Check not listed in listreceivedbyaddress because has 0 confirmations
        assert_array_result(self.nodes[1].listreceivedbyaddress(),
                           {"address":addr},
                           { },
                           True)
        #Bury Tx under 10 block so it will be returned by listreceivedbyaddress
        slow_gen(self.nodes[1], 10)
        self.sync_all()
        assert_array_result(self.nodes[1].listreceivedbyaddress(),
                           {"address":addr},
                           {"address":addr, "account":"", "amount":Decimal("0.1"), "confirmations":10, "txids":[txid,]})
        #With min confidence < 10
        assert_array_result(self.nodes[1].listreceivedbyaddress(5),
                           {"address":addr},
                           {"address":addr, "account":"", "amount":Decimal("0.1"), "confirmations":10, "txids":[txid,]})
        #With min confidence > 10, should not find Tx
        assert_array_result(self.nodes[1].listreceivedbyaddress(11),{"address":addr},{ },True)

        #Empty Tx
        addr = self.nodes[1].getnewaddress()
        assert_array_result(self.nodes[1].listreceivedbyaddress(0,True),
                           {"address":addr},
                           {"address":addr, "account":"", "amount":0, "confirmations":0, "txids":[]})

        '''
            getreceivedbyaddress Test
        '''
        # Send from node 0 to 1
        self.sync_all()
        block_before = self.nodes[0].getblockcount()
        addr = self.nodes[1].getnewaddress()
        txid = self.nodes[0].sendtoaddress(addr, 0.1)
        self.sync_all()
        block_after = self.nodes[0].getblockcount()

        #Check balance is 0 because of 0 confirmations
        balance = self.nodes[1].getreceivedbyaddress(addr)
        if balance != Decimal("0.0"):
            raise AssertionError("Wrong balance returned by getreceivedbyaddress, %0.2f"%(balance))

        #Check balance is 0.1
        balance = satoshi_round(self.nodes[1].getreceivedbyaddress(addr,0))
        if balance != Decimal("0.1"):
            raise AssertionError("Wrong balance returned by getreceivedbyaddress, %0.2f"%(balance))

        #Bury Tx under 10 block so it will be returned by the default getreceivedbyaddress
        slow_gen(self.nodes[1], 10)
        self.sync_all()
        balance = self.nodes[1].getreceivedbyaddress(addr)
        if balance != Decimal("0.1"):
            raise AssertionError("Wrong balance returned by getreceivedbyaddress, %0.2f"%(balance))

        '''
            listreceivedbyaccount + getreceivedbyaccount Test
        '''
        #set pre-state
        addrArr = self.nodes[1].getnewaddress()
        account = self.nodes[1].getaccount(addrArr)
        received_by_account_json = get_sub_array_from_array(self.nodes[1].listreceivedbyaccount(),{"account":account})
        if len(received_by_account_json) == 0:
            raise AssertionError("No accounts found in node")
        balance_by_account = rec_by_accountArr = self.nodes[1].getreceivedbyaccount(account)

        self.sync_all()
        block_before1 = self.nodes[0].getblockcount()
        txid = self.nodes[0].sendtoaddress(addr, 0.1)
        self.sync_all()
        block_after1 = self.nodes[0].getblockcount()

        # listreceivedbyaccount should return received_by_account_json because of 0 confirmations
        assert_array_result(self.nodes[1].listreceivedbyaccount(),
                           {"account":account},
                           received_by_account_json)

        # getreceivedbyaddress should return same balance because of 0 confirmations
        balance = self.nodes[1].getreceivedbyaccount(account)
        if balance != balance_by_account:
            raise AssertionError("Wrong balance returned by getreceivedbyaccount, %0.2f"%(balance))

        slow_gen(self.nodes[1], 10)
        self.sync_all()
        # listreceivedbyaccount should return updated account balance
        assert_array_result(self.nodes[1].listreceivedbyaccount(),
                           {"account":account},
                           {"account":received_by_account_json["account"], "amount":(received_by_account_json["amount"] + Decimal("0.1"))})

        # getreceivedbyaddress should return updates balance
        balance = self.nodes[1].getreceivedbyaccount(account)
        if balance != balance_by_account + Decimal("0.1"):
            raise AssertionError("Wrong balance returned by getreceivedbyaccount, %0.2f"%(balance))

        #Create a new account named "mynewaccount" that has a 0 balance
        self.nodes[1].getaccountaddress("mynewaccount")
        received_by_account_json = get_sub_array_from_array(self.nodes[1].listreceivedbyaccount(0,True),{"account":"mynewaccount"})
        if len(received_by_account_json) == 0:
            raise AssertionError("No accounts found in node")

        # Test includeempty of listreceivedbyaccount
        if received_by_account_json["amount"] != Decimal("0.0"):
            raise AssertionError("Wrong balance returned by listreceivedbyaccount, %0.2f"%(received_by_account_json["amount"]))

        # Test getreceivedbyaccount for 0 amount accounts
        balance = self.nodes[1].getreceivedbyaccount("mynewaccount")
        if balance != Decimal("0.0"):
            raise AssertionError("Wrong balance returned by getreceivedbyaccount, %0.2f"%(balance))

if __name__ == '__main__':
    ReceivedByTest().main()
