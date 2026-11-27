# Gen2 kaputelefon

A webes beállításoknál engedélyezd a kaputelefont, válaszd ki a csengetés állapotjelét, majd add meg a kamera közvetlen MJPEG-címét és a videó hozzáférési adatait. A kapunyitáshoz olyan impulzusvezérlőt válassz, amelyet a panel Miniserver-felhasználója működtethet. Külön megadható a mikrofon erősítése és a hangszóró hangereje.

Csengetéskor a panel felébred és megnyitja az élőképet. A **Beszélgetés** gomb indítja a Gen2 SIP-hívást, amely G.711 hangot továbbít RTP-n. A mikrofon a hívás felépülése után nyílik meg. A **Kapunyitás** gomb a beállított műveletet küldi. A kapu nyitása után a már aktív hangkapcsolat öt másodperc múlva lezárul. A videó legfeljebb 1280×720-as színes vagy szürkeárnyalatos MJPEG-forrást fogad.

A kamera, az I2S és a médiafeladatok az engedélyezett kaputelefon első használatáig nem foglalják le a képpuffereket vagy a mikrofont. A hívás végén a kép- és DSP-pufferek, valamint a médiafeladatok felszabadulnak. A következő híváshoz a dekóder és az inicializált hardverillesztő újrahasznosítható.

A mellékelt **Tubular Chime** jelzés a forrásprojektből származik. Alkotója marlocarlo, a hang 16 kHz-es mono PCM16 formátumban található a csomagban. [Eredeti felvétel](https://freesound.org/people/marlocarlo/sounds/841813/) · [CC0](https://creativecommons.org/publicdomain/zero/1.0/)

## Csengőhang kiválasztása

A webes **Kaputelefon** lapon a csengőhanglistában a kártyára telepített felvételek jelennek meg. A **Néma** választás elnémítja a jelzést, a videó és a hívásvezérlők ettől még megnyílnak. Hallgasd meg a hangmintát a böngészőben, válassz hangot, állítsd be az ismétlést és a hangszóró hangerejét, majd mentsd a profilt. A hang neve és az ismétlés beállítása újraindítás után is megmarad.

A csengőhangcsomag letöltése a panelről indítható. A panel megkeresi a **ZoleeEnergyFlowDisplay** legfrissebb stabil GitHub-kiadását, és letölti az ahhoz tartozó **ZoleeEnergyFlowDisplay-vX.Y.Z-ringtones.zip** fájlt. A letöltéshez a kijelzőnek internetkapcsolatra és írható kártyára van szüksége. A webes oldalon figyelheted az előrehaladást és a telepített verziót. [A forrásprojekt kiadásai](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/releases)

A panel ellenőrzi a kiadáshoz mellékelt **SHA256SUMS.txt** jegyzéket, a ZIP épségét, a névjegyzéket és a hangformátumot. A csomag legfeljebb 32 darab, egyenként legfeljebb 60 másodperces, 16 kHz-es mono PCM16 WAV-hangot tartalmazhat. Az eredeti nevük, címük és a forrásprojekt licencadatai a kártyára kerülnek. A lejátszó csak csengetéskor olvassa be a mentett hangot.

Újabb szinkronizáláskor az ép, azonos csomagot a készülék megtartja. Letöltési hiba, betelt SD vagy megszakítás esetén az előző telepítés használható marad. A letöltés gombbal újrapróbálható. A képcsomagban kapott alapértelmezett csőharang külön is elérhető. A csengőhangcsomag telepítése és a firmware-frissítés egymás után futtatható.
