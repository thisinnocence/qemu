#!/bin/bash

# build riscv64 user-mode qemu
#   ../configure --target-list=riscv64-linux-user --enable-debug
#   make -j

../../build/qemu-riscv64 a.out $1
