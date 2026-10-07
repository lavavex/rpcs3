#pragma once

// Save data list and trophy notification for the PS5 frontend: RPCS3's native overlays
// (the Qt frontend's save_data_dialog and trophy_notification_helper without the Qt fallback)

#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"

class ps5_save_dialog final : public SaveDialogBase
{
public:
	s32 ShowSaveDataList(const std::string& base_dir, std::vector<SaveDataEntry>& save_entries, s32 focused, u32 op, vm::ptr<CellSaveDataListSet> listSet, bool enable_overlay) override;
};

class ps5_trophy_notification final : public TrophyNotificationBase
{
public:
	s32 ShowTrophyNotification(const SceNpTrophyDetails& trophy, const std::vector<uchar>& trophy_icon_buffer) override;
};
