# 720 × 720-as LVGL előnézet és érintési próba

A Linux-program a firmware kezelőfelületét, grafikus elemeit, magyar fontjait és az SD-kártya képeit rajzolja ki. A minta hőfok, a fényjelenetek és az energiaadatok a fixture-ből jönnek. A futás tényleges LVGL mutatóeszközzel érinti meg a lapokat és a gombokat. A kaputelefon médiája, a Wi-Fi-rádió, az ESP32 memóriaelosztása és a valódi Miniserver fizikai ellenőrzést igényel.

Ubuntu alatt C/C++ fordító, CMake, Ninja és a `libcjson-dev` csomag szükséges. A projekt gyökeréből indítható.

```sh
git clone --depth=1 --branch v9.5.0 https://github.com/lvgl/lvgl.git /tmp/zld-lvgl
cmake -S simulator -B /tmp/zld-preview -G Ninja -DLVGL_DIR=/tmp/zld-lvgl
cmake --build /tmp/zld-preview --parallel 4
mkdir -p /tmp/zld-images
/tmp/zld-preview/panel-preview "$PWD/sdcard" "$PWD/simulator/fixture.json" /tmp/zld-images
```

Az előnézetek 720 × 720-as PPM-fájlok. A program ellenőrzi a Wi-Fi jelszómezőt, a webes IP-címet, a termosztátparancsokat, a jelenetválasztást, az RGBW-ablakot, az energiafül húzását, a képernyőkímélő fényerejét és az első érintés kizárólagos ébresztését. A készenléti órán a teszt képpontokból ellenőrzi a színt az időablak váltásakor, a Miniserver-jel megváltozásakor és jelkimaradáskor. Külön ellenőrzi az alsó feliratot és a felső ikonsort. A napi összesítő számítását és SD-visszaállítását a `tests/daily/run.sh` külön teszteli.

A Touch módhoz a program a valódi LVGL-érintési eseményeket adja a firmware-nek. A szimulált Miniserver-határfelület rögzíti az átadott zónát, a koppintások számát, a munkamenetet és az időt. A teszt a határvonalak egymás melletti képpontjait, a 150, 400 és 1000 ms-os időablakot, a nyomva tartást és az alsó sávról indított húzást is ellenőrzi. Profilváltás, hívás, kapcsolatvesztés és feloldás közben a félbehagyott koppintásokat külön vizsgálja. A képpontpróba az óra, a felirat és a szólogó teljes színét ellenőrzi. A rombusz mind a négy élén megméri az 50%-os fedettségű vonal színét a fekete háttéren. Zárolás közben nappal és éjszaka is módosítja a nappali színt, majd ellenőrzi a következő napszakváltást és a három képpont széles osztóvonal takarását a felirat mögött.

| Mentett előnézet | Nézet |
|---|---|
| `standby-touch.ppm` | Alapértelmezett nappali szürke |
| `standby-touch-custom.ppm` | Egyedi nappali kékesszürke |
| `standby-touch-night.ppm` | Éjszakai tompa vörös |
