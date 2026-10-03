// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/feedback_private/feedback_service.h"

#include <memory>
#include <string>
#include <utility>

#include "base/barrier_closure.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/statistics_recorder.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "build/chromeos_buildflags.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/feedback/feedback_data.h"
#include "components/feedback/feedback_report.h"
#include "components/feedback/redaction_tool/redaction_tool.h"
#include "components/feedback/system_logs/system_logs_fetcher.h"
#include "components/feedback/system_logs/system_logs_source.h"
#include "components/variations/net/variations_command_line.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/api/extensions_api_client.h"
#include "extensions/browser/api/feedback_private/feedback_private_delegate.h"
#include "extensions/browser/blob_reader.h"
#include "extensions/browser/extensions_browser_client.h"
#include "net/base/network_change_notifier.h"


#include "base/base64.h"
#include "base/feature_list.h"

namespace extensions {

using system_logs::SysLogsFetcherCallback;
using system_logs::SystemLogsFetcher;
using system_logs::SystemLogsResponse;

namespace {

constexpr char kVariationsStateAttachmentName[] = "variations_state.bin";


void IncludeVariations(scoped_refptr<feedback::FeedbackData> feedback_data) {
  std::vector<uint8_t> ciphertext;
  auto status =
      variations::VariationsCommandLine::GetForCurrentProcess().EncryptToString(
          &ciphertext);
  base::UmaHistogramEnumeration("Variations.VariationsStateEncryptionStatus",
                                status);
  // Variations is at best effort.
  if (status == variations::VariationsStateEncryptionStatus::kSuccess) {
    // This is a binary file.
    feedback_data->AddFile(kVariationsStateAttachmentName,
                           std::string(ciphertext.begin(), ciphertext.end()));
  }
}

void RedactFeedbackData(scoped_refptr<feedback::FeedbackData> feedback_data) {
  redaction::RedactionTool redactor;
  redactor.EnableCreditCardRedaction(true);
  feedback_data->RedactDescription(redactor);
}

}  // namespace

FeedbackService::FeedbackService(content::BrowserContext* browser_context)
    : FeedbackService(
          browser_context,
          ExtensionsAPIClient::Get()->GetFeedbackPrivateDelegate()) {}

FeedbackService::FeedbackService(content::BrowserContext* browser_context,
                                 FeedbackPrivateDelegate* delegate)
    : browser_context_(browser_context), delegate_(delegate) {}

FeedbackService::~FeedbackService() = default;

void FeedbackService::RedactThenSendFeedback(
    const FeedbackParams& params,
    scoped_refptr<feedback::FeedbackData> feedback_data,
    SendFeedbackCallback callback) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE, {base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&RedactFeedbackData, feedback_data),
      base::BindOnce(&FeedbackService::SendFeedback, this, params,
                     feedback_data, std::move(callback)));
}


// After the attached file and screenshot if available are fetched, the callback
// will be invoked. Other further processing will be done in background. The
// report will be sent out once all data are in place.
void FeedbackService::SendFeedback(
    const FeedbackParams& params,
    scoped_refptr<feedback::FeedbackData> feedback_data,
    SendFeedbackCallback callback) {
  auto* browser_client = ExtensionsBrowserClient::Get();
  feedback_data->set_locale(browser_client->GetApplicationLocale());
  feedback_data->set_user_agent(embedder_support::GetUserAgent());

  FetchAttachedFileAndScreenshot(
      feedback_data,
      base::BindOnce(&FeedbackService::OnAttachedFileAndScreenshotFetched, this,
                     params, feedback_data, std::move(callback)));
}

void FeedbackService::FetchAttachedFileAndScreenshot(
    scoped_refptr<feedback::FeedbackData> feedback_data,
    base::OnceClosure callback) {
  const bool must_attach_file = !feedback_data->attached_file_uuid().empty();
  const bool must_attach_screenshot = !feedback_data->screenshot_uuid().empty();
  auto barrier_closure = base::BarrierClosure(
      (must_attach_file ? 1 : 0) + (must_attach_screenshot ? 1 : 0),
      std::move(callback));

  if (must_attach_file) {
    auto populate_attached_file = base::BindOnce(
        [](scoped_refptr<feedback::FeedbackData> feedback_data,
           std::string data, int64_t /*length*/) {
          feedback_data->set_attached_file_uuid(std::string());
          feedback_data->AttachAndCompressFileData(std::move(data));
        },
        feedback_data);

    BlobReader::Read(
        browser_context_->GetBlobRemote(feedback_data->attached_file_uuid()),
        std::move(populate_attached_file).Then(barrier_closure));
  }

  if (must_attach_screenshot) {
    auto populate_screenshot = base::BindOnce(
        [](scoped_refptr<feedback::FeedbackData> feedback_data,
           std::string data, int64_t /*length*/) {
          feedback_data->set_screenshot_uuid(std::string());
          feedback_data->set_image(std::move(data));
        },
        feedback_data);
    BlobReader::Read(
        browser_context_->GetBlobRemote(feedback_data->screenshot_uuid()),
        std::move(populate_screenshot).Then(barrier_closure));
  }
}

void FeedbackService::OnAttachedFileAndScreenshotFetched(
    const FeedbackParams& params,
    scoped_refptr<feedback::FeedbackData> feedback_data,
    SendFeedbackCallback callback) {
  if (params.load_system_info) {
    // The user has chosen to send system logs. They (and on ash more logs)
    // will be loaded in the background without blocking the client.
    FetchSystemInformation(params, feedback_data);
  } else {
    OnAllLogsFetched(params, feedback_data);
  }

  base::UmaHistogramMediumTimes(
      "Feedback.Duration.FormSubmitToConfirmation",
      base::TimeTicks::Now() - params.form_submit_time);

  // True means report will be sent shortly.
  // False means report will be sent once the device is online.
  const bool status = !net::NetworkChangeNotifier::IsOffline();

  UMA_HISTOGRAM_BOOLEAN("Feedback.ReportSending.Online", status);

  // Notify client that data submitted has been received successfully. The
  // report will be sent out once further processing is done.
  std::move(callback).Run(status);
}

void FeedbackService::FetchSystemInformation(
    const FeedbackParams& params,
    scoped_refptr<feedback::FeedbackData> feedback_data) {
  base::TimeTicks fetch_start_time = base::TimeTicks::Now();
  delegate_->FetchSystemInformation(
      browser_context_,
      base::BindOnce(&FeedbackService::OnSystemInformationFetched, this,
                     fetch_start_time, params, feedback_data));
}

void FeedbackService::OnSystemInformationFetched(
    base::TimeTicks fetch_start_time,
    const FeedbackParams& params,
    scoped_refptr<feedback::FeedbackData> feedback_data,
    std::unique_ptr<system_logs::SystemLogsResponse> sys_info) {
  // Fetching is currently slow and could take up to 2 minutes on Chrome OS.
  base::UmaHistogramMediumTimes("Feedback.Duration.FetchSystemInformation",
                                base::TimeTicks::Now() - fetch_start_time);
  if (sys_info) {
    for (auto& itr : *sys_info) {
      if (FeedbackCommon::IncludeInSystemLogs(itr.first,
                                              params.is_internal_email))
        feedback_data->AddLog(std::move(itr.first), std::move(itr.second));
    }
  }
  if (base::FeatureList::IsEnabled(variations::kFeedbackIncludeVariations)) {
    IncludeVariations(feedback_data);
  }

  OnAllLogsFetched(params, feedback_data);
}


void FeedbackService::OnAllLogsFetched(
    const FeedbackParams& params,
    scoped_refptr<feedback::FeedbackData> feedback_data) {
  if (!params.send_tab_titles) {
    feedback_data->RemoveLog(
        feedback::FeedbackReport::kMemUsageWithTabTitlesKey);
  }
  feedback_data->CompressSystemInfo();

  if (params.send_histograms) {
    std::string histograms =
        base::StatisticsRecorder::ToJSON(base::JSON_VERBOSITY_LEVEL_FULL);
    feedback_data->SetAndCompressHistograms(std::move(histograms));
  }

  if (params.send_autofill_metadata) {
    feedback_data->CompressAutofillMetadata();
  }

  DCHECK(feedback_data->attached_file_uuid().empty());
  DCHECK(feedback_data->screenshot_uuid().empty());

  // Signal the feedback object that the data from the feedback page has been
  // filled - the object will manage sending of the actual report.
  feedback_data->OnFeedbackPageDataComplete();
  base::UmaHistogramTimes("Feedback.Duration.FormSubmitToSendQueue",
                          base::TimeTicks::Now() - params.form_submit_time);
}


}  // namespace extensions
