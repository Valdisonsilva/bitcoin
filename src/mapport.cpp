// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <mapport.h>

#include <clientversion.h>
#include <logging.h>
#include <net.h>
#include <netaddress.h>
#include <netbase.h>
#include <threadinterrupt.h>
#include <util/system.h>

#ifdef USE_UPNP
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
// The minimum supported miniUPnPc API version is set to 10. This keeps compatibility
// with Ubuntu 16.04 LTS and Debian 8 libminiupnpc-dev packages.
static_assert(MINIUPNPC_API_VERSION >= 10, "miniUPnPc API version >= 10 assumed");
#endif

#include <cassert>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

#ifdef USE_UPNP
static CThreadInterrupt g_mapport_interrupt;
static std::thread g_mapport_thread;
static MapPortProto g_mapport_current_proto = MapPortProto::NONE;
static unsigned int g_mapport_target_proto = g_mapport_current_proto;
static constexpr auto PORT_MAPPING_REANNOUNCE_PERIOD = std::chrono::minutes(20);
static constexpr auto PORT_MAPPING_RETRY_PERIOD = std::chrono::minutes(5);

static bool ThreadUpnp()
{
    bool ret = false;
    std::string port = strprintf("%u", GetListenPort());
    const char * multicastif = nullptr;
    const char * minissdpdpath = nullptr;
    struct UPNPDev * devlist = nullptr;
    char lanaddr[64];

    int error = 0;
#if MINIUPNPC_API_VERSION < 14
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
#else
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
#endif

    struct UPNPUrls urls;
    struct IGDdatas data;
    int r;

    r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
    if (r == 1)
    {
        if (fDiscover) {
            char externalIPAddress[40];
            r = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
            if (r != UPNPCOMMAND_SUCCESS) {
                LogPrintf("UPnP: GetExternalIPAddress() returned %d\n", r);
            } else {
                if (externalIPAddress[0]) {
                    CNetAddr resolved;
                    if (LookupHost(externalIPAddress, resolved, false)) {
                        LogPrintf("UPnP: ExternalIPAddress = %s\n", resolved.ToString());
                        AddLocal(resolved, LOCAL_MAPPED);
                    }
                } else {
                    LogPrintf("UPnP: GetExternalIPAddress failed.\n");
                }
            }
        }

        std::string strDesc = PACKAGE_NAME " " + FormatFullVersion();

        do {
            r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0, "0");

            if (r != UPNPCOMMAND_SUCCESS) {
                ret = false;
                LogPrintf("AddPortMapping(%s, %s, %s) failed with code %d (%s)\n", port, port, lanaddr, r, strupnperror(r));
                break;
            } else {
                ret = true;
                g_mapport_current_proto = MapPortProto::UPNP;
                LogPrintf("UPnP Port Mapping successful.\n");
            }
        } while (g_mapport_interrupt.sleep_for(PORT_MAPPING_REANNOUNCE_PERIOD));

        r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "TCP", 0);
        LogPrintf("UPNP_DeletePortMapping() returned: %d\n", r);
        freeUPNPDevlist(devlist); devlist = nullptr;
        FreeUPNPUrls(&urls);
    } else {
        LogPrintf("No valid UPnP IGDs found\n");
        freeUPNPDevlist(devlist); devlist = nullptr;
        if (r != 0)
            FreeUPNPUrls(&urls);
    }

    return ret;
}

static void ThreadMapPort()
{
    bool ok;
    do {
        ok = false;

        if (g_mapport_target_proto & MapPortProto::UPNP) {
            ok = ThreadUpnp();
            if (ok) continue;
        }

        if (g_mapport_target_proto == MapPortProto::NONE) {
            g_mapport_current_proto = MapPortProto::NONE;
            return;
        }

    } while (ok || g_mapport_interrupt.sleep_for(PORT_MAPPING_RETRY_PERIOD));
}

void StartMapPort(bool use_upnp)
{
    if (use_upnp) {
        g_mapport_target_proto |= MapPortProto::UPNP;
    } else {
        g_mapport_target_proto &= ~MapPortProto::UPNP;
    }

    if (g_mapport_current_proto == MapPortProto::NONE) {
        if (g_mapport_target_proto == MapPortProto::NONE) {
            return;
        }
    } else if (g_mapport_target_proto & g_mapport_current_proto) {
        return;
    }

    if (g_mapport_target_proto == MapPortProto::NONE) {
        InterruptMapPort();
        StopMapPort();
        return;
    }

    if (g_mapport_thread.joinable()) {
        g_mapport_interrupt();
    } else {
        assert(!g_mapport_interrupt);
        g_mapport_thread = std::thread((std::bind(&TraceThread<void (*)()>, "mapport", &ThreadMapPort)));
    }
}

void InterruptMapPort()
{
    g_mapport_target_proto = MapPortProto::NONE;
    if (g_mapport_thread.joinable()) {
        g_mapport_interrupt();
    }
}

void StopMapPort()
{
    if (g_mapport_thread.joinable()) {
        g_mapport_thread.join();
        g_mapport_interrupt.reset();
    }
}

#else
void StartMapPort(bool use_upnp)
{
    // Intentionally left blank.
}
void InterruptMapPort()
{
    // Intentionally left blank.
}
void StopMapPort()
{
    // Intentionally left blank.
}
#endif
