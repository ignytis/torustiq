#ifndef TORUSTIQ_CORE_PIPELINE_H_
#define TORUSTIQ_CORE_PIPELINE_H_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/pipeline_config.h"
#include "core/plugin_registry.h"

namespace torustiq {

struct PipelineNode;

// Runs a pipeline: one thread per component, connected by bounded queues (backpressure).
// Finite inputs end the pipeline when drained; continuous ones run until Stop().
class Pipeline {
  public:
    // Creates all plugin instances. Throws std::runtime_error for unknown types,
    // bad wiring or a plugin that fails to initialise.
    Pipeline(const PipelineConfig& config, const PluginRegistry& registry);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    // Blocks until all data is processed or the pipeline stops. Returns 0 on success, 1 if a
    // component failed (see error()). Call at most once.
    int Run();

    // Asks inputs to finish; already accepted messages are still delivered. Thread-safe.
    void Stop();

    std::string error() const;

  private:
    void RunNode(PipelineNode& node);
    void FinishNode(PipelineNode& node);
    void Fail(const std::string& message);

    std::vector<std::unique_ptr<PipelineNode>> nodes_;
    mutable std::mutex error_mutex_;
    std::string error_;
};

} // namespace torustiq

#endif // TORUSTIQ_CORE_PIPELINE_H_
