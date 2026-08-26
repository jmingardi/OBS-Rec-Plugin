/*
OBS Replay Timeline
Copyright (C) 2026 jmingardi

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "plugin-controller.hpp"

#include <cstdio>
#include <memory>
#include <new>

#include <obs-module.h>

#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
std::unique_ptr<replay_timeline::PluginController> plugin;
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("ReplayTimeline.ModuleDescription");
}

bool obs_module_load(void)
{
	unsigned int obsMajor = 0;
	unsigned int obsMinor = 0;
	const char *obsVersion = obs_get_version_string();
	if (!obsVersion || std::sscanf(obsVersion, "%u.%u", &obsMajor, &obsMinor) != 2 || obsMajor < 32 ||
	    (obsMajor == 32 && obsMinor < 2)) {
		obs_log(LOG_ERROR, "OBS Studio 32.2 or newer is required (detected %s)",
			obsVersion ? obsVersion : "unknown");
		return false;
	}

	try {
		plugin = std::make_unique<replay_timeline::PluginController>();
		if (!plugin->start()) {
			plugin.reset();
			obs_log(LOG_ERROR, "failed to initialize the Replay Timeline dock");
			return false;
		}
	} catch (const std::bad_alloc &) {
		plugin.reset();
		obs_log(LOG_ERROR, "failed to initialize: out of memory");
		return false;
	} catch (...) {
		plugin.reset();
		obs_log(LOG_ERROR, "failed to initialize due to an unexpected exception");
		return false;
	}

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	if (plugin) {
		plugin->stop();
		plugin.reset();
	}

	obs_log(LOG_INFO, "plugin unloaded");
}
