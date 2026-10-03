// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/profile_policy_connector.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_switcher/browser_switcher_policy_migrator.h"
#include "chrome/browser/enterprise/util/affiliation.h"
#include "chrome/browser/infobars/simple_alert_infobar_creator.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/infobar.h"
#include "components/infobars/core/infobar_delegate.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/cloud/cloud_policy_core.h"
#include "components/policy/core/common/cloud/cloud_policy_manager.h"
#include "components/policy/core/common/cloud/cloud_policy_store.h"
#include "components/policy/core/common/cloud/machine_level_user_cloud_policy_manager.h"
#include "components/policy/core/common/configuration_policy_provider.h"
#include "components/policy/core/common/local_test_policy_provider.h"
#include "components/policy/core/common/policy_bundle.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_namespace.h"
#include "components/policy/core/common/policy_service_impl.h"
#include "components/policy/core/common/policy_types.h"
#include "components/policy/core/common/proxy_policy_provider.h"
#include "components/policy/core/common/schema_registry_tracking_policy_provider.h"
#include "components/policy/policy_constants.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"


#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"  // nogncheck crbug.com/40147906
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"

namespace policy {

namespace internal {
// Class responsible for showing infobar when test policies are set from
// the chrome://policy/test page
class LocalTestInfoBarVisibilityManager :
    public BrowserCollectionObserver,
    public TabStripModelObserver
{
 public:
  LocalTestInfoBarVisibilityManager() = default;

  LocalTestInfoBarVisibilityManager(const LocalTestInfoBarVisibilityManager&) =
      delete;
  LocalTestInfoBarVisibilityManager& operator=(
      const LocalTestInfoBarVisibilityManager&) = delete;

  ~LocalTestInfoBarVisibilityManager() override {
    if (infobar_active_) {
      DismissInfobarsForActiveLocalTestPoliciesAllTabs();
    }
  }

  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    CHECK(browser);

    // TODO(crbug.com/452120900): TabStripModel auto-unregistered by dtor
    browser->GetTabStripModel()->AddObserver(this);
  }

  void OnBrowserClosed(BrowserWindowInterface* browser) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    CHECK(browser);

    if (GlobalBrowserCollection::GetInstance()->IsEmpty()) {
      browser_collection_observation_.Reset();
    }
  }

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (change.type() == TabStripModelChange::kInserted) {
      for (const auto& contents : change.GetInsert()->contents) {
        AddInfobarForActiveLocalTestPolicies(contents.contents);
      }
    } else if (change.type() == TabStripModelChange::kRemoved &&
               tab_strip_model->empty()) {
      tab_strip_model->RemoveObserver(this);
    }
  }

  void AddInfobarsForActiveLocalTestPoliciesAllTabs() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [this](BrowserWindowInterface* browser) {
          CHECK(browser);

          OnBrowserCreated(browser);

          TabStripModel* const tab_strip_model = browser->GetTabStripModel();
          for (int i = 0; i < tab_strip_model->count(); i++) {
            AddInfobarForActiveLocalTestPolicies(
                tab_strip_model->GetWebContentsAt(i));
          }
          return true;
        });
    browser_collection_observation_.Observe(
        GlobalBrowserCollection::GetInstance());
    infobar_active_ = true;
  }

  void AddInfobarForActiveLocalTestPolicies(
      content::WebContents* web_contents) {
    infobars::ContentInfoBarManager::CreateForWebContents(web_contents);
    CreateSimpleAlertInfoBar(
        infobars::ContentInfoBarManager::FromWebContents(web_contents),
        infobars::InfoBarDelegate::LOCAL_TEST_POLICIES_APPLIED_INFOBAR, nullptr,
        l10n_util::GetStringUTF16(IDS_LOCAL_TEST_POLICIES_ENABLED),
        /*auto_expire=*/false, /*should_animate=*/false, /*closeable=*/false,
        /*infobar_priority=*/
        infobars::InfoBarDelegate::InfobarPriority::kLow);
  }

  void DismissInfobarsForActiveLocalTestPoliciesAllTabs() {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [this](BrowserWindowInterface* browser) {
          CHECK(browser);

          browser->GetTabStripModel()->RemoveObserver(this);

          TabStripModel* const tab_strip_model = browser->GetTabStripModel();
          for (int i = 0; i < tab_strip_model->count(); i++) {
            DismissInfobarForActiveLocalTestPolicies(
                tab_strip_model->GetWebContentsAt(i));
          }
          return true;
        });
    browser_collection_observation_.Reset();
    infobar_active_ = false;
  }

  void DismissInfobarForActiveLocalTestPolicies(
      content::WebContents* web_contents) {
    infobars::ContentInfoBarManager::CreateForWebContents(web_contents);
    auto* infobar_manager =
        infobars::ContentInfoBarManager::FromWebContents(web_contents);
    const auto it = std::ranges::find(
        infobar_manager->infobars(),
        infobars::InfoBarDelegate::LOCAL_TEST_POLICIES_APPLIED_INFOBAR,
        &infobars::InfoBar::GetIdentifier);
    if (it != infobar_manager->infobars().cend()) {
      infobar_manager->RemoveInfoBar(*it);
    }
  }

  bool infobar_active() { return infobar_active_; }

 private:
  bool infobar_active_ = false;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
};
}  // namespace internal


ProfilePolicyConnector::ProfilePolicyConnector() = default;

ProfilePolicyConnector::~ProfilePolicyConnector() {
  if (policy_service_) {
    // We should've subscribed by this point, but in case not that's a no-op.
    policy_service_->RemoveObserver(POLICY_DOMAIN_CHROME, this);
  }
}

void ProfilePolicyConnector::Init(
    const user_manager::User* user,
    SchemaRegistry* schema_registry,
    ConfigurationPolicyProvider* configuration_policy_provider,
    const CloudPolicyStore* policy_store,
    policy::ChromeBrowserPolicyConnector* connector,
    bool force_immediate_load) {
  DCHECK(!configuration_policy_provider_);
  DCHECK(!policy_store_);
  DCHECK(!policy_service_);

  configuration_policy_provider_ = configuration_policy_provider;
  policy_store_ = policy_store;
  local_test_infobar_visibility_manager_ =
      std::make_unique<internal::LocalTestInfoBarVisibilityManager>();

  DCHECK_EQ(nullptr, user);

  ConfigurationPolicyProvider* platform_provider =
      connector->GetPlatformProvider();
  if (platform_provider) {
    AppendPolicyProviderWithSchemaTracking(platform_provider, schema_registry);
  }

  ConfigurationPolicyProvider* machine_level_user_cloud_policy_provider =
      connector->proxy_policy_provider();
  if (machine_level_user_cloud_policy_provider) {
    policy_providers_.push_back(machine_level_user_cloud_policy_provider);
  }

  if (connector->command_line_policy_provider()) {
    policy_providers_.push_back(connector->command_line_policy_provider());
  }

    local_test_policy_provider_ = connector->local_test_policy_provider();

  if (configuration_policy_provider) {
    policy_providers_.push_back(configuration_policy_provider);
  }


  std::vector<std::unique_ptr<PolicyMigrator>> migrators;

  policy_service_ = std::make_unique<PolicyServiceImpl>(
      policy_providers_, PolicyServiceImpl::ScopeForMetrics::kUser,
      std::move(migrators));

  if (local_test_policy_provider_ && local_test_policy_provider_->is_active()) {
    UseLocalTestPolicyProvider();
  }
  DoPostInit();
}

void ProfilePolicyConnector::InitForTesting(
    std::unique_ptr<PolicyService> service) {
  DCHECK(!policy_service_);
  policy_service_ = std::move(service);
  DoPostInit();
}

void ProfilePolicyConnector::OverrideIsManagedForTesting(bool is_managed) {
  is_managed_override_ = std::make_unique<bool>(is_managed);
}

void ProfilePolicyConnector::Shutdown() {


  for (auto& wrapped_policy_provider : wrapped_policy_providers_) {
    wrapped_policy_provider->Shutdown();
  }
}

bool ProfilePolicyConnector::IsManaged() const {
  if (is_managed_override_)
    return *is_managed_override_;
  const CloudPolicyStore* actual_policy_store = GetActualPolicyStore();
  if (actual_policy_store)
    return actual_policy_store->is_managed();
  return false;
}

bool ProfilePolicyConnector::IsProfilePolicy(const char* policy_key) const {
  const ConfigurationPolicyProvider* const provider =
      DeterminePolicyProviderForPolicy(policy_key);
  return provider == configuration_policy_provider_;
}


base::flat_set<std::string> ProfilePolicyConnector::user_affiliation_ids()
    const {
  if (!user_affiliation_ids_for_testing_.empty()) {
    return user_affiliation_ids_for_testing_;
  }
  auto* store = GetActualPolicyStore();
  if (!store || !store->has_policy())
    return {};
  const auto& ids = store->policy()->user_affiliation_ids();
  return {ids.begin(), ids.end()};
}

void ProfilePolicyConnector::SetUserAffiliationIdsForTesting(
    const base::flat_set<std::string>& user_affiliation_ids) {
  user_affiliation_ids_for_testing_ = user_affiliation_ids;
}

void ProfilePolicyConnector::OnPolicyServiceInitialized(PolicyDomain domain) {
  DCHECK_EQ(domain, POLICY_DOMAIN_CHROME);
  RecordAffiliationMetrics();
}

void ProfilePolicyConnector::DoPostInit() {
  DCHECK(policy_service_);
  policy_service_->AddObserver(POLICY_DOMAIN_CHROME, this);
}

const CloudPolicyStore* ProfilePolicyConnector::GetActualPolicyStore() const {
  if (policy_store_)
    return policy_store_;
  return nullptr;
}

const ConfigurationPolicyProvider*
ProfilePolicyConnector::DeterminePolicyProviderForPolicy(
    const char* policy_key) const {
  const PolicyNamespace chrome_ns(POLICY_DOMAIN_CHROME, "");
  for (const ConfigurationPolicyProvider* provider : policy_providers_) {
    if (provider->policies().Get(chrome_ns).Get(policy_key))
      return provider;
  }
  return nullptr;
}

void ProfilePolicyConnector::AppendPolicyProviderWithSchemaTracking(
    ConfigurationPolicyProvider* policy_provider,
    SchemaRegistry* schema_registry) {
  auto wrapped_policy_provider =
      std::make_unique<SchemaRegistryTrackingPolicyProvider>(policy_provider);
  wrapped_policy_provider->Init(schema_registry);
  policy_providers_.push_back(wrapped_policy_provider.get());
  wrapped_policy_providers_.push_back(std::move(wrapped_policy_provider));
}

std::string ProfilePolicyConnector::GetTimeToFirstPolicyLoadMetricSuffix()
    const {

  if (!IsManaged()) {
    return "Unmanaged";
  }

  return "Managed";
}

void ProfilePolicyConnector::UseLocalTestPolicyProvider() {
  if (IsManaged()) {
    return;
  }
  local_test_policy_provider_->set_active(true);
  policy_service_->UseLocalTestPolicyProvider(local_test_policy_provider_);
  policy_service()->RefreshPolicies(base::DoNothing(),
                                    PolicyFetchReason::kTest);
  if (!local_test_infobar_visibility_manager_->infobar_active()) {
    local_test_infobar_visibility_manager_
        ->AddInfobarsForActiveLocalTestPoliciesAllTabs();
  }
}

void ProfilePolicyConnector::RevertUseLocalTestPolicyProvider() {
  local_test_policy_provider_->set_active(false);
  policy_service_->UseLocalTestPolicyProvider(nullptr);
  static_cast<LocalTestPolicyProvider*>(local_test_policy_provider_)
      ->ClearPolicies();
  policy_service()->RefreshPolicies(base::DoNothing(),
                                    PolicyFetchReason::kTest);
  if (local_test_infobar_visibility_manager_->infobar_active()) {
    local_test_infobar_visibility_manager_
        ->DismissInfobarsForActiveLocalTestPoliciesAllTabs();
  }
}

bool ProfilePolicyConnector::IsUsingLocalTestPolicyProvider() const {
  return local_test_policy_provider_ &&
         local_test_policy_provider_->is_active();
}

void ProfilePolicyConnector::RecordAffiliationMetrics() {
  const PolicyMap& chrome_policies = policy_service()->GetPolicies(
      policy::PolicyNamespace(policy::POLICY_DOMAIN_CHROME, std::string()));

  base::UmaHistogramBoolean("Enterprise.ProfileAffiliation.IsAffiliated",
                            chrome_policies.IsUserAffiliated());

  if (!chrome_policies.IsUserAffiliated()) {
    const auto reason = enterprise_util::GetUnaffiliatedReason(this);
    base::UmaHistogramEnumeration(
        "Enterprise.ProfileAffiliation.UnaffiliatedReason", reason);
  }

  // base::Unretained is safe because `this` owns the timer.
  management_status_metrics_timer_.Start(
      FROM_HERE, base::Days(7),
      base::BindRepeating(&ProfilePolicyConnector::RecordAffiliationMetrics,
                          base::Unretained(this)));
}


}  // namespace policy
