// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientmodel.h"

#include "bantablemodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "peertablemodel.h"

#include "chainparams.h"
#include "checkpoints.h"
#include "clientversion.h"
#include "net.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#include "darksend.h"
#include "bznodeman.h"
#include "bznode-sync.h"

#include <stdint.h>

#include <QDebug>
#include <QTimer>

class CBlockIndex;

static const int64_t nClientStartupTime = GetTime();
static int64_t nLastHeaderTipUpdateNotification = 0;
static int64_t nLastBlockTipUpdateNotification = 0;

ClientModel::ClientModel(OptionsModel *optionsModel, QObject *parent) :
    QObject(parent),
    optionsModel(optionsModel),
    peerTableModel(0),
    cachedbznodeCountString(""),
    banTableModel(0),
    pollTimer(0),
    pollMnTimer(0),

    lockedExodusStateChanged(false),
    lockedExodusBalanceChanged(false)

{
    peerTableModel = new PeerTableModel(this);
    banTableModel = new BanTableModel(this);
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    pollMnTimer = new QTimer(this);
    connect(pollMnTimer, SIGNAL(timeout()), this, SLOT(updateMnTimer()));
    // no need to update as frequent as data for balances/txes/blocks
    pollMnTimer->start(MODEL_UPDATE_DELAY * 4);

    subscribeToCoreSignals();
}

ClientModel::~ClientModel()
{
    unsubscribeFromCoreSignals();
}

int ClientModel::getNumConnections(unsigned int flags) const
{
    LOCK(cs_vNodes);
    if (flags == CONNECTIONS_ALL) // Shortcut if we want total
        return vNodes.size();

    int nNum = 0;
    BOOST_FOREACH(const CNode* pnode, vNodes)
        if (flags & (pnode->fInbound ? CONNECTIONS_IN : CONNECTIONS_OUT))
            nNum++;

    return nNum;
}

QString ClientModel::getbznodeCountString() const
{
    // return tr("Total: %1 (PS compatible: %2 / Enabled: %3) (IPv4: %4, IPv6: %5, TOR: %6)").arg(QString::number((int)mnodeman.size()))
    return tr("Total: %1 (PS compatible: %2 / Enabled: %3)")
            .arg(QString::number((int)mnodeman.size()))
            .arg(QString::number((int)mnodeman.CountEnabled(MIN_PRIVATESEND_PEER_PROTO_VERSION)))
            .arg(QString::number((int)mnodeman.CountEnabled()));
            // .arg(QString::number((int)mnodeman.CountByIP(NET_IPV4)))
            // .arg(QString::number((int)mnodeman.CountByIP(NET_IPV6)))
            // .arg(QString::number((int)mnodeman.CountByIP(NET_TOR)));
}

int ClientModel::getNumBlocks() const
{
    LOCK(cs_main);
    return chainActive.Height();
}

quint64 ClientModel::getTotalBytesRecv() const
{
    return CNode::GetTotalBytesRecv();
}

quint64 ClientModel::getTotalBytesSent() const
{
    return CNode::GetTotalBytesSent();
}

QDateTime ClientModel::getLastBlockDate() const
{
    LOCK(cs_main);

    if (chainActive.Tip())
        return QDateTime::fromTime_t(chainActive.Tip()->GetBlockTime());

    return QDateTime::fromTime_t(Params().GenesisBlock().GetBlockTime()); // Genesis block's time of current network
}

long ClientModel::getMempoolSize() const
{
    return mempool.size();
}

size_t ClientModel::getMempoolDynamicUsage() const
{
    return mempool.DynamicMemoryUsage();
}

double ClientModel::getVerificationProgress(const CBlockIndex *tipIn) const
{
    CBlockIndex *tip = const_cast<CBlockIndex *>(tipIn);
    if (!tip)
    {
        LOCK(cs_main);
        tip = chainActive.Tip();
    }
    return Checkpoints::GuessVerificationProgress(Params().Checkpoints(), tip);
}

void ClientModel::updateTimer()
{
    // no locking required at this point
    // the following calls will acquire the required lock
    Q_EMIT mempoolSizeChanged(getMempoolSize(), getMempoolDynamicUsage());
    Q_EMIT bytesChanged(getTotalBytesRecv(), getTotalBytesSent());
}

void ClientModel::updateMnTimer()
{
    QString newbznodeCountString = getbznodeCountString();

    if (cachedbznodeCountString != newbznodeCountString)
    {
        cachedbznodeCountString = newbznodeCountString;

        Q_EMIT strbznodesChanged(cachedbznodeCountString);
    }
}

void ClientModel::updateNumConnections(int numConnections)
{
    Q_EMIT numConnectionsChanged(numConnections);
}


void ClientModel::invalidateExodusState()
{
    Q_EMIT reinitExodusState();
}

void ClientModel::updateExodusState()
{
    lockedExodusStateChanged = false;
    Q_EMIT refreshExodusState();
}

bool ClientModel::tryLockExodusStateChanged()
{
    // Try to avoid Exodus queuing too many messages for the UI
    if (lockedExodusStateChanged) {
        return false;
    }

    lockedExodusStateChanged = true;
    return true;
}

void ClientModel::updateExodusBalance()
{
    lockedExodusBalanceChanged = false;
    Q_EMIT refreshExodusBalance();
}

bool ClientModel::tryLockExodusBalanceChanged()
{
    // Try to avoid Exodus queuing too many messages for the UI
    if (lockedExodusBalanceChanged) {
        return false;
    }

    lockedExodusBalanceChanged = true;
    return true;
}

void ClientModel::updateExodusPending(bool pending)
{
    Q_EMIT refreshExodusPending(pending);
}

void ClientModel::updateAlert()
{
    Q_EMIT alertsChanged(getStatusBarWarnings());
}

bool ClientModel::inInitialBlockDownload() const
{
    return IsInitialBlockDownload();
}

enum BlockSource ClientModel::getBlockSource() const
{
    if (fReindex)
        return BLOCK_SOURCE_REINDEX;
    else if (fImporting)
        return BLOCK_SOURCE_DISK;
    else if (getNumConnections() > 0)
        return BLOCK_SOURCE_NETWORK;

    return BLOCK_SOURCE_NONE;
}

QString ClientModel::getStatusBarWarnings() const
{
    return QString::fromStdString(GetWarnings("gui"));
}

OptionsModel *ClientModel::getOptionsModel()
{
    return optionsModel;
}

PeerTableModel *ClientModel::getPeerTableModel()
{
    return peerTableModel;
}

BanTableModel *ClientModel::getBanTableModel()
{
    return banTableModel;
}

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}

QString ClientModel::formatSubVersion() const
{
    return QString::fromStdString(strSubVersion);
}

bool ClientModel::isReleaseVersion() const
{
    return CLIENT_VERSION_IS_RELEASE;
}

QString ClientModel::formatClientStartupTime() const
{
    return QDateTime::fromTime_t(nClientStartupTime).toString();
}

QString ClientModel::dataDir() const
{
    return GUIUtil::boostPathToQString(GetDataDir());
}

void ClientModel::updateBanlist()
{
    banTableModel->refresh();
}

// Handlers for core signals
static void ExodusStateInvalidated(ClientModel *clientmodel)
{
    // This will be triggered if a reorg invalidates the state
    QMetaObject::invokeMethod(clientmodel, "invalidateExodusState", Qt::QueuedConnection);
}

static void ExodusStateChanged(ClientModel *clientmodel)
{
    // This will be triggered for each block that contains Exodus layer transactions
    if (clientmodel->tryLockExodusStateChanged()) {
        QMetaObject::invokeMethod(clientmodel, "updateExodusState", Qt::QueuedConnection);
    }
}

static void ExodusBalanceChanged(ClientModel *clientmodel)
{
    // Triggered when a balance for a wallet address changes
    if (clientmodel->tryLockExodusBalanceChanged()) {
        QMetaObject::invokeMethod(clientmodel, "updateExodusBalance", Qt::QueuedConnection);
    }
}

static void ExodusPendingChanged(ClientModel *clientmodel, bool pending)
{
    // Triggered when Exodus pending map adds/removes transactions
    QMetaObject::invokeMethod(clientmodel, "updateExodusPending", Qt::QueuedConnection, Q_ARG(bool, pending));
}

static void ShowProgress(ClientModel *clientmodel, const std::string &title, int nProgress)
{
    // emits signal "showProgress"
    QMetaObject::invokeMethod(clientmodel, "showProgress", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(int, nProgress));
}

static void NotifyNumConnectionsChanged(ClientModel *clientmodel, int newNumConnections)
{
    // Too noisy: qDebug() << "NotifyNumConnectionsChanged: " + QString::number(newNumConnections);
    QMetaObject::invokeMethod(clientmodel, "updateNumConnections", Qt::QueuedConnection,
                              Q_ARG(int, newNumConnections));
}

static void NotifyAdditionalDataSyncProgressChanged(ClientModel *clientmodel, int count, double nSyncProgress)
{
    QMetaObject::invokeMethod(clientmodel, "additionalDataSyncProgressChanged", Qt::QueuedConnection, Q_ARG(int, count),
                              Q_ARG(double, nSyncProgress));
}

static void NotifyAlertChanged(ClientModel *clientmodel)
{
    qDebug() << "NotifyAlertChanged";
    QMetaObject::invokeMethod(clientmodel, "updateAlert", Qt::QueuedConnection);
}

static void BannedListChanged(ClientModel *clientmodel)
{
    qDebug() << QString("%1: Requesting update for peer banlist").arg(__func__);
    QMetaObject::invokeMethod(clientmodel, "updateBanlist", Qt::QueuedConnection);
}

static void BlockTipChanged(ClientModel *clientmodel, bool initialSync, const CBlockIndex *pIndex, bool fHeader)
{
    // lock free async UI updates in case we have a new block tip
    // during initial sync, only update the UI if the last update
    // was > 250ms (MODEL_UPDATE_DELAY) ago
    int64_t now = 0;
    if (initialSync)
        now = GetTimeMillis();

    int64_t& nLastUpdateNotification = fHeader ? nLastHeaderTipUpdateNotification : nLastBlockTipUpdateNotification;

    // if we are in-sync, update the UI regardless of last update time
    if (!initialSync || now - nLastUpdateNotification > MODEL_UPDATE_DELAY) {
        //pass a async signal to the UI thread
        QMetaObject::invokeMethod(clientmodel, "numBlocksChanged", Qt::QueuedConnection,
                                  Q_ARG(int, pIndex->nHeight),
                                  Q_ARG(QDateTime, QDateTime::fromTime_t(pIndex->GetBlockTime())),
                                  Q_ARG(double, clientmodel->getVerificationProgress(pIndex)),
                                  Q_ARG(bool, fHeader));
        nLastUpdateNotification = now;
    }
}

void ClientModel::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.connect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.connect(boost::bind(NotifyAlertChanged, this));
    uiInterface.BannedListChanged.connect(boost::bind(BannedListChanged, this));
    uiInterface.NotifyBlockTip.connect(boost::bind(BlockTipChanged, this, _1, _2, false));
    uiInterface.NotifyHeaderTip.connect(boost::bind(BlockTipChanged, this, _1, _2, true));
    uiInterface.NotifyAdditionalDataSyncProgressChanged.connect(boost::bind(NotifyAdditionalDataSyncProgressChanged, this, _1, _2));

    // Connect Exodus signals
    uiInterface.ExodusStateChanged.connect(boost::bind(ExodusStateChanged, this));
    uiInterface.ExodusPendingChanged.connect(boost::bind(ExodusPendingChanged, this, _1));
    uiInterface.ExodusBalanceChanged.connect(boost::bind(ExodusBalanceChanged, this));
    uiInterface.ExodusStateInvalidated.connect(boost::bind(ExodusStateInvalidated, this));
}

void ClientModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.disconnect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.disconnect(boost::bind(NotifyAlertChanged, this));
    uiInterface.BannedListChanged.disconnect(boost::bind(BannedListChanged, this));
    uiInterface.NotifyBlockTip.disconnect(boost::bind(BlockTipChanged, this, _1, _2, false));
    uiInterface.NotifyHeaderTip.disconnect(boost::bind(BlockTipChanged, this, _1, _2, true));
    uiInterface.NotifyAdditionalDataSyncProgressChanged.disconnect(boost::bind(NotifyAdditionalDataSyncProgressChanged, this, _1, _2));

    // Disconnect Exodus signals
    uiInterface.ExodusStateChanged.disconnect(boost::bind(ExodusStateChanged, this));
    uiInterface.ExodusPendingChanged.disconnect(boost::bind(ExodusPendingChanged, this, _1));
    uiInterface.ExodusBalanceChanged.disconnect(boost::bind(ExodusBalanceChanged, this));
	uiInterface.ExodusStateInvalidated.disconnect(boost::bind(ExodusStateInvalidated, this));
}
