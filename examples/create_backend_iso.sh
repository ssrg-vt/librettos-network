#!/bin/sh

export PATH="${PATH}:$(pwd)/../rumprun/bin"

rm rumprun-backend.bin.iso
rumprun iso backend.bin -B ixg0 10.0.0.2 netmask 255.255.255.0 mtu 9000
