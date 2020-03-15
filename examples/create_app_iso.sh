#!/bin/sh

export PATH="${PATH}:$(pwd)/../rumprun/bin"

rm rumprun-app.bin.iso
rumprun iso app.bin ixg0 10.0.0.22 netmask 255.255.255.0 mtu 9000
