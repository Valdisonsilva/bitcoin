// Copyright (c) 2020-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>

#include <addrman.h>
#include <netbase.h>
#include <netgroup.h>
#include <random.h>
#include <util/check.h>
#include <util/time.h>

#include <optional>
#include <vector>

/* A "source" is a source address from which we have received a bunch of other addresses. */

static constexpr size_t NUM_SOURCES = 64;
static constexpr size_t NUM_ADDRESSES_PER_SOURCE = 256;

static NetGroupManager EMPTY_NETGROUPMAN{std::vector<bool>()};
static constexpr uint32_t ADDRMAN_CONSISTENCY_CHECK_RATIO{0};

static std::vector<CAddress> g_sources;
static std::vector<std::vector<CAddress>> g_addresses;

static void CreateAddresses()
{
    if (g_sources.size() > 0) { // already created
        return;
    }

    FastRandomContext rng(uint256(std::vector<unsigned char>(32, 123)));

    auto randAddr = [&rng]() {
        in6_addr addr;
        memcpy(&addr, rng.randbytes(sizeof(addr)).data(), sizeof(addr));

        uint16_t port;
        memcpy(&port, rng.randbytes(sizeof(port)).data(), sizeof(port));
        if (port == 0) {
            port = 1;
        }

        CAddress ret(CService(addr, port), NODE_NETWORK);

        ret.nTime = Now<NodeSeconds>();

        return ret;
    };

    for (size_t source_i = 0; source_i < NUM_SOURCES; ++source_i) {
        g_sources.emplace_back(randAddr());
        g_addresses.emplace_back();
        for (size_t addr_i = 0; addr_i < NUM_ADDRESSES_PER_SOURCE; ++addr_i) {
            g_addresses[source_i].emplace_back(randAddr());
        }
    }
}

static void AddAddressesToAddrMan(AddrMan& addrman)
{
    for (size_t source_i = 0; source_i < NUM_SOURCES; ++source_i) {
        addrman.Add(g_addresses[source_i], g_sources[source_i]);
    }
}

static void FillAddrMan(AddrMan& addrman)
{
    CreateAddresses();

    AddAddressesToAddrMan(addrman);
}

/* Benchmarks */

static void AddrManSelectByNetwork(benchmark::Bench& bench)
{
    AddrMan addrman{EMPTY_NETGROUPMAN, /*deterministic=*/false, ADDRMAN_CONSISTENCY_CHECK_RATIO};

    // add single I2P address to new table
    CService i2p_service;
    i2p_service.SetSpecial("udhdrtrcetjm5sxzskjyr5ztpeszydbh4dpl3pl4utgqqw2v4jna.b32.i2p");
    CAddress i2p_address(i2p_service, NODE_NONE);
    i2p_address.nTime = Now<NodeSeconds>();
    const CNetAddr source{LookupHost("252.2.2.2", false).value()};
    addrman.Add({i2p_address}, source);

    FillAddrMan(addrman);

    bench.run([&] {
        (void)addrman.Select(/*new_only=*/false, NET_I2P);
    });
}

BENCHMARK(AddrManSelectByNetwork, benchmark::PriorityLevel::HIGH);
