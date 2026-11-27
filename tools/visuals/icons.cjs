// Usage: node icons.cjs /path/to/lucide-0.468.0; requires sharp.
const fs = require('node:fs/promises');
const path = require('node:path');
const sharp = require('sharp');

async function main() {
    const upstream = process.argv[2];
    if (!upstream) throw new Error('Supply a Lucide 0.468.0 checkout');
    const destination = path.resolve(__dirname, '../../sdcard/icons');
    const rows = await fs.readFile(path.join(__dirname, 'icons.tsv'), 'utf8');
    await fs.mkdir(destination, {recursive: true});
    for (const row of rows.trim().split(/\r?\n/)) {
        const [id, source] = row.split('\t');
        const svg = await fs.readFile(path.join(upstream, 'icons', source + '.svg'), 'utf8');
        const white = svg.replace('currentColor', '#ffffff').replace('stroke-width="2"', 'stroke-width="1.65"');
        await sharp(Buffer.from(white), {density: 576}).resize(64, 64).ensureAlpha()
            .png({compressionLevel: 9}).toFile(path.join(destination, id + '.png'));
    }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
