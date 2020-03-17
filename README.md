# LibrettOS Network Server

Brief installation instructions. (More detailed instructions will come shortly.)

## Prerequisites
```sh
# Download the source code
$ git clone git@github.com:ssrg-vt/librettos-network.git
$ cd librettos-network
# Init the submodules
$ git submodule update --init
# Apply all necessary patches from librettos-src (e.g., ixgbe_nomsix) to src-netbsd
```

## Direct Mode
### Application with the direct mode
```sh
$ CC=cc ./build-rr.sh hw
$ cd examples
$ sudo ./compile_app.sh
```
### Launch Application as Direct Mode
```sh
$ cd examples
$ sudo xl create app.conf
```

## Network Server
### Network Server with the backend driver
```sh
$ CC=cc ./build-rr.sh -b hw
$ cd examples
$ sudo ./compile_backend.sh
```
### Application with the frontend driver
```sh
$ CC=cc ./build-rr.sh -f hw
$ cd examples
$ sudo ./compile_frontend.sh
```
### Launch Network Server / Application
```sh
$ cd examples
# Launch the Network Server
$ sudo xl create backend.conf
# Launch the application
$ sudo xl create frontend.conf
```

## Dynamic Mode Switch
### Insert Kernel Module
```sh
$ cd rumprun-utils/module
# Insert the service module
$ sudo insmod rumprun_service.ko
```
### Bind the target application
```sh
$ cd rumprun-utils
# Get the application's domid 
$ sudo xl list
# Bind the application through rumprun_service util
$ sudo ./rumprun_service bind {application domid}
```
### Triger the dynamic mode switch
```sh
$ cd rumprun-utils
$ sudo ./rumprun_service switch {application domid}
```
