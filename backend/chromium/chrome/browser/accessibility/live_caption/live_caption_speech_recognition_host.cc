// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/accessibility/live_caption/live_caption_speech_recognition_host.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/accessibility/caption_bubble_context_browser.h"
#include "chrome/browser/accessibility/live_caption/live_caption_controller_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/live_caption/live_caption_controller.h"
#include "components/live_caption/pref_names.h"
#include "components/live_caption/views/caption_bubble_model.h"
#include "components/prefs/pref_service.h"
#include "components/soda/soda_installer.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "media/base/media_switches.h"
#include "media/mojo/mojom/speech_recognition_result.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"

namespace {
static constexpr int kWaitKValue = 1;

// The number of consecutive highly confident language identification events
// required to trigger an automatic download of the missing language pack.
static constexpr int kLangIdEventCountThresholdForDownload = 3;

// The number of consecutive highly confident language identification events
// required to extend the uninstallation of the language pack.
static constexpr int kLangIdEventCountThresholdForUninstallationExtension = 10;

std::string RemoveLastKWords(const std::string& input) {
  int words_to_remove = kWaitKValue;

  if (words_to_remove == 0) {
    return input;
  }

  size_t length = input.length();
  size_t last_space_pos = 0;

  while (words_to_remove > 0 && length > 0) {
    length--;
    if (std::isspace(input[length])) {
      words_to_remove--;
      last_space_pos = length;
    }
  }

  if (words_to_remove == 0) {
    return input.substr(0, last_space_pos);
  } else {
    return std::string();
  }
}

// Returns a boolean indicating whether the language is both enabled and not
// already installed.
bool IsLanguageInstallable(std::string_view language_code) {
  for (const auto& language : g_browser_process->local_state()->GetList(
           prefs::kSodaRegisteredLanguagePacks)) {
    if (language.GetString() == language_code) {
      return false;
    }
  }

  return std::ranges::contains(
      speech::SodaInstaller::GetInstance()->GetLiveCaptionEnabledLanguages(),
      language_code);
}

}  // namespace

namespace captions {

// static
void LiveCaptionSpeechRecognitionHost::Create(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<media::mojom::SpeechRecognitionRecognizerClient>
        receiver) {
  CHECK(frame_host);
  // The object is bound to the lifetime of |host| and the mojo
  // connection. See DocumentService for details.
  new LiveCaptionSpeechRecognitionHost(*frame_host, std::move(receiver));
}

LiveCaptionSpeechRecognitionHost::LiveCaptionSpeechRecognitionHost(
    content::RenderFrameHost& frame_host,
    mojo::PendingReceiver<media::mojom::SpeechRecognitionRecognizerClient>
        receiver)
    : DocumentService<media::mojom::SpeechRecognitionRecognizerClient>(
          frame_host,
          std::move(receiver)) {
  content::WebContents* web_contents = GetWebContents();
  if (!web_contents)
    return;
  Observe(web_contents);
  context_ = CaptionBubbleContextBrowser::Create(web_contents);
}

LiveCaptionSpeechRecognitionHost::~LiveCaptionSpeechRecognitionHost() {
  LiveCaptionController* live_caption_controller = GetLiveCaptionController();
  if (live_caption_controller)
    live_caption_controller->OnAudioStreamEnd(&render_frame_host(),
                                              context_.get());
}

void LiveCaptionSpeechRecognitionHost::OnSpeechRecognitionRecognitionEvent(
    const media::SpeechRecognitionResult& result,
    OnSpeechRecognitionRecognitionEventCallback reply) {
  LiveCaptionController* live_caption_controller = GetLiveCaptionController();
  if (!live_caption_controller) {
    std::move(reply).Run(false);
    return;
  }

  std::move(reply).Run(live_caption_controller->DispatchTranscription(
      &render_frame_host(), context_.get(),
      media::SpeechRecognitionResult(
          GetTextForDispatch(result.transcription, result.is_final),
          result.is_final, result.timing_information)));
}

void LiveCaptionSpeechRecognitionHost::OnLanguageIdentificationEvent(
    media::mojom::LanguageIdentificationEventPtr event) {
  LiveCaptionController* live_caption_controller = GetLiveCaptionController();
  if (!live_caption_controller)
    return;

  if (event->asr_switch_result ==
      media::mojom::AsrSwitchResult::kSwitchSucceeded) {
    language_auto_switched_ = true;
  }

  if (auto_detected_language_ != event->language) {
    language_identification_event_count_ = 0;
    auto_detected_language_ = event->language;
  }

  if (event->confidence_level ==
      media::mojom::ConfidenceLevel::kHighlyConfident) {
    language_identification_event_count_++;
  } else {
    language_identification_event_count_ = 0;
  }

  std::optional<speech::SodaLanguagePackComponentConfig> language_config =
      speech::GetLanguageComponentConfigMatchingLanguageSubtag(event->language);
  if (language_config.has_value()) {
    if (language_identification_event_count_ ==
            kLangIdEventCountThresholdForUninstallationExtension &&
        language_auto_switched_) {
      speech::SodaInstaller::GetInstance()->SetUninstallTimer(
          g_browser_process->local_state(),
          language_config.value().language_name);
    }

    if (base::FeatureList::IsEnabled(
            media::kLiveCaptionAutomaticLanguageDownload) &&
        language_identification_event_count_ ==
            kLangIdEventCountThresholdForDownload) {
      if (IsLanguageInstallable(language_config.value().language_name)) {
        // InstallLanguage will only install languages that are not already
        // installed.
        speech::SodaInstaller::GetInstance()->InstallLanguage(
            language_config.value().language_name,
            g_browser_process->local_state());
      }
    }
  }

  live_caption_controller->OnLanguageIdentificationEvent(
      &render_frame_host(), context_.get(), std::move(event));
}

void LiveCaptionSpeechRecognitionHost::OnSpeechRecognitionError() {
  LiveCaptionController* live_caption_controller = GetLiveCaptionController();
  if (live_caption_controller)
    live_caption_controller->OnError(
        context_.get(), CaptionBubbleErrorType::kGeneric,
        base::RepeatingClosure(),
        base::BindRepeating(
            [](CaptionBubbleErrorType error_type, bool checked) {}));
}

void LiveCaptionSpeechRecognitionHost::OnSpeechRecognitionStopped() {
  LiveCaptionController* live_caption_controller = GetLiveCaptionController();
  if (live_caption_controller) {
    live_caption_controller->OnAudioStreamEnd(&render_frame_host(),
                                              context_.get());
  }
}

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_CHROMEOS)
void LiveCaptionSpeechRecognitionHost::MediaEffectivelyFullscreenChanged(
    bool is_fullscreen) {
  LiveCaptionController* live_caption_controller = GetLiveCaptionController();
  if (live_caption_controller)
    live_caption_controller->OnToggleFullscreen(context_.get());
}
#endif

content::WebContents* LiveCaptionSpeechRecognitionHost::GetWebContents() {
  return content::WebContents::FromRenderFrameHost(&render_frame_host());
}

LiveCaptionController*
LiveCaptionSpeechRecognitionHost::GetLiveCaptionController() {
  content::WebContents* web_contents = GetWebContents();
  if (!web_contents)
    return nullptr;
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (!profile)
    return nullptr;
  return LiveCaptionControllerFactory::GetForProfile(profile);
}

std::string LiveCaptionSpeechRecognitionHost::GetTextForDispatch(
    const std::string& input_text,
    bool is_final) {
  std::string text = input_text;
  if (base::FeatureList::IsEnabled(media::kLiveCaptionUseWaitK) && !is_final) {
    text = RemoveLastKWords(text);
  }

  return text;
}
}  // namespace captions
