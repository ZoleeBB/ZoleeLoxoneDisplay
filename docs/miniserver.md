# Miniserver és funkcióblokkok

A kijelző közvetlenül a helyi Miniserverhez kapcsolódik. A kliens a Zolee Energy Flow Display Gen2 / 17.x rendszerekhez készült megvalósítására épül. Más Miniserver-generációval vagy jelentősen eltérő firmware-verzióval a helyi telepítésen is ellenőrizd a katalógust, az állapotfrissítést és a használt blokkokat.

## Első kapcsolat

1. Nyisd meg a kijelzőn megjelenő IP-címet egy böngészőben. A **Miniserver** oldalon add meg a Miniserver helyi IP-címét vagy gépnevét, portját, felhasználónevét és jelszavát. A mezőbe csak a gépnév vagy cím kerül.
2. Helyi WS-kapcsolatnál a szokásos port 80. WSS használatához jelöld be a **Titkosított kapcsolat (TLS)** négyzetet, add meg a HTTPS-portot és illeszd be a szerver hitelesítésszolgáltatójának PEM-tanúsítványát. A megadott cím egyezzen a tanúsítványban szereplő névvel.
3. Kattints a **Mentés** gombra. A kijelző önállóan bejelentkezik, letölti az adott felhasználónak látható struktúrát, majd feliratkozik az élő állapotokra. Új funkció vagy megváltozott UUID esetén kérj friss struktúrát az **Adatlista frissítése** gombbal.
4. A **Lapfülek** oldalon adj hozzá egy elemet. Válaszd ki a funkcióját, majd a hozzá tartozó vezérlőt vagy mért állapotot. Egy fülre 12 elem, egy kijelzőre 8 fül fér. Egyetlen elem teljes oldalas kezelőt kap, több elem esetén a listából érhető el az adott kezelő.
5. Ellenőrizd az első élő visszajelzést. Kapcsolónál az `active` állapot változása, hőmérsékletnél a ténylegesen mért érték mutatja, hogy a visszajelzés is megfelelően van párosítva.

Az adatlistához olyan felhasználót válassz, amely eléri a kívánt elemek vizualizációját. Egy blokknál a **Vezérlőblokk** azonosítja a parancs címzettjét. Egy mért értéknél a **Megjelenített állapot** a blokk adott kimenetét választja ki. A vizualizációs jelszóval védett blokkokhoz a panel nem kér és nem tárol külön vizualizációs kódot.

## Melyik elemet válaszd?

| Elem a kijelzőn | Loxone-típus vagy állapot | Használat |
| --- | --- | --- |
| Mért érték | Numerikus állapot-UUID | Aktuális mérés és a megadott mértékegység |
| Nyomógomb | `Pushbutton`, `Switch`, `TimedSwitch` | Egyszeri impulzus |
| Kapcsoló | `Switch` | Be- és kikapcsolás, tényleges `active` visszajelzés |
| Világításvezérlés | `LightControllerV2` | Világítási képek kiválasztása és az aktív képek jelölése. A részletes nézet külön kezeli a kapcsolókat, dimmereket és a `ColorPickerV2` kimeneteket. |
| Termosztát | `IRoomControllerV2` | Mért és célhőmérséklet, fűtés/hűtés és 0,5 °C-os léptetés |
| Árnyékolás | Automatikus `Jalousie`, `CentralJalousie` | Fel, stop és le. Az egyedi automatikus árnyékoló pozíciója százalékban is megadható. |
| Dimmer és központi világítás | `Dimmer`, `CentralLightController` | Fényerő, be/ki, illetve központi kapcsolás |
| Analóg értékállító | `Slider` | A Miniserver tartományát és lépésközét követő beállítás |
| Időzített kapcsoló | `TimedSwitch` | Impulzus, kikapcsolás és a hátralévő idő |
| Ajtó–ablak összesítő | `WindowMonitor` | Összesítők és a nyílászárók állapotlistája |
| Kapu és motoros ablak | `Gate`, `Window` | Nyitás, stop és zárás a blokkon érvényes iránytiltásokkal. A motoros ablak pozíciót is fogad. |
| Audioserver | `AudioZoneV2` | Lejátszás, szünet, léptetés, hangerő és kedvencek. A natív audioszerver címe a Miniserver struktúrájából származik. |

A részletes világítási modell egyszerre legfeljebb 64 kört és 128 világítási képet kezel. A szín- és fehérfény-beállítás a kimenet által jelentett képességekhez igazodik. A Miniserver által közölt zárolások és a biztonsági állapotok a parancs végső elküldésekor is érvényesek.

## Energiaáramlás és éjszakai mód

Válaszd a lap típusának az **Energiaáramlás** lehetőséget. Az **Értékek összerendelése** mindig az adott fülhöz tartozik. Például a `PowerPV` mezőhöz választhatsz wattban mért teljesítményt, ehhez a szorzó `0.001`, az eltolás `0`. Egy második energiafül ugyanezekhez a mezőkhöz más UUID-ket és átváltást használhat.

| Mező | Várt érték |
| --- | --- |
| `PowerGrid` | kW, pozitív értéknél vételezés, negatívnál visszatáplálás |
| `PowerBat` | kW, pozitív értéknél kisütés, negatívnál töltés |
| `PowerPV`, `PowerFull` | Napelemes termelés és házfogyasztás kW-ban |
| `SocBat`, `BatteryTemperature` | Töltöttség százalékban és akkuhőmérséklet °C-ban |
| `Status` | 1 normál, 2 felkészülés, 3 szigetüzem, 4 hiba, 5 hálózat újra elérhető |
| Időjárás | Külső hőmérséklet °C-ban, szél km/h-ban, páratartalom százalékban, napsütés és csapadék 0/1 értékkel |

A **Kijelző** oldalon külön választható időzített éjszakai mód vagy egy digitális Miniserver-állapot. A jel értéke nappal 0, éjjel 1. Az automatikus képernyőkímélő a saját időzítőjét használja.

## Kapcsolat és tárolás

A panel RSA/AES kulcscserével és a Miniserver által kiadott JWT-vel építi fel a munkamenetet. Az állapotfolyam teljes átvitelét WSS titkosítja. A kliens szükség szerint megújítja a tokent. Szerver- vagy felhasználóváltáskor új hitelesítés indul. A Miniserver- és kamerajelszó, valamint a webes hozzáférés jelszava a microSD-profilban AES-256-GCM titkosítást kap. Az eszközkulcs az adott panel NVS-tárában van. Az SD-kártya és az eszközhöz kötött kulcs együttesen szükséges a mentett profil visszatöltéséhez.

A szerverstruktúra méretkorlátja 512 KiB, a lapos katalógusé 512 állapotbejegyzés. A webes Miniserver-oldal mutatja az utolsó kapcsolati lépést vagy hibát. Hozzáférési hiba esetén ellenőrizd a felhasználó vizualizációs jogosultságát. TLS-hibánál ellenőrizd az időszinkront, a PEM-tanúsítványt, a portot és a gépnevet.

Kapcsolatvesztéskor a kijelző elveti az adott munkamenet élő állapotait és függő parancsait. A kliens 15 másodpercenként életjelet küld. Socket-hibánál vagy 45 másodpercnyi válaszhiánynál újracsatlakozik. Az ismétlések közötti várakozás 2, 4, 8, 16, majd legfeljebb 30 másodperc. Az új session ismét lekéri a struktúrát és a tényleges állapotokat.

A csengetéshez, kamerához, kétirányú hanghoz és kapunyitó impulzushoz szükséges beállítások a kaputelefon leírásában találhatók. [Gen2 kaputelefon](kaputelefon.md)
