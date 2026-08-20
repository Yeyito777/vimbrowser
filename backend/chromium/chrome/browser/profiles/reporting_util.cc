// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/reporting_util.h"

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "components/account_id/account_id.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/enterprise/common/proto/connectors.pb.h"
#include "components/policy/core/common/cloud/cloud_policy_store.h"
#include "components/policy/core/common/cloud/user_cloud_policy_manager.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"


namespace {


}  // namespace

namespace reporting {

base::DictValue GetContext(Profile* profile) {
  base::DictValue context;
  context.SetByDottedPath("browser.userAgent",
                          embedder_support::GetUserAgent());

  if (!profile)
    return context;

  ProfileAttributesStorage& storage =
      g_browser_process->profile_manager()->GetProfileAttributesStorage();
  ProfileAttributesEntry* entry =
      storage.GetProfileAttributesWithPath(profile->GetPath());
  if (entry) {
    context.SetByDottedPath("profile.profileName", entry->GetName());
    context.SetByDottedPath("profile.gaiaEmail", entry->GetUserName());
  }

  context.SetByDottedPath("profile.profilePath",
                          profile->GetPath().AsUTF8Unsafe());

  std::optional<std::string> client_id = GetUserClientId(profile);
  if (client_id)
    context.SetByDottedPath("profile.clientId", *client_id);


  std::optional<std::string> user_dm_token = GetUserDmToken(profile);
  if (user_dm_token)
    context.SetByDottedPath("profile.dmToken", *user_dm_token);

  return context;
}

::chrome::cros::reporting::proto::UploadEventsRequest CreateUploadEventsRequest(
    Profile* profile) {
  ::chrome::cros::reporting::proto::UploadEventsRequest request;
  request.mutable_browser()->set_user_agent(embedder_support::GetUserAgent());

  if (!profile) {
    return request;
  }

  request.mutable_profile()->set_profile_path(
      profile->GetPath().AsUTF8Unsafe());
  ProfileAttributesEntry* profile_attributes =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile->GetPath());
  if (profile_attributes) {
    request.mutable_profile()->set_profile_name(
        base::UTF16ToUTF8(profile_attributes->GetName()));
    request.mutable_profile()->set_gaia_email(
        base::UTF16ToUTF8(profile_attributes->GetUserName()));
  }

  std::optional<std::string> client_id = GetUserClientId(profile);
  if (client_id) {
    request.mutable_profile()->set_client_id(*client_id);
  }


  std::optional<std::string> user_dm_token = GetUserDmToken(profile);
  if (user_dm_token) {
    request.mutable_profile()->set_dm_token(*user_dm_token);
  }

  return request;
}

enterprise_connectors::ClientMetadata GetContextAsClientMetadata(
    Profile* profile) {
  enterprise_connectors::ClientMetadata metadata;
  metadata.mutable_browser()->set_user_agent(embedder_support::GetUserAgent());

  if (!profile)
    return metadata;

  ProfileAttributesStorage& storage =
      g_browser_process->profile_manager()->GetProfileAttributesStorage();
  ProfileAttributesEntry* entry =
      storage.GetProfileAttributesWithPath(profile->GetPath());
  if (entry) {
    metadata.mutable_profile()->set_profile_name(
        base::UTF16ToUTF8(entry->GetName()));
    metadata.mutable_profile()->set_gaia_email(
        base::UTF16ToUTF8(entry->GetUserName()));
  }

  metadata.mutable_profile()->set_profile_path(
      profile->GetPath().AsUTF8Unsafe());

  std::optional<std::string> client_id = GetUserClientId(profile);
  if (client_id)
    metadata.mutable_profile()->set_client_id(*client_id);


  std::optional<std::string> user_dm_token = GetUserDmToken(profile);
  if (user_dm_token)
    metadata.mutable_profile()->set_dm_token(*user_dm_token);

  return metadata;
}

// Returns User DMToken for a given |profile| if:
// * |profile| is NOT incognito profile.
// * |profile| is NOT sign-in screen profile
// * user corresponding to a |profile| is managed.
// Otherwise returns empty string. More about DMToken:
// go/dmserver-domain-model#dmtoken.
std::optional<std::string> GetUserDmToken(Profile* profile) {
  if (!profile) {
    return std::nullopt;
  }
  auto* manager = profile->GetCloudPolicyManager();
  if (!manager) {
    return std::nullopt;
  }
  std::optional<policy::DMToken> dm_token = manager->GetDMToken();
  return dm_token ? std::make_optional(dm_token->value()) : std::nullopt;
}

std::optional<std::string> GetUserClientId(Profile* profile) {
  if (!profile) {
    return std::nullopt;
  }
  auto* manager = profile->GetCloudPolicyManager();
  return manager ? manager->GetClientId() : std::nullopt;
}


}  // namespace reporting
