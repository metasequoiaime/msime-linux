#include "SettingsStore.h"

#include "online/EndpointPolicy.h"

#include "core/data_path.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <vector>

namespace metasequoia::linux_ime
{
namespace
{
class ConfigWriteLock
{
  public:
    explicit ConfigWriteLock(const std::filesystem::path &path)
    {
        if (g_mkdir_with_parents(metasequoia::path_to_utf8(path.parent_path()).c_str(), 0700) != 0)
            return;
        const auto lock_path = metasequoia::path_to_utf8(path) + ".lock";
        descriptor_ = open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (descriptor_ < 0)
            return;
        struct stat status{};
        if (fstat(descriptor_, &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != geteuid())
            return;
        int result;
        do
        {
            result = flock(descriptor_, LOCK_EX);
        } while (result != 0 && errno == EINTR);
        locked_ = result == 0;
    }
    ~ConfigWriteLock()
    {
        if (descriptor_ >= 0)
            close(descriptor_);
    }
    ConfigWriteLock(const ConfigWriteLock &) = delete;
    ConfigWriteLock &operator=(const ConfigWriteLock &) = delete;
    explicit operator bool() const
    {
        return locked_;
    }

  private:
    int descriptor_ = -1;
    bool locked_ = false;
};

constexpr const char *kGroup = "input";
constexpr const char *kOnlineGroup = "online";
constexpr const char *kAiGroup = "ai";
constexpr const char *kTranslationGroup = "translation";
constexpr const char *kUtilityGroup = "utility";
constexpr const char *kVoiceGroup = "voice";
constexpr const char *kKeybindingsGroup = "keybindings";
constexpr std::size_t kMinimumPageSize = 3;
constexpr std::size_t kMaximumPageSize = 9;
constexpr int kMinimumFrequencyValue = 1;
constexpr int kMaximumFrequencyValue = 10;
constexpr gint kMinimumEnglishPrefix = 1;
constexpr gint kMaximumEnglishPrefix = 8;
constexpr gint kMinimumConnectTimeoutMs = 100;
constexpr gint kMaximumConnectTimeoutMs = 10000;
constexpr gint kMinimumTotalTimeoutMs = 500;
constexpr gint kMaximumTotalTimeoutMs = 30000;
constexpr gint kMinimumAiCandidateLimit = 1;
constexpr gint kMaximumAiCandidateLimit = 10;
constexpr std::size_t kMaximumEndpointBytes = 2048;
constexpr std::size_t kMaximumModelBytes = 256;
constexpr std::size_t kMaximumPromptBytes = 8192;

void set_message(std::string *destination, const char *message)
{
    if (destination != nullptr)
    {
        *destination = message;
    }
}

void append_message(std::string *destination, const char *message)
{
    if (destination == nullptr || message == nullptr || *message == '\0')
    {
        return;
    }
    if (!destination->empty())
    {
        destination->append(" ");
    }
    destination->append(message);
}

std::optional<online::AiProvider> parse_ai_provider(std::string_view provider)
{
    if (provider == "deepseek")
    {
        return online::AiProvider::DeepSeek;
    }
    if (provider == "openai")
    {
        return online::AiProvider::OpenAI;
    }
    if (provider == "siliconflow")
    {
        return online::AiProvider::SiliconFlow;
    }
    if (provider == "groq")
    {
        return online::AiProvider::Groq;
    }
    if (provider == "custom")
    {
        return online::AiProvider::Custom;
    }
    return std::nullopt;
}

std::optional<TranslationProvider> parse_translation_provider(std::string_view provider)
{
    if (provider == "local")
    {
        return TranslationProvider::Local;
    }
    if (provider == "deeplx")
    {
        return TranslationProvider::DeepLX;
    }
    return std::nullopt;
}

bool valid_utf8_text(std::string_view value, std::size_t maximum_bytes, bool allow_newlines)
{
    if (value.size() > maximum_bytes || value.find('\0') != std::string_view::npos ||
        !g_utf8_validate(value.data(), static_cast<gssize>(value.size()), nullptr))
    {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [allow_newlines](unsigned char character) {
        if (allow_newlines && (character == '\n' || character == '\r' || character == '\t'))
        {
            return false;
        }
        return character < 0x20U || character == 0x7fU;
    });
}

bool valid_https_endpoint(std::string_view endpoint)
{
    return endpoint.empty() || (endpoint.size() <= kMaximumEndpointBytes && endpoint.rfind("https://", 0) == 0 &&
                                valid_utf8_text(endpoint, kMaximumEndpointBytes, false));
}

bool valid_translation_language(std::string_view language)
{
    constexpr std::string_view languages[]{"en", "fr", "ja", "es", "ru", "de", "ko"};
    return std::find(std::begin(languages), std::end(languages), language) != std::end(languages);
}

bool valid_online_settings(const OnlineSettings &settings)
{
    const auto connect_timeout = settings.connect_timeout.count();
    const auto total_timeout = settings.total_timeout.count();
    return ai_provider_name(settings.ai.provider) != nullptr &&
           translation_provider_name(settings.translation_provider) != nullptr &&
           valid_https_endpoint(settings.ai.endpoint) &&
           valid_utf8_text(settings.ai.model, kMaximumModelBytes, false) &&
           valid_utf8_text(settings.ai.prompt, kMaximumPromptBytes, true) &&
           settings.ai.candidate_limit >= static_cast<std::size_t>(kMinimumAiCandidateLimit) &&
           settings.ai.candidate_limit <= static_cast<std::size_t>(kMaximumAiCandidateLimit) &&
           valid_translation_language(settings.translation_target_language) &&
           valid_https_endpoint(settings.translation_endpoint) && connect_timeout >= kMinimumConnectTimeoutMs &&
           connect_timeout <= kMaximumConnectTimeoutMs && total_timeout >= kMinimumTotalTimeoutMs &&
           total_timeout <= kMaximumTotalTimeoutMs &&
           // Endpoints are checked at this layer and again before a request; a
           // token used to be checked only before the request, so a malformed
           // one was accepted here, stored, and then silently refused later.
           online::token_allowed(settings.ai.token) && online::token_allowed(settings.translation_token);
}

bool valid_voice_settings(const VoiceInputConfig &settings)
{
    return valid_utf8_text(settings.provider, 64, false) && !settings.provider.empty() &&
           valid_https_endpoint(settings.endpoint) && valid_utf8_text(settings.model, 256, false) &&
           !settings.model.empty() && valid_utf8_text(settings.language, 32, false) && !settings.language.empty() &&
           (!settings.enabled || !settings.endpoint.empty()) && valid_https_endpoint(settings.polish_endpoint) &&
           valid_utf8_text(settings.polish_model, 256, false) && !settings.polish_model.empty() &&
           valid_utf8_text(settings.polish_prompt, 8192, true) && !settings.polish_prompt.empty() &&
           (!settings.polish_enabled || !settings.polish_endpoint.empty()) && online::token_allowed(settings.token);
}

const char *mode_name(InputMode mode)
{
    switch (mode)
    {
    case InputMode::Ime:
        return "ime";
    case InputMode::Direct:
        return "direct";
    }
    return nullptr;
}

const char *scheme_name(SchemeType scheme)
{
    switch (scheme)
    {
    case SchemeType::Quanpin:
        return "quanpin";
    case SchemeType::Shuangpin:
        return "shuangpin";
    case SchemeType::Wubi:
        return "wubi";
    case SchemeType::JapaneseRomaji:
        return "japanese";
    }
    return nullptr;
}

const char *punctuation_lock_name(PunctuationLock lock)
{
    switch (lock)
    {
    case PunctuationLock::Follow:
        return "follow";
    case PunctuationLock::Chinese:
        return "chinese";
    case PunctuationLock::English:
        return "english";
    }
    return nullptr;
}

const char *punctuation_name(PunctuationMode mode)
{
    switch (mode)
    {
    case PunctuationMode::Chinese:
        return "chinese";
    case PunctuationMode::English:
        return "english";
    }
    return nullptr;
}

const char *preedit_style_name(PreeditStyle style)
{
    switch (style)
    {
    case PreeditStyle::Raw:
        return "raw";
    case PreeditStyle::Pinyin:
        return "pinyin";
    case PreeditStyle::Hidden:
        return "hidden";
    }
    return nullptr;
}

const char *candidate_window_layout_name(CandidateWindowLayout layout)
{
    switch (layout)
    {
    case CandidateWindowLayout::Vertical:
        return "vertical";
    case CandidateWindowLayout::Horizontal:
        return "horizontal";
    }
    return nullptr;
}

const char *frequency_adjustment_name(FrequencyAdjustmentMode mode)
{
    switch (mode)
    {
    case FrequencyAdjustmentMode::Disabled:
        return "disabled";
    case FrequencyAdjustmentMode::Pin:
        return "pin";
    case FrequencyAdjustmentMode::Halve:
        return "halve";
    case FrequencyAdjustmentMode::Linear:
        return "linear";
    case FrequencyAdjustmentMode::Promote:
        return "promote";
    }
    return nullptr;
}

bool valid_character_width(CharacterWidth width)
{
    return width == CharacterWidth::Half || width == CharacterWidth::Full;
}

bool valid_input_settings(const InputSettings &settings)
{
    return mode_name(settings.mode) != nullptr && mode_name(settings.default_mode) != nullptr &&
           scheme_name(settings.scheme) != nullptr && punctuation_name(settings.punctuation_mode) != nullptr &&
           punctuation_lock_name(settings.punctuation_lock) != nullptr &&
           preedit_style_name(settings.preedit_style) != nullptr &&
           candidate_window_layout_name(settings.candidate_window_layout) != nullptr &&
           frequency_adjustment_name(settings.frequency_adjustment_mode) != nullptr &&
           valid_character_width(settings.character_width) && settings.page_size >= kMinimumPageSize &&
           settings.page_size <= kMaximumPageSize &&
           Session::is_supported_helpcode_schema(settings.quanpin_helpcode_schema) &&
           Session::is_supported_helpcode_schema(settings.shuangpin_helpcode_schema) &&
           settings.frequency_trigger_count >= kMinimumFrequencyValue &&
           settings.frequency_trigger_count <= kMaximumFrequencyValue &&
           settings.frequency_linear_step >= kMinimumFrequencyValue &&
           settings.frequency_linear_step <= kMaximumFrequencyValue &&
           settings.mixed_english_minimum_prefix >= static_cast<std::size_t>(kMinimumEnglishPrefix) &&
           settings.mixed_english_minimum_prefix <= static_cast<std::size_t>(kMaximumEnglishPrefix) &&
           valid_online_settings(settings.online) && valid_voice_settings(settings.voice);
}

bool write_all(int descriptor, const char *data, std::size_t size)
{
    std::size_t written = 0;
    while (written < size)
    {
        const ssize_t count = ::write(descriptor, data + written, size - written);
        if (count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (count == 0)
        {
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    return true;
}

std::string digest_of(const char *data, std::size_t size)
{
    gchar *checksum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, reinterpret_cast<const guchar *>(data), size);
    if (checksum == nullptr)
    {
        return {};
    }
    std::string digest(checksum);
    g_free(checksum);
    return digest;
}
} // namespace

const char *ai_provider_name(online::AiProvider provider)
{
    switch (provider)
    {
    case online::AiProvider::DeepSeek:
        return "deepseek";
    case online::AiProvider::OpenAI:
        return "openai";
    case online::AiProvider::SiliconFlow:
        return "siliconflow";
    case online::AiProvider::Groq:
        return "groq";
    case online::AiProvider::Custom:
        return "custom";
    }
    return nullptr;
}

const char *translation_provider_name(TranslationProvider provider)
{
    switch (provider)
    {
    case TranslationProvider::Local:
        return "local";
    case TranslationProvider::DeepLX:
        return "deeplx";
    }
    return nullptr;
}

std::string settings_file_digest(const std::filesystem::path &path)
{
    gchar *contents = nullptr;
    gsize size = 0;
    if (!g_file_get_contents(metasequoia::path_to_utf8(path).c_str(), &contents, &size, nullptr))
    {
        return {};
    }
    std::string digest = digest_of(contents, size);
    g_free(contents);
    return digest;
}

SettingsStore::SettingsStore() : SettingsStore(metasequoia::path_from_utf8(g_get_user_config_dir()))
{
}

SettingsStore::SettingsStore(std::filesystem::path config_home)
    : config_path_(std::move(config_home) / "metasequoiaime" / "config.ini")
{
}

InputSettings SettingsStore::load(std::string *warning) const
{
    set_message(warning, "");
    InputSettings settings;
    GError *read_error = nullptr;
    gchar *contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(metasequoia::path_to_utf8(config_path_).c_str(), &contents, &length, &read_error))
    {
        if (g_error_matches(read_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
            g_clear_error(&read_error);
            return settings;
        }
        g_clear_error(&read_error);
        set_message(warning, "Unable to read input settings; defaults were used.");
        return settings;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *parse_error = nullptr;
    if (!g_key_file_load_from_data(key_file, contents, length,
                                   static_cast<GKeyFileFlags>(G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS),
                                   &parse_error))
    {
        g_clear_error(&parse_error);
        g_key_file_unref(key_file);
        g_free(contents);
        set_message(warning, "Unable to parse input settings; defaults were used.");
        return settings;
    }
    g_free(contents);

    bool invalid = false;
    if (g_key_file_has_key(key_file, kGroup, "mode", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "mode", nullptr);
        if (value != nullptr && std::string_view(value) == "direct")
        {
            settings.mode = InputMode::Direct;
        }
        else if (value == nullptr || std::string_view(value) != "ime")
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "default-mode", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "default-mode", nullptr);
        if (value != nullptr && std::string_view(value) == "direct")
        {
            settings.default_mode = InputMode::Direct;
        }
        else if (value == nullptr || std::string_view(value) != "ime")
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "scheme", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "scheme", nullptr);
        const std::string_view name = value == nullptr ? std::string_view{} : std::string_view(value);
        if (name == "shuangpin")
        {
            settings.scheme = SchemeType::Shuangpin;
        }
        else if (name == "wubi")
        {
            settings.scheme = SchemeType::Wubi;
        }
        else if (name == "japanese")
        {
            settings.scheme = SchemeType::JapaneseRomaji;
        }
        else if (name != "quanpin")
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "page-size", nullptr))
    {
        GError *value_error = nullptr;
        const gint value = g_key_file_get_integer(key_file, kGroup, "page-size", &value_error);
        if (value_error == nullptr && value >= static_cast<gint>(kMinimumPageSize) &&
            value <= static_cast<gint>(kMaximumPageSize))
        {
            settings.page_size = static_cast<std::size_t>(value);
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "punctuation-lock", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "punctuation-lock", nullptr);
        const std::string_view text = value != nullptr ? std::string_view(value) : std::string_view();
        if (text == "chinese")
        {
            settings.punctuation_lock = PunctuationLock::Chinese;
        }
        else if (text == "english")
        {
            settings.punctuation_lock = PunctuationLock::English;
        }
        else if (text != "follow")
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "punctuation", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "punctuation", nullptr);
        if (value != nullptr && std::string_view(value) == "english")
        {
            settings.punctuation_mode = PunctuationMode::English;
        }
        else if (value == nullptr || std::string_view(value) != "chinese")
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "full-width", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "full-width", &value_error);
        if (value_error == nullptr)
        {
            settings.character_width = value ? CharacterWidth::Full : CharacterWidth::Half;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "comma-period-paging", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "comma-period-paging", &value_error);
        if (value_error == nullptr)
        {
            settings.comma_period_paging = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "word-to-character", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "word-to-character", &value_error);
        if (value_error == nullptr)
        {
            settings.word_to_character = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "bracket-paging", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "bracket-paging", &value_error);
        if (value_error == nullptr)
        {
            settings.bracket_paging = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "smart-punctuation", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "smart-punctuation", &value_error);
        if (value_error == nullptr)
        {
            settings.smart_punctuation = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "smart-punctuation-repeat-to-chinese", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value =
            g_key_file_get_boolean(key_file, kGroup, "smart-punctuation-repeat-to-chinese", &value_error);
        if (value_error == nullptr)
        {
            settings.smart_punctuation_repeat_to_chinese = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "paired-punctuation", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "paired-punctuation", &value_error);
        if (value_error == nullptr)
        {
            settings.paired_punctuation = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "preedit-style", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "preedit-style", nullptr);
        const std::string_view name = value == nullptr ? std::string_view{} : std::string_view(value);
        if (name == "pinyin")
        {
            settings.preedit_style = PreeditStyle::Pinyin;
        }
        else if (name == "hidden")
        {
            settings.preedit_style = PreeditStyle::Hidden;
        }
        else if (name != "raw")
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "candidate-window-layout", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "candidate-window-layout", nullptr);
        const std::string_view name = value == nullptr ? std::string_view{} : std::string_view(value);
        if (name == "horizontal")
        {
            settings.candidate_window_layout = CandidateWindowLayout::Horizontal;
        }
        else if (name != "vertical")
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "quanpin-helpcode", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "quanpin-helpcode", &value_error);
        if (value_error == nullptr)
        {
            settings.quanpin_helpcode_enabled = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "quanpin-helpcode-schema", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "quanpin-helpcode-schema", nullptr);
        if (value != nullptr && Session::is_supported_helpcode_schema(value))
        {
            settings.quanpin_helpcode_schema = value;
        }
        else
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "shuangpin-helpcode", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "shuangpin-helpcode", &value_error);
        if (value_error == nullptr)
        {
            settings.shuangpin_helpcode_enabled = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "shuangpin-helpcode-schema", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "shuangpin-helpcode-schema", nullptr);
        if (value != nullptr && Session::is_supported_helpcode_schema(value))
        {
            settings.shuangpin_helpcode_schema = value;
        }
        else
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "frequency-adjustment", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kGroup, "frequency-adjustment", nullptr);
        const std::string_view name = value == nullptr ? std::string_view{} : std::string_view(value);
        if (name == "disabled")
        {
            settings.frequency_adjustment_mode = FrequencyAdjustmentMode::Disabled;
        }
        else if (name == "pin")
        {
            settings.frequency_adjustment_mode = FrequencyAdjustmentMode::Pin;
        }
        else if (name == "halve")
        {
            settings.frequency_adjustment_mode = FrequencyAdjustmentMode::Halve;
        }
        else if (name == "linear")
        {
            settings.frequency_adjustment_mode = FrequencyAdjustmentMode::Linear;
        }
        else if (name != "promote")
        {
            invalid = true;
        }
        g_free(value);
    }

    if (g_key_file_has_key(key_file, kGroup, "frequency-trigger-count", nullptr))
    {
        GError *value_error = nullptr;
        const gint value = g_key_file_get_integer(key_file, kGroup, "frequency-trigger-count", &value_error);
        if (value_error == nullptr && value >= kMinimumFrequencyValue && value <= kMaximumFrequencyValue)
        {
            settings.frequency_trigger_count = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "frequency-linear-step", nullptr))
    {
        GError *value_error = nullptr;
        const gint value = g_key_file_get_integer(key_file, kGroup, "frequency-linear-step", &value_error);
        if (value_error == nullptr && value >= kMinimumFrequencyValue && value <= kMaximumFrequencyValue)
        {
            settings.frequency_linear_step = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "unicode-mode", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "unicode-mode", &value_error);
        if (value_error == nullptr)
        {
            settings.unicode_mode_enabled = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "super-jianpin-mode", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "super-jianpin-mode", &value_error);
        if (value_error == nullptr)
        {
            settings.super_jianpin_mode_enabled = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "temporary-english-mode", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "temporary-english-mode", &value_error);
        if (value_error == nullptr)
        {
            settings.temporary_english_mode_enabled = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "temporary-japanese-mode", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "temporary-japanese-mode", &value_error);
        if (value_error == nullptr)
        {
            settings.temporary_japanese_mode_enabled = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "mixed-english-candidates", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "mixed-english-candidates", &value_error);
        if (value_error == nullptr)
        {
            settings.mixed_english_candidates_enabled = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "mixed-english-minimum-prefix", nullptr))
    {
        GError *value_error = nullptr;
        const gint value = g_key_file_get_integer(key_file, kGroup, "mixed-english-minimum-prefix", &value_error);
        if (value_error == nullptr && value >= kMinimumEnglishPrefix && value <= kMaximumEnglishPrefix)
        {
            settings.mixed_english_minimum_prefix = static_cast<std::size_t>(value);
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "mixed-emoji-candidates", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "mixed-emoji-candidates", &value_error);
        if (value_error == nullptr)
        {
            settings.mixed_emoji_candidates_enabled = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    if (g_key_file_has_key(key_file, kGroup, "mixed-kaomoji-candidates", nullptr))
    {
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, kGroup, "mixed-kaomoji-candidates", &value_error);
        if (value_error == nullptr)
        {
            settings.mixed_kaomoji_candidates_enabled = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    const auto load_boolean = [&](const char *group, const char *key, bool &destination) {
        if (!g_key_file_has_key(key_file, group, key, nullptr))
        {
            return;
        }
        GError *value_error = nullptr;
        const gboolean value = g_key_file_get_boolean(key_file, group, key, &value_error);
        if (value_error == nullptr)
        {
            destination = value;
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    };
    const auto load_string = [&](const char *group, const char *key, std::string &destination, const auto &validator) {
        if (!g_key_file_has_key(key_file, group, key, nullptr))
        {
            return;
        }
        gchar *value = g_key_file_get_string(key_file, group, key, nullptr);
        const std::string_view text = value == nullptr ? std::string_view{} : std::string_view(value);
        if (value != nullptr && validator(text))
        {
            destination.assign(text);
        }
        else
        {
            invalid = true;
        }
        g_free(value);
    };

    load_boolean(kOnlineGroup, "cloud-enabled", settings.online.cloud_candidates_enabled);
    if (g_key_file_has_key(key_file, kOnlineGroup, "connect-timeout-ms", nullptr))
    {
        GError *value_error = nullptr;
        const gint value = g_key_file_get_integer(key_file, kOnlineGroup, "connect-timeout-ms", &value_error);
        if (value_error == nullptr && value >= kMinimumConnectTimeoutMs && value <= kMaximumConnectTimeoutMs)
        {
            settings.online.connect_timeout = std::chrono::milliseconds(value);
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }
    if (g_key_file_has_key(key_file, kOnlineGroup, "total-timeout-ms", nullptr))
    {
        GError *value_error = nullptr;
        const gint value = g_key_file_get_integer(key_file, kOnlineGroup, "total-timeout-ms", &value_error);
        if (value_error == nullptr && value >= kMinimumTotalTimeoutMs && value <= kMaximumTotalTimeoutMs)
        {
            settings.online.total_timeout = std::chrono::milliseconds(value);
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    load_boolean(kAiGroup, "enabled", settings.online.ai.enabled);
    if (g_key_file_has_key(key_file, kAiGroup, "provider", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kAiGroup, "provider", nullptr);
        const auto provider = parse_ai_provider(value == nullptr ? std::string_view{} : std::string_view(value));
        if (provider.has_value())
        {
            settings.online.ai.provider = *provider;
        }
        else
        {
            invalid = true;
        }
        g_free(value);
    }
    load_string(kAiGroup, "endpoint", settings.online.ai.endpoint, valid_https_endpoint);
    load_string(kAiGroup, "model", settings.online.ai.model,
                [](std::string_view value) { return valid_utf8_text(value, kMaximumModelBytes, false); });
    load_string(kAiGroup, "prompt", settings.online.ai.prompt,
                [](std::string_view value) { return valid_utf8_text(value, kMaximumPromptBytes, true); });
    if (g_key_file_has_key(key_file, kAiGroup, "candidate-limit", nullptr))
    {
        GError *value_error = nullptr;
        const gint value = g_key_file_get_integer(key_file, kAiGroup, "candidate-limit", &value_error);
        if (value_error == nullptr && value >= kMinimumAiCandidateLimit && value <= kMaximumAiCandidateLimit)
        {
            settings.online.ai.candidate_limit = static_cast<std::size_t>(value);
        }
        else
        {
            invalid = true;
        }
        g_clear_error(&value_error);
    }

    load_boolean(kTranslationGroup, "enabled", settings.online.candidate_translations_enabled);
    if (g_key_file_has_key(key_file, kTranslationGroup, "provider", nullptr))
    {
        gchar *value = g_key_file_get_string(key_file, kTranslationGroup, "provider", nullptr);
        const auto provider =
            parse_translation_provider(value == nullptr ? std::string_view{} : std::string_view(value));
        if (provider.has_value())
        {
            settings.online.translation_provider = *provider;
        }
        else
        {
            invalid = true;
        }
        g_free(value);
    }
    load_string(kTranslationGroup, "target-language", settings.online.translation_target_language,
                valid_translation_language);
    load_string(kTranslationGroup, "endpoint", settings.online.translation_endpoint, valid_https_endpoint);
    load_boolean(kUtilityGroup, "quick-phrase-mode", settings.quick_phrase_mode_enabled);
    load_boolean(kUtilityGroup, "date-time-mode", settings.date_time_mode_enabled);
    load_boolean(kUtilityGroup, "emoji-mode", settings.emoji_mode_enabled);
    load_boolean(kUtilityGroup, "kaomoji-mode", settings.kaomoji_mode_enabled);
    load_boolean(kUtilityGroup, "clipboard-history", settings.clipboard_history_enabled);
    load_boolean(kUtilityGroup, "floating-toolbar", settings.floating_toolbar_enabled);
    load_boolean(kGroup, "show-quanpin-helpcode", settings.show_quanpin_helpcode_in_candidates);
    load_boolean(kGroup, "show-shuangpin-helpcode", settings.show_shuangpin_helpcode_in_candidates);
    load_boolean(kKeybindingsGroup, "switch-language-shift", settings.switch_language_shift);
    load_boolean(kKeybindingsGroup, "switch-language-ctrl", settings.switch_language_ctrl);
    load_boolean(kKeybindingsGroup, "switch-language-ctrl-alt-space", settings.switch_language_ctrl_alt_space);
    load_boolean(kVoiceGroup, "enabled", settings.voice.enabled);
    load_string(kVoiceGroup, "provider", settings.voice.provider,
                [](std::string_view value) { return valid_utf8_text(value, 64, false) && !value.empty(); });
    load_string(kVoiceGroup, "endpoint", settings.voice.endpoint, valid_https_endpoint);
    load_string(kVoiceGroup, "model", settings.voice.model,
                [](std::string_view value) { return valid_utf8_text(value, 256, false) && !value.empty(); });
    load_string(kVoiceGroup, "language", settings.voice.language,
                [](std::string_view value) { return valid_utf8_text(value, 32, false) && !value.empty(); });
    load_boolean(kVoiceGroup, "polish-enabled", settings.voice.polish_enabled);
    load_string(kVoiceGroup, "polish-endpoint", settings.voice.polish_endpoint, valid_https_endpoint);
    load_string(kVoiceGroup, "polish-model", settings.voice.polish_model,
                [](std::string_view value) { return valid_utf8_text(value, 256, false) && !value.empty(); });
    load_string(kVoiceGroup, "polish-prompt", settings.voice.polish_prompt,
                [](std::string_view value) { return valid_utf8_text(value, 8192, true) && !value.empty(); });

    g_key_file_unref(key_file);
    if (invalid)
    {
        set_message(warning, "Some input settings were invalid; defaults were used for those fields.");
    }
    return settings;
}

InputSettings SettingsStore::load(const SecretStore &secret_store, std::string *warning) const
{
    InputSettings settings = load(warning);
    if (settings.online.ai.enabled)
    {
        const char *provider = ai_provider_name(settings.online.ai.provider);
        const SecretLookupResult result = provider == nullptr ? SecretLookupResult{SecretStatus::Unavailable, {}, {}}
                                                              : secret_store.lookup(SecretKind::AiApiToken, provider);
        if (result.status == SecretStatus::Found)
        {
            settings.online.ai.token = result.value;
        }
        else
        {
            // Every AI provider authenticates, so neither answer leaves anything to send and the runtime flag has to
            // come down for both. Clearing `enabled` instead is what the settings window hands straight back to save(),
            // so a keyring that happened to be locked at startup was persisted as the user switching AI off and never
            // asked about again. The intent stays on disk; only this flag and the empty token record that nothing can
            // be sent. What separates the two answers is what the user has to do about it: Unavailable is a service
            // that could not be reached and may hand the same credential over on the next attempt, NotFound is a
            // service that answered and holds no credential at all.
            settings.online.ai_credential_available = false;
            append_message(warning, result.status == SecretStatus::Unavailable
                                        ? "The AI credential could not be read; AI suggestions are inactive until "
                                          "the credential service is reachable."
                                        : "No AI credential is stored for the selected provider; AI suggestions are "
                                          "inactive until one is added.");
        }
    }

    if (settings.online.candidate_translations_enabled &&
        settings.online.translation_provider == TranslationProvider::DeepLX)
    {
        const SecretLookupResult result = secret_store.lookup(
            SecretKind::TranslationApiToken, translation_provider_name(settings.online.translation_provider));
        if (result.status == SecretStatus::Found)
        {
            settings.online.translation_token = result.value;
        }
        else if (result.status == SecretStatus::Unavailable)
        {
            settings.online.translation_credential_available = false;
            append_message(warning, "The translation credential could not be read; translation is inactive until "
                                    "the credential service is reachable.");
        }
        // Translation is the one provider where NotFound is not a fault, which is why it is the only branch that tests
        // the status instead of taking an else: a stored credential simply not existing is a supported DeepLX
        // configuration, because a self-hosted endpoint may accept unauthenticated requests and TranslationProvider
        // sends no Authorization header when the token is empty. Unavailable still comes down, because a service that
        // could not answer cannot tell us whether the endpoint the user configured has a credential waiting for it, and
        // sending the request anyway would be the tokenless call to an authenticating endpoint that this flag exists to
        // prevent. save() draws the same line: it refuses a save that would enable AI or voice with no stored
        // credential and accepts one for translation.
    }
    if (settings.voice.enabled)
    {
        const SecretLookupResult result = secret_store.lookup(SecretKind::VoiceApiToken, settings.voice.provider);
        if (result.status == SecretStatus::Found)
        {
            settings.voice.token = result.value;
        }
        else
        {
            // Voice authenticates like AI does, so the same reading applies to both answers.
            settings.voice_credential_available = false;
            append_message(warning, result.status == SecretStatus::Unavailable
                                        ? "The voice credential could not be read; voice input is inactive until "
                                          "the credential service is reachable."
                                        : "No voice credential is stored for the selected provider; voice input is "
                                          "inactive until one is added.");
        }
    }
    return settings;
}

bool SettingsStore::save(const InputSettings &settings, std::string *error, std::string *digest) const
{
    if (!valid_input_settings(settings))
    {
        set_message(error, "Input settings were outside the supported range.");
        return false;
    }
    ConfigWriteLock lock(config_path_);
    if (!lock)
    {
        set_message(error, "Unable to lock input settings.");
        return false;
    }
    return save_unlocked(settings, error, digest);
}

bool SettingsStore::save_unlocked(const InputSettings &settings, std::string *error, std::string *digest) const
{
    set_message(error, "");
    const char *mode = mode_name(settings.mode);
    const char *default_mode = mode_name(settings.default_mode);
    const char *scheme = scheme_name(settings.scheme);
    const char *punctuation = punctuation_name(settings.punctuation_mode);
    const char *punctuation_lock = punctuation_lock_name(settings.punctuation_lock);
    const char *preedit_style = preedit_style_name(settings.preedit_style);
    const char *candidate_window_layout = candidate_window_layout_name(settings.candidate_window_layout);
    const char *frequency_adjustment = frequency_adjustment_name(settings.frequency_adjustment_mode);
    if (!valid_input_settings(settings))
    {
        set_message(error, "Input settings were outside the supported range.");
        return false;
    }

    const std::filesystem::path directory = config_path_.parent_path();
    if (g_mkdir_with_parents(metasequoia::path_to_utf8(directory).c_str(), 0700) != 0)
    {
        set_message(error, "Unable to create the input settings directory.");
        return false;
    }

    const std::string config_file = metasequoia::path_to_utf8(config_path_);
    GKeyFile *key_file = g_key_file_new();
    GError *load_error = nullptr;
    if (!g_key_file_load_from_file(key_file, config_file.c_str(),
                                   static_cast<GKeyFileFlags>(G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS),
                                   &load_error) &&
        !g_error_matches(load_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
    {
        // load() already treats an unparsable file as "start from the defaults", so refusing to save one left the user
        // with no way out of the settings window: every save failed until config.ini was deleted by hand. Keep the
        // broken text beside it for repair and continue with an empty key file, which costs the unknown keys this
        // branch exists to preserve but only for a file whose keys can no longer be read anyway. A file-access failure
        // still aborts, because the write below would fail too. The regular-file test is not redundant with the domain:
        // GLib reports a config path that is a directory or a device as G_KEY_FILE_ERROR_PARSE ("Not a regular file")
        // rather than as a G_FILE_ERROR, and renaming a whole directory aside to drop a fresh config.ini in its place
        // is not recovering from corrupt text -- it is destroying something this code has no business touching. Only a
        // regular file whose contents no longer parse is repaired here; anything else stays a save failure, so a caller
        // that had already written credentials rolls them back rather than leaving them pointing at a configuration
        // that was never stored.
        const bool unparsable = load_error != nullptr && load_error->domain == G_KEY_FILE_ERROR &&
                                g_file_test(config_file.c_str(), G_FILE_TEST_IS_REGULAR);
        g_clear_error(&load_error);
        if (!unparsable)
        {
            g_key_file_unref(key_file);
            set_message(error, "Unable to preserve the existing input settings.");
            return false;
        }
        g_key_file_unref(key_file);
        key_file = g_key_file_new();
        // A failed rescue is not fatal: the atomic replace below overwrites the unparsable file regardless, and a
        // directory that cannot be written to reports itself there.
        (void)g_rename(config_file.c_str(), (config_file + ".corrupt").c_str());
    }
    g_clear_error(&load_error);

    g_key_file_set_string(key_file, kGroup, "mode", mode);
    g_key_file_set_string(key_file, kGroup, "default-mode", default_mode);
    g_key_file_set_string(key_file, kGroup, "scheme", scheme);
    g_key_file_set_integer(key_file, kGroup, "page-size", static_cast<gint>(settings.page_size));
    g_key_file_set_string(key_file, kGroup, "candidate-window-layout", candidate_window_layout);
    g_key_file_set_string(key_file, kGroup, "punctuation", punctuation);
    g_key_file_set_string(key_file, kGroup, "punctuation-lock", punctuation_lock);
    g_key_file_set_boolean(key_file, kGroup, "full-width", settings.character_width == CharacterWidth::Full);
    g_key_file_set_boolean(key_file, kGroup, "comma-period-paging", settings.comma_period_paging);
    g_key_file_set_boolean(key_file, kGroup, "word-to-character", settings.word_to_character);
    g_key_file_set_boolean(key_file, kGroup, "bracket-paging", settings.bracket_paging);
    g_key_file_set_boolean(key_file, kGroup, "smart-punctuation", settings.smart_punctuation);
    g_key_file_set_boolean(key_file, kGroup, "smart-punctuation-repeat-to-chinese",
                           settings.smart_punctuation_repeat_to_chinese);
    g_key_file_set_boolean(key_file, kGroup, "paired-punctuation", settings.paired_punctuation);
    g_key_file_set_string(key_file, kGroup, "preedit-style", preedit_style);
    g_key_file_set_boolean(key_file, kGroup, "quanpin-helpcode", settings.quanpin_helpcode_enabled);
    g_key_file_set_string(key_file, kGroup, "quanpin-helpcode-schema", settings.quanpin_helpcode_schema.c_str());
    g_key_file_set_boolean(key_file, kGroup, "shuangpin-helpcode", settings.shuangpin_helpcode_enabled);
    g_key_file_set_string(key_file, kGroup, "shuangpin-helpcode-schema", settings.shuangpin_helpcode_schema.c_str());
    g_key_file_set_string(key_file, kGroup, "frequency-adjustment", frequency_adjustment);
    g_key_file_set_integer(key_file, kGroup, "frequency-trigger-count", settings.frequency_trigger_count);
    g_key_file_set_integer(key_file, kGroup, "frequency-linear-step", settings.frequency_linear_step);
    g_key_file_set_boolean(key_file, kGroup, "unicode-mode", settings.unicode_mode_enabled);
    g_key_file_set_boolean(key_file, kGroup, "super-jianpin-mode", settings.super_jianpin_mode_enabled);
    g_key_file_set_boolean(key_file, kGroup, "temporary-english-mode", settings.temporary_english_mode_enabled);
    g_key_file_set_boolean(key_file, kGroup, "temporary-japanese-mode", settings.temporary_japanese_mode_enabled);
    g_key_file_set_boolean(key_file, kGroup, "mixed-english-candidates", settings.mixed_english_candidates_enabled);
    g_key_file_set_integer(key_file, kGroup, "mixed-english-minimum-prefix",
                           static_cast<gint>(settings.mixed_english_minimum_prefix));
    g_key_file_set_boolean(key_file, kGroup, "mixed-emoji-candidates", settings.mixed_emoji_candidates_enabled);
    g_key_file_set_boolean(key_file, kGroup, "mixed-kaomoji-candidates", settings.mixed_kaomoji_candidates_enabled);
    g_key_file_set_boolean(key_file, kUtilityGroup, "clipboard-history", settings.clipboard_history_enabled);
    g_key_file_set_boolean(key_file, kUtilityGroup, "quick-phrase-mode", settings.quick_phrase_mode_enabled);
    g_key_file_set_boolean(key_file, kUtilityGroup, "date-time-mode", settings.date_time_mode_enabled);
    g_key_file_set_boolean(key_file, kUtilityGroup, "emoji-mode", settings.emoji_mode_enabled);
    g_key_file_set_boolean(key_file, kUtilityGroup, "kaomoji-mode", settings.kaomoji_mode_enabled);
    g_key_file_set_boolean(key_file, kUtilityGroup, "floating-toolbar", settings.floating_toolbar_enabled);
    g_key_file_set_boolean(key_file, kGroup, "show-quanpin-helpcode", settings.show_quanpin_helpcode_in_candidates);
    g_key_file_set_boolean(key_file, kGroup, "show-shuangpin-helpcode", settings.show_shuangpin_helpcode_in_candidates);
    g_key_file_set_boolean(key_file, kKeybindingsGroup, "switch-language-shift", settings.switch_language_shift);
    g_key_file_set_boolean(key_file, kKeybindingsGroup, "switch-language-ctrl", settings.switch_language_ctrl);
    g_key_file_set_boolean(key_file, kKeybindingsGroup, "switch-language-ctrl-alt-space",
                           settings.switch_language_ctrl_alt_space);

    g_key_file_set_boolean(key_file, kOnlineGroup, "cloud-enabled", settings.online.cloud_candidates_enabled);
    g_key_file_set_integer(key_file, kOnlineGroup, "connect-timeout-ms",
                           static_cast<gint>(settings.online.connect_timeout.count()));
    g_key_file_set_integer(key_file, kOnlineGroup, "total-timeout-ms",
                           static_cast<gint>(settings.online.total_timeout.count()));
    g_key_file_set_boolean(key_file, kAiGroup, "enabled", settings.online.ai.enabled);
    g_key_file_set_string(key_file, kAiGroup, "provider", ai_provider_name(settings.online.ai.provider));
    g_key_file_set_string(key_file, kAiGroup, "endpoint", settings.online.ai.endpoint.c_str());
    g_key_file_set_string(key_file, kAiGroup, "model", settings.online.ai.model.c_str());
    g_key_file_set_string(key_file, kAiGroup, "prompt", settings.online.ai.prompt.c_str());
    g_key_file_set_integer(key_file, kAiGroup, "candidate-limit",
                           static_cast<gint>(settings.online.ai.candidate_limit));
    g_key_file_set_boolean(key_file, kTranslationGroup, "enabled", settings.online.candidate_translations_enabled);
    g_key_file_set_string(key_file, kTranslationGroup, "provider",
                          translation_provider_name(settings.online.translation_provider));
    g_key_file_set_string(key_file, kTranslationGroup, "target-language",
                          settings.online.translation_target_language.c_str());
    g_key_file_set_string(key_file, kTranslationGroup, "endpoint", settings.online.translation_endpoint.c_str());
    g_key_file_set_boolean(key_file, kVoiceGroup, "enabled", settings.voice.enabled);
    g_key_file_set_string(key_file, kVoiceGroup, "provider", settings.voice.provider.c_str());
    g_key_file_set_string(key_file, kVoiceGroup, "endpoint", settings.voice.endpoint.c_str());
    g_key_file_set_string(key_file, kVoiceGroup, "model", settings.voice.model.c_str());
    g_key_file_set_string(key_file, kVoiceGroup, "language", settings.voice.language.c_str());
    g_key_file_set_boolean(key_file, kVoiceGroup, "polish-enabled", settings.voice.polish_enabled);
    g_key_file_set_string(key_file, kVoiceGroup, "polish-endpoint", settings.voice.polish_endpoint.c_str());
    g_key_file_set_string(key_file, kVoiceGroup, "polish-model", settings.voice.polish_model.c_str());
    g_key_file_set_string(key_file, kVoiceGroup, "polish-prompt", settings.voice.polish_prompt.c_str());

    constexpr const char *secret_keys[]{"token", "api-token", "api-key"};
    for (const char *secret_key : secret_keys)
    {
        g_key_file_remove_key(key_file, kAiGroup, secret_key, nullptr);
        g_key_file_remove_key(key_file, kTranslationGroup, secret_key, nullptr);
        g_key_file_remove_key(key_file, kVoiceGroup, secret_key, nullptr);
    }
    constexpr const char *legacy_online_secret_keys[]{"ai-token", "translation-token", "translation-api-key"};
    for (const char *secret_key : legacy_online_secret_keys)
    {
        g_key_file_remove_key(key_file, kOnlineGroup, secret_key, nullptr);
    }

    gsize data_size = 0;
    GError *data_error = nullptr;
    gchar *data = g_key_file_to_data(key_file, &data_size, &data_error);
    g_key_file_unref(key_file);
    if (data == nullptr)
    {
        g_clear_error(&data_error);
        set_message(error, "Unable to serialize input settings.");
        return false;
    }
    // Reported before the write rather than after it. Every failure from here on may still have replaced the file --
    // the rename is the last step and close() can fail after it has already succeeded -- so a caller that watches
    // config.ini has to be told what this call put there whether or not it goes on to report an error.
    if (digest != nullptr)
    {
        *digest = digest_of(data, data_size);
    }

    std::string temporary_template = metasequoia::path_to_utf8(config_path_) + ".tmp.XXXXXX";
    std::vector<char> temporary_path(temporary_template.begin(), temporary_template.end());
    temporary_path.push_back('\0');
    const int descriptor = g_mkstemp(temporary_path.data());
    if (descriptor < 0)
    {
        g_free(data);
        set_message(error, "Unable to create an atomic input settings file.");
        return false;
    }

    const bool wrote = write_all(descriptor, data, data_size);
    g_free(data);
    const bool synced = wrote && fsync(descriptor) == 0;
    const bool closed = close(descriptor) == 0;
    if (!synced || !closed || g_rename(temporary_path.data(), metasequoia::path_to_utf8(config_path_).c_str()) != 0)
    {
        g_unlink(temporary_path.data());
        set_message(error, "Unable to atomically replace input settings.");
        return false;
    }
    return true;
}

bool SettingsStore::save(const InputSettings &settings, SecretStore &secret_store, std::string *error,
                         std::string *digest) const
{
    if (!valid_input_settings(settings))
    {
        set_message(error, "Input settings were outside the supported range.");
        return false;
    }
    ConfigWriteLock lock(config_path_);
    if (!lock)
    {
        set_message(error, "Unable to lock input settings.");
        return false;
    }
    return save_with_secrets_unlocked(settings, secret_store, error, digest);
}

bool SettingsStore::save_with_secrets_unlocked(const InputSettings &settings, SecretStore &secret_store,
                                               std::string *error, std::string *digest) const
{
    set_message(error, "");
    if (!valid_input_settings(settings))
    {
        set_message(error, "Input settings were outside the supported range.");
        return false;
    }

    struct PendingSecret
    {
        SecretKind kind;
        std::string provider;
        std::string value;
        bool remove = false;
        SecretLookupResult previous;
        bool attempted = false;
    };

    // Which credential belongs to which provider is decided by the configuration that is on disk, not by the one being
    // saved: load() hydrates a token for the provider that was selected then, and the settings window keeps that string
    // in memory when the user picks a different provider.
    const InputSettings persisted = load();
    const char *persisted_ai_name = ai_provider_name(persisted.online.ai.provider);
    const std::string persisted_ai_provider =
        persisted.online.ai.enabled && persisted_ai_name != nullptr ? persisted_ai_name : "";
    const std::string persisted_translation_provider =
        persisted.online.candidate_translations_enabled &&
                persisted.online.translation_provider == TranslationProvider::DeepLX
            ? translation_provider_name(TranslationProvider::DeepLX)
            : "";
    const std::string persisted_voice_provider = persisted.voice.enabled ? persisted.voice.provider : std::string();

    std::vector<PendingSecret> pending;
    // An empty token means "keep whatever Secret Service already holds", not "this provider has no credential": load()
    // hydrates a token only when it can read one, so rejecting an empty one refused every save made while the keyring
    // was locked and made enabling a provider impossible, because nothing in the app can put a credential into these
    // fields in the first place. A token that still equals the one filed under the provider the user just switched away
    // from is that same hydration rather than something meant for the new provider, and storing it would copy one
    // provider's key under another provider's name. `missing_message` is null for a provider that may legitimately run
    // unauthenticated, and for one whose credential the user has just asked to forget -- refusing that save would be
    // this store overruling the request that produced the missing credential it is complaining about.
    const auto plan_credential = [&](SecretKind kind, const std::string &provider, const std::string &token,
                                     const std::string &previous_provider, const char *missing_message) -> bool {
        std::string value = token;
        if (!value.empty() && !previous_provider.empty() && previous_provider != provider)
        {
            const SecretLookupResult hydrated = secret_store.lookup(kind, previous_provider);
            if (hydrated.status == SecretStatus::Unavailable)
            {
                set_message(error, "Unable to access credentials; the provider configuration was not saved.");
                return false;
            }
            if (hydrated.status == SecretStatus::Found && hydrated.value == value)
            {
                value.clear();
            }
        }
        if (!value.empty())
        {
            pending.push_back({kind, provider, std::move(value), false, {}, false});
            return true;
        }
        if (missing_message == nullptr)
        {
            return true;
        }
        // Only a service that answers "there is nothing here" proves the provider has no credential; an unreachable one
        // leaves both the stored entry and this save alone.
        if (secret_store.lookup(kind, provider).status == SecretStatus::NotFound)
        {
            set_message(error, missing_message);
            return false;
        }
        return true;
    };
    // Files a removal once per provider. The two reasons to remove overlap -- a save can both stop using a provider and
    // carry a request to forget its credential -- and the same entry queued twice would have the second erase roll back
    // to the state the first one already left behind.
    const auto plan_removal = [&pending](SecretKind kind, const std::string &provider) {
        if (provider.empty() || std::any_of(pending.begin(), pending.end(), [&](const PendingSecret &secret) {
                return secret.remove && secret.kind == kind && secret.provider == provider;
            }))
        {
            return;
        }
        pending.push_back({kind, provider, {}, true, {}, false});
    };
    // A clear request outranks whatever is in the token field. load() hydrates the stored credential into that field
    // whenever it can read one, so after a clear the two disagree by construction; the settings window resolves it the
    // same way by dropping the token when the box is ticked, and a caller that does not is still asking this store to
    // forget the credential rather than to re-file the copy it was handed.
    const std::string ai_token = settings.online.ai_credential_cleared ? std::string() : settings.online.ai.token;
    const std::string translation_token =
        settings.online.translation_credential_cleared ? std::string() : settings.online.translation_token;
    const std::string voice_token = settings.voice_credential_cleared ? std::string() : settings.voice.token;

    // The provider a clear request names is the one the user is looking at, which is the one being saved rather than
    // the one on disk: the row sits under the provider selector and reports that selector's credential. Resolved
    // whether or not the feature is enabled, because forgetting a credential is not conditional on still using it.
    const char *selected_ai_name = ai_provider_name(settings.online.ai.provider);
    const std::string selected_ai_provider = selected_ai_name == nullptr ? std::string() : selected_ai_name;
    if (settings.online.ai.enabled)
    {
        if (selected_ai_name == nullptr)
        {
            set_message(error, "Unable to store the AI credential; the provider configuration was not saved.");
            return false;
        }
        if (!plan_credential(SecretKind::AiApiToken, selected_ai_provider, ai_token, persisted_ai_provider,
                             settings.online.ai_credential_cleared
                                 ? nullptr
                                 : "No AI credential is stored for the selected provider; the provider configuration "
                                   "was not saved."))
        {
            return false;
        }
    }
    // Only DeepLX authenticates. Pushing the token whenever it was non-empty filed the DeepL secret under
    // provider=local as soon as the user switched backends, where nothing ever reads it again.
    const bool translation_uses_credential = settings.online.candidate_translations_enabled &&
                                             settings.online.translation_provider == TranslationProvider::DeepLX;
    // Only the DeepLX name is ever a credential's owner, so that is the one a clear request names whichever backend is
    // selected: switching to the local backend does not make the stored DeepLX credential somebody else's.
    const char *deeplx_name = translation_provider_name(TranslationProvider::DeepLX);
    const std::string deeplx_provider = deeplx_name == nullptr ? std::string() : deeplx_name;
    if (translation_uses_credential)
    {
        if (deeplx_name == nullptr)
        {
            set_message(error, "Unable to store the translation credential; the provider configuration was not saved.");
            return false;
        }
        // A self-hosted DeepLX endpoint may accept unauthenticated requests, so a missing credential is a valid
        // configuration here rather than a reason to refuse the save.
        if (!plan_credential(SecretKind::TranslationApiToken, deeplx_provider, translation_token, {}, nullptr))
        {
            return false;
        }
    }
    if (settings.voice.enabled)
    {
        if (settings.voice.provider.empty())
        {
            set_message(error, "Unable to store the voice credential; the provider configuration was not saved.");
            return false;
        }
        if (!plan_credential(SecretKind::VoiceApiToken, settings.voice.provider, voice_token, persisted_voice_provider,
                             settings.voice_credential_cleared
                                 ? nullptr
                                 : "No voice credential is stored for the selected provider; the provider "
                                   "configuration was not saved."))
        {
            return false;
        }
    }

    // Turning a provider off is one way a user drops its credential: a save that stops using one removes it. A provider
    // switch deliberately keeps the old entry, because nothing in the app can enter a credential again if the user
    // switches back.
    if (!settings.online.ai.enabled)
    {
        plan_removal(SecretKind::AiApiToken, persisted_ai_provider);
    }
    if (!translation_uses_credential)
    {
        plan_removal(SecretKind::TranslationApiToken, persisted_translation_provider);
    }
    if (!settings.voice.enabled)
    {
        plan_removal(SecretKind::VoiceApiToken, persisted_voice_provider);
    }
    // The other way, and the one that leaves the feature configured: an explicit request to forget the credential the
    // selected provider uses, whether or not the feature stays on.
    if (settings.online.ai_credential_cleared)
    {
        plan_removal(SecretKind::AiApiToken, selected_ai_provider);
    }
    if (settings.online.translation_credential_cleared)
    {
        plan_removal(SecretKind::TranslationApiToken, deeplx_provider);
    }
    if (settings.voice_credential_cleared)
    {
        plan_removal(SecretKind::VoiceApiToken, settings.voice.provider);
    }

    for (PendingSecret &secret : pending)
    {
        secret.previous = secret_store.lookup(secret.kind, secret.provider);
    }
    // An unreachable credential service is fatal for a credential this save has to write, but it only cancels the
    // cleanup of one the configuration no longer uses: refusing to turn a provider off because its abandoned token
    // cannot be reached would leave the provider enabled, which is the opposite of what the user asked for.
    if (std::any_of(pending.begin(), pending.end(), [](const PendingSecret &secret) {
            return !secret.remove && secret.previous.status == SecretStatus::Unavailable;
        }))
    {
        set_message(error, "Unable to access credentials; the provider configuration was not saved.");
        return false;
    }
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                 [](const PendingSecret &secret) {
                                     return secret.remove && secret.previous.status != SecretStatus::Found;
                                 }),
                  pending.end());

    const auto rollback = [&secret_store, &pending]() {
        std::string ignored_diagnostic;
        bool restored = true;
        for (auto secret = pending.rbegin(); secret != pending.rend(); ++secret)
        {
            if (!secret->attempted)
            {
                continue;
            }
            if (secret->previous.status == SecretStatus::Found)
            {
                restored =
                    secret_store.store(secret->kind, secret->provider, secret->previous.value, &ignored_diagnostic) &&
                    restored;
            }
            else
            {
                restored = secret_store.erase(secret->kind, secret->provider, &ignored_diagnostic) && restored;
            }
        }
        return restored;
    };

    std::string ignored_diagnostic;
    for (PendingSecret &secret : pending)
    {
        secret.attempted = true;
        const bool applied = secret.remove
                                 ? secret_store.erase(secret.kind, secret.provider, &ignored_diagnostic)
                                 : secret_store.store(secret.kind, secret.provider, secret.value, &ignored_diagnostic);
        if (!applied)
        {
            if (rollback())
            {
                set_message(error, "Unable to update credentials; the provider configuration was not saved.");
            }
            else
            {
                set_message(error,
                            "Unable to roll back credentials after a save failure; credential state may need repair.");
            }
            return false;
        }
    }

    if (!save_unlocked(settings, error, digest))
    {
        if (!rollback())
        {
            set_message(error,
                        "Unable to roll back credentials after a save failure; credential state may need repair.");
        }
        return false;
    }
    return true;
}

std::optional<SettingsFileVersion> SettingsStore::version() const
{
    gchar *contents = nullptr;
    gsize size = 0;
    GError *error = nullptr;
    if (!g_file_get_contents(metasequoia::path_to_utf8(config_path_).c_str(), &contents, &size, &error))
    {
        const bool missing = g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
        g_clear_error(&error);
        return missing ? std::optional<SettingsFileVersion>{{true, {}}} : std::nullopt;
    }
    auto digest = digest_of(contents, size);
    g_free(contents);
    if (digest.empty())
        return std::nullopt;
    return SettingsFileVersion{false, std::move(digest)};
}

bool SettingsStore::save_if_unchanged(const InputSettings &settings, SecretStore &secret_store,
                                      const SettingsFileVersion &expected, std::string *error) const
{
    if (!valid_input_settings(settings))
    {
        set_message(error, "Input settings were outside the supported range.");
        return false;
    }
    ConfigWriteLock lock(config_path_);
    if (!lock)
    {
        set_message(error, "Unable to lock input settings.");
        return false;
    }
    const auto current = version();
    if (!current || !(*current == expected))
    {
        set_message(error, "本机设置已变化或无法读取，请重新预览。");
        return false;
    }
    return save_with_secrets_unlocked(settings, secret_store, error, nullptr);
}

const std::filesystem::path &SettingsStore::config_path() const
{
    return config_path_;
}
} // namespace metasequoia::linux_ime
