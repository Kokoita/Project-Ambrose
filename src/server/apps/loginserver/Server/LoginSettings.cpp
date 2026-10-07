/*
 * Project Ambrose by Imjustchico
 * Reads the Login options from config, refusing an empty or overlong server name, clamping out-of-range values and the AFK warning byte, refusing to enforce an empty revision list, checking the maintenance reason and optional Unix-time window, and reporting each problem.
 */

#include "LoginSettings.h"
#include "ConfigMgr.h"
#include "StringUtil.h"

#include <fmt/format.h>

#include <algorithm>

bool LoginSettings::AllowsRevision(std::string_view revision) const
{
    return std::find(AllowedRevisions.begin(), AllowedRevisions.end(), revision) != AllowedRevisions.end();
}

bool LoginSettings::AllowsSignIn(uint8 securityLevel) const noexcept
{
    return !Maintenance || securityLevel >= MaintenanceBypassLevel;
}

LoginSettings LoginSettings::Load(ConfigMgr const& config, std::vector<std::string>* problems)
{
    auto report = [problems](std::string problem)
    {
        if (problems)
            problems->push_back(std::move(problem));
    };
    auto bounded = [&](std::string const& option, uint32 fallback, uint32 minimum, uint32 maximum)
    {
        uint32 const configured = config.GetOption<uint32>(option, fallback, true);
        uint32 const value = std::clamp(configured, minimum, maximum);
        if (value != configured)
            report(fmt::format("{} = {} is outside {}-{}; using {}", option, configured, minimum, maximum, value));
        return value;
    };

    LoginSettings settings;
    std::string const name = std::string(Ambrose::Trim(config.GetOption<std::string>("Login.Name", std::string(DefaultName), true)));
    if (name.size() > MaxNameBytes)
        report(fmt::format("Login.Name must be at most {} bytes; using {}", MaxNameBytes, DefaultName));
    else
        settings.Name = name;
    std::string const revisions = config.GetOption<std::string>("Login.AllowedRevision", "", true);
    for (std::string_view const revision : Ambrose::Tokenize(revisions, ',', false))
        if (std::string_view const trimmed = Ambrose::Trim(revision); !trimmed.empty())
            settings.AllowedRevisions.emplace_back(trimmed);
    settings.EnforceRevision = config.GetOption<bool>("Login.EnforceRevision", false, true);
    if (settings.EnforceRevision && settings.AllowedRevisions.empty())
    {
        report("Login.EnforceRevision = 1 with no Login.AllowedRevision would refuse every client; revisions are not enforced");
        settings.EnforceRevision = false;
    }
    settings.Maintenance = config.GetOption<bool>("Login.Maintenance", false, true);
    settings.MaintenanceBypassLevel = static_cast<uint8>(bounded("Login.MaintenanceBypassLevel", DefaultMaintenanceBypassLevel, 0, MaxMaintenanceBypassLevel));
    std::string const maintenanceReason = std::string(Ambrose::Trim(config.GetOption<std::string>("Login.MaintenanceReason", std::string(DefaultMaintenanceReason), true)));
    if (maintenanceReason.empty() || maintenanceReason.size() > MaxMaintenanceReasonBytes)
        report(fmt::format("Login.MaintenanceReason must be 1-{} bytes; using the default maintenance reason", MaxMaintenanceReasonBytes));
    else
        settings.MaintenanceReason = maintenanceReason;
    settings.MaintenanceWindowStart = config.GetOption<uint64>("Login.MaintenanceWindowStart", 0, true);
    settings.MaintenanceWindowEnd = config.GetOption<uint64>("Login.MaintenanceWindowEnd", 0, true);
    if (settings.MaintenanceWindowStart > MaxMaintenanceWindowEpochSeconds || settings.MaintenanceWindowEnd > MaxMaintenanceWindowEpochSeconds ||
        ((settings.MaintenanceWindowStart == 0) != (settings.MaintenanceWindowEnd == 0)) ||
        (settings.MaintenanceWindowStart != 0 && settings.MaintenanceWindowEnd <= settings.MaintenanceWindowStart))
    {
        report("Login.MaintenanceWindowStart and Login.MaintenanceWindowEnd must both be zero or form an increasing Unix-time window; clearing the window");
        settings.MaintenanceWindowStart = 0;
        settings.MaintenanceWindowEnd = 0;
    }

    settings.MaxAuthAttempts = bounded("Login.MaxAuthAttempts", DefaultMaxAuthAttempts, 0, MaxAuthAttemptsLimit);
    settings.Lockout = std::chrono::seconds(bounded("Login.LockoutSeconds", DefaultLockoutSeconds, 1, MaxDurationSeconds));
    settings.SessionKeyLifetime = std::chrono::seconds(bounded("Login.SessionKeyLifetime", DefaultSessionKeyLifetimeSeconds, MinSessionKeyLifetimeSeconds, MaxDurationSeconds));
    settings.KeyTtl = std::chrono::seconds(bounded("Login.KeyTTL", DefaultKeyTtlSeconds, MinKeyTtlSeconds, MaxDurationSeconds));

    settings.MaxCharactersPerAccount = bounded("Character.MaxPerAccount", DefaultMaxCharactersPerAccount, 0, MaxCharactersPerAccountLimit);
    settings.AllowChosenNames = config.GetOption<bool>("Character.AllowChosenNames", false, true);
    std::string const deleteMode = config.GetOption<std::string>("Character.DeleteMode", "soft", true);
    settings.HardDelete = Ambrose::EqualsIgnoreCase(deleteMode, "hard");
    if (!settings.HardDelete && !Ambrose::EqualsIgnoreCase(deleteMode, "soft"))
        report(fmt::format("Character.DeleteMode = {} is not soft or hard; using soft", deleteMode));
    settings.KeepDeletedDays = bounded("Character.KeepDeletedDays", 0, 0, MaxKeepDeletedDays);

    settings.AfkTimeout = std::chrono::seconds(bounded("Login.AfkTimeout", DefaultAfkTimeoutSeconds, 0, MaxAfkTimeoutSeconds));
    settings.ShutdownGrace = std::chrono::seconds(bounded("Login.ShutdownGrace", DefaultShutdownGraceSeconds, 0, MaxShutdownGraceSeconds));
    int32 const warning = config.GetOption<int32>("Login.AfkWarning", DefaultAfkWarning, true);
    settings.AfkWarning = static_cast<int8>(std::clamp<int32>(warning, -128, 127));
    if (settings.AfkWarning != warning)
        report(fmt::format("Login.AfkWarning = {} is outside -128-127; using {}", warning, settings.AfkWarning));

    uint32 const policy = config.GetOption<uint32>("Login.DuplicateLoginPolicy", static_cast<uint32>(DuplicateLoginPolicy::KickExisting), true);
    if (policy <= static_cast<uint32>(DuplicateLoginPolicy::KickExisting))
        settings.DuplicateLogins = static_cast<DuplicateLoginPolicy>(policy);
    else
        report(fmt::format("Login.DuplicateLoginPolicy = {} is not 0 (reject) or 1 (kick the existing session); using 1", policy));
    return settings;
}
