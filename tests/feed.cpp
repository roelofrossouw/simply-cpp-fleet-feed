#include "sc_test.h"

#include <sc/fleet-feed/feed.h>

#include <cstdlib>

using namespace sc::fleet_feed;

namespace {
    // By value: the checks bind what they compare by reference, past a temporary optional's life.
    double num(const std::string_view text) { return number(text).value_or(-12345); }
    std::string loc(const std::string_view text) { return local_text(text).value_or("none"); }
}

int main() {
    setenv("TZ", "Africa/Johannesburg", 1);
    tzset();

    SECTION("vms_device");
    CHECK(vms_device("100000004366"));
    CHECK(vms_device("00002"));
    CHECK(!vms_device("353691845392090"));
    CHECK(!vms_device("AT12345"));

    SECTION("unquoted and number");
    CHECK_EQ(unquoted("\"28.333157\""), std::string{"28.333157"});
    CHECK_EQ(unquoted("133"), std::string{"133"});
    CHECK_EQ(unquoted("\""), std::string{"\""});
    CHECK_EQ(num("\"206490.8\""), 206490.8);
    CHECK_EQ(num(" 7 "), 7.0);
    CHECK(!number(""));
    CHECK(!number("\"\""));
    CHECK(!number("abc"));
    CHECK(!number("12x"));
    CHECK(!number("nan"));

    SECTION("iso_time");
    const auto sast = iso_time("2026-11-26T09:17:14+02:00");
    const auto utc = iso_time("2026-11-26T07:17:14Z");
    CHECK(sast.has_value());
    CHECK(utc.has_value());
    CHECK(sast == utc);
    CHECK(iso_time("2026-11-26T07:17:14.250Z") == *utc + std::chrono::milliseconds{250});
    CHECK(iso_time("2026-11-26T09:17:14+0200") == utc);
    CHECK_EQ(std::chrono::duration_cast<std::chrono::seconds>(utc->time_since_epoch()).count(), 1795677434LL);
    CHECK(!iso_time("2026-11-26 09:17:14"));       // no offset
    CHECK(!iso_time("2026-11-26T09:17:14+02:00x"));
    CHECK(!iso_time("2026-13-26T09:17:14Z"));
    CHECK(!iso_time(""));

    SECTION("local_text");
    CHECK_EQ(loc("2026-11-23 13:53:35"), std::string{"2026-11-23 13:53:35"});
    CHECK_EQ(loc("2026-11-23T11:39:48.272"), std::string{"2026-11-23 11:39:48"});
    CHECK_EQ(loc("2026-11-26T09:17:14+02:00"), std::string{"2026-11-26 09:17:14"});
    CHECK(!local_text("2026-11-23 13:53"));
    CHECK(!local_text("2026-11-23 13:53:35 junk"));
    CHECK(!local_text("2026-02-30 10:00:00"));

    SECTION("valid_coordinates");
    CHECK(valid_coordinates(-26.517504, 28.333157));
    CHECK(!valid_coordinates(0, 28.3));
    CHECK(!valid_coordinates(-26.5, 0));
    CHECK(!valid_coordinates(1, 28.3));
    CHECK(!valid_coordinates(-26.5, -1));
    CHECK(!valid_coordinates(91, 28.3));
    CHECK(!valid_coordinates(-26.5, 181));

    SECTION("number_text");
    CHECK_EQ(number_text(28.333157), std::string{"28.333157"});
    CHECK_EQ(number_text(-26.5), std::string{"-26.5"});
    CHECK_EQ(number_text(0.0), std::string{"0"});
    CHECK_EQ(number_text(-0.0000001), std::string{"0"});
    CHECK_EQ(number_text(206490.84, 1), std::string{"206490.8"});
    CHECK_EQ(number_text(133.4, 0), std::string{"133"});

    SECTION("from_tac");
    const auto now = *iso_time("2026-11-26T07:20:00Z");
    const std::map<std::string, std::string> tac{
        {"gpstime", "2026-11-26T09:17:14+02:00"}, {"lat", "-26.204103"}, {"lng", "28.047305"}, {"speed", "54"}, {"ang", "271"},
        {"odometer", "123456789"}, {"ignition", "1"}, {"ping", "2026-11-26T09:19:30+02:00"}};
    const auto tracker = from_tac("353691845392090", tac, now);
    CHECK(tracker.has_value());
    CHECK_EQ(tracker->time, std::string{"2026-11-26 09:17:14"});
    CHECK_EQ(tracker->latitude, -26.204103);
    CHECK_EQ(*tracker->mileage, 123456.789);
    CHECK_EQ(*tracker->acc, true);
    CHECK_EQ(*tracker->online, true); // pinged 30 s ago
    CHECK(tracker->tracker);
    CHECK(has_ping(tac));
    auto old_ping = tac;
    old_ping["ping"] = "2026-11-26T08:00:00+02:00";
    CHECK_EQ(*from_tac("x", old_ping, now)->online, false);
    auto told = tac;
    told["online"] = "0";
    CHECK_EQ(*from_tac("x", told, now)->online, false);
    auto no_fix = tac;
    no_fix["lat"] = "0";
    CHECK(!from_tac("x", no_fix, now));
    no_fix = tac;
    no_fix.erase("gpstime");
    CHECK(!from_tac("x", no_fix, now));
    CHECK(!has_ping({{"gpstime", "x"}}));

    SECTION("from_drtp");
    // Fields as they are on the VMS' Redis (drtp:100000004366, 2026-11-26).
    const std::map<std::string, std::string> drtp{
        {"positioningTime", "\"2026-11-23 13:53:35\""}, {"latitude", "\"26.517504\""}, {"longitude", "\"28.333157\""},
        {"south", "1"}, {"west", "0"}, {"speed", "\"0.0\""}, {"direction", "133"}, {"mileage", "\"206490.8\""},
        {"accStatus", "0"}, {"onlineStatus", "0"}, {"licenseNum", "\"ML203\""}, {"temperatureSensor", ""}};
    const auto camera = from_drtp("100000004366", drtp);
    CHECK(camera.has_value());
    CHECK_EQ(camera->latitude, -26.517504);
    CHECK_EQ(camera->longitude, 28.333157);
    CHECK_EQ(camera->time, std::string{"2026-11-23 13:53:35"});
    CHECK_EQ(camera->registration, std::string{"ML203"});
    CHECK_EQ(*camera->mileage, 206490.8);
    CHECK_EQ(*camera->course, 133.0);
    CHECK_EQ(*camera->acc, false);
    CHECK_EQ(*camera->online, false);
    CHECK(!camera->tracker);
    auto unplaced = drtp;
    unplaced["latitude"] = "\"0.0\"";
    CHECK(!from_drtp("x", unplaced));

    SECTION("device_entry and device_list");
    const auto entry = device_entry(*camera);
    CHECK_EQ(entry.dump(),
             std::string{R"({"deviceNr":"100000004366","regNr":"ML203","fleetNr":"","gpstime":"2026-11-23 13:53:35","latitude":"-26.517504",)"
                         R"("longitude":"28.333157","speed":"0","mileage":"206490.8","acc_state":"0","online":"0","course":"133"})"});
    auto with_fleet = *tracker;
    with_fleet.registration = "AB12CDGP";
    with_fleet.fleet_number = "FL\"7";
    const auto list = device_list("3077", {with_fleet});
    CHECK_EQ(list.dump(),
             std::string{R"({"clientID":"3077","deviceList":[{"deviceNr":"353691845392090","regNr":"AB12CDGP","fleetNr":"FL\"7",)"
                         R"("gpstime":"2026-11-26 09:17:14","latitude":"-26.204103","longitude":"28.047305","speed":"54",)"
                         R"("mileage":"123456.8","acc_state":"1","online":"1","course":"271","temp1":"","temp2":""}]})"});
    CHECK_EQ(device_list("1", {}).dump(), std::string{R"({"clientID":"1","deviceList":[]})"});

    SECTION("sams_value");
    const auto sams = sams_value({{"2507", {*camera}}, {"2939", {}}});
    const auto once = nlohmann::json::parse(sams);
    CHECK(once.is_string()); // encoded twice
    const auto twice = nlohmann::json::parse(once.get<std::string>());
    CHECK(twice.is_array());
    CHECK_EQ(twice.size(), std::size_t{1});
    CHECK_EQ(twice[0]["clientID"].get<std::string>(), std::string{"2507"});
    CHECK_EQ(twice[0]["deviceList"][0]["deviceNr"].get<std::string>(), std::string{"100000004366"});
    CHECK_EQ(sams_value({}), std::string{R"("[]")"});

    SECTION("dhl_points");
    auto unit = *tracker;
    unit.registration = "BULL01";
    auto bad = *camera;
    bad.latitude = 0;
    const auto dhl = dhl_points("1431", {unit, bad, *camera});
    CHECK(dhl.has_value());
    CHECK_EQ(*dhl, std::string{R"({"clientID":"1431","deviceList":[)"
                               R"({"GpsTimeStamp":"2026-11-26 09:17:14","OdoKm":"123456.8","SpeedKmpH":"54","UnitCode":"BULL01","X":"28.047305","Y":"-26.204103"},)"
                               R"({"GpsTimeStamp":"2026-11-23 13:53:35","OdoKm":"206490.8","SpeedKmpH":"0","UnitCode":"ML203","X":"28.333157","Y":"-26.517504"}]})"});
    CHECK(!dhl_points("1431", {bad}));
    auto nameless = *camera;
    nameless.registration.clear();
    CHECK(dhl_points("1431", {nameless})->find(R"("UnitCode":"100000004366")") != std::string::npos);

    SECTION("dhl_file_name");
    CHECK_EQ(dhl_file_name("1431", *utc), std::string{"dhl_1431_20261126091714.json"});

    TEST_SUMMARY();
}
