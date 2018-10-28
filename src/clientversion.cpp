// Copyright (c) 2012-2014 The Bitcoin developers
// Copyright (c) 2018 Oxid developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientversion.h"

#include "tinyformat.h"

#include <string>

/**
 * Name of client reported in the 'version' message. Report the same name
 * for both oxidd and oxid-qt, to make it harder for attackers to
 * target servers or GUI users specifically.
 */
const std::string CLIENT_NAME("Oxid");

/**
 * Client version number
 */
#define CLIENT_VERSION_SUFFIX ""

#ifdef HAVE_BUILD_INFO
#include "build.h"
#endif

#define BUILD_DESC_FROM_UNKNOWN(maj, min, rev) \
    "v" DO_STRINGIZE(maj) "." DO_STRINGIZE(min) "." DO_STRINGIZE(rev)

#ifndef BUILD_DESC
#define BUILD_DESC BUILD_DESC_FROM_UNKNOWN(CLIENT_VERSION_MAJOR, CLIENT_VERSION_MINOR, CLIENT_VERSION_REVISION)
#endif

#ifndef BUILD_DATE
#define BUILD_DATE __DATE__ ", " __TIME__
#endif

const std::string CLIENT_BUILD(BUILD_DESC CLIENT_VERSION_SUFFIX);
const std::string CLIENT_DATE(BUILD_DATE);

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
    if (!comments.empty()) {
        std::vector<std::string>::const_iterator it(comments.begin());
        ss << "(" << *it;
        for (++it; it != comments.end(); ++it)
            ss << "; " << *it;
        ss << ")";
    }
    ss << "/";
    return ss.str();
}
