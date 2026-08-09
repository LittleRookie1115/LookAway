#pragma once

namespace lookaway::startup {

bool command_line_requests_autostart();
bool is_run_at_startup_enabled();
bool set_run_at_startup(bool enabled);
void synchronize_run_at_startup_path();

}  // namespace lookaway::startup
