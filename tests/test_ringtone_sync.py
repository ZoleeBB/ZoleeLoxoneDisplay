"""Run production release download, deflate/CRC, SD promotion and native WAV reader."""
from pathlib import Path
import hashlib
import io
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/ringtones"
COMP = ROOT / "components"
TONES = COMP / "update/tones"


class RingtoneSync(unittest.TestCase):
    def test_real_source_zip_and_recovery(self):
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            for stub in ["esp_http_server.h", "esp_crt_bundle.h", "esp_http_client.h", "esp_heap_caps.h", "mbedtls/sha256.h"]:
                path = work / stub
                path.parent.mkdir(exist_ok=True)
                path.touch()
            cjson = ROOT / "managed_components/espressif__cjson/cJSON"
            json_flags = (["-I", str(cjson), str(cjson / "cJSON.c")] if cjson.exists() else ["-I/usr/include/cjson", "-lcjson"])
            binary = work / "verify"
            subprocess.run(["cc", "-std=gnu17", "-Wno-deprecated-declarations", '-DZLD_CARD_DIR="card"',
                "-include", str(FIXTURE / "host.h"), "-I", str(work), "-I", str(TONES),
                "-I", str(COMP / "update/include"), "-I", str(COMP / "door_media"),
                str(FIXTURE / "verify.c"), str(COMP / "door_media/door_ringtone.c"),
                *[str(TONES / name) for name in ("tone_source.c","tone_store.c","app_ringtone_zip.c","app_ringtone_wav.c")],
                *json_flags, "-lz","-lcrypto","-lm","-o",str(binary)], check=True)
            bundle = work / "tones.zip"
            official = os.environ.get("ZLD_TEST_UPSTREAM_TONES")
            if official:
                shutil.copy(official, bundle)
            else:
                wav = io.BytesIO()
                with wave.open(wav, "wb") as f:
                    f.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
                    f.writeframes(b'\x03\x00' * 4096)
                names = ["tubular-chime.wav","alternate.wav"]
                with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED) as z:
                    z.writestr("ringtone.json",json.dumps({"schema":1,"kind":"intercom-ringtones",
                        "sounds":[{"file":n,"title":n[:-4],"license":"CC0"} for n in names]}))
                    for n in names:
                        z.writestr(n,wav.getvalue())
            with zipfile.ZipFile(bundle) as z:
                sounds = json.loads(z.read("ringtone.json"))["sounds"]
                selected = next(s["file"] for s in sounds if s["file"] != "tubular-chime.wav")
                (work/"selected.wav").write_bytes(z.read(selected))
                (work/"bundled.wav").write_bytes(z.read("tubular-chime.wav"))
            name = "ZoleeEnergyFlowDisplay-v4.0.0-ringtones.zip"
            sums = work/"SHA256SUMS.txt"
            sums.write_text(hashlib.sha256(bundle.read_bytes()).hexdigest()+"  "+name+"\n")
            base = "https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/releases/download/v4.0.0/"
            (work/"release.json").write_text(json.dumps({"draft":False,"prerelease":False,"tag_name":"v4.0.0",
                "assets":[{"name":k,"size":p.stat().st_size,"state":"uploaded","browser_download_url":base+k}
                          for k,p in [(name,bundle),("SHA256SUMS.txt",sums)]]}))
            release_fixture = os.environ.get("ZLD_TEST_UPSTREAM_RELEASE")
            sums_fixture = os.environ.get("ZLD_TEST_UPSTREAM_CHECKSUMS")
            if release_fixture and sums_fixture:
                shutil.copy(release_fixture, work / "release.json")
                shutil.copy(sums_fixture, sums)
            for mode in ("install","reload"):
                subprocess.run([str(binary),mode,str(len(sounds)),selected],cwd=work,check=True)
