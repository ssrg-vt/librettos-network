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
## Build
### Network Server with the backend driver
```sh
$ CC=cc ./build-rr.sh -b hw
$ cd examples
$ sudo ./compile_backend.sh
```
### Application with the frontend driver
```sh
$ CC=cc ./build-rr.sh hw
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
