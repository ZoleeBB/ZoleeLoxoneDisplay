const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const [page, name] = process.argv.slice(2);
const script = fs.readFileSync(page, 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const blob = new Blob([fs.readFileSync(name)]);
const elements = {};
const context = {TextDecoder, Blob, DataView, fetch: () => new Promise(() => {}),
  document: {getElementById: id => (elements[id] ??= {})}};
vm.runInNewContext(script, context);
(async () => {
  const device = {sku:'31416', silicon:'rev1'};
  const pack = await context.openPack(blob, device);
  assert.equal(pack.files.length, 1);
  assert.equal(pack.files[0].name, 'icons/a.png');
  assert.equal(await pack.files[0].blob.text(), 'A');
  assert.equal(new Uint8Array(await pack.firmware.arrayBuffer())[0], 0xe9);
  await assert.rejects(context.openPack(blob.slice(0,blob.size-1), device));
  await assert.rejects(context.openPack(blob, {...device, silicon:'rev3'}));
})().catch(error => {console.error(error); process.exitCode=1;});
