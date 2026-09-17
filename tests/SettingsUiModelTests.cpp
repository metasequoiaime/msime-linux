#include "../src/SettingsUiModel.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace metasequoia::linux_ime;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
} // namespace

int main()
{
    try
    {
        InputSettings settings;
        settings.online.ai.token = "must-not-be-exposed";
        SettingsUiModel model(settings);

        const auto &rows = model.rows();
        require(!rows.empty(), "The settings model did not expose editable rows.");
        for (const auto &row : rows)
        {
            require(row.id.find("token") == std::string::npos &&
                        row.value.find("must-not-be-exposed") == std::string::npos,
                    "Secret credentials leaked into the settings model.");
        }
        const auto find_row = [&model](const std::string &id) {
            const auto &current = model.rows();
            const auto found =
                std::find_if(current.begin(), current.end(), [&id](const SettingsUiRow &row) { return row.id == id; });
            require(found != current.end(), "The settings model is missing an expected row.");
            return *found;
        };
        require(find_row("ai-credential").control == SettingsControl::Secret &&
                    find_row("voice-credential").control == SettingsControl::Secret &&
                    find_row("translation-credential").control == SettingsControl::Secret,
                "The settings model offers no credential entry for a provider that authenticates.");
        // The hydrated AI token is reported as held without being reported; the voice provider has none loaded.
        require(find_row("ai-credential").value == kSettingsCredentialStored &&
                    find_row("voice-credential").value.empty(),
                "A credential row misreported whether a credential is held.");
        require(settings_section_for_id("page-size") == SettingsUiSection::Appearance &&
                    settings_section_for_id("candidate-window-layout") == SettingsUiSection::Appearance &&
                    settings_section_for_id("scheme") == SettingsUiSection::Input &&
                    settings_section_for_id("quanpin-helpcode") == SettingsUiSection::Helpcode &&
                    settings_section_for_id("temporary-english-mode") == SettingsUiSection::Shortcuts &&
                    settings_section_for_id("frequency-adjustment") == SettingsUiSection::Dictionary &&
                    settings_section_for_id("voice-enabled") == SettingsUiSection::Voice &&
                    settings_section_for_id("clipboard-history") == SettingsUiSection::DesktopTools &&
                    settings_section_for_id("ai-enabled") == SettingsUiSection::Online,
                "Settings rows were assigned to the wrong Windows-style section.");
        require(std::string(settings_section_title(SettingsUiSection::Appearance)) == "外观" &&
                    std::string(settings_section_title(SettingsUiSection::Voice)) == "语音输入",
                "Settings section titles were not localized.");

        std::string error;
        require(model.set("scheme", "shuangpin", &error), "A valid choice could not be applied.");
        require(model.settings().scheme == SchemeType::Shuangpin, "The scheme choice was not applied.");
        require(model.set("page-size", "7", &error), "A valid integer could not be applied.");
        require(model.settings().page_size == 7, "The page size was not applied.");
        require(model.set("candidate-window-layout", "horizontal", &error),
                "A valid candidate window layout could not be applied.");
        require(model.settings().candidate_window_layout == CandidateWindowLayout::Horizontal,
                "The candidate window layout was not applied.");
        require(!model.set("candidate-window-layout", "diagonal", &error) && !error.empty(),
                "An unsupported candidate window layout was accepted.");
        require(model.set("cloud-enabled", "false", &error), "A valid online toggle could not be applied.");
        require(!model.settings().online.cloud_candidates_enabled, "The online toggle was not applied.");
        require(model.set("floating-toolbar", "false", &error), "A valid toolbar toggle could not be applied.");
        require(!model.settings().floating_toolbar_enabled, "The toolbar toggle was not applied.");
        require(model.set("voice-polish-enabled", "true", &error), "A valid voice polish toggle could not be applied.");
        require(model.set("voice-polish-prompt", "请整理并补充标点。", &error),
                "A valid voice polish prompt could not be applied.");
        require(model.settings().voice.polish_enabled && model.settings().voice.polish_prompt == "请整理并补充标点。",
                "Voice polish settings were not applied.");
        require(!model.set("page-size", "99", &error) && !error.empty(), "An out-of-range page size was accepted.");
        require(!model.set("unknown-setting", "x", &error) && !error.empty(), "An unknown setting was accepted.");
        require(!model.set("ai-endpoint", "http://insecure.example", &error) && !error.empty(),
                "An insecure AI endpoint was accepted by the settings UI model.");
        require(model.set("ai-endpoint", "https://secure.example", &error), "A valid HTTPS AI endpoint was rejected.");
        require(model.settings().online.ai.token == "must-not-be-exposed", "Editing discarded the in-memory token.");

        // SettingsStore refuses to persist these empty, so the model has to refuse them here, where the failure can
        // still be attributed to the row that produced it.
        require(!model.set("voice-language", "", &error) && !error.empty(), "An empty voice language was accepted.");
        require(!model.set("voice-model", "", &error) && !error.empty(), "An empty voice model was accepted.");
        require(!model.set("voice-polish-model", "", &error) && !error.empty(),
                "An empty voice polish model was accepted.");
        require(!model.set("voice-polish-prompt", "", &error) && !error.empty(),
                "An empty voice polish prompt was accepted.");

        // The endpoint is required only while the feature that sends to it is on, which is the rule SettingsStore
        // applies on save.
        require(!model.settings().voice.enabled, "Voice input was expected to start disabled.");
        require(model.set("voice-endpoint", "", &error),
                "An empty voice endpoint was rejected while voice input was off.");
        require(model.settings().voice.endpoint.empty(), "The empty voice endpoint was not applied.");
        require(!model.set("voice-endpoint", "http://insecure.example", &error) && !error.empty(),
                "An insecure voice endpoint was accepted.");
        require(model.set("voice-enabled", "true", &error) &&
                    model.set("voice-endpoint", "https://voice.example", &error),
                "A valid voice endpoint was rejected while voice input was on.");
        require(!model.set("voice-endpoint", "", &error) && !error.empty(),
                "An empty voice endpoint was accepted while voice input was on.");
        require(model.settings().voice.endpoint == "https://voice.example",
                "A rejected voice endpoint replaced the applied one.");

        // An untouched credential entry submits an empty string, which has to mean "keep what Secret Service holds":
        // the window never renders a stored credential back, so treating empty as "no credential" would erase one on
        // the first save of an unrelated setting.
        require(model.set("ai-credential", "", &error), "An empty credential entry was rejected.");
        require(model.settings().online.ai.token == "must-not-be-exposed",
                "An empty credential entry discarded the stored credential.");
        require(model.set("voice-credential", "typed-voice-credential", &error),
                "A typed credential could not be entered.");
        require(model.settings().voice.token == "typed-voice-credential", "The typed credential was not applied.");
        for (const auto &row : model.rows())
        {
            require(row.value.find("typed-voice-credential") == std::string::npos,
                    "A typed credential was rendered back into a settings row.");
        }
        require(find_row("voice-credential").value == kSettingsCredentialStored,
                "An entered credential was not reported as held.");

        // A credential is concatenated into an Authorization header, so a carriage return in one adds headers to the
        // request. The rejection must name the problem without quoting the value: this message reaches a dialog and any
        // diagnostic the caller writes.
        const std::string header_injection = "abcdef\r\nX-Injected: 1";
        require(!model.set("ai-credential", header_injection, &error) && !error.empty(),
                "A credential carrying a carriage return was accepted.");
        require(error.find("abcdef") == std::string::npos && error.find(header_injection) == std::string::npos,
                "The rejection message quoted the credential it refused.");
        require(model.settings().online.ai.token == "must-not-be-exposed",
                "A rejected credential replaced the stored one.");

        // Re-selecting the provider that is already active is what a page flush does on every save, and it must not
        // drop the credential the user just typed.
        const std::string voice_provider = model.settings().voice.provider;
        require(model.set("voice-provider", voice_provider, &error),
                "The active voice provider could not be reapplied.");
        require(model.settings().voice.token == "typed-voice-credential",
                "Reapplying the active voice provider discarded the credential.");
        // Switching provider does drop it: SettingsStore refuses to re-file one provider's credential under another's
        // name, so carrying it across can only produce a save failure.
        require(model.set("voice-provider", voice_provider == "groq" ? "openai" : "groq", &error),
                "A valid voice provider was rejected.");
        require(model.settings().voice.token.empty(),
                "Switching voice provider kept the previous provider's credential.");
        require(model.set("ai-provider", "openai", &error), "A valid AI provider was rejected.");
        require(model.settings().online.ai.token.empty(),
                "Switching AI provider kept the previous provider's credential.");
        require(model.set("translation-credential", "typed-translation-credential", &error),
                "A typed translation credential could not be entered.");
        require(model.set("translation-provider", "deeplx", &error), "A valid translation provider was rejected.");
        require(model.settings().online.translation_token.empty(),
                "Switching translation provider kept the previous provider's credential.");

        // A credential row is only offered where the configuration reads the value. The model still builds, parses and
        // reports a hidden row -- only the window skips it -- so this is a property of the row rather than of the
        // vector, and hiding one cannot silently drop a setting.
        require(model.settings().online.candidate_translations_enabled &&
                    model.settings().online.translation_provider == TranslationProvider::DeepLX,
                "The visibility checks below expect the DeepLX backend selected above.");
        require(find_row("translation-credential").visible &&
                    find_row(settings_credential_clear_id("translation-credential")).visible,
                "The translation token row is hidden for the one backend that authenticates.");
        require(model.set("translation-provider", "local", &error), "The local translation backend was rejected.");
        require(!find_row("translation-credential").visible &&
                    !find_row(settings_credential_clear_id("translation-credential")).visible,
                "The translation token row is still offered while the provider is 本地, where the value is never read "
                "and the store never files one under it.");
        require(find_row("translation-credential").control == SettingsControl::Secret,
                "A hidden row stopped being a row the model reports.");
        require(!model.settings().online.ai.enabled && !find_row("ai-credential").visible,
                "The AI token row is offered while AI suggestions are off.");
        require(model.set("ai-enabled", "true", &error) && find_row("ai-credential").visible,
                "The AI token row stayed hidden after AI suggestions were turned on.");
        require(model.settings().voice.enabled && find_row("voice-credential").visible,
                "The voice token row is hidden while voice input is on.");
        require(settings_section_for_id(settings_credential_clear_id("ai-credential")) == SettingsUiSection::Online &&
                    settings_section_for_id(settings_credential_clear_id("voice-credential")) ==
                        SettingsUiSection::Voice,
                "A credential clear row landed on a different page from the credential it belongs to.");

        // The gesture that says "forget this token". An empty entry cannot mean it -- that has to keep whatever Secret
        // Service holds, or an untouched form would erase a credential on every save -- so it is a row of its own.
        require(find_row(settings_credential_clear_id("ai-credential")).control == SettingsControl::Boolean &&
                    find_row(settings_credential_clear_id("ai-credential")).value == "false",
                "The credential clear row is missing, or starts asking for a removal nobody requested.");
        require(model.set("ai-credential", "sk-to-be-forgotten", &error) &&
                    model.settings().online.ai.token == "sk-to-be-forgotten",
                "A credential could not be entered before the clear checks.");
        require(model.set(settings_credential_clear_id("ai-credential"), "true", &error),
                "A request to forget a credential was rejected.");
        require(model.settings().online.ai_credential_cleared && model.settings().online.ai.token.empty(),
                "A clear request left the in-memory credential in place, which SettingsStore reads as the credential "
                "to re-file over the removal.");
        require(find_row("ai-credential").value.empty(),
                "A row whose credential has just been forgotten still reports that one is held.");
        require(model.set("ai-credential", "sk-replacement", &error), "A replacement credential was rejected.");
        require(!model.settings().online.ai_credential_cleared && model.settings().online.ai.token == "sk-replacement",
                "Entering a credential did not withdraw the request to forget the one it replaces, so the two rows "
                "would reach the store disagreeing.");
        // What makes that resolution work in a page flush, which applies rows in order.
        const auto &ordered = model.rows();
        const auto clear_row = std::find_if(ordered.begin(), ordered.end(), [](const SettingsUiRow &row) {
            return row.id == settings_credential_clear_id("ai-credential");
        });
        const auto entry_row = std::find_if(ordered.begin(), ordered.end(),
                                            [](const SettingsUiRow &row) { return row.id == "ai-credential"; });
        require(clear_row != ordered.end() && entry_row != ordered.end() && clear_row < entry_row,
                "The credential entry no longer follows its clear row, so a credential typed on the same visit would "
                "be discarded by the tick instead of withdrawing it.");

        std::cout << "Settings UI model tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
