// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/chrome_sync_controller_builder.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/security_events/security_event_recorder.h"
#include "chrome/browser/themes/theme_local_data_batch_uploader.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_syncable_service.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/channel_info.h"
#include "components/desks_storage/core/desk_sync_service.h"
#include "components/spellcheck/spellcheck_buildflags.h"
#include "components/sync/base/data_type.h"
#include "components/sync/base/features.h"
#include "components/sync/base/pref_names.h"
#include "components/sync/base/report_unrecoverable_error.h"
#include "components/sync/model/data_type_controller_delegate.h"
#include "components/sync/model/data_type_store.h"
#include "components/sync/model/data_type_store_service.h"
#include "components/sync/model/forwarding_data_type_controller_delegate.h"
#include "components/sync/service/data_type_controller.h"
#include "components/sync/service/syncable_service_based_data_type_controller.h"
#include "content/public/browser/browser_thread.h"

#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
#include "chrome/browser/extensions/api/storage/settings_sync_util.h"  // nogncheck
#include "chrome/browser/extensions/sync/extension_local_data_batch_uploader.h"  // nogncheck
#include "chrome/browser/extensions/sync/extension_sync_service.h"  // nogncheck
#include "chrome/browser/sync/glue/extension_data_type_controller.h"
#include "chrome/browser/sync/glue/extension_setting_data_type_controller.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_sync_bridge.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)



ChromeSyncControllerBuilder::ChromeSyncControllerBuilder() = default;

ChromeSyncControllerBuilder::~ChromeSyncControllerBuilder() = default;

void ChromeSyncControllerBuilder::SetDataTypeStoreService(
    syncer::DataTypeStoreService* data_type_store_service) {
  data_type_store_service_.Set(data_type_store_service);
}

void ChromeSyncControllerBuilder::SetSecurityEventRecorder(
    SecurityEventRecorder* security_event_recorder) {
  security_event_recorder_.Set(security_event_recorder);
}

#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
void ChromeSyncControllerBuilder::SetExtensionSyncService(
    ExtensionSyncService* extension_sync_service) {
  extension_sync_service_.Set(extension_sync_service);
}

void ChromeSyncControllerBuilder::SetExtensionSystemProfile(Profile* profile) {
  extension_system_profile_.Set(profile);
}
#endif  // BUILDFLAG(ENABLE_EXTENSIONS_CORE)

#if BUILDFLAG(ENABLE_EXTENSIONS)
void ChromeSyncControllerBuilder::SetThemeService(ThemeService* theme_service) {
  theme_service_.Set(theme_service);
}

void ChromeSyncControllerBuilder::SetWebAppProvider(
    web_app::WebAppProvider* web_app_provider) {
  web_app_provider_.Set(web_app_provider);
}
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

#if BUILDFLAG(ENABLE_SPELLCHECK)
void ChromeSyncControllerBuilder::SetSpellcheckService(
    SpellcheckService* spellcheck_service) {
  spellcheck_service_.Set(spellcheck_service);
}
#endif  // BUILDFLAG(ENABLE_SPELLCHECK)



std::vector<std::unique_ptr<syncer::DataTypeController>>
ChromeSyncControllerBuilder::Build(syncer::SyncService* sync_service) {
  std::vector<std::unique_ptr<syncer::DataTypeController>> controllers;

  const base::RepeatingClosure dump_stack = base::BindRepeating(
      &syncer::ReportUnrecoverableError, chrome::GetChannel());

  syncer::RepeatingDataTypeStoreFactory data_type_store_factory =
      data_type_store_service_.value()->GetStoreFactory();

    syncer::DataTypeControllerDelegate* security_events_delegate =
        security_event_recorder_.value()->GetControllerDelegate().get();
    // Forward both full-sync and transport-only modes to the same delegate,
    // since behavior for SECURITY_EVENTS does not differ.
    controllers.push_back(std::make_unique<syncer::DataTypeController>(
        syncer::SECURITY_EVENTS,
        /*delegate_for_full_sync_mode=*/
        std::make_unique<syncer::ForwardingDataTypeControllerDelegate>(
            security_events_delegate),
        /*delegate_for_transport_mode=*/
        std::make_unique<syncer::ForwardingDataTypeControllerDelegate>(
            security_events_delegate)));

#if BUILDFLAG(ENABLE_EXTENSIONS_CORE)
    if (extension_sync_service_.value()) {
      // TODO(crbug.com/425381293): Add an ExtensionLocalDataBatchUploader once
      // its implementation is complete.
      controllers.push_back(
          std::make_unique<browser_sync::ExtensionDataTypeController>(
              syncer::EXTENSIONS, data_type_store_factory,
              extension_sync_service_.value()->AsWeakPtr(), dump_stack,
              browser_sync::ExtensionDataTypeController::DelegateMode::
                  kTransportModeWithSingleModel,
              extension_system_profile_.value(),
              std::make_unique<extensions::ExtensionLocalDataBatchUploader>(
                  extension_system_profile_.value())));

      controllers.push_back(
          std::make_unique<browser_sync::ExtensionSettingDataTypeController>(
              syncer::EXTENSION_SETTINGS, data_type_store_factory,
              extensions::settings_sync_util::GetSyncableServiceProvider(
                  extension_system_profile_.value(),
                  syncer::EXTENSION_SETTINGS),
              dump_stack,
              browser_sync::ExtensionSettingDataTypeController::DelegateMode::
                  kTransportModeWithSingleModel,
              extension_system_profile_.value()));
    }
#endif  // BUILDFLAG(ENABLE_EXTENSIONS_CORE)

#if BUILDFLAG(ENABLE_EXTENSIONS)
    if (extension_sync_service_.value()) {
      controllers.push_back(
          std::make_unique<browser_sync::ExtensionDataTypeController>(
              syncer::APPS, data_type_store_factory,
              extension_sync_service_.value()->AsWeakPtr(), dump_stack,
              browser_sync::ExtensionDataTypeController::DelegateMode::
                  kLegacyFullSyncModeOnly,
              extension_system_profile_.value()));

      controllers.push_back(
          std::make_unique<browser_sync::ExtensionSettingDataTypeController>(
              syncer::APP_SETTINGS, data_type_store_factory,
              extensions::settings_sync_util::GetSyncableServiceProvider(
                  extension_system_profile_.value(), syncer::APP_SETTINGS),
              dump_stack,
              browser_sync::ExtensionSettingDataTypeController::DelegateMode::
                  kLegacyFullSyncModeOnly,
              extension_system_profile_.value()));
    }

    if (theme_service_.value()) {
      controllers.push_back(
          std::make_unique<browser_sync::ExtensionDataTypeController>(
              syncer::THEMES, data_type_store_factory,
              theme_service_.value()->GetThemeSyncableService()->AsWeakPtr(),
              dump_stack,
              base::FeatureList::IsEnabled(
                  syncer::kSeparateLocalAndAccountThemes)
                  ? browser_sync::ExtensionDataTypeController::DelegateMode::
                        kTransportModeWithSingleModel
                  : browser_sync::ExtensionDataTypeController::DelegateMode::
                        kLegacyFullSyncModeOnly,
              extension_system_profile_.value(),
              // SyncService depends on ThemeService. So the
              // ThemeSyncableService instance should outlive the controller.
              std::make_unique<ThemeLocalDataBatchUploader>(
                  theme_service_.value()->GetThemeSyncableService())));
    }

    if (web_app_provider_.value()) {
      syncer::DataTypeControllerDelegate* delegate =
          web_app_provider_.value()
              ->sync_bridge_unsafe()
              .change_processor()
              ->GetControllerDelegate()
              .get();

      controllers.push_back(std::make_unique<syncer::DataTypeController>(
          syncer::WEB_APPS,
          /*delegate_for_full_sync_mode=*/
          std::make_unique<syncer::ForwardingDataTypeControllerDelegate>(
              delegate),
          /*delegate_for_transport_mode=*/
          std::make_unique<syncer::ForwardingDataTypeControllerDelegate>(
              delegate)
          ));
    }
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)


#if BUILDFLAG(ENABLE_SPELLCHECK)
    // Chrome prefers OS provided spell checkers where they exist. So only sync
    // the custom dictionary on platforms that typically don't provide one.
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN)
    // Dictionary sync is enabled by default.
    if (spellcheck_service_.value()) {
      controllers.push_back(
          std::make_unique<syncer::SyncableServiceBasedDataTypeController>(
              syncer::DICTIONARY, data_type_store_factory,
              spellcheck_service_.value()->GetCustomDictionary()->AsWeakPtr(),
              dump_stack,
              base::FeatureList::IsEnabled(
                  syncer::kSpellcheckSeparateLocalAndAccountDictionaries)
                  ? syncer::SyncableServiceBasedDataTypeController::
                        DelegateMode::kTransportModeWithSingleModel
                  : syncer::SyncableServiceBasedDataTypeController::
                        DelegateMode::kLegacyFullSyncModeOnly));
    }
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN)
#endif  // BUILDFLAG(ENABLE_SPELLCHECK)


    return controllers;
}
