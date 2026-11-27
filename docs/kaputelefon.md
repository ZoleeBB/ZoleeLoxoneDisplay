# Gen2 kaputelefon

A webes beállításoknál engedélyezd a kaputelefont, válaszd ki a csengetés állapotjelét, majd add meg a kamera közvetlen MJPEG-címét és a videó hozzáférési adatait. A kapunyitáshoz olyan impulzusvezérlőt válassz, amelyet a panel Miniserver-felhasználója működtethet. Külön megadható a mikrofon erősítése és a hangszóró hangereje.

Csengetéskor a panel felébred és megnyitja az élőképet. A **Beszélgetés** gomb indítja a Gen2 SIP-hívást, amely G.711 hangot továbbít RTP-n. A mikrofon a hívás felépülése után nyílik meg. A **Kapunyitás** gomb a beállított műveletet küldi. A kapu nyitása után a már aktív hangkapcsolat öt másodperc múlva lezárul. A videó legfeljebb 1280×720-as színes vagy szürkeárnyalatos MJPEG-forrást fogad.

A kamera, az I2S és a médiafeladatok az engedélyezett kaputelefon első használatáig nem foglalják le a képpuffereket vagy a mikrofont. A hívás végén a kép- és DSP-pufferek, valamint a médiafeladatok felszabadulnak. A következő híváshoz a dekóder és az inicializált hardverillesztő újrahasznosítható.

A mellékelt **Tubular Chime** jelzés a forrásprojektből származik. Alkotója marlocarlo, a hang 16 kHz-es mono PCM16 formátumban található a csomagban. [Eredeti felvétel](https://freesound.org/people/marlocarlo/sounds/841813/) · [CC0](https://creativecommons.org/publicdomain/zero/1.0/)
