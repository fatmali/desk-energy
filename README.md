# Desk Energy

A standalone Pomodoro timer and energy dashboard for the ideaspark ESP32-WROOM
16MB board with a 1.9-inch 170x320 ST7789 TFT.

The timer works offline. Optional energy updates follow this path:
**Apple Watch -> Apple Health -> iPhone Shortcuts -> Scriptable -> Cloudflare
relay -> ESP32**. The phone and display do not need the same Wi-Fi network.
There is no custom iPhone app to periodically re-sign.

## License

Original project code is available under the [MIT license](LICENSE),
Copyright (c) 2026 Desk Energy contributors. Bundled font data and external
dependencies retain their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).

This is a personal desk companion, not a medical device or a validated measure
of mental energy. No Health data, private upload codes or deployment credentials
are included. Generated firmware, signed Shortcut exports, installed dependencies
and private configuration are excluded from Git.

## Device setup

1. Install the firmware over USB. After setup, any suitable USB power supply can
   power the board without a Mac or USB data connection.
2. On a new device, the initial screen shows a `DeskFocus-...` Wi-Fi network and
   a random 16-character access code for two minutes. Keep that code private.
3. Join the setup network using the code as its Wi-Fi password. Stay connected
   if the phone reports no internet. Open **http://192.168.4.1/**, not HTTPS.
4. Browser login: **focus**. Password: the access code.
5. In **Wi-Fi setup**, enter a **2.4 GHz** WPA/WPA2 personal or open network.
   Enterprise networks and captive portals are unsupported. Credentials stay
   in device preferences, not source code.
6. Reconnect the phone to normal Wi-Fi. Open the device's
   `http://deskfocus-....local/` address, available in the browser's Device
   section. The IP address is also available there or in USB startup logs.

The setup access point closes 60 seconds after connection. If Wi-Fi is
unavailable for 30 seconds, it reopens so settings and timer controls remain
accessible. Phones may leave networks without internet; disable that behavior
while using setup.

The access code does not automatically reappear on subsequent startups. An
authenticated browser can use **Show access code on screen** to reveal it
temporarily. BOOT behavior is unchanged.

### USB provisioning

At 115200 baud, send one newline-terminated JSON object:

```json
{"ssid":"NETWORK_NAME","password":"NETWORK_PASSWORD"}
```

To change networks while retaining the saved password:

```json
{"ssid":"NETWORK_NAME","reuse_password":true}
```

Password reuse requires an existing saved network and no `password` field.
The bounded parser validates and persists settings without echoing credentials.
Do not put real passwords in source code or shell history.

## Everyday use

- Tap **BOOT** to start, pause or resume. Hold for **1.5 seconds** to reset.
  A long press does not also trigger a tap.
- The browser also offers **Start / Pause / Resume / Reset / Skip**.
- Focus lasts **25 minutes**, short breaks **5 minutes**, and every fourth
  completed focus earns a **15-minute** break. Phases advance automatically.
- Skipped, unfinished focuses do not count. Resetting the timer preserves
  today's completed count.
- Daily counts are saved after completion and reset at local midnight.
  Opening the browser supplies its current time and UTC offset; the initial
  offset is Nairobi (UTC+3). NTP maintains UTC when internet is available.
  Reopen the browser after a timezone or daylight-saving change.
- Monotonic timing survives clock changes and Wi-Fi loss. Power loss resets
  the current timer but preserves the saved daily count. Before clock sync,
  the browser shows the daily count as unavailable. Undated completions are
  reconciled during that boot, but cannot survive power loss.
- Tasks and optional computer build/test reports appear in the browser, not
  on the TFT.

BOOT is active-low GPIO0, not EN/RST. Holding it during startup can enter
download mode; release BOOT and tap EN/RST to boot normally.

Serial commands: `s` start/resume, `p` pause, `r` reset, `n` skip, `t` status,
`?` help. Status includes dashboard timing, Wi-Fi signal, relay enablement and
energy status, but not the health score, network name or credentials.

## Energy dashboard

[EnergyDashboard.h](include/EnergyDashboard.h) draws the score, local clock,
seven hourly history slots and a level bar into a 320x8 RGB565 strip. The timer
replaces the history while active. History is RAM-only and resets on reboot.

The score, percent, level bar and history use four colors:

| Score | Color |
|-------|-------|
| 0-24 | Coral |
| 25-49 | Amber |
| 50-74 | Mint |
| 75-100 | Vivid violet |

A stale but unexpired score keeps its color, with a tiny amber clock icon beside
the time. Stale values are not recorded as fresh history. Missing, expired,
unavailable and clock-unsynchronized states hide the score and have distinct
messages.

Freshness checks computation age, observation age and monotonic receipt age.
A reading becomes stale at two hours and expires by its original `valid_until`,
at most six hours after computation. Identical retries do not reset its age.
The last validated reading survives reboot/upload, with its original timestamps.
Before clock synchronization, it is not presented as current data.

The relay's 36-hour metric eligibility window is not a freshness guarantee.
Syncing old Watch observations again does not turn them into new measurements.

## iPhone sync

A real Scriptable upload has been confirmed. Locked-phone Scriptable execution
and all-day unattended reliability are not yet confirmed.

1. Install **Scriptable**. Create a script named **Desk Energy** using
   [Desk Energy.js](tools/scriptable/Desk%20Energy.js), or place that file in
   Scriptable's iCloud Drive folder. Set `relayURL` at the top to your own
   deployed Worker's HTTPS `/v1/energy` endpoint. The script contains no credentials.
2. Run it in Scriptable once. Paste the private upload code into its secure
   prompt. The code is saved in Scriptable's encrypted Keychain, not in the
   script or Shortcut. Setup itself uploads no Health data.
3. Generate and sign **Desk Energy Health Data.shortcut** using the development
   commands below, then AirDrop it to the iPhone and import it.
   It contains 20 data-reading actions. The three Find actions must read
   **Heart Rate Variability**, **Resting Heart Rate**, and **Active Calories**.
   Heart queries retain uncapped history over 3650 days; activity is today only.
4. After its final **Text** action, add **Scriptable -> Run Script**. Select
   **Desk Energy**, set **Parameter** to **Text**, and leave **Run in App off**.
   Existing configured phone copies do not need to be rebuilt for code cleanup.
5. Run once unlocked and allow the requested permissions. If Shortcuts blocks
   sharing thousands of Health items, enable **Settings -> Apps -> Shortcuts ->
   Advanced -> Allow Sharing Large Amounts of Data**, then allow Scriptable.
   This setting affects all Shortcuts, so use only trusted workflows.

Successful script output has `synced: true`. Errors throw and notify instead
of reporting false success. The ESP32 polls the relay every minute using
certificate-validated HTTPS; failures retain the last reading and back off to
five minutes. A successful upload can still yield unavailable or stale data.

### Hourly automation

On the iPhone, configure **Time of Day -> Daily -> Run Immediately** to run
**Desk Energy Health Data** every hour from **06:00 through 18:00 inclusive**.
That is 13 daily times, each requiring its own personal automation.
Personal automations are not installed with a shared Shortcut.

Keep Scriptable's **Run in App off**. iOS controls locked-phone execution;
scheduled syncing does not force the Watch to collect new measurements.

### Data and calculation

The Shortcut passes private labeled text beginning with `DESK_ENERGY_V1`:
`hrv_values`, `hrv_dates`, `rhr_values`, `rhr_dates`, `activity_values`,
`activity_starts`, `activity_ends`, and `activity_sources`. Values and dates
from each query stay paired. Dates include time and timezone in ISO 8601.
Do not log, share or save this raw text. No clipboard access is needed.

Scriptable also accepts a JSON dictionary with those lists through
`args.shortcutParameter`. Empty lists mean unavailable data; malformed values,
future dates, unequal list lengths and lists over 10,000 samples are errors.
Units are milliseconds, beats/minute and kilocalories. Recognized suffixes:
`ms`, HRV `s`, `bpm`, `count/min`, `beats/min`, `kcal`, and `Cal`.

The script constructs personal daily-median baselines using samples strictly
earlier than the latest observation. The relay uses a metric only with at least
seven baseline days and an observation no older than 36 hours. There is no
population fallback or self-comparison. Activity is summed only for one source
with non-overlapping intervals; otherwise it is omitted with a notification.

The [relay model](relay/src/model.ts) is a **heuristic, not a medical measurement
or validated mental-energy score**. HRV has weight 25% and resting heart rate
15%, normalized over eligible components. HRV is compared proportionally to its
baseline; resting heart rate loses five points per bpm above baseline. Scores
are capped at 100. Activity alone cannot establish a score.

The score loses three points/hour from the first successful estimate of the
local day, not measured waking time, plus 0.025 points/kcal capped at 30.
Reserve and score cannot rise within that day. This is not a sleep, circadian
or menstrual-cycle model. Genuine observation times are preserved.

### Relay configuration and privacy

The Worker is configured in [wrangler.jsonc](relay/wrangler.jsonc).
It uses one SQLite-backed Durable Object, with separate reader/writer secrets.
No billing upgrade is required for the current usage on the Free plan.

For your own deployment:

```sh
cd relay
npm ci
npx wrangler login
npx wrangler secret put READ_TOKEN
npx wrangler secret put WRITE_TOKEN
npm run deploy
```

Choose a unique Worker `name` in the configuration before running those commands.
Generate two different 32-byte random hexadecimal tokens, for example with
`openssl rand -hex 32`, and enter each at its secret prompt, never in source or
command arguments. The reader token belongs on the ESP32; the writer token is
the Scriptable upload code. The resulting `workers.dev` URL belongs in
Scriptable's `relayURL`. Use your own Cloudflare account and review its quotas.

To provision an existing relay over USB without embedding credentials in
firmware, send one line containing its hostname and private reader token:

```json
{"relay":{"host":"WORKER.ACCOUNT.workers.dev","token":"64_LOWERCASE_HEX_CHARACTERS"}}
```

Only a `workers.dev` hostname is accepted. `{"relay":null}` disables polling
without erasing the saved reading; it still becomes stale and expires normally.
Settings and the last validated score are saved in ordinary NVS, not encrypted
flash. Re-enabling polling requires USB provisioning.

HTTPS protects transit, **not end-to-end privacy**: Cloudflare can process the
small summary. Raw histories and source names stay on the phone. The relay
retains only the latest reading, daytime session and input digest. Do not share
upload codes, reader tokens, configured credentials or raw Health exports.
Local secret files remain under the ignored `relay/private/` directory.
Cloudflare quotas and iOS background restrictions still apply.

The old native-app pairing QR and local Health receiver have been removed.
Energy updates arrive only through the authenticated HTTPS relay. The separate
Wi-Fi setup/access-code screen and browser administration remain available.

## Optional computer build/test reports

[desk_focus.py](tools/desk_focus.py) reports job label, identifier, kind and
state over the local network, not command arguments, output, source code or
environment variables. No daemon is needed.

```sh
python3 tools/desk_focus.py configure --url http://DEVICE-IP
python3 tools/desk_focus.py run build "ESP32 firmware" -- \
  ~/.platformio/penv/bin/platformio run -e esp32dev
python3 tools/desk_focus.py run test "Timer and helper tests" -- \
  sh tools/test_host.sh
```

Configuration prompts privately for the device access code and saves it with
owner-only permissions under `~/.config/desk-focus/device.json`. A `.local`
hostname is preferable to an IP when mDNS works.

Use the VS Code tasks **Desk Focus: Build with status** or **Desk Focus: Test
with status** to include reporting. The wrapper preserves command output and
exit status. Reporting failures do not prevent the command from running.
After 60 seconds without a heartbeat, status becomes `unknown`, not success.
Older runs cannot replace a newer job's status.

## Firmware updates and local security

Build, reset the timer to **Ready**, then use **Device & wireless updates** in
the authenticated browser to upload `.pio/build/esp32dev/firmware.bin`.
Do not upload a bootloader, partition table or merged full-flash image there.
Interrupted/rejected uploads do not activate the partial image. A valid but
broken app can still require USB recovery. Wi-Fi settings and saved readings
survive updates; firmware images are not cryptographically signed.

The browser uses HTTP with Basic authentication: **trusted LAN only**.
Do not port-forward it. Admin writes also require a same-origin custom header;
JSON bodies are limited to 768 bytes. Firmware authentication is checked before
flash writes.
People with the access code can control and update the device. Physical flash
access or access to the computer account can expose stored credentials.

## Hardware and development

- Espressif32 8.0.0, Arduino, `esp32dev`, 16MB flash and `default_16MB.csv`.
- ST7789 SPI: MOSI23, SCLK18, CS15, DC2, RST4, backlight32 (HIGH = on).
- Display: 320x170 landscape, rotation 1; SPI 40 MHz.
- BOOT: GPIO0 with input pull-up. No display readback is available.

```sh
~/.platformio/penv/bin/platformio run -e esp32dev
~/.platformio/penv/bin/platformio run -e esp32dev -t upload
sh tools/test_host.sh
node tools/shortcuts/build-sync.mjs
shortcuts sign --mode anyone \
  --input tools/shortcuts/health-data.unsigned.shortcut \
  --output "tools/shortcuts/Desk Energy Health Data.shortcut"
cd relay
npm ci
npm run check
npm test
npm run build
```

Host tests cover timer/button timing, daily persistence, text validation,
computer reporting, energy validation/freshness/storage and relay settings.
JavaScript tests preserve the working
Shortcut's metadata/filters/wiring and execute the actual Scriptable file with
mocked iOS APIs, feeding its summary into the real relay calculator.
Worker tests run in the Cloudflare runtime.

No test fabricates live Health measurements. Signing verifies a Shortcut's
import signature, not iPhone permissions or locked-phone execution. Actual
panel colors still require visual confirmation.
