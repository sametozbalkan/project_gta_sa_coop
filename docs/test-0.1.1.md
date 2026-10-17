# 0.1.1-alpha — iki oyunculu test (0.1.0'dan sonraki düzeltme paketi)

Hedef sürüm: **0.1.1-alpha**. Kutuları yalnızca iki bilgisayarda gözledikten sonra işaretleyin.

**Durum (2026-10-17):** 0.1.1, 0.1.0'ın düzeltme paketi; GitHub'da ön sürüm (pre-release) olarak yayımlandı. Görev
sırası ve kayıt yapısı değişmedi: 0.1.0 ile alınan co-op kayıtları yüklenir. **İlk kez oynuyorsanız** New Game ile
başlayın ve görevleri #1'den sırayla oynayın; görev görev ayrıntılı kontroller repodaki `docs/test-0.1.0.md`
listesinde (oradaki sürüm adı yerine 0.1.1-alpha okuyun). Hiçbir madde henüz oyunda denenmedi.

## 0.1.1'de ne değişti

- **Araç senkronu:**
  - Bir arabayı hangi oyunun yönettiği artık açıkça bildiriliyor. İki oyunun aynı arabayı birden yönetip titretmesi
    ya da hiçbirinin yönetmeyip arabanın bir ekranda donması beklenmiyor.
  - Host'un görevi arkadaşın sürdüğü bir arabayı silerse araba iki ekrandan birden kalkar, arkadaş yaya kalır.
  - Oyunlardan birinin kendi başına sildiği bir araba, onu yöneten oyuncunun sonraki güncellemesiyle geri gelir.
  - Sunucu yolcu koltuklarını doğru sayar. Oyundan çıkan oyuncunun sürdüğü host görev arabası host'a geçer.
  - Arabadan her iniş öbür ekrana gider: tuşla iniş, görevin indirmesi, arabadan çekilip atılma.
- **Oturumda yeniden başlama:** biriniz bağlıyken kayıt yüklerse ya da New Game seçerse, öbürünün NPC'leri ve onların
  sürdüğü arabalar geri gelir; host görevdeyse görev karakterleri (Sweet, Smoke, Ryder) işaretleriyle döner.
  Önceden bunlar kayboluyor ya da sürücüsüz donmuş arabalar olarak kalıyordu.
- **Bağlantı saati:** oyunların ortak saati artık geriye kaymıyor; bu, araç ve NPC paketlerinin doğru sırada
  işlenmesi için gerekli.
- **Görev ortasında ayrılan oyuncu:** arkadaş görev sırasında oyundan çıkarsa, oyunu çökerse ya da bağlantısı koparsa
  host'un oyunu çökmez, görev host'la sürer. Arkadaş geri bağlanınca görevin yazılarını, işaretlerini ve sayaçlarını
  görür; o görevin mantığı onu yeniden saymaz, bir sonraki görevde sayar.
- **Hikâye görevleri yalnız host'ta başlar:** arkadaşın oyununda hikâye işaretleri görev başlatmaz; arkadaşın kendi
  yan aktiviteleri (dükkânlar, taksi, spor salonu) serbest.
- **Dükkân çalışanları:** arkadaşın oyununda oluşmuş bir berber ya da dükkân çalışanı, arkadaş çıkınca host'ta
  kaybolmaz.
- **Alt+Tab:** Alt+Tab yapanın basılı tuşları diğer ekranda bırakılır; karakteri yürümeye, arabası hızlanmaya devam
  etmez.
- **Sunucu:** yalnızca oyunun betiklerinin yüklediği hikâye karakterlerini kabul eder.

## Hazırlık ve oturum

1. İkinizde de **aynı teslim paketi** olsun: arkadaş yeni `CoopAndreas-Kurulum.zip` dosyasının tamamını çıkarıp
   **`Kur.bat`** dosyasını çalıştırır. ZIP yanındaki `.sha256` dosyasıyla ZIP özetini karşılaştırın.
2. Sol altta ikinizde de **0.1.1-alpha** yazmalı. `Version mismatch` görürseniz teste başlamayın.
3. Steam, tünel ve bağlanma 0.1.0'daki gibi. Host'un `server-config.ini` dosyasında `story-host` satırı duruyor.
4. İkiniz de oyunu oyun klasöründeki **`CoopAndreas (tani)`** kısayoluyla açın. Açmadan önce eski `CoopAndreas_*.log`
   dosyalarını silin. Sorunda iki taraftan şu dosyaları ve saati gönderin: `CoopAndreas_diag.log`,
   `CoopAndreas_session.log`, `CoopAndreas_mirror.log`, `CoopAndreas_world_state.log`, `CoopAndreas_ped_sync.log`,
   `CoopAndreas_vehicle_sync.log`, varsa `CoopAndreas_crashes\*.log`, bir de host'un sunucu penceresindeki son satırlar.
5. A3 ve B bölümü belirli görevleri ister: #3, #4, #6 ve #9. Bu görevlerden önce ayrı yuvalara alınmış co-op kayıtları
   varsa onlardan devam edin; yoksa New Game'den o göreve kadar oynayın. Co-op kaydını yalnız host, görev dışında
   güvenli evde alır.

Oturum kaydı: tarih `____` · host `____` · arkadaş `____` · paket SHA-256 `____` · manifest commit `____`.

## A. Görev dışında

### A1. Birlikte araba kullanma

- [ ] **Sürücü ve yolcu:** host bir arabayı sürsün, arkadaş yolcu olsun; birkaç dakika gezin. Arabada titreme ya da
  ışınlanma olmamalı.
- [ ] **Yer değiştirme:** host arabadan insin, arkadaş yolcu koltuğunda kalsın. Sonra arkadaş inip şoför koltuğuna,
  host yolcu koltuğuna binsin ve sürün. İki ekranda da araba tek, akıcı ve doğru koltuklarla görünmeli.
- [ ] **Dört kapı:** arkadaş dört kapılı bir arabaya G tuşuyla sırayla sağ ön, sol arka ve sağ arka kapıdan binsin;
  bir kez de sürücü kapısının yanında G'ye bassın. Her seferinde host'un ekranında arkadaş doğru koltukta görünmeli.
- [ ] **Trafik:** yan yana ve sonra ayrı yerlerde 5–10 dakika dolaşın. Çift, donmuş ya da zıplayan trafik arabası
  olmamalı. Bir arabayı patlatıp uzaklaşın: enkaz iki ekrandan birlikte kaybolmalı. İki tarafın
  `CoopAndreas_vehicle_sync.log` dosyasında `destructor-hooks hooked=5/5` satırı olmalı.
- [ ] **Sürücü iner, yolcu devralır:** host sürücü, arkadaş yolcu olsun. Host F ile insin ve yaklaşık 150 m
  uzaklaşsın; arkadaş bir süre bekleyip sürücü koltuğuna geçsin ve sürsün. Rolleri değiştirip tekrarlayın. Araba
  inerken donmamalı ya da kendi kendine ilerlememeli, iki ekranda aynı yerde olmalı.
- [ ] **Arabadan çekme:** arkadaş sürerken host sürücü kapısından onu arabadan çeksin; sonra rolleri değiştirin. İki
  ekranda da çekilen sürüklenerek iner, çeken oturur; araba ileri geri zıplamaz.
- [ ] **Yarım kalan biniş:** arkadaş kilitli bir arabaya binmeye çalışıp vazgeçsin. 10 saniye sonra sunucu
  penceresinde `has been on foot for 10 s … is free again` satırı görünmeli.
- [ ] **Yoğun trafikte katılma:** host yoğun trafikte sürerken arkadaş bağlansın ve host'un yanına gitsin: arkadaşın
  ekranında donmuş araba olmamalı.
- [ ] **Ters araba:** birinizin arabasını açık bir garajın içinde ters çevirin ve 30 saniye bekleyin. Araba durmadan
  kaybolup belirmemeli (30 saniyede en fazla 3 kez), oyun çökmemeli.

### A2. Alt+Tab'da tuşlar

Alt+Tab yapan "A", izleyen "B" olsun; ikiniz de bir kez A olun.

- [ ] **Yürürken:** A, W'ye basılı koşarken Alt+Tab yapıp 10 saniye masaüstünde kalsın. B'nin ekranında A'nın
  karakteri hemen durmalı. A dönünce karakter normal hareket etmeli.
- [ ] **Araba kullanırken:** A, W basılıyken hızlanırken Alt+Tab yapıp 10 saniye beklesin. B'nin ekranında araba
  hızlanmamalı; gaz kesilmiş gibi süzülüp yavaşlamalı (bir yere çarpabilir). A dönünce araba gerçek yerine sıçrayabilir;
  bu beklenen davranış.
- [ ] **Nişan ve ateş:** A otomatik silahla (Tec-9, AK) sağ tıkla nişan alıp sol tıkla sürekli ateş ederken Alt+Tab
  yapsın. B'nin ekranında ateş hemen kesilmeli, karakter silahı indirmeli. Aynısını yalnız nişan alırken de deneyin.
- [ ] (İsteğe bağlı) Yolcuyken drive-by ateşi sırasında Alt+Tab: B'de ateş kesilmeli.

### A3. Oturumda yeniden başlama

- [ ] **Host kayıt yüklüyor:** Ganton'da trafiğin yoğun olduğu bir caddede yan yana bir dakika bekleyin. Host ESC →
  Load Game ile bir co-op kaydı yüklesin. Host'un ekranında arkadaşın NPC'lerinin sürdüğü arabalar sürücüleriyle
  hareket etmeli, sürücüsüz donmuş araba olmamalı. Sunucu penceresinde `started over: N peds sent again` satırı
  çıkmalı.
- [ ] **Arkadaş New Game:** aynısını arkadaş ESC → New Game ile yapsın. Arkadaşın ekranında host'un trafiği ve
  yayaları geri gelmeli.
- [ ] **Host görevdeyken:** arkadaş önceden Grove Street'teki evde kendi kaydını alsın. Host #4 Tagging Up Turf'ü
  başlatsın; Sweet ekrandayken arkadaş o kaydı yüklesin (ya da New Game seçsin). Arkadaşın ekranında Sweet host'taki
  yerinde ve hareket ederek, radar işaretiyle görünmeli.
- [ ] (İsteğe bağlı) #6 kovalamacasında arkadaş Greenwood'u sürerken New Game seçsin: sunucuda `… of the host's
  mission goes back to … started over` satırı çıkmalı; Greenwood host'ta kalmalı, kovalamaca sürmeli. Arkadaşın
  oyunu açılınca Greenwood bıraktığı yerde tek kopya olarak görünmeli.

### A4. Hikâye işaretleri ve yan aktiviteler

- [ ] **Hikâye işareti:** host uzaktayken arkadaş tek başına bir hikâye işaretine girsin (ör. Sweet'in evi, Ryder ya
  da Smoke). Görev başlamamalı. Host girince başlamalı.
- [ ] **Yan aktiviteler:** arkadaş host'tan bağımsız olarak Binco'dan kıyafet alsın ya da Reece's'te saç kestirsin,
  taksiye binip 2 tuşuyla taksi görevi başlatsın, açıksa Ganton spor salonunda çalışsın. Hepsi çalışmalı.

## B. Görevlerde

Hazırlık: host'un `CoopAndreas_ped_sync.log` dosyasında, arkadaş görev ortasında çıkınca `stand-in=kept`, görev bitince
`stand-in=released` satırları görünmeli.

### B1. #3 Ryder — berberde arkadaşın çıkması

- [ ] İkiniz de Reece's berberine girin; berber arkadaşın oyununda oluşsun diye **arkadaş önce** girsin.
- [ ] Host saç kestirirken arkadaş menüden **Quit Game** ile çıksın.
- [ ] Beklenen: host'ta çökme yok, berber yerinde kalır, saç kesimi biter. Sunucu penceresinde
  `ped … goes to …, who claimed it` satırı görünür.

### B2. #3 ve #4 — görevin arabadan indirmesi

- [ ] #3 Ryder'da pizzacıya varınca ve #4 Tagging Up Turf'te ilk grafiti noktasında, görev sizi arabadan indirirken
  iki ekranda da iki oyuncu normal iniş animasyonuyla inmeli; iniş iki kez oynamamalı, araba kendi kendine
  gitmemeli. Loglarda `exit-notice` satırları görünür.

### B3. #4 Tagging Up Turf — menüden çıkış

- [ ] Görevi başlatın; ikiniz de Sweet'in arabasına binin, host sürsün.
- [ ] Idlewood yolunda arkadaş menüden **Quit Game** ile çıksın.
- [ ] Beklenen: host'ta çökme yok, sohbette `disconnected` yazar, arkadaşın koltuğu boşalır. Görev host için sürer ve
  geçilebilir.
- [ ] Arkadaş yeniden bağlansın: host'un görev yazılarını, işaretlerini ve kürelerini görmeli; görev onu beklememeli.

### B4. #6 Drive-Thru — görev sonunda arkadaşın arabası

İlk geçişte (görevi normal bitirin):

- [ ] Görev bittiğinde arkadaş kendi Greenwood'unu sürmeye devam etsin: içindeyken araba kaybolmamalı.
- [ ] Sonra arkadaş arabadan inip uzaklaşsın. Araba kaybolursa iki ekrandan birlikte kaybolmalı; host'un ekranında
  donmuş bir hayalet araba kalmamalı.
- [ ] (İsteğe bağlı) Aynısını #2 Big Smoke'taki BMX ile deneyin.

### B5. #6 Drive-Thru — kovalamacada arkadaşın oyunu kapanırsa

İkinci geçişte (#6'dan önceki kayıttan):

- [ ] Kovalamaca sürerken, arkadaş kendi Greenwood'unu sürerken oyununu Görev Yöneticisi'nden kapatsın (`gta_sa.exe`,
  Görevi sonlandır).
- [ ] Beklenen: host'un ekranında arkadaş 30–60 saniye donuk görünür, sonra sohbette `disconnected` yazar. Host'ta çökme
  olmaz; Greenwood kaybolmaz, yerinde kalır; kovalamaca sürer ve görev geçilebilir. Sunucu penceresinde
  `[Vehicle] : vehicle … of the host's mission goes back to …` satırı görünür.
- [ ] Arkadaş yeniden bağlanıp görevin yazılarını ve işaretlerini görmeli.

### B6. #9 Sweet's Girl — görevin arkadaşın arabasını silmesi

Bu sahne zorlanmış bir test: görev normalde yalnız kendi arabalarını siler.

- [ ] Otoparktaki saldırganlar temizlendikten sonra arkadaş, Ballas'ların Sentinel'lerinden birine binsin ve host'la
  birlikte Grove Street'e sürsün.
- [ ] Son ara sahne başlarken görev o arabayı siler (host içinde değilse).
- [ ] Beklenen: araba iki ekrandan birden kaybolur, arkadaş yaya kalır, çökme olmaz. Arkadaşın
  `CoopAndreas_vehicle_sync.log` dosyasında `removal-applied`, host'unkinde `host-mission-delete` görünür.

## C. Her görevde ve dolaşırken

- [ ] Arkadaşın ekranında hikâye karakterleri görünür (Sweet, Smoke, Ryder, Emmet …). Sunucu penceresinde
  `tried to spawn a ped with special model` ile başlayan sarı uyarı çıkmaz; çıkarsa satırdaki ismi bildirin.
- [ ] Çift, donmuş, havada asılı ya da kendiliğinden kaybolan araba yok.
- [ ] **Uzun oturum:** oturum sonunda iki tarafın `CoopAndreas_vehicle_sync.log` dosyası 2 MB'ın altında olmalı ve
  oturumun sonuna kadar yazmış olmalı; satırların çoğu dakikalık `routine` özeti olmalı. `CoopAndreas_diag.log`
  dosyasındaki `clock:` satırında `ignored` sayısı küçük kalmalı.

## Bilinen sınırlar

- Görev ortasında geri gelen arkadaş o görevin mantığına katılmaz, yalnız yazılarını ve işaretlerini görür; sonraki
  görevde normal katılır.
- Host'un kendi bağlantısı görev ortasında koparsa bazı görevler (#2–#5, #7) araç beklerken takılabilir; sonraki
  düzeltme paketinde.
- Arkadaş bir arabaya binmeye başlayıp vazgeçerse sunucu onu 10 saniye o koltukta sayar.
- Tren ve görev nesneleri (kasa, kapı, varil) bu pakette de arkadaşa gitmez; tasarımları hazır
  (`docs/design/trains.md`, `docs/design/mission-objects.md`), grup 2'den önce gelecek.

## Sonuç

| Bölüm | Sonuç | Not |
|---|---|---|
| A1 Birlikte araba | | |
| A2 Alt+Tab | | |
| A3 Oturumda yeniden başlama | | |
| A4 Hikâye işaretleri, yan aktiviteler | | |
| B1 #3 berber | | |
| B2 #3, #4 görevin indirmesi | | |
| B3 #4 menüden çıkış | | |
| B4 #6 görev sonu araba | | |
| B5 #6 kovalamacada kapanma | | |
| B6 #9 araba silme | | |
| C Genel | | |
