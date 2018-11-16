// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2018 Oxid developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addrman.h"

#include "masternode-payments.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "obfuscation.h"
#include "spork.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapMasternodeBlocks;
CCriticalSection cs_mapMasternodePayeeVotes;

//
// CMasternodePaymentDB
//

CMasternodePaymentDB::CMasternodePaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "MasternodePayments";
}

bool CMasternodePaymentDB::Write(const CMasternodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // masternode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("masternode","Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CMasternodePaymentDB::ReadResult CMasternodePaymentDB::Read(CMasternodePayments& objToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CMasternodePayments object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("masternode","Loaded info from mnpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("masternode","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("masternode","Masternode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrint("masternode","Masternode payments manager - result:\n");
        LogPrint("masternode","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpMasternodePayments()
{
    int64_t nStart = GetTimeMillis();

    CMasternodePaymentDB paymentdb;
    CMasternodePayments tempPayments;

    LogPrint("masternode","Verifying mnpayments.dat format...\n");
    CMasternodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CMasternodePaymentDB::FileError)
        LogPrint("masternode","Missing budgets file - mnpayments.dat, will try to recreate\n");
    else if (readResult != CMasternodePaymentDB::Ok) {
        LogPrint("masternode","Error reading mnpayments.dat: ");
        if (readResult == CMasternodePaymentDB::IncorrectFormat)
            LogPrint("masternode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("masternode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("masternode","Writting info to mnpayments.dat...\n");
    paymentdb.Write(masternodePayments);

    LogPrint("masternode","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint("masternode","IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    } else if (nHeight == SLOW_START_BLOCK + 1) {
        LogPrintf("IsBlockValueValid() nHeight=%d nMinted=%d MAX_REWARD=%d\n", nHeight, nMinted, MAX_REWARD * COIN);
        if (nMinted > MAX_REWARD * COIN) {
            return false;
        } else {
            return true;
        }
    }

    LogPrintf("IsBlockValueValid(): nMinted: %d, nExpectedValue: %d\n", FormatMoney(nMinted), FormatMoney(nExpectedValue));

    if (nMinted > nExpectedValue) {
        return false;
    }

    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
    if (!masternodeSync.IsSynced()) { // find the longest chain
        LogPrint("mnpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

    const CTransaction& txNew = (nBlockHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

    //check for masternode payee
    if (masternodePayments.IsTransactionValid(txNew, nBlockHeight)) {
        return true;
    }

    if (IsSporkActive(SPORK_6_MASTERNODE_PAYMENT_ENFORCEMENT)) {
        return false;
    }
    LogPrintf("Masternode payment enforcement is disabled, accepting block\n");

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;
    masternodePayments.FillBlockPayee(txNew, nFees, fProofOfStake);
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    return masternodePayments.GetRequiredPaymentsString(nBlockHeight);
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;
    CAmount blockValue = GetBlockValue(pindexPrev->nHeight);

    // ------------------ MASTERNODE ------------------
    bool hasMasternodePayment = true;
    CScript masternodePayee;
    unsigned mnTier = CMasternode::nodeTier::MASTERNODE;

    //spork
    if (!masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, masternodePayee, mnTier)) {
        //no masternode detected
        CMasternode* winningMasternode = mnodeman.GetCurrentMasterNode(mnTier, 1);
        if (winningMasternode) {
            masternodePayee = GetScriptForDestination(winningMasternode->pubKeyCollateralAddress.GetID());
            LogPrintf("CMasternodePayments::FillBlockPayee(Masternode) Winning %d block Masternode [%s] -> %s\n", pindexPrev->nHeight, CMasternode::mnTierToString(winningMasternode->mnTier()), winningMasternode->ToString());
        } else {
            LogPrintf("CMasternodePayments::FillBlockPayee(Masternode) Failed to detect %s to pay\n", CMasternode::mnTierToString(mnTier));
            hasMasternodePayment = false;
        }
    } else {
        CTxDestination address5;
        ExtractDestination(masternodePayee, address5);
        CBitcoinAddress address6(address5);
        LogPrintf("CMasternodePayments::FillBlockPayee(Masternode) block %d payee found: %s\n", pindexPrev->nHeight + 1, address6.ToString().c_str());
    }

    CAmount masternodePayment = GetMasternodePayment(pindexPrev->nHeight, blockValue, mnTier);

    // ------------------ SUPERNODE ------------------
    bool hasSupernodePayment = true;
    CScript supernodePayee;
    unsigned snTier = CMasternode::nodeTier::SUPERNODE;

    //spork
    if (!masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, supernodePayee, snTier)) {
        //no supernode detected
        CMasternode* winningSupernode = mnodeman.GetCurrentMasterNode(snTier, 1);
        if (winningSupernode) {
            supernodePayee = GetScriptForDestination(winningSupernode->pubKeyCollateralAddress.GetID());
            LogPrintf("CMasternodePayments::FillBlockPayee(Supernode) Winning %d block Supernode [%s] -> %s\n", pindexPrev->nHeight, CMasternode::mnTierToString(winningSupernode->mnTier()), winningSupernode->ToString());
        } else {
            LogPrintf("CMasternodePayments::FillBlockPayee(Supernode) Failed to detect %s to pay\n", CMasternode::mnTierToString(snTier));
            hasSupernodePayment = false;
        }
    } else {
        CTxDestination address7;
        ExtractDestination(supernodePayee, address7);
        CBitcoinAddress address8(address7);
        LogPrintf("CMasternodePayments::FillBlockPayee(Supernode) block %d payee found: %s\n", pindexPrev->nHeight + 1, address8.ToString().c_str());
    }

    CAmount supernodePayment = GetMasternodePayment(pindexPrev->nHeight, blockValue, snTier);

    LogPrintf("CMasternodePayments::FillBlockPayee(Masternode): block %d value: %d => Masternode payment: %d\n", pindexPrev->nHeight, FormatMoney(blockValue).c_str(), FormatMoney(masternodePayment).c_str());
    LogPrintf("CMasternodePayments::FillBlockPayee(Supernode):  block %d value: %d => Supernode payment:  %d\n", pindexPrev->nHeight, FormatMoney(blockValue).c_str(), FormatMoney(supernodePayment).c_str());

    CAmount nValueToSubtract = masternodePayment + supernodePayment;
    if (hasMasternodePayment || hasSupernodePayment) {

        if (fProofOfStake) { // Proof-of-Stake // TX2<EMPTY-vout, vout>
            /**For Proof Of Stake vout[0] must be null
             * Stake reward can be split into many different outputs, so we must
             * use vout.size() to align with several different cases.
             * An additional output is appended as the masternode payment
             */
            LogPrintf("CMasternodePayments::FillBlockPayee(): Proof-of-Stake + Masternodes +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
            unsigned int i = 0;
            i = txNew.vout.size();
            txNew.vout.resize(i + 2);
            i = txNew.vout.size(); // new size
            unsigned int nStakeIndex = i - 3;
            unsigned int nMasternodeIndex = i - 2;
            unsigned int nSupernodeIndex = i - 1;

            if (hasMasternodePayment) {
                txNew.vout[nMasternodeIndex].scriptPubKey = masternodePayee;
                txNew.vout[nMasternodeIndex].nValue = masternodePayment;

                LogPrintf("CMasternodePayments::FillBlockPayee(Masternode): Proof-of-Stake txNew: \n%s\n", txNew.ToString());
                LogPrintf("CMasternodePayments::FillBlockPayee(Masternode): Proof-of-Stake Masternode reward txNew.vout[%d].nValue=%d\n", nMasternodeIndex, FormatMoney(txNew.vout[nMasternodeIndex].nValue).c_str());
            } else {
                txNew.vout[nMasternodeIndex].SetEmpty();
            }

            if (hasSupernodePayment) {
                txNew.vout[nSupernodeIndex].scriptPubKey = supernodePayee;
                txNew.vout[nSupernodeIndex].nValue = supernodePayment;

                LogPrintf("CMasternodePayments::FillBlockPayee(Supernode): Proof-of-Stake txNew: \n%s\n", txNew.ToString());
                LogPrintf("CMasternodePayments::FillBlockPayee(Supernode): Proof-of-Stake Supernode reward txNew.vout[%d].nValue=%d\n", i, FormatMoney(txNew.vout[nSupernodeIndex].nValue).c_str());
            } else {
                txNew.vout[nSupernodeIndex].SetEmpty();
            }

            // Subtract masternode and supernode payment from the stake reward
            txNew.vout[nStakeIndex].nValue -= nValueToSubtract;

        } else { // Proof-of-Work
            LogPrintf("CMasternodePayments::FillBlockPayee(): Proof-of-Work + Masternodes +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
            txNew.vout.resize(3);
            unsigned int nStakeIndex = 0;
            unsigned int nMasternodeIndex = 1;
            unsigned int nSupernodeIndex = 2;

            if (hasMasternodePayment) {

                txNew.vout[nMasternodeIndex].scriptPubKey = masternodePayee;
                txNew.vout[nMasternodeIndex].nValue = masternodePayment;
                LogPrintf("CMasternodePayments::FillBlockPayee(Masternode): Proof-of-Work: \n%s \nSize: %d \nValue: %d\n", txNew.ToString(), txNew.vout.size(), txNew.vout[nMasternodeIndex].nValue);
            } else {
                txNew.vout[nMasternodeIndex].SetEmpty();
            }

            if (hasSupernodePayment) {
                txNew.vout[nSupernodeIndex].scriptPubKey = supernodePayee;
                txNew.vout[nSupernodeIndex].nValue = supernodePayment;
                LogPrintf("CMasternodePayments::FillBlockPayee(Supernode): Proof-of-Work: \n%s \nSize: %d \nValue: %d\n", txNew.ToString(), txNew.vout.size(), txNew.vout[nSupernodeIndex].nValue);
            } else {
                txNew.vout[nSupernodeIndex].SetEmpty();
            }

            // Subtract masternode and supernode payment from the proof-of-work reward
            txNew.vout[nStakeIndex].nValue = blockValue - nValueToSubtract;
        }

        if (hasMasternodePayment) {
            CTxDestination address1;
            ExtractDestination(masternodePayee, address1);
            CBitcoinAddress address2(address1);
            LogPrintf("CMasternodePayments::FillBlockPayee(Masternode) %s payment of %s to %s\n", CMasternode::mnTierToString(mnTier), FormatMoney(masternodePayment).c_str(), address2.ToString().c_str());
        }
        if (hasSupernodePayment) {
            CTxDestination address3;
            ExtractDestination(supernodePayee, address3);
            CBitcoinAddress address4(address3);
            LogPrintf("CMasternodePayments::FillBlockPayee(Supernode) %s payment of %s to %s\n", CMasternode::mnTierToString(snTier), FormatMoney(supernodePayment).c_str(), address4.ToString().c_str());
        }
    } else {
        if (!fProofOfStake) {
            txNew.vout[0].nValue = blockValue;
            LogPrintf("CMasternodePayments::FillBlockPayee(): Proof-of-Work: %s \nSize: %d \nValue: %d\n", txNew.ToString(), txNew.vout.size(), txNew.vout[0].nValue);
        } else {
            unsigned int i = txNew.vout.size();
            txNew.vout.resize(i + 2);
            i = txNew.vout.size(); // new size
            unsigned int nStakeIndex = i - 3;
            unsigned int nMasternodeIndex = i - 2;
            unsigned int nSupernodeIndex = i - 1;
            // Just in case masternode fails we don't want to pay it's rewards to staker
            txNew.vout[nStakeIndex].nValue -= nValueToSubtract;
            txNew.vout[nMasternodeIndex].SetEmpty();
            txNew.vout[nSupernodeIndex].SetEmpty();
            LogPrintf("CMasternodePayments::FillBlockPayee() No MN/SN Found. Stake Reward: %s\n", FormatMoney(txNew.vout[nStakeIndex].nValue).c_str());
        }
    }
}

int CMasternodePayments::GetMinMasternodePaymentsProto()
{
    if (IsSporkActive(SPORK_7_MASTERNODE_PAY_UPDATED_NODES))
        return ActiveProtocol();                          // Allow only updated peers
    else
        return MIN_PEER_PROTO_VERSION_BEFORE_ENFORCEMENT; // Also allow old peers as long as they are allowed to run
}

void CMasternodePayments::ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!masternodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Obfuscation/Masternode related functionality

    if (strCommand == "mnget") { //Masternode Payments Request Sync
        if (fLiteMode) return;   //disable all Obfuscation/Masternode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest("mnget")) {
                if (masternodeSync.IsSynced()) {
                    LogPrintf("CMasternodePayments::ProcessMessageMasternodePayments() mnget - peer already asked me for the list\n");
                    Misbehaving(pfrom->GetId(), 20);
                    return;
                }
            }
        }

        pfrom->FulfilledRequest("mnget");
        masternodePayments.Sync(pfrom, nCountNeeded);
        LogPrintf("mnget - Sent Masternode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == "mnw") { //Masternode Payments Declare Winner
        //this is required in litemodef
        CMasternodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        auto winnerMasternode = mnodeman.Find(winner.payee);

        if (!winnerMasternode) {
            LogPrintf("mnw - Unknown payee %s\n", address2.ToString().c_str());
            return;
        }

        if (winnerMasternode->mnTier() == CMasternode::nodeTier::UNKNOWN) {
            LogPrintf("mnw - Masternode tier is UNKNOWN!!! Exiting...\n");
            return;
        }

        winner.payeeTier = winnerMasternode->mnTier();

        if (masternodePayments.mapMasternodePayeeVotes.count(winner.GetHash())) {
            LogPrintf("mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            if(strError != "") {
                LogPrintf("mnw - invalid message 1 - %s\n", strError);
            }
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled(winner.payeeTier) / 100 * 125);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrintf("mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        if (!winner.IsValid(pfrom, strError)) {
            if(strError != "") {
                LogPrintf("mnw - invalid message 2 - %s\n", strError);
            }
            return;
        }

        if (!masternodePayments.CanVote(winner.vinMasternode.prevout, winner.nBlockHeight, winner.payeeTier)) {
            LogPrintf("mnw - masternode already voted - %s\n", winner.vinMasternode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            if (masternodeSync.IsSynced()) {
                LogPrintf("MISBEHAVING: !winner.SignatureValid() and masternodeSync.IsSynced()\n");
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, winner.vinMasternode);
            return;
        }

        if (masternodePayments.AddWinningMasternode(winner)) {
            winner.Relay();
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
        }
    }
}

bool CMasternodePaymentWinner::Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode)
{
    std::string errorMessage;
    std::string strMasterNodeSignMessage;

    std::string strMessage = vinMasternode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             payee.ToString();

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyMasternode)) {
        LogPrintf("CMasternodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyMasternode, vchSig, strMessage, errorMessage)) {
        LogPrintf("CMasternodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee, unsigned mnTier)
{
    auto block = mapMasternodeBlocks.find(nBlockHeight);

    if (block == mapMasternodeBlocks.cend()) {
        return false;
    }

    return block->second.GetPayee(payee, mnTier);
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMasternodePayments::IsScheduled(CMasternode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapMasternodeBlocks.count(h)) {
            if (mapMasternodeBlocks[h].GetPayee(payee, mn.mnTier())) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CMasternodePayments::AddWinningMasternode(CMasternodePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

        if (mapMasternodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapMasternodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapMasternodeBlocks.count(winnerIn.nBlockHeight)) {
            CMasternodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapMasternodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapMasternodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payeeTier, winnerIn.payee, 1);

    return true;
}

bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    std::map<unsigned, int> maxSignatures;
    std::string strPayeesPossible = "";

    //require at least 6 signatures
    BOOST_FOREACH (CMasternodePayee& payee, vecPayments) {
        if (payee.nVotes < MNPAYMENTS_SIGNATURES_REQUIRED) continue;
        auto node = maxSignatures.emplace(payee.mnTier, payee.nVotes);
        if (node.second) continue;
        if (payee.nVotes >= node.first->second) {
            node.first->second = payee.nVotes;
        }
    }

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (!maxSignatures.size()) return true;

    CAmount nReward = GetBlockValue(nBlockHeight);

    BOOST_FOREACH (const CMasternodePayee& payee, vecPayments) {
        if (payee.nVotes < MNPAYMENTS_SIGNATURES_REQUIRED) continue;

        auto requiredMasternodePayment = GetMasternodePayment(nBlockHeight, nReward, payee.mnTier);
        auto payeeOut = std::find_if(txNew.vout.cbegin(), txNew.vout.cend(), [&payee, &requiredMasternodePayment](const CTxOut& out) {
            bool payeeFound = payee.scriptPubKey == out.scriptPubKey;
            bool isRequiredMasternodePayment = out.nValue >= requiredMasternodePayment;
            if (payeeFound && !isRequiredMasternodePayment) {
                LogPrintf("Masternode payment is out of range. out.nValue=%s requiredMasternodePayment=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredMasternodePayment).c_str());
            }
            return payeeFound && isRequiredMasternodePayment;
        });

        if (payeeOut != txNew.vout.cend()) {
            maxSignatures.erase(payee.mnTier);
            if (maxSignatures.size()) continue;
            return true;
        }

        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);

        auto address2 = std::to_string(payee.mnTier) + ":" + CBitcoinAddress{address1}.ToString();

        if (strPayeesPossible == "") {
            strPayeesPossible += address2;
        } else {
            strPayeesPossible += "," + address2;
        }
    }

    LogPrintf("CMasternodePayments::IsTransactionValid - Missing required payment to %s\n", strPayeesPossible.c_str());
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    BOOST_FOREACH (CMasternodePayee& payee, vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        std::string payeeString = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.mnTier) + ":" + boost::lexical_cast<std::string>(payee.nVotes);

        if (ret != "Unknown") {
            ret += "," + payeeString;
        } else {
            ret = payeeString;
        }
    }

    return ret;
}

std::string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CMasternodePayments::CleanPaymentList()
{
    LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() / 100 * 125), 1000);

    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CMasternodePayments::CleanPaymentList - Removing old Masternode payment - block %d\n", winner.nBlockHeight);
            masternodeSync.mapSeenSyncMNW.erase((*it).first);
            mapMasternodePayeeVotes.erase(it++);
            mapMasternodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CMasternodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (!pmn) {
        strError = strprintf("Unknown Masternode %s", vinMasternode.prevout.hash.ToString());
        LogPrint("masternode","CMasternodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinMasternode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Masternode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint("masternode","CMasternodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetMasternodeRank(vinMasternode, nBlockHeight - 100, ActiveProtocol());

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("masternode","CMasternodePaymentWinner::IsValid - %s\n", strError);
            //if (masternodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fMasterNode) return false;

    if (nBlockHeight <= nLastBlockHeight) return false;

    int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight - 100, ActiveProtocol());

    if (n == -1) {
        LogPrintf("CMasternodePayments::ProcessBlock - Unknown Masternode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrintf("CMasternodePayments::ProcessBlock - Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    std::string errorMessage;
    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!obfuScationSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode)) {
        LogPrintf("CMasternodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    std::vector<CMasternodePaymentWinner> winners;

    for (unsigned mnTier = CMasternode::nodeTier::MASTERNODE; mnTier <= CMasternode::nodeTier::SUPERNODE; ++mnTier) {

        CMasternodePaymentWinner newWinner{activeMasternode.vin};
        std::string mnTierString = mnTier == CMasternode::nodeTier::MASTERNODE ? "MASTERNODE" : "SUPERNODE";

        LogPrintf("BLOCK: %d CMasternodePayments::ProcessBlock() -> %s newWinner: %s\n", nBlockHeight, mnTierString, newWinner.ToString());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CMasternode* pmn = mnodeman.GetNextMasternodeInQueueForPayment(nBlockHeight, mnTier, true, nCount);

        if (!pmn) {
            LogPrintf("CMasternodePayments::ProcessBlock() Failed to find %s to pay\n", mnTierString);
            continue;
        }

        LogPrintf("CMasternodePayments::ProcessBlock() Next Masternode in the queue for payment: %s Tier=%s\n", pmn->ToString(), pmn->mnTierString());

        newWinner.nBlockHeight = nBlockHeight;

        CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
        newWinner.AddPayee(payee, mnTier);

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("CMasternodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);

        if (!newWinner.Sign(keyMasternode, pubKeyMasternode)) {
            LogPrintf("CMasternodePayments::ProcessBlock() - Failed to sign winner %s\n", mnTierString);
            continue;
        } else {
            LogPrintf("CMasternodePayments::ProcessBlock() - Signed winner %s\n", mnTierString);
        }

        if (!AddWinningMasternode(newWinner)) {
            LogPrintf("CMasternodePayments::ProcessBlock() - Failed to add winner %s\n", mnTierString);
            continue;
        } else {
            LogPrintf("CMasternodePayments::ProcessBlock() - Added winner %s\n", mnTierString);
        }

        winners.emplace_back(newWinner);
    }

    if (winners.empty()) return false;

    for (CMasternodePaymentWinner& winner : winners) {

        // Check for correct winner
        CTxDestination address3;
        ExtractDestination(winner.payee, address3);
        CBitcoinAddress address4(address3);
        LogPrintf("CMasternodePayments::ProcessBlock(nBlock=%d) - Relaying %s with address %s\n", winner.nBlockHeight, CMasternode::mnTierToString(winner.payeeTier), address4.ToString().c_str());

        winner.Relay();
    }

    nLastBlockHeight = nBlockHeight;

    return true;
}

void CMasternodePaymentWinner::Relay()
{
    CInv inv(MSG_MASTERNODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CMasternodePaymentWinner::SignatureValid()
{
    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (pmn != NULL) {
        std::string strMessage = vinMasternode.prevout.ToStringShort() +
                                 boost::lexical_cast<std::string>(nBlockHeight) +
                                 payee.ToString();

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pmn->pubKeyMasternode, vchSig, strMessage, errorMessage)) {
            return error("CMasternodePaymentWinner::SignatureValid() - Got bad Masternode address signature %s\n", vinMasternode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CMasternodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapMasternodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    auto nCounts = mnodeman.CountEnabledByTiers();
	for (auto& count : nCounts) {
        count.second = std::min(nCountNeeded, count.second / 100 * 125);
	}

    int nInvCount = 0;
    for (const auto& masternodePayeeVote : mapMasternodePayeeVotes) {
        CMasternodePaymentWinner winner = masternodePayeeVote.second;
        if (winner.nBlockHeight >= nHeight - nCounts[winner.payeeTier] && winner.nBlockHeight <= nHeight + 20) {
	        node->PushInventory(CInv(MSG_MASTERNODE_WINNER, winner.GetHash()));
	        ++nInvCount;
        }
    }

    node->PushMessage("ssc", MASTERNODE_SYNC_MNW, nInvCount);
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}

int CMasternodePayments::GetOldestBlock()
{
    LOCK(cs_mapMasternodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CMasternodeBlockPayees>::iterator it = mapMasternodeBlocks.begin();
    while (it != mapMasternodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}

int CMasternodePayments::GetNewestBlock()
{
    LOCK(cs_mapMasternodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CMasternodeBlockPayees>::iterator it = mapMasternodeBlocks.begin();
    while (it != mapMasternodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
