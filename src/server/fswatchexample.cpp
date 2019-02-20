#include "Headers.hpp"

void cb(const std::vector<fsw::event> &events, void *context) {
  cout << "GOT EVENT: " << events.size() << endl;
  for (const auto &it : events) {
    cout << it.get_path() << " " << it.get_time() << " (";
    for (const fsw_event_flag &it2 : it.get_flags()) {
      cout << fsw_get_event_flag_name(it2) << ", ";
    }
    cout << ")" << endl;
  }
}

int mmain(int argc, char **argv) {
  // Create the default platform monitor
  fsw::monitor *active_monitor = fsw::monitor_factory::create_monitor(
      fsw_monitor_type::system_default_monitor_type, {"/usr/local"}, cb);

  // Configure the monitor
  // active_monitor->set_properties(monitor_properties);
  active_monitor->set_allow_overflow(true);
  // active_monitor->set_latency(latency);
  active_monitor->set_recursive(true);
  // active_monitor->set_directory_only(directory_only);
  // active_monitor->set_event_type_filters(event_filters);
  // active_monitor->set_filters(filters);
  active_monitor->set_follow_symlinks(true);
  // active_monitor->set_watch_access(watch_access);

  // Start the monitor
  active_monitor->start();
  return 0;
}
