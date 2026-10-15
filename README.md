# GTA San Andreas co-op (özel derleme)

GTA San Andreas hikâyesini bir arkadaşla iki kişi oynamak için [CoopAndreas](https://github.com/Tornamic/CoopAndreas)'ın
özel bir derlemesi ve onu kuran araçlar. Oyun dosyaları, sürüm düşürme yamaları ve derleme çıktıları repoda yok.

- `CoopAndreas/` — CoopAndreas (GPL-3.0), `git subtree` ile alındı. Bizim değişikliklerimiz üstünde ayrı commit'ler:
  Discord seri anahtarı yok, çökme raporu isteğe bağlı, kare sınırlayıcı hep açık, `--coopdiag` tanı günlüğü.
- `tunnel/` — CoopTunnel: CGNAT arkasından oynamak için oyunun UDP trafiğini Steam relay'i (AppID 480) üzerinden taşır.
- `installer/` — arkadaşın kurulumu: Steam'deki GTA SA'yı kopyalar, 1.0 US'e düşürür, modu ve tüneli kurar.
- `scripts/` — derleme, oyuna kurma ve paketleme. `tools/` — hata ayıklama yardımcıları.
- `CLAUDE.md` — geliştirme notları ve kurallar. `YAPILACAKLAR.md` — iş listesi.

Derlemek için Visual Studio 2026 (MSVC x86) ve `.tools/` altında xmake 3.1.1 (portable) gerekir; ayrıntılar `CLAUDE.md`'de.

Grup 1 (#6–#10) iki oyunculu test bekliyor: [test listesi](docs/test-grup-1.md).
Arkadaş paketini PowerShell 7 ile hazırlama: `pwsh -File scripts/package.ps1 -TestPlan docs/test-grup-1.md`.
Paket test listesini, kaynak commit'ini ve dosya SHA-256'larını içerir; arşivin yanında `.zip.sha256` bulunur.

Lisans: GPL-3.0 (`LICENSE`).
