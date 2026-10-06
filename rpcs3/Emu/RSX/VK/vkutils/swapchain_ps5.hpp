#pragma once

#include "swapchain_core.h"
#include "util/logs.hpp"

#include <vector>

namespace vk
{
#if defined(__PROSPERO__)
	// The PS5 has no window system. PS5_Vulkan's RADV exposes VideoOut as a VK_KHR_display
	// display, so presentation goes through an ordinary WSI swapchain on a display plane surface.
	// The native (CPU blit) path is unused.
	using swapchain_NATIVE = native_swapchain_base;

	// Display plane surface: the same steps as PS5_VulkanTemplate's createDirect2DisplaySurface,
	// which runs on the console.
	[[maybe_unused]] static
	VkSurfaceKHR make_WSI_surface(VkInstance vk_instance, VkPhysicalDevice gpu, display_handle_t window_handle, WSI_config* config)
	{
		const auto& target = std::get<ps5_display_t>(window_handle);

		u32 display_count = 0;
		vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, nullptr);
		std::vector<VkDisplayPropertiesKHR> displays(display_count);
		vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, displays.data());

		u32 plane_count = 0;
		vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, nullptr);
		std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
		vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, planes.data());

		ensure(display_count && plane_count, "PS5: no VK_KHR_display display or plane");

		// Prefer the requested size; otherwise take the largest mode of the first display
		VkDisplayKHR display = VK_NULL_HANDLE;
		VkDisplayModeKHR mode = VK_NULL_HANDLE;
		VkExtent2D extent{};

		for (const auto& props : displays)
		{
			u32 mode_count = 0;
			vkGetDisplayModePropertiesKHR(gpu, props.display, &mode_count, nullptr);
			std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
			vkGetDisplayModePropertiesKHR(gpu, props.display, &mode_count, modes.data());

			for (const auto& m : modes)
			{
				const auto& region = m.parameters.visibleRegion;
				rsx_log.notice("PS5 display mode: %ux%u @ %u mHz", region.width, region.height, m.parameters.refreshRate);

				const bool exact = region.width == target.width && region.height == target.height;
				const bool larger = u64{region.width} * region.height > u64{extent.width} * extent.height;

				if (exact || (display == VK_NULL_HANDLE) || (larger && !(extent.width == target.width && extent.height == target.height)))
				{
					display = props.display;
					mode = m.displayMode;
					extent = region;
				}
			}

			if (extent.width == target.width && extent.height == target.height)
			{
				break;
			}
		}

		ensure(mode != VK_NULL_HANDLE, "PS5: no display mode");

		// A plane that can show this display
		u32 plane_index = umax;
		for (u32 i = 0; i < plane_count && plane_index == umax; i++)
		{
			u32 count = 0;
			vkGetDisplayPlaneSupportedDisplaysKHR(gpu, i, &count, nullptr);
			std::vector<VkDisplayKHR> supported(count);
			vkGetDisplayPlaneSupportedDisplaysKHR(gpu, i, &count, supported.data());

			for (VkDisplayKHR d : supported)
			{
				if (d == display)
				{
					plane_index = i;
					break;
				}
			}
		}

		ensure(plane_index != umax, "PS5: no display plane for the display");

		VkDisplayPlaneCapabilitiesKHR caps{};
		vkGetDisplayPlaneCapabilitiesKHR(gpu, mode, plane_index, &caps);

		VkDisplayPlaneAlphaFlagBitsKHR alpha = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
		if (!(caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR))
		{
			for (auto bit : { VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR, VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR, VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR })
			{
				if (caps.supportedAlpha & bit)
				{
					alpha = bit;
					break;
				}
			}
		}

		VkDisplaySurfaceCreateInfoKHR info{};
		info.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
		info.displayMode = mode;
		info.planeIndex = plane_index;
		info.planeStackIndex = planes[plane_index].currentStackIndex;
		info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		info.globalAlpha = 1.0f;
		info.alphaMode = alpha;
		info.imageExtent = extent;

		rsx_log.notice("PS5 display surface: %ux%u, plane %u", extent.width, extent.height, plane_index);

		// VideoOut never reports window-manager resizes
		config->supports_automatic_wm_reports = false;

		VkSurfaceKHR result = VK_NULL_HANDLE;
		CHECK_RESULT(vkCreateDisplayPlaneSurfaceKHR(vk_instance, &info, nullptr, &result));
		return ensure(result, "Failed to initialize Vulkan display surface");
	}
#endif
}
