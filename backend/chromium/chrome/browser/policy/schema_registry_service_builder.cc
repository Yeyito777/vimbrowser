// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/schema_registry_service_builder.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "build/build_config.h"
#include "chrome/browser/policy/schema_registry_service.h"
#include "components/policy/core/common/schema.h"
#include "components/policy/core/common/schema_registry.h"
#include "content/public/browser/browser_context.h"


namespace policy {


std::unique_ptr<SchemaRegistryService> BuildSchemaRegistryServiceForProfile(
    content::BrowserContext* context,
    const Schema& chrome_schema,
    const Schema& extension_install_policy_schema,
    CombinedSchemaRegistry* global_registry) {
  DCHECK(!context->IsOffTheRecord());

  std::unique_ptr<SchemaRegistry> registry;


  if (!registry)
    registry = std::make_unique<SchemaRegistry>();


  return BuildSchemaRegistryService(std::move(registry), chrome_schema,
                                    extension_install_policy_schema,
                                    global_registry);
}

std::unique_ptr<SchemaRegistryService> BuildSchemaRegistryService(
    std::unique_ptr<SchemaRegistry> registry,
    const Schema& chrome_schema,
    const Schema& extension_install_policy_schema,
    CombinedSchemaRegistry* global_registry) {
  return std::make_unique<SchemaRegistryService>(
      std::move(registry), chrome_schema, extension_install_policy_schema,
      global_registry);
}

}  // namespace policy
