# Yapılacaklar

Durum (2026-10-15): host (oveRange) ve arkadaşı ilk co-op oturumunu Steam tüneliyle oynadı, çalışıyor.
Proje GitHub'da: github.com/sametozbalkan/project_gta_sa_coop. Bu oturum projenin mimarı; işler `gtasa-coder`
(Opus 5.5 medium) ve `gtasa-coder-high` (Opus 5.5 high) ajanlarına gidiyor (`CLAUDE.md` > "Working model").

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

## 2. dalga (çalışıyor)
| İş | Ajan | Durum |
|---|---|---|
| Görevlerin aranma komutları host'tan iki oyuncuya; San Fierro/Las Venturas kilidi arkadaşa da (sürüm 0.3.101) | gtasa-coder-high | birleştirildi (59d055e) |
| Görev grubu 1: #6 Drive-Thru, #7 Nines And AK's, #8 Drive-By, #9 Sweet's Girl, #10 Cesar Vialpando | gtasa-coder / -high | çalışıyor |

## Görev grupları (plan)
Görevler hikâye sırasıyla, gruplar halinde uyarlanıyor; her grup tek pakette gelir. **Bir grup bitince paket size
gelir, siz test edip onay verene kadar sıradaki grup başlamaz** (kullanıcının kuralı); hata düzeltmeleri ve görev
dışı işler bu arada sürebilir. Hiçbir görev atlanmaz: engele takılan görevde grup durur, engel çözülünce devam
edilir; tek kişilik mini oyunlarda ikinci oyuncu izler. Kurallar: görev sırası bozulmaz (açılma sırası, tetikler, ilerleme bayraklarına dokunulmaz) ve kayıtlar
paketten pakete açılmaya devam eder (derleme hattı her pakette denetler). Liste ve riskler: `docs/missions.md`.
- Los Santos: grup 1 #6–#10 (Sweet), grup 2 #11–#17 (Smoke, Ryder), grup 3 #18–#22 (müzik ve Cesar),
  grup 4 #23–#28 (Crash, Doberman, Los Sepulcros, Reuniting The Families, The Green Sabre).
- Sonra Badlands (#29–#39), San Fierro (#40–#65), çöl (#66–#74), Las Venturas (#75–#92), Los Santos'a dönüş (#93–#103).
- #1–#5 upstream'de uyarlanmış; sizin testiniz bekleniyor.

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
   - İlk 10 görev (#1–#10, elle yazı/işaret satırlarıyla uyarlanmış): katman, görevin zaten gösterdiğini tekrar
     göstermeyecek şekilde tasarlanır (aynı varlığın işareti, aynı yazı atlanır; olmazsa görev başına "yansıtma"
     kapatma anahtarı). Bu görevler düzenlenmez, yeniden test gerekmez; ileride birinde hata düzeltilirken yeni kalıba
     geçirilir. `docs/missions.md`'de hangi görevin hangi kalıpla yapıldığı yazılır. Tam tutarlılık istenirse #1–#10
     ayrı bir New Game'de yeniden test edilerek sadeleştirilebilir.
2. **Dünya durumu kaydı:** host'un dünyasının kurallarını belirleyen şeyler (şehir kilidi, harita nesne grupları ve köprü
   bariyerleri, çete bölgeleri, en yüksek aranma seviyesi, garajlar vb.) tek bir yerde tanımlanır; değiştikçe ve biri
   oyuna katıldığında otomatik gönderilir. Şehir kilidi bu kalıpla yapıldı; farkları tek tek kovalamak yerine genel yapı.
   Köprü bariyerleri işi bunun içinde yapılır.

## Sonraki işler (öncelik sırasıyla)
- **Sağlamlık:** geçersiz oyuncu tutamacı verilen co-op komutları host'u çökertiyor (ör. arkadaş görev ortasında
  düşerse); görevi kimin başlatabileceğini denetleyen host kontrolü (`CanPlayerStartMission`) upstream'de yorum satırı.
- **Görev boyutu sınırı:** Reuniting The Families (#27) 69.000 baytlık sınıra 561 bayt uzakta; #27'den önce ya görev
  içinde kısaltma ya da sınırı yükselten istemci yaması.
- **Köprü bariyerleri (The Green Sabre #28'den önce):** San Fierro/Las Venturas köprülerindeki bariyerler bir harita
  nesne grubu; host'un görevleri kaldırınca arkadaşın oyununda fiziksel olarak duruyor. Çözüm: ilgili IPL komutları
  (0776/0777/0778) uyarlanmış görevlerde senkron + oyuna sonradan katılana anlık durum. Aynı pakete: sonradan
  katılanın en yüksek aranma seviyesi (Verdant Meadows 6'ya çıkarıyor) ve çete bölgesi gücü.
- **Görevlerin isteyeceği istemci özellikleri** (sırası gelince): yangın (#23), çete savaşları (#25, #98), römork (#31),
  RC araçlar (#44–#46), paraşüt (#72, #81, #89), jetpack (#73). Tek kişilik mini oyunlarda (#10 lowrider, #18 dans,
  #43 sürücü okulu, #70 uçuş okulu) ikinci oyuncu izler.
- **Bağımsız proje (yeniden adlandırma), isim bekleniyor.** Oyuncunun gördüğü her yerde yeni ad (oyundaki sürüm
  yazısı ve mesajlar, DLL/klasör/ini adları, kısayollar, kurulum, README), kendi sürüm numaramız, upstream otomatik
  takip edilmez. İç kod adları kalır. Lisans gereği kalanlar: GPL-3.0, LICENSE, kaynak dosyalarındaki orijinal telif
  satırları ve README'de tek satırlık teşekkür. Kurulum eski adları taşır, kayıtlar etkilenmez. İsim gelince grup arasında.
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

## Bakım
- Arkadaşın Wi-Fi'ı ~90 ms sıçramalar yapıyor olabilir (güç tasarrufu); kablo ya da Maksimum Performans.
