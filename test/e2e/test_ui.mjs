// Browser end-to-end tests: Chromium (Playwright) driving web/index.html as
// served by the firmware's real server code in test/hostsim.
//
//   make -C test/hostsim && node test/e2e/test_ui.mjs
//
// Needs Playwright: `npm i -D playwright && npx playwright install chromium`
// (or a global install). Screenshots land in $SHOTS when it is set.
import { spawn } from "node:child_process";
import { createRequire } from "node:module";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import net from "node:net";

const require = createRequire(import.meta.url);
let chromium;
for (const p of ["playwright", "/opt/node22/lib/node_modules/playwright"]) {
  try { ({ chromium } = require(p)); break; } catch (e) {}
}
if (!chromium) { console.log("playwright not installed — skipping UI tests"); process.exit(0); }

const HERE = path.dirname(new URL(import.meta.url).pathname);
const SIM = path.join(HERE, "../hostsim/pocketwui-sim");
const SHOTS = process.env.SHOTS;
const PASSWORD = "ui-test-pass";
let failures = 0;
const check = (cond, what) => { console.log((cond ? "ok    " : "FAIL  ") + what); if (!cond) failures++; };

const port = await new Promise(r => { const s = net.createServer().listen(0, () => { const p = s.address().port; s.close(() => r(p)); }); });
const card = fs.mkdtempSync(path.join(os.tmpdir(), "pocketwui-ui-"));
const execSync = (await import("node:child_process")).execSync;
execSync(`python3 ${path.join(HERE, "make_demo_card.py")} ${card}`);
const sim = spawn(SIM, ["--root", card, "--port", String(port), "--password", PASSWORD], { stdio: "ignore" });
await new Promise(r => setTimeout(r, 400));
const B = `http://127.0.0.1:${port}`;

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1200, height: 760 }, acceptDownloads: true });
const p = await ctx.newPage();
const errors = [];
p.on("pageerror", e => errors.push(e.message));
// The browser logs every 4xx as a console error; those are checked by URL
// below instead (only the deliberate wrong-password attempt may fail).
p.on("console", m => { if (m.type() === "error" && !m.text().startsWith("Failed to load resource")) errors.push(m.text()); });
const bad = [];
p.on("response", r => { if (r.status() >= 400) bad.push(r.status() + " " + r.request().method() + " " + r.url().replace(B, "")); });
const shot = async name => { if (SHOTS) await p.screenshot({ path: path.join(SHOTS, name + ".png") }); };
const settle = () => p.waitForTimeout(350);
const rows = () => p.locator("#list tbody tr .nm .t").allTextContents();
const rowMenu = async (name, item) => {
  await p.locator("#list tbody tr", { hasText: name }).locator("td.act button").click();
  await p.locator(".menu button", { hasText: item }).click();
};
const disk = f => path.join(card, f);

try {
  // sign-in
  await p.goto(B); await settle();
  await shot("login");
  await p.fill("#pw", "wrong"); await p.click("#loginBtn");
  await p.waitForFunction(() => document.querySelector("#loginMsg").textContent.length > 0);
  check((await p.textContent("#loginMsg")).includes("wrong password"), "wrong password shows a message");
  await p.fill("#pw", PASSWORD); await p.click("#loginBtn"); await settle();
  check((await rows()).join(",") === "Documents,downloads,firmware,Music,Photos,README.txt", "root listing, folders first");
  await shot("home");

  // navigation + gallery
  await p.click("text=Photos"); await settle();
  await p.click("text=Road trip"); await settle();
  await p.click("text=beach.png"); await settle();
  check((await p.textContent("#sheet .t")) === "beach.png", "image preview opens");
  await p.keyboard.press("ArrowRight"); await settle();
  check((await p.textContent("#sheet .t")) === "forest.png", "arrow key moves through the gallery");
  await shot("gallery");
  await p.keyboard.press("Escape");
  await p.click(".crumbs a:has-text('Photos')"); await settle();
  check((await rows()).includes("cat.png"), "breadcrumb navigates up");

  // sorting
  await p.click("th:has-text('Name')"); await settle();
  check((await rows())[0] === "Road trip" && (await rows())[1] === "cat.png", "sort toggles, folders stay first");
  await p.click("th:has-text('Name')"); await settle();

  // upload
  await p.click(".crumbs a[title='SD card']"); await settle();
  const up1 = path.join(os.tmpdir(), "ui-upload-small.txt"), up2 = path.join(os.tmpdir(), "ui-upload-big.bin");
  fs.writeFileSync(up1, "uploaded from the browser\n");
  const bigData = Buffer.alloc(700 * 1024).map((_, i) => (i * 31) & 255);
  fs.writeFileSync(up2, bigData);
  await p.setInputFiles("#file", [up1, up2]);
  await p.waitForTimeout(1500);
  check(fs.readFileSync(disk("ui-upload-big.bin")).equals(bigData), "upload of a 700 KB file is byte-identical");
  check((await rows()).includes("ui-upload-small.txt"), "uploaded files appear in the list");

  // new folder, rename
  await p.click("#bNew"); await p.click(".menu button:has-text('New folder')");
  await p.fill("#sheet input", "Projects"); await p.keyboard.press("Enter"); await settle();
  check(fs.statSync(disk("Projects")).isDirectory(), "new folder");
  await rowMenu("ui-upload-small.txt", "Rename");
  await p.fill("#sheet input", "hello.txt"); await p.keyboard.press("Enter"); await settle();
  check(fs.existsSync(disk("hello.txt")) && !fs.existsSync(disk("ui-upload-small.txt")), "rename");

  // edit + save
  await p.click("text=hello.txt"); await settle();
  await p.fill("#edit", "edited in PocketWUI\n");
  await p.keyboard.press("Control+s"); await p.waitForTimeout(600);
  check(fs.readFileSync(disk("hello.txt"), "utf8") === "edited in PocketWUI\n", "text editor saves with Ctrl+S");

  // copy with the folder picker (a background job)
  await rowMenu("hello.txt", "Copy to");
  await p.click("#sheet .list div:has-text('Projects')"); await settle();
  await p.click("#sheet .foot button:has-text('Copy here')");
  await p.waitForTimeout(1500);
  check(fs.readFileSync(disk("Projects/hello.txt"), "utf8") === "edited in PocketWUI\n", "copy to a picked folder");

  // multi-select move
  await p.locator("#list tbody tr", { hasText: "ui-upload-big.bin" }).locator("input[type=checkbox]").check();
  await p.locator("#list tbody tr", { hasText: "hello.txt" }).locator("input[type=checkbox]").check();
  check((await p.textContent("#selbar b")) === "2 selected", "selection bar counts");
  await shot("selection");
  await p.click("#selbar button:has-text('Move')");
  await p.click("#sheet .list div:has-text('Projects')"); await settle();
  await p.click("#sheet .foot button:has-text('Move here')"); await settle();
  // hello.txt was already copied into Projects: the move asks before replacing.
  check((await p.textContent("#sheet header")).includes("Replace"), "moving onto an existing name asks first");
  await p.click("#sheet .foot button:has-text('Replace')"); await p.waitForTimeout(800);
  check(fs.existsSync(disk("Projects/ui-upload-big.bin")) && !fs.existsSync(disk("ui-upload-big.bin")) &&
        !fs.existsSync(disk("hello.txt")) && fs.existsSync(disk(".trash/hello.txt")), "move selection; the replaced file went to the Trash");

  // download folder as zip
  const [zdl] = await Promise.all([p.waitForEvent("download"), rowMenu("Projects", "Download as ZIP")]);
  const zbuf = fs.readFileSync(await zdl.path());
  check(zdl.suggestedFilename() === "Projects.zip" && zbuf.readUInt32LE(0) === 0x04034b50, "folder downloads as a ZIP");

  // delete -> trash -> empty
  await rowMenu("README.txt", "Move to Trash"); await settle();
  check(fs.existsSync(disk(".trash/README.txt")), "delete goes to the Trash");
  await p.click("#bMore"); await p.click(".menu button:has-text('Trash')"); await settle();
  check((await rows()).includes("README.txt") && (await rows()).includes("hello.txt"), "Trash view lists it");
  await shot("trash");
  await p.click("#banner button:has-text('Empty Trash')");
  await p.click("#sheet .foot button:has-text('Empty Trash')"); await settle();
  check(!fs.existsSync(disk(".trash")), "Empty Trash");

  // search
  await p.fill("#q", "png"); await p.keyboard.press("Enter"); await p.waitForTimeout(600);
  check((await rows()).length === 5, "whole-card search finds 5 PNGs");
  await shot("search");
  await rowMenu("cat.png", "Show in folder"); await settle();
  check((await p.textContent("#crumbs")).includes("Photos"), "search result → show in folder");

  // markdown
  await p.click(".crumbs a[title='SD card']"); await settle();
  await p.click("text=Documents"); await settle();
  await p.click("text=Shopping list.md"); await settle();
  check(await p.locator(".md h1").count() === 1 && await p.locator(".md li").count() === 3, "Markdown renders");
  await shot("markdown");
  await p.keyboard.press("Escape");

  // firmware file
  await p.click(".crumbs a[title='SD card']"); await settle();
  await p.click("text=downloads"); await settle();
  await p.click("text=Bruce.bin"); await settle();
  check((await p.textContent("#sheet .body")).includes("isn't a firmware file"), "random .bin is explained, not installed");
  await p.keyboard.press("Escape");

  // settings: wifi + usb drive
  await p.click("#bSet"); await settle();
  await p.click("button:has-text('Scan for networks')"); await settle();
  check(await p.locator("#sheet .list >> text=Cafe Guest").count() === 1, "Wi‑Fi scan lists networks");
  await shot("settings-wifi");
  await p.click(".tabs button:has-text('USB')"); await settle();
  await shot("settings-usb");
  await p.click("button:has-text('Lend it to the computer')"); await settle();
  check((await p.textContent("#banner")).includes("on your computer"), "USB drive mode shows the banner");
  await p.click("#banner button:has-text('Take it back')"); await settle();
  check(await p.locator("#banner.hidden").count() === 1, "taking the card back clears the banner");

  // console
  await p.click("#bTerm"); await p.fill("#cmd", "ls /Documents"); await p.keyboard.press("Enter"); await settle();
  check((await p.textContent("#out")).includes("Shopping list.md"), "console ls");

  // phone layout
  await p.setViewportSize({ width: 390, height: 820 }); await p.click("#bTerm"); await settle();
  await p.click(".crumbs a[title='SD card']"); await settle();
  await shot("phone");
  check(await p.locator("td.num").first().isHidden(), "phone layout hides the size/date columns");

  check(errors.length === 0, "no page errors" + (errors.length ? ": " + errors.join(" | ") : ""));
  // Expected: the deliberate wrong password, and the name clash that triggered "Replace?".
  const expected = ["401 POST /api/login", "409 POST /api/rename?from=%2Fhello.txt&to=%2FProjects%2Fhello.txt"];
  const unexpected = bad.filter(b => !expected.includes(b));
  check(unexpected.length === 0, "no unexpected failed requests" + (unexpected.length ? ": " + unexpected.join(" | ") : ""));
} catch (e) {
  console.log("FAIL  exception: " + e.message);
  failures++;
} finally {
  await browser.close();
  sim.kill();
  fs.rmSync(card, { recursive: true, force: true });
}
console.log(failures ? `\n${failures} failure(s)` : "\nall UI tests passed");
process.exit(failures ? 1 : 0);
