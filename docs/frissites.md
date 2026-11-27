# Képcsomag és firmware frissítése

Az első USB-telepítés után nyisd meg a kijelzőn látható IP-címet, majd a **Frissítés** oldalt. Közvetlenül az `/update` címen is elérhető. A panelhez FAT32-es microSD-kártya szükséges.

## A kártya eltávolítása vagy cseréje

A kijelző írható microSD-kártyát igényel. Induláskor egy munkamenet-azonosítót ír a kártyára, majd működés közben két másodpercenként visszaolvassa. Az első olvasási hiba felfüggeszti a normál kezelést. Ha ugyanaz a kártya a következő ellenőrzéskor elérhető, a működés folytatódik. Két egymás utáni hiba után a panel újraindul, és az ekkor behelyezett kártyáról tölti be a beállításait. Kártya nélkül az indítóképernyő behelyezésre vár.

A webes profil olvasásánál, mentésekor és a firmware aktiválása előtt a készülék azonnal ellenőrzi a munkamenetet. Ha folyamatban van egy frissítési művelet, a helyreállító újraindítás megvárja a művelet lezárását. Tervezett kártyacsere előtt kapcsold ki a panelt, hogy az éppen írt fájlok mentése befejeződhessen.

## Első képcsomag

1. Kattints a **Képcsomag letöltése az SD-kártyára** gombra. Az internetkapcsolattal rendelkező kijelző a saját firmware-éhez tartozó Git-commitból tölti le a hiányzó fájlokat. A böngésző mutatja a haladást, a letöltés a lap bezárása után is folytatódik.
2. Amikor a képcsomag használatra kész, térj vissza a beállításokhoz. A beállított oldalak újraindítás nélkül is megjelenhetnek.
3. Ha az eszköz nem éri el a GitHubot, töltsd le **a telepített USB-képpel együtt kiadott** `.zld` fájlt a számítógépre. Válaszd ki a frissítési oldalon, majd kattints a **Képcsomag telepítése** gombra. Ez az út csak helyi hálózati elérést igényel a kijelzőhöz.

Az online telepítés a firmware-be mentett Git-azonosítót használja. Saját, még közzé nem tett módosításokhoz a helyi fordításból készült csomag illik. Minden képet a firmware-be fordított SHA-256-jegyzék alapján ellenőriz a panel.

## Frissítés új verzióra

A kiadásokban és az Actions build letölthető mellékletei között szerepel a hardverhez tartozó `ZoleeLoxoneDisplay-<SKU>-<szilícium>.zld` fájl. A csomag neve például `ZoleeLoxoneDisplay-31416-rev1.zld`. A saját változatodat a frissítési oldal tetején látod.

1. Válaszd ki a megfelelő `.zld` fájlt, majd kattints a **Firmware és képek frissítése** gombra.
2. A panel összeveti a csomagot a kártyával. A böngésző kizárólag az eltérő vagy hiányzó SD-fájlokat küldi el. A feltöltés sorrendjét és a fájlok számát az oldal jelzi.
3. A firmware az inaktív alkalmazáspartícióra kerül. A panel a teljes firmware SHA-256-át, az ESP-képet, a projektazonosítót, a hardverváltozatot, a képjegyzéket és a kártyán lévő fájlokat ellenőrzi, mielőtt aktiválja az új verziót.
4. Sikeres telepítés után a kijelző újraindul. Az új alkalmazás ismét ellenőrzi a szükséges SD-fájlokat. A sikertelen próbaindításkor a rendszer visszaállhat a korábbi firmware-re.

A félbeszakadt telepítés ugyanazzal a csomaggal újrakezdhető. A már épségben felkerült fájlokat a panel megtartja. A futó verzió és az előző firmware erőforrásai használhatók maradnak, mert az egyes képek tartalmuk szerint elkülönülő fájlba kerülnek. A régi képek ezért foglalhatnak helyet a kártyán. A konfiguráció külön fájlokban található.

## Csomag készítése

Az ESP-IDF-fordítás a `sdcard/` tartalmából előállítja a beégetett jegyzéket. Ha módosul egy kép, a hozzá tartozó firmware-t is újra kell fordítani. Az azonos fájlú új verziónál az SD-adatátvitel kimarad.

```bash
python3 tools/package_update.py --sku 31416 --silicon rev1 \
    --firmware build/zolee_loxone_display.bin \
    --output dist/ZoleeLoxoneDisplay-31416-rev1.zld
python3 -m unittest discover -s tests -p test_update.py -v
```

[A négy firmware-változat fordítása és az első USB-telepítés](hardver-es-forditas.md)
