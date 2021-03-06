#include <graphenelib/asset.h>
#include <graphenelib/contract.hpp>
#include <graphenelib/contract_asset.hpp>
#include <graphenelib/crypto.h>
#include <graphenelib/dispatcher.hpp>
#include <graphenelib/global.h>
#include <graphenelib/multi_index.hpp>
#include <graphenelib/print.hpp>
#include <graphenelib/system.h>
#include <graphenelib/types.h>

using namespace graphene;

class redpacket : public contract
{
  public:
    redpacket(uint64_t id)
        : contract(id)
        , packets(_self, _self)
        , records(_self, _self)
    {
    }

    // @abi action
    // @abi payable
    void create(std::string pubkey, uint64_t number)
    {
        // validate pubkey

        int64_t total_amount = get_action_asset_amount();
        uint64_t asset_id = get_action_asset_id();
        uint64_t owner = get_trx_sender();

        auto packet_it = packets.find(owner);
        graphene_assert(packet_it == packets.end(), "already has one redpacket");
        graphene_assert(number <= 1000, "max 1000 redpacket");

        // allocate redpacket
        int64_t block_num = get_head_block_num();
        vector<int> shares;
        int64_t shares_sum = 0;
        for (int i = 0; i < number; i++) {
            std::string random_str = pubkey + std::to_string(i) + std::to_string(block_num);
            print("random_str = ", random_str.c_str(), "\n");

            checksum160 sum160;
            ripemd160(const_cast<char *>(random_str.c_str()), random_str.length(), &sum160);
            uint8_t share = sum160.hash[0] == 0 ? 10 : sum160.hash[0];
            shares.emplace_back(share);
            shares_sum += share;
        }

        packets.emplace(owner, [&](auto &o) {
            o.issuer = owner;
            o.pub_key = pubkey;
            o.total_amount = contract_asset{total_amount, asset_id};
            o.number = number;
            int64_t share_used_sum = 0;
            for (int i = 0; i < number - 1; i++) {
                int64_t share_amount = total_amount * shares[i] / shares_sum;
                print("share: ", shares[i], "share amount: ", share_amount,  "\n");
                o.subpackets.emplace_back(share_amount);
                share_used_sum += share_amount;
            }
            o.subpackets.emplace_back(total_amount - share_used_sum);
        });
    }

    // @abi action
    void open(uint64_t packet_issuer, std::string &sig, uint64_t timestamp)
    {
        uint64_t sender = get_trx_sender();
        // check timestamp

        // check redpacket
        auto packet_iter = packets.find(packet_issuer);
        graphene_assert(packet_iter != packets.end(), "no redpacket");

        // check signature
        std::string s = std::to_string(timestamp);
        int ret = verify_signature(s.c_str(), s.length(), sig.c_str(), sig.length(), packet_iter->pub_key.c_str(), packet_iter->pub_key.length());
        graphene_assert(ret == 0, "signature not valid");

        // check record
        auto record_iter = records.find(packet_issuer);
        if (record_iter == records.end()) {
            records.emplace(sender, [&](auto& o){
                            o.packet_issuer = packet_issuer;
                            o.accounts = {};
                            });
            record_iter = records.find(packet_issuer);
        } else {
            auto act_iter = std::find_if(record_iter->accounts.begin(), record_iter->accounts.end(),
                                    [&](const account& act) { return act.account_id == sender; });
            graphene_assert(act_iter == record_iter->accounts.end(), "redpacket can only be opened once");
        }

        // update records
        uint64_t idx = timestamp % packet_iter->subpackets.size();
        records.modify(record_iter, sender, [&](auto &o) {
            o.packet_issuer = packet_issuer;
            o.accounts.push_back({sender, packet_iter->subpackets[idx]});
        });

        auto subpacket_it = packet_iter->subpackets.begin() + idx;
        packets.modify(packet_iter, sender, [&](auto &o) {
            o.subpackets.erase(subpacket_it);
        });

        // if left packet number is 0
        if (packet_iter->subpackets.empty()) {
            // remove packet
            packets.erase(packet_iter);
            records.erase(record_iter);
        }
        withdraw_asset(_self, sender, packet_iter->total_amount.asset_id, packet_iter->subpackets[idx]);
    }

    // @abi action
    void close()
    {
        uint64_t owner = get_trx_sender();
        auto packet_iter = packets.find(owner);
        graphene_assert(packet_iter != packets.end(), "no redpacket");

        auto record_iter = records.find(owner);
        graphene_assert(record_iter != records.end(), "no redpacket");

        uint64_t asset_id = packet_iter->total_amount.asset_id;
        int64_t left_amount;
        for (uint64_t subpacket : packet_iter->subpackets) {
            left_amount += subpacket;
        }

        packets.erase(packet_iter);
        records.erase(record_iter);

        withdraw_asset(_self, owner, asset_id, left_amount);
    }

  private:
    //@abi table packet i64
    struct packet {
        uint64_t                issuer;
        std::string             pub_key;
        contract_asset          total_amount;
        uint32_t                number;
        vector<uint64_t>        subpackets;

        uint64_t primary_key() const { return issuer; }

        GRAPHENE_SERIALIZE(packet, (issuer)(pub_key)(total_amount)(number)(subpackets))
    };
    typedef graphene::multi_index<N(packet), packet> packet_index;

    struct account {
        uint64_t    account_id;
        uint64_t    amount;

        GRAPHENE_SERIALIZE(account, (account_id)(amount))
    };

    //@abi table record i64
    struct record {
        uint64_t packet_issuer;
        std::vector<account> accounts;

        uint64_t primary_key() const { return packet_issuer; }

        GRAPHENE_SERIALIZE(record, (packet_issuer)(accounts))
    };
    typedef graphene::multi_index<N(record), record> record_index;

    packet_index        packets;
    record_index        records;

};

GRAPHENE_ABI(redpacket, (create)(open)(close))
