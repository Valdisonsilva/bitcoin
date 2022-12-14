// Copyright (c) 2019-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/settings.h>

#include <chainparamsbase.h>
#include <fs.h>
#include <logging.h>
#include <tinyformat.h>
#include <univalue.h>
#include <util/check.h>
#include <util/strencodings.h>
#include <util/system.h>

#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace util {
namespace {

enum class Source {
   FORCED,
   COMMAND_LINE,
   RW_SETTINGS,
   CONFIG_FILE_NETWORK_SECTION,
   CONFIG_FILE_DEFAULT_SECTION
};

//! Merge settings from multiple sources in precedence order:
//! Forced config > command line > read-write settings file > config file network-specific section > config file default section
//!
//! This function is provided with a callback function fn that contains
//! specific logic for how to merge the sources.
template <typename Fn>
static void MergeSettings(const Settings& settings, const std::string& section, const std::string& name, Fn&& fn)
{
    // Merge in the forced settings
    if (auto* value = FindKey(settings.forced_settings, name)) {
        fn(SettingsSpan(*value), Source::FORCED);
    }
    // Merge in the command-line options
    if (auto* values = FindKey(settings.command_line_options, name)) {
        fn(SettingsSpan(*values), Source::COMMAND_LINE);
    }
    // Merge in the read-write settings
    if (const SettingsValue* value = FindKey(settings.rw_settings, name)) {
        fn(SettingsSpan(*value), Source::RW_SETTINGS);
    }
    // Merge in the network-specific section of the config file
    if (!section.empty()) {
        if (auto* map = FindKey(settings.ro_config, section)) {
            if (auto* values = FindKey(*map, name)) {
                fn(SettingsSpan(*values), Source::CONFIG_FILE_NETWORK_SECTION);
            }
        }
    }
    // Merge in the default section of the config file
    if (auto* map = FindKey(settings.ro_config, "")) {
        if (auto* values = FindKey(*map, name)) {
            fn(SettingsSpan(*values), Source::CONFIG_FILE_DEFAULT_SECTION);
        }
    }
}
} // namespace

bool ReadSettings(const fs::path& path, std::map<std::string, SettingsValue>& values, std::vector<std::string>& errors)
{
    values.clear();
    errors.clear();

    // Ok for file to not exist
    if (!fs::exists(path)) return true;

    std::ifstream file;
    file.open(path);
    if (!file.is_open()) {
      errors.emplace_back(strprintf("%s. Please check permissions.", fs::PathToString(path)));
      return false;
    }

    SettingsValue in;
    if (!in.read(std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()})) {
        errors.emplace_back(strprintf("Unable to parse settings file %s", fs::PathToString(path)));
        return false;
    }

    if (file.fail()) {
        errors.emplace_back(strprintf("Failed reading settings file %s", fs::PathToString(path)));
        return false;
    }
    file.close(); // Done with file descriptor. Release while copying data.

    if (!in.isObject()) {
        errors.emplace_back(strprintf("Found non-object value %s in settings file %s", in.write(), fs::PathToString(path)));
        return false;
    }

    const std::vector<std::string>& in_keys = in.getKeys();
    const std::vector<SettingsValue>& in_values = in.getValues();
    for (size_t i = 0; i < in_keys.size(); ++i) {
        auto inserted = values.emplace(in_keys[i], in_values[i]);
        if (!inserted.second) {
            errors.emplace_back(strprintf("Found duplicate key %s in settings file %s", in_keys[i], fs::PathToString(path)));
        }
    }
    return errors.empty();
}

bool WriteSettings(const fs::path& path,
    const std::map<std::string, SettingsValue>& values,
    std::vector<std::string>& errors)
{
    SettingsValue out(SettingsValue::VOBJ);
    for (const auto& value : values) {
        out.__pushKV(value.first, value.second);
    }
    std::ofstream file;
    file.open(path);
    if (file.fail()) {
        errors.emplace_back(strprintf("Error: Unable to open settings file %s for writing", fs::PathToString(path)));
        return false;
    }
    file << out.write(/* prettyIndent= */ 4, /* indentLevel= */ 1) << std::endl;
    file.close();
    return true;
}

SettingsValue GetSetting(const Settings& settings,
    const std::string& section,
    const std::string& name,
    bool ignore_default_section_config,
    bool ignore_nonpersistent,
    bool get_chain_name)
{
    SettingsValue result;
    bool done = false; // Done merging any more settings sources.
    MergeSettings(settings, section, name, [&](SettingsSpan span, Source source) {
        // Weird behavior preserved for backwards compatibility: Apply negated
        // setting even if non-negated setting would be ignored. A negated
        // value in the default section is applied to network specific options,
        // even though normal non-negated values there would be ignored.
        const bool never_ignore_negated_setting = span.last_negated();

        // Weird behavior preserved for backwards compatibility: Take first
        // assigned value instead of last. In general, later settings take
        // precedence over early settings, but for backwards compatibility in
        // the config file the precedence is reversed for all settings except
        // chain name settings.
        const bool reverse_precedence =
            (source == Source::CONFIG_FILE_NETWORK_SECTION || source == Source::CONFIG_FILE_DEFAULT_SECTION) &&
            !get_chain_name;

        // Weird behavior preserved for backwards compatibility: Negated
        // -regtest and -testnet arguments which you would expect to override
        // values set in the configuration file are currently accepted but
        // silently ignored. It would be better to apply these just like other
        // negated values, or at least warn they are ignored.
        const bool skip_negated_command_line = get_chain_name;

        if (done) return;

        // Ignore settings in default config section if requested.
        if (ignore_default_section_config && source == Source::CONFIG_FILE_DEFAULT_SECTION &&
            !never_ignore_negated_setting) {
            return;
        }

        // Ignore nonpersistent settings if requested.
        if (ignore_nonpersistent && (source == Source::COMMAND_LINE || source == Source::FORCED)) return;

        // Skip negated command line settings.
        if (skip_negated_command_line && span.last_negated()) return;

        if (!span.empty()) {
            result = reverse_precedence ? span.begin()[0] : span.end()[-1];
            done = true;
        } else if (span.last_negated()) {
            result = false;
            done = true;
        }
    });
    return result;
}

std::vector<SettingsValue> GetSettingsList(const Settings& settings,
    const std::string& section,
    const std::string& name,
    bool ignore_default_section_config)
{
    std::vector<SettingsValue> result;
    bool done = false; // Done merging any more settings sources.
    bool prev_negated_empty = false;
    MergeSettings(settings, section, name, [&](SettingsSpan span, Source source) {
        // Weird behavior preserved for backwards compatibility: Apply config
        // file settings even if negated on command line. Negating a setting on
        // command line will ignore earlier settings on the command line and
        // ignore settings in the config file, unless the negated command line
        // value is followed by non-negated value, in which case config file
        // settings will be brought back from the dead (but earlier command
        // line settings will still be ignored).
        const bool add_zombie_config_values =
            (source == Source::CONFIG_FILE_NETWORK_SECTION || source == Source::CONFIG_FILE_DEFAULT_SECTION) &&
            !prev_negated_empty;

        // Ignore settings in default config section if requested.
        if (ignore_default_section_config && source == Source::CONFIG_FILE_DEFAULT_SECTION) return;

        // Add new settings to the result if isn't already complete, or if the
        // values are zombies.
        if (!done || add_zombie_config_values) {
            for (const auto& value : span) {
                if (value.isArray()) {
                    result.insert(result.end(), value.getValues().begin(), value.getValues().end());
                } else {
                    result.push_back(value);
                }
            }
        }

        // If a setting was negated, or if a setting was forced, set
        // done to true to ignore any later lower priority settings.
        done |= span.negated() > 0 || source == Source::FORCED;

        // Update the negated and empty state used for the zombie values check.
        prev_negated_empty |= span.last_negated() && result.empty();
    });
    return result;
}

bool OnlyHasDefaultSectionSetting(const Settings& settings, const std::string& section, const std::string& name)
{
    bool has_default_section_setting = false;
    bool has_other_setting = false;
    MergeSettings(settings, section, name, [&](SettingsSpan span, Source source) {
        if (span.empty()) return;
        else if (source == Source::CONFIG_FILE_DEFAULT_SECTION) has_default_section_setting = true;
        else has_other_setting = true;
    });
    // If a value is set in the default section and not explicitly overwritten by the
    // user on the command line or in a different section, then we want to enable
    // warnings about the value being ignored.
    return has_default_section_setting && !has_other_setting;
}

SettingsSpan::SettingsSpan(const std::vector<SettingsValue>& vec) noexcept : SettingsSpan(vec.data(), vec.size()) {}
const SettingsValue* SettingsSpan::begin() const { return data + negated(); }
const SettingsValue* SettingsSpan::end() const { return data + size; }
bool SettingsSpan::empty() const { return size == 0 || last_negated(); }
bool SettingsSpan::last_negated() const { return size > 0 && data[size - 1].isFalse(); }
size_t SettingsSpan::negated() const
{
    for (size_t i = size; i > 0; --i) {
        if (data[i - 1].isFalse()) return i; // Return number of negated values (position of last false value)
    }
    return 0;
}

} // namespace util

// Define default constructor and destructor that are not inline, so code instantiating this class doesn't need to
// #include class definitions for all members.
// For example, m_settings has an internal dependency on univalue.
ArgsManager::ArgsManager() = default;
ArgsManager::~ArgsManager() = default;

struct KeyInfo {
    std::string name;
    std::string section;
    bool negated{false};
};

static std::string SettingName(const std::string& arg)
{
    return arg.size() > 0 && arg[0] == '-' ? arg.substr(1) : arg;
}

bool IsConfSupported(KeyInfo& key, std::string& error) {
    if (key.name == "conf") {
        error = "conf cannot be set in the configuration file; use includeconf= if you want to include additional config files";
        return false;
    }
    if (key.name == "reindex") {
        // reindex can be set in a config file but it is strongly discouraged as this will cause the node to reindex on
        // every restart. Allow the config but throw a warning
        LogPrintf("Warning: reindex=1 is set in the configuration file, which will significantly slow down startup. Consider removing or commenting out this option for better performance, unless there is currently a condition which makes rebuilding the indexes necessary\n");
        return true;
    }
    return true;
}

/**
 * Parse "name", "section.name", "noname", "section.noname" settings keys.
 *
 * @note Where an option was negated can be later checked using the
 * IsArgNegated() method. One use case for this is to have a way to disable
 * options that are not normally boolean (e.g. using -nodebuglogfile to request
 * that debug log output is not sent to any file at all).
 */
KeyInfo InterpretKey(std::string key)
{
    KeyInfo result;
    // Split section name from key name for keys like "testnet.foo" or "regtest.bar"
    size_t option_index = key.find('.');
    if (option_index != std::string::npos) {
        result.section = key.substr(0, option_index);
        key.erase(0, option_index + 1);
    }
    if (key.substr(0, 2) == "no") {
        key.erase(0, 2);
        result.negated = true;
    }
    result.name = key;
    return result;
}

static bool GetConfigOptions(std::istream& stream, const std::string& filepath, std::string& error, std::vector<std::pair<std::string, std::string>>& options, std::list<SectionInfo>& sections)
{
    std::string str, prefix;
    std::string::size_type pos;
    int linenr = 1;
    while (std::getline(stream, str)) {
        bool used_hash = false;
        if ((pos = str.find('#')) != std::string::npos) {
            str = str.substr(0, pos);
            used_hash = true;
        }
        const static std::string pattern = " \t\r\n";
        str = TrimString(str, pattern);
        if (!str.empty()) {
            if (*str.begin() == '[' && *str.rbegin() == ']') {
                const std::string section = str.substr(1, str.size() - 2);
                sections.emplace_back(SectionInfo{section, filepath, linenr});
                prefix = section + '.';
            } else if (*str.begin() == '-') {
                error = strprintf("parse error on line %i: %s, options in configuration file must be specified without leading -", linenr, str);
                return false;
            } else if ((pos = str.find('=')) != std::string::npos) {
                std::string name = prefix + TrimString(std::string_view{str}.substr(0, pos), pattern);
                std::string_view value = TrimStringView(std::string_view{str}.substr(pos + 1), pattern);
                if (used_hash && name.find("rpcpassword") != std::string::npos) {
                    error = strprintf("parse error on line %i, using # in rpcpassword can be ambiguous and should be avoided", linenr);
                    return false;
                }
                options.emplace_back(name, value);
                if ((pos = name.rfind('.')) != std::string::npos && prefix.length() <= pos) {
                    sections.emplace_back(SectionInfo{name.substr(0, pos), filepath, linenr});
                }
            } else {
                error = strprintf("parse error on line %i: %s", linenr, str);
                if (str.size() >= 2 && str.substr(0, 2) == "no") {
                    error += strprintf(", if you intended to specify a negated option, use %s=1 instead", str);
                }
                return false;
            }
        }
        ++linenr;
    }
    return true;
}

/**
 * Interpret a string argument as a boolean.
 *
 * The definition of LocaleIndependentAtoi<int>() requires that non-numeric string values
 * like "foo", return 0. This means that if a user unintentionally supplies a
 * non-integer argument here, the return value is always false. This means that
 * -foo=false does what the user probably expects, but -foo=true is well defined
 * but does not do what they probably expected.
 *
 * The return value of LocaleIndependentAtoi<int>(...) is zero when given input not
 * representable as an int.
 *
 * For a more extensive discussion of this topic (and a wide range of opinions
 * on the Right Way to change this code), see PR12713.
 */
static bool InterpretBool(const std::string& strValue)
{
    if (strValue.empty())
        return true;
    return (LocaleIndependentAtoi<int>(strValue) != 0);
}

/**
 * Interpret settings value based on registered flags.
 *
 * @param[in]   key      key information to know if key was negated
 * @param[in]   value    string value of setting to be parsed
 * @param[in]   flags    ArgsManager registered argument flags
 * @param[out]  error    Error description if settings value is not valid
 *
 * @return parsed settings value if it is valid, otherwise nullopt accompanied
 * by a descriptive error string
 */
static std::optional<util::SettingsValue> InterpretValue(const KeyInfo& key, const std::string* value,
                                                         unsigned int flags, std::string& error)
{
    // Return negated settings as false values.
    if (key.negated) {
        if (flags & ArgsManager::DISALLOW_NEGATION) {
            error = strprintf("Negating of -%s is meaningless and therefore forbidden", key.name);
            return std::nullopt;
        }
        // Double negatives like -nofoo=0 are supported (but discouraged)
        if (value && !InterpretBool(*value)) {
            LogPrintf("Warning: parsed potentially confusing double-negative -%s=%s\n", key.name, *value);
            return true;
        }
        return false;
    }
    if (!value && (flags & ArgsManager::DISALLOW_ELISION)) {
        error = strprintf("Can not set -%s with no value. Please specify value with -%s=value.", key.name, key.name);
        return std::nullopt;
    }
    return value ? *value : "";
}

const std::set<std::string> ArgsManager::GetUnsuitableSectionOnlyArgs() const
{
    std::set<std::string> unsuitables;

    LOCK(cs_args);

    // if there's no section selected, don't worry
    if (m_network.empty()) return std::set<std::string> {};

    // if it's okay to use the default section for this network, don't worry
    if (m_network == CBaseChainParams::MAIN) return std::set<std::string> {};

    for (const auto& arg : m_network_only_args) {
        if (OnlyHasDefaultSectionSetting(m_settings, m_network, SettingName(arg))) {
            unsuitables.insert(arg);
        }
    }
    return unsuitables;
}

const std::list<SectionInfo> ArgsManager::GetUnrecognizedSections() const
{
    // Section names to be recognized in the config file.
    static const std::set<std::string> available_sections{
        CBaseChainParams::REGTEST,
        CBaseChainParams::SIGNET,
        CBaseChainParams::TESTNET,
        CBaseChainParams::MAIN
    };

    LOCK(cs_args);
    std::list<SectionInfo> unrecognized = m_config_sections;
    unrecognized.remove_if([](const SectionInfo& appeared){ return available_sections.find(appeared.m_name) != available_sections.end(); });
    return unrecognized;
}

void ArgsManager::SelectConfigNetwork(const std::string& network)
{
    LOCK(cs_args);
    m_network = network;
}

bool ArgsManager::ParseParameters(int argc, const char* const argv[], std::string& error)
{
    LOCK(cs_args);
    m_settings.command_line_options.clear();

    for (int i = 1; i < argc; i++) {
        std::string key(argv[i]);

#ifdef MAC_OSX
        // At the first time when a user gets the "App downloaded from the
        // internet" warning, and clicks the Open button, macOS passes
        // a unique process serial number (PSN) as -psn_... command-line
        // argument, which we filter out.
        if (key.substr(0, 5) == "-psn_") continue;
#endif

        if (key == "-") break; //bitcoin-tx using stdin
        std::optional<std::string> val;
        size_t is_index = key.find('=');
        if (is_index != std::string::npos) {
            val = key.substr(is_index + 1);
            key.erase(is_index);
        }
#ifdef WIN32
        key = ToLower(key);
        if (key[0] == '/')
            key[0] = '-';
#endif

        if (key[0] != '-') {
            if (!m_accept_any_command && m_command.empty()) {
                // The first non-dash arg is a registered command
                std::optional<unsigned int> flags = GetArgFlags(key);
                if (!flags || !(*flags & ArgsManager::COMMAND)) {
                    error = strprintf("Invalid command '%s'", argv[i]);
                    return false;
                }
            }
            m_command.push_back(key);
            while (++i < argc) {
                // The remaining args are command args
                m_command.push_back(argv[i]);
            }
            break;
        }

        // Transform --foo to -foo
        if (key.length() > 1 && key[1] == '-')
            key.erase(0, 1);

        // Transform -foo to foo
        key.erase(0, 1);
        KeyInfo keyinfo = InterpretKey(key);
        std::optional<unsigned int> flags = GetArgFlags('-' + keyinfo.name);

        // Unknown command line options and command line options with dot
        // characters (which are returned from InterpretKey with nonempty
        // section strings) are not valid.
        if (!flags || !keyinfo.section.empty()) {
            error = strprintf("Invalid parameter %s", argv[i]);
            return false;
        }

        std::optional<util::SettingsValue> value = InterpretValue(keyinfo, val ? &*val : nullptr, *flags, error);
        if (!value) return false;

        m_settings.command_line_options[keyinfo.name].push_back(*value);
    }

    // we do not allow -includeconf from command line, only -noincludeconf
    if (auto* includes = util::FindKey(m_settings.command_line_options, "includeconf")) {
        const util::SettingsSpan values{*includes};
        // Range may be empty if -noincludeconf was passed
        if (!values.empty()) {
            error = "-includeconf cannot be used from commandline; -includeconf=" + values.begin()->write();
            return false; // pick first value as example
        }
    }
    return true;
}

std::optional<unsigned int> ArgsManager::GetArgFlags(const std::string& name) const
{
    LOCK(cs_args);
    for (const auto& arg_map : m_available_args) {
        const auto search = arg_map.second.find(name);
        if (search != arg_map.second.end()) {
            return search->second.m_flags;
        }
    }
    return std::nullopt;
}

fs::path ArgsManager::GetPathArg(std::string arg, const fs::path& default_value) const
{
    if (IsArgNegated(arg)) return fs::path{};
    std::string path_str = GetArg(arg, "");
    if (path_str.empty()) return default_value;
    fs::path result = fs::PathFromString(path_str).lexically_normal();
    // Remove trailing slash, if present.
    return result.has_filename() ? result : result.parent_path();
}

const fs::path& ArgsManager::GetBlocksDirPath() const
{
    LOCK(cs_args);
    fs::path& path = m_cached_blocks_path;

    // Cache the path to avoid calling fs::create_directories on every call of
    // this function
    if (!path.empty()) return path;

    if (IsArgSet("-blocksdir")) {
        path = fs::absolute(GetPathArg("-blocksdir"));
        if (!fs::is_directory(path)) {
            path = "";
            return path;
        }
    } else {
        path = GetDataDirBase();
    }

    path /= fs::PathFromString(BaseParams().DataDir());
    path /= "blocks";
    fs::create_directories(path);
    return path;
}

const fs::path& ArgsManager::GetDataDir(bool net_specific) const
{
    LOCK(cs_args);
    fs::path& path = net_specific ? m_cached_network_datadir_path : m_cached_datadir_path;

    // Cache the path to avoid calling fs::create_directories on every call of
    // this function
    if (!path.empty()) return path;

    const fs::path datadir{GetPathArg("-datadir")};
    if (!datadir.empty()) {
        path = fs::absolute(datadir);
        if (!fs::is_directory(path)) {
            path = "";
            return path;
        }
    } else {
        path = GetDefaultDataDir();
    }

    if (!fs::exists(path)) {
        fs::create_directories(path / "wallets");
    }

    if (net_specific && !BaseParams().DataDir().empty()) {
        path /= fs::PathFromString(BaseParams().DataDir());
        if (!fs::exists(path)) {
            fs::create_directories(path / "wallets");
        }
    }

    return path;
}

void ArgsManager::ClearPathCache()
{
    LOCK(cs_args);

    m_cached_datadir_path = fs::path();
    m_cached_network_datadir_path = fs::path();
    m_cached_blocks_path = fs::path();
}

std::optional<const ArgsManager::Command> ArgsManager::GetCommand() const
{
    Command ret;
    LOCK(cs_args);
    auto it = m_command.begin();
    if (it == m_command.end()) {
        // No command was passed
        return std::nullopt;
    }
    if (!m_accept_any_command) {
        // The registered command
        ret.command = *(it++);
    }
    while (it != m_command.end()) {
        // The unregistered command and args (if any)
        ret.args.push_back(*(it++));
    }
    return ret;
}

std::vector<std::string> ArgsManager::GetArgs(const std::string& strArg) const
{
    std::vector<std::string> result;
    for (const util::SettingsValue& value : GetSettingsList(strArg)) {
        result.push_back(value.isFalse() ? "0" : value.isTrue() ? "1" : value.get_str());
    }
    return result;
}

bool ArgsManager::IsArgSet(const std::string& strArg) const
{
    return !GetSetting(strArg).isNull();
}

bool ArgsManager::InitSettings(std::string& error)
{
    if (!GetSettingsPath()) {
        return true; // Do nothing if settings file disabled.
    }

    std::vector<std::string> errors;
    if (!ReadSettingsFile(&errors)) {
        error = strprintf("Failed loading settings file:\n%s\n", MakeUnorderedList(errors));
        return false;
    }
    if (!WriteSettingsFile(&errors)) {
        error = strprintf("Failed saving settings file:\n%s\n", MakeUnorderedList(errors));
        return false;
    }
    return true;
}

bool ArgsManager::GetSettingsPath(fs::path* filepath, bool temp, bool backup) const
{
    fs::path settings = GetPathArg("-settings", BITCOIN_SETTINGS_FILENAME);
    if (settings.empty()) {
        return false;
    }
    if (backup) {
        settings += ".bak";
    }
    if (filepath) {
        *filepath = fsbridge::AbsPathJoin(GetDataDirNet(), temp ? settings + ".tmp" : settings);
    }
    return true;
}

static void SaveErrors(const std::vector<std::string> errors, std::vector<std::string>* error_out)
{
    for (const auto& error : errors) {
        if (error_out) {
            error_out->emplace_back(error);
        } else {
            LogPrintf("%s\n", error);
        }
    }
}

bool ArgsManager::ReadSettingsFile(std::vector<std::string>* errors)
{
    fs::path path;
    if (!GetSettingsPath(&path, /* temp= */ false)) {
        return true; // Do nothing if settings file disabled.
    }

    LOCK(cs_args);
    m_settings.rw_settings.clear();
    std::vector<std::string> read_errors;
    if (!util::ReadSettings(path, m_settings.rw_settings, read_errors)) {
        SaveErrors(read_errors, errors);
        return false;
    }
    for (const auto& setting : m_settings.rw_settings) {
        KeyInfo key = InterpretKey(setting.first); // Split setting key into section and argname
        if (!GetArgFlags('-' + key.name)) {
            LogPrintf("Ignoring unknown rw_settings value %s\n", setting.first);
        }
    }
    return true;
}

bool ArgsManager::WriteSettingsFile(std::vector<std::string>* errors, bool backup) const
{
    fs::path path, path_tmp;
    if (!GetSettingsPath(&path, /*temp=*/false, backup) || !GetSettingsPath(&path_tmp, /*temp=*/true, backup)) {
        throw std::logic_error("Attempt to write settings file when dynamic settings are disabled.");
    }

    LOCK(cs_args);
    std::vector<std::string> write_errors;
    if (!util::WriteSettings(path_tmp, m_settings.rw_settings, write_errors)) {
        SaveErrors(write_errors, errors);
        return false;
    }
    if (!RenameOver(path_tmp, path)) {
        SaveErrors({strprintf("Failed renaming settings file %s to %s\n", fs::PathToString(path_tmp), fs::PathToString(path))}, errors);
        return false;
    }
    return true;
}

util::SettingsValue ArgsManager::GetPersistentSetting(const std::string& name) const
{
    LOCK(cs_args);
    return util::GetSetting(m_settings, m_network, name, !UseDefaultSection("-" + name),
        /*ignore_nonpersistent=*/true, /*get_chain_name=*/false);
}

bool ArgsManager::IsArgNegated(const std::string& strArg) const
{
    return GetSetting(strArg).isFalse();
}

std::string ArgsManager::GetArg(const std::string& strArg, const std::string& strDefault) const
{
    return GetArg(strArg).value_or(strDefault);
}

std::optional<std::string> ArgsManager::GetArg(const std::string& strArg) const
{
    const util::SettingsValue value = GetSetting(strArg);
    return SettingToString(value);
}

bool ArgsManager::GetBoolArg(const std::string& strArg, bool fDefault) const
{
    return GetBoolArg(strArg).value_or(fDefault);
}

std::optional<bool> ArgsManager::GetBoolArg(const std::string& strArg) const
{
    const util::SettingsValue value = GetSetting(strArg);
    return SettingToBool(value);
}

bool ArgsManager::SoftSetArg(const std::string& strArg, const std::string& strValue)
{
    LOCK(cs_args);
    if (IsArgSet(strArg)) return false;
    ForceSetArg(strArg, strValue);
    return true;
}

bool ArgsManager::SoftSetBoolArg(const std::string& strArg, bool fValue)
{
    if (fValue)
        return SoftSetArg(strArg, std::string("1"));
    else
        return SoftSetArg(strArg, std::string("0"));
}

void ArgsManager::ForceSetArg(const std::string& strArg, const std::string& strValue)
{
    LOCK(cs_args);
    m_settings.forced_settings[SettingName(strArg)] = strValue;
}

void ArgsManager::AddCommand(const std::string& cmd, const std::string& help)
{
    Assert(cmd.find('=') == std::string::npos);
    Assert(cmd.at(0) != '-');

    LOCK(cs_args);
    m_accept_any_command = false; // latch to false
    std::map<std::string, Arg>& arg_map = m_available_args[OptionsCategory::COMMANDS];
    auto ret = arg_map.emplace(cmd, Arg{"", help, ArgsManager::COMMAND});
    Assert(ret.second); // Fail on duplicate commands
}

void ArgsManager::AddArg(const std::string& name, const std::string& help, unsigned int flags, const OptionsCategory& cat)
{
    Assert((flags & ArgsManager::COMMAND) == 0); // use AddCommand

    // Split arg name from its help param
    size_t eq_index = name.find('=');
    if (eq_index == std::string::npos) {
        eq_index = name.size();
    }
    std::string arg_name = name.substr(0, eq_index);

    LOCK(cs_args);
    std::map<std::string, Arg>& arg_map = m_available_args[cat];
    auto ret = arg_map.emplace(arg_name, Arg{name.substr(eq_index, name.size() - eq_index), help, flags});
    assert(ret.second); // Make sure an insertion actually happened

    if (flags & ArgsManager::NETWORK_ONLY) {
        m_network_only_args.emplace(arg_name);
    }
}

void ArgsManager::AddHiddenArgs(const std::vector<std::string>& names)
{
    for (const std::string& name : names) {
        AddArg(name, "", ArgsManager::ALLOW_ANY, OptionsCategory::HIDDEN);
    }
}

std::string ArgsManager::GetHelpMessage() const
{
    const bool show_debug = GetBoolArg("-help-debug", false);

    std::string usage;
    LOCK(cs_args);
    for (const auto& arg_map : m_available_args) {
        switch(arg_map.first) {
            case OptionsCategory::OPTIONS:
                usage += HelpMessageGroup("Options:");
                break;
            case OptionsCategory::CONNECTION:
                usage += HelpMessageGroup("Connection options:");
                break;
            case OptionsCategory::ZMQ:
                usage += HelpMessageGroup("ZeroMQ notification options:");
                break;
            case OptionsCategory::DEBUG_TEST:
                usage += HelpMessageGroup("Debugging/Testing options:");
                break;
            case OptionsCategory::NODE_RELAY:
                usage += HelpMessageGroup("Node relay options:");
                break;
            case OptionsCategory::BLOCK_CREATION:
                usage += HelpMessageGroup("Block creation options:");
                break;
            case OptionsCategory::RPC:
                usage += HelpMessageGroup("RPC server options:");
                break;
            case OptionsCategory::WALLET:
                usage += HelpMessageGroup("Wallet options:");
                break;
            case OptionsCategory::WALLET_DEBUG_TEST:
                if (show_debug) usage += HelpMessageGroup("Wallet debugging/testing options:");
                break;
            case OptionsCategory::CHAINPARAMS:
                usage += HelpMessageGroup("Chain selection options:");
                break;
            case OptionsCategory::GUI:
                usage += HelpMessageGroup("UI Options:");
                break;
            case OptionsCategory::COMMANDS:
                usage += HelpMessageGroup("Commands:");
                break;
            case OptionsCategory::REGISTER_COMMANDS:
                usage += HelpMessageGroup("Register Commands:");
                break;
            default:
                break;
        }

        // When we get to the hidden options, stop
        if (arg_map.first == OptionsCategory::HIDDEN) break;

        for (const auto& arg : arg_map.second) {
            if (show_debug || !(arg.second.m_flags & ArgsManager::DEBUG_ONLY)) {
                std::string name;
                if (arg.second.m_help_param.empty()) {
                    name = arg.first;
                } else {
                    name = arg.first + arg.second.m_help_param;
                }
                usage += HelpMessageOpt(name, arg.second.m_help_text);
            }
        }
    }
    return usage;
}

int64_t ArgsManager::GetIntArg(const std::string& strArg, int64_t nDefault) const
{
    return GetIntArg(strArg).value_or(nDefault);
}

std::optional<int64_t> ArgsManager::GetIntArg(const std::string& strArg) const
{
    const util::SettingsValue value = GetSetting(strArg);
    return SettingToInt(value);
}

void ArgsManager::logArgsPrefix(
    const std::string& prefix,
    const std::string& section,
    const std::map<std::string, std::vector<util::SettingsValue>>& args) const
{
    std::string section_str = section.empty() ? "" : "[" + section + "] ";
    for (const auto& arg : args) {
        for (const auto& value : arg.second) {
            std::optional<unsigned int> flags = GetArgFlags('-' + arg.first);
            if (flags) {
                std::string value_str = (*flags & SENSITIVE) ? "****" : value.write();
                LogPrintf("%s %s%s=%s\n", prefix, section_str, arg.first, value_str);
            }
        }
    }
}

void ArgsManager::LogArgs() const
{
    LOCK(cs_args);
    for (const auto& section : m_settings.ro_config) {
        logArgsPrefix("Config file arg:", section.first, section.second);
    }
    for (const auto& setting : m_settings.rw_settings) {
        LogPrintf("Setting file arg: %s = %s\n", setting.first, setting.second.write());
    }
    logArgsPrefix("Command-line arg:", "", m_settings.command_line_options);
}

bool ArgsManager::ReadConfigStream(std::istream& stream, const std::string& filepath, std::string& error, bool ignore_invalid_keys)
{
    LOCK(cs_args);
    std::vector<std::pair<std::string, std::string>> options;
    if (!GetConfigOptions(stream, filepath, error, options, m_config_sections)) {
        return false;
    }
    for (const std::pair<std::string, std::string>& option : options) {
        KeyInfo key = InterpretKey(option.first);
        std::optional<unsigned int> flags = GetArgFlags('-' + key.name);
        if (!IsConfSupported(key, error)) return false;
        if (flags) {
            std::optional<util::SettingsValue> value = InterpretValue(key, &option.second, *flags, error);
            if (!value) {
                return false;
            }
            m_settings.ro_config[key.section][key.name].push_back(*value);
        } else {
            if (ignore_invalid_keys) {
                LogPrintf("Ignoring unknown configuration value %s\n", option.first);
            } else {
                error = strprintf("Invalid configuration value %s", option.first);
                return false;
            }
        }
    }
    return true;
}

bool ArgsManager::ReadConfigFiles(std::string& error, bool ignore_invalid_keys)
{
    {
        LOCK(cs_args);
        m_settings.ro_config.clear();
        m_config_sections.clear();
    }

    const fs::path conf_path = GetPathArg("-conf", BITCOIN_CONF_FILENAME);
    std::ifstream stream{GetConfigFile(conf_path)};

    // not ok to have a config file specified that cannot be opened
    if (IsArgSet("-conf") && !stream.good()) {
        error = strprintf("specified config file \"%s\" could not be opened.", fs::PathToString(conf_path));
        return false;
    }
    // ok to not have a config file
    if (stream.good()) {
        if (!ReadConfigStream(stream, fs::PathToString(conf_path), error, ignore_invalid_keys)) {
            return false;
        }
        // `-includeconf` cannot be included in the command line arguments except
        // as `-noincludeconf` (which indicates that no included conf file should be used).
        bool use_conf_file{true};
        {
            LOCK(cs_args);
            if (auto* includes = util::FindKey(m_settings.command_line_options, "includeconf")) {
                // ParseParameters() fails if a non-negated -includeconf is passed on the command-line
                assert(util::SettingsSpan(*includes).last_negated());
                use_conf_file = false;
            }
        }
        if (use_conf_file) {
            std::string chain_id = GetChainName();
            std::vector<std::string> conf_file_names;

            auto add_includes = [&](const std::string& network, size_t skip = 0) {
                size_t num_values = 0;
                LOCK(cs_args);
                if (auto* section = util::FindKey(m_settings.ro_config, network)) {
                    if (auto* values = util::FindKey(*section, "includeconf")) {
                        for (size_t i = std::max(skip, util::SettingsSpan(*values).negated()); i < values->size(); ++i) {
                            conf_file_names.push_back((*values)[i].get_str());
                        }
                        num_values = values->size();
                    }
                }
                return num_values;
            };

            // We haven't set m_network yet (that happens in SelectParams()), so manually check
            // for network.includeconf args.
            const size_t chain_includes = add_includes(chain_id);
            const size_t default_includes = add_includes({});

            for (const std::string& conf_file_name : conf_file_names) {
                std::ifstream conf_file_stream{GetConfigFile(fs::PathFromString(conf_file_name))};
                if (conf_file_stream.good()) {
                    if (!ReadConfigStream(conf_file_stream, conf_file_name, error, ignore_invalid_keys)) {
                        return false;
                    }
                    LogPrintf("Included configuration file %s\n", conf_file_name);
                } else {
                    error = "Failed to include configuration file " + conf_file_name;
                    return false;
                }
            }

            // Warn about recursive -includeconf
            conf_file_names.clear();
            add_includes(chain_id, /* skip= */ chain_includes);
            add_includes({}, /* skip= */ default_includes);
            std::string chain_id_final = GetChainName();
            if (chain_id_final != chain_id) {
                // Also warn about recursive includeconf for the chain that was specified in one of the includeconfs
                add_includes(chain_id_final);
            }
            for (const std::string& conf_file_name : conf_file_names) {
                tfm::format(std::cerr, "warning: -includeconf cannot be used from included files; ignoring -includeconf=%s\n", conf_file_name);
            }
        }
    }

    // If datadir is changed in .conf file:
    gArgs.ClearPathCache();
    if (!CheckDataDirOption()) {
        error = strprintf("specified data directory \"%s\" does not exist.", GetArg("-datadir", ""));
        return false;
    }
    return true;
}

std::string ArgsManager::GetChainName() const
{
    auto get_net = [&](const std::string& arg) {
        LOCK(cs_args);
        util::SettingsValue value = util::GetSetting(m_settings, /* section= */ "", SettingName(arg),
            /* ignore_default_section_config= */ false,
            /*ignore_nonpersistent=*/false,
            /* get_chain_name= */ true);
        return value.isNull() ? false : value.isBool() ? value.get_bool() : InterpretBool(value.get_str());
    };

    const bool fRegTest = get_net("-regtest");
    const bool fSigNet  = get_net("-signet");
    const bool fTestNet = get_net("-testnet");
    const bool is_chain_arg_set = IsArgSet("-chain");

    if ((int)is_chain_arg_set + (int)fRegTest + (int)fSigNet + (int)fTestNet > 1) {
        throw std::runtime_error("Invalid combination of -regtest, -signet, -testnet and -chain. Can use at most one.");
    }
    if (fRegTest)
        return CBaseChainParams::REGTEST;
    if (fSigNet) {
        return CBaseChainParams::SIGNET;
    }
    if (fTestNet)
        return CBaseChainParams::TESTNET;

    return GetArg("-chain", CBaseChainParams::MAIN);
}

bool ArgsManager::UseDefaultSection(const std::string& arg) const
{
    return m_network == CBaseChainParams::MAIN || m_network_only_args.count(arg) == 0;
}

util::SettingsValue ArgsManager::GetSetting(const std::string& arg) const
{
    LOCK(cs_args);
    return util::GetSetting(
        m_settings, m_network, SettingName(arg), !UseDefaultSection(arg),
        /*ignore_nonpersistent=*/false, /*get_chain_name=*/false);
}

std::vector<util::SettingsValue> ArgsManager::GetSettingsList(const std::string& arg) const
{
    LOCK(cs_args);
    return util::GetSettingsList(m_settings, m_network, SettingName(arg), !UseDefaultSection(arg));
}

std::optional<std::string> SettingToString(const util::SettingsValue& value)
{
    if (value.isNull()) return std::nullopt;
    if (value.isFalse()) return "0";
    if (value.isTrue()) return "1";
    if (value.isNum()) return value.getValStr();
    return value.get_str();
}

std::string SettingToString(const util::SettingsValue& value, const std::string& strDefault)
{
    return SettingToString(value).value_or(strDefault);
}

std::optional<int64_t> SettingToInt(const util::SettingsValue& value)
{
    if (value.isNull()) return std::nullopt;
    if (value.isFalse()) return 0;
    if (value.isTrue()) return 1;
    if (value.isNum()) return value.getInt<int64_t>();
    return LocaleIndependentAtoi<int64_t>(value.get_str());
}

int64_t SettingToInt(const util::SettingsValue& value, int64_t nDefault)
{
    return SettingToInt(value).value_or(nDefault);
}

std::optional<bool> SettingToBool(const util::SettingsValue& value)
{
    if (value.isNull()) return std::nullopt;
    if (value.isBool()) return value.get_bool();
    return InterpretBool(value.get_str());
}

bool SettingToBool(const util::SettingsValue& value, bool fDefault)
{
    return SettingToBool(value).value_or(fDefault);
}
