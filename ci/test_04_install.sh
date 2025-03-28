#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

travis_retry docker pull "$DOCKER_NAME_TAG"

mkdir -p "${TRAVIS_BUILD_DIR}/sanitizer-output/"
export ASAN_OPTIONS=""
export LSAN_OPTIONS="suppressions=${TRAVIS_BUILD_DIR}/test/sanitizer_suppressions/lsan"
export TSAN_OPTIONS="suppressions=${TRAVIS_BUILD_DIR}/test/sanitizer_suppressions/tsan:log_path=${TRAVIS_BUILD_DIR}/sanitizer-output/tsan"
export UBSAN_OPTIONS="suppressions=${TRAVIS_BUILD_DIR}/test/sanitizer_suppressions/ubsan:print_stacktrace=1:halt_on_error=1"
env | grep -E '^(STOCK_CONFIG|CCACHE_|WINEDEBUG|LC_ALL|BOOST_TEST_RANDOM|CONFIG_SHELL|(ASAN|LSAN|TSAN|UBSAN)_OPTIONS)' | tee /tmp/env
if [[ $HOST = *-mingw32 ]]; then
  DOCKER_ADMIN="--cap-add SYS_ADMIN"
elif [[ $STOCK_CONFIG = *--with-sanitizers=*address* ]]; then # If ran with (ASan + LSan), Docker needs access to ptrace (https://github.com/google/sanitizers/issues/764)
  DOCKER_ADMIN="--cap-add SYS_PTRACE"
fi
DOCKER_ID=$(docker run $DOCKER_ADMIN -idt --mount type=bind,src=$TRAVIS_BUILD_DIR,dst=$TRAVIS_BUILD_DIR --mount type=bind,src=$CCACHE_DIR,dst=$CCACHE_DIR -w $TRAVIS_BUILD_DIR --env-file /tmp/env $DOCKER_NAME_TAG)

DOCKER_EXEC () {
  docker exec $DOCKER_ID bash -c "cd $PWD && $*"
}

if [ -n "$DPKG_ADD_ARCH" ]; then
  DOCKER_EXEC dpkg --add-architecture "$DPKG_ADD_ARCH"
fi

INSTALL_COMMAND="DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends --no-upgrade -qq"


# Install some things we need to get kitware repo
travis_retry DOCKER_EXEC apt-get update
travis_retry DOCKER_EXEC $INSTALL_COMMAND apt-transport-https ca-certificates gnupg software-properties-common curl

# Add the kitware repo for cmake
travis_retry DOCKER_EXEC "curl -fsSL https://apt.kitware.com/keys/kitware-archive-latest.asc | apt-key add - "
travis_retry DOCKER_EXEC apt-add-repository \'deb https://apt.kitware.com/ubuntu/ bionic main\'

# Update repo list and install additional apps needed
travis_retry DOCKER_EXEC apt-get update
travis_retry DOCKER_EXEC $INSTALL_COMMAND $PACKAGES $DOCKER_PACKAGES

if [ "$RUN_FUNCTIONAL_TESTS" = "true" ]; then
  BEGIN_FOLD local-ntp-server
  travis_retry DOCKER_EXEC $INSTALL_COMMAND ntp
  travis_retry DOCKER_EXEC service ntp start
  END_FOLD
fi

