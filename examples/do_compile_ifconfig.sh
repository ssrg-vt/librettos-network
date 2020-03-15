#!/bin/sh

(cd .. && x86_64-rumprun-netbsd-gcc -Wall -O2 -o examples/ifconfig ifconfig/af_atalk.c ifconfig/af_inet6.c ifconfig/ieee80211.c ifconfig/ifconfig.c ifconfig/pfsync.c ifconfig/env.c ifconfig/parse.c ifconfig/carp.c ifconfig/agr.c ifconfig/ether.c ifconfig/af_inetany.c ifconfig/media.c ifconfig/tunnel.c ifconfig/ifconfig_hostops.c ifconfig/af_inet.c ifconfig/vlan.c ifconfig/af_link.c ifconfig/util.c -DPORTMAP -DRUMPRUN)
