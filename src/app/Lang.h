#pragma once

#include <cstdint>
#include <string>

namespace psm {

// UI languages. English is the default; the saved code ("en", "tr") picks one at startup.
enum class Language : std::uint8_t { English, Turkish, Count };

// Every UI string, as X(id, English, Turkish). Strings used as printf formats keep the same
// specifiers in the same order in both languages. "###id" keeps a window's ID when its title changes.
#define PSM_STRINGS(X) \
    X(MasterOutput, "Master output", "Ana çıkış") \
    X(SystemDefault, "System default", "Sistem varsayılanı") \
    X(MasterOutputTip, "Speakers or headphones the mixer plays on", "Mikserin çaldığı hoparlör veya kulaklık") \
    X(VolumeFmt, "Volume %.0f%%", "Ses %.0f%%") \
    X(VolumeMutedFmt, "Volume %.0f%% (muted)", "Ses %.0f%% (sessiz)") \
    X(MasterVolumeSyncTip, \
      "The Windows volume of this device: the taskbar slider and your headset buttons move it too.", \
      "Bu cihazın Windows ses düzeyi: görev çubuğundaki sürgü ve kulaklık tuşların da onu değiştirir.") \
    X(MasterVolumeMutedTip, "Muted in Windows. Unmute it there or with your headset.", \
      "Windows'ta sessize alınmış. Oradan ya da kulaklığından sesi aç.") \
    X(SpareOutput, "Spare output", "Yedek çıkış") \
    X(SpareOutputTip, \
      "Apps in a channel have their own sound moved to this device, so you\n" \
      "hear them only once, through the mixer. Pick a device you don't listen to,\n" \
      "e.g. monitor/HDMI audio. Apps go back to normal when removed from a channel.", \
      "Kanaldaki uygulamaların kendi sesi bu cihaza taşınır, böylece onları\n" \
      "yalnızca bir kez, mikserden duyarsın. Dinlemediğin bir cihaz seç,\n" \
      "örneğin monitör/HDMI sesi. Kanaldan çıkarılan uygulamalar normale döner.") \
    X(SpareOff, "Off (apps also play directly)", "Kapalı (uygulamalar doğrudan da çalar)") \
    X(SpareNone, "None available (apps also play directly)", "Uygun cihaz yok (uygulamalar doğrudan da çalar)") \
    X(Automatic, "Automatic", "Otomatik") \
    X(SpareNoDevice, \
      "No spare device: every output is one you listen on. Connect a second output " \
      "(a monitor with audio, a USB headset) or install the free VB-Cable driver.", \
      "Yedek cihaz yok: tüm çıkışları dinliyorsun. İkinci bir çıkış bağla " \
      "(sesli bir monitör, USB kulaklık) ya da ücretsiz VB-Cable sürücüsünü kur.") \
    X(ResetAppOutputs, "Reset all app outputs", "Tüm uygulama çıkışlarını sıfırla") \
    X(ResetAppOutputsTip, "Puts every app back on your normal output, like Windows' own Reset button.", \
      "Windows'un kendi Sıfırla düğmesi gibi, tüm uygulamaları normal çıkışına geri koyar.") \
    X(AddChannel, "+ Add channel", "+ Kanal ekle") \
    X(AddChannelTip, "A new channel for apps", "Uygulamalar için yeni bir kanal") \
    X(AddMicChannel, "+ Add mic channel", "+ Mikrofon kanalı ekle") \
    X(NewChannelName, "Channel %d", "Kanal %d") \
    X(NewMicName, "Mic", "Mikrofon") \
    X(ResetChannels, "Reset channels", "Kanalları sıfırla") \
    X(ResetConfirm, "Replace all channels with the six default ones?", "Tüm kanallar altı varsayılan kanalla değiştirilsin mi?") \
    X(ResetDetail, "Music, Game, Film, Chat, Podcast and Mic. Your apps go back to normal.", \
      "Music, Game, Film, Chat, Podcast ve Mic. Uygulamaların normale döner.") \
    X(Reset, "Reset", "Sıfırla") \
    X(Cancel, "Cancel", "İptal") \
    X(Settings, "Settings", "Ayarlar") \
    X(Help, "Help", "Yardım") \
    X(HelpTitle, "Help###help", "Yardım###help") \
    X(Appearance, "Appearance", "Görünüm") \
    X(ThemeLabel, "Theme", "Tema") \
    X(LanguageLabel, "Language", "Dil") \
    X(SystemSection, "Windows", "Windows") \
    X(TrayOption, "Keep running in the system tray when closed", "Kapatınca sistem tepsisinde çalışmaya devam et") \
    X(TrayOptionTip, \
      "Closing the window hides it; the mix keeps playing. Click the tray icon to open it, right-click it to quit.", \
      "Pencereyi kapatmak onu gizler; ses çalmaya devam eder. Açmak için tepsi simgesine tıkla, çıkmak için sağ tıkla.") \
    X(AutostartOption, "Start with Windows", "Windows açılınca başlat") \
    X(AutostartOptionTip, "Starts in the tray when you sign in to Windows, so your channels are ready.", \
      "Windows'a giriş yaptığında tepside başlar, böylece kanalların hazır olur.") \
    X(TrayOpen, "Open PocketSoundMixer", "PocketSoundMixer'ı aç") \
    X(TrayQuit, "Quit", "Çıkış") \
    X(ThemeDark, "Dark", "Koyu") \
    X(ThemeMidnight, "Midnight", "Gece") \
    X(ThemeGraphite, "Graphite", "Grafit") \
    X(ThemeViolet, "Violet", "Mor") \
    X(ThemeLight, "Light", "Açık") \
    X(NoChannels, "No channels. Click \"+ Add channel\".", "Kanal yok. \"+ Kanal ekle\"ye tıkla.") \
    X(ChannelLimitFmt, "Channel limit reached (%d)", "Kanal sınırına ulaşıldı (%d)") \
    X(ChannelFullFmt, "This channel is full (%d sources)", "Bu kanal dolu (%d kaynak)") \
    X(RemoveChannelFmt, "Remove \"%s\"?", "\"%s\" kaldırılsın mı?") \
    X(Remove, "Remove", "Kaldır") \
    X(VolumeTipFmt, "Volume %.0f%%", "Ses %.0f%%") \
    X(PanCenter, "Center", "Orta") \
    X(PanLeftFmt, "Left %.2f", "Sol %.2f") \
    X(PanRightFmt, "Right %.2f", "Sağ %.2f") \
    X(MuteTip, "Mute", "Sessiz") \
    X(SoloTip, "Solo: only soloed channels play", "Solo: yalnızca solo kanallar çalar") \
    X(RemoveFromChannelTip, "Remove from this channel", "Bu kanaldan çıkar") \
    X(AppOnlyViaMixer, "(only via mixer)", "(yalnızca mikserden)") \
    X(AppPlaying, "(playing)", "(çalıyor)") \
    X(AppWaiting, "(waiting for it to start)", "(başlaması bekleniyor)") \
    X(NoAppsYet, "No apps yet. Click \"+ App\" to add one or more.", "Henüz uygulama yok. Eklemek için \"+ Uygulama\"ya tıkla.") \
    X(AppsWindowsOnly, "Putting apps in channels works on Windows only for now.", \
      "Uygulamaları kanallara koymak şimdilik yalnızca Windows'ta çalışıyor.") \
    X(AddApp, "+ App", "+ Uygulama") \
    X(ClearAll, "Clear all", "Hepsini temizle") \
    X(AppsPlayingNow, "Apps playing sound now (pick as many as you like):", "Şu an ses çalan uygulamalar (istediğin kadar seç):") \
    X(InThisChannel, "  (in this channel)", "  (bu kanalda)") \
    X(InOtherChannelFmt, "  (in %s, moves here)", "  (%s kanalında, buraya taşınır)") \
    X(NoAppsNow, "None right now. Start playing something, or type the exe name:", \
      "Şu an yok. Bir şey çalmaya başla veya exe adını yaz:") \
    X(ExeHint, "e.g. Spotify.exe", "örn. Spotify.exe") \
    X(Add, "Add", "Ekle") \
    X(HearTwiceWarning, "The app's own sound also keeps playing, so you may hear it twice. Set \"Spare output\" at the top.", \
      "Uygulamanın kendi sesi de çalmaya devam eder, iki kez duyabilirsin. Üstteki \"Yedek çıkış\"ı ayarla.") \
    X(MovedToFmt, \
      "The app's own sound is moved to \"%s\", so you hear it only through the mixer. Change it with \"Spare output\" at the top.", \
      "Uygulamanın kendi sesi \"%s\" cihazına taşınır, böylece yalnızca mikserden duyarsın. Üstteki \"Yedek çıkış\"tan değiştir.") \
    X(Microphone, "Microphone", "Mikrofon") \
    X(DefaultMicrophone, "Default microphone", "Varsayılan mikrofon") \
    X(None, "None", "Yok") \
    X(NoMicrophones, "No microphones found", "Mikrofon bulunamadı") \
    X(MicMutedHint, "Muted: the bar shows your mic works. Unmute (M) to hear yourself; use headphones.", \
      "Sessizde: çubuk mikrofonun çalıştığını gösterir. Kendini duymak için sesi aç (M); kulaklık kullan.") \
    X(MicLiveHint, "You hear yourself now. Use headphones, or mute (M) to stop the echo.", \
      "Şu an kendini duyuyorsun. Kulaklık kullan ya da yankıyı durdurmak için sessize al (M).") \
    X(Output, "Output", "Çıkış") \
    X(AutomaticMaster, "Automatic (Master)", "Otomatik (Ana)") \
    X(NotConnected, " (not connected)", " (bağlı değil)") \
    X(ChannelOutputTip, "Speakers or headphones this channel plays on", "Bu kanalın çaldığı hoparlör veya kulaklık") \
    X(Save, "Save", "Kaydet") \
    X(Delete, "Delete", "Sil") \
    X(RestoreBuiltIns, "Restore built-in presets", "Hazır presetleri geri getir") \
    X(RestoreBuiltInsTip, "Brings back the built-in presets you deleted", "Sildiğin hazır presetleri geri getirir") \
    X(DeletePresetTip, "Delete this preset", "Bu preseti sil") \
    X(FlatNotDeletable, "Flat can't be deleted", "Flat silinemez") \
    X(PickPresetToDelete, "Pick a preset to delete it", "Silmek için bir preset seç") \
    X(DeletePresetFmt, "Delete the preset \"%s\"?", "\"%s\" preseti silinsin mi?") \
    X(DeletePresetDetail, "Channels using it keep their current sound.", "Onu kullanan kanallar mevcut seslerini korur.") \
    X(SavePresetPrompt, "Save this EQ as a preset:", "Bu EQ'yu preset olarak kaydet:") \
    X(BandTipFmt, "%s Hz: %+.1f dB  (%s)", "%s Hz: %+.1f dB  (%s)") \
    X(Louder, "louder", "daha yüksek") \
    X(Quieter, "quieter", "daha alçak") \
    X(Unchanged, "unchanged", "değişmez") \
    /* Help */ \
    X(OpenGuide, "Open the full user guide", "Kullanım kılavuzunu aç") \
    X(OpenGuideTip, "Opens the user guide in your browser, in this language", "Kullanım kılavuzunu tarayıcında, bu dilde açar") \
    X(GuideUrl, "https://github.com/enesozdin/PocketSoundMixer/blob/main/docs/USER_GUIDE.md", \
      "https://github.com/enesozdin/PocketSoundMixer/blob/main/docs/USER_GUIDE.tr.md") \
    X(HelpMasterHead, "Master (top row)", "Ana (üst satır)") \
    X(HelpMasterOutput, "Master output: where you listen. \"System default\" follows Windows; or pick your headphones or speakers.", \
      "Ana çıkış: dinlediğin yer. \"Sistem varsayılanı\" Windows'u izler; ya da kulaklığını veya hoparlörünü seç.") \
    X(HelpMasterVolume, \
      "Volume: the whole mix, 0-100%. On Windows it is the device's own Windows volume, so the taskbar slider " \
      "and headset buttons move it too. The bar next to it shows how loud everything is together.", \
      "Ses: tüm karışım, %0-100. Windows'ta cihazın kendi Windows ses düzeyidir, yani görev çubuğundaki sürgü " \
      "ve kulaklık tuşları da onu değiştirir. Yanındaki çubuk her şeyin birlikte ne kadar yüksek olduğunu gösterir.") \
    X(HelpButtonsHead, "Buttons under it", "Altındaki butonlar") \
    X(HelpAddChannel, "+ Add channel: a new channel for apps.", "+ Kanal ekle: uygulamalar için yeni bir kanal.") \
    X(HelpAddMic, "+ Add mic channel: a channel for a microphone.", "+ Mikrofon kanalı ekle: bir mikrofon için kanal.") \
    X(HelpReset, "Reset channels: back to Music, Game, Film, Chat, Podcast and Mic.", \
      "Kanalları sıfırla: Music, Game, Film, Chat, Podcast ve Mic'e geri döner.") \
    X(HelpSettings, \
      "Settings (right): theme (Dark, Midnight, Graphite, Violet or Light), language (English or Türkçe), " \
      "and on Windows: keep running in the system tray, and start with Windows. Both are on at first.", \
      "Ayarlar (sağda): tema (Koyu, Gece, Grafit, Mor veya Açık), dil (English veya Türkçe) " \
      "ve Windows'ta: sistem tepsisinde çalışmaya devam et ve Windows açılınca başlat. İkisi de başta açık.") \
    X(HelpHelp, "Help (far right): this guide.", "Yardım (en sağda): bu rehber.") \
    X(HelpAppsHead, "App channels", "Uygulama kanalları") \
    X(HelpApps1, "\"+ App\" puts apps in a channel. Pick as many as you like, e.g. Spotify and a browser in Music.", \
      "\"+ Uygulama\" uygulamaları kanala koyar. İstediğin kadar seç, örneğin Music'e Spotify ve bir tarayıcı.") \
    X(HelpApps2, "An app can be in one channel at a time. Picking it in another channel moves it there.", \
      "Bir uygulama aynı anda tek kanalda olabilir. Başka bir kanalda seçmek onu oraya taşır.") \
    X(HelpApps3, "A closed app shows \"waiting for it to start\" and joins by itself when it starts.", \
      "Kapalı bir uygulama \"başlaması bekleniyor\" gösterir ve başlayınca kendiliğinden bağlanır.") \
    X(HelpApps4, "The small x next to an app takes it out of the channel. Clear all empties the channel.", \
      "Uygulamanın yanındaki küçük x onu kanaldan çıkarır. Hepsini temizle kanalı boşaltır.") \
    X(HelpApps5, "Apps you never put in a channel play normally, as if the mixer wasn't there.", \
      "Hiçbir kanala koymadığın uygulamalar, mikser yokmuş gibi normal çalar.") \
    X(HelpApps6, \
      "Spare output (top row): apps in a channel have their own sound moved to this device, so you hear them " \
      "only once, through the mixer. Pick a device you don't listen to, e.g. monitor/HDMI audio. " \
      "\"Off\" leaves apps alone (you may hear them twice). \"Reset all app outputs\" puts every app back to normal.", \
      "Yedek çıkış (üst satır): kanaldaki uygulamaların kendi sesi bu cihaza taşınır, böylece onları yalnızca " \
      "bir kez, mikserden duyarsın. Dinlemediğin bir cihaz seç, örneğin monitör/HDMI sesi. " \
      "\"Kapalı\" uygulamalara dokunmaz (iki kez duyabilirsin). \"Tüm uygulama çıkışlarını sıfırla\" hepsini normale döndürür.") \
    X(HelpMicHead, "Mic channel", "Mikrofon kanalı") \
    X(HelpMic1, "Choose your microphone in the list at the top of the channel.", "Mikrofonunu kanalın üstündeki listeden seç.") \
    X(HelpMic2, "It starts muted, so you don't hear yourself. The bar still moves when you talk, so you can see it works.", \
      "Kendini duymaman için sessizde başlar. Konuşunca çubuk yine de hareket eder, böylece çalıştığını görürsün.") \
    X(HelpMic3, "Unmute (M) to hear yourself, e.g. to check how you sound. Use headphones, or the speakers echo.", \
      "Kendini duymak için sesi aç (M), örneğin nasıl duyulduğunu kontrol etmek için. Kulaklık kullan, yoksa hoparlör yankı yapar.") \
    X(HelpChannelHead, "Every channel", "Her kanal") \
    X(HelpChannel1, "Name: click it to rename. The x in the corner removes the channel.", \
      "Ad: yeniden adlandırmak için tıkla. Köşedeki x kanalı kaldırır.") \
    X(HelpChannel2, \
      "Output: where this channel plays. \"Automatic (Master)\" follows the Master output. " \
      "Pick a device to send just this channel there, e.g. chat to a headset and music to speakers. " \
      "Your pick stays, even if the device is unplugged for a while.", \
      "Çıkış: bu kanalın çaldığı yer. \"Otomatik (Ana)\" ana çıkışı izler. " \
      "Sadece bu kanalı oraya göndermek için bir cihaz seç, örneğin sohbeti kulaklığa, müziği hoparlöre. " \
      "Cihaz bir süre çıkarılsa bile seçimin kalır.") \
    X(HelpChannel3, \
      "Volume: 0-100%. The meter beside it is green when normal, yellow when loud, and red at the limit. " \
      "The thin line shows the latest peak.", \
      "Ses: %0-100. Yanındaki gösterge normalde yeşil, yüksekte sarı, sınırda kırmızıdır. " \
      "İnce çizgi son tepe değerini gösterir.") \
    X(HelpChannel4, "Balance: left or right; it snaps to the center.", "Denge: sol veya sağ; ortaya yapışır.") \
    X(HelpChannel5, "M mutes the channel. S (solo) plays only the soloed channels.", \
      "M kanalı sessize alır. S (solo) yalnızca solo kanalları çalar.") \
    X(HelpEqHead, "Equalizer", "Ekolayzır") \
    X(HelpEq1, "10 sliders from deep bass (31 Hz, left) to treble (16k, right). Up is louder, down is quieter, the middle is unchanged.", \
      "Derin bastan (31 Hz, solda) tize (16k, sağda) 10 sürgü. Yukarı daha yüksek, aşağı daha alçak, orta değişmez.") \
    X(HelpEq2, "The curve above the sliders shows the overall shape.", "Sürgülerin üstündeki eğri genel şekli gösterir.") \
    X(HelpEq3, "Pick a preset from the list. A * means you changed it. Save stores your own; pick Flat to undo every change.", \
      "Listeden bir preset seç. * değiştirdiğin anlamına gelir. Kaydet kendi presetini saklar; tüm değişiklikleri geri almak için Flat'i seç.") \
    X(HelpEq4, \
      "Delete removes the chosen preset, built-in ones too (except Flat). Channels using it keep their sound. " \
      "\"Restore built-in presets\" at the bottom of the preset list brings deleted built-ins back.", \
      "Sil seçili preseti kaldırır, hazır olanları da (Flat hariç). Onu kullanan kanallar seslerini korur. " \
      "Preset listesinin en altındaki \"Hazır presetleri geri getir\" silinen hazır presetleri geri getirir.")

enum class S : std::uint16_t {
#define PSM_STRING_ID(id, en, tr) id,
    PSM_STRINGS(PSM_STRING_ID)
#undef PSM_STRING_ID
    Count
};

namespace detail {
extern Language g_language;
extern const char* const kStrings[static_cast<int>(Language::Count)][static_cast<int>(S::Count)];
} // namespace detail

// One array lookup: cheap enough to call every frame.
inline const char* tr(S id)
{
    return detail::kStrings[static_cast<int>(detail::g_language)][static_cast<int>(id)];
}

void setLanguage(Language language);
Language currentLanguage();
const char* languageCode(Language language);       // "en", "tr": what the session saves
Language languageFromCode(const std::string& code); // unknown codes give English
const char* languageName(Language language);       // in that language: "English", "Türkçe"

} // namespace psm
