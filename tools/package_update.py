#!/usr/bin/env python3
"""Build the compiled SD index and the browser-installable, board-specific OTA bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b"ZLDPAK1\n"
MAX_ENTRIES = 2048
MAX_MANIFEST = 512 * 1024
MAX_ASSET = 32 * 1024 * 1024
MAX_FIRMWARE = 12 * 1024 * 1024


def sha256_file(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest() if hasattr(hashlib, "file_digest") else _digest(stream)


def _digest(stream):
    sha = hashlib.sha256()
    for chunk in iter(lambda: stream.read(65536), b""):
        sha.update(chunk)
    return sha.hexdigest()


def entries(directory):
    rows = []
    for path in sorted(directory.rglob("*"), key=lambda p: p.as_posix()):
        if path.is_symlink():
            raise ValueError(f"symlink in card package: {path}")
        if not path.is_file():
            continue
        name = path.relative_to(directory).as_posix()
        size = path.stat().st_size
        if (len(name) > 159 or not re.fullmatch(r"[A-Za-z0-9_./-]+", name)
                or any(part.startswith(".") for part in name.split("/"))
                or not re.search(r"\.[a-z0-9]{1,8}$", name)):
            raise ValueError(f"invalid asset name: {name}")
        if not 0 < size <= MAX_ASSET:
            raise ValueError(f"invalid asset size: {name}")
        rows.append((name, size, sha256_file(path)))
    if not 0 < len(rows) <= MAX_ENTRIES:
        raise ValueError("missing or oversized SD index")
    return rows


def manifest_bytes(rows):
    data = "".join(f"{sha} {size} {name}\n" for name, size, sha in rows).encode("ascii")
    if len(data) > MAX_MANIFEST:
        raise ValueError("oversized manifest")
    return data


def firmware_marker(sku, silicon, generation):
    return f"ZLD-BUNDLE-1/{sku}/{silicon}/{generation}/END".encode("ascii") + b"\0"


def write_index(directory, output):
    rows = entries(directory)
    generation = hashlib.sha256(manifest_bytes(rows)).hexdigest()
    data = (f'#define ZLD_GENERATION "{generation}"\n'
            'static const asset_entry_t active_entries[] = {\n' +
            "".join(f'    {{"{name}", "{sha}", {size}U}},\n' for name, size, sha in rows) +
            "};\n")
    output.write_text(data, encoding="ascii")


def package(directory, firmware, sku, silicon, output):
    rows = entries(directory)
    manifest = manifest_bytes(rows)
    generation = hashlib.sha256(manifest).hexdigest()
    firmware_size = firmware.stat().st_size
    if not 0 < firmware_size <= MAX_FIRMWARE:
        raise ValueError("firmware exceeds OTA slot")
    image = firmware.read_bytes()
    if image[0] != 0xE9 or firmware_marker(sku, silicon, generation) not in image:
        raise ValueError("firmware does not match SKU, silicon revision or current SD contents; rebuild it")
    header = json.dumps({"format": 1, "sku": sku, "silicon": silicon, "manifestSize": len(manifest),
                         "generation": generation, "firmwareSize": firmware_size,
                         "firmwareSha": hashlib.sha256(image).hexdigest()}, separators=(",", ":")).encode("ascii")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as stream:
        tmp = Path(stream.name)
        try:
            stream.write(MAGIC + struct.pack("<I", len(header)) + header + manifest)
            for name, _, _ in rows:
                with (directory / name).open("rb") as asset:
                    shutil.copyfileobj(asset, stream)
            stream.write(image)
            stream.close()
            tmp.replace(output)
        finally:
            tmp.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=ROOT / "sdcard")
    parser.add_argument("--index", type=Path, help="generate the C index used when building the firmware")
    parser.add_argument("--sku", choices=("31416", "31570"))
    parser.add_argument("--silicon", choices=("rev1", "rev3"))
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.index:
        write_index(args.assets, args.index)
    elif all((args.sku, args.silicon, args.firmware, args.output)):
        package(args.assets, args.firmware, args.sku, args.silicon, args.output)
    else:
        parser.error("specify --index or all four package arguments")


if __name__ == "__main__":
    main()
