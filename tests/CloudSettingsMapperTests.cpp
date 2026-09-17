#include "account/CloudSettingsMapper.h"
#include <iostream>
#include <stdexcept>

using namespace metasequoia::linux_ime;
namespace
{
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <typename Action> void fails(Action action)
{
    try
    {
        action();
    }
    catch (const account::Failure &)
    {
        return;
    }
    throw std::runtime_error("invalid settings accepted");
}
} // namespace
int main()
{
    InputSettings local;
    local.page_size = 5;
    local.candidate_window_layout = CandidateWindowLayout::Horizontal;
    local.online.cloud_candidates_enabled = true;
    local.online.ai.token = "synthetic-private-credential";
    local.online.ai.endpoint = "https://private.invalid/never-export";
    local.voice.token = "synthetic-private-voice";
    local.clipboard_history_enabled = true;
    const auto exported = account::CloudSettingsMapper::export_settings(local);
    require(exported.size() == 43, "unexpected binding coverage");
    require(std::get<std::int64_t>(exported.at("appearance.page_size")) == 5, "page size not exported");
    require(std::get<std::string>(exported.at("appearance.candidate_window_layout")) == "horizontal",
            "candidate window layout not exported");
    for (const auto &[key, value] : exported)
    {
        require(key.find("credential") == std::string::npos && key.find("endpoint") == std::string::npos &&
                    key.find("clipboard") == std::string::npos,
                "private setting exported");
        if (const auto *text = std::get_if<std::string>(&value))
            require(text->find("synthetic-private") == std::string::npos &&
                        text->find("private.invalid") == std::string::npos,
                    "credential value exported");
    }
    account::Preferences remote{17,
                                {{"appearance.page_size", std::int64_t(9)},
                                 {"general.cloud_candidates", false},
                                 {"platform.ios.custom_keyboard_skin", std::string("synthetic-other-platform")}}};
    auto preview = account::CloudSettingsMapper::prepare_download(local, remote);
    require(preview.changes.size() == 2 && preview.unsupported_fields.size() == 1,
            "preview did not separate supported fields");
    require(preview.settings.page_size == 9 && !preview.settings.online.cloud_candidates_enabled,
            "download not applied to candidate");
    require(local.page_size == 5 && local.online.cloud_candidates_enabled, "preview mutated source");
    require(preview.settings.online.ai.token == local.online.ai.token &&
                preview.settings.voice.token == local.voice.token && preview.settings.clipboard_history_enabled,
            "download modified private settings");
    const auto upload = account::CloudSettingsMapper::prepare_upload(local, remote);
    require(upload.revision == 17 && std::get<std::string>(upload.settings.at("platform.ios.custom_keyboard_skin")) ==
                                         "synthetic-other-platform",
            "upload lost remote platform fields or revision");
    require(std::get<std::int64_t>(upload.settings.at("appearance.page_size")) == 5,
            "upload did not merge local fields");
    remote.settings["appearance.page_size"] = std::int64_t(100);
    fails([&] { account::CloudSettingsMapper::prepare_download(local, remote); });
    remote.settings["appearance.page_size"] = true;
    fails([&] { account::CloudSettingsMapper::prepare_download(local, remote); });
    remote.settings["appearance.page_size"] = std::int64_t(7);
    remote.settings["helpcode.quanpin_helpcode_schema"] = std::string("unsupported-schema");
    fails([&] { account::CloudSettingsMapper::prepare_download(local, remote); });
    require(local.page_size == 5 && local.online.ai.token == "synthetic-private-credential",
            "failed preview partially applied");
    account::Preferences mismatched_provider{1,
                                             {{"ai_assistant.provider", std::string("unsupported-provider")},
                                              {"ai_assistant.model", std::string("synthetic-model")}}};
    fails([&] { account::CloudSettingsMapper::prepare_download(local, mismatched_provider); });
    mismatched_provider.settings.erase("ai_assistant.provider");
    fails([&] { account::CloudSettingsMapper::prepare_download(local, mismatched_provider); });
    require(local.online.ai.token == "synthetic-private-credential", "provider mismatch changed local credentials");
    account::Preferences round_trip{1, exported};
    require(account::CloudSettingsMapper::prepare_download(local, round_trip).changes.empty(),
            "unchanged snapshot produced changes");
    std::cout << "cloud settings mapping and preview tests passed\n";
}
