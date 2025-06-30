# Bitcoinzero [BZX] (Lelantus) Core 2023

[![Build Status](https://travis-ci.org/BitcoinZeroOfficial/bitcoinzero.svg?branch=master)](https://travis-ci.org/BitcoinZeroOfficial/bitcoinzero)

## Bitcoinzero

- Coin Suffix: BZX
- Algorithm: Lyra2Z
- Target Spacing: 150 Seconds
- Retarget: every block
- Confirmation: 6 Blocks
- Maturity: 120 Blocks
- Max Coins: N/A
- Min TX Fee: 0.001 BZX
- Block Size: 4MB

## Net Parameters

- P2P Port=29301
- RPC Port=29201
- Client core= based on Firo 14.1X.X
- Client name=bitcoinzero.qt
- Conf file=bitcoinzero.conf

## Installation folder

- Windows: C:\Users\Username\AppData\Roaming\bitcoinzero
- Mac: /Library/Application Support/bitcoinzero
- Unix: /.bitcoinzero

# Debian/Ubuntu Linux Daemon Build Instructions

    install dependencies:
    Build a node or qt:

    if you need a swap memory:
    free
    dd if=/dev/zero of=/var/swap.img bs=2048 count=1048576
    mkswap /var/swap.img
    swapon /var/swap.img
    free


    sudo apt-get update
    sudo apt-get upgrade

    sudo apt-get install make automake cmake curl g++-multilib libtool binutils-gold bsdmainutils pkg-config python3 patch bison

    git clone https://github.com/BitcoinZeroOfficial/bitcoinzero

    cd bitcoinzero
    for vps:
    cd depends
    make -j4   (-j is optional, number of your cores, -j4)
    cd ..
    ./autogen.sh
    CONFIG_SITE=$PWD/depends/x86_64-pc-linux-gnu/share/config.site ./configure --disable-option-checking --prefix=$PWD/depends/x86_64-pc-linux-gnu --disable-dependency-tracking --enable-zmq --with-gui=no --enable-glibc-back-compat --enable-reduce-exports --disable-shared --with-pic --enable-module-recovery --disable-jni
    cd src/bls-signatures/build && make chiabls_la && cd -  (optional, only if bls libs fail to build)
    cd src/bls-signatures/build && make && cd - (optional, only if bls libs fail to build)
    make -j4   (-j is optional, number of your cores, -j4)

    for qt:
    cd depends
    make
    cd ..
    ./autogen.sh
    CONFIG_SITE=$PWD/depends/x86_64-pc-linux-gnu/share/config.site ./configure --disable-option-checking --prefix=$PWD/depends/x86_64-pc-linux-gnu --disable-dependency-tracking --enable-zmq --with-gui --enable-glibc-back-compat --enable-reduce-exports --disable-shared --with-pic --enable-module-recovery --disable-jni
    cd src/bls-signatures/build && make chiabls_la && cd -   (optional, only if bls libs fail to build)
    cd src/bls-signatures/build && make && cd - (optional, only if bls libs fail to build)
    make -j4   (-j is optional, number of your cores, -j4)

    cd src
    strip bitcoinzerod
    strip bitcoinzero-cli
    or:
    cd src
    cd qt
    strip bitcoinzero-qt

    files are:
    bitcoinzerod
    bitcoinzero-cli
    bitcoinzero-qt
    bitcoinzero.conf

    data folder:
    bitcoinzero

    port 29301
    rpc port 29201

