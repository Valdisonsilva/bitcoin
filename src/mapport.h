// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAPPORT_H
#define BITCOIN_MAPPORT_H

/** -natpmp default */
#ifdef USE_NATPMP
static constexpr bool DEFAULT_NATPMP = USE_NATPMP;
#else
static constexpr bool DEFAULT_NATPMP = false;
#endif

/** -upnp default */
#ifdef USE_UPNP
static constexpr bool DEFAULT_UPNP = USE_UPNP;
#else
static constexpr bool DEFAULT_UPNP = false;
#endif

enum MapPortProto : unsigned int {
    NONE = 0x00,
    NAT_PMP = 0x01,
    UPNP = 0x02,
};

void StartMapPort(bool use_natpmp, bool use_upnp);
void InterruptMapPort();
void StopMapPort();

#endif // BITCOIN_MAPPORT_H
