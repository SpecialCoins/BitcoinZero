# Bznode Instructions and Notes

- Version 0.2.2
- Date: 20 jan 2020

## Prerequisites

- Ubuntu 16.04+
- Port **29301** is open
- Libraries to build from bitcoinzero source if you want to build it yourself

## Step 0. ON VPS: Acquire the binaries

Either

<details open>
<summary><strong>Download the prebuilt binaries</strong></summary>
<strong>0.1</strong> Install prebuild client and full chain

    wget https://github.com/SpecialCoins/BitcoinZero/releases/download/5.0.3.8/linux-x64.tar.gz
    tar xvfz linux-x64.tar.gz
    ---
    (optional to remove old chain):
    cd .bitcoinzero
    rm -f -r blocks
    rm -f -r chainsate
	or/and:
	(option(2) to create chainfiles):
	mkdir .bitcoinzero
	cd .bitcoinzero
	sudo apt-get install unzip
	wget https://github.com/SpecialCoins/BitcoinZero/releases/download/chainfiles/chainfiles.zip
	unzip chainfiles.zip
	cd ..
	---
    ./bitcoinzerod -daemon
    ./bitcoinzero-cli getinfo

</details>

or

<details>
<summary><strong>Build from source</strong></summary>
<strong>0.1.</strong>  Check out from source:

    git clone https://github.com/BitcoinZeroOfficial/bitcoinzero/

<strong>0.2.</strong> See [README.md](README.md) for instructions on building.

</details>

## Step 1. ON VPS: Open port 29301 (Optional - only if firewall is running)

**1.1.** Run:

    sudo ufw allow ssh
    sudo ufw allow 29301
    sudo ufw default allow outgoing
    sudo ufw enable

## Step 2. ON LOCAL MACHINE: First run on your Local Wallet

<details open>
<summary><strong>If you are using the qt wallet</strong></summary>
<strong>2.0.</strong>  Open the wallet

<strong>2.1.</strong> Click Help -> Debug Window -> Console

<strong>2.2.</strong> Generate bznodeprivkey:

    bznode genkey

(Store this key)

<strong>2.3.</strong> Get wallet address:

    getaccountaddress BZ1

<strong>2.4.</strong> Send to received address <strong>exactly 45000 BZX</strong> in <strong>1 transaction</strong>. Wait for 15 confirmations.

<strong>2.5.</strong> Close the wallet

</details>

<details>
<summary><strong>If you are using the daemon</strong></summary>
<strong>2.0.</strong>  Go to the checked out folder or where you extracted the binaries

    cd bitcoinzero/src

<strong>2.1.</strong> Start daemon:

    ./bitcoinzerod -daemon -server

<strong>2.2.</strong> Generate bznodeprivkey:

    ./bitcoinzero-cli bznode genkey

(Store this key)

<strong>2.3.</strong> Get wallet address:

    ./bitcoinzero-cli getaccountaddress BZ1

<strong>2.4.</strong> Send to received address <strong>exactly 45000 BZX</strong> in <strong>1 transaction</strong>. Wait for 15 confirmations.

<strong>2.5.</strong> Stop daemon:

    ./bitcoinzero-cli stop

</details>

## For both:

**2.6.** Create file **bznode.conf** (in **~/.bitcoinzero**, **C:\Users\USER\AppData\Roaming\bitcoinzero** or **~/Library/Application Support/bitcoinzero** depending on your Operating System) containing the following info:

- LABEL: A one word name you make up to call your node (ex. BZ1)
- IP:PORT: Your bznode VPS's IP, and the port is always 29301.
- BZNODEPRIVKEY: This is the result of your "bznode genkey" from earlier.
- TRANSACTION HASH: The collateral tx. hash from the 45000 BZX deposit.
- INDEX: The Index from the transaction hash

To get TRANSACTION HASH, run:

```
./bitcoinzero-cli bznode outputs
```

or

```
bznode outputs
```

depending on your wallet/daemon setup.

The output will look like:

    { "d6fd38868bb8f9958e34d5155437d009b72dfd33fc28874c87fd42e51c0f74fdb" : "0", }

Sample of bznode.conf:

    BZ1 51.52.53.54:29301 XrxSr3fXpX3dZcU7CoiFuFWqeHYw83r28btCFfIHqf6zkMp1PZ4 d6fd38868bb8f9958e34d5155437d009b72dfd33fc28874c87fd42e51c0f74fdb 0

**2.7.** Lock unspent

As long as the bznode is listed in your bznode.conf file the funds are automatically locked so you don't accidentially spend them.

## Step 3. ON VPS: Update config files

**3.1.** Create file **bitcoinzero.conf** (in folder **~/.bitcoinzero**)

    server=1
    bznode=1
    bznodeprivkey=XXXXXXXXXXXXXXXXX  ## Replace with your bznode private key
    externalip=XXX.XXX.XXX.XXX ## Replace with your node external IP

## Step 4. ON LOCAL MACHINE: Start the bznode

<details open>
<summary><strong>With qt wallet</strong></summary>
<strong>4.1</strong> Start the bznode via your gui wallet in the bznodes tab
</details>

<details>
<summary><strong>With daemon</strong></summary>
<strong>4.1</strong> Start bznode:

    ./bitcoinzero-cli bznode start-alias <LABEL>

For example:

    ./bitcoinzero-cli bznode start-alias BZ1

<strong>4.2</strong> To check node status:

    ./bitcoinzero-cli bznode debug

</details>

If not successfully started, just repeat start command
