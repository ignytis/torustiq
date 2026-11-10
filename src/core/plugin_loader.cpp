#include "core/plugin_loader.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace torustiq {
namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr const char* kLibraryExtension = ".dll";
void* OpenLibrary(const std::string& path) {
    return reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
}
void* FindSymbol(void* handle, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
}
void CloseLibrary(void* handle) {
    FreeLibrary(static_cast<HMODULE>(handle));
}
std::string LastError() {
    return "error code " + std::to_string(GetLastError());
}
#else
#ifdef __APPLE__
constexpr const char* kLibraryExtension = ".dylib";
#else
constexpr const char* kLibraryExtension = ".so";
#endif
void* OpenLibrary(const std::string& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void* FindSymbol(void* handle, const char* name) {
    return dlsym(handle, name);
}
void CloseLibrary(void* handle) {
    dlclose(handle);
}
std::string LastError() {
    const char* error = dlerror();
    return error != nullptr ? error : "unknown error";
}
#endif

template <typename Fn> Fn Symbol(void* handle, const char* name) {
    return reinterpret_cast<Fn>(FindSymbol(handle, name));
}

// Reads the plugin ID of an open library; empty string (and a warning) if it is not valid.
std::string ReadId(void* handle, const std::string& path) {
    auto get_info = Symbol<torustiq_plugin_get_info_fn>(handle, TORUSTIQ_PLUGIN_GET_INFO_SYMBOL);
    if (get_info == nullptr) {
        std::cerr << "warning: " << path << ": no " << TORUSTIQ_PLUGIN_GET_INFO_SYMBOL
                  << ", skipping\n";
        return {};
    }
    const torustiq_plugin_info* info = get_info();
    if (info == nullptr || info->id == nullptr || info->id[0] == '\0') {
        std::cerr << "warning: " << path << ": invalid plugin info, skipping\n";
        return {};
    }
    if (info->abi_version != TORUSTIQ_SDK_ABI_VERSION) {
        std::cerr << "warning: " << path << ": ABI version " << info->abi_version
                  << " does not match " << TORUSTIQ_SDK_ABI_VERSION << ", skipping\n";
        return {};
    }
    return info->id;
}

} // namespace

PluginLoader::~PluginLoader() {
    // Unload in reverse order of loading.
    for (auto it = load_order_.rbegin(); it != load_order_.rend(); ++it) {
        const Loaded& plugin = loaded_[*it];
        if (plugin.shutdown != nullptr) {
            plugin.shutdown();
        }
        CloseLibrary(plugin.handle);
    }
}

void PluginLoader::Scan(const std::string& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return;
    }
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        // is_regular_file follows symlinks, so symlinked libraries are found.
        if (entry.is_regular_file(ec) && entry.path().extension() == kLibraryExtension) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end()); // deterministic duplicate handling

    for (const fs::path& file : files) {
        const std::string path = file.string();
        void* handle = OpenLibrary(path);
        if (handle == nullptr) {
            std::cerr << "warning: cannot open " << path << ": " << LastError() << "\n";
            continue;
        }
        const std::string id = ReadId(handle, path);
        CloseLibrary(handle); // not initialised: only loaded again if the pipeline needs it
        if (id.empty()) {
            continue;
        }
        if (!available_.emplace(id, path).second) {
            std::cerr << "warning: " << path << ": plugin '" << id << "' is already provided by "
                      << available_[id] << ", skipping\n";
        }
    }
}

bool PluginLoader::HasPlugin(const std::string& id) const {
    return available_.count(id) != 0;
}

bool PluginLoader::IsLoaded(const std::string& id) const {
    return loaded_.count(id) != 0;
}

void PluginLoader::Load(const std::string& id, PluginRegistry& registry) {
    if (IsLoaded(id)) {
        return;
    }
    auto found = available_.find(id);
    if (found == available_.end()) {
        throw std::runtime_error("plugin '" + id + "' not found");
    }
    const std::string& path = found->second;
    void* handle = OpenLibrary(path);
    if (handle == nullptr) {
        throw std::runtime_error("cannot open " + path + ": " + LastError());
    }
    auto init = Symbol<torustiq_plugin_init_fn>(handle, TORUSTIQ_PLUGIN_INIT_SYMBOL);
    if (ReadId(handle, path) != id || init == nullptr) {
        CloseLibrary(handle);
        throw std::runtime_error("plugin '" + id + "' (" + path + ") is invalid: it must export " +
                                 TORUSTIQ_PLUGIN_INIT_SYMBOL);
    }
    const torustiq_registrar registrar = registry.registrar();
    if (init(&registrar) != TORUSTIQ_OK) {
        CloseLibrary(handle);
        throw std::runtime_error("initialisation of plugin '" + id + "' failed");
    }
    Loaded plugin;
    plugin.handle = handle;
    plugin.shutdown = Symbol<torustiq_plugin_shutdown_fn>(handle, TORUSTIQ_PLUGIN_SHUTDOWN_SYMBOL);
    loaded_.emplace(id, plugin);
    load_order_.push_back(id);
}

void LoadReferencedPlugins(const PipelineConfig& config,
                           PluginLoader& loader,
                           PluginRegistry& registry) {
    std::set<std::string> ids;
    auto collect = [&](const std::vector<ComponentConfig>& components, auto&& is_known) {
        for (const ComponentConfig& component : components) {
            if (!is_known(component.type) && loader.HasPlugin(component.type)) {
                ids.insert(component.type);
            }
        }
    };
    collect(config.inputs, [&](const std::string& t) { return registry.FindInput(t) != nullptr; });
    collect(config.transforms,
            [&](const std::string& t) { return registry.FindTransform(t) != nullptr; });
    collect(config.outputs,
            [&](const std::string& t) { return registry.FindOutput(t) != nullptr; });
    for (const std::string& id : ids) {
        loader.Load(id, registry);
    }
}

} // namespace torustiq
