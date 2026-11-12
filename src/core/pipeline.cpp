#include "core/pipeline.h"

#include <atomic>
#include <map>
#include <stdexcept>
#include <thread>

#include "core/bounded_queue.h"
#include "core/message.h"

namespace torustiq {
namespace {
constexpr size_t kQueueCapacity = 256;

const char* PayloadKindName(uint32_t kind) {
    switch (kind) {
    case TORUSTIQ_PAYLOAD_BYTES:
        return "bytes";
    case TORUSTIQ_PAYLOAD_TEXT:
        return "text";
    default:
        return "structured";
    }
}
} // namespace

enum class NodeKind { kInput, kTransform, kOutput };

struct PipelineNode {
    PipelineNode(NodeKind node_kind, std::string node_id)
        : kind(node_kind), id(std::move(node_id)), queue(kQueueCapacity) {}

    ~PipelineNode() {
        if (handle == nullptr) {
            return;
        }
        switch (kind) {
        case NodeKind::kInput:
            input.destroy(handle);
            break;
        case NodeKind::kTransform:
            transform.destroy(handle);
            break;
        case NodeKind::kOutput:
            output.destroy(handle);
            break;
        }
    }

    NodeKind kind;
    std::string id;
    torustiq_input_vtable input{};
    torustiq_transform_vtable transform{};
    torustiq_output_vtable output{};
    void* handle = nullptr;
    torustiq_payload_spec output_spec{TORUSTIQ_PAYLOAD_BYTES}; // what this node emits

    BoundedQueue<Message> queue;          // incoming messages (unused for inputs)
    std::vector<PipelineNode*> consumers; // downstream nodes
    std::atomic<int> producers_left{0};
};

namespace {

// Emit callback handed to plugins: copies the message to every downstream node.
int32_t Emit(void* user_data, const torustiq_message* message) {
    if (user_data == nullptr || message == nullptr) {
        return TORUSTIQ_ERROR;
    }
    auto* node = static_cast<PipelineNode*>(user_data);
    // The payload kind was agreed when the pipeline was built; this only catches a plugin that
    // breaks its own contract.
    if (PayloadKindOf(message->payload) != node->output_spec.kind) {
        return TORUSTIQ_ERROR;
    }
    Message copy;
    try {
        copy = MessageFromC(*message);
    } catch (const std::exception&) { // never let an exception cross the C boundary
        return TORUSTIQ_ERROR;
    }
    bool stopped = false;
    for (size_t i = 0; i < node->consumers.size(); ++i) {
        const bool last = i + 1 == node->consumers.size();
        if (!node->consumers[i]->queue.Push(last ? std::move(copy) : copy)) {
            stopped = true;
        }
    }
    return stopped ? TORUSTIQ_STOPPED : TORUSTIQ_OK;
}

} // namespace

Pipeline::Pipeline(const PipelineConfig& config, const PluginRegistry& registry) {
    std::map<std::string, PipelineNode*> by_id;

    auto add = [&](NodeKind kind, const ComponentConfig& component) {
        if (by_id.contains(component.id)) {
            throw std::runtime_error("duplicate component id '" + component.id + "'");
        }
        auto node = std::make_unique<PipelineNode>(kind, component.id);

        // Resolve upstream steps first: their output specs are this step's input spec.
        std::vector<PipelineNode*> upstreams;
        for (const std::string& upstream_id : component.inputs) {
            auto it = by_id.find(upstream_id);
            if (it == by_id.end() || it->second->kind == NodeKind::kOutput) {
                throw std::runtime_error("'" + component.id + "': invalid input '" + upstream_id +
                                         "'");
            }
            upstreams.push_back(it->second);
        }
        if (kind != NodeKind::kInput && upstreams.empty()) {
            throw std::runtime_error("'" + component.id + "' has no inputs");
        }
        torustiq_payload_spec input_spec{TORUSTIQ_PAYLOAD_BYTES};
        if (!upstreams.empty()) {
            input_spec = upstreams.front()->output_spec;
            for (const PipelineNode* upstream : upstreams) {
                if (upstream->output_spec.kind != input_spec.kind) {
                    throw std::runtime_error(
                        "'" + component.id + "' receives different payload kinds: '" +
                        upstreams.front()->id + "' emits " + PayloadKindName(input_spec.kind) +
                        ", '" + upstream->id + "' emits " +
                        PayloadKindName(upstream->output_spec.kind));
                }
            }
        }

        ConfigView params(component.config);
        int32_t status = TORUSTIQ_ERROR;
        switch (kind) {
        case NodeKind::kInput: {
            const auto* vtable = registry.FindInput(component.type);
            if (vtable == nullptr) {
                throw std::runtime_error("unknown input type '" + component.type + "' for '" +
                                         component.id + "'");
            }
            node->input = *vtable;
            status = vtable->create(&params.get(), &node->output_spec, &node->handle);
            break;
        }
        case NodeKind::kTransform: {
            const auto* vtable = registry.FindTransform(component.type);
            if (vtable == nullptr) {
                throw std::runtime_error("unknown transform type '" + component.type + "' for '" +
                                         component.id + "'");
            }
            node->transform = *vtable;
            status = vtable->create(&params.get(), &input_spec, &node->output_spec, &node->handle);
            break;
        }
        case NodeKind::kOutput: {
            const auto* vtable = registry.FindOutput(component.type);
            if (vtable == nullptr) {
                throw std::runtime_error("unknown output type '" + component.type + "' for '" +
                                         component.id + "'");
            }
            node->output = *vtable;
            status = vtable->create(&params.get(), &input_spec, &node->handle);
            break;
        }
        }
        if (status != TORUSTIQ_OK) {
            node->handle = nullptr; // a failed create() has nothing to destroy
            if (status == TORUSTIQ_UNSUPPORTED_PAYLOAD) {
                throw std::runtime_error("'" + component.id + "' (" + component.type +
                                         ") does not accept " + PayloadKindName(input_spec.kind) +
                                         " payloads from '" + upstreams.front()->id +
                                         "'; add a conversion step (e.g. decode_text, "
                                         "encode_text, decode_json or encode_json) between them");
            }
            throw std::runtime_error("failed to initialise '" + component.id + "' (" +
                                     component.type + ")");
        }
        if (kind != NodeKind::kOutput && node->output_spec.kind != TORUSTIQ_PAYLOAD_BYTES &&
            node->output_spec.kind != TORUSTIQ_PAYLOAD_VALUE &&
            node->output_spec.kind != TORUSTIQ_PAYLOAD_TEXT) {
            throw std::runtime_error("'" + component.id + "' declared an unknown payload kind");
        }
        PipelineNode* raw = node.get();
        by_id[component.id] = raw;
        nodes_.push_back(std::move(node));
        for (PipelineNode* upstream : upstreams) {
            upstream->consumers.push_back(raw);
            raw->producers_left++;
        }
    };

    for (const auto& c : config.inputs) {
        add(NodeKind::kInput, c);
    }
    for (const auto& c : config.transforms) {
        add(NodeKind::kTransform, c);
    }
    for (const auto& c : config.outputs) {
        add(NodeKind::kOutput, c);
    }
}

Pipeline::~Pipeline() = default;

int Pipeline::Run() {
    std::vector<std::thread> threads;
    threads.reserve(nodes_.size());
    for (auto& node : nodes_) {
        threads.emplace_back([this, &node] { RunNode(*node); });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    return error().empty() ? 0 : 1;
}

void Pipeline::Stop() {
    for (auto& node : nodes_) {
        if (node->kind == NodeKind::kInput && node->input.stop != nullptr) {
            node->input.stop(node->handle);
        }
    }
}

std::string Pipeline::error() const {
    std::lock_guard lock(error_mutex_);
    return error_;
}

void Pipeline::RunNode(PipelineNode& node) {
    try {
        switch (node.kind) {
        case NodeKind::kInput: {
            const int32_t status = node.input.run(node.handle, Emit, &node);
            if (status != TORUSTIQ_OK && status != TORUSTIQ_STOPPED) {
                Fail("input '" + node.id + "' failed");
            }
            break;
        }
        case NodeKind::kTransform:
            while (auto message = node.queue.Pop()) {
                MessageView view(*message);
                if (node.transform.process(node.handle, &view.get(), Emit, &node) ==
                    TORUSTIQ_ERROR) {
                    Fail("transform '" + node.id + "' failed");
                    break;
                }
            }
            break;
        case NodeKind::kOutput:
            while (auto message = node.queue.Pop()) {
                MessageView view(*message);
                if (node.output.write(node.handle, &view.get()) != TORUSTIQ_OK) {
                    Fail("output '" + node.id + "' failed to write");
                    break;
                }
            }
            if (node.output.flush != nullptr && node.output.flush(node.handle) != TORUSTIQ_OK) {
                Fail("output '" + node.id + "' failed to flush");
            }
            break;
        }
    } catch (const std::exception& e) {
        Fail("'" + node.id + "': " + e.what());
    }
    FinishNode(node);
}

// Closes downstream queues once all of their producers are done.
void Pipeline::FinishNode(PipelineNode& node) {
    for (PipelineNode* consumer : node.consumers) {
        if (--consumer->producers_left == 0) {
            consumer->queue.Close();
        }
    }
}

void Pipeline::Fail(const std::string& message) {
    {
        std::lock_guard lock(error_mutex_);
        if (error_.empty()) {
            error_ = message;
        }
    }
    Stop();
    for (auto& node : nodes_) {
        node->queue.Abort();
    }
}

} // namespace torustiq
