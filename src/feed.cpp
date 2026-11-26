#include "sc/fleet-feed/feed.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace sc::fleet_feed {
    namespace {
        std::string_view trimmed(std::string_view text) {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
            return text;
        }

        // A field's value without the VMS' quotes, or "" when it isn't there.
        std::string field(const std::map<std::string, std::string> &fields, const char *name) {
            const auto found = fields.find(name);
            if (found == fields.end()) return {};
            return unquoted(trimmed(found->second));
        }

        std::optional<double> number_field(const std::map<std::string, std::string> &fields, const char *name) {
            return number(field(fields, name));
        }

        std::optional<bool> flag_field(const std::map<std::string, std::string> &fields, const char *name) {
            const auto value = number_field(fields, name);
            if (!value) return std::nullopt;
            return *value != 0;
        }

        bool digits(const std::string_view text, const std::size_t at, const std::size_t count) {
            if (at + count > text.size()) return false;
            for (std::size_t i = at; i < at + count; ++i)
                if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
            return true;
        }

        int value_of(const std::string_view text, const std::size_t at, const std::size_t count) {
            int value = 0;
            for (std::size_t i = at; i < at + count; ++i) value = value * 10 + (text[i] - '0');
            return value;
        }

        // "YYYY-MM-DD?HH:MM:SS" at the start of text (? a space or T), the parts checked.
        bool date_time(const std::string_view text, int &year, int &month, int &day, int &hour, int &minute, int &second) {
            if (text.size() < 19 || !digits(text, 0, 4) || text[4] != '-' || !digits(text, 5, 2) || text[7] != '-' || !digits(text, 8, 2) ||
                (text[10] != ' ' && text[10] != 'T') || !digits(text, 11, 2) || text[13] != ':' || !digits(text, 14, 2) || text[16] != ':' ||
                !digits(text, 17, 2)) {
                return false;
            }
            year = value_of(text, 0, 4);
            month = value_of(text, 5, 2);
            day = value_of(text, 8, 2);
            hour = value_of(text, 11, 2);
            minute = value_of(text, 14, 2);
            second = value_of(text, 17, 2);
            static constexpr int month_days[]{31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return year >= 1970 && month >= 1 && month <= 12 && day >= 1 && day <= month_days[month - 1] && hour <= 23 && minute <= 59 &&
                   second <= 60;
        }

        // Past "YYYY-MM-DD?HH:MM:SS": an optional fraction; returns where it ends.
        std::size_t after_fraction(const std::string_view text) {
            std::size_t at = 19;
            if (at < text.size() && text[at] == '.') {
                ++at;
                const auto first = at;
                while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) ++at;
                if (at == first) return std::string_view::npos;
            }
            return at;
        }

        // Days from 1970-01-01 to a civil date (Howard Hinnant's algorithm).
        long long days_from_civil(int year, const int month, const int day) {
            year -= month <= 2;
            const long long era = (year >= 0 ? year : year - 399) / 400;
            const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
            const unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
            const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
            return era * 146097 + static_cast<long long>(day_of_era) - 719468;
        }

        std::string text_of(const std::optional<double> value, const int decimals = 6) {
            return value ? number_text(*value, decimals) : std::string{};
        }

        std::string text_of(const std::optional<bool> value) {
            if (!value) return {};
            return *value ? "1" : "0";
        }
    }

    bool vms_device(const std::string_view device) {
        return device.starts_with("100") || device.starts_with("0");
    }

    std::string unquoted(const std::string_view text) {
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"') return std::string{text.substr(1, text.size() - 2)};
        return std::string{text};
    }

    std::optional<double> number(const std::string_view text) {
        const auto clean = unquoted(trimmed(text));
        if (clean.empty()) return std::nullopt;
        char *end = nullptr;
        const double value = std::strtod(clean.c_str(), &end);
        if (end != clean.c_str() + clean.size() || !std::isfinite(value)) return std::nullopt;
        return value;
    }

    std::optional<clock::time_point> iso_time(const std::string_view text) {
        int year, month, day, hour, minute, second;
        if (!date_time(text, year, month, day, hour, minute, second) || text[10] != 'T') return std::nullopt;
        auto at = after_fraction(text);
        if (at == std::string_view::npos || at >= text.size()) return std::nullopt;
        long long offset_s = 0;
        if (text[at] == 'Z') {
            ++at;
        } else if (text[at] == '+' || text[at] == '-') {
            const int sign = text[at] == '-' ? -1 : 1;
            if (!digits(text, at + 1, 2)) return std::nullopt;
            const int hours = value_of(text, at + 1, 2);
            at += 3;
            int minutes = 0;
            if (at < text.size() && text[at] == ':') ++at;
            if (digits(text, at, 2)) {
                minutes = value_of(text, at, 2);
                at += 2;
            }
            offset_s = sign * (hours * 3600LL + minutes * 60LL);
        } else {
            return std::nullopt;
        }
        if (at != text.size()) return std::nullopt;
        std::chrono::milliseconds fraction{0};
        if (text.size() > 19 && text[19] == '.') {
            std::string digits_text;
            for (std::size_t i = 20; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i) digits_text += text[i];
            digits_text.resize(3, '0');
            fraction = std::chrono::milliseconds{std::atoi(digits_text.c_str())};
        }
        const long long seconds = days_from_civil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL + second - offset_s;
        return clock::time_point{std::chrono::seconds{seconds}} + fraction;
    }

    std::optional<std::string> local_text(const std::string_view text) {
        const auto clean = trimmed(text);
        int year, month, day, hour, minute, second;
        if (!date_time(clean, year, month, day, hour, minute, second)) return std::nullopt;
        // What may follow: a fraction, then (ISO) an offset, which is left as written: the wall time
        // is the device's local time, as the receivers have always had it.
        const auto at = after_fraction(clean);
        if (at == std::string_view::npos) return std::nullopt;
        if (at < clean.size()) {
            const auto rest = clean.substr(at);
            const bool offset = rest == "Z" || ((rest.front() == '+' || rest.front() == '-') && rest.size() >= 3 && rest.size() <= 6);
            if (!offset || clean[10] != 'T') return std::nullopt;
        }
        std::string out{clean.substr(0, 19)};
        out[10] = ' ';
        return out;
    }

    bool valid_coordinates(const double latitude, const double longitude) {
        const auto bad = [](const double value, const double limit) {
            return !std::isfinite(value) || value == 0 || value == 1 || value == -1 || std::fabs(value) > limit;
        };
        return !bad(latitude, 90) && !bad(longitude, 180);
    }

    std::optional<position> from_tac(const std::string &device, const std::map<std::string, std::string> &fields, const clock::time_point now,
                                     const std::chrono::seconds online_for) {
        const auto time = local_text(field(fields, "gpstime"));
        const auto latitude = number_field(fields, "lat");
        const auto longitude = number_field(fields, "lng");
        if (!time || !latitude || !longitude || !valid_coordinates(*latitude, *longitude)) return std::nullopt;
        position out;
        out.device = device;
        out.tracker = true;
        out.time = *time;
        out.latitude = *latitude;
        out.longitude = *longitude;
        out.speed = number_field(fields, "speed");
        out.course = number_field(fields, "ang");
        if (const auto metres = number_field(fields, "odometer")) out.mileage = *metres / 1000.0;
        out.acc = flag_field(fields, "ignition");
        out.online = flag_field(fields, "online");
        if (!out.online) {
            if (const auto ping = iso_time(field(fields, "ping"))) out.online = now - *ping <= online_for;
        }
        out.temp1 = number_field(fields, "temp1");
        out.temp2 = number_field(fields, "temp2");
        return out;
    }

    bool has_ping(const std::map<std::string, std::string> &fields) {
        return !field(fields, "ping").empty();
    }

    std::optional<position> from_drtp(const std::string &device, const std::map<std::string, std::string> &fields) {
        const auto time = local_text(field(fields, "positioningTime"));
        auto latitude = number_field(fields, "latitude");
        auto longitude = number_field(fields, "longitude");
        if (!time || !latitude || !longitude) return std::nullopt;
        // The VMS keeps the coordinates unsigned, with flags for the hemispheres.
        if (field(fields, "south") == "1") *latitude = -*latitude;
        if (field(fields, "west") == "1") *longitude = -*longitude;
        if (!valid_coordinates(*latitude, *longitude)) return std::nullopt;
        position out;
        out.device = device;
        out.time = *time;
        out.latitude = *latitude;
        out.longitude = *longitude;
        out.registration = field(fields, "licenseNum");
        out.speed = number_field(fields, "speed");
        out.course = number_field(fields, "direction");
        out.mileage = number_field(fields, "mileage");
        out.acc = flag_field(fields, "accStatus");
        out.online = flag_field(fields, "onlineStatus");
        return out;
    }

    std::string number_text(const double value, const int decimals) {
        char text[64];
        std::snprintf(text, sizeof text, "%.*f", decimals < 0 ? 0 : decimals, value);
        std::string out{text};
        if (out.find('.') != std::string::npos) {
            while (out.back() == '0') out.pop_back();
            if (out.back() == '.') out.pop_back();
        }
        if (out == "-0") out = "0";
        return out;
    }

    nlohmann::ordered_json device_entry(const position &at) {
        nlohmann::ordered_json entry{
            {"deviceNr", at.device},
            {"regNr", at.registration},
            {"fleetNr", at.fleet_number},
            {"gpstime", at.time},
            {"latitude", number_text(at.latitude)},
            {"longitude", number_text(at.longitude)},
            {"speed", text_of(at.speed, 1)},
            {"mileage", text_of(at.mileage, 1)},
            {"acc_state", text_of(at.acc)},
            {"online", text_of(at.online)},
            {"course", text_of(at.course, 0)}};
        if (at.tracker) {
            entry["temp1"] = text_of(at.temp1, 1);
            entry["temp2"] = text_of(at.temp2, 1);
        }
        return entry;
    }

    nlohmann::ordered_json device_list(const std::string &account, const std::vector<position> &devices) {
        auto list = nlohmann::ordered_json::array();
        for (const auto &each: devices) list.push_back(device_entry(each));
        return {{"clientID", account}, {"deviceList", std::move(list)}};
    }

    std::string sams_value(const std::vector<std::pair<std::string, std::vector<position>>> &accounts) {
        auto all = nlohmann::ordered_json::array();
        for (const auto &[account, devices]: accounts)
            if (!devices.empty()) all.push_back(device_list(account, devices));
        // kafka.py sent json.dumps() of the file's text: a JSON string holding the JSON.
        return nlohmann::json(all.dump()).dump();
    }

    std::optional<std::string> dhl_points(const std::string &account, const std::vector<position> &devices) {
        auto list = nlohmann::json::array(); // nlohmann::json sorts its keys, as the Perl's canonical JSON did
        for (const auto &each: devices) {
            if (!valid_coordinates(each.latitude, each.longitude)) continue;
            list.push_back({{"UnitCode", each.registration.empty() ? each.device : each.registration},
                            {"X", number_text(each.longitude)},
                            {"Y", number_text(each.latitude)},
                            {"GpsTimeStamp", each.time},
                            {"SpeedKmpH", text_of(each.speed, 1)},
                            {"OdoKm", text_of(each.mileage, 1)}});
        }
        if (list.empty()) return std::nullopt;
        return nlohmann::json{{"clientID", account}, {"deviceList", std::move(list)}}.dump();
    }

    std::string dhl_file_name(const std::string &account, const clock::time_point at) {
        const auto seconds = clock::to_time_t(at);
        std::tm local{};
        localtime_r(&seconds, &local);
        char text[32];
        std::strftime(text, sizeof text, "%Y%m%d%H%M%S", &local);
        return "dhl_" + account + "_" + text + ".json";
    }
}
