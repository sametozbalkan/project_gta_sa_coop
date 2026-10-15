# Yapılacaklar

Durum (2026-10-15): host (oveRange) ve arkadaşı ilk co-op oturumunu Steam tüneliyle oynadı, çalışıyor.
Proje GitHub'da: github.com/sametozbalkan/project_gta_sa_coop. Bu oturum projenin mimarı; işler `gtasa-coder`
(Opus 5.5 medium) ve `gtasa-coder-high` (Opus 5.5 high) ajanlarına gidiyor (`CLAUDE.md` > "Working model").

## 1. dalga (2026-10-15'te başladı)
| İş | Ajan | Durum |
|---|---|---|
| Alt+Tab'da sunucudan düşmeyi düzelt | gtasa-coder-high | birleştirildi (1f159e8) |
| Tagging Up Turf: arkadaşın sıktığı tag sayılmıyor | gtasa-coder-high | birleştirildi (fff678f) |
| Radyo senkronu (aynı araçta aynı istasyon) | gtasa-coder-high | birleştirildi (98fff3d) |
| Görev derleme hattı, görev uyarlama rehberi ve görev envanteri | gtasa-coder-high | çalışıyor |
| Tünel yayın betiği, yeni repo düzeni için paketleme, pakete yama arşivi seçeneği | gtasa-coder | birleştirildi (35db590) |
| Discord Rich Presence'ı kapat, dış bağlantıları denetle | gtasa-coder | birleştirildi (294dc4f) |
| Gizlilik kalıntıları: çökme raporu yüklemesi, ImGui linkleri, eski Discord kaydı, sunucu `bind` ayarı | gtasa-coder | birleştirildi (2322d12) |

Dalga bitince: birleştir, derle, oyuna kur, arkadaş için yeni paket; sonra ikiniz oyunda test edersiniz.

## Sonraki dalgalar (öncelik sırasıyla)
- **Bağımsız proje (yeniden adlandırma), isim bekleniyor.** Kullanıcı projenin CoopAndreas'tan ayrı, kendi adıyla
  bir proje olmasını istiyor (isim sonra, yaratıcı bir şey). Plan: oyuncunun gördüğü her yerde yeni ad (oyundaki
  sürüm yazısı ve mesajlar, DLL/klasör/ini adları, kısayollar, kurulum, README), kendi sürüm numaramız, upstream
  otomatik takip edilmez (işe yarayan bir şey gerekirse elle alınır). İç kod adları kalır. Lisans gereği kalanlar:
  GPL-3.0, LICENSE, kaynak dosyalarındaki orijinal telif satırları ve README'de tek satırlık teşekkür. Kurulum
  programı eski adları yenilerine taşır, kayıtlar etkilenmez. İsim gelince görev grupları arasında tek seferde yapılır.
- Görevleri hikâye sırasıyla, **gruplar halinde** uyarlamak (rehber çıkınca). Her grup bir bölümün birkaç
  görevi; görevler ajanlarda paralel uyarlanır, grup tek pakette gelir. Host ve arkadaşı test etmeyi dert etmiyor,
  grupları aralıksız deneyecekler; onlar bir grubu oynarken sıradaki grup hazırlanır, buldukları hatalar sonraki
  pakete girer.
  - Görev sırası bozulmaz (kullanıcının kuralı): açılma sırası, başlatma tetikleri ve ilerleme bayraklarına
    dokunulmaz; bir uyarlama sadece kendi görevinin içini değiştirir.
  - Kayıtlar paketten pakete çalışmaya devam etmeli: global değişken eklenmez, MAIN'e dokunulmaz; derleme hattı
    bunu her pakette denetler.
  95 hikâye görevinden yalnızca ilk 4'ü uyarlanmış; asıl darboğaz bu.
- Upstream PR #106 (para, aranma seviyesi, hileler): **alınmadı** (2026-10-15 incelemesi). Oyuna sonradan katılan
  oyuncu diğerinin parasını sıfırlıyor, aynı anda kazanılan para kayboluyor, hileler (intihar, arabaları patlatma)
  iki oyunda birden çalışıyor. Yerine:
  - **2. dalga, çalışıyor:** görevlerin aranma komutları (yıldız ver, temizle, üst sınır) host'tan iki oyuncuya
    uygulanır; suçlar herkesin kendine kalır. San Fierro/Las Venturas kilidi açılınca arkadaşın oyunu da açık sayar
    (şu an orada arkadaşa 4 yıldız veriyor). Sürüm 0.3.101.
  - Para: herkesin cüzdanı ayrı kalır; uyarlanmış görevlerin ödülü zaten ikinize de ödeniyor.
  - Hileler: herkesin kendi oyununda kalır.
- Upstream README'de yapılmamış diğerleri, hikâyede gerektikleri sırayla: para, nefes çubuğu, max HP,
  araç kornası/sireni (NPC), yangın, pickup'lar, aranma seviyesi, çete savaşları, paraşüt, trailer, hidrolik.

## Oyunda görülen eksikler (ayrıntı)
- **Radyo senkronu yok.** Aynı araçtaki oyuncular farklı istasyon duyuyor. Kodda sadece tuş durumundaki
  `m_bRadioTrackSkip` taşınıyor (`client/src/CUtil.cpp`); README'de "radio sync" yapılmamış.
- **Görevde arkadaşın sıktığı sprey sayılmıyor** (Tagging Up Turf, `scm/scripts/SWEET1.txt`): arkadaşın
  sıktığı tag görünüyor ama host'un görev sayacına yansımadı, host da üstüne sıkmak zorunda kaldı.
  Bakılacak yerler: `client/src/Hooks/WorldHooks.cpp` (`CTagManager__SetAlpha_Hook`, 0x565C5E),
  `client/src/game_sa/CTagManager.*` (`CTagSync`), `shared/network/packets/world.h` (`TagUpdate`,
  `UpdateAllTags`), görevin tag'leri nasıl saydığı (SWEET1.txt) ve `COMMAND_SET_TAG_STATUS_IN_AREA` opcode senkronu.

## Bilinen sınırlamalar
- Alt+Tab'da ~30 sn sonra sunucudan düşme (oyun arka planda ENet'i işlemiyor). 1. dalgada.
- Eski (Steam) kayıtlar yüklenemez; her zaman New Game.

## Bakım
- Pakete doğrulanmış `.tools/patches/Patches_v2.zip`'i koyma seçeneği (yazarın sunucusu kapanırsa diye). 1. dalgada.
- Arkadaşın Wi-Fi'ı ~90 ms sıçramalar yapıyor olabilir (güç tasarrufu); kablo ya da Maksimum Performans.
