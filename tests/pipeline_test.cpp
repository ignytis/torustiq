#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "core/message.h"
#include "core/pipeline.h"
#include "core/pipeline_config.h"

namespace torustiq {
namespace {

// Collected output, shared by the fake output plugin.
std::mutex g_mutex;
std::vector<std::string> g_bodies;
std::atomic<int> g_flushes{0};

std::string Body(const torustiq_message& m) {
    return std::string(reinterpret_cast<const char*>(m.payload.as.bytes.data),
                       m.payload.as.bytes.len);
}

// Input emitting `count` messages ("0", "1", ...). count < 0: endless until stop().
struct FakeInput {
    long count = 0;
    std::atomic<bool> stopped{false};
};
int32_t InCreate(const torustiq_config* config, torustiq_payload_spec* spec, void** out) {
    spec->kind = TORUSTIQ_PAYLOAD_BYTES;
    auto* in = new FakeInput();
    const char* count = torustiq_config_get(config, "count");
    in->count = count != nullptr ? std::stol(count) : 0;
    *out = in;
    return TORUSTIQ_OK;
}
int32_t InRun(void* h, torustiq_emit_fn emit, void* ud) {
    auto* in = static_cast<FakeInput*>(h);
    for (long i = 0; (in->count < 0 || i < in->count) && !in->stopped; ++i) {
        const std::string s = std::to_string(i);
        torustiq_message m{};
        m.payload = torustiq_value_from_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
        if (emit(ud, &m) != TORUSTIQ_OK) {
            break;
        }
    }
    return TORUSTIQ_OK;
}
void InStop(void* h) {
    static_cast<FakeInput*>(h)->stopped = true;
}
void InDestroy(void* h) {
    delete static_cast<FakeInput*>(h);
}
int32_t FailingInRun(void*, torustiq_emit_fn, void*) {
    return TORUSTIQ_ERROR;
}

// Transform emitting every message twice with a "!" suffix; fails on body "fail".
int32_t TrCreate(const torustiq_config*,
                 const torustiq_payload_spec* in,
                 torustiq_payload_spec* spec,
                 void** out) {
    if (in->kind != TORUSTIQ_PAYLOAD_BYTES) {
        return TORUSTIQ_UNSUPPORTED_PAYLOAD;
    }
    spec->kind = TORUSTIQ_PAYLOAD_BYTES;
    *out = nullptr;
    return TORUSTIQ_OK;
}
int32_t TrProcess(void*, const torustiq_message* m, torustiq_emit_fn emit, void* ud) {
    std::string s = Body(*m);
    if (s == "fail") {
        return TORUSTIQ_ERROR;
    }
    s += "!";
    torustiq_message out{};
    out.payload = torustiq_value_from_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    emit(ud, &out);
    return emit(ud, &out);
}
int32_t FailOnOneProcess(void*, const torustiq_message* m, torustiq_emit_fn, void*) {
    return Body(*m) == "1" ? TORUSTIQ_ERROR : TORUSTIQ_OK;
}
void TrDestroy(void*) {}

int32_t OutCreate(const torustiq_config*, const torustiq_payload_spec* in, void** out) {
    if (in->kind != TORUSTIQ_PAYLOAD_BYTES) {
        return TORUSTIQ_UNSUPPORTED_PAYLOAD;
    }
    *out = nullptr;
    return TORUSTIQ_OK;
}
int32_t OutWrite(void*, const torustiq_message* m) {
    std::lock_guard lock(g_mutex);
    g_bodies.push_back(Body(*m));
    return TORUSTIQ_OK;
}
int32_t OutFlush(void*) {
    ++g_flushes;
    return TORUSTIQ_OK;
}
void OutDestroy(void*) {}

// Transform turning bytes into the structured value {"text": <body>}; for testing negotiation.
int32_t ToValueCreate(const torustiq_config*,
                      const torustiq_payload_spec* in,
                      torustiq_payload_spec* spec,
                      void** out) {
    if (in->kind != TORUSTIQ_PAYLOAD_BYTES) {
        return TORUSTIQ_UNSUPPORTED_PAYLOAD;
    }
    spec->kind = TORUSTIQ_PAYLOAD_VALUE;
    *out = nullptr;
    return TORUSTIQ_OK;
}
int32_t ToValueProcess(void*, const torustiq_message* m, torustiq_emit_fn emit, void* ud) {
    torustiq_map_entry entry{};
    entry.key.data = reinterpret_cast<const uint8_t*>("text");
    entry.key.len = 4;
    entry.value = m->payload;
    torustiq_message out{};
    out.payload.kind = TORUSTIQ_VALUE_MAP;
    out.payload.as.map.entries = &entry;
    out.payload.as.map.count = 1;
    return emit(ud, &out);
}
// Breaks its contract: declares structured output but emits bytes.
int32_t LiarProcess(void*, const torustiq_message* m, torustiq_emit_fn emit, void* ud) {
    return emit(ud, m);
}
// Structured-only output: records the "text" member.
int32_t ValueOutCreate(const torustiq_config*, const torustiq_payload_spec* in, void** out) {
    if (in->kind != TORUSTIQ_PAYLOAD_VALUE) {
        return TORUSTIQ_UNSUPPORTED_PAYLOAD;
    }
    *out = nullptr;
    return TORUSTIQ_OK;
}
int32_t ValueOutWrite(void*, const torustiq_message* m) {
    const torustiq_value* text = torustiq_value_map_get(&m->payload, "text");
    if (text == nullptr) {
        return TORUSTIQ_ERROR;
    }
    std::lock_guard lock(g_mutex);
    g_bodies.push_back(
        std::string(reinterpret_cast<const char*>(text->as.bytes.data), text->as.bytes.len));
    return TORUSTIQ_OK;
}

class PipelineTest : public ::testing::Test {
  protected:
    void SetUp() override {
        g_bodies.clear();
        g_flushes = 0;
        torustiq_input_vtable in{};
        in.abi_version = TORUSTIQ_SDK_ABI_VERSION;
        in.name = "fake_in";
        in.create = InCreate;
        in.run = InRun;
        in.stop = InStop;
        in.destroy = InDestroy;
        ASSERT_EQ(registry_.RegisterInput(in), TORUSTIQ_OK);
        in.name = "failing_in";
        in.run = FailingInRun;
        ASSERT_EQ(registry_.RegisterInput(in), TORUSTIQ_OK);

        torustiq_transform_vtable tr{};
        tr.abi_version = TORUSTIQ_SDK_ABI_VERSION;
        tr.name = "double";
        tr.create = TrCreate;
        tr.process = TrProcess;
        tr.destroy = TrDestroy;
        ASSERT_EQ(registry_.RegisterTransform(tr), TORUSTIQ_OK);
        tr.name = "fail_on_one";
        tr.process = FailOnOneProcess;
        ASSERT_EQ(registry_.RegisterTransform(tr), TORUSTIQ_OK);
        tr.name = "to_value";
        tr.create = ToValueCreate;
        tr.process = ToValueProcess;
        ASSERT_EQ(registry_.RegisterTransform(tr), TORUSTIQ_OK);
        tr.name = "liar";
        tr.process = LiarProcess;
        ASSERT_EQ(registry_.RegisterTransform(tr), TORUSTIQ_OK);

        torustiq_output_vtable out{};
        out.abi_version = TORUSTIQ_SDK_ABI_VERSION;
        out.name = "collect";
        out.create = OutCreate;
        out.write = OutWrite;
        out.flush = OutFlush;
        out.destroy = OutDestroy;
        ASSERT_EQ(registry_.RegisterOutput(out), TORUSTIQ_OK);
        out.name = "collect_value";
        out.create = ValueOutCreate;
        out.write = ValueOutWrite;
        ASSERT_EQ(registry_.RegisterOutput(out), TORUSTIQ_OK);
    }

    PluginRegistry registry_;
};

TEST_F(PipelineTest, FiniteInputRunsToCompletionInOrder) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in, config: {count: 3}}}
outputs: {o: {type: collect}}
)");
    Pipeline pipeline(config, registry_);
    EXPECT_EQ(pipeline.Run(), 0);
    EXPECT_EQ(g_bodies, (std::vector<std::string>{"0", "1", "2"}));
    EXPECT_EQ(g_flushes, 1);
}

TEST_F(PipelineTest, TransformsAreChained) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in, config: {count: 2}}}
transforms: {t1: {type: double}, t2: {type: double}}
outputs: {o: {type: collect}}
)");
    Pipeline pipeline(config, registry_);
    EXPECT_EQ(pipeline.Run(), 0);
    // 2 messages -> x2 -> x2, each transform appends "!".
    EXPECT_EQ(g_bodies.size(), 8u);
    EXPECT_EQ(g_bodies.front(), "0!!");
}

TEST_F(PipelineTest, FanOutToMultipleOutputs) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in, config: {count: 5}}}
outputs:
  o1: {type: collect}
  o2: {type: collect}
)");
    Pipeline pipeline(config, registry_);
    EXPECT_EQ(pipeline.Run(), 0);
    EXPECT_EQ(g_bodies.size(), 10u);
    EXPECT_EQ(g_flushes, 2);
}

TEST_F(PipelineTest, LargeVolumeWithBackpressure) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in, config: {count: 5000}}}
transforms: {t: {type: double}}
outputs: {o: {type: collect}}
)");
    Pipeline pipeline(config, registry_);
    EXPECT_EQ(pipeline.Run(), 0);
    EXPECT_EQ(g_bodies.size(), 10000u);
}

TEST_F(PipelineTest, StopEndsContinuousInput) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in, config: {count: -1}}}
outputs: {o: {type: collect}}
)");
    Pipeline pipeline(config, registry_);
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pipeline.Stop();
    });
    EXPECT_EQ(pipeline.Run(), 0);
    stopper.join();
    EXPECT_FALSE(g_bodies.empty());
    EXPECT_EQ(g_flushes, 1);
}

TEST_F(PipelineTest, InputFailureIsReported) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: failing_in}}
outputs: {o: {type: collect}}
)");
    Pipeline pipeline(config, registry_);
    EXPECT_EQ(pipeline.Run(), 1);
    EXPECT_NE(pipeline.error().find("'i'"), std::string::npos);
}

TEST_F(PipelineTest, TransformFailureStopsPipeline) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in, config: {count: -1}}}
transforms: {t: {type: fail_on_one}}
outputs: {o: {type: collect}}
)");
    Pipeline pipeline(config, registry_);
    EXPECT_EQ(pipeline.Run(), 1); // must terminate although the input is endless
    EXPECT_NE(pipeline.error().find("'t'"), std::string::npos);
}

TEST_F(PipelineTest, StructuredPayloadFlowsThroughAgreedKinds) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in, config: {count: 3}}}
transforms: {t: {type: to_value}}
outputs: {o: {type: collect_value}}
)");
    Pipeline pipeline(config, registry_);
    EXPECT_EQ(pipeline.Run(), 0) << pipeline.error();
    EXPECT_EQ(g_bodies, (std::vector<std::string>{"0", "1", "2"}));
}

TEST_F(PipelineTest, KindMismatchIsAConfigErrorNamingTheStep) {
    // bytes -> structured-only output
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in}}
outputs: {o: {type: collect_value}}
)");
    try {
        Pipeline pipeline(config, registry_);
        FAIL() << "expected a mismatch error";
    } catch (const std::runtime_error& e) {
        const std::string what = e.what();
        EXPECT_NE(what.find("'o'"), std::string::npos) << what;
        EXPECT_NE(what.find("bytes"), std::string::npos) << what;
        EXPECT_NE(what.find("'i'"), std::string::npos) << what;
    }
    // structured -> bytes-only transform
    auto config2 = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in}}
transforms: {a: {type: to_value}, b: {type: double}}
outputs: {o: {type: collect}}
)");
    EXPECT_THROW(Pipeline(config2, registry_), std::runtime_error);
}

TEST_F(PipelineTest, InputsFeedingOneStepMustAgreeOnKind) {
    auto config = ParsePipelineConfig(R"(
inputs:
  a: {type: fake_in}
  b: {type: fake_in}
transforms:
  v: {type: to_value, inputs: [a]}
outputs:
  o: {type: collect_value, inputs: [v, b]}
)");
    EXPECT_THROW(Pipeline(config, registry_), std::runtime_error);
}

TEST_F(PipelineTest, EmittingTheWrongKindFailsThePipeline) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in, config: {count: 2}}}
transforms: {t: {type: liar}}
outputs: {o: {type: collect_value}}
)");
    Pipeline pipeline(config, registry_);
    EXPECT_EQ(pipeline.Run(), 1);
    EXPECT_NE(pipeline.error().find("'t'"), std::string::npos);
    EXPECT_TRUE(g_bodies.empty());
}

TEST_F(PipelineTest, UnknownTypeThrows) {
    auto config = ParsePipelineConfig(R"(
inputs: {i: {type: nope}}
outputs: {o: {type: collect}}
)");
    EXPECT_THROW(Pipeline(config, registry_), std::runtime_error);
    auto config2 = ParsePipelineConfig(R"(
inputs: {i: {type: fake_in}}
transforms: {t: {type: lua}}
outputs: {o: {type: collect}}
)");
    EXPECT_THROW(Pipeline(config2, registry_), std::runtime_error);
}

} // namespace
} // namespace torustiq
