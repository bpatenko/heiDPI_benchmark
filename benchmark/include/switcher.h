#pragma once
#include <atomic>
#include "scenario.h"

void startSwitcher(const ScenarioFile& config, std::atomic<bool>& running);
