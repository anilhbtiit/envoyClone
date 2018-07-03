#!/bin/bash

set -e

VERSION=1.2.11

wget -O zlib-"$VERSION".tar.gz https://github.com/madler/zlib/archive/v"$VERSION".tar.gz
tar xf zlib-"$VERSION".tar.gz
cd zlib-"$VERSION"

if [[ "${OS}" == "Windows_NT" ]]; then
  mkdir build
  cd build
  cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" ..
  ninja
  ninja install
  cp "CMakeFiles/zlibstatic.dir/zlibstatic.pdb" "$THIRDPARTY_BUILD/lib/zlibstatic.pdb"
else
  ./configure --prefix="$THIRDPARTY_BUILD"
  make V=1 install
fi
