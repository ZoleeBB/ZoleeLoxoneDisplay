# A tájképek forrása

A `landscapes.tsv` azonosítóihoz tartozó képeket az OpenAI képgenerátorával, egyenként készítettük ehhez a projekthez. A helynevek az ábrázolás földrajzi inspirációját jelölik.

A négyzetes kompozícióban kevés nagy folt, természetes színtelítettség és mérsékelt kontraszt kapott hangsúlyt. A firmware a 720 × 720 pixeles képet függőlegesen középre vágja a felső fülsáv alatti, 720 × 640 pixeles tartalmi területhez. Új háttérnél a fontos részleteket ehhez a középső sávhoz érdemes igazítani.

A generált képekből Pillow `ImageOps.fit` és `Image.Resampling.LANCZOS` készítette a 720 × 720 pixeles RGB-képet. Az SD-kártyán lévő változat baseline JPEG, 86-os minőséggel, 4:2:0 színmintavételezéssel. A katalógus újraépítése ebből a JPEG-ből állítja elő a 120 × 120 pixeles, 82-es minőségű miniatűrt. A már kiadott azonosítók megőrzik a mentett lapbeállítások kapcsolatát a képekkel.
