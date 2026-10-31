#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <userver/components/component_base.hpp>
#include <userver/dynamic_config/updates_sink/component.hpp>
#include <userver/engine/task/task_processor_fwd.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/yaml_config/schema.hpp>

namespace factoryctl::config {

// The dynamic config from a YAML file (config::parseDocs), read at start and
// again when its text changes. A file userver refuses (bad YAML, a bad value)
// changes nothing: the values before it stay, the log says why; at start they
// are the defaults. No file is no values: the defaults.
class FileUpdater final : public userver::components::ComponentBase {
 public:
  static constexpr std::string_view kName = "dynamic-config-file-updater";

  FileUpdater(const userver::components::ComponentConfig& config,
              const userver::components::ComponentContext& context);
  ~FileUpdater() override;

  static auto GetStaticConfigSchema() -> userver::yaml_config::Schema;

 private:
  auto update() -> void;

  userver::components::DynamicConfigUpdatesSinkBase& sink_;
  userver::engine::TaskProcessor& fs_;
  const std::string path_;
  const std::unordered_set<std::string> known_;
  // The text last tried, good or not: a bad one is reported once.
  std::optional<std::string> last_;
  bool loaded_ = false;
  userver::utils::PeriodicTask task_;
};

}  // namespace factoryctl::config
