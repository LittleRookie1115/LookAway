#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "resource.h"

namespace lookaway::app {

inline constexpr wchar_t kMainClass[] = L"LookAwayMainWindow";
inline constexpr wchar_t kReminderClass[] = L"LookAwayReminderWindow";
inline constexpr wchar_t kSettingsClass[] = L"LookAwaySettingsWindow";
inline constexpr wchar_t kStatisticsClass[] = L"LookAwayStatisticsWindow";
inline constexpr wchar_t kCollectionClass[] = L"LookAwayCollectionWindow";
inline constexpr wchar_t kMutexName[] = L"Local\\LookAway.SingleInstance.1";
inline constexpr wchar_t kRegistryPath[] = L"Software\\LookAway";
inline constexpr wchar_t kUsageRegistryValue[] = L"UsageHistory";
inline constexpr wchar_t kRewardStateValueName[] = L"RewardCollection";
inline constexpr wchar_t kRunKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValueName[] = L"LookAway";
inline constexpr wchar_t kAutostartArg[] = L"--autostart";

inline constexpr UINT kTrayMessage = WM_APP + 1;
inline constexpr UINT kShowExisting = WM_APP + 2;
inline constexpr UINT_PTR kTickTimer = 1;
inline constexpr UINT kTrayId = 1;
inline constexpr UINT_PTR kAnimationTimer = 2;
inline constexpr UINT kMenuOpen = 1001;
inline constexpr UINT kMenuPause = 1002;
inline constexpr UINT kMenuReset = 1003;
inline constexpr UINT kMenuStatistics = 1004;
inline constexpr UINT kMenuSettings = 1005;
inline constexpr UINT kMenuAutostart = 1006;
inline constexpr UINT kMenuExit = 1007;
inline constexpr UINT kMenuCollection = 1008;

inline constexpr int kMinWorkMinutes = 5;
inline constexpr int kMaxWorkMinutes = 60;
inline constexpr int kWorkMinuteStep = 5;
inline constexpr int kMinRestMinutes = 1;
inline constexpr int kMaxRestMinutes = 20;

inline constexpr COLORREF kBackground = RGB(246, 247, 244);
inline constexpr COLORREF kSurface = RGB(255, 255, 255);
inline constexpr COLORREF kInk = RGB(31, 38, 35);
inline constexpr COLORREF kMuted = RGB(100, 108, 103);
inline constexpr COLORREF kLine = RGB(222, 226, 221);
inline constexpr COLORREF kTrack = RGB(224, 229, 225);
inline constexpr COLORREF kGreen = RGB(38, 132, 91);
inline constexpr COLORREF kGreenDark = RGB(24, 91, 64);
inline constexpr COLORREF kGreenSoft = RGB(225, 241, 233);
inline constexpr COLORREF kAmber = RGB(177, 111, 25);
inline constexpr COLORREF kAmberSoft = RGB(250, 237, 216);
inline constexpr COLORREF kRestBlue = RGB(55, 104, 154);
inline constexpr COLORREF kRestSoft = RGB(226, 237, 248);
inline constexpr ULONGLONG kFileTimeTicksPerDay = 864000000000ULL;
inline constexpr ULONGLONG kFileTimeTicksPerHour = 36000000000ULL;
inline constexpr std::size_t kCardCount = 15;
inline constexpr std::uint32_t kRewardStateMagic = 0x4C415243;  // "LARC"
inline constexpr std::uint32_t kLegacyRewardStateVersion = 1;
inline constexpr std::uint32_t kRewardStateVersion = 2;

struct CardDefinition {
    const wchar_t* character;
    const wchar_t* comment;
    int resource_id;
};

inline constexpr std::array<CardDefinition, kCardCount> kCards{{
    {L"哲&铃", L"这是…恐怖片海报吗？！主角竟然有点像我和哥哥…", IDR_CARD_01},
    {L"蕾米埃尔", L"等等...蕾米，我能看出你对自画像投注了更多心血，但似乎大可不必…", IDR_CARD_02},
    {L"安比", L"这是安比在扮演——热狗…以骸…还是热以骸狗？！", IDR_CARD_03},
    {L"妮可", L"这、这好可爱！蕾米，你可以保持这个创作风格吗？", IDR_CARD_04},
    {L"诺姆", L"这是…『震惊！玛瑟尔集团首席技术专家竟以邦布为食』的造谣帖配图吗？", IDR_CARD_05},
    {L"维琳娜", L"蕾米…小邦布们看到这张画会做噩梦吧...", IDR_CARD_06},
    {L"仪玄", L"我隐约觉得，师父应该不是会在敲敲里用荷花头像，签名写『上善若水』的类型…吧？", IDR_CARD_07},
    {L"叶瞬光", L"虚狩儿童绘本吗？竟有种说不出的...和谐？", IDR_CARD_08},
    {L"星见雅", L"本来想说整张图里只有蜜瓜们没在冒犯雅小姐，但如果它们还长了腿…我就有点说不好了。", IDR_CARD_09},
    {L"艾莲", L"咦…竟然非常可爱？蕾米考虑转行成为动物画手吗？不不不，我不是说胳膊和腿的那部分——", IDR_CARD_10},
    {L"希希芙", L"希希芙到底偷吃了多少鸡蛋啊——", IDR_CARD_11},
    {L"柏妮思", L"♪3-2-1...Fire! ♪Burnice, Burnice, Burnice, Burnice, Burnice, Burnice, Go Go!♪", IDR_CARD_12},
    {L"挽昼", L"你…也听到那些邦布们的傻话了？『挽昼女士！妈妈！』之类的…？", IDR_CARD_13},
    {L"希格莉德", L"谁敢直视这位——勇武非凡、高大挺拔的空巡局第五代总务次官？", IDR_CARD_14},
    {L"比利", L"蕾米…看到这里，我才发现你的艺术也有真正的知己。比利一定会喜欢的…", IDR_CARD_15},
}};

#pragma pack(push, 1)
struct RewardStateDisk {
    std::uint32_t magic;
    std::uint32_t version;
    std::int64_t day_index;
    std::uint32_t daily_completed_cycles;
    std::uint8_t daily_reward_granted;
    std::uint32_t draw_tickets;
    std::uint32_t total_draws;
    std::uint16_t card_counts[kCardCount];
};
#pragma pack(pop)

}  // namespace lookaway::app
