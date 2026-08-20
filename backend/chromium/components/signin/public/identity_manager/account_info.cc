// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/public/identity_manager/account_info.h"

#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/signin/public/identity_manager/signin_constants.h"
#include "components/signin/public/identity_manager/tribool.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "google_apis/gaia/gaia_id.h"


using signin::constants::kNoHostedDomainFound;

namespace {

// Updates |field| with |new_value| if non-empty and different; if |new_value|
// is equal to |default_value| then it won't override |field| unless it is not
// set. Returns whether |field| was changed.
bool UpdateField(std::string* field,
                 const std::string& new_value,
                 const char* default_value) {
  if (*field == new_value || new_value.empty()) {
    return false;
  }

  if (!field->empty() && default_value && new_value == default_value) {
    return false;
  }

  *field = new_value;
  return true;
}

// Updates |field| with |new_value| if different from the default value.
// Returns whether |field| was changed.
template <typename T>
bool UpdateField(T* field, T new_value, T default_value) {
  if (*field == new_value || new_value == default_value) {
    return false;
  }

  *field = new_value;
  return true;
}

// Updates |field| with |new_value| if non-empty. Returns whether |field| was
// changed.
bool UpdateField(GaiaId* field, const GaiaId& new_value) {
  if (*field == new_value || new_value.empty()) {
    return false;
  }

  *field = new_value;
  return true;
}

// Updates |field| with |new_value| if true. Returns whether |field| was
// changed.
bool UpdateField(bool* field, bool new_value) {
  return UpdateField<bool>(field, new_value, false);
}

// Updates |field| with |new_value| if true. Returns whether |field| was
// changed.
bool UpdateField(signin::Tribool* field, signin::Tribool new_value) {
  return UpdateField<signin::Tribool>(field, new_value,
                                      signin::Tribool::kUnknown);
}

}  // namespace

// This must be a string which can never be a valid picture URL.
const char kNoPictureURLFound[] = "NO_PICTURE_URL";

CoreAccountInfo::CoreAccountInfo() = default;

CoreAccountInfo::~CoreAccountInfo() = default;

CoreAccountInfo::CoreAccountInfo(const CoreAccountInfo& other) = default;

CoreAccountInfo::CoreAccountInfo(CoreAccountInfo&& other) noexcept = default;

CoreAccountInfo& CoreAccountInfo::operator=(const CoreAccountInfo& other) =
    default;

CoreAccountInfo& CoreAccountInfo::operator=(CoreAccountInfo&& other) noexcept =
    default;

bool CoreAccountInfo::IsEmpty() const {
  return account_id.empty() && email.empty() && gaia.empty();
}

AccountInfo::AccountInfo() = default;

AccountInfo::~AccountInfo() = default;

AccountInfo::AccountInfo(const AccountInfo& other) = default;

AccountInfo::AccountInfo(AccountInfo&& other) noexcept = default;

AccountInfo& AccountInfo::operator=(const AccountInfo& other) = default;

AccountInfo& AccountInfo::operator=(AccountInfo&& other) noexcept = default;

const CoreAccountId& AccountInfo::GetAccountId() const {
  return account_id;
}

const GaiaId& AccountInfo::GetGaiaId() const {
  return gaia;
}

std::string_view AccountInfo::GetEmail() const {
  return email;
}

bool AccountInfo::IsUnderAdvancedProtection() const {
  return is_under_advanced_protection;
}

std::optional<std::string_view> AccountInfo::GetFullName() const {
  if (full_name_.empty()) {
    return std::nullopt;
  }
  return full_name_;
}

std::optional<std::string_view> AccountInfo::GetGivenName() const {
  if (given_name_.empty()) {
    return std::nullopt;
  }
  return given_name_;
}

std::optional<std::string_view> AccountInfo::GetHostedDomain() const {
  if (hosted_domain_.empty()) {
    return std::nullopt;
  }
  if (hosted_domain_ == kNoHostedDomainFound) {
    return base::EmptyString();
  }
  return hosted_domain_;
}

std::optional<std::string_view> AccountInfo::GetAvatarUrl() const {
  if (picture_url_.empty()) {
    return std::nullopt;
  }
  if (picture_url_ == kNoPictureURLFound) {
    return base::EmptyString();
  }
  return picture_url_;
}

std::optional<std::string_view>
AccountInfo::GetLastDownloadedAvatarUrlWithSize() const {
  if (last_downloaded_image_url_with_size_.empty()) {
    return std::nullopt;
  }
  return last_downloaded_image_url_with_size_;
}

std::optional<gfx::Image> AccountInfo::GetAvatarImage() const {
  if (account_image.IsEmpty()) {
    return std::nullopt;
  }
  return account_image;
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
std::optional<signin_metrics::AccessPoint>
AccountInfo::GetLastAuthenticationAccessPoint() const {
  return access_point;
}
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

const AccountCapabilities& AccountInfo::GetAccountCapabilities() const {
  return capabilities;
}

signin::Tribool AccountInfo::IsChildAccount() const {
  return is_child_account_;
}

std::optional<std::string_view> AccountInfo::GetLocale() const {
  if (locale.empty()) {
    return std::nullopt;
  }
  return locale;
}

bool AccountInfo::IsEmpty() const {
  return CoreAccountInfo::IsEmpty() && hosted_domain_.empty() &&
         full_name_.empty() && given_name_.empty() && locale.empty() &&
         picture_url_.empty();
}

bool AccountInfo::IsValid() const {
  return !account_id.empty() && !email.empty() && !gaia.empty() &&
         !hosted_domain_.empty() && !full_name_.empty() &&
         !given_name_.empty() && !picture_url_.empty();
}

bool AccountInfo::UpdateWith(const AccountInfo& other) {
  if (account_id != other.account_id) {
    // Only updates with a compatible AccountInfo.
    return false;
  }

  bool modified = false;
  modified |= UpdateField(&gaia, other.gaia);
  modified |= UpdateField(&email, other.email, nullptr);
  modified |= UpdateField(&full_name_, other.full_name_, nullptr);
  modified |= UpdateField(&given_name_, other.given_name_, nullptr);
  modified |=
      UpdateField(&hosted_domain_, other.hosted_domain_, kNoHostedDomainFound);
  modified |= UpdateField(&locale, other.locale, nullptr);
  modified |=
      UpdateField(&picture_url_, other.picture_url_, kNoPictureURLFound);
  modified |= UpdateField(&is_child_account_, other.is_child_account_);
  modified |= UpdateField(&access_point, other.access_point,
                          std::optional<signin_metrics::AccessPoint>());
  modified |= UpdateField(&is_under_advanced_protection,
                          other.is_under_advanced_protection);
  modified |= capabilities.UpdateWith(other.capabilities);

  return modified;
}

// static
signin::Tribool AccountInfo::IsManaged(const std::string& hosted_domain) {
  return hosted_domain.empty()
             ? signin::Tribool::kUnknown
             : signin::TriboolFromBool(hosted_domain != kNoHostedDomainFound);
}

bool AccountInfo::IsMemberOfFlexOrg() const {
  return capabilities.is_subject_to_enterprise_features() ==
             signin::Tribool::kTrue &&
         IsManaged(hosted_domain_) != signin::Tribool::kTrue;
}

signin::Tribool AccountInfo::IsManaged() const {
  return IsManaged(hosted_domain_);
}

signin::Tribool AccountInfo::CanApplyAccountLevelEnterprisePolicies() const {
  return IsManaged();
}

bool AccountInfo::IsEduAccount() const {
  return capabilities.can_use_edu_features() == signin::Tribool::kTrue &&
         IsManaged() == signin::Tribool::kTrue;
}

bool AccountInfo::CanHaveEmailAddressDisplayed() const {
  return capabilities.can_have_email_address_displayed() ==
             signin::Tribool::kTrue ||
         capabilities.can_have_email_address_displayed() ==
             signin::Tribool::kUnknown;
}

AccountInfo::Builder::Builder(const GaiaId& gaia_id, std::string_view email) {
  CHECK(!gaia_id.empty());
  CHECK(!email.empty());
  account_info_.gaia = gaia_id;
  account_info_.email = std::string(email);
}

AccountInfo::Builder::Builder(const CoreAccountInfo& core_account_info) {
  // Ideally, this code should test that both gaia_id and email aren't empty
  // but some flows (like `AccountFetcherService`) create `AccountInfo` objects
  // with `account_id` only.
  // Allow modifications of incomplete AccountInfo objects for now.
  // TODO(crbug.com/40283608): verify that `gaia_id` and `email` aren't empty
  // when the account fetcher case is fixed.
  CHECK(!core_account_info.IsEmpty());
  account_info_.account_id = core_account_info.account_id;
  account_info_.gaia = core_account_info.gaia;
  account_info_.email = core_account_info.email;
  account_info_.is_under_advanced_protection =
      core_account_info.is_under_advanced_protection;
}

AccountInfo::Builder::Builder(const AccountInfo& account_info)
    : account_info_(account_info) {
  // Ideally, this code should test that both gaia_id and email aren't empty
  // but some flows (like `AccountFetcherService`) create `AccountInfo` objects
  // with `account_id` only.
  // Allow modifications of incomplete AccountInfo objects for now.
  // TODO(crbug.com/40283608): verify that `gaia_id` and `email` aren't empty
  // when the account fetcher case is fixed.
  CHECK(!account_info.IsEmpty());
}

AccountInfo::Builder::~Builder() = default;

AccountInfo AccountInfo::Builder::Build() {
  return std::move(account_info_);
}

AccountInfo::Builder& AccountInfo::Builder::SetEmail(std::string_view email) {
  CHECK(!email.empty());
  account_info_.email = std::string(email);
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetAccountId(
    const CoreAccountId& account_id) {
  CHECK(!account_id.empty());
  account_info_.account_id = account_id;
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetIsUnderAdvancedProtection(
    bool is_under_advanced_protection) {
  account_info_.is_under_advanced_protection = is_under_advanced_protection;
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetFullName(
    std::string_view full_name) {
  CHECK(!full_name.empty());
  account_info_.full_name_ = std::string(full_name);
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetGivenName(
    std::string_view given_name) {
  CHECK(!given_name.empty());
  account_info_.given_name_ = std::string(given_name);
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetLastDownloadedAvatarUrlWithSize(
    std::string_view avatar_url_with_size) {
  account_info_.last_downloaded_image_url_with_size_ =
      std::string(avatar_url_with_size);
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetAvatarImage(
    const gfx::Image& avatar_image) {
  account_info_.account_image = avatar_image;
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetLocale(
    std::string_view locale_val) {
  CHECK(!locale_val.empty());
  account_info_.locale = std::string(locale_val);
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetHostedDomain(
    std::string_view hosted_domain_val) {
  account_info_.hosted_domain_ = hosted_domain_val.empty()
                                     ? kNoHostedDomainFound
                                     : std::string(hosted_domain_val);
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetAvatarUrl(
    std::string_view avatar_url) {
  account_info_.picture_url_ =
      avatar_url.empty() ? kNoPictureURLFound : std::string(avatar_url);
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetLastAuthenticationAccessPoint(
    signin_metrics::AccessPoint access_point_val) {
  account_info_.access_point = access_point_val;
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::SetIsChildAccount(
    signin::Tribool is_child_account) {
  account_info_.is_child_account_ = is_child_account;
  return *this;
}

AccountInfo::Builder& AccountInfo::Builder::UpdateAccountCapabilitiesWith(
    const AccountCapabilities& other) {
  account_info_.capabilities.UpdateWith(other);
  return *this;
}

AccountInfo::Builder::Builder() = default;

// static
AccountInfo::Builder AccountInfo::Builder::CreateWithPossiblyEmptyGaiaId(
    const GaiaId& gaia_id,
    std::string_view email) {
  CHECK(!email.empty());
  AccountInfo::Builder builder;
  builder.account_info_.gaia = gaia_id;
  builder.account_info_.email = email;
  return builder;
}

bool operator==(const CoreAccountInfo& l, const CoreAccountInfo& r) {
  return l.account_id == r.account_id && l.gaia == r.gaia &&
         gaia::AreEmailsSame(l.email, r.email) &&
         l.is_under_advanced_protection == r.is_under_advanced_protection;
}

std::ostream& operator<<(std::ostream& os, const CoreAccountInfo& account) {
  os << "account_id: " << account.account_id << ", gaia: " << account.gaia
     << ", email: " << account.email << ", adv_prot: " << std::boolalpha
     << account.is_under_advanced_protection;
  return os;
}
