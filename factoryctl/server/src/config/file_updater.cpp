#include "config/file_updater.hpp"

#include <algorithm>
#include <chrono>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/dynamic_config/updates_sink/find.hpp>
#include <userver/fs/read.hpp>
#include <userver/logging/log.hpp>
#include <userver/utils/text_light.hpp>
#include <userver/yaml_config/merge_schemas.hpp>
#include <vector>

#include "config/dynamic.hpp"

namespace factoryctl::config {

namespace {

auto knownNames(const userver::components::ComponentContext& context)
    -> std::unordered_set<std::string> {
  return context.FindComponent<userver::components::DynamicConfig>()
      .GetDefaultDocsMap()
      .GetNames();
}

auto listed(std::vector<std::string> names) -> std::string {
  std::ranges::sort(names);
  return names.empty() ? "none, the defaults"
                       : userver::utils::text::Join(names, ", ");
}

}  // namespace

FileUpdater::FileUpdater(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : ComponentBase(config, context),
      sink_(userver::dynamic_config::FindUpdatesSink(config, context)),
      fs_(context.GetTaskProcessor(
          config["fs-task-processor"].As<std::string>("fs-task-processor"))),
      path_(config["path"].As<std::string>()),
      known_(knownNames(context)) {
  // At once: everything that reads the config waits for the first values.
  update();
  using userver::utils::PeriodicTask;
  task_.Start(std::string{kName},
              PeriodicTask::Settings{
                  config["update-period"].As<std::chrono::milliseconds>(
                      std::chrono::seconds{5})},
              [this] { update(); });
}

FileUpdater::~FileUpdater() { task_.Stop(); }

auto FileUpdater::update() -> void {
  std::string text;
  try {
    if (userver::fs::FileExists(fs_, path_)) {
      text = userver::fs::ReadFileContents(fs_, path_);
    }
  } catch (const std::exception& e) {
    // Being saved, most likely: the next round reads it again.
    LOG_WARNING() << "dynamic config: cannot read " << path_ << ": "
                  << e.what();
    return;
  }
  if (last_ == text) return;
  last_ = text;

  try {
    auto docs = parseDocs(text);
    const auto unknown = unknownNames(docs, known_);
    for (const auto& name : unknown) {
      if (const auto key = keyOfField(name)) {
        LOG_WARNING() << "dynamic config: " << path_ << ": " << name
                      << " is a field of " << *key << ", ignored at the top: "
                      << "write it under `" << *key << ":`, indented";
      } else {
        LOG_WARNING() << "dynamic config: " << path_ << ": no key " << name
                      << " in this server, ignored";
      }
    }
    std::vector<std::string> taken;
    for (const auto& name : docs.GetNames()) {
      if (!std::ranges::contains(unknown, name)) taken.push_back(name);
    }
    sink_.SetConfig(kName, std::move(docs));
    loaded_ = true;
    LOG_INFO() << "dynamic config from " << path_ << ": "
               << listed(std::move(taken));
  } catch (const std::exception& e) {
    if (loaded_) {
      LOG_ERROR() << "dynamic config: " << path_
                  << " refused, the values before it stay: " << e.what();
      return;
    }
    LOG_ERROR() << "dynamic config: " << path_
                << " refused, the defaults are used: " << e.what();
    sink_.SetConfig(kName, userver::dynamic_config::DocsMap{});
    loaded_ = true;
  }
}

auto FileUpdater::GetStaticConfigSchema() -> userver::yaml_config::Schema {
  return userver::yaml_config::MergeSchemas<ComponentBase>(R"(
type: object
description: the dynamic config from a YAML file
additionalProperties: false
properties:
    path: {type: string, description: the YAML file of keys}
    update-period: {type: string, description: how often the file is checked}
    fs-task-processor: {type: string, description: where the file is read}
)");
}

}  // namespace factoryctl::config
