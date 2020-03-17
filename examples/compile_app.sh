#!/bin/sh

export PATH="${PATH}:$(pwd)/../rumprun/bin"

./do_compile_ifconfig.sh

cp ../rumprun/rumprun-x86_64/lib/libutil.a ../rumprun/rumprun-x86_64/lib/rumprun-hw/
cp ../rumprun/rumprun-x86_64/lib/libprop.a ../rumprun/rumprun-x86_64/lib/rumprun-hw/

x86_64-rumprun-netbsd-cookfs -s 1 rootfs.fs rootfs

#rumprun-bake -m "add rootfs.fs" hw_generic app.bin ifconfig memcached
rumprun-bake -m "add rootfs.fs" hw_generic app.bin ifconfig nginx
#rumprun-bake -m "add rootfs.fs" hw_generic app.bin netpipe
#rumprun-bake -m "add rootfs.fs" hw_generic app.bin redis-server
#rumprun-bake -m "add rootfs.fs" hw_generic app.bin nfsd

sudo ./create_app_iso.sh
