# 0.1.0-alpha — iki oyunculu test (grup 1, New Game'den #1–#10)

Hedef sürüm: **0.1.0-alpha**. Kutuları yalnızca iki bilgisayarda gözledikten sonra işaretleyin.

**Durum (2026-10-16):** 0.3.102 ile yapılan ilk turda #1–#6 geçti, #7'de host arkadaşın ekranında kayboldu ve Binco
kapısında arkadaşın oyunu çöktü. 0.3.103 bunu düzeltmişti; o sürümün testi atlandı (kullanıcının kararı), onun
kontrolleri aşağıda **B** bölümünde. 0.1.0 ilk on görevi yeni sisteme geçirdi; bu yüzden test **New Game**'den,
#1'den başlar.

## 0.1.0'da ne değişti

- **Görev yansıtma:** host'un görevi ekranda ne gösteriyorsa arkadaşta da görünür: görev adı, hedef ve diyalog
  yazıları, yardım kutuları, "Mission passed" yazısı ve müziği, araç/karakter/hedef işaretleri, sarı görev küreleri,
  sayaçlar ve zamanlayıcılar. İlk on görevdeki elle eklenmiş kopyalar kaldırıldı: her şey **bir kez** görünmeli.
- **Ödüller:** görev geçilince verilen para ve saygı ikinize de gelir. Görev içindeki bahis ve harcamalar yalnızca
  host'tan düşer (#10'daki bahis gibi).
- **Dünya durumu:** şehir kilidi, en yüksek aranma seviyesi, garajlar ve dükkân kapıları, görevlerin trafik/yaya/polis/
  hava ayarları host'unkini izler.
- **Host sabitleme:** sunucu ayarındaki (`story-host`) oyuncu hep host'tur; arkadaş önce bağlansa da host olmaz.
- **Host görevin ortasında düşerse** arkadaş ara sahnede ya da kontrolsüz kalmaz.
- **Arkadaşın saygısı** da eşyalarıyla birlikte host'un kaydının yanında saklanır.
- 0.3.103'ten: host'un kaybolması ve Binco çökmesi düzeltmesi, arkadaşın eşyalarının kayıtla saklanması.

## Hazırlık ve oturum

1. İkinizde de **aynı teslim paketi** olsun. Arkadaş yeni `CoopAndreas-Kurulum.zip` dosyasının tamamını çıkarıp
   **`Kur.bat`** dosyasını çalıştırır (mevcut 1.0 US kurulumda yalnız mod dosyaları yenilenir). Host'un oyununa paket
   kuruldu. ZIP yanındaki `CoopAndreas-Kurulum.zip.sha256` ile ZIP özetini karşılaştırın; pakette bu listenin kopyası
   `TEST-LISTESI.md` ve `manifest.json` bulunur.
2. Sol altta ikinizde de **0.1.0-alpha** yazmalı. `Version mismatch` görürseniz teste başlamayın.
3. Host'un `C:\Games\GTASA_Coop\server\server-config.ini` dosyasında `story-host = <host'un takma adı>` satırı olmalı
   (kurulum ekledi). Takma adınızı değiştirirseniz bu satırı da değiştirin; yoksa kimse host olmaz.
4. Steam iki bilgisayarda ayrı hesaplarla açık ve hesaplar arkadaş. Host sunucuyu ve `1-Host-ac.bat` tünelini açar;
   arkadaş `3-Oyuna-baglan.bat` ile bağlanır.
5. İkiniz de oyunu oyun klasöründeki **`CoopAndreas (tani)`** kısayoluyla açın. Açmadan önce oyun klasöründeki eski
   `CoopAndreas_*.log` dosyalarını silin. Sorunda iki taraftan şu dosyaları ve saati gönderin:
   `CoopAndreas_diag.log`, `CoopAndreas_session.log`, `CoopAndreas_mirror.log`, `CoopAndreas_world_state.log`,
   `CoopAndreas_ped_sync.log` ve varsa `CoopAndreas_crashes\*.log`.
6. İkiniz de **New Game** ile başlayın; eski Steam veya tek oyunculu kayıt yüklemeyin. Co-op kaydını yalnız host,
   görev dışında güvenli evde alır.
7. Görevleri sırayla, normal temas noktalarından başlatın; sırayı değiştirmeyin, hileyle görev atlamayın.

Oturum kaydı: tarih `____` · host `____` · arkadaş `____` · paket SHA-256 `____` · manifest commit `____`.

## A. 0.1.0 kontrolleri

### A1. Host sabitleme ve bağlantı

- [ ] **Arkadaş önce bağlanır.** Sunucu konsolunda "No host until the story host … connects" yazar; arkadaşın
  sohbetinde host satırı yoktur.
- [ ] **Host bağlanır.** Host'ta "You are the host now", arkadaşta "<host> is the host now" görünür; host'un temas
  işaretleri arkadaşın haritasında belirir.
- [ ] **Arkadaş çıkıp tekrar bağlanır.** Konsolda "stays the host" yazar, arkadaş host satırını görür.
- [ ] **Host görevin ortasında düşer.** Bir ara sahnede ya da kontrollerin kapalı olduğu bir anda host oyunu kapatsın.
  Arkadaşta yaklaşık 1 saniye içinde ara sahne biter, kamera ve kontroller geri gelir, trafik normale döner;
  yansıtılan işaretler, küreler ve sayaçlar kalkar; oyun çökmez.
- [ ] **Host geri döner.** Sunucu konsolunda "[Game] : N Disconnected." görünene kadar (30–60 sn) bekleyin, sonra host
  oyunu açıp bağlansın: yeniden host olur, arkadaş işaretleri alır. Daha erken bağlanmak "nickname taken" verir;
  o zaman oyunu kapatıp bekleyin.

### A2. Görev yansıtma (her görevde gözleyin)

- [ ] Görev adı sağ altta, hedef ve diyalog yazıları ve yardım kutuları arkadaşta **bir kez** görünür.
- [ ] İşaretler (karakter, araç, hedef nokta) aynı renk ve boyutta, aynı anda değişir ve kaybolur. Arkadaş kendi
  üstünde işaret görmez. Yeni beliren bir karakterin işareti önce yalnız radarda, en geç yarım saniye sonra normal
  görünebilir.
- [ ] Sarı görev küreleri aynı yerde görünür. Host bir binanın içindeyken dışarıdaki arkadaş sokakta küre görmez.
- [ ] Geçişte "Mission passed" yazısı ve müziği arkadaşta da var; para ve saygı ikinize gelir, host'unki iki kat olmaz.
- [ ] Başarısızlıkta "Mission failed" ve neden yazısı arkadaşta da görünür; yardım kutusu temizlenir.
- [ ] Görev sırasında arkadaş oyunu kapatıp New Game ile yeniden bağlansın: o anki hedef yazısı, işaretler, küre ve
  sayaçlar gelir.
- [ ] Görev sırasında arkadaş 30 saniye Alt+Tab yapsın: dönüşte yazılar ve işaretler günceldir.

### A3. Dünya durumu

- [ ] **Mod garajları:** #10'dan sonra arkadaş Loco Low Co'ya (Willowfield, lowrider ile) ve Los Santos Transfender'a
  (Temple, normal araçla) girebilir, mod menüsü çalışır. Diğer mod garajları kapalı kalır.
- [ ] **Arkadaşın yeniden başlaması:** arkadaş bağlıyken menüden New Game başlatsın; giriş bitince Binco ve berber
  kapıları ile iki mod garajı yine açık olmalı.
- [ ] **Başarısız görev sonrası trafik:** #2'de BMX yolculuğu başlayınca trafik azalır. Görevi o noktadan sonra bilerek
  başarısız edin: arkadaşta da 1–2 saniye içinde araçlar ve yayalar geri gelir.
- [ ] **Aranma sınırı:** görev dışında arkadaş suç işleyip yıldız toplasın; host'un sınırını (hikâyenin bu noktasında 4)
  geçemez.

### A4. Arkadaşın eşyaları ve saygısı

Arkadaşın silahları, canı, zırhı, parası, kıyafetleri, dövüş stili, özellikleri, istatistikleri ve **saygısı** (görev,
öldürme ve kıyafet saygısı), host kayıt alınca kaydın yanına `GTASAsf<n>.b.coop` olarak yazılır. Eşleşme takma adla
yapılır; takma adı değiştirmeyin. Son değişiklikten sonra kayıt almadan önce yaklaşık 3 saniye bekleyin.
#2–#10 boyunca arkadaşın görev saygısı toplam **34** olmalı (#2 3, #3 3, #4 3, #5 3, #6 5, #7 4, #8 6, #9 7, #10 0).

- [ ] (a) **Oturumlar arası:** arkadaş silah, zırh, para, kıyafet alsın. Host kayıt alır ("Kept the state of 1 other
  player(s)"). İkiniz çıkın; host o kaydı yükler, arkadaş New Game ile bağlanır. Arkadaşın sohbetinde "Your saved player
  state is back (mission respect N, respect R, clothes C)" görünür; eşyalar ve saygı yerindedir.
- [ ] (b) **Çökme sonrası:** arkadaş bir şey değiştirsin, 3 sn beklesin, oyunu kapatsın. Host'ta "<isim> disconnected"
  görünene kadar (30–60 sn) bekleyin; arkadaş New Game ile bağlanınca son durumu gelir.
- [ ] (c) **Geri alma:** host kayıt alır (arkadaşın durumu X). Arkadaş durumunu değiştirir (Y). Host o kaydı yükler,
  arkadaş bağlı kalır; arkadaş yaya olunca X'e döner.
- [ ] (d) **Arkadaş yokken kayıt:** arkadaş Z durumuna gelir, 3 sn bekler, çıkar. Host kayıt alır. Sonraki oturumda
  arkadaş bağlanınca Z gelir.
- [ ] (e) **Eski kayıt:** 0.3.103 ile alınmış bir co-op kaydı yüklenirse arkadaşın sohbetinde "no respect in this older
  save" görünür; eşyaları gelir, saygısı olduğu gibi kalır. Yan dosyası olmayan bir kayıtta "This save keeps no other
  player's state" görünür.

## B. Atlanan 0.3.103 turunun kontrolleri

### B1. #7 Nines And AK's — host'un kaybolması ve Binco çökmesi

0.3.102'de arkadaşın oyunu, içinde host'un oturduğu bir aracı silince host'un karakterini de sildi; host Binco'nun
kapısından girince arkadaşın oyunu çöktü. Düzeltme: araç silinmeden önce içindekiler dışarı alınır, oyun bir ağ
oyuncusunu silmez, karakter yine de kaybolursa en geç 1 saniyede yeniden oluşturulur.

- [ ] #7'yi baştan sona oynayın. Arkadaş host'u **her an** görür; host yayayken başının üstünde isim etiketi vardır.
  Host bir an kaybolursa yaklaşık 1 saniyede geri gelmeli; saati yazın.
- [ ] Binco kapısında host içeri girip çıkar, arkadaş dışarıdan izler: arkadaşın oyunu çökmez, host içerideyken
  görünmez, çıkınca yeniden görünür.
- [ ] Görev dışında host iki üç kapıdan daha girip çıksın (Sweet'in evi, bir Cluckin' Bell); aynı beklenti.

### B2. Araçlar

- [ ] Host birkaç araca binip insin; sonra bir araçtan inip onu patlatsın.
- [ ] Arkadaş host'un sürdüğü araçta yolcu olsun; önce host, sonra arkadaş insin. Kimse kaybolmaz, kimse kendiliğinden
  araçtan düşmez.
- [ ] Bir görev arkadaşın oturduğu aracı silerse arkadaş aracın yerinde yaya kalır ve görev onu tanımaya devam eder.
- [ ] Arkadaş host'un arabasına ve motosikletine G ile yolcu olarak binsin; host'un ekranında oturmuş görünür.

### B3. Açılış çözünürlüğü

- [ ] Device Selection penceresinde **2560×1440×32** var; seçip oyun menüsüne ulaşabiliyorsunuz. Oyun içinde görüntü
  doğru, kapatıp açınca yeniden seçilebiliyor.

## C. Her görevde ortak kontrol

- [ ] Başlangıç, ara sahneler ve kararmadan dönüş iki tarafta tamamlanıyor; arkadaş iç mekânda, araç içinde veya siyah
  ekranda takılmıyor, kontrol gereken anda geri geliyor.
- [ ] A2'deki yansıtma beklentileri o görevde tutuyor: her yazı ve işaret bir kez.
- [ ] Geçişte iki ekranda "Mission passed", para ve saygı ikinize. **Her görev para vermez.**
- [ ] Başarısızlıkta iki ekranda "Mission failed" ve neden yazısı. Oyuncu ölümü veya tutuklanması için ayrı neden
  yazısı beklenmez. Arkadaşın ölmesi ya da tutuklanması da görevi başarısız eder.
- [ ] Geçişten ve başarısızlıktan sonra işaret, küre, sayaç, kilitli kamera/tuş ve siyah ekran kalmıyor. Başarısız
  görev normal başlangıcından yeniden denenebiliyor.

Her görevde başarısızlık denemesini **başarılı bitirmeden önce** yapın; sonra görevi yeniden başlatıp geçin.

## D. Görevler, sırayla

### #1 In the Beginning (`INTRO`)

- [ ] New Game, Grove Street başlangıcına ulaşır. Havaalanı sahnesi ve ilk BMX yolculuğu atlanır; bu normaldir.

### #2 Big Smoke (`INTRO1`)

- [ ] İkiniz birlikte başlayın; arkadaşın ayrı BMX'i ve onun mavi işareti gelir (host'un kendi BMX'inin işaretini de
  görür; bu normal). Arkadaş BMX'e binip inince kendi işareti ve "bisiklete bin" yazısı gelip gider. Diyaloglar ve
  hedef yazıları bir kez, Grove Street'te tek küre görünür. Bisiklet yolu ve eve dönüş kilitlenmeden biter.
- [ ] Arkadaş Sweet'in 60 m'den fazla gerisinde kalınca "çok uzaktasın" uyarısı gelir, yetişince kalkar.
- [ ] Geçiş: saygı ikinize (+3).
- [ ] A3'teki trafik denemesini bu görevde yapın.

### #3 Ryder (`INTRO2`)

- [ ] Berber ve pizza akışı, restoran sahnesi ve Grove'a dönüş biter; arkadaş kontrolünü geri alır. Restoran
  görevlisinin tüfeğe geçtiği sahneyi bir kez normal izleyerek, bir kez atlayarak geçin; çökme ya da donma olmamalı.
- [ ] Arkadaşın Greenwood'unda mavi işaret var; "aracını kırmızı işarete sür" ve "Ryder'ı eve götür" yazıları
  arkadaşın kendi konumuna göre gelir. Berberde ve evde tek küre görünür.
- [ ] Geçiş: saygı ikinize (+3).

### #4 Tagging Up Turf (`SWEET1`)

- [ ] Bir Grove tag'ini arkadaş tek başına tamamlasın; host tekrar sıkmadan görev ilerlesin. Diğer tag'leri paylaşın;
  aynı tag'e birlikte sıkınca tamamlanma gerilemesin. İki kişiyken Sweet'in arabasında birlikte gidin.
- [ ] Sweet'in arabasında tek işaret var. Arkadaşın "Sweet'le git" ve "Sweet'in arabasına dön" yazıları kendi
  durumunu izler; çatıdaki noktaya varınca tel çit yardımı arkadaşa gelir.
- [ ] Geçiş: ikinize de **$200**, saygı (+3) ve 1.000 sprey.

### #5 Cleaning The Hood (`SWEET1B`)

- [ ] Dövüşe ve ev içi çatışmaya birlikte katılın; giriş ve çıkış yerleştirmesi düzgün, bitişte serbest kontrol var.
- [ ] Her hedef yazısı bir kez, her hedefte tek işaret ya da küre görünür.
- [ ] Arkadaş Ryder'dan 30 m'den fazla uzaklaşınca "çok uzaktasın" yazısı gelir; 10 m'ye dönünce o anki hedef yeniden
  görünür.
- [ ] Geçiş: saygı ikinize (+3).

### #6 Drive-Thru (`SWEET3`)

- [ ] İlk sahneden sonra arkadaş kendi **Greenwood** arabasını ve onun mavi işaretini görür. Host'un arabasındaki
  koltuklar CJ, Sweet, Ryder ve Smoke'a ait; arkadaş o arabaya binmeye çalışınca indirilir.
- [ ] Cluckin' Bell hedefi ve Sweet'in arabası işareti arkadaşta bir kez görünür; arkadaş kendi aracındayken çıkış
  şeridinde kendine ait bir işaretçi görür. Drive-thru sahnesinde arkadaşın aracı çıkış şeridine taşınır; araçlar
  üst üste binmez.
- [ ] Kovalamacada arkadaş **Tec-9 ve 300 mermi** ile iki drive-by yardım yazısını alır, Ballas aracında tek kırmızı
  işaret görür ve hasara katkı verir. "Ballas'ları öldür" yazısı bir kez görünür.
- [ ] Başarısızlık: host'un görev arabasını kaybedin ya da bir yoldaşın ölmesine izin verin.
- [ ] Geçiş: ikinize de **$200** ve saygı (+5). Tec-9 arkadaşta kalır; bu normaldir.

### #7 Nines And AK's (`SWEET2`)

- [ ] Önce **B1**'i uygulayın.
- [ ] Arkadaş Smoke'un arabasının **arka sağ koltuğuna** biner. Smoke'un arabasında tek işaret, Emmet'te tek küre
  görünür; arkadaş 1,5 saniyeden uzun araç dışında kalınca "Smoke'un arabasına dön" uyarısı gelir.
- [ ] Emmet'e varışta arkadaş da hedefin yaklaşık 10 m yakınında olmalı; uzaktayken geçiş bekler.
- [ ] Şişe eğitimini host yapar. Patlatılacak araç aşamasında arkadaş ayrı atış noktasına alınır, tabanca ve hedef
  gelir; arkadaşın ateşi aracı patlatıp aşamayı ilerletebilir. Patlayan araca yaklaşmayın.
- [ ] Binco'da tek küre görünür; dükkân eğitimi host'a aittir, arkadaş dışarıda bekler.
- [ ] Başarısızlık: ilgili fazda Smoke'u, Emmet'i ya da Smoke'un arabasını kaybedin; neden yazısı bir kez görünür,
  arkadaşın tabancası 5 mermiye iner.
- [ ] Geçiş: saygı ikinize (+4); para ödülü yok.

### #8 Drive-By (`SWEET4`)

- [ ] Arkadaş ayrı bir **Greenwood ve Tec-9 (1000 mermi)** alır; host'un arabası CJ ve üç yoldaşa ayrıdır.
- [ ] Arkadaş yaya kalınca ilk sürüş fazı ilerlemez; geride kalırsa host bekler, yaklaşınca akış sürer.
- [ ] Her Balla'da tek kırmızı işaret görünür; arkadaşa "mahalleyi tara" yazısı gelir ve kendi arabasından ateş
  ederek katkı verir. Sonra arkadaşın da 2 yıldızı olur; aranma ve boyacı yazıları bir kez görünür.
- [ ] **Yeni:** host'un araç can göstergesi arkadaşın ekranında da görünür ve değeri değişir.
- [ ] Polis aşamasında görevin aranma yükseltmesi iki tarafta uygulanır; Pay 'n' Spray host'un yıldızlarına göre
  değerlendirilir.
- [ ] Başarısızlık: bir yoldaşın ölmesine ya da host'un görev aracının kaybına izin verin.
- [ ] Geçiş: ikinize de **$500** ve saygı (+6); gösterge ve işaretler kalkar.

### #9 Sweet's Girl (`HOODS5`)

- [ ] Arkadaş host'un yanına alınır ve tabanca alır; kurtarma hedefi ve her saldırganda tek kırmızı işaret görünür.
- [ ] **Yeni:** Sweet'in can çubuğu arkadaşın ekranında da görünür ve değeri değişir.
- [ ] Dört kapılı araçta Sweet ve kız arkadaşı ön sağ ve arka sol koltuklara, arkadaş **arka sağ koltuğa** alınır;
  arkadaş önce o koltuğa oturmuşsa silinmeden doğru yere taşınır.
- [ ] Geride kalan kişide (Sweet ya da kız arkadaşı) tek mavi işaret görünür.
- [ ] Grove'a dönüşte arkadaş araçtan iner, el sıkışma sahnesinde yaya noktasına alınır.
- [ ] Başarısızlık: Sweet'in ya da kız arkadaşının ölmesine izin verin.
- [ ] Geçiş: saygı ikinize (+7); para ödülü yok.

### #10 Cesar Vialpando (`SWEET6`)

- [ ] Garaj yazısı bir kez (kuyrukta iki kez değil), garajda ve buluşmada tek küre görünür. Arkadaş garajın yaklaşık
  10 m yakınına gelmeden geçiş beklenir. Mod garajını görev sırasında host tek başına kullanır; arkadaş dışarıda
  bekler.
- [ ] Modifiyeden sonra arkadaş lowrider'ın arka koltuğunda gider ya da takip eder; buluşmada yaklaşık 15 m yakında
  olmalıdır.
- [ ] Bahis ve ritim oyununu yalnız host oynar. **Bahis sırasında arkadaşın parası değişmez.** Yarışma diyalogları
  ve başarısızlık yazıları bir kez görünür.
- [ ] Başarısızlık: host bir turu kaybetsin; sonuç yazısı arkadaşa da gelir, kamera ve tuşlar normale döner.
- [ ] Geçiş: "Mission passed" ekranındaki para ikinize de gelir (bahsin iki katı; arkadaş bahis yatırmadığı halde).
- [ ] Görevden sonra **A3**'teki mod garajı kontrolünü yapın.

## Bilinen sınırlar (0.1.0)

- Host'a özel mesajlar ("Sweet'in arabasına bin" gibi) arkadaşta da görünür; bu kabul edildi.
- Arkadaş spor salonundayken host'un görevi yazı gösterirse salonun yazıları kaybolabilir.
- Yarış kontrol noktaları (#22), köprü bariyerleri (#28), çete bölgeleri ve savaşları (#25) sonraki paketlerde gelir.
- Görevlerin açtığı kapı ve geçit gibi nesneler senkron değildir (ilk etkisi #42 ve #60 civarında).
- Arkadaş mülk satın almasın: satın alması hiçbir kayda geçmez, parası gider.
- Alt+Tab yapan oyuncunun oyunu duraklar; host'taysa görev de bekler.
- Çöken host, sunucu eski bağlantısını düşürene kadar (30–60 sn) yeniden bağlanamaz.

## Sonuç kaydı

| Görev | Geçiş | Başarısızlık/tekrar | Temizlik | Not |
|---|---|---|---|---|
| #1–#5 | | | | |
| #6 Drive-Thru | | | | |
| #7 Nines And AK's | | | | |
| #8 Drive-By | | | | |
| #9 Sweet's Girl | | | | |
| #10 Cesar Vialpando | | | | |

Her hata için görev ve faz, hangi oyuncu, beklenen ve görülen davranış, saat ve tekrar adımlarını yazın; yukarıdaki
log dosyalarını ekleyin.

**#10 bitince durun. Grup 1'in sonuçları onaylanmadan #11 ve sonrasına geçilmez.**
