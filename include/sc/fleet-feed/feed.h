#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Device positions for third-party fleet systems: a device's latest position read from the status
// hashes kept in Redis (a tracker's tac:<id>, a VMS camera unit's drtp:<id>), and the payloads the
// receiving systems take (a device list per account, SAMS's array, DHL's GPS points).
namespace sc::fleet_feed {
    using clock = std::chrono::system_clock;

    // One device's latest position, in the terms of the device list the receivers know.
    struct position {
        std::string device;        // deviceNr: the IMEI or the VMS device number
        std::string registration;  // regNr
        std::string fleet_number;  // fleetNr
        std::string time;          // gpstime: local time, "YYYY-MM-DD HH:MM:SS"
        double latitude = 0;
        double longitude = 0;
        std::optional<double> speed;   // km/h
        std::optional<double> course;  // degrees from north
        std::optional<double> mileage; // km
        std::optional<bool> acc;       // ignition
        std::optional<bool> online;
        std::optional<double> temp1;   // degrees C
        std::optional<double> temp2;
        bool tracker = false;          // from tac: (a tracker), not drtp: (a camera unit): its list entry has temp1/temp2
    };

    // A VMS device (its id starts with 100 or 0): its status is drtp:<id> on the VMS's Redis,
    // anything else's is tac:<id> on the cluster.
    [[nodiscard]] bool vms_device(std::string_view device);

    // The VMS keeps its hash values as JSON text: "\"28.333157\"" is 28.333157. Bare values are
    // returned as they are.
    [[nodiscard]] std::string unquoted(std::string_view text);

    // A number from a status field ("28.33", "\"28.33\"", " 7 "); none for empty or other text.
    [[nodiscard]] std::optional<double> number(std::string_view text);

    // An ISO 8601 time with its offset ("2026-11-26T09:17:14+02:00", "...14.250Z") as the moment
    // it is; none for other text.
    [[nodiscard]] std::optional<clock::time_point> iso_time(std::string_view text);

    // A local time ("2026-11-26 09:17:14", "2026-11-26T09:17:14.272") in the legacy form
    // "2026-11-26 09:17:14": the wall time kept as written (no zone conversion); none for other text.
    [[nodiscard]] std::optional<std::string> local_text(std::string_view text);

    // Whether a position is worth sending: not 0 or +-1 (devices without a fix send those), and in
    // range.
    [[nodiscard]] bool valid_coordinates(double latitude, double longitude);

    // How long after its last ping a tracker still counts as online, when its status has no
    // "online" field of its own.
    inline constexpr std::chrono::seconds default_online_for{600};

    // A tracker's position from its tac:<id> hash (written by streamvms-status): gpstime, lat,
    // lng, speed, ang, odometer (m), ignition, ping, online. None without a time and valid
    // coordinates. registration and fleet_number are left empty for the caller.
    [[nodiscard]] std::optional<position> from_tac(const std::string &device, const std::map<std::string, std::string> &fields,
                                                   clock::time_point now = clock::now(),
                                                   std::chrono::seconds online_for = default_online_for);

    // Whether a tac: hash has a ping: when it hasn't, the device is looked up on the VMS's Redis
    // as well (as oneapi does).
    [[nodiscard]] bool has_ping(const std::map<std::string, std::string> &fields);

    // A camera unit's position from its drtp:<id> hash (the VMS): positioningTime, latitude and
    // longitude (unsigned, with south/west flags), speed, direction, mileage (km), accStatus,
    // onlineStatus. None without a time and valid coordinates. registration is licenseNum.
    [[nodiscard]] std::optional<position> from_drtp(const std::string &device, const std::map<std::string, std::string> &fields);

    // A number as the device list writes it: up to decimals places, trailing zeros dropped
    // ("28.333157", "0", "-26.5").
    [[nodiscard]] std::string number_text(double value, int decimals = 6);

    // One deviceList entry: deviceNr, regNr, fleetNr, gpstime, latitude, longitude, speed,
    // mileage, acc_state, online, course (and temp1, temp2 for a tracker), every value text, as the
    // receivers have always had them; unknown values are "".
    [[nodiscard]] nlohmann::ordered_json device_entry(const position &at);

    // {"clientID": "<account>", "deviceList": [...]}: one account's devices, the payload most
    // receivers take.
    [[nodiscard]] nlohmann::ordered_json device_list(const std::string &account, const std::vector<position> &devices);

    // SAMS' Kafka value: the accounts' device lists as a JSON array, that array's text then
    // encoded again as a JSON string ("\"[{\\\"clientID\\\"...\""), as their consumer has always
    // read it. Accounts without devices are left out.
    [[nodiscard]] std::string sams_value(const std::vector<std::pair<std::string, std::vector<position>>> &accounts);

    // DHL's GPS points: {"clientID", "deviceList": [{GpsTimeStamp, OdoKm, SpeedKmpH, UnitCode, X, Y}]}
    // with the keys sorted, X the longitude, Y the latitude, UnitCode the registration (else the
    // device). Only devices with valid coordinates; none when that leaves none.
    [[nodiscard]] std::optional<std::string> dhl_points(const std::string &account, const std::vector<position> &devices);

    // The file DHL's SFTP folder takes: "dhl_<account>_<YYYYMMDDHHMMSS>.json", local time.
    [[nodiscard]] std::string dhl_file_name(const std::string &account, clock::time_point at = clock::now());
}
