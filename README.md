# simply-cpp-fleet-feed

Device positions for third-party fleet systems: a device's latest position read from the status
hashes kept in Redis, the payloads fleet systems take, and delivering them over HTTP, Kafka or SFTP.

Part of the [simply-cpp](https://github.com/roelofrossouw/simply-cpp) suite. C++20, Apache-2.0.

## What's in it

`#include <sc/fleet-feed/feed.h>`: positions and payloads, no I/O.

- `sc::fleet_feed::position`: device, registration, fleet number, local time, latitude/longitude,
  speed, course, mileage (km), ACC, online, two temperatures.
- `from_tac(device, fields)`: a tracker's position from its `tac:<id>` hash (gpstime with its offset,
  lat, lng, speed, ang, odometer in metres, ignition, ping, online).
- `from_drtp(device, fields)`: a VMS camera unit's position from its `drtp:<id>` hash. The values are
  JSON text (`"\"28.33\""`), the coordinates unsigned with `south`/`west` flags, mileage in km.
- `vms_device(id)` (ids starting with `100` or `0`), `unquoted`, `number`, `iso_time`, `local_text`,
  `valid_coordinates` (not 0 or ±1, in range).
- Payloads:
  - `device_list(account, positions)`: `{"clientID", "deviceList": [{deviceNr, regNr, fleetNr,
    gpstime, latitude, longitude, speed, mileage, acc_state, online, course}]}`, every value text.
  - `sams_value(accounts)`: all accounts' device lists as one array, its text JSON-encoded again
    (a Kafka value read with two decodes).
  - `dhl_points(account, positions)`: `{"clientID", "deviceList": [{GpsTimeStamp, OdoKm, SpeedKmpH,
    UnitCode, X, Y}]}` with sorted keys, only valid positions; `dhl_file_name()`.

`#include <sc/fleet-feed/sender.h>`: delivery.

- `sc::fleet_feed::target`: where to (`https://…`, `kafka://broker:9092;broker2:9092/topic`,
  `sftp://host:22/folder`) and how to sign in:
  - `none`
  - `header`: `auth_name: auth_prefix + secret` (`Authorization: Bearer …`, or a bare key)
  - `url_key`: the secret appended to the URL's path
  - `query_key`: `?auth_name=secret`
  - `login_jwt`: POST `login_body` (`{username}`/`{password}` filled in, JSON-escaped) to
    `login_url`, take `token_field`, send `Bearer <token>`; the token is kept for `token_lifetime`
    or until a 401 (then one new login).
  - Fixed extra headers; Kafka over `sasl_ssl` with `PLAIN` (Confluent) and the like.
- `sc::fleet_feed::sender{target}.send(body, file_name)` returns `{ok, status, detail}`; a
  receiver's failure is the result, never an exception.

```cpp
#include <sc/fleet-feed/feed.h>
#include <sc/fleet-feed/sender.h>

sc::fleet_feed::sender to{{.name = "example", .url = "https://fleet.example.com/positions",
                           .auth = sc::fleet_feed::auth_kind::header, .auth_prefix = "Bearer ", .secret = key}};
const auto result = to.send(sc::fleet_feed::device_list("3077", positions).dump());
```

## Building

Needs sc-core, sc-http, sc-kafka and sc-sftp (installed, or fetched by `find_or_install_package`).

```sh
cmake -B build -S . && cmake --build build && ctest --test-dir build
```
