#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Delivering a payload to a receiving system: an HTTP POST (with the sign-in styles fleet systems
// use), a Kafka record, or a file on an SFTP server.
namespace sc::fleet_feed {
    // How an HTTP receiver is signed in to.
    enum class auth_kind {
        none,
        header,    // a fixed key in a header: auth_name (Authorization) = auth_prefix + secret
        url_key,   // the key at the end of the URL's path (url + secret)
        query_key, // the key as a query parameter: ?<auth_name>=<secret>
        login_jwt  // POST login_body to login_url, take token_field from the answer, send "Bearer <token>"
    };

    // "none", "header", "url_key", "query_key", "login_jwt"; throws std::invalid_argument for others.
    [[nodiscard]] auth_kind auth_kind_from(std::string_view name);
    [[nodiscard]] const char *auth_kind_name(auth_kind kind);

    struct target {
        std::string name; // for messages
        // http(s)://host/path, kafka://broker:port[;broker:port]/topic, or sftp://host[:port]/folder.
        std::string url;
        auth_kind auth = auth_kind::none;
        std::string auth_name = "Authorization"; // header: the header; query_key: the parameter
        std::string auth_prefix;                 // header: written before the secret ("Bearer ")
        std::string username;                    // login_jwt: the login's; kafka: SASL's; sftp: the user
        std::string secret;                      // the key, the token, or the password
        // login_jwt: the login request; {username} and {password} are replaced (JSON-escaped).
        std::string login_url;
        std::string login_body = R"({"username": "{username}", "password": "{password}"})";
        std::string token_field = "token";
        std::chrono::seconds token_lifetime{3600}; // a token is used this long, or until a 401
        std::vector<std::pair<std::string, std::string>> headers; // fixed extra headers ({"ClientID", "RIMS"})
        std::chrono::seconds timeout{30};
        // kafka: plaintext, ssl, sasl_plaintext or sasl_ssl, and the SASL mechanism.
        std::string kafka_security = "plaintext";
        std::string kafka_mechanism = "PLAIN";
        // sftp: a private key instead of the password, and the known_hosts file (empty: the user's).
        std::filesystem::path sftp_key;
        std::filesystem::path known_hosts;
    };

    // What became of one delivery.
    struct result {
        bool ok = false;
        long status = 0;    // the HTTP status; 0 for Kafka, SFTP, or no answer
        std::string detail; // the answer's first 500 bytes, or the error
    };

    // A URL split up: scheme, host[:port] part, path (with its leading '/', may be empty).
    struct url_parts {
        std::string scheme;
        std::string authority;
        std::string path;
    };
    [[nodiscard]] std::optional<url_parts> split_url(std::string_view url);

    // body with {username} and {password} replaced by the values, JSON-escaped (without quotes).
    [[nodiscard]] std::string fill_login(std::string_view body, std::string_view username, std::string_view password);

    // Delivers payloads to one target. Not for use from several threads at once.
    class sender {
    public:
        explicit sender(target to);
        ~sender();

        sender(const sender &) = delete;
        sender &operator=(const sender &) = delete;

        // HTTP: POST body as application/json. Kafka: one record, no key, waited for. SFTP: written
        // as <folder>/<file_name> (through a temporary name). Never throws for the receiver's
        // failures: they are the result.
        result send(const std::string &body, const std::string &file_name = {});

        [[nodiscard]] const target &settings() const { return target_; }

    private:
        result post(const std::string &body, bool retried);
        result produce(const std::string &body);
        result upload(const std::string &body, const std::string &file_name);
        std::optional<std::string> token(std::string &problem);

        target target_;
        std::string token_;
        std::chrono::steady_clock::time_point token_at_{};
        struct kafka_link;
        std::unique_ptr<kafka_link> kafka_;
    };
}
