import { readAccountDirectory } from "./account-directory";
import { defaultPrefs, uploadDisplayPrefs } from "./display-prefs";
import { update } from "./dom";
import { describeError, isChooserCancel, LocalizedError } from "./errors";
import { htmlLang, loadLanguage, saveLanguage, setCurrentLanguage, translate, type Language, type MessageKey, type Params } from "./i18n";
import { decodeScanResult, decodeStatusPayload, decodeWebSettings, encodeBegin, encodeSetSlotPayload, Opcode, type Frame } from "./protocol";
import * as S from "./state";
import { Note4Transport, type Transport } from "./transport";
import { moveFocusId } from "./view/directory-panel";
import { syncDialog } from "./view/exit-dialog";
import type { Actions } from "./view/helpers";
import { page } from "./view/page";

const SCAN_TIMEOUT_MS = 20_000;

export type AppOptions = {
  transport?: Transport;
  storage?: Storage;
  /** IANA zone pre-filled when the device has no display time zone yet. */
  browserZone?: string;
};

function defaultStorage(): Storage | undefined {
  try { return typeof localStorage === "undefined" ? undefined : localStorage; } catch { return undefined; }
}

export default function mountApp(root: HTMLElement, options: AppOptions = {}): () => void {
  const transport = options.transport ?? new Note4Transport();
  const storage = options.storage ?? defaultStorage();
  const browserZone = options.browserZone ?? Intl.DateTimeFormat().resolvedOptions().timeZone;
  let state = S.initialState(loadLanguage(storage), transport.supported);
  let mounted = true;
  let paintQueued = false;
  let pendingFocus: string | undefined;
  let scanTimer: ReturnType<typeof setTimeout> | undefined;

  // ---------- state plumbing ----------

  function set(next: (current: S.AppState) => S.AppState): void {
    state = next(state);
    if (paintQueued || !mounted) return;
    paintQueued = true;
    queueMicrotask(() => { paintQueued = false; if (mounted) paint(); });
  }
  const setUi = (patch: Partial<S.UiState>) => set((s) => ({ ...s, ui: { ...s.ui, ...patch } }));
  const setSession = (patch: Partial<S.SessionState>) => set((s) => ({ ...s, session: { ...s.session, ...patch } }));
  const setNetwork = (next: (network: S.NetworkState) => S.NetworkState) => set((s) => ({ ...s, network: next(s.network) }));
  const setMagpie = (next: (magpie: S.MagpieState) => S.MagpieState) => set((s) => ({ ...s, magpie: next(s.magpie) }));
  const setDirectory = (next: (directory: S.DirectoryState) => S.DirectoryState) => set((s) => ({ ...s, directory: next(s.directory) }));
  const showStatus = (key: MessageKey, params?: Params) => setUi({ status: { key, params } });
  const showAlert = (key: MessageKey, params?: Params) => setUi({ alert: { key, params } });

  function reportError(error: unknown): void {
    // Closing the device chooser is a normal choice, not a failure.
    if (isChooserCancel(error)) setUi({ status: describeError(error), alert: undefined });
    else setUi({ status: undefined, alert: describeError(error) });
  }

  function applyLanguage(language: Language): void {
    setCurrentLanguage(language);
    document.documentElement.lang = htmlLang(language);
    document.title = translate(language, "app.title");
  }

  /** Runs one device operation with the shared busy guard, progress label and error reporting. */
  async function runBusy(progress: S.ProgressKey, status: MessageKey, work: () => Promise<void>): Promise<void> {
    if (state.ui.busy || !mounted) return;
    setUi({ busy: progress, status: { key: status }, alert: undefined });
    try {
      await work();
    } catch (error) {
      if (mounted) reportError(error);
    } finally {
      setUi({ busy: undefined });
    }
  }

  /** BEGIN → caller's uploads → COMMIT; the shared revision advances once the commit is durable. */
  async function transaction(revision: number, steps: () => Promise<unknown>): Promise<void> {
    await transport.send(Opcode.Begin, encodeBegin(revision));
    await steps();
    await transport.send(Opcode.Commit);
    setSession({ revision: revision + 1 });
    if (!state.session.editing) throw new LocalizedError("error.setupExited");
  }

  function rejectWithFieldErrors(apply: (s: S.AppState) => S.AppState, errors: S.FieldErrors, idOf: (key: string) => string): void {
    set(apply);
    showAlert("error.fixFields");
    const first = Object.keys(errors)[0];
    if (first) pendingFocus = idOf(first);
  }

  function clearScanTimer(): void {
    if (scanTimer !== undefined) clearTimeout(scanTimer);
    scanTimer = undefined;
  }

  // ---------- device events ----------

  function handleStatus(frame: Frame): void {
    const status = decodeStatusPayload(frame.payload);
    const canEdit = status.state === 1 || status.state === 2;
    if (!canEdit) {
      set((s) => S.stopEditing({ ...s, network: S.applyDeviceStatus(s.network, status) }));
      if (!state.session.wifi) showStatus("status.deviceLeftSetup");
      return;
    }
    const { editing } = state.session, busy = state.ui.busy;
    if (!editing && !busy) {
      // The device (re-)entered setup on its own: reload everything.
      set((s) => ({ ...s, network: S.applyDeviceStatus(s.network, status), magpie: { ...s.magpie, ready: false } }));
      void transport.send(Opcode.GetWebSettings).catch(() => showAlert("error.firmwareUpgrade"));
    } else if (!editing || (!state.network.dirty && (!busy || busy === "refreshing"))) {
      // STATUS also answers BEGIN; never overwrite a draft that is being edited or saved.
      setNetwork((network) => S.applyDeviceStatus(network, status));
    }
    setSession({ editing: true, wifi: undefined });
    if (!busy) {
      setSession({ applying: false });
      showStatus("status.loaded");
    }
  }

  function handleFrame(frame: Frame): void {
    try {
      switch (frame.opcode) {
        case Opcode.Status: handleStatus(frame); break;
        case Opcode.WebSettings: {
          const metadata = decodeWebSettings(frame.payload);
          set((s) => ({ ...s, session: { ...s.session, revision: metadata.revision },
            magpie: S.magpieFromDevice(s.magpie, metadata, browserZone) }));
          if (!state.session.applying) showStatus("status.loaded");
          break;
        }
        case Opcode.ScanResult: setNetwork((network) => S.addScanResult(network, decodeScanResult(frame.payload))); break;
        case Opcode.ScanDone:
          clearScanTimer();
          setNetwork((network) => ({ ...network, scanning: false }));
          showStatus("status.scanDone", { count: state.network.scans.length });
          break;
        case Opcode.Applying:
          setSession({ applying: true });
          showStatus("status.applying");
          break;
        case Opcode.Connecting: {
          const round = frame.payload[0] ?? 1;
          set((s) => S.wifiEvent(S.stopEditing(s), { kind: "connecting", slot: frame.slot, round }));
          showStatus("wifi.trying", { index: frame.slot + 1, ssid: state.network.slots[frame.slot]?.ssid ?? "", round });
          break;
        }
        case Opcode.ConnectionFailed: set((s) => S.wifiEvent(s, { kind: "slotFailed", slot: frame.slot })); break;
        case Opcode.Connected:
          set((s) => S.wifiEvent(S.stopEditing(s), { kind: "connected" }));
          setUi({ status: { key: "wifi.connected" }, alert: undefined });
          break;
        case Opcode.Failure:
          set((s) => S.wifiEvent({ ...s, session: { ...s.session, applying: false } }, { kind: "failed" }));
          setUi({ status: undefined, alert: { key: "wifi.failed" } });
          break;
      }
    } catch (error) {
      reportError(error);
    }
  }

  const offEvent = transport.onEvent(handleFrame);
  const offDisconnect = transport.onDisconnect(() => {
    clearScanTimer();
    set(S.disconnected);
    setUi({ status: { key: "status.disconnected" }, alert: undefined });
  });

  // ---------- actions ----------

  async function afterConnect(name: string): Promise<void> {
    set((s) => S.connected(s, name));
    showStatus("status.reading");
    await transport.send(Opcode.GetStatus);
    try {
      await transport.send(Opcode.GetWebSettings);
    } catch {
      showAlert("error.firmwareUpgrade");
    }
  }

  function exitNow(): void {
    void runBusy("exiting", "status.exiting", async () => {
      await transport.send(Opcode.ExitSetup);
      set(S.stopEditing);
      showStatus("status.exited");
    });
  }

  const actions: Actions = {
    setLanguage(language) {
      saveLanguage(storage, language);
      applyLanguage(language);
      setUi({ language });
    },
    connect: () => void runBusy("connecting", "status.connecting", async () => afterConnect(await transport.connect())),
    reconnect: () => void runBusy("reconnecting", "status.connecting", async () => afterConnect(await transport.reconnect())),
    selectTab(tab, focus = false) {
      setUi({ tab });
      if (focus) pendingFocus = `${tab}-tab`;
    },
    scan() {
      if (S.isLocked(state) || state.network.scanning) return;
      void runBusy("scanning", "status.scanning", async () => {
        setNetwork((network) => ({ ...network, scans: [], scanning: true }));
        try {
          await transport.send(Opcode.Scan);
        } catch (error) {
          setNetwork((network) => ({ ...network, scanning: false }));
          throw error;
        }
        clearScanTimer();
        // Fall back if SCAN_DONE never arrives so the button does not stay disabled.
        scanTimer = setTimeout(() => { scanTimer = undefined; setNetwork((network) => ({ ...network, scanning: false })); }, SCAN_TIMEOUT_MS);
      });
    },
    pickScan(ssid) {
      if (S.isLocked(state)) return;
      const index = S.scanTarget(state.network);
      setNetwork((network) => S.pickScan(network, ssid));
      pendingFocus = `ssid-${index}`;
    },
    focusSlot(index) {
      if (state.network.target !== index) setNetwork((network) => ({ ...network, target: index }));
    },
    editSsid: (index, ssid) => setNetwork((network) => S.editSsid(network, index, ssid)),
    editSlot: (index, patch) => setNetwork((network) => S.editSlot(network, index, patch)),
    addSlot() {
      if (S.isLocked(state) || !S.canAddSlot(state.network).ok) return;
      pendingFocus = `ssid-${state.network.visible}`;
      setNetwork(S.addSlot);
    },
    saveNetwork() {
      if (S.isLocked(state)) return;
      if (!state.magpie.ready) { showAlert("error.magpieNotReady"); return; }
      const errors = S.validateNetwork(state.network);
      if (Object.keys(errors).length) {
        rejectWithFieldErrors((s) => ({ ...s, network: { ...s.network, errors } }), errors, (key) => key);
        return;
      }
      const revision = state.session.revision;
      const payloads = state.network.slots.map(encodeSetSlotPayload);
      void runBusy("savingNetwork", "status.saving", async () => {
        await transaction(revision, async () => {
          for (let index = 0; index < payloads.length; index += 1) await transport.send(Opcode.SetSlot, payloads[index], index);
        });
        setNetwork(S.networkSaved);
        setSession({ applying: false });
        showStatus("status.saved");
      });
    },
    editMagpie: (patch) => setMagpie((magpie) => S.editMagpie(magpie, patch)),
    saveMagpie() {
      if (S.isLocked(state)) return;
      if (!state.magpie.ready) { showAlert("error.magpieNotReady"); return; }
      const revision = state.session.revision;
      const { errors, payload } = S.validateMagpie(state.magpie, revision);
      if (!payload) {
        rejectWithFieldErrors((s) => ({ ...s, magpie: { ...s.magpie, errors } }), errors, (key) => `magpie-${key}`);
        return;
      }
      const instanceChanged = S.endpointChanged(state.magpie);
      void runBusy("savingMagpie", "status.saving", async () => {
        await transaction(revision, () => transport.send(Opcode.SetWebSettings, payload));
        set((s) => ({ ...s, magpie: S.magpieSaved(s.magpie),
          directory: instanceChanged ? { value: undefined, prefs: defaultPrefs(), dirty: false } : s.directory }));
        setSession({ applying: false });
        showStatus("status.saved");
      });
    },
    loadDirectory() {
      if (S.isLocked(state)) return;
      void runBusy("loadingDirectory", "status.loadingDirectory", async () => {
        const next = await readAccountDirectory(transport);
        if (!state.session.editing) throw new LocalizedError("error.setupExited");
        setDirectory((directory) => S.directoryLoaded(directory, next));
        showStatus("status.directoryLoaded");
      });
    },
    editPrefs: (patch) => setDirectory((directory) => S.editPrefs(directory, patch)),
    moveGroup(identity, direction) {
      setDirectory((directory) => S.moveGroup(directory, identity, direction));
      pendingFocus = moveFocusId(identity, direction);
    },
    toggleHidden: (identity) => setDirectory((directory) => S.toggleHidden(directory, identity)),
    cleanupMissing: () => setDirectory(S.cleanupMissing),
    savePrefs() {
      if (state.ui.busy || !state.session.editing || !state.directory.value) return;
      const revision = state.session.revision;
      const candidate = S.prefsForUpload(state.directory);
      void runBusy("savingPrefs", "status.saving", async () => {
        await transaction(revision, () => uploadDisplayPrefs(transport, revision, candidate));
        setDirectory((directory) => ({ ...directory, prefs: candidate, dirty: false }));
        showStatus("status.prefsSaved");
      });
    },
    refresh() {
      if (S.isLocked(state)) return;
      void runBusy("refreshing", "status.refreshing", async () => {
        await transport.send(Opcode.GetStatus);
        await transport.send(Opcode.GetWebSettings);
      });
    },
    requestExit() {
      if (S.isLocked(state)) return;
      if (S.hasUnsavedChanges(state)) setUi({ exitDialog: true });
      else exitNow();
    },
    confirmExit() {
      if (!S.isLocked(state)) exitNow();
    },
    cancelExit() {
      if (state.ui.exitDialog) setUi({ exitDialog: false });
    },
  };

  // ---------- rendering ----------

  function focusPending(): void {
    const target = pendingFocus;
    pendingFocus = undefined;
    if (!target) return;
    let node: HTMLElement | null = root.querySelector<HTMLElement>(`[id="${target.replace(/"/g, '\\"')}"]`);
    if (!node) {
      const marked = [...root.querySelectorAll<HTMLElement>("[data-focus-id]")];
      node = marked.find((element) => element.getAttribute("data-focus-id") === target) ?? null;
      // A moved item at the top or bottom has that direction disabled; keep focus on its row.
      if (node && (node as HTMLButtonElement).disabled) {
        node = [...(node.parentElement?.querySelectorAll<HTMLButtonElement>("button") ?? [])].find((button) => !button.disabled) ?? node;
      }
    }
    node?.focus();
  }

  function paint(): void {
    const t = (key: MessageKey, params?: Params) => translate(state.ui.language, key, params);
    update(root, page({ state, t, text: (value) => t(value.key, value.params), actions }));
    syncDialog(root, state.ui.exitDialog);
    focusPending();
  }

  applyLanguage(state.ui.language);
  paint();

  return () => {
    mounted = false;
    clearScanTimer();
    offEvent();
    offDisconnect();
    transport.disconnect();
    state = S.clearSecrets(state);
    root.replaceChildren();
  };
}
