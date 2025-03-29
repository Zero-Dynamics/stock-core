The [pull-tester](/test/pull-tester/) folder contains a script to call
multiple tests from the [rpc-tests](/test/rpc-tests/) folder.

Every pull request to the stock repository is built and run through
the regression test suite. You can also run all or only individual
tests locally.

Test dependencies
=================
Before running the tests, the following must be installed.

Unix
----
The python3-zmq library is required. On Ubuntu or Debian it can be installed via:
```
sudo apt-get install python3-zmq
```

OS X
------
```
pip3 install pyzmq
```

Running tests
=============

You can run any single test by calling

    test/pull-tester/rpc-tests.py <testname>

Or you can run any combination of tests by calling

    test/pull-tester/rpc-tests.py <testname1> <testname2> <testname3> ...

Run the regression test suite with

    test/pull-tester/rpc-tests.py

Run all possible tests with

    test/pull-tester/rpc-tests.py -extended

By default, tests will be run in parallel if you want to specify how many
tests should be run in parallel, append `-parallel=n` (default n=4).

If you want to create a basic coverage report for the rpc test suite, append `--coverage`.

Possible options, which apply to each individual test run:

```
  -h, --help            show this help message and exit
  --nocleanup           Leave stockds and test.* datadir on exit or error
  --noshutdown          Don't stop stockds after the test execution
  --srcdir=SRCDIR       Source directory containing stockd/stock-cli
                        (default: ../../src)
  --tmpdir=TMPDIR       Root directory for datadirs
  --tracerpc            Print out all RPC calls as they are made
  --coveragedir=COVERAGEDIR
                        Write tested RPC commands into this directory
```

If you set the environment variable `PYTHON_DEBUG=1` you will get some debug
output (example: `PYTHON_DEBUG=1 test/pull-tester/rpc-tests.py wallet`).

A 200-block -regtest blockchain and wallets for four nodes
is created the first time a regression test is run and
is stored in the cache/ directory. Each node has 25 mature
blocks (25*50=1250 0DYNS) in its wallet.

After the first run, the cache/ blockchain and wallets are
copied into a temporary directory and used as the initial
test state.

If you get into a bad state, you should be able
to recover with:

```bash
rm -rf cache
killall stockd
```

Writing tests
=============
You are encouraged to write tests for new or existing features.
Further information about the test framework and individual rpc
tests is found in [test/rpc-tests](/test/rpc-tests).
