# Bitcoinzero [BZX] (Sigma) Core 2020

[![Build Status](https://travis-ci.org/BitcoinZeroOfficial/bitcoinzero.svg?branch=master)](https://travis-ci.org/BitcoinZeroOfficial/bitcoinzero)

## Bitcoinzero

- Coin Suffix: BZX
- Algorithm: Lyra2Z
- Target Spacing: 150 Seconds
- Retarget: every block
- Confirmation: 6 Blocks
- Maturity: 120 Blocks
- Max Coins: 40,000,000 BZX
- Min TX Fee: 0.001 BZX
- Block Size: 4MB

## Net Parameters

- P2P Port=29301
- RPC Port=29201
- Client core=13.4
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

    sudo apt-get install git build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils libboost-all-dev

    sudo apt-get install software-properties-common
    sudo add-apt-repository ppa:bitcoin/bitcoin
    sudo apt-get update
    sudo apt-get install libdb4.8-dev libdb4.8++-dev

    sudo apt-get install libminiupnpc-dev libzmq3-dev
    for qt:
    sudo apt-get install libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools libprotobuf-dev protobuf-compiler libqrencode-dev

    git clone https://github.com/BitcoinZeroOfficial/bitcoinzero

    cd bitcoinzero
    for vps:
    ./autogen.sh
    ./configure  --without-gui
    make -j 4   (-j is optional, number of your cores, -j 4)

    for qt:
    ./autogen.sh
    ./configure
    make -j 4   (-j is optional, number of your cores, -j 4)

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
    bznode.conf
    data folder:
    bitcoinzero

    port 29301
    rpc port 29201

# Example bitcoinzero.conf Configuration

    rescan=0
    listen=1
    server=1
    daemon=1
    bznode=1
    externalip=
    bznodeprivkey=
    addnode=node_ip
    rpcallowip=127.0.0.1
    rpcuser=MAKEUPYOUROWNUSERNAME
    rpcpassword=MAKEUPYOUROWNPASSWORD
