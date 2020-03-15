#!/bin/sh

export PATH="${PATH}:$(pwd)/../rumprun/bin"

rm rumprun-frontend.bin.iso
rumprun iso -n inet,static,10.0.0.2/24 frontend.bin -F ixg0
