#!/bin/sh
gcc "$@" "-Wno-cast-function-type" "-Wno-tautological-compare" "-Wno-packed-not-aligned" "-Wno-address-of-packed-member" "-Wno-array-bounds" "-Wno-attributes" "-Wno-uninitialized"
