# 1. dalga oyun testi

Sürüm: **CoopAndreas 0.3.100-alpha** (oyunda sol alt köşede yazar, iki tarafta aynı olmalı).

## Hazırlık
- **Host:** yeni derleme oyuna kuruldu, bir şey yapman gerekmiyor. Sunucu artık sadece bu bilgisayardan bağlanılabilir
  (`server-config.ini`: `bind = 127.0.0.1`); tünel ve senin oyunun zaten 127.0.0.1 kullanıyor.
- **Arkadaş:** yeni `CoopAndreas-Kurulum.zip`'i bir klasöre çıkarıp `Kur.bat`'ı çalıştırır. Oyun tekrar
  indirilmez, sadece mod dosyaları yenilenir.
- Her zaman **New Game**; eski kayıtlar yüklenmez.
- Bir sorun çıkarsa: `C:\Games\GTASA_Coop\CoopAndreas_crashes\` içindeki dosyaları ve, takılma varsa,
  "CoopAndreas (tani)" kısayoluyla açılan oyunun `CoopAndreas_diag.log` dosyasını gönder.

## 1. Tagging Up Turf (görev 3)
1. Görevi başlatın. Grove'daki iki tag'den birini arkadaş sonuna kadar sıkar.
   - Beklenen: tag iki ekranda da tamam görünür, "tags sprayed" mesajı çıkar, o tag'in yeşil işareti kaybolur.
2. Diğerini host sıkar; görev "Get back in Sweet's car" ile devam eder. Host'un arkadaşın tag'ine tekrar sıkması gerekmez.
3. Idlewood'da birer tag, çatıdaki tag'i arkadaş bitirir. Görev geçmeli.
4. İkiniz aynı tag'e birlikte sıkın: iki ekranda da tamam kalmalı.

## 2. Radyo
1. Host sürer, arkadaş yolcu biner: arkadaş aynı istasyonu duyar.
2. Host istasyon değiştirir: arkadaş ~1,5 sn sonra aynı istasyona geçer. "Radio Off" anında kapanır.
3. Arkadaş fare tekeriyle istasyon değiştiremez (bilerek; istasyonu sürücü seçer).
4. Arkadaş inip uzaklaşır: radyosu susar, host'un radyosunu yürürken duymaz.
5. Roller değişince (arkadaş sürer, host yolcu) aynısı.
6. İkiniz ayrı arabalarda: herkes sadece kendi radyosunu duyar. Polis arabasında ikiniz de telsizi duyarsınız.

## 3. Alt+Tab
1. Arkadaş 2 dakika Alt+Tab yapar, host bu arada gezer ve sohbete yazar.
   - Beklenen: kimse düşmez. Arkadaş dönünce (açılan duraklatma menüsünü kapatır) host'u bulunduğu yerde görür,
     sohbet mesajı gelmiş olur.
2. Aynısı 10 dakika, bir de pencere küçültülmüşken.
3. Host görevdeyken 2 dakika Alt+Tab yapar: görev host dönene kadar bekler, sonra ikinizde devam eder.
   - Bilinen: Alt+Tab'daki oyuncu bir tuşa basılı bırakmışsa, diğer ekranda o yöne yürümeye devam eder; dönünce düzelir.

## 4. Gizlilik
- Discord açıkken oynayın: Discord profilinizde CoopAndreas etkinliği (can, para, bölge satırları) görünmemeli.
  Discord'un kendi "GTA San Andreas oynuyor" yazısı görünebilir; o bizden değil.
