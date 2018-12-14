// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2018 Oxid developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_PAYMENTS_H
#define MASTERNODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "masternode.h"
#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapMasternodeBlocks;
extern CCriticalSection cs_mapMasternodePayeeVotes;

class CMasternodePayments;
class CMasternodePaymentWinner;
class CMasternodeBlockPayees;

extern CMasternodePayments masternodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake);
bool GetMasternodeTransaction(CMasternode* winnerMasternode, CTransaction& txOut, uint256& hashBlock);

void DumpMasternodePayments();

/** Save Masternode Payment Data (mnpayments.dat)
 */
class CMasternodePaymentDB
{
private:
    boost::filesystem::path pathDB;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CMasternodePaymentDB();
    bool Write(const CMasternodePayments& objToSave);
    ReadResult Read(CMasternodePayments& objToLoad, bool fDryRun = false);
};

class CMasternodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;
    unsigned mnTier;

    CMasternodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
        mnTier = CMasternode::nodeTier::UNKNOWN;
    }

    CMasternodePayee(CScript payee, int nVotesIn, unsigned mnTierIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
        mnTier = mnTierIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
        READWRITE(mnTier);
    }
};

// Keep track of votes for payees from masternodes
class CMasternodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CMasternodePayee> vecPayments;

    CMasternodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CMasternodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(unsigned mnTier, CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        auto payee = std::find_if(vecPayments.begin(), vecPayments.end(), [&payeeIn](const CMasternodePayee& p){
            return p.scriptPubKey == payeeIn;
        });

        if(payee == vecPayments.end())
            vecPayments.emplace_back(payeeIn, nIncrement, mnTier);
        else
            payee->nVotes += nIncrement;
    }

    bool GetPayee(CScript& payee, unsigned mnTier) const
    {
        LOCK(cs_vecPayments);

        auto payment = vecPayments.cend();

        for(auto p = vecPayments.cbegin(), e = vecPayments.cend(); p != e; ++p) {

            if(p->mnTier != mnTier)
                continue;

            if(payment == vecPayments.cend() || p->nVotes > payment->nVotes)
                payment = p;
        };

        if(payment == vecPayments.cend())
            return false;

        if(payment->mnTier != mnTier) {
            LogPrintf("CMasternodeBlockPayees::GetPayee() WRONG %s TIER FOUND!\n", CMasternode::mnTierToString(mnTier));
            return false;
        }

        payee = payment->scriptPubKey;

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);
        LogPrintf("CMasternodeBlockPayees::GetPayee() %s payee found: %s\n", CMasternode::mnTierToString(mnTier), address2.ToString().c_str());

        return true;
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CMasternodePayee& p, vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
    }
};

// for storing the winning payments
class CMasternodePaymentWinner
{
public:
    CTxIn vinMasternode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;
    unsigned payeeTier;

    CMasternodePaymentWinner()
    {
        nBlockHeight = 0;
        vinMasternode = CTxIn();
        payee = CScript();
        payeeTier = CMasternode::nodeTier::UNKNOWN;
    }

    CMasternodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinMasternode = vinIn;
        payee = CScript();
        payeeTier = CMasternode::nodeTier::UNKNOWN;
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinMasternode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn, unsigned payeeTierIn)
    {
        payee = payeeIn;
        payeeTier = payeeTierIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinMasternode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        return strprintf("CMasternodePaymentWinner=(%s nBlockHeight=%d Payee=%s Tier=%s)\n", vinMasternode.ToString(), nBlockHeight, payee.ToString(), payeeTier);
    }
};

//
// Masternode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasternodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CMasternodePaymentWinner> mapMasternodePayeeVotes;
    std::map<int, CMasternodeBlockPayees> mapMasternodeBlocks;
    std::map<uint256, int> mapMasternodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CMasternodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);
        mapMasternodeBlocks.clear();
        mapMasternodePayeeVotes.clear();
    }

    bool AddWinningMasternode(CMasternodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CMasternode& mn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee, unsigned mnTier);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CMasternode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outMasternode, int nBlockHeight, unsigned mnTier)
    {
        LOCK(cs_mapMasternodePayeeVotes);

        uint256 key = outMasternode.hash + outMasternode.n + mnTier;

        if (mapMasternodesLastVote.count(key)) {
            if (mapMasternodesLastVote[key] == nBlockHeight) {
                return false;
            }
        }

        //record this masternode voted
        mapMasternodesLastVote[key] = nBlockHeight;
        return true;
    }

    int GetMinMasternodePaymentsProto();
    void ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapMasternodePayeeVotes);
        READWRITE(mapMasternodeBlocks);
    }
};


#endif
