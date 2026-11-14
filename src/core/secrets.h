#ifndef TORUSTIQ_CORE_SECRETS_H_
#define TORUSTIQ_CORE_SECRETS_H_

#include "core/pipeline_config.h"
#include "core/plugin_registry.h"

namespace torustiq {

// Resolves the secrets referenced by the pipeline's components and substitutes their values into
// the components' `config` (removing the entries from `secret_refs`).
//
// Only referenced secrets are resolved, each once. Throws ConfigError for an unknown provider kind
// and std::runtime_error when a provider fails. Messages name the secret, never its value.
void ResolveSecrets(PipelineConfig& config, const PluginRegistry& registry);

} // namespace torustiq

#endif // TORUSTIQ_CORE_SECRETS_H_
