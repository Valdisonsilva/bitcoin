// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <common/run_command.h>
#include <util/string.h>
#include <util/time.h>

#include <tinyformat.h>
#include <univalue.h>

#include <cassert>

#ifdef ENABLE_EXTERNAL_SIGNER
#include <util/subprocess.hpp>
#endif // ENABLE_EXTERNAL_SIGNER

UniValue RunCommandParseJSON(const std::vector<std::string>& str_command, const std::string& str_std_in)
{
#ifdef ENABLE_EXTERNAL_SIGNER
    namespace sp = subprocess;

    UniValue result_json;
    std::istringstream stdout_stream;
    std::istringstream stderr_stream;

    if (str_command.empty()) return UniValue::VNULL;

    std::cerr << "============================= str_command is " << str_command[0] << "\n";

    auto c = sp::Popen(str_command, sp::input{sp::PIPE}, sp::output{sp::PIPE}, sp::error{sp::PIPE});
    if (!str_std_in.empty()) {
        c.send(str_std_in);
    }
    auto [out_res, err_res] = c.communicate();
    stdout_stream.str(std::string{out_res.buf.begin(), out_res.buf.end()});
    stderr_stream.str(std::string{err_res.buf.begin(), err_res.buf.end()});

    std::string result;
    std::string error;
    std::getline(stdout_stream, result);
    std::getline(stderr_stream, error);

    std::cerr << "============================= result is " << result << "\n";
    std::cerr << "============================= error is " << error << "\n";

    std::cerr << "============================= c.poll() is " << c.poll() << "\n";

#ifdef WIN32
    // int poll_result;
    // while (true) {
    //     poll_result = c.poll();
    //     if (poll_result != -1) break;
    //     UninterruptibleSleep(100ms);
    // }
    // // assert(c.poll() != -1);
#endif
    // c.wait();
    const int n_error = c.retcode();

    std::cerr << "============================= n_error is " << n_error << "\n\n\n\n";

    if (n_error) throw std::runtime_error(strprintf("RunCommandParseJSON error: process(%s) returned %d: %s\n", Join(str_command, " "), n_error, error));
    if (!result_json.read(result)) throw std::runtime_error("Unable to parse JSON: " + result);

    return result_json;
#else
    throw std::runtime_error("Compiled without external signing support (required for external signing).");
#endif // ENABLE_EXTERNAL_SIGNER
}
