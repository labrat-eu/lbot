#include <labrat/robot/manager.hpp>

namespace labrat::robot {

std::unique_ptr<Manager> Manager::instance;

Manager::Manager() {}

Manager::~Manager() {
  node_map.clear();
}

Manager &Manager::get() {
  if (!instance) {
    instance = std::unique_ptr<Manager>(new Manager());
  }

  return *instance;
}

void Manager::addPlugin(const Plugin &plugin) {
  plugin_list.emplace_back(plugin);
}

}  // namespace labrat::robot
