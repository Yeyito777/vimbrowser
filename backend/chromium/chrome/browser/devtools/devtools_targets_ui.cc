// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/devtools/devtools_targets_ui.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/devtools/serialize_host_descriptions.h"
#include "chrome/browser/profiles/profile.h"
#include "components/media_router/browser/presentation/local_presentation_manager.h"
#include "components/media_router/browser/presentation/local_presentation_manager_factory.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_observer.h"

using content::DevToolsAgentHost;

namespace {

const char kTargetSourceField[]  = "source";
const char kTargetSourceLocal[]  = "local";
const char kTargetSourceRemote[]  = "remote";

const char kTargetIdField[]  = "id";
const char kTargetTypeField[]  = "type";
const char kAttachedField[]  = "attached";
const char kUrlField[]  = "url";
const char kNameField[]  = "name";
const char kFaviconUrlField[] = "faviconUrl";
const char kDescriptionField[] = "description";

const char kGuestList[] = "guests";

// LocalTargetsUIHandler ---------------------------------------------

class LocalTargetsUIHandler : public DevToolsTargetsUIHandler,
                              public content::DevToolsAgentHostObserver {
 public:
  LocalTargetsUIHandler(const Callback& callback, Profile* profile);
  ~LocalTargetsUIHandler() override;

  // DevToolsTargetsUIHandler overrides.
  void ForceUpdate() override;

private:
 // content::DevToolsAgentHostObserver overrides.
 bool ShouldForceDevToolsAgentHostCreation() override;
 void DevToolsAgentHostCreated(DevToolsAgentHost* host) override;
 void DevToolsAgentHostDestroyed(DevToolsAgentHost* host) override;

 void ScheduleUpdate();
 void UpdateTargets();

 bool AllowDevToolsFor(DevToolsAgentHost* host);

 raw_ptr<Profile> profile_;
 raw_ptr<media_router::LocalPresentationManager> local_presentation_manager_;
 std::unique_ptr<base::OneShotTimer> timer_;
 base::WeakPtrFactory<LocalTargetsUIHandler> weak_factory_{this};
};

LocalTargetsUIHandler::LocalTargetsUIHandler(const Callback& callback,
                                             Profile* profile)
    : DevToolsTargetsUIHandler(kTargetSourceLocal, callback),
      profile_(profile),
      local_presentation_manager_(
          media_router::LocalPresentationManagerFactory::
              GetOrCreateForBrowserContext(profile_)) {
  DevToolsAgentHost::AddObserver(this);
  UpdateTargets();
}

LocalTargetsUIHandler::~LocalTargetsUIHandler() {
  DevToolsAgentHost::RemoveObserver(this);
}

bool LocalTargetsUIHandler::ShouldForceDevToolsAgentHostCreation() {
  return true;
}

void LocalTargetsUIHandler::DevToolsAgentHostCreated(DevToolsAgentHost*) {
  ScheduleUpdate();
}

void LocalTargetsUIHandler::DevToolsAgentHostDestroyed(DevToolsAgentHost*) {
  ScheduleUpdate();
}

void LocalTargetsUIHandler::ForceUpdate() {
  ScheduleUpdate();
}

void LocalTargetsUIHandler::ScheduleUpdate() {
  const int kUpdateDelay = 100;
  timer_ = std::make_unique<base::OneShotTimer>();
  timer_->Start(FROM_HERE, base::Milliseconds(kUpdateDelay),
                base::BindOnce(&LocalTargetsUIHandler::UpdateTargets,
                               base::Unretained(this)));
}

void LocalTargetsUIHandler::UpdateTargets() {
  content::DevToolsAgentHost::List targets =
      DevToolsAgentHost::GetOrCreateAll();

  std::vector<HostDescriptionNode> hosts;
  hosts.reserve(targets.size());
  targets_.clear();
  for (const scoped_refptr<DevToolsAgentHost>& host : targets) {
    if (!AllowDevToolsFor(host.get()))
      continue;

    targets_[host->GetId()] = host;
    hosts.push_back({host->GetId(), host->GetParentId(),
                     base::Value(Serialize(host.get()))});
  }

  SendSerializedTargets(
      base::Value(SerializeHostDescriptions(std::move(hosts), kGuestList)));
}

bool LocalTargetsUIHandler::AllowDevToolsFor(DevToolsAgentHost* host) {
  return local_presentation_manager_->IsLocalPresentation(
             host->GetWebContents()) ||
         (Profile::FromBrowserContext(host->GetBrowserContext()) == profile_ &&
          DevToolsWindow::AllowDevToolsFor(profile_, host->GetWebContents()));
}

} // namespace

// DevToolsTargetsUIHandler ---------------------------------------------------

DevToolsTargetsUIHandler::DevToolsTargetsUIHandler(const std::string& source_id,
                                                   Callback callback)
    : source_id_(source_id), callback_(std::move(callback)) {}

DevToolsTargetsUIHandler::~DevToolsTargetsUIHandler() = default;

// static
std::unique_ptr<DevToolsTargetsUIHandler>
DevToolsTargetsUIHandler::CreateForLocal(
    DevToolsTargetsUIHandler::Callback callback,
    Profile* profile) {
  return std::unique_ptr<DevToolsTargetsUIHandler>(
      new LocalTargetsUIHandler(callback, profile));
}

// static
std::unique_ptr<DevToolsTargetsUIHandler>
DevToolsTargetsUIHandler::CreateForAdb(
    DevToolsTargetsUIHandler::Callback callback,
    Profile*) {
  return std::make_unique<DevToolsTargetsUIHandler>(kTargetSourceRemote,
                                                    std::move(callback));
}

scoped_refptr<DevToolsAgentHost> DevToolsTargetsUIHandler::GetTarget(
    const std::string& target_id) {
  auto it = targets_.find(target_id);
  if (it != targets_.end())
    return it->second;
  return nullptr;
}

void DevToolsTargetsUIHandler::Open(const std::string& browser_id,
                                    const std::string& url) {
}

scoped_refptr<DevToolsAgentHost>
DevToolsTargetsUIHandler::GetBrowserAgentHost(const std::string& browser_id) {
  return nullptr;
}

base::DictValue DevToolsTargetsUIHandler::Serialize(DevToolsAgentHost* host) {
  base::DictValue target_data;
  target_data.Set(kTargetSourceField, source_id_);
  target_data.Set(kTargetIdField, host->GetId());
  target_data.Set(kTargetTypeField, host->GetType());
  target_data.Set(kAttachedField, host->IsAttached());
  target_data.Set(kUrlField, host->GetURL().spec());
  target_data.Set(kNameField, host->GetTitle());
  target_data.Set(kFaviconUrlField, host->GetFaviconURL().spec());
  target_data.Set(kDescriptionField, host->GetDescription());
  return target_data;
}

void DevToolsTargetsUIHandler::SendSerializedTargets(const base::Value& list) {
  callback_.Run(source_id_, list);
}

void DevToolsTargetsUIHandler::ForceUpdate() {
}

// PortForwardingStatusSerializer ---------------------------------------------

PortForwardingStatusSerializer::PortForwardingStatusSerializer(
    const Callback&, Profile*) {}

PortForwardingStatusSerializer::~PortForwardingStatusSerializer() = default;
