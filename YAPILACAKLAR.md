# Yapılacaklar

Durum (2026-10-16): host ve arkadaşı Steam tüneliyle oynuyor. Grup 1'in ilk iki oyunculu turunda (0.3.102) #1–#6
geçti; #7'de bulunan hata 0.3.103'te düzeltildi. 0.3.103 testi atlandı (kullanıcının kararı): ikinci tur doğrudan
0.1.0 ile yapılacak.
Proje GitHub'da: github.com/sametozbalkan/project_gta_sa_coop. Grup 1 teslimatını Codex `codex/group1-delivery`
branch'inde hazırladı, mimar (Claude) ve Opus 5.5 ajanları sürdürdü; 2026-10-16'da branch `main`'e alındı ve iş
`main`'den devam ediyor.

## 1. dalga (bitti; test paketi 0.3.100-alpha, test listesi `docs/test-dalga-1.md`)
| İş | Ajan | Durum |
|---|---|---|
| Alt+Tab'da sunucudan düşmeyi düzelt | gtasa-coder-high | birleştirildi (1f159e8) |
| Tagging Up Turf: arkadaşın sıktığı tag sayılmıyor | gtasa-coder-high | birleştirildi (fff678f) |
| Radyo senkronu (aynı araçta aynı istasyon) | gtasa-coder-high | birleştirildi (98fff3d) |
| Görev derleme hattı, görev uyarlama rehberi ve görev envanteri | gtasa-coder-high | birleştirildi (5d7d80a) |
| Tünel yayın betiği, yeni repo düzeni için paketleme, pakete yama arşivi seçeneği | gtasa-coder | birleştirildi (35db590) |
| Discord Rich Presence'ı kapat, dış bağlantıları denetle | gtasa-coder | birleştirildi (294dc4f) |
| Gizlilik kalıntıları: çökme raporu yüklemesi, ImGui linkleri, eski Discord kaydı, sunucu `bind` ayarı | gtasa-coder | birleştirildi (2322d12) |
| Upstream PR #106 incelemesi (para, aranma, hileler) | gtasa-coder-high | alınmadı, aşağıya bak |

## 2. dalga (grup 1 iki oyunculu test bekliyor)
| İş | Ajan | Durum |
|---|---|---|
| Görevlerin aranma komutları host'tan iki oyuncuya; San Fierro/Las Venturas kilidi arkadaşa da (sürüm 0.3.101) | gtasa-coder-high | birleştirildi (59d055e) |
| Görev grubu 1: #6 Drive-Thru, #7 Nines And AK's, #8 Drive-By, #9 Sweet's Girl, #10 Cesar Vialpando | gtasa-coder / -high | kaynaklar main'de (ecbab75, e4f12f7, 2f9cfda, c297af5, 14b15e9); birlikte derlendi, test onayı bekliyor |
| Ryder restoran/pompalı sahnesinde arkadaşın çökmesi (0x4D68BA) | gpt-6.1-sol / high | 0.3.102-alpha: NPC yaşam süresi/model koruması ve en güncel durumu erteleme; 0.3.102 turunda bu sahnede çökme bildirilmedi |
| Nines And AK's: host arkadaşın ekranında kayboldu, Binco kapısında arkadaş çöktü (0x4D68BA) | gtasa-coder-high | 0.3.103-alpha: arkadaşın oyunu, içinde host'un oturduğu aracı silince oyun host'un karakterini de siliyordu. Araç silinmeden önce oyuncular dışarı alınıyor, oyun ağ oyuncularını silmiyor, kaybolan karakter 1 saniyede yeniden oluşturuluyor; kapı, araç, silah ve kıyafet işlemleri karakter hazır değilse bekliyor. Derlendi, iki oyunculu test bekliyor |
| Arkadaşın eşyalarını host'un kaydıyla saklama | gtasa-coder-high | 0.3.103-alpha (abf127d): silah, can, zırh, para, kıyafet, dövüş stili, özellikler ve beceri istatistikleri kaydın yanında `.coop` dosyasında; yükleyince ve çökme sonrası yeniden bağlanınca geri gelir. İki yeni paket türü. Derlendi, iki oyunculu test bekliyor |
| README (İngilizce + Türkçe), gizlilik belgesi, kurulumda tanı kısayolu | gtasa-coder + mimar | README birleştirildi (9113d9d); isim ve Buy Me a Coffee rozeti bekliyor |
| Açılıştaki Device Selection listesinde 2560×1440 yok | Codex + gpt-6.1-sol / high inceleme | Açılışa ait ayrı en-boy oranı filtresi, doğrulanan 1.0 US baytlarıyla kaldırıldı; gerçek pencere seçimi kullanıcı testi bekliyor. İstemci düzeltmesi, sürüm/protokol 0.3.102-alpha kalır. |

Teslimatlar (arkadaş paketi `dist/CoopAndreas-Kurulum.zip`; `manifest.json` kaynak commit'ini ve dosya SHA-256'larını,
yanındaki `.zip.sha256` arşiv özetini verir; eski paketler ve doğrulama günlükleri `backups/` altında, git dışında):
- **0.3.101-alpha** (Codex, 2026-10-15): grup 1 görev dosyaları birlikte derlendi (kayıt ve boyut denetimleri geçti),
  test listesi `docs/test-grup-1.md` (pakette `TEST-LISTESI.md`), doğrulanabilir paket.
- **0.3.102-alpha** (Codex): Ryder sahnesindeki NPC çökmesi; açılış penceresinde geniş ekran çözünürlükleri.
- **0.1.0-alpha** (2026-10-16): görev yansıtma katmanı, dünya durumu kaydı, host sabitleme, arkadaşın saygısının
  saklanması, #2–#10 yeni kalıpta; 0.3.103'ün düzeltmeleri de içinde. Test listesi `docs/test-0.1.0.md`.
- **0.3.103-alpha** (2026-10-16, testi atlandı): host'un kaybolması ve Binco çökmesi düzeltmesi, arkadaşın durumunun kayıtla
  saklanması, kurulumda `CoopAndreas (tani)` kısayolu. **Eski sürüm düzeninin son paketi.** İki tarafta da kurulmalı.

## Görev grupları (plan)
Görevler hikâye sırasıyla, gruplar halinde uyarlanıyor; her grup tek pakette gelir. **Bir grup bitince paket size
gelir, siz test edip onay verene kadar sıradaki grup başlamaz** (kullanıcının kuralı); hata düzeltmeleri ve görev
dışı işler bu arada sürebilir. Hiçbir görev atlanmaz: engele takılan görevde grup durur, engel çözülünce devam
edilir; tek kişilik mini oyunlarda ikinci oyuncu izler. Kurallar: görev sırası bozulmaz (açılma sırası, tetikler, ilerleme bayraklarına dokunulmaz) ve kayıtlar
paketten pakete açılmaya devam eder (derleme hattı her pakette denetler). Liste ve riskler: `docs/missions.md`.
- Los Santos: grup 1 #6–#10 (Sweet), grup 2 #11–#17 (Smoke, Ryder), grup 3 #18–#22 (müzik ve Cesar),
  grup 4 #23–#28 (Crash, Doberman, Los Sepulcros, Reuniting The Families, The Green Sabre).
- Sonra Badlands (#29–#39), San Fierro (#40–#65), çöl (#66–#74), Las Venturas (#75–#92), Los Santos'a dönüş (#93–#103).
- #1–#6 ilk turda (0.3.102) geçti; #7–#10 ikinci turda (0.3.103).

## Grup 1 testinden sonra, grup 2'den önce: mimari iyileştirmeler (kullanıcıyla konuşuldu, onaylanırsa)
Temel model kalıyor: görev host'ta çalışır, arkadaş yansımasını görür. İki iyileştirme önerildi; grup 1 testi
bitince öneri olarak sunulacak, kullanıcı onaylarsa grup 2 yeni kalıpla başlar.
1. **Genel görev yansıtma katmanı** (2-3 gtasa-coder-high işi): host'un görev betiğinin ekranda gösterdiği her şey
   (hedef yazıları, araç/karakter işaretleri, kontrol noktaları, zamanlayıcı ve sayaçlar, görev ekranı göstergeleri)
   istemci kodunda otomatik olarak arkadaşa da gider; görev betiğine satır eklenmez. Kazanç: uyarlanmamış görevler bile
   "arkadaş hedefleri görüp yardım edebiliyor" düzeyine çıkar, uyarlama sadece oyun kararlarına (koltuk, ara sahnede
   konum, başarısızlık koşulları) iner, görev boyutu şişmez (69.000 bayt sorunu hafifler). Dikkat: arkadaşa ait
   olmayan mesajlar da görünür (ekip olarak sorun değil); elle uyarlanmış görevlerde aynı yazı iki kez çıkmasın diye
   bir kural gerekir; uyarlama rehberi buna göre güncellenir.
   - İlk 10 görev (#1–#10, elle yazı/işaret satırlarıyla uyarlanmış): kullanıcı kaydı dert etmiyor, New Game'den
     baştan oynamak sorun değil. Bu yüzden tam tutarlılık: katman gelince #1–#10 da yeni kalıba geçirilir (gereksiz
     kalan yazı/işaret satırları çıkar, oyun mantığı kalır), New Game'de yeniden test edilir. Katmanın "tekrar
     gösterme" ayıklamasına gerek kalmaz, daha sade olur. İsteğe bağlı: aynı turda atlanan giriş görevi (havaalanı +
     Grove'a BMX) co-op olarak geri getirilebilir.
2. **Dünya durumu kaydı:** host'un dünyasının kurallarını belirleyen şeyler (şehir kilidi, harita nesne grupları ve köprü
   bariyerleri, çete bölgeleri, en yüksek aranma seviyesi, garajlar vb.) tek bir yerde tanımlanır; değiştikçe ve biri
   oyuna katıldığında otomatik gönderilir. Şehir kilidi bu kalıpla yapıldı; farkları tek tek kovalamak yerine genel yapı.
   Köprü bariyerleri işi bunun içinde yapılır.

Tasarımlar ve plan hazır (2026-10-16; kararlar onaylandı, uygulama başladı): `docs/design/README.md`
(kararlar, iki tasarımın birleşimi, sıra), `docs/design/mirror-layer.md`, `docs/design/world-state.md`. Sonraki paket
**0.1.0-alpha** (grup 1, yeni sürüm düzeni): ortak temel L0 (yaşam döngüsü, host'tan aktarım, host sabitleme), sonra
M1 + WS-1, sonra M2–M4 + WS-3 + WS-4, sonra #1–#10 dönüşümü (C1) ve New Game'den yeniden test. İhtiyaç duyan gruptan
önce: M5 (#22), WS-2 köprü bariyerleri (#28), WS-5 çeteler (#25), WS-6 (#42), nesne senkronu tasarımı (#60), WS-7 (#99).
Kullanıcı kararları (2026-10-16): görev geçiş ödülü (para ve saygı) ikinize, bahis ve harcamalar yalnızca host'ta; çete
savaşını host'un oyunu başlatır, arkadaşın öldürdükleri de sayılır; host sabitleme. 0.3.103 testi atlandı:
0.1.0 doğrudan iki kişiyle test edilecek; 0.3.103'ün kontrolleri 0.1.0 test listesinde (`v0.3.103-alpha` etiketi duruyor).
İlerleme: L0 birleştirildi (eca37e3: oturum olayları, host sabitleme, host düşünce arkadaşın kontrolü geri alması);
WS-1 birleştirildi (d633f08: dünya durumu kaydı, şehir kilidi ona taşındı, en yüksek aranma seviyesi; paket biçimi
değişti, main artık 0.1.0-alpha ve 0.3.103 ile bağlanamaz). M1 birleştirildi (675cfe9: görev yazıları, başlıklar ve
geçiş ödülleri arkadaşa; 102 hikâye betiğinde her geçiş ödülü kurala takılıyor, #10'un bahsi yalnızca host'ta, ödemesi
(bahsin iki katı) ikinize; #103'teki 250.000$ geçişten kareler önce verildiği için o görev uyarlanırken karar verilecek).
3. dalga birleştirildi (2f8cf8e'ye kadar): M2 görev işaretleri, M3 sarı küreler (18 kanca), M4 sayaçlar ve
zamanlayıcılar (5 kanca), WS-3 garajlar ve dükkân kapıları, WS-4 görev sonu ayarları, P1 arkadaşın saygısının kayıtla
saklanması. Kanca denetimi 411 nokta; on politika testi geçiyor. C1 (#2–#10 dönüşümü, 9.181 bayt kazanç) ve uyarlama
rehberi birleştirildi. **0.1.0-alpha paketlendi** (etiket `v0.1.0-alpha`, test listesi `docs/test-0.1.0.md`); host'un
oyununa kuruldu, iki oyunculu test bekliyor.
**0.1.1 GitHub'da ön sürüm olarak yayımlandı** (2026-10-17, etiket `v0.1.1-alpha`; kullanıcının arkadaşlarının testi
için; oyun kopyasında hâlâ 0.1.0 var). Önceki plan: 0.1.0 testinden sonra, testte çıkan düzeltmelerle birlikte
gelecek, test listesi `docs/test-0.1.1.md`):
- **Araç senkronu:** aracı kimin yönettiği açıkça bildiriliyor; araç kayıtları aracın yok edilmesiyle bitiyor; host
  görevinin sildiği araç her yerden kalkıyor; sunucuda oyuncu başına tek koltuk var; çıkan oyuncunun sürdüğü host görev
  arabası host'a geçiyor. Paket biçimi değişti: main artık 0.1.1-alpha ve 0.1.0 ile bağlanamaz.
- **Görev ortasında ayrılan oyuncu:** host'ta yer tutucu karakter, SWEET1 ve SWEET3'te ek denetim, sahiplenilen dükkân
  çalışanının devri.
- **Diğerleri:** hikâye görevleri yalnız host'ta başlıyor; Alt+Tab'da tuşlar bırakılıyor; sunucunun özel karakter
  listesi düzeltildi; Ghidra ad hattı `FUNC_` tanımlarını okuyor.
- **Durum:** araç paketi iki inceleme ve iki düzeltme turundan geçti. Bunlarla gelenler: oturumda yeniden başlayan
  oyun NPC'leri sunucudan baştan alıyor, bağlantı saati geriye kaymıyor, her arabadan iniş öbür ekrana gidiyor.
  Kanca denetimi 417 nokta, 14 politika testi geçiyor; 0.1.1 hâlâ paketlenmedi.

## Sonraki işler (öncelik sırasıyla)
- **Grup 2 altyapısı: trenler ve görev nesneleri** (tasarımlar 2026-10-17'de hazır: `docs/design/trains.md`,
  `docs/design/mission-objects.md`):
  - **Kararlar kullanıcıda:** rastgele trenler yalnız host'ta, Catalyst'te arkadaşın yeri, arkadaşın kasa taşıması ve
    forklift, kalıcı kapı ve geçitler, görev patlamaları, kasalardan çıkan eşyalar.
  - **İş yükü:** tam planla ~10–11 ajan işi (ardından grup 2 uyarlamaları 4–5); tasarruflu yolla ~4–5 (+3). Kullanıcının
    limiti düşük; 0.1.0 testinden sonra karar verilecek.
  - **Tasarımların bulduğu mevcut hatalar:**
    - arkadaşın mermileri host'ta nesnelere işlemiyor, ağda olmayan bir yayaya isabet 0 numaralı NPC'ye gidebiliyor
      (OB-0, küçük; nesne çekirdeğiyle ya da tek başına);
    - her oyun kendi rastgele trenlerini senkronsuz üretiyor (#11, #13, #16'da arkadaşın treni yolda kalıyor);
    - trene binen oyuncu öbür ekranda bindiği yerde donuk;
    - tren tutamacı içeren tekrarlanan bir komut 0 numaralı araca gidiyor.
  - **Kalıcı nesneler** (kapı, geçit, kulübe) ilk #23'te gerekiyor; `docs/design/README.md` W-D11 #60 diyor, karar
    kaydedilirken düzeltilecek.
- **Host'un kendi bağlantısı görev ortasında koparsa:** INTRO1, INTRO2, SWEET1, SWEET1B ve SWEET2'deki ağ kimliği
  bekleme döngülerinin zaman aşımı yok; görev host'ta takılabilir. SWEET3'teki 2 saniyelik zaman aşımı bunlara da
  eklenmeli (görev içi küçük değişiklik).
- **Araç senkronunda açık kalanlar** (küçük, nadir):
  - 255 araç numarasının hepsi doluyken sunucu araç oluşturma isteğine cevap vermiyor;
  - yeniden oluşturulan kopyanın hasarı bir sonraki hasar güncellemesine kadar eksik;
  - host'un eski görevinden kalıp arkadaşın yönettiği bir araba, host'un yeniden başlamasından sonra kalıyor;
  - istemcinin geçici yaya yuvaları zaman aşımına uğramıyor;
  - bazı modüller saat senkronunu `m_serverTimeOffset != 0` ile soruyor; `CServerTime::m_bSynced` daha doğru.
- **Görev uyarlama denetleyicisi:** ayrılan oyuncu kurallarını (denetim ve kullanım aynı karede, `wait` arada değil)
  tarayan betik `tools/` altına alınıp `compile-scm.ps1`'e bağlanabilir.
- **Görev boyutu sınırı:** Reuniting The Families (#27) 69.000 baytlık sınıra 561 bayt uzakta. Yansıtma katmanıyla
  #27'nin küçük bir uyarlaması bu alana sığıyor (kayıt 2, oyuncu toplama 11, ışınlama 11 bayt); sınırı yükseltmek
  artık #27 için şart değil, ama co-op bilardo (`POOL2`) gibi büyük işler için gerekebilir. ReVa ile ilk bulgular (2026-10-16): 200.000 (MAIN) yalnızca
  `CTheScripts::Init`'te iki okumada, 69.000 yalnızca görev yükleme komutunun (0417) işleyicisinde geçiyor; toplam
  269.000, Hoodlum exe'sinde bir sayaç olarak duruyor (0x401CF8). Betik alanını (0xA49960) 21 fonksiyon 58 yerde
  kullanıyor ve hemen arkasında başka bir değişken var: alan yerinde büyütülemez; ya taşınır (bütün kullanımlar
  yamalanır) ya da görev bloğu için başka bir yol bulunur.
- **Görev uyarlanırken verilecek kararlar:** #26'da SWEET7 host'un saygısını 200'e çekiyor (`0629`), yansımıyor;
  #103'teki 250.000$ geçişten kareler önce verildiği için yansımıyor.
- **Temizlik** (0.1.0 testinden sonra; testte denenen kapı koduna dokunuyor): dünya durumu kaydına yerel kaynaklı
  girişler için arayüz (kapı adaptörü şu an sahte bir paket yolu kullanıyor).
- **Görevlerden önce gelecek altyapı paketleri** (`docs/design/README.md` 4. bölüm; her biri ihtiyaç duyan gruptan
  önce): M5 yarış kontrol noktaları ve betik küreleri (#22, grup 3), WS-5 çete bölgeleri ve savaşları, arkadaşın
  öldürdüklerinin host'un savaşında sayılması dahil (#25, grup 4), WS-2 köprü bariyerleri ve yollar (#28, grup 4),
  WS-6 harita nesneleri (#42), nesne senkronu tasarımı (#60), WS-7 ayaklanma (#99). En yüksek aranma seviyesi ve
  sonradan katılan için anlık durum 0.1.0'da geldi.
- **Görevlerin isteyeceği istemci özellikleri** (sırası gelince): yangın (#23), çete savaşları (#25, #98), römork (#31),
  RC araçlar (#44–#46), paraşüt (#72, #81, #89), jetpack (#73). Tek kişilik mini oyunlarda (#10 lowrider, #18 dans,
  #43 sürücü okulu, #70 uçuş okulu) ikinci oyuncu izler.
- **Bağımsız proje (yeniden adlandırma), isim bekleniyor** (öneriler: Tandem, Grove Pact, Riding Shotgun). Tek paket:
  oyundaki pencere başlığı, sürüm yazısı ve mesajlar; DLL, mod klasörü, ini, başlatıcı, kısayol, kurulum, paket ve log
  adları; repodaki `CoopAndreas/` klasörü, betikler, belgeler ve adı içeren birkaç kod tanımı; istenirse GitHub repo adı.
  Kalanlar: GPL-3.0 ve LICENSE, DLL'deki orijinal telif satırı ("Tornamic (C) 2026", yanına bizimki eklenir), README'de
  tek teşekkür satırı, git geçmişi, genel "co-op" kelimeleri (`Coop.` görev komutları, kayıttaki "(Coop)" öneki).
  Kurulum eski kurulumu yeni ada taşır (dosyalar, kısayollar, ini'deki takma ad ve sunucu), kayıtlar etkilenmez.
  Aynı pakette: ENet'in lisans dosyası (`third_party/enet`'te eksik). Upstream otomatik takip edilmez.
- **Sürümleme** (kullanıcının kararı, 2026-10-16; 0.1.0 ile başladı): orta sayı = teslim edilen görev grubu (grup 1 =
  0.1, grup 2 = 0.2 …), son sayı = o grubun düzeltme paketleri, 1.0.0 = hikâyenin tamamı co-op; "-alpha" hikâyenin
  büyük kısmı bitene kadar kalır.
- **Buy Me a Coffee:** hesap açılınca README'nin başına rozet ve `.github/FUNDING.yml` (GitHub'ın Sponsor düğmesi).
  Bağış gönüllü kalır; parayla açılan özellik ya da erken erişim yok, GTA/Rockstar logosu yok.
- Upstream PR #106 **alınmadı**: sonradan katılan oyuncu diğerinin parasını sıfırlıyor, aynı anda kazanılan para
  kayboluyor, hileler iki oyunda birden çalışıyor. Para herkesin kendine kalıyor (uyarlanmış görevlerin ödülü ikinize de
  ödeniyor), hileler yerel kalıyor; aranma seviyesi 2. dalgada host'un görev komutlarıyla geliyor.

## Ana hikâyeden sonra (acelesi yok)
- Yan içerik: yarışlar, okullar, araç yan görevleri vb.
- **Bilardo co-op** (kullanıcının isteği): ikinizin birbirine karşı oynaması. Oyundaki bilardo tek oyuncu vs. NPC;
  iki oyuncuya çevirmek mini oyunun durumunu senkronlamayı ister. Betiği `POOL2` 62.754 bayt (sınıra 6.246 bayt),
  yani #27 için yapılacak boyut sınırı çözümü burada da işe yarar.

## Bilinen sınırlamalar
- Eski (Steam) kayıtlar yüklenemez; her zaman New Game. Co-op kaydını host alır: güvenli evde, görev dışındayken.
- Alt+Tab'da oyun duraklar (host'taysa görev de bekler); bağlantı kopmaz. 0.1.1'den itibaren basılı tuşlar diğer
  ekranda bırakılır.
- Arkadaşın saklanan durumu takma adıyla eşleşir; takma ad değişirse bulunmaz. `.coop` dosyası kaydıyla birlikte taşınmalı.

## Bakım
- **ReVa (Ghidra MCP)** kuruldu (2026-10-16), yalnızca bu projede: Claude Code bu klasörde açılınca kendiliğinden
  başlar (~8 sn, ~2 GB RAM), tamamen yerel. gta_sa.exe bir kez analiz edildi, gta-reversed ve plugin-sdk adları
  aktarıldı (`tools/ghidra/`). Kaldırmak için `claude mcp remove ReVa -s local`. Kullanıcı ortamındaki `JAVA_HOME` artık
  olmayan bir klasörü gösteriyor (JDK 21.0.12'ye güncellenmiş); ReVa kendi ayarıyla çalışıyor, istenirse `JAVA_HOME`
  `C:\Program Files\Java\latest\jdk-21` yapılabilir.
- Ghidra ad hattının yeni adları (11) projeye henüz işlenmedi: bu klasörde ReVa'lı hiçbir oturum açık değilken
  `pwsh -File tools/ghidra/update-project.ps1` çalıştırılmalı.
- Arkadaşın Wi-Fi'ı ~90 ms sıçramalar yapıyor olabilir (güç tasarrufu); kablo ya da Maksimum Performans.
