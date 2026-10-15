# Yapılacaklar

Durum (2026-10-15): host (oveRange) ve arkadaşı ilk co-op oturumunu Steam tüneliyle oynadı, çalışıyor.
Proje GitHub'da: github.com/sametozbalkan/project_gta_sa_coop. Bu oturum projenin mimarı; işler `gtasa-coder`
(Opus 5.5 medium) ve `gtasa-coder-high` (Opus 5.5 high) ajanlarına gidiyor (`CLAUDE.md` > "Working model").

## 1. dalga
| İş | Ajan | Durum |
|---|---|---|
| Alt+Tab'da sunucudan düşmeyi düzelt | gtasa-coder-high | planlandı |
| Tagging Up Turf: arkadaşın sıktığı tag sayılmıyor | gtasa-coder-high | planlandı |
| Radyo senkronu (aynı araçta aynı istasyon) | gtasa-coder-high | planlandı |
| Görev derleme hattı, görev uyarlama rehberi ve görev envanteri | gtasa-coder-high | planlandı |
| Tünel yayın betiği, pakete yama arşivini koyma seçeneği | gtasa-coder | planlandı |

Dalga bitince: birleştir, derle, oyuna kur, arkadaş için yeni paket; sonra ikiniz oyunda test edersiniz.

## Sonraki dalgalar (öncelik sırasıyla)
- Görevleri hikâye sırasıyla uyarlamak (rehber çıkınca; görev başına bir iş, her biri oyunda test ister).
  95 hikâye görevinden yalnızca ilk 4'ü uyarlanmış; asıl darboğaz bu.
- Upstream PR #106 (para, aranma seviyesi, hileler; açık, 11 dosya): önce salt-okunur kontrol, iyiyse fork'a al.
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
