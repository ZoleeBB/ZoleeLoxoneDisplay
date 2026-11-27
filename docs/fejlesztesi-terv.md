# INIT fejlesztési terv

A kiinduló állapot a [ZoleeEnergyFlowDisplay 6786e84 kiadási fája](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/tree/6786e8428c42d28fe43ba0765eb3748c125ed645). A Miniserver, a funkcióblokkok, a kaputelefon és a négyzetes energiakép átemelhető ebből a változatból. A hálózati és grafikus illesztés a Waveshare 4B hardverhez készül.

| Lépés | Eredmény | Elfogadási próba |
|---|---|---|
| 1. Hardver | SKU 31416 és 31570, 720×720-as kijelző, érintés, SD és hálózat | Build a célprofilokra, első indulás üres SD-vel, érintési sarkok és háttérvilágítás |
| 2. Üzembe helyezés | Wi-Fi hálózatválasztó, IP-címes képernyő és webes beállítás | Hibás jelszó javítása, újraindítás, Ethernet a 31570-en |
| 3. Oldalszerkesztő | Sorrendezhető fülek, egyszínű ikonok, színválasztó és képes háttértár | Mentés, átnevezés, sorrendcsere, újratöltés, magyar ékezetek |
| 4. Loxone Direct | Hitelesítés, struktúra, állapotfolyam és blokkvezérlés | Valós Miniserveren külső állapotváltozás, művelet, kapcsolat bontása és helyreállása |
| 5. Szobakezelés | Egyelemű részletnézet, több elemnél lista, világítási kimenetek, klíma | Aktív hangulat, RGBW/dimmer, félfokos termosztátlépés és visszajelzés |
| 6. Energia és kaputelefon | 4B Slave energiakép és fülszintű hozzárendelés, csengetésre kép és hang | Ismert előjelű adatok, csengetés–beszéd–kapunyitás–bontás, ismételt hívás |
| 7. Környezeti fény | Külön éjszakai vezérlés és kikapcsolható inaktivitási idő | Éjfélen átnyúló időzítés, külső jel, halvány óra, ébresztés érintéssel |
| 8. Kiadás | Összetartozó firmware és SD-csomag, külön első telepítés | Változatlan fájl kihagyása, megszakítás és újrapróbálás, eltérő hardver elutasítása |
| 9. Átadás | Magyar útmutató, README-banner, forrásprojekt támogatási címei | Dokumentációs hivatkozások, automatikus tesztek, fizikai mérési jegyzőkönyv |

## Készülékes ellenőrzés

Az elfogadási próbákat mindkét cikkszámon rögzíteni kell az adott panel szilíciumrevíziójával és firmware-azonosítójával együtt. A memória összevetési pontjai az üres lap, a világítási részletek, az energiakép, a hívás megnyitása és tíz bontás utáni nyugalmi állapot. Külön mérendő a szabad belső DMA-memória és a legnagyobb összefüggő blokk. A kaputelefonnál ellenőrizendő a választott hang ismétlése, a csengetésből beszélgetésbe váltás és a készüléken hallható hangerő. A képernyőkímélőnél a nappali szürke és az éjszakai vörös óra olvashatóságát mindkét modellen érdemes megnézni a beállított éjszakai fényerőn.

## Az INIT ág ellenőrzései

| Ellenőrzés | Eredmény |
| --- | --- |
| Firmware és USB-kép | Az ESP-IDF 5.5.4 mindkét cikkszámra, v1.x és v3.x processzorprofilra lefordította és összelinkelte az alkalmazást. A csomagellenőrzés a firmware azonosítóját és az egyes SD-fájlok SHA-256 értékét is ellenőrzi. |
| Valódi kezelőfelület szimulált adatokkal | LVGL 9.5.0 és szimulált érintőeszköz ellenőrzi a Wi-Fi-billentyűzetet, a füleket, a termosztátot, az aktív hangulatot, az RGBW-kimenetet és a félkövér óráról érintéssel történő visszalépést. A palettateszt világos, sötét és telített színeken ellenőrzi a mezők árnyalatát és a feliratok kontrasztját. |
| Webes szerkesztő | A kiszolgált HTML/CSS/JS felületet asztali és 390 képpont széles mobil Chromiumban, helyi API-tesztadatokkal próbáltuk ki. A két kategóriába rendezett háttérképek és az ikonok a tényleges csomagból töltődtek be. |
| Kártya és frissítés | A tesztek lefedik a hiányzó fájlt, a sérült SHA-t, a megszakított másolást, a folytatást, a téves célpanel elutasítását és az SD eltávolítását a firmware írása közben. |
| Csengőhangok | Az Energy Flow Display v4.0.0 kiadásának 18 hangos csomagjával ellenőriztük az online és a helyi telepítési útvonalat. Az audiofeladat natív próbája lefedi a mentett WAV kiválasztását, az ismétlést, a némítást, a hívás átvételét és a memória felszabadítását. |
| Állapot és profil | Automatizált próba ellenőrzi a titkosított mentést és helyreállítást, a Loxone-értékek és lapok hozzárendelését, a napi integrálást és a SIP/RTP hangprotokollt. |

A fizikai beüzemelés nyitott próbái a panelre írás, a valós Miniserverrel végzett állapot- és parancspróba, a hangszóró és mikrofon, az Ethernetes panel, valamint a készüléken mért fényerő és szabad DMA-memória. Az előnézetek és az ismételhető szimulátorteszt a kezelési útmutatóból nyithatók meg. [Kijelzőképek és tesztkörnyezet](screens.md)
