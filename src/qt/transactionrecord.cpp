// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2018 Oxid developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "base58.h"
#include "instanttx.h"
#include "obfuscation.h"
#include "timedata.h"
#include "utilmoneystr.h"
#include "wallet.h"

#include <stdint.h>

/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx& wtx)
{
    if (wtx.IsCoinBase()) {
        // Ensures we show generated coins / mined transactions at depth 1
        if (!wtx.IsInMainChain()) {
            return false;
        }
    }
    return true;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet* wallet, const CWalletTx& wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.GetComputedTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() nNet=%s nCredit=%d - nDebit=%d +++++++\n", FormatMoney(nNet).c_str(), FormatMoney(nCredit).c_str(), FormatMoney(nDebit).c_str());
    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() wtx=%s +++++++\n", wtx.ToString());

    if (wtx.IsCoinStake()) {
        TransactionRecord sub(hash, nTime);
        unsigned int nStakeIndex = wtx.vout.size() - 3;
        unsigned int nMasternodeIndex = wtx.vout.size() - 2;
        unsigned int nSupernodeIndex = wtx.vout.size() - 1;
        CTxDestination address;
        if (!ExtractDestination(wtx.vout[1].scriptPubKey, address)) {
            return parts;
        }

        unsigned int index = 0;
        CAmount nNodeRewards = 0;
        BOOST_FOREACH (const CTxOut& txout, wtx.vout) {
            if (wallet->IsMine(txout)) {
                if (index == nMasternodeIndex) {
                    nNodeRewards += txout.nValue;
                } else if (index == nSupernodeIndex) {
                    nNodeRewards += txout.nValue;
                }
            }
            index++;
        }
        LogPrintf("+++++++ TransactionRecord::decomposeTransaction() wtx.IsCoinStake() nNodeRewards=%d +++++++\n", FormatMoney(nNodeRewards).c_str());
        index = 0;
        BOOST_FOREACH (const CTxOut& txout, wtx.vout) {
            isminetype mine = wallet->IsMine(txout);
            if (mine) {
                CTxDestination destStake;
                CTxDestination destMasternode;
                CTxDestination destSupernode;
                if (index == nStakeIndex && ExtractDestination(txout.scriptPubKey, destStake) && IsMine(*wallet, destStake)) {
                    isminetype mine = wallet->IsMine(txout);
                    sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                    sub.type = TransactionRecord::StakeMint;
                    sub.address = CBitcoinAddress(destStake).ToString();
                    sub.credit = nNet - nNodeRewards;
                    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() sub.address=%s sub.credit=%d +++++++\n", sub.address, FormatMoney(sub.credit).c_str());
                } else if (index == nMasternodeIndex && ExtractDestination(txout.scriptPubKey, destMasternode) && IsMine(*wallet, destMasternode)) {
                    isminetype mine = wallet->IsMine(txout);
                    sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                    sub.type = TransactionRecord::MNReward;
                    sub.address = CBitcoinAddress(destMasternode).ToString();
                    sub.credit = txout.nValue;
                    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() sub.address=%s sub.credit=%d +++++++\n", sub.address, FormatMoney(sub.credit).c_str());
                } else if (index == nSupernodeIndex && ExtractDestination(txout.scriptPubKey, destSupernode) && IsMine(*wallet, destSupernode)) {
                    isminetype mine = wallet->IsMine(txout);
                    sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                    sub.type = TransactionRecord::SNReward;
                    sub.address = CBitcoinAddress(destSupernode).ToString();
                    sub.credit = txout.nValue;
                    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() sub.address=%s sub.credit=%d +++++++\n", sub.address, FormatMoney(sub.credit).c_str());
                }
                parts.append(sub);
            }
            index++;
        }
    } else if (nNet > 0 || wtx.IsCoinBase()) { // Proof-of-Work active + Masternode
        //
        // Credit
        //
        BOOST_FOREACH (const CTxOut& txout, wtx.vout) {
            isminetype mine = wallet->IsMine(txout);
            if (mine) {
                LogPrintf("+++++++ TransactionRecord::decomposeTransaction() wtx.IsCoinBase() +++++++\n");
                TransactionRecord sub(hash, nTime);
                CTxDestination address;
                sub.idx = parts.size(); // sequence number
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address)) {
                    // Received by Oxid Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = CBitcoinAddress(address).ToString();
                    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() sub.address=%s +++++++\n", sub.address);
                } else {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() sub.address=%s +++++++\n", sub.address);
                }
                if (wtx.IsCoinBase()) {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                parts.append(sub);
            }
        }
    } else { // Sending coins
        LogPrintf("+++++++ TransactionRecord::decomposeTransaction() Sending coins +++++++\n");
        bool fAllFromMeDenom = true;
        int nFromMe = 0;
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        BOOST_FOREACH (const CTxIn& txin, wtx.vin) {
            if (wallet->IsMine(txin)) {
                fAllFromMeDenom = fAllFromMeDenom && wallet->IsDenominated(txin);
                nFromMe++;
            }
            isminetype mine = wallet->IsMine(txin);
            if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if (fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        bool fAllToMeDenom = true;
        int nToMe = 0;
        BOOST_FOREACH (const CTxOut& txout, wtx.vout) {
            if (wallet->IsMine(txout)) {
                fAllToMeDenom = fAllToMeDenom && wallet->IsDenominatedAmount(txout.nValue);
                nToMe++;
            }
            isminetype mine = wallet->IsMine(txout);
            if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if (fAllToMe > mine) fAllToMe = mine;
        }

        if (fAllFromMeDenom && fAllToMeDenom && nFromMe * nToMe) {
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::ObfuscationDenominate, "", -nDebit, nCredit));
            parts.last().involvesWatchAddress = false; // maybe pass to TransactionRecord as constructor argument
        } else if (fAllFromMe && fAllToMe) {

            TransactionRecord sub(hash, nTime);
            // Payment to self by default
            sub.type = TransactionRecord::SendToSelf;
            sub.address = "";

            if (mapValue["DS"] == "1") {
                sub.type = TransactionRecord::Obfuscated;
                CTxDestination address;
                if (ExtractDestination(wtx.vout[0].scriptPubKey, address)) {
                    // Sent to Oxid Address
                    sub.address = CBitcoinAddress(address).ToString();
                    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() sub.address=%s +++++++\n", sub.address);
                } else {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.address = mapValue["to"];
                    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() sub.address=%s +++++++\n", sub.address);
                }
            } else {
                for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
                    const CTxOut& txout = wtx.vout[nOut];
                    sub.idx = parts.size();

                    if (wallet->IsCollateralAmount(txout.nValue)) sub.type = TransactionRecord::ObfuscationMakeCollaterals;
                    if (wallet->IsDenominatedAmount(txout.nValue)) sub.type = TransactionRecord::ObfuscationCreateDenominations;
                    if (nDebit - wtx.GetValueOut() == OBFUSCATION_COLLATERAL) sub.type = TransactionRecord::ObfuscationCollateralPayment;
                }
            }
            CAmount nChange = wtx.GetChange();

            sub.debit = -(nDebit - nChange);
            sub.credit = nCredit - nChange;

            LogPrintf("+++++++ TransactionRecord::decomposeTransaction() 276 sub.credit=%d +++++++\n", sub.credit);

            parts.append(sub);
            parts.last().involvesWatchAddress = involvesWatchAddress; // maybe pass to TransactionRecord as constructor argument
        } else if (fAllFromMe) {
            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.GetValueOut();

            for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
                const CTxOut& txout = wtx.vout[nOut];
                TransactionRecord sub(hash, nTime);
                sub.idx = parts.size();
                sub.involvesWatchAddress = involvesWatchAddress;

                if (wallet->IsMine(txout)) {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address)) {
                    // Sent to Oxid Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.address = CBitcoinAddress(address).ToString();
                    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() sub.address=%s +++++++\n", sub.address);
                } else {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.address = mapValue["to"];
                    LogPrintf("+++++++ TransactionRecord::decomposeTransaction() sub.address=%s +++++++\n", sub.address);
                }

                if (mapValue["DS"] == "1") {
                    sub.type = TransactionRecord::Obfuscated;
                }

                CAmount nValue = txout.nValue;

                LogPrintf("+++++++ TransactionRecord::decomposeTransaction() nValue=%d +++++++\n", FormatMoney(nValue).c_str());

                /* Add fee to first output */
                if (nTxFee > 0) {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                LogPrintf("+++++++ TransactionRecord::decomposeTransaction() nValue=%d +++++++\n", FormatMoney(sub.debit).c_str());

                parts.append(sub);
            }
        } else {
            //
            // Mixed debit transaction, can't break down payees
            //
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
            parts.last().involvesWatchAddress = involvesWatchAddress;
        }
    }
    return parts;
}

void TransactionRecord::updateStatus(const CWalletTx& wtx)
{
    AssertLockHeld(cs_main);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = NULL;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = chainActive.Height();
    status.cur_num_ix_locks = nCompleteTXLocks;

    if (!IsFinalTx(wtx, chainActive.Height() + 1)) {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD) {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.nLockTime - chainActive.Height();
        } else {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if (type == TransactionRecord::Generated || type == TransactionRecord::StakeMint || type == TransactionRecord::MNReward || type == TransactionRecord::SNReward) {
        if (wtx.GetBlocksToMaturity() > 0) {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain()) {
                status.matures_in = wtx.GetBlocksToMaturity();

                // Check if the block was requested by anyone
                if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
            } else {
                status.status = TransactionStatus::NotAccepted;
            }
        } else {
            status.status = TransactionStatus::Confirmed;
        }
    } else {
        if (status.depth < 0) {
            status.status = TransactionStatus::Conflicted;
        } else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0) {
            status.status = TransactionStatus::Offline;
        } else if (status.depth == 0) {
            status.status = TransactionStatus::Unconfirmed;
        } else if (status.depth < RecommendedNumConfirmations) {
            status.status = TransactionStatus::Confirming;
        } else {
            status.status = TransactionStatus::Confirmed;
        }
    }
}

bool TransactionRecord::statusUpdateNeeded()
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.cur_num_ix_locks != nCompleteTXLocks;
}

QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}
