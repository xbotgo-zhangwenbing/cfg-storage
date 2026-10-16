#pragma once

#include <obs-data.h>
#include <memory>

namespace xbotgo {
struct ObsDataDeleter {
	void operator()(obs_data_t *data) const { obs_data_release(data); }
};

// 跨线程传输只移动所有权，不与业务线程共享可修改的 obs_data_t。
using ObsData = std::unique_ptr<obs_data_t, ObsDataDeleter>;
} // namespace xbotgo
