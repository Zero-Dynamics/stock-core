#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

DOCKER_EXEC echo \> \$HOME/.stock  # Make sure default datadir does not exist and is never read by creating a dummy file

mkdir -p depends/SDKs depends/sdk-sources

if [ -n "$OSX_SDK" -a ! -f depends/sdk-sources/MacOSX${OSX_SDK}.sdk.tar.gz ]; then
  curl --location --fail $SDK_URL/MacOSX${OSX_SDK}.sdk.tar.gz -o depends/sdk-sources/MacOSX${OSX_SDK}.sdk.tar.gz
fi
if [ -n "$OSX_SDK" -a -f depends/sdk-sources/MacOSX${OSX_SDK}.sdk.tar.gz ]; then
  tar -C depends/SDKs -xf depends/sdk-sources/MacOSX${OSX_SDK}.sdk.tar.gz
fi
if [[ $HOST = *-mingw32 ]]; then
  DOCKER_EXEC update-alternatives --set $HOST-g++ \$\(which $HOST-g++-posix\)
fi
if [ -z "$NO_DEPENDS" ]; then
  DOCKER_EXEC CONFIG_SHELL= ./contrib/run_with_dots make $MAKEJOBS -C depends HOST=$HOST $DEP_OPTS || ( echo "Depends Build failure. Verbose build follows." && CONFIG_SHELL= make $MAKEJOBS -C depends HOST=$HOST $DEP_OPTS V=1 ; false )

  if [[ $HOST = *-mingw32 ]]; then
    # Copy the missing headers that windows build needs
    DOCKER_EXEC cp /usr/$HOST/include/malloc.h depends/$HOST/include/alloca.h
    DOCKER_EXEC cp /usr/$HOST/include/wincrypt.h depends/$HOST/include/Wincrypt.h
  fi
fi

