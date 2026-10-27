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

Open `http://localhost:5173` in Chrome or Edge on a computer with a Bluetooth adapter. Web Bluetooth requires HTTPS or localhost; support depends on browser and operating system. iOS Safari and Firefox do not provide the required API; the page explains this and disables **Connect device** instead of failing on click.

The page is in English by default. The **English / 中文** switch in the header changes the page language, is remembered in the browser's `localStorage`, and updates `<html lang>`. It does not change the device: the device screen language is a separate **Device display language** setting on the Magpie tab (default English).

1. The device enters setup when no valid saved configuration exists. Hold the lower side button (GPIO18) for 1.2 seconds to enter setup later.
2. Click **Connect device** and select `magpie-usage-xxxxxx`, matching the Bluetooth name on the device screen. The six hexadecimal digits come from the last three bytes of its Bluetooth MAC. Enter the six-digit code shown by the Rust device screen in the operating system's pairing prompt. Credentials are accepted only over an encrypted, authenticated BLE connection.
3. The page reads saved SSIDs and password-presence flags. Existing passwords are never returned. Click **Scan for Wi‑Fi** to list 2.4 GHz networks by signal strength (bars and a lock for secured networks); choosing one fills the focused or first empty network card. An SSID can also be typed manually.
4. An empty configuration starts with one network card. After completing the visible cards, click **Add network** to add one more, up to five networks in priority order. While a card is incomplete, or five are shown, the button is disabled and a hint explains why. Saved networks are shown when connected. A password can be retained for the same saved SSID, replaced, or cleared for an open network. Empty slots are skipped. Duplicate SSIDs are rejected.
5. Switch between the **Network** and **Magpie** tabs (arrow keys also move between tabs). Enter the management Web address (maximum 512 bytes, optional proxy prefix) and the Web key from its `?k=` link (16–256 ASCII letters, digits or `-._~`). Existing credentials are never returned. Retain, replace or clear the Web key; changing instance requires replacing or clearing it. HTTP is disabled until explicitly allowed. Configure the reading interval (30–3600 seconds, default 30; checked when saving, with the error shown next to the field), IANA display timezone, A/B layout (default A), adaptive font (default enabled) and device display language (English or Chinese, default English). When the device has no timezone, the browser pre-fills its own timezone. The device and page share an explicit supported-zone list; other zones are rejected. Revision conflicts preserve drafts; use **Refresh from device**, inspect the draft and save explicitly again. Saving does not establish a Magpie login.
6. Each tab has its own save button, **Save network settings** or **Save Magpie settings**. It saves that section in NVS while preserving the other saved section and staying in setup. Unsaved edits in the other tab remain in the browser, and tabs with unsaved edits are marked. A successful COMMIT acknowledgment means the NVS commit completed. New secrets for the applied section are cleared from the form. Updated firmware is required for these commands.
7. Click **Exit setup** at the bottom of the page to leave setup; if there are unsaved edits, a dialog asks you to confirm discarding them. On the device, press the front round OK button (GPIO0) to open the device exit dialog, then OK to exit or UP/DOWN to keep editing. Applied settings remain saved. Only after exit does the device attempt each configured network for up to 15 seconds, with 750 ms between attempts and three complete failed rounds. A later disconnect starts with the next configured network. With no saved networks, exit leaves an idle page; hold DOWN to re-enter setup. On the failure page, OK starts another three rounds.

The active browser connection receives connection progress after explicitly leaving setup; the page shows which network and round is being tried and which networks failed. If the Bluetooth link drops, **Reconnect** reconnects to the same device without the chooser when the browser allows it, and otherwise opens the chooser. New BLE connections are advertised only while setup is active. A disconnect or two-minute transaction timeout discards unsaved staging. The device persists BLE bonds, so disconnecting, leaving and re-entering setup, or rebooting the device reuses the existing pairing. If the operating system still holds a bond that the device no longer has (for example after a full flash erase) and does not offer a new pairing prompt, remove the corresponding `magpie-usage-xxxxxx` device from the operating system's Bluetooth settings once and connect again. When the two sides disagree about an existing bond and the operating system starts a new pairing, the device replaces its own record and displays a new six-digit code.

The wire protocol is documented in `../docs/ble-provisioning.md`. The browser and firmware share version 1 framing and password modes. Browser writes split frame payloads into eight-byte chunks so every write fits ATT MTU 23. Notifications may carry larger frames (up to 192 payload bytes) when the negotiated MTU allows, and the page reassembles fragments of any size. No Wi-Fi password is included in status, scan or progress responses.

```powershell
bun run --cwd provisioning-web build
bun run --cwd provisioning-web test
bun x --cwd provisioning-web playwright install chromium
bun run --cwd provisioning-web test:browser
```

The Chromium validation starts its own Vite server on loopback port 5174, drives the actual page with a synthetic Web Bluetooth peer, and closes both afterward. It checks five-slot editing, scanning, focus, independent tab saves, all Web settings (including device display language) on reconnect, credential-presence metadata, and explicit exit. Screenshots are written under the ignored `.note4-build/validation/provisioning/browser/` directory. This validates the browser and wire contract; the peer does not execute the firmware or emulate radio authentication.

Real radio pairing, AP connectivity and physical display acceptance require the NOTE4 hardware. Automated tests cover protocol framing, CRC, password modes, notification reassembly, page state, translations and the page's interaction flows against a fake transport; they do not certify those hardware interactions.
