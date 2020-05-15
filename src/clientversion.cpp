// Copyright (c) 2012-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>

#include <tinyformat.h>


/**
 * Name of client reported in the 'version' message. Report the same name
 * for both bitcoind and bitcoin-qt, to make it harder for attackers to
 * target servers or GUI users specifically.
 */
const std::string CLIENT_NAME("Satoshi");

//! git will put "#define GIT_COMMIT_ID ..." on the next line inside archives. $Format:%n#define GIT_COMMIT_ID "%H"$

#define BITCOIN_CLIENT_VERSION "v" STRINGIZE(CLIENT_VERSION_MAJOR) "." STRINGIZE(CLIENT_VERSION_MINOR) "." STRINGIZE(CLIENT_VERSION_REVISION)

#if CLIENT_VERSION_BUILD > 0
    #define BITCOIN_CLIENT_BUILD "." STRINGIZE(CLIENT_VERSION_BUILD)
#else
    #define BITCOIN_CLIENT_BUILD ""
#endif

#if CLIENT_VERSION_RC > 0
    #define BITCOIN_CLIENT_RC "rc" STRINGIZE(CLIENT_VERSION_RC)
#else
    #define BITCOIN_CLIENT_RC ""
#endif

#ifdef GIT_COMMIT_ID
    #define BUILD_SUFFIX "-g" GIT_COMMIT_ID
#else
    #define BUILD_SUFFIX ""
#endif

const std::string CLIENT_BUILD(BITCOIN_CLIENT_VERSION BITCOIN_CLIENT_BUILD BITCOIN_CLIENT_RC BUILD_SUFFIX);

static std::string FormatVersion(int nVersion)
{
    if (nVersion % 100 == 0)
        return strprintf("%d.%d.%d", nVersion / 1000000, (nVersion / 10000) % 100, (nVersion / 100) % 100);
    else
        return strprintf("%d.%d.%d.%d", nVersion / 1000000, (nVersion / 10000) % 100, (nVersion / 100) % 100, nVersion % 100);
}

std::string FormatFullVersion()
{
    return CLIENT_BUILD;
}

/**
 * Format the subversion field according to BIP 14 spec (https://github.com/bitcoin/bips/blob/master/bip-0014.mediawiki)
 */
std::string FormatSubVersion(const std::string& name, int nClientVersion, const std::vector<std::string>& comments)
{
    std::ostringstream ss;
    ss << "/";
    ss << name << ":" << FormatVersion(nClientVersion);
    if (!comments.empty())
    {
        std::vector<std::string>::const_iterator it(comments.begin());
        ss << "(" << *it;
        for(++it; it != comments.end(); ++it)
            ss << "; " << *it;
        ss << ")";
    }
    ss << "/";
    return ss.str();
}
