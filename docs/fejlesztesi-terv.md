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

Az elfogadási próbákat mindkét cikkszámon rögzíteni kell az adott panel szilíciumrevíziójával és firmware-azonosítójával együtt. A memória összevetési pontjai az üres lap, a világítási részletek, az energiakép, a hívás megnyitása és tíz bontás utáni nyugalmi állapot. Külön mérendő a szabad belső DMA-memória és a legnagyobb összefüggő blokk. Az automatizált fordítás és a készülékes próba eredményét külön vezetjük.
