// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/password_manager/chrome_password_manager_client.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/types/optional_util.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "chrome/browser/affiliations/affiliation_service_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/device_reauth/chrome_device_authenticator_factory.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_tab_helper.h"
#include "chrome/browser/password_manager/account_password_store_factory.h"
#include "chrome/browser/password_manager/chrome_password_change_service.h"
#include "chrome/browser/password_manager/chrome_webauthn_credentials_delegate.h"
#include "chrome/browser/password_manager/chrome_webauthn_credentials_delegate_factory.h"
#include "chrome/browser/password_manager/factories/field_info_manager_factory.h"
#include "chrome/browser/password_manager/factories/password_reuse_manager_factory.h"
#include "chrome/browser/password_manager/password_change_service_factory.h"
#include "chrome/browser/password_manager/password_manager_settings_service_factory.h"
#include "chrome/browser/password_manager/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/signin_ui_util.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/ui/passwords/password_cross_domain_confirmation_popup_controller_impl.h"
#include "chrome/browser/ui/passwords/password_generation_popup_controller_impl.h"
#include "chrome/browser/ui/passwords/passwords_client_ui_delegate.h"
#include "chrome/browser/ui/passwords/passwords_model_delegate.h"
#include "chrome/browser/ui/passwords/ui_utils.h"
#include "chrome/browser/ui/user_education/browser_user_education_interface.h"
#include "chrome/browser/ui/webauthn/authenticator_request_window.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/autofill/content/browser/content_autofill_client.h"
#include "components/autofill/content/browser/renderer_forms_from_browser_form.h"
#include "components/autofill/core/browser/autofill_server_prediction.h"
#include "components/autofill/core/browser/foundations/scoped_autofill_managers_observation.h"
#include "components/autofill/core/browser/integrators/password_manager/password_manager_autofill_helper_delegate.h"
#include "components/autofill/core/browser/logging/log_manager.h"
#include "components/autofill/core/browser/logging/log_router.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/autofill/core/common/mojom/autofill_types.mojom-shared.h"
#include "components/autofill/core/common/password_generation_util.h"
#include "components/browsing_data/content/browsing_data_helper.h"
#include "components/device_reauth/device_authenticator.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_contents.h"
#include "components/password_manager/content/browser/bad_message.h"
#include "components/password_manager/content/browser/content_password_manager_driver.h"
#include "components/password_manager/content/browser/content_password_manager_driver_factory.h"
#include "components/password_manager/content/browser/form_meta_data.h"
#include "components/password_manager/content/browser/password_manager_log_router_factory.h"
#include "components/password_manager/content/browser/password_requirements_service_factory.h"
#include "components/password_manager/core/browser/browser_save_password_progress_logger.h"
#include "components/password_manager/core/browser/credential_manager_impl.h"
#include "components/password_manager/core/browser/features/password_features.h"
#include "components/password_manager/core/browser/hsts_query.h"
#include "components/password_manager/core/browser/http_auth_manager.h"
#include "components/password_manager/core/browser/http_auth_manager_impl.h"
#include "components/password_manager/core/browser/leak_detection_dialog_utils.h"
#include "components/password_manager/core/browser/passkey_credential.h"
#include "components/password_manager/core/browser/password_bubble_experiment.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_form_manager_for_ui.h"
#include "components/password_manager/core/browser/password_manager_blocklist_policy.h"
#include "components/password_manager/core/browser/password_manager_constants.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/password_manager/core/browser/password_manager_setting.h"
#include "components/password_manager/core/browser/password_manager_settings_service.h"
#include "components/password_manager/core/browser/password_manager_util.h"
#include "components/password_manager/core/browser/password_requirements_service.h"
#include "components/password_manager/core/browser/password_store/password_store_backend_error.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_sync_util.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/policy/core/browser/url_list/url_blocklist_manager.h"
#include "components/prefs/pref_service.h"
#include "components/profile_metrics/browser_profile_type.h"
#include "components/safe_browsing/buildflags.h"
#include "components/sessions/content/content_record_password_state.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/site_isolation/site_isolation_policy.h"
#include "components/sync/base/user_selectable_type.h"
#include "components/sync/service/sync_service.h"
#include "components/sync/service/sync_user_settings.h"
#include "content/public/browser/back_forward_cache.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "extensions/buildflags/buildflags.h"
#include "net/base/url_util.h"
#include "net/cert/cert_status_flags.h"
#include "services/metrics/public/cpp/metrics_utils.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
#include "chrome/browser/safe_browsing/advanced_protection_status_manager.h"
#include "chrome/browser/safe_browsing/advanced_protection_status_manager_factory.h"
#include "chrome/browser/safe_browsing/chrome_password_protection_service.h"
#include "chrome/browser/safe_browsing/user_interaction_observer.h"
#endif

#include "chrome/browser/actor/actor_keyed_service.h"
#include "chrome/browser/password_manager/factories/password_counter_factory.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/hats/hats_service.h"
#include "chrome/browser/ui/hats/hats_service_factory.h"
#include "chrome/browser/ui/hats/survey_config.h"
#include "components/password_manager/core/browser/password_counter.h"
#include "components/policy/core/common/features.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/common/constants.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
#include "chrome/browser/enterprise/connectors/reporting/reporting_event_router_factory.h"
#include "components/enterprise/connectors/core/reporting_event_router.h"
#endif  // BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS) || BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
#include "chrome/browser/signin/dice_web_signin_interceptor_factory.h"
#include "chrome/browser/ui/browser.h"
#endif


using autofill::mojom::FocusedFieldType;
using autofill::password_generation::PasswordGenerationType;
using password_manager::BadMessageReason;
using password_manager::ContentPasswordManagerDriverFactory;
using password_manager::FieldInfoManager;
using password_manager::PasswordForm;
using password_manager::PasswordManagerClientHelper;
using password_manager::PasswordManagerDriver;
using password_manager::PasswordManagerMetricsRecorder;
using password_manager::PasswordManagerSetting;
using password_manager::PasswordManagerSettingsService;
using password_manager::PasswordStoreBackendError;
using password_manager::metrics_util::PasswordType;
using sessions::SerializedNavigationEntry;

// Shorten the name to spare line breaks. The code provides enough context
// already.
using Logger = autofill::SavePasswordProgressLogger;

namespace {

#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
constexpr char kPasswordBreachEntryTrigger[] = "PASSWORD_ENTRY";
#endif


}  // namespace

// static
void ChromePasswordManagerClient::CreateForWebContents(
    content::WebContents* contents) {
  if (FromWebContents(contents)) {
    return;
  }

  contents->SetUserData(
      UserDataKey(),
      base::WrapUnique(new ChromePasswordManagerClient(contents)));
}

// static
void ChromePasswordManagerClient::BindPasswordGenerationDriver(
    mojo::PendingAssociatedReceiver<autofill::mojom::PasswordGenerationDriver>
        receiver,
    content::RenderFrameHost* rfh) {
  // [spec] https://wicg.github.io/anonymous-iframe/#spec-autofill
  if (rfh->IsCredentialless()) {
    return;
  }
  auto* web_contents = content::WebContents::FromRenderFrameHost(rfh);
  if (!web_contents) {
    return;
  }
  auto* tab_helper = ChromePasswordManagerClient::FromWebContents(web_contents);
  if (!tab_helper) {
    return;
  }
  tab_helper->password_generation_driver_receivers_.Bind(rfh,
                                                         std::move(receiver));
}

ChromePasswordManagerClient::~ChromePasswordManagerClient() = default;

bool ChromePasswordManagerClient::IsSavingAndFillingEnabled(
    const GURL& url) const {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableAutomation)) {
    // Disable the password saving UI for automated tests. It obscures the
    // page, and there is no API to access (or dismiss) UI bubbles/infobars.
    return false;
  }
  password_manager::PasswordManagerSettingsService* settings_service =
      PasswordManagerSettingsServiceFactory::GetForProfile(GetProfile());
  return settings_service &&
         settings_service->IsSettingEnabled(
             PasswordManagerSetting::kOfferToSavePasswords) &&
         !IsOffTheRecord() && IsFillingEnabled(url);
}

bool ChromePasswordManagerClient::IsFillingEnabled(const GURL& url) const {
  const Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  // Guest profiles don't have PasswordStore at all, so filling should be
  // disabled for them.
  if (!profile || profile->IsGuestSession()) {
    return false;
  }

  // Filling is impossible if password store in unavailable.
  if (!GetProfilePasswordStore()) {
    return false;
  }

  const bool ssl_errors = net::IsCertStatusError(GetMainFrameCertStatus());
  autofill::LogManager* log_manager = GetOrCreateLogManager();
  if (log_manager && log_manager->IsLoggingActive()) {
    password_manager::BrowserSavePasswordProgressLogger logger(log_manager);
    logger.LogBoolean(Logger::STRING_SSL_ERRORS_PRESENT, ssl_errors);
  }
  return !ssl_errors && IsPasswordManagementEnabledForCurrentPage(url);
}

bool ChromePasswordManagerClient::IsFieldFilledWithOtp(
    autofill::FormGlobalId form_id,
    autofill::FieldGlobalId field_id) {
  auto* autofill_client =
      autofill::ContentAutofillClient::FromWebContents(web_contents());
  if (!autofill_client) {
    return false;
  }
  auto* helper = autofill_client->GetPasswordManagerAutofillHelper();
  if (!helper) {
    return false;
  }
  return helper->IsFieldFilledWithOtp(form_id, field_id);
}

bool ChromePasswordManagerClient::IsAutoSignInEnabled() const {
  password_manager::PasswordManagerSettingsService* settings_service =
      PasswordManagerSettingsServiceFactory::GetForProfile(GetProfile());
  return settings_service && settings_service->IsSettingEnabled(
                                 PasswordManagerSetting::kAutoSignIn);
}

void ChromePasswordManagerClient::TriggerUserPerceptionOfPasswordManagerSurvey(
    const std::string& filling_assistance) {
  if (filling_assistance.empty()) {
    return;
  }

  HatsService* hats_service = HatsServiceFactory::GetForProfile(
      GetProfile(), /*create_if_necessary=*/true);
  if (!hats_service) {
    return;
  }

  hats_service->LaunchDelayedSurveyForWebContents(
      kHatsSurveyTriggerAutofillPasswordUserPerception, web_contents(),
      /*timeout_ms=*/5000, /*product_specific_bits_data=*/
      {}, {{"Filling assistance", filling_assistance}});
}

bool ChromePasswordManagerClient::PromptUserToSaveOrUpdatePassword(
    std::unique_ptr<password_manager::PasswordFormManagerForUI> form_to_save,
    bool update_password) {
  // The save password infobar and the password bubble prompt in case of
  // "webby" URLs and do not prompt in case of "non-webby" URLS (e.g. file://).
  if (!CanShowBubbleOnURL(web_contents()->GetLastCommittedURL())) {
    return false;
  }
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (!manage_passwords_ui_controller) {
    return false;
  }
  if (update_password) {
    manage_passwords_ui_controller->OnUpdatePasswordSubmitted(
        std::move(form_to_save));
  } else {
    manage_passwords_ui_controller->OnPasswordSubmitted(
        std::move(form_to_save));
  }
  return true;
}

void ChromePasswordManagerClient::PromptUserToMovePasswordToAccount(
    std::unique_ptr<password_manager::PasswordFormManagerForUI> form_to_move) {
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (manage_passwords_ui_controller) {
    manage_passwords_ui_controller->OnShowMoveToAccountBubble(
        std::move(form_to_move));
  }
}

void ChromePasswordManagerClient::ShowManualFallbackForSaving(
    std::unique_ptr<password_manager::PasswordFormManagerForUI> form_to_save,
    bool has_generated_password,
    bool is_update) {
  if (!CanShowBubbleOnURL(web_contents()->GetLastCommittedURL())) {
    return;
  }

  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  // There may be no UI controller for ChromeOS login page
  // (see crbug.com/41349753).
  if (manage_passwords_ui_controller) {
    manage_passwords_ui_controller->OnShowManualFallbackForSaving(
        std::move(form_to_save), has_generated_password, is_update);
  }
}

void ChromePasswordManagerClient::HideManualFallbackForSaving() {
  if (!CanShowBubbleOnURL(web_contents()->GetLastCommittedURL())) {
    return;
  }

  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  // There may be no UI controller for ChromeOS login page
  // (see crbug.com/41349753).
  if (manage_passwords_ui_controller) {
    manage_passwords_ui_controller->OnHideManualFallbackForSaving();
  }
}

void ChromePasswordManagerClient::FocusedInputChanged(
    PasswordManagerDriver* driver,
    autofill::FieldRendererId focused_field_id,
    autofill::mojom::FocusedFieldType focused_field_type) {
}

bool ChromePasswordManagerClient::PromptUserToChooseCredentials(
    std::vector<std::unique_ptr<PasswordForm>> local_forms,
    const url::Origin& origin,
    CredentialsCallback callback) {
  // Set up an intercept callback if the prompt is zero-clickable (e.g. just one
  // form provided).
  CredentialsCallback intercept = base::BindOnce(
      &PasswordManagerClientHelper::OnCredentialsChosen,
      base::Unretained(&helper_), std::move(callback), local_forms.size() == 1);
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (!manage_passwords_ui_controller) {
    return false;
  }
  return manage_passwords_ui_controller->OnChooseCredentials(
      std::move(local_forms), origin, std::move(intercept));
}


bool ChromePasswordManagerClient::IsReauthBeforeFillingRequired(
    device_reauth::DeviceAuthenticator* authenticator) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  if (!GetLocalStatePrefs() || !GetPrefs() || !authenticator) {
    return false;
  }
  return GetPasswordFeatureManager()
      ->IsBiometricAuthenticationBeforeFillingEnabled();
#else
  return false;
#endif
}

std::unique_ptr<device_reauth::DeviceAuthenticator>
ChromePasswordManagerClient::GetDeviceAuthenticator() {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  device_reauth::DeviceAuthParams params(
      base::Seconds(60), device_reauth::DeviceAuthSource::kPasswordManager);

  return ChromeDeviceAuthenticatorFactory::GetForProfile(
      GetProfile(), web_contents()->GetTopLevelNativeWindow(), params);
#else
  return nullptr;
#endif
}

void ChromePasswordManagerClient::GeneratePassword(
    PasswordGenerationType type) {
  password_manager::ContentPasswordManagerDriver* content_driver =
      password_manager::ContentPasswordManagerDriver::GetForRenderFrameHost(
          web_contents()->GetFocusedFrame());
  if (!content_driver) {
    return;
  }
  // Using unretained pointer is safe because |this| outlives
  // ContentPasswordManagerDriver that holds the connection.
  content_driver->GeneratePassword(base::BindOnce(
      &ChromePasswordManagerClient::GenerationResultAvailable,
      base::Unretained(this), type, content_driver->AsWeakPtrImpl()));
}

void ChromePasswordManagerClient::NotifyUserAutoSignin(
    std::vector<std::unique_ptr<PasswordForm>> local_forms,
    const url::Origin& origin) {
  DCHECK(!local_forms.empty());
  helper_.NotifyUserAutoSignin();
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (manage_passwords_ui_controller) {
    manage_passwords_ui_controller->OnAutoSignin(std::move(local_forms),
                                                 origin);
  }
}

void ChromePasswordManagerClient::NotifyUserCouldBeAutoSignedIn(
    std::unique_ptr<PasswordForm> form) {
  helper_.NotifyUserCouldBeAutoSignedIn(std::move(form));
}

void ChromePasswordManagerClient::NotifySuccessfulLoginWithExistingPassword(
    std::unique_ptr<password_manager::PasswordFormManagerForUI>
        submitted_manager) {
  helper_.NotifySuccessfulLoginWithExistingPassword(
      std::move(submitted_manager));
}

void ChromePasswordManagerClient::NotifyStorePasswordCalled() {
  helper_.NotifyStorePasswordCalled();
  was_store_ever_called_ = true;
}

bool ChromePasswordManagerClient::IsPasswordChangeOngoing() {
  ChromePasswordChangeService* password_change_service =
      PasswordChangeServiceFactory::GetForProfile(GetProfile());
  if (!password_change_service) {
    return false;
  }

  return password_change_service->GetPasswordChangeDelegate(web_contents()) !=
         nullptr;
}

void ChromePasswordManagerClient::NotifyOnSuccessfulLogin(
    const std::u16string& submitted_username) {
  ChromePasswordChangeService* password_change_service =
      PasswordChangeServiceFactory::GetForProfile(GetProfile());
  if (password_change_service &&
      password_change_service->GetPasswordChangeDelegate(web_contents())) {
    password_change_service->GetPasswordChangeDelegate(web_contents())
        ->OnPasswordFormSubmission(web_contents());
  }
}


void ChromePasswordManagerClient::UpdateCredentialCache(
    const url::Origin& origin,
    base::span<const PasswordForm> best_matches,
    bool is_blocklisted,
    std::optional<PasswordStoreBackendError> backend_error) {
}

void ChromePasswordManagerClient::AutomaticPasswordSave(
    std::unique_ptr<password_manager::PasswordFormManagerForUI> saved_form,
    bool is_update_confirmation) {
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (manage_passwords_ui_controller) {
    manage_passwords_ui_controller->OnAutomaticPasswordSave(
        std::move(saved_form), is_update_confirmation);
  }
}

void ChromePasswordManagerClient::PasswordWasAutofilled(
    base::span<const PasswordForm> best_matches,
    const url::Origin& origin,
    base::span<const PasswordForm> federated_matches,
    bool was_autofilled_on_pageload) {
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (!manage_passwords_ui_controller) {
    return;
  }
  manage_passwords_ui_controller->OnPasswordAutofilled(best_matches, origin,
                                                       federated_matches);
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  if (was_autofilled_on_pageload &&
      !IsAuthenticatorRequestWindowUrl(GetLastCommittedURL()) &&
      password_manager_util::
          ShouldShowBiometricAuthenticationBeforeFillingPromo(this)) {
    manage_passwords_ui_controller->OnBiometricAuthenticationForFilling(
        GetPrefs());
  }
#endif
}

void ChromePasswordManagerClient::AutofillHttpAuth(
    const PasswordForm& preferred_match,
    const password_manager::PasswordFormManagerForUI* form_manager) {
  if (web_contents()->GetVisibility() == content::Visibility::HIDDEN) {
    // Do not autofill credentials if current tab is not visible.
    return;
  }

  CHECK(!form_manager->GetBestMatches().empty());

  // Make a copy of best matches as form_manager is not guaranteed to outlive
  // authentication.
  std::vector<PasswordForm> best_matches;
  for (const auto& result : form_manager->GetBestMatches()) {
    best_matches.emplace_back(result);
  }

  httpauth_manager_.Autofill(
      preferred_match, form_manager,
      base::BindOnce(&ChromePasswordManagerClient::PasswordWasAutofilled,
                     weak_ptr_factory_.GetWeakPtr(), std::move(best_matches),
                     url::Origin::Create(form_manager->GetURL()),
                     base::span<const PasswordForm>(),
                     /*was_autofilled_on_pageload=*/false));
}

void ChromePasswordManagerClient::NotifyUserCredentialsWereLeaked(
    password_manager::LeakedPasswordDetails details) {
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (manage_passwords_ui_controller) {
    manage_passwords_ui_controller->OnCredentialLeak(std::move(details));
  }
}

void ChromePasswordManagerClient::NotifyKeychainError() {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (manage_passwords_ui_controller) {
    manage_passwords_ui_controller->OnKeychainError();
  }
#endif
}

PrefService* ChromePasswordManagerClient::GetPrefs() const {
  return GetProfile()->GetPrefs();
}

PrefService* ChromePasswordManagerClient::GetLocalStatePrefs() const {
  return g_browser_process->local_state();
}

const syncer::SyncService* ChromePasswordManagerClient::GetSyncService() const {
  if (SyncServiceFactory::HasSyncService(GetProfile())) {
    return SyncServiceFactory::GetForProfile(GetProfile());
  }
  return nullptr;
}

affiliations::AffiliationService*
ChromePasswordManagerClient::GetAffiliationService() {
  return AffiliationServiceFactory::GetForProfile(GetProfile());
}

password_manager::PasswordStoreInterface*
ChromePasswordManagerClient::GetProfilePasswordStore() const {
  // Always use EXPLICIT_ACCESS as the password manager checks IsOffTheRecord
  // itself when it shouldn't access the PasswordStore.
  return ProfilePasswordStoreFactory::GetForProfile(
             GetProfile(), ServiceAccessType::EXPLICIT_ACCESS)
      .get();
}

password_manager::PasswordStoreInterface*
ChromePasswordManagerClient::GetAccountPasswordStore() const {
  // Always use EXPLICIT_ACCESS as the password manager checks IsOffTheRecord
  // itself when it shouldn't access the PasswordStore.
  return AccountPasswordStoreFactory::GetForProfile(
             GetProfile(), ServiceAccessType::EXPLICIT_ACCESS)
      .get();
}

password_manager::PasswordReuseManager*
ChromePasswordManagerClient::GetPasswordReuseManager() const {
  return PasswordReuseManagerFactory::GetForProfile(GetProfile());
}

password_manager::PasswordChangeServiceInterface*
ChromePasswordManagerClient::GetPasswordChangeService() const {
  return PasswordChangeServiceFactory::GetForProfile(GetProfile());
}

bool ChromePasswordManagerClient::WasLastNavigationHTTPError() const {
  DCHECK(web_contents());

  std::unique_ptr<password_manager::BrowserSavePasswordProgressLogger> logger;
  autofill::LogManager* log_manager = GetOrCreateLogManager();
  if (log_manager && log_manager->IsLoggingActive()) {
    logger =
        std::make_unique<password_manager::BrowserSavePasswordProgressLogger>(
            log_manager);
    logger->LogMessage(Logger::STRING_WAS_LAST_NAVIGATION_HTTP_ERROR_METHOD);
  }

  content::NavigationEntry* entry =
      web_contents()->GetController().GetVisibleEntry();
  if (!entry) {
    return false;
  }
  int http_status_code = entry->GetHttpStatusCode();

  if (logger) {
    logger->LogNumber(Logger::STRING_HTTP_STATUS_CODE, http_status_code);
  }

  if (http_status_code >= 400 && http_status_code < 600) {
    return true;
  }
  return false;
}

net::CertStatus ChromePasswordManagerClient::GetMainFrameCertStatus() const {
  content::NavigationEntry* entry =
      web_contents()->GetController().GetLastCommittedEntry();
  if (!entry) {
    return 0;
  }
  return entry->GetSSL().cert_status;
}

void ChromePasswordManagerClient::PromptUserToEnableAutosignin() {
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (manage_passwords_ui_controller) {
    manage_passwords_ui_controller->OnPromptEnableAutoSignin();
  }
}

bool ChromePasswordManagerClient::IsOffTheRecord() const {
  return web_contents()->GetBrowserContext()->IsOffTheRecord();
}

profile_metrics::BrowserProfileType
ChromePasswordManagerClient::GetProfileType() const {
  content::BrowserContext* browser_context =
      web_contents()->GetBrowserContext();
  return profile_metrics::GetBrowserProfileType(browser_context);
}

const password_manager::PasswordManagerInterface*
ChromePasswordManagerClient::GetPasswordManager() const {
  return &password_manager_;
}

const password_manager::PasswordFeatureManager*
ChromePasswordManagerClient::GetPasswordFeatureManager() const {
  return &password_feature_manager_;
}

password_manager::HttpAuthManager*
ChromePasswordManagerClient::GetHttpAuthManager() {
  return &httpauth_manager_;
}

autofill::AutofillCrowdsourcingManager*
ChromePasswordManagerClient::GetAutofillCrowdsourcingManager() {
  if (auto* client =
          autofill::ContentAutofillClient::FromWebContents(web_contents())) {
    return &client->GetCrowdsourcingManager();
  }
  return nullptr;
}

bool ChromePasswordManagerClient::IsCommittedMainFrameSecure() const {
  return network::IsOriginPotentiallyTrustworthy(
      web_contents()->GetPrimaryMainFrame()->GetLastCommittedOrigin());
}

const GURL& ChromePasswordManagerClient::GetLastCommittedURL() const {
  return web_contents()->GetLastCommittedURL();
}

url::Origin ChromePasswordManagerClient::GetLastCommittedOrigin() const {
  DCHECK(web_contents());
  return web_contents()->GetPrimaryMainFrame()->GetLastCommittedOrigin();
}
const password_manager::CredentialsFilter*
ChromePasswordManagerClient::GetStoreResultFilter() const {
  return &credentials_filter_;
}

autofill::LogManager* ChromePasswordManagerClient::GetCurrentLogManager() {
  return GetOrCreateLogManager();
}

autofill::LogManager* ChromePasswordManagerClient::GetOrCreateLogManager()
    const {
  if (!log_manager_ && log_router_ && log_router_->HasReceivers()) {
    ContentPasswordManagerDriverFactory* driver_factory = GetDriverFactory();
    log_manager_ = autofill::LogManager::Create(
        log_router_, base::BindRepeating(&ContentPasswordManagerDriverFactory::
                                             RequestSendLoggingAvailability,
                                         base::Unretained(driver_factory)));
    driver_factory->RequestSendLoggingAvailability();
  }
  return log_manager_.get();
}

void ChromePasswordManagerClient::AnnotateNavigationEntry(
    bool has_password_field) {
  content::NavigationEntry* entry =
      web_contents()->GetController().GetLastCommittedEntry();
  if (!entry) {
    return;
  }

  SerializedNavigationEntry::PasswordState old_state =
      sessions::GetPasswordStateFromNavigation(entry);

  SerializedNavigationEntry::PasswordState new_state =
      (has_password_field ? SerializedNavigationEntry::HAS_PASSWORD_FIELD
                          : SerializedNavigationEntry::NO_PASSWORD_FIELD);

  if (new_state > old_state) {
    SetPasswordStateInNavigation(new_state, entry);
    if (HistoryTabHelper* history_tab_helper =
            HistoryTabHelper::FromWebContents(web_contents())) {
      history_tab_helper->OnPasswordStateUpdated(new_state);
    }
  }
}

autofill::LanguageCode ChromePasswordManagerClient::GetPageLanguage() const {
  return autofill::LanguageCode();
}

#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
safe_browsing::PasswordProtectionService*
ChromePasswordManagerClient::GetPasswordProtectionService() const {
  return safe_browsing::ChromePasswordProtectionService::
      GetPasswordProtectionService(GetProfile());
}
#endif

#if defined(ON_FOCUS_PING_ENABLED) && BUILDFLAG(SAFE_BROWSING_AVAILABLE)
void ChromePasswordManagerClient::CheckSafeBrowsingReputation(
    const GURL& form_action,
    const GURL& frame_url) {
  safe_browsing::PasswordProtectionService* pps =
      GetPasswordProtectionService();
  if (pps) {
    pps->MaybeStartPasswordFieldOnFocusRequest(
        web_contents(), web_contents()->GetLastCommittedURL(), form_action,
        frame_url);
  }
}
#endif  // defined(ON_FOCUS_PING_ENABLED) && BUILDFLAG(SAFE_BROWSING_AVAILABLE)

#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
void ChromePasswordManagerClient::MaybeReportEnterpriseLoginEvent(
    const GURL& url,
    bool is_federated,
    const url::SchemeHostPort& federated_origin,
    const std::u16string& login_user_name) const {
  enterprise_connectors::ReportingEventRouter* router =
      enterprise_connectors::ReportingEventRouterFactory::GetForBrowserContext(
          GetProfile());
  if (!router) {
    return;
  }

  // The router is responsible for checking if the reporting of this event type
  // is enabled by the admin.
  router->OnLoginEvent(url, is_federated, federated_origin, login_user_name);
}

void ChromePasswordManagerClient::MaybeReportEnterprisePasswordBreachEvent(
    const std::vector<std::pair<GURL, std::u16string>>& identities) const {
  enterprise_connectors::ReportingEventRouter* router =
      enterprise_connectors::ReportingEventRouterFactory::GetForBrowserContext(
          GetProfile());
  if (!router) {
    return;
  }

  // The router is responsible for checking if the reporting of this event type
  // is enabled by the admin.
  router->OnPasswordBreach(kPasswordBreachEntryTrigger, identities);
}
#endif  // BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS) ||  BUILDFLAG(IS_ANDROID)

ukm::SourceId ChromePasswordManagerClient::GetUkmSourceId() {
  return web_contents()->GetPrimaryMainFrame()->GetPageUkmSourceId();
}

PasswordManagerMetricsRecorder*
ChromePasswordManagerClient::GetMetricsRecorder() {
  if (!metrics_recorder_) {
    metrics_recorder_.emplace(GetUkmSourceId());
  }
  return base::OptionalToPtr(metrics_recorder_);
}


password_manager::PasswordRequirementsService*
ChromePasswordManagerClient::GetPasswordRequirementsService() {
  return password_manager::PasswordRequirementsServiceFactory::
      GetForBrowserContext(
          Profile::FromBrowserContext(web_contents()->GetBrowserContext()));
}

favicon::FaviconService* ChromePasswordManagerClient::GetFaviconService() {
  return FaviconServiceFactory::GetForProfile(
      GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
}

signin::IdentityManager* ChromePasswordManagerClient::GetIdentityManager() {
  return IdentityManagerFactory::GetForProfile(
      GetProfile()->GetOriginalProfile());
}

const signin::IdentityManager* ChromePasswordManagerClient::GetIdentityManager()
    const {
  return IdentityManagerFactory::GetForProfile(
      GetProfile()->GetOriginalProfile());
}

FieldInfoManager* ChromePasswordManagerClient::GetFieldInfoManager() const {
  return FieldInfoManagerFactory::GetForProfile(GetProfile());
}

scoped_refptr<network::SharedURLLoaderFactory>
ChromePasswordManagerClient::GetURLLoaderFactory() {
  return GetProfile()
      ->GetDefaultStoragePartition()
      ->GetURLLoaderFactoryForBrowserProcess();
}

network::mojom::NetworkContext* ChromePasswordManagerClient::GetNetworkContext()
    const {
  return GetProfile()->GetDefaultStoragePartition()->GetNetworkContext();
}

void ChromePasswordManagerClient::UpdateFormManagers() {
  password_manager_.UpdateFormManagers();
}

void ChromePasswordManagerClient::NavigateToManagePasswordsPage(
    password_manager::ManagePasswordsReferrer referrer) {
  Browser* browser = chrome::FindBrowserWithTab(web_contents());
  if (!browser) {
    browser = chrome::FindLastActive();
  }
  ::NavigateToManagePasswordsPage(browser, referrer);
}


bool ChromePasswordManagerClient::IsIsolationForPasswordSitesEnabled() const {
  // TODO(crbug.com/41401202): Move the following function (and the feature) to
  // the password component. Then remove IsIsolationForPasswordsSitesEnabled()
  // from the PasswordManagerClient interface.
  return site_isolation::SiteIsolationPolicy::
      IsIsolationForPasswordSitesEnabled();
}

bool ChromePasswordManagerClient::IsNewTabPage() const {
  auto origin = GetLastCommittedURL().DeprecatedGetOriginAsURL();
  return origin ==
             GURL(chrome::kChromeUINewTabPageURL).DeprecatedGetOriginAsURL() ||
         origin == GURL(chrome::kChromeUINewTabURL).DeprecatedGetOriginAsURL();
}

password_manager::WebAuthnCredentialsDelegate*
ChromePasswordManagerClient::GetWebAuthnCredentialsDelegateForDriver(
    PasswordManagerDriver* driver) {
  auto* frame_host =
      static_cast<password_manager::ContentPasswordManagerDriver*>(driver)
          ->render_frame_host();
  return ChromeWebAuthnCredentialsDelegateFactory::GetFactory(web_contents())
      ->GetDelegateForFrame(frame_host);
}


version_info::Channel ChromePasswordManagerClient::GetChannel() const {
  return chrome::GetChannel();
}

void ChromePasswordManagerClient::RefreshPasswordManagerSettingsIfNeeded()
    const {
}

void ChromePasswordManagerClient::OpenPasswordDetailsBubble(
    const PasswordForm& form) {
  PasswordsClientUIDelegate* manage_passwords_ui_controller =
      PasswordsClientUIDelegateFromWebContents(web_contents());
  if (manage_passwords_ui_controller) {
    manage_passwords_ui_controller->OnOpenPasswordDetailsBubble(form);
  }
}

std::unique_ptr<
    password_manager::PasswordCrossDomainConfirmationPopupController>
ChromePasswordManagerClient::ShowCrossDomainConfirmationPopup(
    const gfx::RectF& element_bounds,
    base::i18n::TextDirection text_direction,
    const GURL& domain,
    const std::u16string& password_hostname,
    bool show_warning_text,
    base::OnceClosure confirmation_callback) {
  gfx::Rect client_area = web_contents()->GetContainerBounds();
  gfx::RectF element_bounds_in_screen_space =
      element_bounds + client_area.OffsetFromOrigin();
  auto controller =
      cross_domain_confirmation_popup_factory_for_testing_
          ? cross_domain_confirmation_popup_factory_for_testing_.Run()
          : std::make_unique<
                PasswordCrossDomainConfirmationPopupControllerImpl>(
                web_contents());

  controller->Show(element_bounds_in_screen_space, text_direction, domain,
                   password_hostname, std::move(confirmation_callback),
                   show_warning_text);

  return controller;
}

void ChromePasswordManagerClient::TriggerSignIn(
    signin_metrics::AccessPoint access_point) const {
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  signin_ui_util::ShowReauthForPrimaryAccountWithAuthError(GetProfile(),
                                                           access_point);
#endif
}

void ChromePasswordManagerClient::AutomaticGenerationAvailable(
    const autofill::password_generation::PasswordGenerationUIData& ui_data) {
  content::RenderFrameHost* rfh =
      password_generation_driver_receivers_.GetCurrentTargetFrame();
  if (!password_manager::bad_message::CheckChildProcessSecurityPolicyForURL(
          rfh, ui_data.form_data.url(),
          BadMessageReason::
              CPMD_BAD_ORIGIN_AUTOMATIC_GENERATION_STATUS_CHANGED)) {
    return;
  }
  if (!password_manager::bad_message::CheckFrameNotPrerendering(rfh)) {
    return;
  }
  password_manager::ContentPasswordManagerDriver* driver =
      password_manager::ContentPasswordManagerDriver::GetForRenderFrameHost(
          rfh);
  // This method is called over Mojo via a RenderFrameHostReceiverSet; the
  // current target frame must be live.
  CHECK(driver);
  // This guards against possibility that generation was available on page load
  // but later became unavailable due to inability to save passwords.
  if (!driver->GetPasswordGenerationHelper() ||
      !driver->GetPasswordGenerationHelper()->IsGenerationEnabled(
          /*log_debug_data*/ false)) {
    return;
  }
  // Attempt to show the autofill dropdown UI first.
  gfx::RectF element_bounds_in_top_frame_space =
      TransformToRootCoordinates(driver->render_frame_host(), ui_data.bounds);
  if (driver->GetPasswordAutofillManager()
          ->MaybeShowPasswordSuggestionsWithGeneration(
              element_bounds_in_top_frame_space, ui_data.text_direction,
              /*show_password_suggestions=*/
              ui_data.is_generation_element_password_type)) {
    // (see crbug.com/1338105)
    if (popup_controller_) {
      popup_controller_->GeneratedPasswordRejected();
    }

    driver->SetSuggestionAvailability(
        ui_data.generation_element_id,
        autofill::mojom::AutofillSuggestionAvailability::kAutofillAvailable);
    return;
  }

  if (!ui_data.generation_rejected) {
    ShowPasswordGenerationPopup(PasswordGenerationType::kAutomatic, driver,
                                ui_data);
  }
}

void ChromePasswordManagerClient::PresaveGeneratedPassword(
    const autofill::FormData& form_data,
    const std::u16string& password_value) {
  content::RenderFrameHost* rfh =
      password_generation_driver_receivers_.GetCurrentTargetFrame();
  if (!password_manager::bad_message::CheckFrameNotPrerendering(rfh)) {
    return;
  }

  if (popup_controller_) {
    popup_controller_->UpdateGeneratedPassword(password_value);
  }

  PasswordManagerDriver* driver =
      password_manager::ContentPasswordManagerDriver::GetForRenderFrameHost(
          rfh);
  // This method is called over Mojo via a RenderFrameHostReceiverSet; the
  // current target frame must be live.
  CHECK(driver);
  password_manager_.OnPresaveGeneratedPassword(
      driver,
      password_manager::GetFormWithFrameAndFormMetaData(
          password_generation_driver_receivers_.GetCurrentTargetFrame(),
          form_data),
      password_value);
}

void ChromePasswordManagerClient::PasswordNoLongerGenerated(
    const autofill::FormData& form_data) {
  content::RenderFrameHost* rfh =
      password_generation_driver_receivers_.GetCurrentTargetFrame();
  if (!password_manager::bad_message::CheckFrameNotPrerendering(rfh)) {
    return;
  }
  PasswordManagerDriver* driver =
      password_manager::ContentPasswordManagerDriver::GetForRenderFrameHost(
          rfh);
  // This method is called over Mojo via a RenderFrameHostReceiverSet; the
  // current target frame must be live.
  CHECK(driver);
  password_manager_.OnPasswordNoLongerGenerated(
      driver, password_manager::GetFormWithFrameAndFormMetaData(
                  password_generation_driver_receivers_.GetCurrentTargetFrame(),
                  form_data));

  PasswordGenerationPopupController* controller = popup_controller_.get();
  if (controller &&
      controller->state() ==
          PasswordGenerationPopupController::kEditGeneratedPassword) {
    popup_controller_->GeneratedPasswordRejected();
  }
}

void ChromePasswordManagerClient::ShowPasswordEditingPopup(
    const gfx::RectF& bounds,
    const autofill::FormData& form_data,
    autofill::FieldRendererId field_renderer_id,
    const std::u16string& password_value) {
  content::RenderFrameHost* rfh =
      password_generation_driver_receivers_.GetCurrentTargetFrame();
  if (!password_manager::bad_message::CheckFrameNotPrerendering(rfh)) {
    return;
  }
  if (!password_manager::bad_message::CheckGeneratedPassword(rfh,
                                                             password_value)) {
    return;
  }
  auto* driver =
      password_manager::ContentPasswordManagerDriver::GetForRenderFrameHost(
          rfh);
  // This method is called over Mojo via a RenderFrameHostReceiverSet; the
  // current target frame must be live.
  CHECK(driver);

  gfx::RectF element_bounds_in_screen_space =
      GetBoundsInScreenSpace(TransformToRootCoordinates(
          password_generation_driver_receivers_.GetCurrentTargetFrame(),
          bounds));
  autofill::password_generation::PasswordGenerationUIData ui_data(
      bounds, /*max_length=*/0, /*generation_element=*/std::u16string(),
      field_renderer_id,
      /*is_generation_element_password_type=*/true, base::i18n::TextDirection(),
      password_manager::GetFormWithFrameAndFormMetaData(
          password_generation_driver_receivers_.GetCurrentTargetFrame(),
          form_data),
      /*input_field_empty=*/false);
  popup_controller_ = PasswordGenerationPopupControllerImpl::GetOrCreate(
      popup_controller_, element_bounds_in_screen_space, ui_data,
      driver->AsWeakPtr(), observer_, web_contents(),
      password_generation_driver_receivers_.GetCurrentTargetFrame());
  CHECK(!password_value.empty());
  popup_controller_->UpdateGeneratedPassword(password_value);
  popup_controller_->Show(
      PasswordGenerationPopupController::kEditGeneratedPassword);
}

void ChromePasswordManagerClient::PasswordGenerationRejectedByTyping() {
  content::RenderFrameHost* rfh =
      password_generation_driver_receivers_.GetCurrentTargetFrame();
  if (!password_manager::bad_message::CheckFrameNotPrerendering(rfh)) {
    return;
  }
  if (popup_controller_) {
    popup_controller_->GeneratedPasswordRejected();
  }
}

void ChromePasswordManagerClient::FrameWasScrolled() {
  content::RenderFrameHost* rfh =
      password_generation_driver_receivers_.GetCurrentTargetFrame();
  if (!password_manager::bad_message::CheckFrameNotPrerendering(rfh)) {
    return;
  }
  if (popup_controller_) {
    popup_controller_->FrameWasScrolled();
  }
}

void ChromePasswordManagerClient::GenerationElementLostFocus() {
  content::RenderFrameHost* rfh =
      password_generation_driver_receivers_.GetCurrentTargetFrame();
  if (!password_manager::bad_message::CheckFrameNotPrerendering(rfh)) {
    return;
  }
  // TODO(crbug.com/40629608): Look into removing this since FocusedInputChanged
  // seems to be a good replacement.
  if (popup_controller_) {
    popup_controller_->GenerationElementLostFocus();
  }
}

autofill::PasswordManagerDelegate*
ChromePasswordManagerClient::GetAutofillDelegate(
    const autofill::FieldGlobalId& field_id) {
  if (content::RenderFrameHost* rfh = autofill::FindRenderFrameHostByToken(
          *web_contents(), field_id.frame_token)) {
    if (password_manager::ContentPasswordManagerDriver* driver =
            password_manager::ContentPasswordManagerDriver::
                GetForRenderFrameHost(rfh)) {
      return driver->GetPasswordAutofillManager();
    }
  }
  return nullptr;
}

void ChromePasswordManagerClient::SetTestObserver(
    PasswordGenerationPopupObserver* observer) {
  observer_ = observer;
}

// static
bool ChromePasswordManagerClient::CanShowBubbleOnURL(const GURL& url) {
  std::string scheme = url.GetScheme();
  return (content::ChildProcessSecurityPolicy::GetInstance()->IsWebSafeScheme(
              scheme) &&
#if BUILDFLAG(ENABLE_EXTENSIONS)
          scheme != extensions::kExtensionScheme &&
#endif
          scheme != content::kChromeDevToolsScheme);
}

Profile* ChromePasswordManagerClient::GetProfile() const {
  if (!web_contents()) {
    return nullptr;
  }
  return Profile::FromBrowserContext(web_contents()->GetBrowserContext());
}


credential_management::ContentCredentialManager*
ChromePasswordManagerClient::GetContentCredentialManager() {
  return &content_credential_manager_;
}

password_manager::UndoPasswordChangeController*
ChromePasswordManagerClient::GetUndoPasswordChangeController() {
  return &undo_password_change_controller_;
}

bool ChromePasswordManagerClient::IsActorTaskActive() {
  actor::ActorKeyedService* actor_service =
      actor::ActorKeyedService::Get(GetProfile());
  if (!actor_service) {
    return false;
  }

  const tabs::TabInterface* tab_interface =
      tabs::TabInterface::MaybeGetFromContents(web_contents());
  return tab_interface && actor_service->IsActiveOnTab(*tab_interface);
}

ChromePasswordManagerClient::ChromePasswordManagerClient(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<ChromePasswordManagerClient>(*web_contents),
      password_manager_(this),
      password_feature_manager_(
          GetProfile()->GetPrefs(),
          g_browser_process->local_state(),
          SyncServiceFactory::GetForProfile(GetProfile())),
      httpauth_manager_(this),
      content_credential_manager_(
          std::make_unique<password_manager::CredentialManagerImpl>(this)),
      password_generation_driver_receivers_(web_contents, this),
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
      credentials_filter_(
          this,
          DiceWebSigninInterceptorFactory::GetForProfile(GetProfile())),
#else
      credentials_filter_(this),
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
      log_router_(password_manager::PasswordManagerLogRouterFactory::
                      GetForBrowserContext(GetProfile())),
      helper_(this) {
  ContentPasswordManagerDriverFactory::CreateForWebContents(web_contents, this);

  autofill_managers_observation_.Observe(
      autofill::ContentAutofillClient::FromWebContents(web_contents),
      autofill::ScopedAutofillManagersObservation::InitializationPolicy::
          kObservePreexistingManagers);
}

void ChromePasswordManagerClient::PrimaryPageChanged(content::Page& page) {
  // Logging has no sense on WebUI sites.
  if (GetCurrentLogManager()) {
    log_manager_->SetSuspended(web_contents()->GetWebUI() != nullptr);
  }

  // Send any collected metrics by destroying the metrics recorder.
  metrics_recorder_.reset();

  httpauth_manager_.OnDidFinishMainFrameNavigation();

  // From this point on, the ContentCredentialManager will service API calls
  // in the context of the new WebContents::GetLastCommittedURL, which may
  // very well be cross-origin. Disconnect existing client, and drop pending
  // requests.
  content_credential_manager_.DisconnectBinding();


  // Hide form filling UI on navigating away.
  HideFillingUI();

  undo_password_change_controller_.OnNavigation(
      page.GetMainDocument().GetLastCommittedOrigin(),
      page.GetMainDocument().GetPageUkmSourceId());
}

void ChromePasswordManagerClient::WebContentsDestroyed() {
  // crbug/1090011
  // Drop the connection before the WebContentsObserver destructors are invoked.
  // Other classes may contain callbacks to the Mojo methods. Those callbacks
  // don't like to be destroyed earlier than the pipe itself.
  content_credential_manager_.DisconnectBinding();

}

void ChromePasswordManagerClient::ResourceLoadComplete(
    content::RenderFrameHost* render_frame_host,
    const content::GlobalRequestID& request_id,
    const blink::mojom::ResourceLoadInfo& resource_load_info) {
  if (resource_load_info.method == "POST" &&
      resource_load_info.http_status_code >= 400 &&
      resource_load_info.http_status_code <= 403) {
    password_manager_.OnResourceLoadingFailed(
        password_manager::ContentPasswordManagerDriver::GetForRenderFrameHost(
            render_frame_host),
        resource_load_info.original_url);
  }
}

void ChromePasswordManagerClient::OnFieldTypesDetermined(
    autofill::AutofillManager& manager,
    autofill::FormGlobalId form_id,
    FieldTypeSource source,
    bool small_forms_were_parsed) {
  PropagatePredictionsToPasswordManager(manager, form_id, source);
}

void ChromePasswordManagerClient::PropagatePredictionsToPasswordManager(
    autofill::AutofillManager& manager,
    autofill::FormGlobalId form_id,
    FieldTypeSource source) {
  // `password_manager_` needs to receive data split by renderer forms.
  std::optional<autofill::RendererForms> renderer_forms =
      autofill::RendererFormsFromBrowserForm(manager, form_id);
  if (!renderer_forms.has_value()) {
    return;
  }
  for (const auto& [renderer_form, rfh_id] : renderer_forms.value()) {
    auto* rfh = content::RenderFrameHost::FromID(rfh_id);
    if (!rfh) {
      continue;
    }
    auto* driver =
        password_manager::ContentPasswordManagerDriver::GetForRenderFrameHost(
            rfh);
    if (!driver) {
      continue;
    }

    std::vector<autofill::FieldGlobalId> field_ids_for_renderer_form =
        base::ToVector(renderer_form.fields(),
                       &autofill::FormFieldData::global_id);
    switch (source) {
      case FieldTypeSource::kAutofillServer:
      case FieldTypeSource::kAutofillAiModel:
        password_manager_.ProcessAutofillPredictions(
            driver, renderer_form,
            manager.GetServerPredictionsForForm(form_id,
                                                field_ids_for_renderer_form));
        break;
      case FieldTypeSource::kHeuristicsOrAutocomplete: {
        bool use_model_predictions_for_actor =
            IsActorTaskActive() && base::FeatureList::IsEnabled(
                                       password_manager::features::
                                           kActorLoginLocalClassificationModel);
        if (apply_client_side_prediction_override_ ||
            base::FeatureList::IsEnabled(
                password_manager::features::
                    kApplyClientsideModelPredictionsForPasswordTypes) ||
            use_model_predictions_for_actor) {
          auto model_predictions = manager.GetHeuristicPredictionForForm(
              autofill::HeuristicSource::kPasswordManagerMachineLearning,
              form_id, field_ids_for_renderer_form);
          password_manager_.ProcessClassificationModelPredictions(
              driver, renderer_form, model_predictions);
        }
        break;
      }
    }
  }
}

password_manager::ContentPasswordManagerDriverFactory*
ChromePasswordManagerClient::GetDriverFactory() const {
  return password_manager::ContentPasswordManagerDriverFactory::FromWebContents(
      web_contents());
}

gfx::RectF ChromePasswordManagerClient::GetBoundsInScreenSpace(
    const gfx::RectF& bounds) {
  gfx::Rect client_area = web_contents()->GetContainerBounds();
  return bounds + client_area.OffsetFromOrigin();
}

void ChromePasswordManagerClient::HideFillingUI() {
}

bool ChromePasswordManagerClient::IsPasswordManagementEnabledForCurrentPage(
    const GURL& url) const {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  if (IsPasswordManagerForUrlDisallowedByPolicy(url)) {
    return false;
  }
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)

  bool is_enabled = CanShowBubbleOnURL(url);

  // The password manager is disabled on Google Password Manager page.
  if (url.DeprecatedGetOriginAsURL() ==
      GURL(password_manager::kPasswordManagerAccountDashboardURL)) {
    is_enabled = false;
  }
#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  // SafeBrowsing Delayed Warnings experiment can delay some SafeBrowsing
  // warnings until user interaction. If the current page has a delayed warning,
  // it'll have a user interaction observer attached. Disable password
  // management in that case.
  if (auto* observer =
          safe_browsing::SafeBrowsingUserInteractionObserver::FromWebContents(
              web_contents())) {
    observer->OnPasswordSaveOrAutofillDenied();
    is_enabled = false;
  }
#endif

  autofill::LogManager* log_manager = GetOrCreateLogManager();
  if (log_manager && log_manager->IsLoggingActive()) {
    password_manager::BrowserSavePasswordProgressLogger logger(log_manager);
    logger.LogURL(Logger::STRING_SECURITY_ORIGIN, url);
    logger.LogBoolean(
        Logger::STRING_PASSWORD_MANAGEMENT_ENABLED_FOR_CURRENT_PAGE,
        is_enabled);
  }
  return is_enabled;
}

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
bool ChromePasswordManagerClient::IsPasswordManagerForUrlDisallowedByPolicy(
    const GURL& url) const {
  if (!GetPrefs() || !GetPrefs()->HasPrefPath(
                         password_manager::prefs::kPasswordManagerBlocklist)) {
    return false;
  }
  PasswordManagerBlocklistPolicy* blocklist_policy =
      PasswordManagerBlocklistPolicyFactory::GetForBrowserContext(
          web_contents()->GetBrowserContext());

  if (blocklist_policy &&
      blocklist_policy->GetURLBlocklistState(url) ==
          policy::URLBlocklist::URLBlocklistState::URL_IN_BLOCKLIST) {
    return true;
  }

  return false;
}
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)

void ChromePasswordManagerClient::GenerationResultAvailable(
    PasswordGenerationType type,
    base::WeakPtr<password_manager::ContentPasswordManagerDriver> driver,
    const std::optional<
        autofill::password_generation::PasswordGenerationUIData>& ui_data) {
  if (!ui_data || !driver) {
    return;
  }
  // Check the data because it's a Mojo callback and the input isn't trusted.
  if (!password_manager::bad_message::CheckChildProcessSecurityPolicyForURL(
          driver->render_frame_host(), ui_data->form_data.url(),
          BadMessageReason::
              CPMD_BAD_ORIGIN_SHOW_MANUAL_PASSWORD_GENERATION_POPUP)) {
    return;
  }
  ShowPasswordGenerationPopup(type, driver.get(), *ui_data);
}


void ChromePasswordManagerClient::ShowPasswordGenerationPopup(
    PasswordGenerationType type,
    password_manager::ContentPasswordManagerDriver* driver,
    const autofill::password_generation::PasswordGenerationUIData& ui_data) {
  gfx::RectF element_bounds_in_top_frame_space =
      TransformToRootCoordinates(driver->render_frame_host(), ui_data.bounds);

  gfx::RectF element_bounds_in_screen_space =
      GetBoundsInScreenSpace(element_bounds_in_top_frame_space);
  password_manager_.SetGenerationElementAndTypeForForm(
      driver, ui_data.form_data.renderer_id(), ui_data.generation_element_id,
      type);

  popup_controller_ = PasswordGenerationPopupControllerImpl::GetOrCreate(
      popup_controller_, element_bounds_in_screen_space, ui_data,
      driver->AsWeakPtr(), observer_, web_contents(),
      driver->render_frame_host());

  popup_controller_->GeneratePasswordValue(type);
  popup_controller_->Show(PasswordGenerationPopupController::kOfferGeneration);

  driver->SetSuggestionAvailability(
      ui_data.generation_element_id,
      popup_controller_ && popup_controller_->IsVisible()
          ? autofill::mojom::AutofillSuggestionAvailability::kAutofillAvailable
          : autofill::mojom::AutofillSuggestionAvailability::kNoSuggestions);
}

void ChromePasswordManagerClient::MaybeShowSavePasswordPrimingPromo(
    const GURL& current_url) {
  // If the user has any stored passwords do not show the promo.
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  password_manager::PasswordCounter* counter =
      PasswordCounterFactory::GetForProfile(profile);
  if (counter && counter->autofillable_passwords() > 0) {
    return;
  }

  // If the current page is not eligible for password saving, do not show the
  // promo.
  if (!IsSavingAndFillingEnabled(current_url)) {
    return;
  }

  if (auto* const user_ed =
          BrowserUserEducationInterface::MaybeGetForWebContentsInTab(
              web_contents())) {
    if (signin::IdentityManager* const identity_manager =
            IdentityManagerFactory::GetForProfile(GetProfile())) {
      const bool signed_in =
          identity_manager->HasPrimaryAccount(signin::ConsentLevel::kSignin);
      user_education::FeaturePromoParams params(
          feature_engagement::kIPHPasswordsSavePrimingPromoFeature);
      params.body_params = l10n_util::GetStringUTF16(
          signed_in ? IDS_PASSWORDS_SAVE_PRIMING_PROMO_BODY_SIGNED_IN
                    : IDS_PASSWORDS_SAVE_PRIMING_PROMO_BODY_NOT_SIGNED_IN);
      user_ed->MaybeShowFeaturePromo(std::move(params));
    }
  }
}


gfx::RectF ChromePasswordManagerClient::TransformToRootCoordinates(
    content::RenderFrameHost* frame_host,
    const gfx::RectF& bounds_in_frame_coordinates) {
  content::RenderWidgetHostView* rwhv = frame_host->GetView();
  if (!rwhv) {
    return bounds_in_frame_coordinates;
  }
  return gfx::RectF(rwhv->TransformPointToRootCoordSpaceF(
                        bounds_in_frame_coordinates.origin()),
                    bounds_in_frame_coordinates.size());
}


WEB_CONTENTS_USER_DATA_KEY_IMPL(ChromePasswordManagerClient);
