# PocketSoundMixer kullanım kılavuzu

*English: [User guide](USER_GUIDE.md)*

PocketSoundMixer, bilgisayarındaki her ses türünün seviyesini ve tınısını ayrı ayrı ayarlamanı sağlar. Örneğin müziği bir kanala, oyunu başka bir kanala, sesli sohbeti üçüncü bir kanala koyup her birine kendi ses seviyesini ve ekolayzırını verebilirsin.

Bu kılavuz, uygulamanın Türkçe arayüzündeki adları kullanır. Dili **Ayarlar > Dil**'den değiştirebilirsin.

Uygulama başına kanallar için Windows 11 veya Windows 10 build 20348 ya da daha yenisi gerekir. macOS ve Linux'ta şimdilik yalnızca mikrofon kanalı çalışır.

## İlk açılış

Mikser altı kanalla açılır: **Music**, **Game**, **Film**, **Chat**, **Podcast** ve **Mic**. Her kanalın kendine uygun bir ekolayzır preseti vardır. Kanallar boş başlar, yani sen bir kanala eklemedikçe tüm uygulamaların eskisi gibi çalmaya devam eder.

Mic kanalı varsayılan mikrofonunu tutar ve sessizde başlar, böylece kendini hoparlörden duymazsın. Konuştuğunda göstergesi yine de hareket eder, böylece mikrofonun çalıştığını görürsün.

Bu altı kanala istediğin zaman dönmek için **Kanalları sıfırla**'ya tıklayıp onayla. Uygulamaların normale döner; ana çıkış, ses seviyesi, tema ve dil olduğu gibi kalır.

Pencereyi istediğin gibi boyutlandırabilirsin. Pencere, buton satırını ve bir kanalın tamamını gösteren bir minimum boyutta durur; pencere darken üst satır alt satırlara kayar.

## Ana satırın altındaki butonlar

| Buton | Ne yapar |
|---|---|
| **+ Kanal ekle** | Uygulamalar için bir kanal ekler. |
| **+ Mikrofon kanalı ekle** | Bir mikrofon için kanal ekler (önce sessizde). |
| **Kanalları sıfırla** | Tüm kanalları altı varsayılan kanalla değiştirir. |
| **Ayarlar** (sağda) | **Tema**: Koyu, Gece (turkuaz vurgulu, neredeyse siyah), Grafit (siyah ve gri), Mor (koyu mor vurgulu siyah) veya Açık. **Dil**: English (varsayılan) veya Türkçe; tüm mikser bir anda değişir. Seçimlerin hatırlanır. |
| **Yardım** (en sağda) | Pencerenin sağında, seçtiğin dilde kısa bir rehber açar. En üstteki **Kullanım kılavuzunu aç** bu kılavuzu tarayıcında, yine o dilde açar. Kapatmak için Yardım'a tekrar ya da x'e tıkla. |

## Ana satır (en üst)

| Kontrol | Ne yapar |
|---|---|
| Ana çıkış | Mikserin çaldığı yer. **Sistem varsayılanı** Windows'taki varsayılan cihazı izler, yani orada cihaz değiştirdiğinde o da değişir. Belirli bir kulaklık veya hoparlör de seçebilirsin. Seçim hatırlanır. |
| Ses | Tüm karışımı açar veya kısar. Windows'ta bu, ana çıkış cihazının kendi Windows ses düzeyidir; bu yüzden görev çubuğundaki sürgüyle hep aynıdır ve kulaklığının ses tuşlarına bastığında o da değişir. Cihaz Windows'ta sessize alınmışsa *(sessiz)* yazar. |
| Gösterge | Her şeyin birlikte ne kadar yüksek olduğunu gösterir. Üstteki çubuk sol, alttaki çubuk sağ taraftır. |
| Yedek çıkış (Windows) | Kanaldaki uygulamaların kendi sesinin taşındığı yer; böylece onları yalnızca mikserden duyarsın. Bkz. [Bir uygulamayı yalnızca bir kez duymak](#bir-uygulamayı-yalnızca-bir-kez-duymak). |

Kayıtlı cihaz çıkarılmışsa mikser onun yerine sistem varsayılanında çalar.

## Uygulamaları bir kanala koymak

1. Uygulamada bir şey çalmaya başla, örneğin Spotify'da bir şarkı.
2. İstediğin kanalda **+ Uygulama**'ya tıkla, örneğin Music.
3. Uygulamanın önündeki kutucuğu işaretle. Pencere açık kalır, böylece aynı kanala başka uygulamaları da işaretleyebilirsin. Bir uygulamayı tekrar çıkarmak için işaretini kaldır.

Kanallardaki uygulamalar hakkında bilmen gerekenler:

- Bir kanalda en fazla 8 uygulama olabilir. Sesleri birlikte karıştırılır, sonra kanalın ekolayzırından ve ses seviyesinden geçer.
- Bir uygulama yalnızca bir kanalda olabilir. Başka bir kanalda olan bir uygulamayı seçersen bu kanala taşınır. Liste her uygulamanın nerede olduğunu gösterir.
- Uygulama henüz çalmadığı için listede yoksa program adını yaz (örneğin `Spotify.exe`) ve **Ekle**'ye tıkla.
- Uygulama kapalıysa kanalda *başlaması bekleniyor* yazar. Uygulama açıldığında kendiliğinden bağlanır.
- Bir uygulamayı kanaldan çıkarmak için adının yanındaki küçük **x**'e tıkla. **Hepsini temizle** o kanaldaki her şeyi kaldırır.
- Hiçbir kanala koymadığın uygulamalar etkilenmez. Windows'un varsayılan çıkışında normal çalmaya devam ederler.

### Bir uygulamayı yalnızca bir kez duymak

Mikserin uygulamanın sesini yakalaması gerekir. Uygulama hoparlörlerinde de çalmaya devam etseydi onu iki kez duyardın. Bu yüzden mikser, uygulamanın kendi çıkışını dinlemediğin *yedek* bir cihaza taşır, örneğin monitör/HDMI sesi. Uygulamayı kanaldan çıkardığında veya mikseri kapattığında uygulama normale döner.

Bu cihazı üst satırda, ana çıkışın yanındaki **Yedek çıkış** ile seçersin:

- **Otomatik** (varsayılan) dinlemediğin ilk cihazı seçer. Liste hangisini seçtiğini gösterir.
- Hep aynı cihazı kullanmak için bir cihaz seç.
- **Kapalı** uygulamalara dokunmaz; doğrudan da çalarlar ve onları iki kez duyabilirsin.
- **Tüm uygulama çıkışlarını sıfırla** tüm uygulamaları normal çıkışlarına geri koyar.

+ Uygulama penceresi de uygulamanın kendi sesinin nereye gittiğini söyler.

- Yedek cihaz asla dinlediğin bir cihaz olmaz: ne ana çıkış, ne de bir kanalın çaldığı cihaz.
- Bilgisayarında tek bir çıkış cihazı varsa uygulamayı taşıyacak yer yoktur, bu yüzden onu iki kez duyarsın. İkinci bir çıkış bağla (hoparlörlü bir monitör veya USB kulaklık).
- Mikser çöktükten sonra bir uygulama sessiz kalırsa **Yedek çıkış**'ı aç ve **Tüm uygulama çıkışlarını sıfırla**'ya tıkla.

## Mikrofon kanalı

Mikrofonların kendi kanalı vardır, böylece uygulama kanallarında yalnızca uygulamalar olur. Mikrofon kanalının üstünde **Varsayılan mikrofon**'u, belirli bir cihazı veya **Yok**'u seç.

Mikrofon kanalı sessizde başlar. Konuştuğunda göstergesi soluk renkte de olsa hareket eder. Kendini duymak için sesi aç (**M** butonu), örneğin bir ekolayzır presetiyle nasıl duyulduğunu kontrol etmek için. Bunu yaparken kulaklık kullan, yoksa hoparlörler mikrofona geri besleme yapar.

### Mikrofon presetleri

Mikrofon kanallarının sesler için hazırlanmış kendi preset listesi vardır: **Flat**, **Clear Voice** (uğultuyu keser, netlik katar), **Warm Voice**, **Broadcast**, **Cut Rumble** (masa darbeleri, vınlama, trafik), **Less Boom** (mikrofona çok yakın oturuyorsan) ve **Less Hiss** (cızırtı). Uygulama kanalları müzik, oyun ve film presetlerini korur. Bir mikrofon kanalında kaydettiğin presetler yalnızca mikrofon kanallarında görünür.

İkinci bir mikrofon mu lazım? **+ Mikrofon kanalı ekle**'ye tıkla.

## Kanal kontrolleri

| Kontrol | Ne yapar |
|---|---|
| Ad | Kanalı yeniden adlandırmak için tıkla. |
| Çıkış | Bu kanalın çaldığı yer. **Otomatik (Ana)** ana çıkışı izler. Yalnızca bu kanalı oraya göndermek için bir cihaz seç, örneğin sohbeti kulaklığa, müziği hoparlöre. Cihaz çıkarılmış olsa bile seçimin kalır; cihaz geri gelene kadar kanal ana çıkışta çalar. Ana çıkışın yanında aynı anda en fazla 3 cihaz kullanılabilir. |
| **x** (sağ üstte) | Kanalı kaldırır. |
| Ses sürgüsü | %0 ile %100 arası. Ses yüksekliğini kulağın eşit duyacağı adımlarla değiştirir: %50 belirgin şekilde daha alçak, %10 ise zar zor duyulur. |
| Gösterge | Yeşil normal, sarı yüksek, kırmızı sesin sınırda olduğu anlamına gelir; bir şeyi kıs. İnce çizgi son tepe değerini kısa bir süre gösterir. Sessizdeki bir kanalda gösterge soluktur ama yine de hareket eder. |
| Denge | Sesi sola veya sağa kaydırır. Ortaya yakın sürüklediğinde ortaya yapışır. |
| **M** | Sessiz. |
| **S** | Solo. Herhangi bir kanal solodayken yalnızca solo kanallar çalar. |

## Ekolayzır

10 sürgü farklı frekansları değiştirir; solda derin bastan (31 Hz) sağda en yüksek tize (16 kHz) kadar.

- Yukarı o aralığı yükseltir, aşağı alçaltır, orta değiştirmez. Tam değeri görmek için farenin imlecini bir sürgünün üstünde tut.
- Sürgülerin üstündeki küçük eğri genel şekli gösterir.
- Tüm değişiklikleri geri almak için **Flat** presetini seç.

Birkaç hızlı tarif:

| Amaç | Dene |
|---|---|
| Müzikte daha fazla vuruş | 63 ve 125'i biraz yükselt. |
| Sohbet veya podcastlerde daha net sesler | 2k ve 4k'yı yükselt, 63 ve 125'i alçalt. |
| Oyunlarda ayak seslerini duymak | 2k ile 4k arasını yükselt, 125 ile 250 arasını alçalt. |
| Daha az sert ses | 4k ve 8k'yı alçalt. |

## Presetler

- Her kanaldaki listeden bir preset seç. Adın sonundaki `*` o zamandan beri sürgüleri değiştirdiğin anlamına gelir.
- **Kaydet** mevcut sürgüleri bir preset olarak saklar. Yeni bir ad ver, ya da güncellemek için kendi presetlerinden birinin adını kullan.
- Kaydet'in yanındaki **Sil**, seçili preseti onayladıktan sonra siler. Hazır presetler de silinebilir, **Flat** hariç. Silinen preseti kullanan kanallar mevcut seslerini korur.
- Uygulama kanallarının ve mikrofon kanallarının listeleri ayrıdır (bkz. [Mikrofon presetleri](#mikrofon-presetleri)).
- Bir hazır preseti sildikten sonra preset listesinin en altında **Hazır presetleri geri getir** belirir ve hepsini geri getirir. Hazır presetler düzenlenemez.

Preset adları (Flat, Music, Vocal...) ve varsayılan kanal adları senin verindir, bu yüzden Türkçe arayüzde de değişmez. Kanalları istediğin gibi yeniden adlandırabilirsin.

## Ayarların kaydedildiği yer

Kanallar, uygulamaları, ses seviyeleri, ana çıkış, tema ve dil mikseri kapattığında kaydedilir ve bir sonraki açılışta geri gelir. Presetler, birini kaydettiğin veya sildiğin anda kaydedilir.

| Sistem | Klasör |
|---|---|
| Windows | `%APPDATA%\PocketSoundMixer` |
| macOS | `~/Library/Application Support/PocketSoundMixer` |
| Linux | `~/.config/PocketSoundMixer` |

Sıfırdan başlamak için mikseri kapat ve o klasördeki `session.json` dosyasını sil.

## Sorun giderme

| Sorun | Ne yapmalı |
|---|---|
| Bir uygulamayı iki kez duyuyorum | Yedek çıkış cihazı yok. Bkz. [Bir uygulamayı yalnızca bir kez duymak](#bir-uygulamayı-yalnızca-bir-kez-duymak). |
| Mikser çöktükten sonra bir uygulama sessiz | **Yedek çıkış**'ı aç ve **Tüm uygulama çıkışlarını sıfırla**'ya tıkla. |
| Uygulama + Uygulama listesinde yok | Önce onda bir şey çal ya da program adını yaz. |
| Hiçbir şey çalmıyor | Ana çıkışı ve sesi kontrol et, sonra kanalın sessizde olmadığını ve başka bir kanalın soloda olmadığını kontrol et. |
| Gösterge kırmızı | O kanalı kıs ya da yükselttiğin ekolayzır sürgülerini alçalt. |
| Bazı hata mesajları İngilizce | Ses motorundan gelen hata mesajları şimdilik çevrilmiyor. |
