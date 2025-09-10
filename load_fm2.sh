#!/bin/bash
pushd ../qemu-master-mt/
git checkout $1
sudo ./setup.sh
popd
