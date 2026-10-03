// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/component_updater/widevine_cdm_component_installer.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/native_library.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "base/version.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/cdm/common/cdm_manifest.h"
#include "components/component_updater/component_installer.h"
#include "components/component_updater/component_updater_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/cdm_registry.h"
#include "content/public/common/cdm_info.h"
#include "content/public/common/content_paths.h"
#include "crypto/sha2.h"
#include "media/base/cdm_capability.h"
#include "media/cdm/cdm_paths.h"
#include "third_party/widevine/cdm/buildflags.h"
#include "third_party/widevine/cdm/widevine_cdm_common.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include "base/path_service.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/media/component_widevine_cdm_hint_file_linux.h"
#endif


#if !BUILDFLAG(ENABLE_WIDEVINE_CDM_COMPONENT)
#error This file should only be compiled when Widevine CDM component is enabled
#endif

namespace component_updater {

namespace {

// CRX hash. The extension id is: oimompecagnajdejgnnjijobebaeigek.
const uint8_t kWidevineSha2Hash[] = {
    0xe8, 0xce, 0xcf, 0x42, 0x06, 0xd0, 0x93, 0x49, 0x6d, 0xd9, 0x89,
    0xe1, 0x41, 0x04, 0x86, 0x4a, 0x8f, 0xbd, 0x86, 0x12, 0xb9, 0x58,
    0x9b, 0xfb, 0x4f, 0xbb, 0x1b, 0xa9, 0xd3, 0x85, 0x37, 0xef};
static_assert(std::size(kWidevineSha2Hash) == crypto::kSHA256Length);


#if !BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CHROMEOS)
// On Linux and ChromeOS the Widevine CDM is loaded at startup before the
// zygote is locked down. As a result there is no need to register the CDM
// with Chrome as it can't be used until Chrome is restarted.
void RegisterWidevineCdmWithChrome(const base::Version& cdm_version,
                                   const base::FilePath& cdm_path,
                                   base::DictValue manifest) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // This check must be a subset of the check in VerifyInstallation() to
  // avoid the case where the CDM is accepted by the component updater
  // but not registered.
  media::CdmCapability capability;
  if (!ParseCdmManifest(manifest, &capability)) {
    VLOG(1) << "Not registering Widevine CDM due to malformed manifest.";
    return;
  }

  VLOG(1) << "Registering Widevine CDM " << cdm_version << " with Chrome";

  content::CdmInfo cdm_info(
      kWidevineKeySystem, content::CdmInfo::Robustness::kSoftwareSecure,
      std::move(capability), /*supports_sub_key_systems=*/false,
      kWidevineCdmDisplayName, kWidevineCdmType, cdm_path);
  content::CdmRegistry::GetInstance()->RegisterCdm(cdm_info);
}
#endif  // !BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
// On Linux and ChromeOS the Widevine CDM is loaded at startup before the
// zygote is locked down. To locate the Widevine CDM at startup, a hint file
// is used. Update the hint file with the new Widevine CDM path.
bool UpdateHintFile(const base::FilePath& cdm_base_path) {
  // Also record the current bundled Widevine CDMs version, if a bundled
  // Widevine CDM is supported and it exists.
  std::optional<base::Version> bundled_version;

#if BUILDFLAG(BUNDLE_WIDEVINE_CDM)
  base::FilePath bundled_cdm_file_path;
  CHECK(base::PathService::Get(chrome::DIR_BUNDLED_WIDEVINE_CDM,
                               &bundled_cdm_file_path));

  auto manifest_path =
      bundled_cdm_file_path.Append(FILE_PATH_LITERAL("manifest.json"));
  media::CdmCapability capability;
  if (ParseCdmManifestFromPath(manifest_path, &capability)) {
    bundled_version = capability.version;
  }
#endif  // BUILDFLAG(BUNDLE_WIDEVINE_CDM)

  return UpdateWidevineCdmHintFile(cdm_base_path, bundled_version);
}

#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

// Determine the full path to the Widevine CDM binary.
base::FilePath GetCdmPathFromInstallDir(const base::FilePath& install_dir) {
  base::FilePath cdm_platform_dir =
      media::GetPlatformSpecificDirectory(install_dir);
  std::string cdm_lib_name =
      base::GetNativeLibraryName(kWidevineCdmLibraryName);
  base::FilePath cdm_path = cdm_platform_dir.AppendASCII(cdm_lib_name);
  DVLOG(1) << __func__ << ": cdm_path=" << cdm_path;
  return cdm_path;
}


}  // namespace

class WidevineCdmComponentInstallerPolicy : public ComponentInstallerPolicy {
 public:
  WidevineCdmComponentInstallerPolicy();

  WidevineCdmComponentInstallerPolicy(
      const WidevineCdmComponentInstallerPolicy&) = delete;
  WidevineCdmComponentInstallerPolicy& operator=(
      const WidevineCdmComponentInstallerPolicy&) = delete;

 private:
  // The following methods override ComponentInstallerPolicy.
  bool SupportsGroupPolicyEnabledComponentUpdates() const override;
  bool RequiresNetworkEncryption() const override;
  update_client::CrxInstaller::Result OnCustomInstall(
      const base::DictValue& manifest,
      const base::FilePath& install_dir) override;
  void OnCustomUninstall() override;
  bool VerifyInstallation(const base::DictValue& manifest,
                          const base::FilePath& install_dir) const override;
  void ComponentReady(const base::Version& version,
                      const base::FilePath& path,
                      base::DictValue manifest) override;
  base::FilePath GetRelativeInstallDir() const override;
  void GetHash(std::vector<uint8_t>* hash) const override;
  std::string GetName() const override;
  update_client::InstallerAttributes GetInstallerAttributes() const override;

  // Updates CDM path if necessary.
  void UpdateCdmPath(const base::Version& cdm_version,
                     const base::FilePath& cdm_install_dir,
                     base::DictValue manifest);
};

WidevineCdmComponentInstallerPolicy::WidevineCdmComponentInstallerPolicy() =
    default;

bool WidevineCdmComponentInstallerPolicy::
    SupportsGroupPolicyEnabledComponentUpdates() const {
  return true;
}

bool WidevineCdmComponentInstallerPolicy::RequiresNetworkEncryption() const {
  return false;
}

update_client::CrxInstaller::Result
WidevineCdmComponentInstallerPolicy::OnCustomInstall(
    const base::DictValue& manifest,
    const base::FilePath& install_dir) {
  DVLOG(1) << __func__ << ": install_dir=" << install_dir
           << ", manifest=" << manifest;


  return update_client::CrxInstaller::Result(update_client::InstallError::NONE);
}

void WidevineCdmComponentInstallerPolicy::OnCustomUninstall() {}

// Once the CDM is ready, update the CDM path.
void WidevineCdmComponentInstallerPolicy::ComponentReady(
    const base::Version& version,
    const base::FilePath& path,
    base::DictValue manifest) {
  DVLOG(1) << __func__ << ": version=" << version << ", path=" << path;
  if (!IsCdmManifestCompatibleWithChrome(manifest)) {
    VLOG(1) << "Widevine CDM component " << version << " is incompatible.";
    return;
  }

  // Widevine CDM affects encrypted media playback, hence USER_VISIBLE.
  // See http://crbug.com/41423424 for the context.
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WidevineCdmComponentInstallerPolicy::UpdateCdmPath,
                     base::Unretained(this), version, path,
                     std::move(manifest)));
}

bool WidevineCdmComponentInstallerPolicy::VerifyInstallation(
    const base::DictValue& manifest,
    const base::FilePath& install_dir) const {
  // On ChromeOS, what gets downloaded is an image rather than the directory
  // structure expected. As a result, we can not check that there is an
  // library contained until the image is loaded. But on all other systems
  // we can check for the library.
  base::FilePath cdm_path = GetCdmPathFromInstallDir(install_dir);
  if (!base::PathExists(cdm_path)) {
    return false;
  }

  // Validate that the manifest looks reasonable.
  media::CdmCapability capability;
  return IsCdmManifestCompatibleWithChrome(manifest) &&
         ParseCdmManifest(manifest, &capability);
}

// The base directory on Windows looks like:
// <profile>\AppData\Local\Google\Chrome\User Data\WidevineCdm\.
base::FilePath WidevineCdmComponentInstallerPolicy::GetRelativeInstallDir()
    const {
  return base::FilePath::FromUTF8Unsafe(kWidevineCdmBaseDirectory);
}

void WidevineCdmComponentInstallerPolicy::GetHash(
    std::vector<uint8_t>* hash) const {
  hash->assign(std::begin(kWidevineSha2Hash), std::end(kWidevineSha2Hash));
}

std::string WidevineCdmComponentInstallerPolicy::GetName() const {
  return kWidevineCdmDisplayName;
}

update_client::InstallerAttributes
WidevineCdmComponentInstallerPolicy::GetInstallerAttributes() const {
  return update_client::InstallerAttributes();
}

void WidevineCdmComponentInstallerPolicy::UpdateCdmPath(
    const base::Version& cdm_version,
    const base::FilePath& cdm_install_dir,
    base::DictValue manifest) {
  // This function is called by ComponentReady() on a separate thread.
  DVLOG(1) << __func__ << ": version=" << cdm_version
           << ", dir=" << cdm_install_dir;

  // On some platforms (e.g. Mac) we use symlinks for paths. Convert paths to
  // absolute paths to avoid unexpected failure. base::MakeAbsoluteFilePath()
  // requires IO so it can only be done in this function.
  const base::FilePath absolute_cdm_install_dir =
      base::MakeAbsoluteFilePath(cdm_install_dir);
  if (absolute_cdm_install_dir.empty()) {
    PLOG(WARNING) << "Failed to get absolute CDM install path.";
    return;
  }

#if BUILDFLAG(IS_LINUX)
  VLOG(1) << "Updating hint file with Widevine CDM " << cdm_version;

  // This is running on a thread that allows IO, so simply update the hint file.
  if (!UpdateHintFile(absolute_cdm_install_dir)) {
    PLOG(WARNING) << "Failed to update Widevine CDM hint path.";
  }

#else
  // On other platforms (e.g. Windows, Mac) where the CDM can be dynamically
  // loaded, register the new CDM so that it can be used.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&RegisterWidevineCdmWithChrome, cdm_version,
                     GetCdmPathFromInstallDir(absolute_cdm_install_dir),
                     std::move(manifest)));
#endif
}

void RegisterWidevineCdmComponent(ComponentUpdateService* cus) {
  auto installer = base::MakeRefCounted<ComponentInstaller>(
      std::make_unique<WidevineCdmComponentInstallerPolicy>());
  installer->Register(cus, base::OnceClosure());
}

}  // namespace component_updater
