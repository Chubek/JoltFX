/* Serve the built launch page first; this exercises its actual WASM module. */
import test from "node:test";
import assert from "node:assert/strict";
import { firefox } from "playwright";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

test("browser launch, typed animation, project interchange and history", { timeout: 90000 }, async () => {
  const browser = await firefox.launch({ headless: true });
  const directory = await mkdtemp(join(tmpdir(), "joltfx-browser-"));
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    await page.goto(process.env.JFX_WEB_URL ?? "http://localhost:8080/public/index.html");
    await page.getByRole("button", { name: "Add cube", exact: true }).waitFor({ timeout: 60000 });
    await page.getByRole("button", { name: "Add cube", exact: true }).click();
    await page.getByRole("button", { name: "Preview 3D workspace", exact: true }).click();
    const preview = page.locator("canvas").first();
    const first = await preview.evaluate(canvas => canvas.toDataURL());
    const source = '(import jolt.math (sin HALF-PI)) (defkernel move [time frame index value] (+ value (* time (sin HALF-PI))))';
    await page.getByLabel("Joltscript animation", { exact: true }).fill(source);
    await page.getByRole("button", { name: "Apply animation script", exact: true }).click();
    const frame = page.getByLabel("Frame", { exact: true });
    await frame.fill("30"); await frame.dispatchEvent("change");
    const moved = await preview.evaluate(canvas => canvas.toDataURL());
    assert.notEqual(moved, first);
    const [download] = await Promise.all([
      page.waitForEvent("download"), page.getByRole("button", { name: "Save project", exact: true }).click(),
    ]);
    const file = join(directory, "scene.jfx");
    await download.saveAs(file);
    const encoded = (await readFile(file, "utf8")).match(/^script 0 ([0-9a-f]+)$/m)?.[1];
    assert.ok(encoded);
    assert.equal(Buffer.from(encoded, "hex").toString("utf8"), source);
    await page.getByRole("button", { name: "Undo", exact: true }).click();
    assert.equal(await preview.evaluate(canvas => canvas.toDataURL()), first);
    await page.getByRole("button", { name: "Redo", exact: true }).click();
    assert.equal(await preview.evaluate(canvas => canvas.toDataURL()), moved);
    await page.getByLabel("Open project", { exact: true }).setInputFiles(file);
    await page.waitForFunction(input => input.value === "0", await frame.elementHandle());
    await frame.fill("30"); await frame.dispatchEvent("change");
    assert.equal(await preview.evaluate(canvas => canvas.toDataURL()), moved);
    assert.deepEqual(errors, []);
  } finally {
    await browser.close();
    await rm(directory, { recursive: true, force: true });
  }
});
