// Optional real WebGPU integration test. Requires Playwright with Chromium.
// Usage: node test-webgpu.mjs SCENE.html CPU.png
// MONKEYTRACE_PLAYWRIGHT may name an absolute Playwright module directory.
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import assert from 'node:assert/strict';

const [htmlPath, pngPath] = process.argv.slice(2);
if (!htmlPath || !pngPath) throw new Error('Usage: test-webgpu.mjs SCENE.html CPU.png');
const modulePath = process.env.MONKEYTRACE_PLAYWRIGHT;
const {chromium} = await import(modulePath ? pathToFileURL(resolve(modulePath, 'index.mjs')).href : 'playwright');
const html = await readFile(htmlPath);
const png = await readFile(pngPath);
const server = createServer((req, res) => {
  const image = req.url === '/cpu.png';
  res.setHeader('Content-Type', image ? 'image/png' : 'text/html');
  res.end(image ? png : html);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let browser;
try {
  browser = await chromium.launch({headless: true, executablePath: process.env.MONKEYTRACE_CHROMIUM || undefined, args: ['--enable-unsafe-webgpu', '--use-angle=swiftshader', '--enable-features=Vulkan', '--no-sandbox']});
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', err => errors.push(String(err)));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(() => ['complete', 'error'].includes(document.getElementById('image').dataset.state), undefined, {timeout: 60000});
  assert.equal(await page.locator('#image').getAttribute('data-state'), 'complete', await page.locator('#status').textContent());
  const comparison = await page.evaluate(async () => {
    const gpu = document.getElementById('image');
    const cpuImage = await createImageBitmap(await (await fetch('/cpu.png')).blob());
    if (cpuImage.width !== gpu.width || cpuImage.height !== gpu.height) throw new Error('CPU/GPU dimensions differ');
    const cpu = document.createElement('canvas'); cpu.width = gpu.width; cpu.height = gpu.height;
    cpu.getContext('2d').drawImage(cpuImage, 0, 0); cpuImage.close();
    const a = gpu.getContext('2d').getImageData(0, 0, gpu.width, gpu.height).data;
    const b = cpu.getContext('2d').getImageData(0, 0, gpu.width, gpu.height).data;
    let sum = 0, max = 0, different = 0;
    for (let i = 0; i < a.length; i++) {
      const delta = Math.abs(a[i] - b[i]); sum += delta; max = Math.max(max, delta);
      if (delta > 2) different++;
    }
    return {mean: sum / a.length, max, fraction: different / a.length, status: document.getElementById('status').textContent};
  });
  assert.ok(comparison.mean < 1 && comparison.fraction < 0.01, JSON.stringify(comparison));
  // Reuse resources and ensure a second dispatch/readback succeeds.
  await page.locator('#render').click();
  await page.waitForFunction(() => ['complete', 'error'].includes(document.getElementById('image').dataset.state));
  assert.equal(await page.locator('#image').getAttribute('data-state'), 'complete', await page.locator('#status').textContent());
  assert.deepEqual(errors, []);
  console.log(JSON.stringify(comparison));
} finally {
  if (browser) await browser.close();
  await new Promise(resolve => server.close(resolve));
}
