#include "sc_test.h"

#include <sc/fleet-feed/sender.h>

using namespace sc::fleet_feed;

// The parts that need no receiver; deliveries are tried by status-push's tests against a local
// server.
int main() {
    SECTION("auth_kind");
    CHECK(auth_kind_from("none") == auth_kind::none);
    CHECK(auth_kind_from("header") == auth_kind::header);
    CHECK(auth_kind_from("url_key") == auth_kind::url_key);
    CHECK(auth_kind_from("query_key") == auth_kind::query_key);
    CHECK(auth_kind_from("login_jwt") == auth_kind::login_jwt);
    CHECK_THROWS_AS(auth_kind_from("digest"), std::invalid_argument);
    CHECK_EQ(std::string{auth_kind_name(auth_kind::login_jwt)}, std::string{"login_jwt"});

    SECTION("split_url");
    const auto http = split_url("https://prd.example.com/truck-assist/tracking");
    CHECK(http.has_value());
    CHECK_EQ(http->scheme, std::string{"https"});
    CHECK_EQ(http->authority, std::string{"prd.example.com"});
    CHECK_EQ(http->path, std::string{"/truck-assist/tracking"});
    const auto kafka = split_url("kafka://b1.example.com:9092;b2.example.com:9092/source-truckassist");
    CHECK_EQ(kafka->authority, std::string{"b1.example.com:9092;b2.example.com:9092"});
    CHECK_EQ(kafka->path, std::string{"/source-truckassist"});
    CHECK_EQ(split_url("sftp://sftp.example.com:2222")->path, std::string{});
    CHECK(!split_url("example.com/x"));
    CHECK(!split_url("http:///x"));

    SECTION("fill_login");
    CHECK_EQ(fill_login(R"({"email": "{username}", "password": "{password}"})", "a@example.com", R"(p"w\1)"),
             std::string{R"({"email": "a@example.com", "password": "p\"w\\1"})"});

    SECTION("sender checks its URL");
    CHECK_THROWS_AS(sender(target{.name = "x", .url = "ftp://example.com/"}), std::invalid_argument);
    CHECK_THROWS_AS(sender(target{.name = "x", .url = "kafka://b1.example.com:9092"}), std::invalid_argument);
    CHECK_THROWS_AS(sender(target{.name = "x", .url = "nothing"}), std::invalid_argument);
    CHECK_NOTHROW(sender(target{.name = "x", .url = "https://example.com/hook"}));

    SECTION("an unreachable receiver is a result, not an exception");
    sender nowhere{target{.name = "nowhere", .url = "http://127.0.0.1:9/hook", .timeout = std::chrono::seconds{2}}};
    const auto got = nowhere.send("{}");
    CHECK(!got.ok);
    CHECK_EQ(got.status, 0L);
    CHECK(!got.detail.empty());
    sender no_file{target{.name = "sftp", .url = "sftp://127.0.0.1:9/out"}};
    CHECK(!no_file.send("{}", "").ok);
    CHECK(!no_file.send("{}", "../x").ok);

    TEST_SUMMARY();
}
