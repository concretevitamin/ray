#!/bin/bash

set -x

# Cause the script to exit if a single command fails.
set -e

TP_DIR=$(cd "$(dirname "${BASH_SOURCE:-$0}")"; pwd)

if [ ! -d $TP_DIR/arrow ]; then
  git clone https://github.com/concretevitamin/arrow.git "$TP_DIR/arrow"
fi
cd $TP_DIR/arrow
git fetch origin
# git checkout  62700c0c8f4ab64fd36b060669e3953f518f7b06
git checkout d0c77948
