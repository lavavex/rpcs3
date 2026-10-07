#include "stdafx.h"
#include "ps5_dialogs.h"

#include "Emu/IdManager.h"
#include "Emu/Cell/Modules/cellSysutil.h"
#include "Emu/RSX/Overlays/overlay_manager.h"
#include "Emu/RSX/Overlays/overlay_save_dialog.h"
#include "Emu/RSX/Overlays/overlay_trophy_notification.h"

LOG_CHANNEL(cellSaveData);

s32 ps5_save_dialog::ShowSaveDataList(const std::string& base_dir, std::vector<SaveDataEntry>& save_entries, s32 focused, u32 op, vm::ptr<CellSaveDataListSet> listSet, bool enable_overlay)
{
	cellSaveData.notice("ShowSaveDataList(save_entries=%d, focused=%d, op=0x%x, listSet=*0x%x, enable_overlay=%d)", save_entries.size(), focused, op, listSet, enable_overlay);

	const bool use_end = sysutil_send_system_cmd(CELL_SYSUTIL_DRAWING_BEGIN, 0) >= 0;

	if (!use_end)
	{
		cellSaveData.error("ShowSaveDataList(): Not able to notify DRAWING_BEGIN callback because one has already been sent!");
	}

	s32 result = -2;

	if (auto manager = g_fxo->try_get<rsx::overlays::display_manager>())
	{
		result = manager->create<rsx::overlays::save_dialog>()->show(base_dir, save_entries, focused, op, listSet, enable_overlay);

		if (result == rsx::overlays::user_interface::selection_code::error)
		{
			cellSaveData.error("ShowSaveDataList: Native UI dialog returned error");
			result = -2;
		}
	}
	else
	{
		cellSaveData.error("ShowSaveDataList: no overlay manager");
	}

	if (use_end)
	{
		sysutil_send_system_cmd(CELL_SYSUTIL_DRAWING_END, 0);
	}

	return result;
}

s32 ps5_trophy_notification::ShowTrophyNotification(const SceNpTrophyDetails& trophy, const std::vector<uchar>& trophy_icon_buffer)
{
	if (auto manager = g_fxo->try_get<rsx::overlays::display_manager>())
	{
		// More than one notification may be queued; the notification class schedules them
		auto popup = std::make_shared<rsx::overlays::trophy_notification>();
		return manager->add(popup, false)->show(trophy, trophy_icon_buffer);
	}

	return 0;
}
