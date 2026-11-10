#ifndef TORUSTIQ_CORE_PLUGIN_LOADER_H_
#define TORUSTIQ_CORE_PLUGIN_LOADER_H_

#include <map>
#include <string>
#include <vector>

#include "core/pipeline_config.h"
#include "core/plugin_registry.h"
#include "torustiq/sdk.h"

namespace torustiq {

// Loads plugins from shared libraries, lazily.
//
// Scan() opens every library once, only to read its torustiq_plugin_info (ID and ABI version),
// and closes it again. Load() opens the library for real and calls torustiq_plugin_init exactly
// once, so only plugins that the pipeline refers to are initialised. Step initialisation is the
// `create` function of the registered vtable and is called by Pipeline for each step.
//
// The loader must outlive every Pipeline and PluginRegistry that uses loaded plugins: their
// vtables point into the libraries, which are unloaded (after torustiq_plugin_shutdown) by the
// destructor.
class PluginLoader {
  public:
    PluginLoader() = default;
    ~PluginLoader();
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    // Reads info of all libraries in `dir` (non-recursive). A missing directory is not an
    // error. Libraries that cannot be read, have a wrong ABI version or duplicate an ID are
    // skipped with a warning on stderr.
    void Scan(const std::string& dir);

    // True if a scanned library provides this plugin ID.
    bool HasPlugin(const std::string& id) const;

    // Initialises the plugin once and registers its components. Later calls for the same ID do
    // nothing. Throws std::runtime_error for an unknown ID or a failure.
    void Load(const std::string& id, PluginRegistry& registry);

    bool IsLoaded(const std::string& id) const;

  private:
    struct Loaded {
        void* handle = nullptr;
        torustiq_plugin_shutdown_fn shutdown = nullptr;
    };

    std::map<std::string, std::string> available_; // plugin ID -> library path
    std::map<std::string, Loaded> loaded_;
    std::vector<std::string> load_order_;
};

// Loads plugins for every component type of `config` that is not already in `registry`
// (built-in plugins win). Types unknown to the loader are left for Pipeline to report.
void LoadReferencedPlugins(const PipelineConfig& config,
                           PluginLoader& loader,
                           PluginRegistry& registry);

} // namespace torustiq

#endif // TORUSTIQ_CORE_PLUGIN_LOADER_H_
