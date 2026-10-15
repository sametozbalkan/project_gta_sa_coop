# Grup 1 — iki oyunculu kabul testi (#1–#10)

Hedef sürüm: **CoopAndreas 0.3.102-alpha**. Bu belge kaynak kodundan çıkarılan beklenen davranışları ve
oyuncuların uygulayacağı testleri içerir. **İki oyunculu oyun testi bekliyor.**
Kutular yalnızca iki bilgisayarda gözlendikten sonra işaretlenir.

## Hazırlık ve oturum

1. İkinizde de **aynı teslim paketini** kullanın. Yeni `CoopAndreas-Kurulum.zip` dosyasının tamamını çıkarıp
   güncel **`Kur.bat`** dosyasını çalıştırın; mevcut 1.0 US kurulumda mod dosyaları yenilenir. Varsayılan oyun
   klasörü `C:\Games\GTASA_Coop`; Steam kurulumunu kullanmayın. Önceki paketin klasörüne yeni dosyaları karıştırmayın.
2. Bu teslim normal güncelleme ZIP'idir; yaklaşık 880 MB'lık downgrade yamalarını içermez. Arkadaşın mevcut
   1.0 US kurulumu güncellenirken bunlar yeniden indirilmez. Pakette bu listenin kopyası **`TEST-LISTESI.md`**
   ve dosya SHA-256 değerlerini, kaynak commit'ini ve sürümü içeren **`manifest.json`** bulunur.
   ZIP yanında verilen **`CoopAndreas-Kurulum.zip.sha256`** ile ZIP özetini karşılaştırın;
   iki bilgisayara aynı ZIP'i dağıtın;
   yalnızca sürüm yazısının aynı olması paket eşitliğinin tam kanıtı değildir.
3. Steam iki bilgisayarda ayrı hesaplarla açık, hesaplar arkadaş olsun. Host sunucuyu ve
   `CoopAndreas Tünel - Host` / `1-Host-ac.bat` tünelini açar. **Host oyuna ilk bağlanır**; ilk bağlanan
   oyuncu görev host'u seçilir. Sonra arkadaş `CoopAndreas Tünel - Bağlan` / `3-Oyuna-baglan.bat` ile bağlanır.
4. İkiniz `CoopAndreas` kısayolundan (`gta_sa.exe --coop`) açın; Connect: `127.0.0.1:6767`, farklı takma adlar.
   Sol altta ikinizde de **0.3.102-alpha** görünmeli. `Version mismatch` varsa teste başlamayın, aynı paketi kurun.
5. Bu tam sıra testi **New Game** ile başlar. **Eski Steam/tek oyunculu kaydı yüklemeyin**: farklı SCM nedeniyle
   çökebilir. Bu uyarı, aynı co-op hattında oluşturulan kayıtların paketten pakete korunması hedefinden ayrıdır.
   Co-op kaydını yalnız host, görev dışında güvenli evde alır; arkadaşın kaydı host ilerlemesinin yerine kullanılmaz.
6. Görevleri aşağıdaki sırayla, normal temas noktalarından başlatın; ikiniz de yaya olarak işarete yaklaşın.
   Başka görev açılmış olsa da sırayı değiştirmeyin; hileyle görev veya ilerleme bayrağı atlamayın.

Oturum kaydı: tarih `____` · host `____` · arkadaş `____` · paket/SHA-256 `____` · manifest `____`.

### Açılış çözünürlüğü

- [ ] Oyun açılırken Device Selection penceresindeki çözünürlük listesinde **2560×1440×32** bulunuyor;
  bu modu seçip oyun menüsüne ulaşabiliyorsunuz. Birden çok ekran varsa seçilen aygıtın bu çözünürlüğü
  desteklediğinden emin olun. Oyun yalnız seçilen aygıtın sunduğu modları listeler.
- [ ] Oyun içi görüntü ayarlarında seçilen çözünürlük doğru, görüntü kesilmiyor; kapatıp yeniden açınca
  çözünürlük tekrar seçilebiliyor. Önceden çalışan düşük çözünürlükler de kullanılabiliyor.

Bu istemci düzeltmesi de 0.3.102-alpha yazar; güncel paketi **manifest commit'i veya ZIP SHA-256** ile ayırt edin.
Çözünürlük listesi için sürücü ve kod kontrolleri yapıldı; gerçek pencere/oyun görüntüsü kabulü bekliyor.

## Her görevde uygulanacak ortak kontrol

- [ ] Başlangıç, ara sahneler ve kararmadan dönüş iki tarafta tamamlanıyor; arkadaş görünmez iç mekânda,
  araç içinde sıkışmış veya süresiz siyah ekranda kalmıyor. Oyuncu kontrolü gereken anda geri geliyor.
- [ ] O göreve aşağıda yazılan arkadaş hedef metinleri/işaretleri doğru fazda geliyor. Her host işaretinin,
  altyazısının veya yardım metninin otomatik yansıtıldığı varsayılmıyor.
- [ ] Başarılı bitişte iki ekranda görev geçti mesajı var; ilgili para/saygı ödülü iki tarafta uygulanıyor.
  **Her görev para vermez.** Hikâye açılışları ve sonraki görevin teması host'ta kontrol edilir.
- [ ] Başarısız denemede iki ekranda görev başarısız mesajı var; aşağıda belirtilen görev nedenleri arkadaşa da geliyor.
  Oyuncu ölümü/tutuklanması için ayrıca bir neden metni beklenmez.
- [ ] Başarıdan ve başarısızlıktan sonra görev işaretleri, kontrol noktaları, kilitli kamera/tuşlar ve siyah ekran
  kalmıyor; normal dolaşım mümkün. Başarısızlıktan sonra aynı görev normal başlangıcından yeniden denenebiliyor.

Bir görevin başarısızlık denemesini **başarılı bitirmeden önce** yapın; sonra aynı görevi yeniden başlatıp geçin.
Başarısızlık testi yapılmadıysa o sütunu “bekliyor” bırakın. İlk uygun görevde arkadaşın ölmesiyle, ayrı bir
denemede tutuklanmasıyla ortak başarısızlığı doğrulayın. Görev ortasında bağlantı kesmeyi rutin test saymayın:
geçersiz oyuncu tutamacı sağlamlığı ayrı bilinen iştir.

## #1–#5 kısa regresyon — sırayla

| Sıra | Kontrol | Sonuç |
|---|---|---|
| #1 In the Beginning (`INTRO`) | New Game Grove Street başlangıcına ulaşır. Bu pakette havaalanı ara sahnesi ve ilk BMX yolculuğu upstream tarafından atlanır; oynanmış kabul etmeyin. | ☐ geçti ☐ kaldı |
| #2 Big Smoke (`INTRO1`) | İkiniz birlikte başlayın; arkadaşın ayrı BMX'i ve işareti gelir. Bisiklet yolunu, ara sahneleri ve eve dönüşü kilitlenmeden bitirin. | ☐ geçti ☐ kaldı |
| #3 Ryder (`INTRO2`) | Berber/pizza akışı, aşağıdaki öncelikli restoran/tüfek regresyonu ve Grove'a dönüş biter; arkadaş kontrolünü geri alır. | ☐ geçti ☐ kaldı |
| #4 Tagging Up Turf (`SWEET1`) | Bir Grove tag'ini arkadaş tek başına tamamlasın; host tekrar sıkmadan görev ilerlesin. Idlewood/çatı tag'lerini paylaşın; aynı tag'e birlikte sıkınca tamamlanma gerilemesin. İki kişiyken Sweet'in arabasında birlikte gidin. | ☐ geçti ☐ kaldı |
| #5 Cleaning The Hood (`SWEET1B`) | Dövüş ve ev içi çatışmasına birlikte katılın; giriş/çıkış konumlandırması ve bitişte serbest kontrol düzgün olsun. | ☐ geçti ☐ kaldı |

Önceki dalganın ayrıntılı listesi repoda `docs/test-dalga-1.md`. Bu belgedeki #4, o listedeki
“görev 3” ifadesinin yerine envanterdeki hikâye numarasıdır.

### Öncelik: #3 Ryder — restoran/tüfek çökmesi

**0.3.102-alpha düzeltmesinin iki oyunculu oyun kabulü bekliyor; çökme giderildi diye doğrulanmış değildir.**
New Game'den #1–#2'yi normal sırada tamamlayarak #3'e gelin. İkinci geçiş türünü denemek için uygun host co-op
kaydı/yeniden normal oynama kullanın; görev tetiklerini, NPC oluşturmayı veya hikâye bayraklarını zorla atlamayın.
Arkadaş bu denemeyi `--coopdiag` ile açsın; Windows Çalıştır penceresinde:
`"C:\Games\GTASA_Coop\gta_sa.exe" --coop --coopdiag` (`--coop` modun yüklenmesi için gereklidir).
Erteleme veya geçersiz NPC gibi bir tanı olayı oluşursa oyun klasörüne **`CoopAndreas_ped_sync.log`** yazılır.
Çökme veya NPC'nin normale dönmemesi durumunda
bu dosyayı varsa çökme loguyla birlikte alın. Kayıt `deferred/resumed/invalid/cancelled` olaylarını ve NPC'nin
kimlik/model, havuz referansı, alan ve hazır olma bilgilerini içerir; süreçte 512 satır, dosyada 1 MiB ve NPC
ve olay türü başına saniyede bir satır sınırları olduğundan her olayın yazıldığı varsayılmaz.

- [ ] Host ve arkadaş restoran akışını birlikte oynasın. Restoran görevlisinin tüfeğe geçtiği, nişan aldığı ve
  ateş ettiği geçişi **ara sahneleri normal izleyerek**, ayrı denemede **oyunun sunduğu sahne atlama tuşuyla**
  tamamlayın. İki ekranda çökme, donma veya süresiz kararma olmamalı; her iki denemenin sonucunu kaydedin.
- [ ] Restorandan normal çıkışı, tüfekli NPC'nin dışarıdaki davranışını ve görevin Grove'a dönüşünü kontrol edin.
  Akış izin verirse restoran çevresine geri yaklaşın; NPC'nin kaybolup/yeniden görünmesi, yeniden nişan alması
  veya silah değiştirmesi çökme üretmemeli. Dönüş için görevi bozmayın; mümkün değilse bu gözlemi “uygulanamadı” yazın.
- [ ] Arkadaş ekranındaki uzak NPC'nin görünümü, silahı, nişanı ve ateşi tekrar sürmeli. Geçişte durum uygulaması
  ertelenmişse, güvenli duruma dönünce **en son alınan durum** uygulanmalı; eski tüfek/nişan/çömelme durumu
  sonradan gelip kalıcılaşmamalı. Erteleme gerçekleştiği gözlenemiyorsa bunu doğrulanmış işaretlemeyin.
- [ ] NPC'nin ölümü, sahne sonunda temizlenmesi veya kontrolünün başka oyuncuya geçmesi normal oynanışta
  gözlenirse eski NPC'ye ait silah/nişan durumu başka NPC'ye taşınmamalı ve çökme olmamalı.
  Bu durumları üretmek için yapay NPC silme/oluşturma ya da sahiplik komutu kullanmayın; gözlenmeyeni “bekliyor” bırakın.
- [ ] Görev sonunda iki tarafta kamera ve kontroller geri gelsin; #4 normal sırada başlayabilsin.
  Sorunda normal/atlanan sahne, host/arkadaş, son görülen NPC/silah ve saat ile belgenin sonundaki yerel logları kaydedin.

Normal sahne `____` · atlanan sahne `____` · ertelenen son durum `____` · doğal NPC yaşam döngüsü `____`.

## #6 Drive-Thru — `SWEET3`

Kaynak: `CoopAndreas/scm/scripts/SWEET3.txt`, `SWEET3_COOP_START`,
`SWEET3_COOP_UPDATE`, `SWEET3_COOP_CHASE_CARS`, `SWEET3_COOP_CHASE_START`, `SWEET3_COOP_CLEANUP`.

- [ ] İlk sahneden sonra arkadaş Grove'da kendi **Greenwood** arabasını ve yaya iken mavi işaretini görüyor.
  Host'un arabasındaki koltuklar CJ, Sweet, Ryder ve Smoke'a ait; arkadaş kendi arabasıyla takip ediyor.
  İlk fazlarda host'un arabasına binmeyi deneyince tekrar indiriliyor; NPC koltuğu çalınmıyor.
- [ ] Arabaya binen arkadaş Cluckin' Bell hedef yazısını, çıkış tarafındaki kontrol noktasını ve Sweet'in
  arabasını takip işaretini görüyor. Host normal drive-thru noktasına gidince akış ilerliyor;
  bu görevde tüm arkadaşların hedefte olmasını bekleyen yeni bir kapı varsaymayın.
- [ ] Drive-thru sahnesinde arkadaşın sürdüğü araç çıkış şeridine taşınıyor; sahne sonunda araçlar üst üste
  binmiyor ve kovalamaca başlayabiliyor. Arkadaş yaya kalmışsa kendi aracı yakınında konumlandırılıyor.
- [ ] Arkadaş kovalamacada **Tec-9 ve 300 mermi**, drive-by yardımı ve kırmızı Ballas araç işareti alıyor;
  kendi sürücü koltuğundan sağa/sola bakarak ateş edip hasara katkı verebiliyor.
  Ballas araçtan inerse hedef işaretleri yaşayan saldırganlara geçiyor.
- [ ] Grove'a dönüş ve ardından Smoke'u evine bırakma yazıları arkadaşa geliyor. Son fazda Sweet/Ryder'ın
  boşalttığı yolcu koltuğuna binilebilir; arkadaşın host aracının sürücüsü olması desteklenmiyor.
- [ ] **Başarısızlık denemesi:** host'un görev arabasını kaybedin veya bir yoldaşın ölmesine izin verin.
  İki tarafta başarısızlık ve ilgili araç/Sweet/Ryder/Smoke nedeni görünsün; ardından görevi normal yoldan tekrar geçin.
- [ ] Başarı: iki tarafta **$200 ve saygı**. Başarı/başarısızlık temizliğinde arkadaşın görev işaretleri
  kalkıyor; ek Greenwood görev sahipliğinden bırakılıyor (anında yok olması şart değil).
  Tec-9 ve kalan mermisi arkadaşta kalıyor; bu bir temizlik hatası sayılmaz.

## #7 Nines And AK's — `SWEET2`

Kaynak: `CoopAndreas/scm/scripts/SWEET2.txt`, `SWEET2_COOP_ENTER_SMOKES_CAR`,
`SWEET2_COOP_FOLLOW_SMOKES_CAR`, benzin deposu bölümü ve `SWEET2_COOP_FAILED`.

- [ ] Başlangıçta arkadaş host yakınında, Smoke'un arabası mavi işaretli. Host sürer, Smoke ön yolcudur;
  arkadaş **arka sağ koltuğa** binme görevi alır (10 saniye sınırı olan görev).
  Koltuğa oturduktan sonra varış kontrol noktası gelir; dışarıda kalınca kısa gecikmeyle araca dön yazısı/mavi işaret gelir.
- [ ] Emmet'e varışta arkadaş da hedefin yaklaşık **10 m kutusu** içinde olmalı; arkadaş uzaktayken geçişi bekletin,
  yanına gelince devam ettiğini doğrulayın. Sahne geçişlerinde beraber araçtan iniliyor ve host yakınına taşınılıyor.
- [ ] **İlk şişe/nişan eğitimini host yapar.** Bunlar yerel nesne/hedef mantığıdır; arkadaşın şişeyi vurup
  host eğitimini tamamlamasını veya kendine ait ayrı bir eğitim sayacı görmesini kabul ölçütü yapmayın.
- [ ] **Benzin deposu/arabayı patlatma** aşamasında arkadaş ayrı atış noktasına yerleştirilir, tabanca ve
  eğitim hedefi/yardım metni gelir. Host ateş etmeden arkadaşın ortak araca ateşi hasar verip aşamayı ilerletebilmeli.
  Yerleştirme noktasından patlayan araca yaklaşmayın: host patlamaya dayanıklı yapılırken arkadaş yapılmıyor.
- [ ] Emmet'ten sonra arkadaşın eğitim tabancası **60 mermiye** ayarlanır, Smoke'un arabasına dönüş ve yolculuk sürer.
  Smoke'un evinde birlikte inme/konumlandırma tamamlanır.
- [ ] Binco kapısında arkadaş 10 m çevrede bekler; uzaktaysa host'un geçişi bekler. **Dükkân eğitimi host'a aittir**;
  host içeri girip çıkar, arkadaşın da alışveriş yapması zorunlu değildir. Gönderilen kıyafet/renk yazıları görünür,
  bitişte iki tarafta kamera ve kontrol geri döner.
- [ ] **Başarısızlık denemesi:** ilgili fazda Smoke'u, Emmet'i veya Smoke'un arabasını kaybedin.
  İki tarafta başarısızlık ve uygun neden; arkadaş eğitim tabancasını aldıysa sağ kalan arkadaşta **5 mermiye**
  dönüş ve işaret temizliği kontrol edilir. Ardından görevi tekrar geçin.
- [ ] Başarıda **saygı**, iki ekranda geçti mesajı; para ödülü beklenmez. Smoke temasının host'ta açılması normaldir,
  fakat #8 yerine o göreve başlamayın.

## #8 Drive-By — `SWEET4`

Kaynak: `CoopAndreas/scm/scripts/SWEET4.txt`, başlangıç araç dağıtımı,
`SWEET4_COOP_FRAME`, `$sw4_stage` 0/3/7 ve görev sonu temizliği.

- [ ] Başlangıç sahnesinden sonra arkadaş host yakınına taşınır, **ayrı Greenwood ve Tec-9 (1000 mermi)** alır.
  Host'un arabası CJ ve üç yoldaşa ayrıdır; arkadaş o arabaya girerse indiriliyor.
  Yaya arkadaş kendi aracının mavi işaretini görür; araca binince işaret kalkar.
- [ ] Arkadaş yaya kaldığında ilk sürüş fazı ilerlemez; herhangi bir arabaya binince ilerler. Ballas bölgesine
  yaklaşırken arkadaş geride kalırsa, sonra Grove'a dönüşte geride kalırsa, host bekler;
  arkadaş hedefin yaklaşık **50 m yatay / 20 m düşey kutusuna** gelince akış sürer.
- [ ] Bölgeye git, mahalleyi tara, Pay 'n' Spray'e git ve Grove'a dön hedef yazıları arkadaşa gelir.
  Yaşayan Ballas'larda kırmızı işaretler belirir; arkadaş kendi arabasından ateş ederek öldürmelere katkı verir.
  Bu uyarlamada arkadaşa özel varış kontrol noktası eklenmemiştir; host'un güzergâhını birlikte takip edin.
- [ ] Polis aşamasında görev kaynaklı aranma yükseltmesi iki tarafta uygulanır. Pay 'n' Spray ve sonraki ilerleme
  **host'un aranma durumuna** göre değerlendirilir; arkadaşın kendi suçları/yıldız kaybetmesi ayrıdır.
  Sadece host'un boyacıya girmesinin arkadaşın bağımsız yıldızlarını da hemen sileceğini varsaymayın.
- [ ] **Başarısızlık denemesi:** bir yoldaşın ölmesine veya host'un görev aracının kaybına izin verin;
  iki ekranda başarısızlık, yoldaş kaybında ilgili mesaj ve serbest kontrole dönüş kontrol edilir. Sonra tekrar geçin.
- [ ] Başarıda iki tarafta **$500 ve saygı**. İşaretler temizlenir, araçlar görev sahipliğinden bırakılır;
  yaya yoğunluğu normale döner. Arkadaşta araç can göstergesi/sayaç görünmemesi bilinen sınırdır.

## #9 Sweet's Girl — `HOODS5`

Kaynak: `CoopAndreas/scm/scripts/HOODS5.txt`, `HOODS5_COOP_SEVILLE_BLIPS`,
`HOODS5_COOP_SEAT`, `HOODS5_COOP_END_LEAVE`, `HOODS5_COOP_END_PLACE`.

- [ ] Başlangıçta arkadaş host'un yanına Grove'a taşınır; kurtarma/Emmet silah yönlendirme yazıları gelir.
  İlk Seville hedefi için arkadaşa ayrı kontrol noktası eklenmemiştir; host'u takip edin.
  Host kurtarma bölgesine yaklaşınca yaşayan Seville saldırganlarının kırmızı işaretleri arkadaşa gelir.
- [ ] Arkadaş çatışmaya katılıp düşman öldürebilir. Kaynakta silah yönlendirme fazında arkadaşa tabanca ve
  16 mermi verme vardır; **Sweet'in can çubuğu arkadaşa yansıtılmaz**, host takip edip haber verir.
- [ ] Çatışma sonrası kurtarma kontrol noktası ve **dört kapılı araç** isteği arkadaşa gelir.
  Host dört kapılı araç getirir. Sweet ve kız arkadaşı ön sağ/arka sol koltuklara, arkadaş **arka sağ koltuğa**
  alınır. Arkadaşı önce ayrılmış koltuğa oturtarak da deneyin: silinmeden/düşmeden doğru koltuğa taşınmalı.
- [ ] Grove'a dönüş kontrol noktası ve yoldaş araca dön uyarıları fazlarına göre görünür.
  Varışta arkadaş araçtan çıkar; el sıkışma sahnesinde Grove'daki yaya noktasına alınır, aracı beraberinde taşımaz.
- [ ] **Başarısızlık denemesi:** Sweet'in veya kız arkadaşının ölmesine izin verin;
  iki ekranda görev başarısız ve uygun ölüm nedeni görünür. Tekrar başladığında eski hedef işaretleri kalmaz.
- [ ] Başarıda **saygı**, iki ekranda geçti mesajı; para ödülü beklenmez. Kurtarma/dönüş kontrol noktaları ve
  hedef işaretleri temizlenir; arkadaş normal yürüyebilir ve araca binebilir.

## #10 Cesar Vialpando — `SWEET6`

Kaynak: `CoopAndreas/scm/scripts/SWEET6.txt`, `SWEET6_COOP_ALL_NEAR`,
`SWEET6_COOP_MOD_SHOP`, yarışma öncesi koltuk/izleyici yerleştirmesi ve bitiş temizliği.

- [ ] Başlangıçta arkadaş host yanına taşınır; Willowfield garajı yazısı ve kontrol noktası gelir.
  Arkadaş garajın **10 m kutusu** dışındayken geçiş bekler, yakına gelince devam eder.
  Garaj sahnesinde arkadaş dışarıda belirlenmiş yaya noktasına alınır; silahı gizlenir, sonra geri görünür.
- [ ] **Mod garajını host tek başına kullanır.** Arkadaş lowrider'a binse bile bu aşamada araçtan çıkarılır;
  host modifiye iç mekânındayken arkadaş dışarıda bekler. Arkadaşın aynı menüyü/iç mekânı kullanması beklenmez.
- [ ] Modifiye sonrası arkadaş lowrider'ın **arka koltuğunda** yolculuk edebilir veya kendi imkânıyla takip eder.
  Buluşmaya git yazısı/kontrol noktası gelir; yarışma öncesi arkadaş **15 m kutusu** içinde olmalıdır.
  Uzakta bırakıp beklemeyi, yaklaştırınca devamı kontrol edin.
- [ ] **Bahis ve lowrider ritim oyununu yalnız host oynar.** Arkadaş arka koltuktaysa araçta kalır;
  sürücü/ön yolcu koltuğundaysa veya araç dışındaysa izleyici noktasına alınır. Ön koltuk CJ ve kız için boş kalır.
  Arkadaşın silinmesi, başka oyuncunun aracına zorla sürücü olması veya sahnede takılması olmamalı.
- [ ] Sabit sahne kameraları iki tarafta çalışır; arkadaş için ritim tuşları, skor sayaçları ve bahis menüsünün
  birebir görünmesi/oynanması beklenmez. Yarışma bitince araç dışındaki arkadaş host yanına geri alınır;
  arka koltuktaki arkadaşın araçta kalması normaldir.
- [ ] **Başarısızlık denemesi:** host bir turu kaybetsin; kayıp sonucu yazısı arkadaşa da gelsin, ikinizde de
  başarısızlık ve kamera/tuş/radarın normale dönmesi kontrol edilsin. Alternatif araç kaybında lowrider'ın
  mahvolduğu mesajını kontrol edin. Sonra aynı görevi yeniden başlayıp kazanın.
- [ ] Başarıda iki ekranda geçti mesajı ve görevde hesaplanan para ödülü uygulanır; aynı başlangıç parasını
  zorunlu tutmayın, iki tarafta ödül öncesi/sonrası değişimi kaydedin. Kontrol noktaları temizlenir, gizlenen silah
  yeniden görünür, serbest dolaşım geri gelir.

## Sınırlar, regresyon ve sonuç kaydı

Bu pakette görev **host'ta çalışır**. Arkadaş elle eklenen görev metinlerini/işaretlerini, mevcut kamera/opcode
ve varlık senkronunu görür. Genel görev yansıtma katmanı henüz gelecek iştir. Görev sayaçları/zamanlayıcıları,
her temas işareti ve bütün dünya ilerlemesi otomatik ortak kabul edilmez. Görevlerin aranma ayarlama/temizleme
komutları senkronlanır; suç işleyerek kazanılan yıldızlar ve görev koşullarının okuduğu host yıldızları ayrıdır.
Los Santos dışındaki şehir kilidi senkronunun bulunması, köprü bariyerlerinin de ortak kaldırıldığı anlamına gelmez.

- [ ] Görev dışında aynı araçta host sürerken, sonra arkadaş sürerken radyo istasyonunu değiştirin;
  yolcu sürücüyü takip etsin. Ayrı araçlarda her oyuncu kendi radyosunu duysun.
- [ ] Görev dışında arkadaş 2 dakika Alt+Tab yapıp dönsün; bağlantı kopmasın. Uygun görev sırasında host
  kısa süre Alt+Tab yapınca görev bekleyip dönüşte devam etsin. Arka plandaki oyun duraklar;
  basılı tuşun diğer ekranda sürmesi bilinen sınırdır.
- [ ] #10 sonrası host görev dışında güvenli evde co-op kayıt alsın. Sonraki oturumda yükleme denenirse yalnız
  host kendi co-op kaydını yüklesin; arkadaşın konumu, yeniden bağlanma ihtiyacı ve görev tetikleyicilerinin
  çalışması ayrıca gözlensin. **Kayıttan yükleme davranışı iki oyuncuyla henüz doğrulanmış sayılmaz.**

| Görev | Başarı yolu | Başarısızlık/tekrar | Temizlik | Not / hata kaydı |
|---|---|---|---|---|
| #6 Drive-Thru | bekliyor | bekliyor | bekliyor | |
| #7 Nines And AK's | bekliyor | bekliyor | bekliyor | |
| #8 Drive-By | bekliyor | bekliyor | bekliyor | |
| #9 Sweet's Girl | bekliyor | bekliyor | bekliyor | |
| #10 Cesar Vialpando | bekliyor | bekliyor | bekliyor | |

Her hata için görev/faz, hangi oyuncu, beklenen/görülen davranış, ekran görüntüsü veya kısa video, saat ve
tekrar adımlarını yazın. Çökmede iki taraftan varsa `C:\Games\GTASA_Coop\CoopAndreas_crashes\*.log`;
takılmada `CoopAndreas (tani)` / `--coopdiag` ile oluşan `C:\Games\GTASA_Coop\CoopAndreas_diag.log`
dosyasını ekleyin. Son başarılı adımı ve paket kimliğini kaydedin; engelde görevi atlayarak devam etmeyin.

**#10 tamamlandığında durun. #11 OG Loc veya başka bir sonraki gruba başlamayın. Grup 1'in sonuçları
kullanıcı tarafından onaylanmadan Grup 2 açılmaz.**
