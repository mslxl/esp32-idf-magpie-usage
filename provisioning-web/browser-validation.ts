import { chromium } from 'playwright';
import { createServer } from 'vite';
import { mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { decodeFrame, decodeSetSlotPayload, encodeFrame, Opcode, splitPayload } from './src/protocol';
const output = new URL('../.note4-build/validation/provisioning/browser/', import.meta.url);
await mkdir(output, { recursive: true });
const server = await createServer({ root: fileURLToPath(new URL('.', import.meta.url)),
  server: { host: '127.0.0.1', port: 5174, strictPort: true } });
await server.listen();
const browser = await chromium.launch({ headless: true });
try {
  for (const saved of [[], ['Home', 'Work'], ['', '', 'Sparse']]) {
    const page = await browser.newPage();
    const errors: string[] = [];
    page.on('pageerror', error => errors.push(error.message));
    const written: number[] = [];
    const network = Array.from({ length: 5 }, (_, i) => ({ ssid: saved[i] ?? '', passwordPresent: Boolean(saved[i]) }));
    let revision = 0;
    let committedMagpie = { endpoint: '', keyPresent: false, flags: 2, interval: 30, timezone: '' };
    let stagedMagpie = committedMagpie;
    let stagedNetwork = network.map(slot => ({ ...slot }));
    let chunks: number[] = [];
    const requests: number[] = [];
    await page.exposeFunction('note4Write', async (bytes: number[]) => {
      const frame = decodeFrame(new Uint8Array(bytes));
      if (frame.sequence === 0) chunks = [];
      chunks.push(...frame.payload);
      if (frame.sequence !== frame.totalSequences - 1) return;
      frame.payload = new Uint8Array(chunks);
      requests.push(frame.opcode);
      const notify = async (opcode: number, payload: number[]) => {
        const chunks = splitPayload(new Uint8Array(payload), 8);
        for (let sequence = 0; sequence < chunks.length; sequence++) {
          const data = encodeFrame({ opcode, requestId: frame.requestId, slot: 0, sequence,
            totalSequences: chunks.length, payload: chunks[sequence] });
          await page.evaluate(bytes => (window as any).note4Notify(bytes), Array.from(data));
        }
      };
      await notify(Opcode.Ack, [0]);
      if (frame.opcode === Opcode.Begin) {
        if (frame.payload.length !== 5 || frame.payload[0] !== 2 || new DataView(frame.payload.buffer).getUint32(1, true) !== revision) throw new Error('invalid BEGIN revision');
        stagedNetwork = network.map(slot => ({ ...slot }));
        stagedMagpie = { ...committedMagpie };
      }
      if (frame.opcode === Opcode.GetStatus || frame.opcode === Opcode.Begin) {
        const payload = [1, network.reduce((mask, slot, i) => slot.ssid ? mask | (1 << i) : mask, 0)];
        for (const slot of network) {
          const ssid = Array.from(new TextEncoder().encode(slot.ssid));
          payload.push(ssid.length, ...ssid, slot.passwordPresent ? 1 : 0);
        }
        await notify(Opcode.Status, payload);
      }
      if (frame.opcode === Opcode.SetWebSettings) {
        const length = frame.payload[10] | frame.payload[11] << 8;
        const keyLength = frame.payload[12] | frame.payload[13] << 8;
        stagedMagpie = { endpoint: new TextDecoder().decode(frame.payload.slice(14, 14 + length)),
          keyPresent: frame.payload[5] === 1 || (frame.payload[5] === 0 && committedMagpie.keyPresent),
          flags: frame.payload[6], interval: frame.payload[7] | frame.payload[8] << 8,
          timezone: new TextDecoder().decode(frame.payload.slice(14 + length + keyLength)) };
      }
      if (frame.opcode === Opcode.Commit) {
        network.splice(0, 5, ...stagedNetwork);
        committedMagpie = { ...stagedMagpie };
        revision++;
      }
      if (frame.opcode === Opcode.GetWebSettings) {
        const url = Array.from(new TextEncoder().encode(committedMagpie.endpoint));
        const zone = Array.from(new TextEncoder().encode(committedMagpie.timezone));
        await notify(Opcode.WebSettings, [2, revision, 0, 0, 0, committedMagpie.keyPresent ? 1 : 0,
          committedMagpie.flags, committedMagpie.interval & 255, committedMagpie.interval >> 8,
          zone.length, url.length & 255, url.length >> 8, ...url, ...zone]);
      }
      if (frame.opcode === Opcode.Scan) {
        const ssid = Array.from(new TextEncoder().encode('Test AP'));
        await notify(Opcode.ScanResult, [ssid.length, ...ssid, 206, 6, 1]);
        await notify(Opcode.ScanDone, []);
      }
      if (frame.opcode === Opcode.SetSlot) {
        written.push(frame.slot);
        const slot = decodeSetSlotPayload(frame.payload);
        stagedNetwork[frame.slot] = { ssid: slot.ssid, passwordPresent: slot.passwordMode === 1 ||
          (slot.passwordMode === 0 && network[frame.slot].passwordPresent) };
      }
    });
    await page.addInitScript(() => {
      let listener: ((event: unknown) => void) | undefined;
      const status = { addEventListener: (_: string, fn: (event: unknown) => void) => { listener = fn; },
        startNotifications: async () => status };
      (window as any).note4Notify = (bytes: number[]) => {
        const value = new Uint8Array(bytes);
        listener?.({ target: { value: new DataView(value.buffer) } });
      };
      const write = { writeValueWithResponse: async (value: ArrayBuffer) =>
        (window as any).note4Write(Array.from(new Uint8Array(value))) };
      Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
        requestDevice: async () => ({ name: 'magpie-usage-test', addEventListener: () => {},
          gatt: { connect: async () => ({ getPrimaryService: async () => ({
            getCharacteristic: async (uuid: string) => uuid.startsWith('7c2e0003') ? status : write,
          }) }) } }),
      } });
    });
    await page.goto('http://127.0.0.1:5174/');
    await page.getByRole('button', { name: '连接设备' }).click();
    await page.getByText('已读取设备配置；密码和 Key 不会从设备传回网页').waitFor();
    const cards = page.locator('section.slot');
    const add = page.getByRole('button', { name: '添加 Wi-Fi' });
    if (await cards.count() !== Math.max(1, saved.length)) throw new Error('wrong loaded card count');
    for (let i = 0; i < saved.length; i++) {
      if (await cards.nth(i).locator('input[type=text]').inputValue() !== saved[i]) throw new Error('saved slot moved');
    }
    if (saved.length === 0) {
      if (await cards.first().locator('input[type=text]').count() !== 1 || await cards.first().locator('select').count()) throw new Error('duplicate SSID control');
      await page.getByRole('button', { name: '扫描 Wi‑Fi' }).click();
      await page.getByText('扫描完成', { exact: true }).waitFor();
      const ssidInput = cards.first().locator('input[type=text]');
      const resultsId = await ssidInput.getAttribute('list');
      if (await page.locator(`datalist#${resultsId} option[value="Test AP"]`).count() !== 1) throw new Error('scanned network missing from SSID suggestions');
      if (await add.count()) throw new Error('add visible before configuration');
      await page.screenshot({ path: fileURLToPath(new URL('one-slot.png', output)), fullPage: true });
      await cards.first().locator('input[type=text]').fill('Network 1');
      await cards.first().locator('select').selectOption('1');
      if (await add.count()) throw new Error('add visible with missing new password');
      const passwordInput = cards.first().locator('input[type=password]');
      await passwordInput.click();
      for (const character of 'test-password') {
        await page.keyboard.type(character);
        if (!await passwordInput.evaluate(input => input === document.activeElement)) throw new Error('password input lost focus after a character');
      }
      if (await passwordInput.inputValue() !== 'test-password') throw new Error('password typing lost characters');
      for (let count = 1; count < 5; count++) {
        await add.click();
        if (await cards.count() !== count + 1) throw new Error('add did not create one card');
        if (await add.count()) throw new Error('add visible with empty newest card');
        await cards.nth(count).locator('input[type=text]').fill(`Network ${count + 1}`);
      }
      if (await add.count()) throw new Error('add exceeds five slots');
      await page.getByRole('button', { name: '应用设置' }).click();
      await page.getByText('设置已保存，仍处于设置模式', { exact: true }).waitFor();
      if (JSON.stringify(written) !== '[0,1,2,3,4]') throw new Error('not all wire slots sent');
    } else if (saved.length === 2) {
      await add.click();
      if (await cards.count() !== 3) throw new Error('cannot append saved slots');
      await page.screenshot({ path: fileURLToPath(new URL('three-slots.png', output)), fullPage: true });
      await page.getByRole('button', { name: '应用设置' }).click();
      await page.getByText('设置已保存，仍处于设置模式', { exact: true }).waitFor();
      if (JSON.stringify(written) !== '[0,1,2,3,4]') throw new Error('hidden empty slots not cleared');
    }
    if (saved.length === 0) {
      const apply = page.getByRole('button', { name: '应用设置', exact: true });
      if (!await apply.isEnabled()) throw new Error('saving exited setup');
      await page.getByRole('tab', { name: 'Magpie 配置' }).click();
      const endpoint = page.getByRole('textbox', { name: 'Magpie endpoint' });
      await endpoint.click();
      for (const character of 'https://magpie.test/proxy') {
        await page.keyboard.type(character);
        if (!await endpoint.evaluate(input => input === document.activeElement)) throw new Error('endpoint lost focus');
      }
      await page.getByLabel('Key 处理', { exact: true }).selectOption('1');
      const key = page.getByLabel('Magpie Key', { exact: true });
      await key.click();
      for (const character of 'web-key-0123456789') {
        await page.keyboard.type(character);
        if (!await key.evaluate(input => input === document.activeElement)) throw new Error('key lost focus');
      }
      await page.getByRole('tab', { name: '网络', exact: true }).click();
      if (await cards.first().locator('input[type=text]').inputValue() !== 'Network 1') throw new Error('tab switch lost network edits');
      await page.getByRole('tab', { name: 'Magpie 配置' }).click();
      if (await key.inputValue() !== 'web-key-0123456789') throw new Error('tab switch lost key');
      await page.getByLabel('允许明文 HTTP').check();
      await page.getByLabel('额度读取间隔', { exact: true }).fill('120');
      await page.getByLabel('显示时区', { exact: true }).fill('America/New_York');
      await page.getByLabel('额度布局', { exact: true }).selectOption('B');
      await page.getByLabel('自适应字号').uncheck();
      await apply.click();
      await page.getByText('设置已保存，仍处于设置模式', { exact: true }).waitFor();
      if (committedMagpie.flags !== 5 || committedMagpie.interval !== 120 || committedMagpie.timezone !== 'America/New_York') throw new Error('Web display settings not saved');
      if (await key.count()) throw new Error('key not cleared after save');
      if (!await apply.isEnabled()) throw new Error('Magpie save exited setup');
      if (committedMagpie.endpoint !== 'https://magpie.test/proxy' || !committedMagpie.keyPresent || network[0].ssid !== 'Network 1') throw new Error('Magpie save did not preserve settings');
      await page.getByRole('tab', { name: '网络', exact: true }).click();
      await apply.click();
      await page.getByText('设置已保存，仍处于设置模式', { exact: true }).waitFor();
      if (!committedMagpie.keyPresent) throw new Error('network save cleared Magpie');
      await page.reload();
      await page.getByRole('button', { name: '连接设备' }).click();
      await page.getByText('已读取设备配置；密码和 Key 不会从设备传回网页').waitFor();
      await page.getByRole('tab', { name: 'Magpie 配置' }).click();
      if (await endpoint.inputValue() !== 'https://magpie.test/proxy' || await key.count()) throw new Error('metadata reload exposed or lost saved configuration');
      if (await page.getByLabel('额度读取间隔', { exact: true }).inputValue() !== '120' ||
          await page.getByLabel('显示时区', { exact: true }).inputValue() !== 'America/New_York' ||
          await page.getByLabel('额度布局', { exact: true }).inputValue() !== 'B' ||
          !await page.getByLabel('允许明文 HTTP').isChecked() || await page.getByLabel('自适应字号').isChecked()) throw new Error('reconnect lost saved Web display settings');
      await page.screenshot({ path: fileURLToPath(new URL('magpie-settings.png', output)), fullPage: true });
      await page.getByRole('button', { name: '退出设置模式', exact: true }).click();
      await page.getByText('已退出设置模式，设备将使用已保存的配置', { exact: true }).waitFor();
      if (await apply.isEnabled()) throw new Error('explicit exit did not stop editing');
    }
    if (errors.length) throw new Error(errors.join('\n'));
    await page.close();
  }
  console.log('PASS: empty configuration shows one card; + requires completed visible slots; appends one at a time; caps at five; restores saved/sparse slots; submits all five wire slots');
} finally {
  await browser.close();
  await server.close();
}
