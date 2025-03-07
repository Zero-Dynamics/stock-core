#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Run Regression Test Suite

This module calls down into individual test cases via subprocess. It will
forward all unrecognized arguments onto the individual test scripts, other
than:

    - `-extended`: run the "extended" test suite in addition to the basic one.
    - `-win`: signal that this is running in a Windows environment, and we
      should run the tests.
    - `--coverage`: this generates a basic coverage report for the RPC
      interface.

For a description of arguments recognized by test scripts, see
`test/pull-tester/test_framework/test_framework.py:StockTestFramework.main`.

"""

import os
import time
import shutil
import sys
import subprocess
import tempfile
import re

sys.path.append("test/pull-tester/")
from tests_config import *

BOLD = ("","")
if os.name == 'posix':
    # primitive formatting on supported
    # terminal via ANSI escape sequences:
    BOLD = ('\033[0m', '\033[1m')

RPC_TESTS_DIR = SRCDIR + '/test/rpc-tests/'

#If imported values are not defined then set to zero (or disabled)
if 'ENABLE_WALLET' not in vars():
    ENABLE_WALLET=0
if 'ENABLE_STOCKD' not in vars():
    ENABLE_STOCKD=0
if 'ENABLE_UTILS' not in vars():
    ENABLE_UTILS=0
if 'ENABLE_ZMQ' not in vars():
    ENABLE_ZMQ=0

ENABLE_COVERAGE = False
FAILFAST = False

#Create a set to store arguments and create the passon string
opts = set()
passon_args = []
PASSON_REGEX = re.compile("^--")
PARALLEL_REGEX = re.compile('^-parallel=')

print_help = False
run_parallel = 4

for arg in sys.argv[1:]:
    if arg == "--help" or arg == "-h" or arg == "-?":
        print_help = True
        break
    if arg == '--coverage':
        ENABLE_COVERAGE = True
    elif arg == '--failfast':
        FAILFAST = True
    elif PASSON_REGEX.match(arg):
        passon_args.append(arg)
    elif PARALLEL_REGEX.match(arg):
        run_parallel = int(arg.split(sep='=', maxsplit=1)[1])
    else:
        opts.add(arg)

#Set env vars
if "STOCKD" not in os.environ:
    os.environ["STOCKD"] = BUILDDIR + '/src/stockd' + EXEEXT
if "STOCKCLI" not in os.environ:
    os.environ["STOCKCLI"] = BUILDDIR + '/src/stock-cli' + EXEEXT

if EXEEXT == ".exe" and "-win" not in opts:
    # https://github.com/stock/stock/commit/d52802551752140cf41f0d9a225a43e84404d3e9
    # https://github.com/stock/stock/pull/5677#issuecomment-136646964
    print("Win tests currently disabled by default.  Use -win option to enable")
    sys.exit(0)

if not (ENABLE_WALLET == 1 and ENABLE_UTILS == 1 and ENABLE_STOCKD == 1):
    print("No rpc tests to run. Wallet, utils, and stockd must all be enabled")
    sys.exit(0)

# python3-zmq may not be installed. Handle this gracefully and with some helpful info
if ENABLE_ZMQ:
    try:
        import zmq
    except ImportError as e:
        print("WARNING: \"import zmq\" failed. Set ENABLE_ZMQ=0 or " \
            "to run zmq tests, see dependency info in /test/README.md.")
        ENABLE_ZMQ=0

#Tests
testScripts = [
    # longest test should go first, to favor running tests in parallel
# 'p2p-fullblocktest.py',
#    'walletbackup.py',
#    'bip68-112-113-p2p.py',

    'staking_mininputvalue.py',
     'wallet.py',
    'wallet-hd.py',

#    'listtransactions.py',
     'receivedby.py',
#    'mempool_resurrect_test.py',
#    'txn_doublespend.py --mineblock',
#    'txn_clone.py',
     'getchaintips.py',
    'rawtransactions.py',
     'rest.py',
     'mempool_spendcoinbase.py',
#    'mempool_reorg.py',
#    'mempool_limit.py',
     'httpbasics.py',
     'multi_rpc.py',
     'zapwallettxes.py',
#    'proxy_test.py',
    'merkle_blocks.py',
#    'fundrawtransaction.py',
#    'signrawtransactions.py',
     'nodehandling.py',
     'reindex.py',
#    'addressindex.py',
    'timestampindex.py',
#    'spentindex.py',
#    'txindex.py',
#    'decodescript.py',
#    'blockchain.py',
     'disablewallet.py',
#    'sendheaders.py',
#    'keypool.py',
#    'prioritise_transaction.py',
#    'invalidblockrequest.py',
#    'invalidtxrequest.py',
#    'abandonconflict.py',
#    'p2p-versionbits-warning.py',
#    'p2p-segwit.py',
#    'segwit.py',
     'importprunedfunds.py',
     'signmessages.py',
    'cfund-donate.py',
    'cfund-fork-reorg-preq.py',
    'cfund-fork-reorg-proposal.py',
    'cfund-listproposals.py',
    'cfund-paymentrequest-extract-funds.py',
    'cfund-paymentrequest-payout.py',
    'cfund-paymentrequest-duplicate.py',
    'cfund-paymentrequest-hardfork-452.py',
    'cfund-paymentrequest-state-accept.py',
    'cfund-paymentrequest-state-accept-expired-proposal.py',
    'cfund-paymentrequest-state-expired.py',
    'cfund-proposal-state-accept.py',
    'cfund-proposal-state-expired.py',
    'cfund-reorg.py',
    'cfund-rawtx-create-proposal.py',
#    'cfund-rawtx-paymentrequest-create.py',
    'cfund-rawtx-paymentrequest-vote.py',
    'cfund-rawtx-proposal-vote.py',
    'cfund-vote.py',
    'cfund-proposalvotelist.py',
    'cfund-paymentrequestvotelist.py',
    'cfund-paymentrequest-state-reorg.py',
    'reject-version-bit.py',
    'getcoldstakingaddress.py',
    'getstakereport.py',
    'importaddress.py',
#    'getstakinginfo.py',
    'coldstaking_staking.py',
    'coldstaking_spending.py',
    'coldstaking_fee.py',
    'staticr-staking-amount.py',
    'hardfork-451.py',
    'hardfork-452.py',
    'staticr-tx-send.py',
    'stakingaddress.py',
    'mnemonic.py',
    'sendtoaddress.py',
    'stakeimmaturebalance.py',
    'rpc-help.py',
    'dao-consultations.py',
    'dao-consultation-consensus.py',
    'dao-consultation-consensus-cycle-length.py',
    'createrawscriptaddress.py',
    'cfunddb-statehash.py',
    'dao/001-proposal-expired.py',
    'dao/002-proposal-rejected.py',
    'dao/003-proposal-accepted.py',
    'dao/004-proposal-expired-preq.py',
    'dao/005-proposal-rejected-preq.py',
    'dao/006-proposal-accepted-preq.py',
]
#if ENABLE_ZMQ:
#    testScripts.append('zmq_test.py')

testScriptsExt = [
    'bip9-softforks.py',
    'bip65-cltv.py',
    'bip65-cltv-p2p.py',
    'bip68-sequence.py',
    'bipdersig-p2p.py',
    'bipdersig.py',
    'getblocktemplate_longpoll.py',
    'getblocktemplate_proposals.py',
    'txn_doublespend.py',
    'txn_clone.py --mineblock',
    'forknotify.py',
    'invalidateblock.py',
#    'rpcbind_test.py', #temporary, bug in libevent, see #6655
    'smartfees.py',
    'maxblocksinflight.py',
    'p2p-acceptblock.py',
    'mempool_packages.py',
    'maxuploadtarget.py',
    'replace-by-fee.py',
    'p2p-feefilter.py',
    'pruning.py', # leave pruning last as it takes a REALLY long time
]


def runtests():
    test_list = []
    if '-extended' in opts:
        test_list = testScripts + testScriptsExt
    elif len(opts) == 0 or (len(opts) == 1 and "-win" in opts):
        test_list = testScripts
    else:
        for t in testScripts + testScriptsExt:
            if t in opts or re.sub(".py$", "", t) in opts:
                test_list.append(t)

    if print_help:
        # Only print help of the first script and exit
        subprocess.check_call((RPC_TESTS_DIR + test_list[0]).split() + ['-h'])
        sys.exit(0)

    coverage = None

    if ENABLE_COVERAGE:
        coverage = RPCCoverage()
        print("Initializing coverage directory at %s\n" % coverage.dir)
    flags = ["--srcdir=%s/src" % BUILDDIR] + passon_args
    if coverage:
        flags.append(coverage.flag)

    if len(test_list) > 1 and run_parallel > 1:
        # Populate cache
        subprocess.check_output([RPC_TESTS_DIR + 'create_cache.py'] + flags)

    #Run Tests
    max_len_name = len(max(test_list, key=len))
    time_sum = 0
    time0 = time.time()
    job_queue = RPCTestHandler(run_parallel, test_list, flags)
    results = BOLD[1] + "%s | %s | %s\n\n" % ("TEST".ljust(max_len_name), "PASSED", "DURATION") + BOLD[0]
    all_passed = True
    for _ in range(len(test_list)):
        (name, stdout, stderr, passed, duration) = job_queue.get_next()
        all_passed = all_passed and passed
        time_sum += duration

        print('\n' + BOLD[1] + name + BOLD[0] + ":")
        print(stdout)
        print('stderr:\n' if not stderr == '' else '', stderr)
        results += "%s | %s | %s s\n" % (name.ljust(max_len_name), str(passed).ljust(6), duration)
        print("Pass: %s%s%s, Duration: %s s\n" % (BOLD[1], passed, BOLD[0], duration))

        # Check if we need to quit
        if FAILFAST and not passed:
            print("Early exiting after test failure")
            break

    results += BOLD[1] + "\n%s | %s | %s s (accumulated)" % ("ALL".ljust(max_len_name), str(all_passed).ljust(6), time_sum) + BOLD[0]
    print(results)
    print("\nRuntime: %s s" % (int(time.time() - time0)))

    if coverage:
        coverage.report_rpc_coverage()

        print("Cleaning up coverage data")
        coverage.cleanup()

    sys.exit(not all_passed)


class RPCTestHandler:
    """
    Trigger the testscrips passed in via the list.
    """

    def __init__(self, num_tests_parallel, test_list=None, flags=None):
        assert(num_tests_parallel >= 1)
        self.num_jobs = num_tests_parallel
        self.test_list = test_list
        self.flags = flags
        self.num_running = 0
        self.jobs = []

    def get_next(self):
        while self.num_running < self.num_jobs and self.test_list:
            # Add tests
            self.num_running += 1
            t = self.test_list.pop(0)
            port_seed = ["--portseed=%s" % len(self.test_list)]
            self.jobs.append((t,
                              time.time(),
                              subprocess.Popen((RPC_TESTS_DIR + t).split() + self.flags + port_seed,
                                               universal_newlines=True,
                                               stdout=subprocess.PIPE,
                                               stderr=subprocess.PIPE)))
        if not self.jobs:
            raise IndexError('pop from empty list')
        while True:
            # Return first proc that finishes
            time.sleep(.5)
            for j in self.jobs:
                (name, time0, proc) = j
                if proc.poll() is not None:
                    try:
                        (stdout, stderr) = proc.communicate(timeout=10)
                    except TimeoutExpired:
                        proc.kill()
                        (stdout, stderr) = proc.communicate()
                    passed = stderr == "" and proc.returncode == 0
                    self.num_running -= 1
                    self.jobs.remove(j)
                    return name, stdout, stderr, passed, int(time.time() - time0)
            print('.', end='', flush=True)


class RPCCoverage(object):
    """
    Coverage reporting utilities for pull-tester.

    Coverage calculation works by having each test script subprocess write
    coverage files into a particular directory. These files contain the RPC
    commands invoked during testing, as well as a complete listing of RPC
    commands per `stock-cli help` (`rpc_interface.txt`).

    After all tests complete, the commands run are combined and diff'd against
    the complete list to calculate uncovered RPC commands.

    See also: test/rpc-tests/test_framework/coverage.py

    """
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="coverage")
        self.flag = '--coveragedir=%s' % self.dir

    def report_rpc_coverage(self):
        """
        Print out RPC commands that were unexercised by tests.

        """
        uncovered = self._get_uncovered_rpc_commands()

        if uncovered:
            print("Uncovered RPC commands:")
            print("".join(("  - %s\n" % i) for i in sorted(uncovered)))
        else:
            print("All RPC commands covered.")

    def cleanup(self):
        return shutil.rmtree(self.dir)

    def _get_uncovered_rpc_commands(self):
        """
        Return a set of currently untested RPC commands.

        """
        # This is shared from `test/rpc-tests/test-framework/coverage.py`
        REFERENCE_FILENAME = 'rpc_interface.txt'
        COVERAGE_FILE_PREFIX = 'coverage.'

        coverage_ref_filename = os.path.join(self.dir, REFERENCE_FILENAME)
        coverage_filenames = set()
        all_cmds = set()
        covered_cmds = set()

        if not os.path.isfile(coverage_ref_filename):
            raise RuntimeError("No coverage reference found")

        with open(coverage_ref_filename, 'r') as f:
            all_cmds.update([i.strip() for i in f.readlines()])

        for root, dirs, files in os.walk(self.dir):
            for filename in files:
                if filename.startswith(COVERAGE_FILE_PREFIX):
                    coverage_filenames.add(os.path.join(root, filename))

        for filename in coverage_filenames:
            with open(filename, 'r') as f:
                covered_cmds.update([i.strip() for i in f.readlines()])

        return all_cmds - covered_cmds


if __name__ == '__main__':
    runtests()
