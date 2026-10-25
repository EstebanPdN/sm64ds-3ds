#pragma once
#include <atomic>
namespace enhance {
struct Settings {
 std::atomic<unsigned> physics_mode{2},controller_mode{0},move_sensitivity{100};
 bool developer_dumps=false;
};
inline Settings& live(){static Settings settings;return settings;}
}
