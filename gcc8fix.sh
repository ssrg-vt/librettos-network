#!/bin/sh
gcc "$@" "-Wno-cast-function-type" "-Wno-tautological-compare" "-Wno-packed-not-aligned" "-Wno-uninitialized"
