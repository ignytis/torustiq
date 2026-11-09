#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include <CLI/CLI.hpp>

#include "core/pipeline.h"
#include "core/pipeline_config.h"
#include "core/plugin_registry.h"
#include "plugins/builtin_plugins.h"

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void HandleSignal(int) {
    g_stop_requested = 1;
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"Torustiq: a data pipeline runner"};
    std::string pipeline_path;
    app.add_option("pipeline", pipeline_path, "Path to the YAML pipeline definition")
        ->required()
        ->check(CLI::ExistingFile);
    CLI11_PARSE(app, argc, argv);

    try {
        const torustiq::PipelineConfig config = torustiq::LoadPipelineConfig(pipeline_path);
        torustiq::PluginRegistry registry;
        torustiq::RegisterBuiltinPlugins(registry);
        torustiq::Pipeline pipeline(config, registry);

        std::signal(SIGINT, HandleSignal);
        std::signal(SIGTERM, HandleSignal);
        std::atomic<bool> finished{false};
        std::thread watcher([&] {
            while (!finished.load()) {
                if (g_stop_requested != 0) {
                    pipeline.Stop();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });

        const int result = pipeline.Run();
        finished.store(true);
        watcher.join();
        if (result != 0) {
            std::cerr << "error: " << pipeline.error() << "\n";
        }
        return result;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
