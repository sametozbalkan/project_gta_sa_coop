# Yapılacaklar

Durum (2026-10-16): host ve arkadaşı Steam tüneliyle oynuyor. Grup 1'in ilk iki oyunculu turunda (0.3.102) #1–#6
geçti; #7'de bulunan hata 0.3.103'te düzeltildi, ikinci tur bekleniyor.
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
- **0.3.103-alpha** (2026-10-16): host'un kaybolması ve Binco çökmesi düzeltmesi, arkadaşın durumunun kayıtla
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

## Sonraki işler (öncelik sırasıyla)
- **Sağlamlık:** geçersiz oyuncu tutamacı verilen co-op komutları host'u çökertiyor (ör. arkadaş görev ortasında
  düşerse); 0.3.103'te host tarafındaki yazı ve işaret komutlarına boş oyuncu denetimi eklendi, kalan yerler ayrıca
  taranacak. Görevi kimin başlatabileceğini denetleyen host kontrolü (`CanPlayerStartMission`) upstream'de yorum satırı.
- **Araç senkron protokolü** (ayrı gtasa-coder-high işi): `ASSIGN_VEHICLE` aracı kimin senkronladığını belirleyen
  bayrağı ayarlamak yerine tersine çeviriyor (iki taraf anlaşamayabilir); onaylanmadan silinen geçici araç eski bir
  işaretçi bırakıyor; host'un görevi arkadaşın sürdüğü (arkadaşın senkronladığı) aracı silerse silme arkadaşa gitmiyor
  ve host'ta eski bir araç işaretçisi kalıyor (`Hooks/WorldHooks.cpp`); sunucu yolcu koltuğu -1'i sürücü koltuğuna
  çeviriyor (`server/src/PacketHandlers/vehicles.cpp`).
- **Görev boyutu sınırı:** Reuniting The Families (#27) 69.000 baytlık sınıra 561 bayt uzakta; #27'den önce ya görev
  içinde kısaltma ya da sınırı yükselten istemci yaması. ReVa ile ilk bulgular (2026-10-16): 200.000 (MAIN) yalnızca
  `CTheScripts::Init`'te iki okumada, 69.000 yalnızca görev yükleme komutunun (0417) işleyicisinde geçiyor; toplam
  269.000, Hoodlum exe'sinde bir sayaç olarak duruyor (0x401CF8). Betik alanını (0xA49960) 21 fonksiyon 58 yerde
  kullanıyor ve hemen arkasında başka bir değişken var: alan yerinde büyütülemez; ya taşınır (bütün kullanımlar
  yamalanır) ya da görev bloğu için başka bir yol bulunur.
- **Köprü bariyerleri (The Green Sabre #28'den önce):** San Fierro/Las Venturas köprülerindeki bariyerler bir harita
  nesne grubu; host'un görevleri kaldırınca arkadaşın oyununda fiziksel olarak duruyor. Çözüm: ilgili IPL komutları
  (0776/0777/0778) uyarlanmış görevlerde senkron + oyuna sonradan katılana anlık durum. Aynı pakete: sonradan
  katılanın en yüksek aranma seviyesi (Verdant Meadows 6'ya çıkarıyor) ve çete bölgesi gücü.
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
- **Sürümleme** (kullanıcının kararı, 2026-10-16): 0.3.103 eski düzenin son paketi. Sonraki paketten (isim değişikliği
  ya da grup 2, hangisi önce) itibaren orta sayı = teslim edilen görev grubu (grup 1 = 0.1, grup 2 = 0.2 …), son sayı =
  o grubun düzeltme paketleri, 1.0.0 = hikâyenin tamamı co-op; "-alpha" hikâyenin büyük kısmı bitene kadar kalır.
- **Buy Me a Coffee:** hesap açılınca README'nin başına rozet ve `.github/FUNDING.yml` (GitHub'ın Sponsor düğmesi).
  Bağış gönüllü kalır; parayla açılan özellik ya da erken erişim yok, GTA/Rockstar logosu yok.
- **Saygı (respect) saklama:** arkadaşın saygısı şimdilik kayıtla saklanmıyor (oyun onu bileşenlerden hesaplıyor);
  gerekirse sonra.
- Alt+Tab'a küçük ek: pencere arka plana geçerken "tuşlar bırakıldı" bilgisi gönderilsin (diğer ekranda basılı kalan
  tuşla yürümeye devam etmesin).
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
- Alt+Tab'da oyun duraklar (host'taysa görev de bekler); bağlantı kopmaz.
- Arkadaşın saklanan durumu takma adıyla eşleşir; takma ad değişirse bulunmaz. `.coop` dosyası kaydıyla birlikte taşınmalı.

## Bakım
- **ReVa (Ghidra MCP)** kuruldu (2026-10-16), yalnızca bu projede: Claude Code bu klasörde açılınca kendiliğinden
  başlar (~8 sn, ~2 GB RAM), tamamen yerel. gta_sa.exe bir kez analiz edildi, gta-reversed ve plugin-sdk adları
  aktarıldı (`tools/ghidra/`). Kaldırmak için `claude mcp remove ReVa -s local`. Kullanıcı ortamındaki `JAVA_HOME` artık
  olmayan bir klasörü gösteriyor (JDK 21.0.12'ye güncellenmiş); ReVa kendi ayarıyla çalışıyor, istenirse `JAVA_HOME`
  `C:\Program Files\Java\latest\jdk-21` yapılabilir.
- Arkadaşın Wi-Fi'ı ~90 ms sıçramalar yapıyor olabilir (güç tasarrufu); kablo ya da Maksimum Performans.
