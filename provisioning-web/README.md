# Magpie Usage Wi-Fi setup

## GitHub Pages

Deployment URL after the first successful workflow run: https://mslxl.github.io/esp32-idf-magpie-usage/

In the repository's **Settings → Pages → Build and deployment**, select **GitHub Actions** as the source. The `Deploy provisioning page` workflow tests and builds the browser app on changes to `provisioning-web/` on `main`, then deploys only `provisioning-web/dist`. It can also be started from **Actions → Deploy provisioning page → Run workflow**, selecting `main`. Manual runs on other branches build without publishing.

Vite emits relative asset URLs so the page works under the repository path. GitHub Pages supplies HTTPS. The browser talks to the device through local Bluetooth; Wi-Fi credentials are not sent to GitHub Pages. Browser/OS Web Bluetooth support and a local Bluetooth adapter are still required.

## Local development

From the repository root:

```powershell
bun install --cwd provisioning-web
bun run --cwd provisioning-web dev --host 127.0.0.1
```

Open `http://localhost:5173` in Chrome or Edge on a computer with a Bluetooth adapter. Web Bluetooth requires HTTPS or localhost; support depends on browser and operating system. iOS Safari and Firefox do not provide the required API.

1. The device enters setup when no valid saved configuration exists. Hold the lower side button (GPIO18) for 1.2 seconds to enter setup later.
2. Click Connect and select `magpie-usage-xxxxxx`, matching the Bluetooth name on the device screen. The six hexadecimal digits come from the last three bytes of its Bluetooth MAC. Enter the six-digit code shown by the Rust device screen in the operating system's pairing prompt. Credentials are accepted only over an encrypted, authenticated BLE connection.
3. The page reads saved SSIDs and password-presence flags. Existing passwords are never returned. Scan for 2.4 GHz networks or enter an SSID manually.
4. An empty configuration starts with one network card. After completing the visible cards, click `+` to add one more, up to five networks in priority order. A newly added empty card hides `+` until it is configured. Saved networks are shown when connected. A password can be retained for the same saved SSID, replaced, or cleared for an open network. Empty slots are skipped. Duplicate SSIDs are rejected.
5. Switch between **网络** and **Magpie 配置**. Enter the management Web address (maximum 512 bytes, optional proxy prefix) and the Web key from its `?k=` link (16–256 ASCII letters, digits or `-._~`). Existing credentials are never returned. Retain, replace or clear the Web key; changing instance requires replacing or clearing it. HTTP is disabled until explicitly allowed. Configure the reading interval (30–3600 seconds, default 30), IANA display timezone, A/B layout (default A) and adaptive font (default enabled). When the device has no timezone, the browser pre-fills its own timezone. The device and page share an explicit supported-zone list; other zones are rejected. Revision conflicts preserve drafts; use **重新读取配置版本**, inspect the draft and save explicitly again. Saving does not establish a Magpie login.
6. **应用设置** saves the active tab in NVS while preserving the other saved section and staying in setup. Unsaved edits in the other tab remain in the browser. A successful COMMIT acknowledgment means the NVS commit completed. New secrets for the applied section are cleared from the form. Updated firmware is required for these commands.
7. Click **退出设置模式** to leave setup; if there are unsaved edits, confirm their discard. On the device, press the front round OK button (GPIO0) to open the device exit dialog, then OK to exit or UP/DOWN to keep editing. Applied settings remain saved. Only after exit does the device attempt each configured network for up to 15 seconds, with 750 ms between attempts and three complete failed rounds. A later disconnect starts with the next configured network. With no saved networks, exit leaves an idle page; hold DOWN to re-enter setup. On the failure page, OK starts another three rounds.

The active browser connection receives connection progress after explicitly leaving setup. New BLE connections are advertised only while setup is active. A disconnect or two-minute transaction timeout discards unsaved staging. If the host has a stale bond, remove the corresponding `magpie-usage-xxxxxx` device from the operating system's Bluetooth settings before pairing again.

The wire protocol is documented in `../docs/ble-provisioning.md`. The browser and firmware share version 1 framing and password modes. Frame payloads are split into eight-byte chunks so every write/notification fits ATT MTU 23. No Wi-Fi password is included in status, scan or progress responses.

```powershell
bun run --cwd provisioning-web build
bun run --cwd provisioning-web test
bun x --cwd provisioning-web playwright install chromium
bun run --cwd provisioning-web test:browser
```

The Chromium validation starts its own Vite server on loopback port 5174, drives the actual page with a synthetic Web Bluetooth peer, and closes both afterward. It checks five-slot editing, scanning, focus, independent tab saves, all Web settings on reconnect, credential-presence metadata, and explicit exit. Screenshots are written under the ignored `.note4-build/validation/provisioning/browser/` directory. This validates the browser and wire contract; the peer does not execute the firmware or emulate radio authentication.

Real radio pairing, AP connectivity and physical display acceptance require the NOTE4 hardware. Automated tests cover protocol framing, CRC, password modes, and notification reassembly; they do not certify those hardware interactions.
