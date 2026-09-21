// Host test for the browser upload loop in web/index.html.
//
// putBlob() is the one piece of the UI with real state machinery: it chunks a
// blob, retries a failed chunk, and re-seeks when the device reports a
// different file size (HTTP 409). Rather than duplicate it here, this pulls the
// function straight out of index.html and runs it against a fake device.
//
//   node test/host/test_upload.mjs
import { readFileSync } from "node:fs";
import assert from "node:assert/strict";

const html = readFileSync(new URL("../../web/index.html", import.meta.url), "utf8");
const src = html.match(/\nasync function putBlob\(path, blob, job\)\{[\s\S]*?\n\}\n/);
assert.ok(src, "putBlob() not found in web/index.html — did it get renamed?");

const CHUNK = 48 * 1024;
let api;                                   // swapped per scenario
const human = b => b + "B";
const enc = encodeURIComponent;
const putBlob = new Function("api", "enc", "human", "CHUNK", "RETRIES",
  src[0] + "; return putBlob;")(( ...a) => api(...a), enc, human, CHUNK, 4);

const blob = n => ({ size: n, slice: (a, b) => ({ length: b - a }) });
const offsetOf = url => Number(new URL(url, "http://x").searchParams.get("offset"));
const ok = size => ({ status: 200, ok: true, json: async () => ({ ok: true, size }) });
const conflict = size => ({ status: 409, ok: false, json: async () => ({ error: "offset mismatch", size }) });

// 1. Happy path: a 3-chunk file arrives in order, offsets contiguous.
{
  const seen = [];
  api = async url => { seen.push(offsetOf(url)); return ok(0); };
  await putBlob("/a.bin", blob(CHUNK * 2 + 10), null);
  assert.deepEqual(seen, [0, CHUNK, CHUNK * 2], "chunks must be sent back to back");
}

// 2. An empty file still gets exactly one (final) request.
{
  let n = 0;
  api = async url => { n++; assert.ok(url.includes("final=1")); return ok(0); };
  await putBlob("/empty", blob(0), null);
  assert.equal(n, 1, "a zero-byte file is one final chunk, not zero and not a loop");
}

// 3. The regression: a flaky link that truncates many chunks mid-write.
//    Every 409 here advances the file, so it is progress, not desync — the old
//    code counted resyncs over the whole upload and gave up after five with
//    "upload out of sync" even though the transfer was healthy.
{
  const total = CHUNK * 30;
  let landed = 0, flaky = 0;
  api = async url => {
    const off = offsetOf(url);
    if (off !== landed) return conflict(landed);
    if (++flaky % 2 === 0) { landed += CHUNK / 2; return conflict(landed); }  // half written, then dropped
    landed = Math.min(landed + CHUNK, total);
    return ok(landed);
  };
  await putBlob("/big.bin", blob(total), null);
  assert.equal(landed, total, "a link that keeps truncating chunks must still finish");
}

// 4. A device stuck on one offset is a genuine fault and must still give up
//    rather than spin forever.
{
  api = async () => conflict(1024);
  await assert.rejects(() => putBlob("/x.bin", blob(CHUNK * 4), null), /stuck at/);
}

// 5. A device holding more bytes than we are sending is unrecoverable.
{
  api = async () => conflict(CHUNK * 99);
  await assert.rejects(() => putBlob("/x.bin", blob(CHUNK), null), /more than/);
}

console.log("test_upload: ALL PASS");
