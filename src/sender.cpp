#include "sc/fleet-feed/sender.h"

#include <sc/http/client.h>
#include <sc/kafka/kafka.h>
#include <sc/sftp/sftp.h>

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace sc::fleet_feed {
    namespace {
        constexpr std::size_t detail_size = 500;

        std::string shortened(std::string text) {
            if (text.size() > detail_size) text.resize(detail_size);
            for (auto &c: text)
                if (c == '\n' || c == '\r') c = ' ';
            return text;
        }

        // A JSON string's content, without its quotes.
        std::string json_escaped(const std::string_view text) {
            const auto quoted = nlohmann::json(std::string{text}).dump();
            return quoted.substr(1, quoted.size() - 2);
        }

        void replace_all(std::string &text, const std::string_view what, const std::string &with) {
            for (std::size_t at = 0; (at = text.find(what, at)) != std::string::npos; at += with.size()) text.replace(at, what.size(), with);
        }
    }

    auth_kind auth_kind_from(const std::string_view name) {
        if (name == "none") return auth_kind::none;
        if (name == "header") return auth_kind::header;
        if (name == "url_key") return auth_kind::url_key;
        if (name == "query_key") return auth_kind::query_key;
        if (name == "login_jwt") return auth_kind::login_jwt;
        throw std::invalid_argument("unknown auth \"" + std::string{name} + "\" (none, header, url_key, query_key, login_jwt)");
    }

    const char *auth_kind_name(const auth_kind kind) {
        switch (kind) {
            case auth_kind::none: return "none";
            case auth_kind::header: return "header";
            case auth_kind::url_key: return "url_key";
            case auth_kind::query_key: return "query_key";
            case auth_kind::login_jwt: return "login_jwt";
        }
        return "none";
    }

    std::optional<url_parts> split_url(const std::string_view url) {
        const auto scheme_end = url.find("://");
        if (scheme_end == std::string_view::npos || scheme_end == 0) return std::nullopt;
        url_parts parts;
        parts.scheme = std::string{url.substr(0, scheme_end)};
        const auto rest = url.substr(scheme_end + 3);
        const auto slash = rest.find('/');
        parts.authority = std::string{rest.substr(0, slash)};
        if (slash != std::string_view::npos) parts.path = std::string{rest.substr(slash)};
        if (parts.authority.empty()) return std::nullopt;
        return parts;
    }

    std::string fill_login(const std::string_view body, const std::string_view username, const std::string_view password) {
        std::string out{body};
        replace_all(out, "{username}", json_escaped(username));
        replace_all(out, "{password}", json_escaped(password));
        return out;
    }

    struct sender::kafka_link {
        std::string topic;
        std::unique_ptr<sc::kafka::producer> producer;
    };

    sender::sender(target to) : target_(std::move(to)) {
        const auto parts = split_url(target_.url);
        if (!parts) throw std::invalid_argument(target_.name + ": \"" + target_.url + "\" isn't a URL");
        if (parts->scheme != "http" && parts->scheme != "https" && parts->scheme != "kafka" && parts->scheme != "sftp")
            throw std::invalid_argument(target_.name + ": " + parts->scheme + ":// isn't sent to (http, https, kafka, sftp)");
        if (parts->scheme == "kafka" && parts->path.size() < 2) throw std::invalid_argument(target_.name + ": a kafka:// URL names its topic");
    }

    sender::~sender() = default;

    result sender::send(const std::string &body, const std::string &file_name) {
        const auto parts = split_url(target_.url);
        try {
            if (parts->scheme == "kafka") return produce(body);
            if (parts->scheme == "sftp") return upload(body, file_name);
            return post(body, false);
        } catch (const std::exception &error) {
            return {false, 0, shortened(error.what())};
        }
    }

    std::optional<std::string> sender::token(std::string &problem) {
        if (!token_.empty() && std::chrono::steady_clock::now() - token_at_ < target_.token_lifetime) return token_;
        token_.clear();
        sc::http::client login{target_.login_url};
        login.timeout(static_cast<long>(target_.timeout.count())).quiet();
        login.header("Content-Type", "application/json");
        login.header("Accept", "application/json");
        const auto answer = login.post(fill_login(target_.login_body, target_.username, target_.secret));
        if (login.status() < 200 || login.status() > 299) {
            problem = "login: " + (login.status() ? "HTTP " + std::to_string(login.status()) + " " + shortened(answer) : login.error());
            return std::nullopt;
        }
        const auto parsed = nlohmann::json::parse(answer, nullptr, false);
        if (!parsed.is_object() || !parsed.contains(target_.token_field) || !parsed[target_.token_field].is_string()) {
            problem = "login: no \"" + target_.token_field + "\" in the answer";
            return std::nullopt;
        }
        token_ = parsed[target_.token_field].get<std::string>();
        token_at_ = std::chrono::steady_clock::now();
        return token_;
    }

    result sender::post(const std::string &body, const bool retried) {
        std::string url = target_.url;
        if (target_.auth == auth_kind::url_key) url += target_.secret;
        sc::http::client http{url};
        http.timeout(static_cast<long>(target_.timeout.count())).connect_timeout(10).quiet();
        if (target_.auth == auth_kind::query_key) http.query(target_.auth_name, target_.secret);
        http.header("Content-Type", "application/json");
        http.header("Accept", "application/json");
        for (const auto &[name, value]: target_.headers) http.header(name, value);
        if (target_.auth == auth_kind::header) http.header(target_.auth_name, target_.auth_prefix + target_.secret);
        if (target_.auth == auth_kind::login_jwt) {
            std::string problem;
            const auto bearer = token(problem);
            if (!bearer) return {false, 0, problem};
            http.bearer(*bearer);
        }
        const auto answer = http.post(body);
        const long status = http.status();
        // A token that ran out before its time: one new login, once.
        if (status == 401 && target_.auth == auth_kind::login_jwt && !retried) {
            token_.clear();
            return post(body, true);
        }
        if (status == 0) return {false, 0, http.error().empty() ? "no answer" : shortened(http.error())};
        return {status >= 200 && status <= 299, status, shortened(answer)};
    }

    result sender::produce(const std::string &body) {
        if (!kafka_) {
            const auto parts = split_url(target_.url);
            auto link = std::make_unique<kafka_link>();
            link->topic = parts->path.substr(1);
            const auto security = sc::kafka::security::from(target_.kafka_security, target_.kafka_mechanism, target_.username, target_.secret);
            link->producer = std::make_unique<sc::kafka::producer>(sc::ip_endpoints{parts->authority, 9092}, "status-push", security);
            kafka_ = std::move(link);
        }
        const auto failed_before = kafka_->producer->failed();
        kafka_->producer->send(kafka_->topic, "", body);
        if (!kafka_->producer->flush(target_.timeout)) return {false, 0, "Kafka: not delivered within " + std::to_string(target_.timeout.count()) + "s"};
        if (kafka_->producer->failed() != failed_before) return {false, 0, "Kafka refused the record"};
        return {true, 0, "delivered to " + kafka_->topic};
    }

    result sender::upload(const std::string &body, const std::string &file_name) {
        if (file_name.empty() || file_name.find('/') != std::string::npos) return {false, 0, "sftp: no file name"};
        const auto parts = split_url(target_.url);
        sc::sftp::options how;
        const auto colon = parts->authority.rfind(':');
        how.host = parts->authority.substr(0, colon);
        if (colon != std::string::npos) how.port = static_cast<std::uint16_t>(std::stoi(parts->authority.substr(colon + 1)));
        how.user = target_.username;
        if (!target_.sftp_key.empty()) {
            how.key = target_.sftp_key;
            how.passphrase = target_.secret;
        } else {
            how.password = target_.secret;
        }
        if (!target_.known_hosts.empty()) how.known_hosts = target_.known_hosts;
        how.timeout = target_.timeout;
        sc::sftp::client sftp{how};
        sftp.connect();
        auto folder = parts->path.empty() ? std::string{"."} : parts->path;
        while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
        const auto remote = folder + "/" + file_name;
        sftp.write(remote, body);
        sftp.disconnect();
        return {true, 0, "wrote " + remote};
    }
}
