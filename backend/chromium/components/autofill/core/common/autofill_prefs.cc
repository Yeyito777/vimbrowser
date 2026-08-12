// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/common/autofill_prefs.h"

#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "build/build_config.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"


namespace autofill::prefs {

namespace {

// Deprecated pref names. Kept around to clear them, until they are removed one
// year later.
constexpr char kAutofillRanExtraDeduplication[] =
    "autofill.ran_extra_deduplication";

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  // Synced prefs. Used for cross-device choices, e.g., credit card Autofill.
  registry->RegisterBooleanPref(
      kAutofillProfileEnabled, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterIntegerPref(
      kAutofillLastVersionDeduped, 0,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      kAutofillAiIdentityEntitiesEnabled, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      kAutofillAiSyncedOptInStatus, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterIntegerPref(
      kAutofillAiLastVersionDeduped, 0,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      kAutofillAiTravelEntitiesEnabled, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_IOS)
  registry->RegisterBooleanPref(
      kAutofillAiReauthBeforeViewingSensitiveData, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_ANDROID) ||
        // BUILDFLAG(IS_CHROMEOS)
  registry->RegisterBooleanPref(
      kAutofillHasSeenIban, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      kAutofillCreditCardEnabled, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      kAutofillPaymentCvcStorage, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      kAutofillPaymentCardBenefits, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);

  registry->RegisterStringPref(
      kAutofillNameAndEmailProfileSignature, "",
      user_prefs::PrefRegistrySyncable::SYNCABLE_PRIORITY_PREF);
  registry->RegisterIntegerPref(
      kAutofillNameAndEmailProfileNotSelectedCounter, 0,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PRIORITY_PREF);
  registry->RegisterBooleanPref(
      kAutofillWasNameAndEmailProfileUsed, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PRIORITY_PREF);

  // Non-synced prefs. Used for per-device choices, e.g., signin promo.
  registry->RegisterDictionaryPref(kAutofillAiOptInStatus);
  registry->RegisterBooleanPref(kAutofillCreditCardFidoAuthEnabled, false);
  registry->RegisterIntegerPref(kAutocompleteLastVersionRetentionPolicy, 0);
  registry->RegisterStringPref(kAutofillUploadEncodingSeed, "");
  registry->RegisterDictionaryPref(kAutofillVoteUploadEvents);
  registry->RegisterDictionaryPref(
      kAutofillVoteSecondaryFormSignatureUploadEvents);
  registry->RegisterDictionaryPref(kAutofillMetadataUploadEvents);
  registry->RegisterTimePref(kAutofillUploadEventsLastResetTimestamp, {});
  registry->RegisterDictionaryPref(kAutofillSyncTransportOptIn);
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS)
  registry->RegisterBooleanPref(kAutofillPaymentMethodsMandatoryReauth, false);
  registry->RegisterIntegerPref(
      kAutofillPaymentMethodsMandatoryReauthPromoShownCounter, 0);
#elif BUILDFLAG(IS_IOS)
  registry->RegisterBooleanPref(kAutofillPaymentMethodsMandatoryReauth, true);
#endif


  registry->RegisterBooleanPref(
      kAutofillBnplEnabled, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      kAutofillHasSeenBnpl, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);

  registry->RegisterBooleanPref(
      kAutofillAmountExtractionAiTermsSeen, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);

  if (base::FeatureList::IsEnabled(
          features::kAutofillEnableSupportForHomeAndWork)) {
    registry->RegisterDictionaryPref(
        kAutofillHomeMetadata,
        user_prefs::PrefRegistrySyncable::SYNCABLE_PRIORITY_PREF);
    registry->RegisterDictionaryPref(
        kAutofillWorkMetadata,
        user_prefs::PrefRegistrySyncable::SYNCABLE_PRIORITY_PREF);
    registry->RegisterIntegerPref(kAutofillSilentUpdatesToHomeAddress, 0);
    registry->RegisterIntegerPref(kAutofillSilentUpdatesToWorkAddress, 0);
  }

  // Deprecated prefs registered for migration.
  registry->RegisterBooleanPref(kAutofillEnabledDeprecated, true);
  registry->RegisterStringPref(kAutofillAblationSeedPref, "");
  registry->RegisterBooleanPref(kAutofillRanExtraDeduplication, false);
  // Don't add new prefs here. Add them before any deprecated prefs instead.
}

void MigrateDeprecatedAutofillPrefs(PrefService* pref_service) {
  // Added 03/2025
  pref_service->ClearPref(kAutofillEnabledDeprecated);
  // Added 01/2026
  pref_service->ClearPref(kAutofillRanExtraDeduplication);
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kAutofillAblationSeedPref, "");
}

bool IsAutocompleteEnabled(const PrefService* prefs) {
  return IsAutofillProfileEnabled(prefs);
}

bool IsCreditCardFIDOAuthEnabled(PrefService* prefs) {
  return prefs->GetBoolean(kAutofillCreditCardFidoAuthEnabled);
}

void SetCreditCardFIDOAuthEnabled(PrefService* prefs, bool enabled) {
  prefs->SetBoolean(kAutofillCreditCardFidoAuthEnabled, enabled);
}

bool IsAutofillPaymentMethodsEnabled(const PrefService* prefs) {
  return prefs->GetBoolean(kAutofillCreditCardEnabled);
}

void SetAutofillPaymentMethodsEnabled(PrefService* prefs, bool enabled) {
  prefs->SetBoolean(kAutofillCreditCardEnabled, enabled);
}

bool HasSeenIban(const PrefService* prefs) {
  return prefs->GetBoolean(kAutofillHasSeenIban);
}

// If called, always sets the pref to true, and once true, it will follow the
// user around forever.
void SetAutofillHasSeenIban(PrefService* prefs) {
  prefs->SetBoolean(kAutofillHasSeenIban, true);
}

bool IsAutofillProfileManaged(const PrefService* prefs) {
  return prefs->IsManagedPreference(kAutofillProfileEnabled);
}

bool IsAutofillCreditCardManaged(const PrefService* prefs) {
  return prefs->IsManagedPreference(kAutofillCreditCardEnabled);
}

bool IsAutofillProfileEnabled(const PrefService* prefs) {
  return prefs->GetBoolean(kAutofillProfileEnabled);
}

void SetAutofillProfileEnabled(PrefService* prefs, bool enabled) {
  if (prefs->GetBoolean(kAutofillProfileEnabled) == enabled) {
    return;
  }

  prefs->SetBoolean(kAutofillProfileEnabled, enabled);
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  //
  // LINT.IfChange(AutofillAddressOptInChange)
  enum class AutofillAddressOptInChange {
    kOptIn = 0,
    kOptOut = 1,
    kMaxValue = kOptOut
  };
  // LINT.ThenChange(/tools/metrics/histograms/metadata/autofill/enums.xml:AutofillAddressOptInChange)
  using enum AutofillAddressOptInChange;
  base::UmaHistogramEnumeration("Autofill.Address.IsEnabled.Change",
                                enabled ? kOptIn : kOptOut);
}

bool IsAutofillAiSyncedOptInStatusEnabled(const PrefService* prefs) {
  return prefs->GetBoolean(kAutofillAiSyncedOptInStatus);
}

void SetAutofillAiSyncedOptInStatus(PrefService* prefs, bool enabled) {
  prefs->SetBoolean(kAutofillAiSyncedOptInStatus, enabled);
}

bool IsAutofillAiReauthBeforeFillingEnabled(const PrefService* prefs) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_IOS)
  return prefs->GetBoolean(kAutofillAiReauthBeforeViewingSensitiveData) &&
         base::FeatureList::IsEnabled(features::kAutofillAiReauthRequired);
#else
  return false;
#endif
}

void SetAutofillAiReauthBeforeFillingEnabled(PrefService* prefs, bool enabled) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_IOS)
  prefs->SetBoolean(kAutofillAiReauthBeforeViewingSensitiveData, enabled);
#endif
}

bool IsPaymentMethodsMandatoryReauthEnabled(const PrefService* prefs) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_IOS)
  return prefs->GetBoolean(kAutofillPaymentMethodsMandatoryReauth);
#elif BUILDFLAG(IS_CHROMEOS)
  if (!base::FeatureList::IsEnabled(
          features::kAutofillEnablePaymentsMandatoryReauthChromeOs)) {
    return false;
  }
  return prefs->GetBoolean(kAutofillPaymentMethodsMandatoryReauth);
#else
  return false;
#endif
}

void SetPaymentMethodsMandatoryReauthEnabled(PrefService* prefs, bool enabled) {

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_IOS)
  prefs->SetBoolean(kAutofillPaymentMethodsMandatoryReauth, enabled);
#elif BUILDFLAG(IS_CHROMEOS)
  if (!base::FeatureList::IsEnabled(
          features::kAutofillEnablePaymentsMandatoryReauthChromeOs)) {
    return;
  }
  prefs->SetBoolean(kAutofillPaymentMethodsMandatoryReauth, enabled);
#endif
}

bool IsPaymentMethodsMandatoryReauthSetExplicitly(const PrefService* prefs) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  return prefs->GetUserPrefValue(kAutofillPaymentMethodsMandatoryReauth) !=
         nullptr;
#elif BUILDFLAG(IS_CHROMEOS)
  if (!base::FeatureList::IsEnabled(
          features::kAutofillEnablePaymentsMandatoryReauthChromeOs)) {
    return false;
  }
  return prefs->GetUserPrefValue(kAutofillPaymentMethodsMandatoryReauth) !=
         nullptr;
#else
  return false;
#endif
}

bool IsPaymentMethodsMandatoryReauthPromoShownCounterBelowMaxCap(
    const PrefService* prefs) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  return prefs->GetInteger(
             kAutofillPaymentMethodsMandatoryReauthPromoShownCounter) <
         kMaxValueForMandatoryReauthPromoShownCounter;
#elif BUILDFLAG(IS_CHROMEOS)
  if (!base::FeatureList::IsEnabled(
          features::kAutofillEnablePaymentsMandatoryReauthChromeOs)) {
    return false;
  }
  return prefs->GetInteger(
             kAutofillPaymentMethodsMandatoryReauthPromoShownCounter) <
         kMaxValueForMandatoryReauthPromoShownCounter;
#else
  return false;
#endif
}

void IncrementPaymentMethodsMandatoryReauthPromoShownCounter(
    PrefService* prefs) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  if (prefs->GetInteger(
          kAutofillPaymentMethodsMandatoryReauthPromoShownCounter) >=
      kMaxValueForMandatoryReauthPromoShownCounter) {
    return;
  }

  prefs->SetInteger(
      kAutofillPaymentMethodsMandatoryReauthPromoShownCounter,
      prefs->GetInteger(
          kAutofillPaymentMethodsMandatoryReauthPromoShownCounter) +
          1);
#elif BUILDFLAG(IS_CHROMEOS)
  if (!base::FeatureList::IsEnabled(
          features::kAutofillEnablePaymentsMandatoryReauthChromeOs)) {
    return;
  }

  if (prefs->GetInteger(
          kAutofillPaymentMethodsMandatoryReauthPromoShownCounter) >=
      kMaxValueForMandatoryReauthPromoShownCounter) {
    return;
  }

  prefs->SetInteger(
      kAutofillPaymentMethodsMandatoryReauthPromoShownCounter,
      prefs->GetInteger(
          kAutofillPaymentMethodsMandatoryReauthPromoShownCounter) +
          1);
#endif
}

bool IsPaymentCvcStorageEnabled(const PrefService* prefs) {
  return prefs->GetBoolean(kAutofillPaymentCvcStorage);
}

void SetPaymentCvcStorage(PrefService* prefs, bool value) {
  prefs->SetBoolean(kAutofillPaymentCvcStorage, value);
}

bool IsPaymentCardBenefitsEnabled(const PrefService* prefs) {
  return prefs->GetBoolean(kAutofillPaymentCardBenefits);
}

void SetPaymentCardBenefits(PrefService* prefs, bool value) {
  prefs->SetBoolean(kAutofillPaymentCardBenefits, value);
}

void ClearSyncTransportOptIns(PrefService* prefs) {
  prefs->SetDict(kAutofillSyncTransportOptIn, base::DictValue());
}

void SetFacilitatedPaymentsEwallet(PrefService* prefs, bool value) {
}

bool IsFacilitatedPaymentsEwalletEnabled(const PrefService* prefs) {
  return false;
}

void SetFacilitatedPaymentsPix(PrefService* prefs, bool value) {
}

bool IsFacilitatedPaymentsPixEnabled(const PrefService* prefs) {
  return false;
}

void SetFacilitatedPaymentsPixAccountLinking(PrefService* prefs, bool value) {
}

bool IsFacilitatedPaymentsPixAccountLinkingEnabled(const PrefService* prefs) {
  // Default to false on other platforms as the feature is Android-only.
  return false;
}

bool IsFacilitatedPaymentsA2AEnabled(const PrefService* prefs) {
  // Default to false on other platforms as the feature is Android-only.
  return false;
}

void SetFacilitatedPaymentsA2ATriggeredOnce(PrefService* prefs, bool value) {
}

void SetAutofillBnplEnabled(PrefService* prefs, bool value) {
  prefs->SetBoolean(kAutofillBnplEnabled, value);
}

bool IsAutofillBnplEnabled(const PrefService* prefs) {
  return prefs->GetBoolean(kAutofillBnplEnabled);
}

// If called, always sets the pref to true, and once true, it will follow the
// user around forever.
void SetAutofillHasSeenBnpl(PrefService* prefs) {
  prefs->SetBoolean(kAutofillHasSeenBnpl, true);
}

bool HasSeenBnpl(const PrefService* prefs) {
  return prefs->GetBoolean(kAutofillHasSeenBnpl);
}

// If called, always sets the pref to true, and once true, it will follow the
// user around forever.
void SetAutofillAmountExtractionAiTermsSeen(PrefService* prefs) {
  if (base::FeatureList::IsEnabled(
          features::kAutofillEnableAiBasedAmountExtraction)) {
    prefs->SetBoolean(kAutofillAmountExtractionAiTermsSeen, true);
  }
}

bool AmountExtractionAiTermsSeen(const PrefService* prefs) {
  return base::FeatureList::IsEnabled(
             features::kAutofillEnableAiBasedAmountExtraction) &&
         prefs->GetBoolean(kAutofillAmountExtractionAiTermsSeen);
}
}  // namespace autofill::prefs
