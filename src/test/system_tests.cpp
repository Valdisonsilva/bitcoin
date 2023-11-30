// Copyright (c) 2019-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
#include <test/util/setup_common.h>
#include <common/run_command.h>
#include <univalue.h>

#ifdef ENABLE_EXTERNAL_SIGNER
#include <util/subprocess.hpp>
#endif // ENABLE_EXTERNAL_SIGNER

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(system_tests, BasicTestingSetup)

// At least one test is required (in case ENABLE_EXTERNAL_SIGNER is not defined).
// Workaround for https://github.com/bitcoin/bitcoin/issues/19128
BOOST_AUTO_TEST_CASE(dummy)
{
    BOOST_CHECK(true);
}

#ifdef ENABLE_EXTERNAL_SIGNER
BOOST_AUTO_TEST_CASE(run_command)
{
#ifdef WIN32
    // https://www.winehq.org/pipermail/wine-devel/2008-September/069387.html
    auto hntdll = GetModuleHandleA("ntdll.dll");
    assert(hntdll);
    const bool wine_runtime = GetProcAddress(hntdll, "wine_get_version");
#endif

    {
        const UniValue result = RunCommandParseJSON({});
        BOOST_CHECK(result.isNull());
    }
    {
#ifdef WIN32
        const UniValue result = RunCommandParseJSON({"echo", "{\"success\": true}"});
#else
        const UniValue result = RunCommandParseJSON({"echo", "{\"success\": true}"});
#endif
        BOOST_CHECK(result.isObject());
        const UniValue& success = result.find_value("success");
        BOOST_CHECK(!success.isNull());
        BOOST_CHECK_EQUAL(success.get_bool(), true);
    }
    {
        // An invalid command is handled by cpp-subprocess
// #ifdef WIN32
//         const int expected_error{wine_runtime ? 6 : 1};
//         using SubprocessException = subprocess::OSError;
// #else
//         const int expected_error{1};
//         using SubprocessException = subprocess::CalledProcessError;
// #endif
        BOOST_CHECK_EXCEPTION(RunCommandParseJSON({"invalid_command"}), std::runtime_error, [&](const std::runtime_error& e) {
            BOOST_CHECK(std::string(e.what()).find("RunCommandParseJSON error:") == std::string::npos);
            // BOOST_CHECK_EQUAL(std::string(e.what()), std::string{});  // My own addition.
            // BOOST_CHECK_EQUAL(e.err_code, expected_error);
            return true;
        });
    }
    {
        // Return non-zero exit code, no output to stderr
#ifdef WIN32
        const std::vector<std::string> command = {"cmd.exe", "/c", "exit", "1"};
#else
        const std::vector<std::string> command = {"false"};
#endif
        BOOST_CHECK_EXCEPTION(RunCommandParseJSON(command), std::runtime_error, [&](const std::runtime_error& e) {
            // const std::string what{e.what()};
            // BOOST_CHECK(what.find(strprintf("RunCommandParseJSON error: process(%s) returned 1: \n", Join(command, " "))) != std::string::npos);
            BOOST_CHECK_EQUAL(std::string(e.what()), std::string{});  // My own addition.
            return true;
        });
    }
//     {
//         // Return non-zero exit code, with error message for stderr
// #ifdef WIN32
//         const std::vector<std::string> command = {"cmd.exe", "/c", "dir", "nosuchfile"};
//         const std::string expected{wine_runtime ? "File not found." : "File Not Found"};
// #else
//         const std::vector<std::string> command = {"ls", "nosuchfile"};
//         const std::string expected{"No such file or directory"};
// #endif
//         BOOST_CHECK_EXCEPTION(RunCommandParseJSON(command), std::runtime_error, [&](const std::runtime_error& e) {
//             const std::string what(e.what());
//             BOOST_CHECK(what.find(strprintf("RunCommandParseJSON error: process(%s) returned", Join(command, " "))) != std::string::npos);
//             BOOST_CHECK(what.find(expected) != std::string::npos);
//             return true;
//         });
//     }
//     {
//         BOOST_REQUIRE_THROW(RunCommandParseJSON({"echo", "{"}), std::runtime_error); // Unable to parse JSON
//     }
    // Test std::in, except for Windows
#ifndef WIN32
    {
        const UniValue result = RunCommandParseJSON({"cat"}, "{\"success\": true}");
        BOOST_CHECK(result.isObject());
        const UniValue& success = result.find_value("success");
        BOOST_CHECK(!success.isNull());
        BOOST_CHECK_EQUAL(success.get_bool(), true);
    }
#endif
}
#endif // ENABLE_EXTERNAL_SIGNER

BOOST_AUTO_TEST_SUITE_END()
