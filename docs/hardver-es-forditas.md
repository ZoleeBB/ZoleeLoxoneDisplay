# Hardver és firmware-fordítás

| Cikkszám | Alaplap | Hálózat |
| --- | --- | --- |
| 31416 | Waveshare ESP32-P4-WIFI6-Touch-LCD-4B | ESP32-C6, 2,4 GHz Wi-Fi |
| 31570 | Waveshare ESP32-P4-WIFI6-Touch-LCD-4B-ETH-2RO, gyári néven ESP32-P4-86-Panel-ETH-2RO | Wi-Fi és vezetékes Ethernet, IP101 PHY |

Mindkét eszköz 720 × 720 képpontos ST7703 MIPI DSI kijelzőt, GT911 érintővezérlőt és négybites SDMMC foglalatot használ. A 31570 Ethernet-csatlakozásához a megfelelő alsó panel szükséges. A LAN és a Wi-Fi DHCP-vel kap címet. Ha mindkettő aktív, a készülék a vezetékes címet jeleníti meg és azt az interfészt részesíti előnyben. A panelt a gyártó előírásának megfelelően kell táplálni. A kijelzőhöz FAT32-re formázott microSD-kártyát használj.

## A megfelelő bináris kiválasztása

A cikkszám mellett a P4 processzor szilíciumrevíziója is számít. A termékcímke PCB-revíziója és a processzor revíziója külön adat. Az USB-UART csatlakozást követően az esptool is kiírja az utóbbit.

```sh
python -m esptool --chip esp32p4 --port COM5 chip_id
```

| Szilícium | Profil |
| --- | --- |
| ESP32-P4 v1.x, köztük a forrásprojekt fizikai v1.3 panelje | `rev1` |
| ESP32-P4 v3.x | `rev3` |

Az ESP-IDF eltérő indítókódot készít a két családra. Mindig azt a fájlt töltsd le, amelyiknek a nevében a saját cikkszámod és szilíciumprofilod szerepel.

## Helyi build

Az ESP-IDF **v5.5.4** környezet aktiválása után az alábbi példa a 31570 v3.x változatot fordítja.

```sh
idf.py -B build-31570-rev3 -DPANEL_SKU=31570 -DPANEL_SILICON=rev3 build size
idf.py -B build-31570-rev3 -p COM5 flash monitor
```

Minden változathoz külön buildkönyvtárat használj. A firmware a panelbe épített C6-ot SDIO-n éri el a forrásprojekt rögzített ESP-Hosted és Wi-Fi Remote verzióival. A P4 szoftverének flash-elése a C6 gyári képét érintetlenül hagyja. A panel és az érintő vezérlését a Waveshare BSP 3.0.1 végzi. Az energiaábrához és a grafikus felülethez LVGL 9.5.0 tartozik.

## Telepítés GitHub Actions eredményből

Az adott profilhoz tartozó artifactot bontsd ki. A `-usb.bin` az indítót, partíciós táblát és alkalmazást tartalmazza. Első telepítéshez a megfelelő változatot a 0x0 címre írd. Az alábbi példa a 31416 cikkszámú v1.x panelé.

```sh
python -m esptool --chip esp32p4 --port COM5 write_flash 0x0 ZoleeLoxoneDisplay-31416-rev1-usb.bin
```

A hozzá tartozó `.zld` tartalmazza az SD-erőforrásokat is. Az eszköz webes frissítési oldalán tudod telepíteni. A verziócímkével kiadott build fájljai a GitHub Releases oldalon is megjelennek.

## Gyártói dokumentáció

- [Waveshare műszaki adatok és csatlakozók](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B)
- [Waveshare BSP 3.0.1 az ESP Component Registryben](https://components.espressif.com/components/waveshare/esp32_p4_wifi6_touch_lcd_4b/versions/3.0.1/readme)
- [Waveshare példaprogramok és kapcsolási rajzok](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B)
