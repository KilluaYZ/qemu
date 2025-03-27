#!/bin/bash

# build qemu into single bc file
ROOT=$(dirname $(realpath $0))

if [ ! -d "$ROOT/.venv" ]; then
    echo "python 虚拟环境不存在，创建环境..."
    python -m venv .venv
    source $ROOT/.venv/bin/activate
    pip install wllvm sphinx sphinx_rtd_theme 
fi

source $ROOT/.venv/bin/activate

# Build the device simulator（确保生成aplib.so）
# make -C device-simulator || exit 1  # 添加错误检查


# 如果build目录存在则删除
if [ -d "$ROOT/build" ]; then
    rm -rf build
fi

mkdir -p $ROOT/build
pushd $ROOT/build

  # 添加动态链接库路径和库名称
  ../configure --target-list=x86_64-softmmu --enable-virtfs --disable-werror --enable-debug --extra-cflags="-I$ROOT/device-simulator/" --extra-cxxflags="-I$ROOT/device-simulator/" --extra-ldflags="$ROOT/device-simulator/aplib.so -Wl,--allow-shlib-undefined"

  make -j20
  
popd
