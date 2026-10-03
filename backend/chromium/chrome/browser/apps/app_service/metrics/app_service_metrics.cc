// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_service/metrics/app_service_metrics.h"

#include "chrome/common/web_app_id_constants.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/time/time.h"
#include "build/branding_buildflags.h"
#include "chrome/common/extensions/extension_constants.h"
#include "components/app_constants/constants.h"
#include "components/services/app_service/public/cpp/app_launch_util.h"
#include "extensions/common/constants.h"


namespace {

void RecordDefaultAppLaunch(apps::DefaultAppName default_app_name,
                            apps::LaunchSource launch_source) {
  switch (launch_source) {
    case apps::LaunchSource::kUnknown:
    case apps::LaunchSource::kFromParentalControls:
    case apps::LaunchSource::kFromTest:
      return;
    case apps::LaunchSource::kFromAppListGrid:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromAppListGrid",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromAppListGridContextMenu:
      base::UmaHistogramEnumeration(
          "Apps.DefaultAppLaunch.FromAppListGridContextMenu", default_app_name);
      break;
    case apps::LaunchSource::kFromAppListQuery:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromAppListQuery",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromAppListQueryContextMenu:
      base::UmaHistogramEnumeration(
          "Apps.DefaultAppLaunch.FromAppListQueryContextMenu",
          default_app_name);
      break;
    case apps::LaunchSource::kFromAppListRecommendation:
      base::UmaHistogramEnumeration(
          "Apps.DefaultAppLaunch.FromAppListRecommendation", default_app_name);
      break;
    case apps::LaunchSource::kFromShelf:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromShelf",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromFileManager:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromFileManager",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromLink:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromLink",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromOmnibox:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromOmnibox",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromChromeInternal:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromChromeInternal",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromKeyboard:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromKeyboard",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromOtherApp:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromOtherApp",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromMenu:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromMenu",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromInstalledNotification:
      base::UmaHistogramEnumeration(
          "Apps.DefaultAppLaunch.FromInstalledNotification", default_app_name);
      break;
    case apps::LaunchSource::kFromArc:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromArc",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromSharesheet:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromSharesheet",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromReleaseNotesNotification:
      base::UmaHistogramEnumeration(
          "Apps.DefaultAppLaunch.FromReleaseNotesNotification",
          default_app_name);
      break;
    case apps::LaunchSource::kFromFullRestore:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromFullRestore",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromSmartTextContextMenu:
      base::UmaHistogramEnumeration(
          "Apps.DefaultAppLaunch.FromSmartTextContextMenu", default_app_name);
      break;
    case apps::LaunchSource::kFromDiscoverTabNotification:
      base::UmaHistogramEnumeration(
          "Apps.DefaultAppLaunch.FromDiscoverTabNotification",
          default_app_name);
      break;
    case apps::LaunchSource::kFromManagementApi:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromManagementApi",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromKiosk:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromKiosk",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromNewTabPage:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromNewTabPage",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromIntentUrl:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromIntentUrl",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromOsLogin:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromOsLogin",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromProtocolHandler:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromProtocolHandler",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromUrlHandler:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromUrlHandler",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromLockScreen:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromLockScreen",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromSysTrayCalendar:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromSysTrayCalendar",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromInstaller:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromInstaller",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromFirstRun:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromFirstRun",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromWelcomeTour:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromWelcomeTour",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromFocusMode:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromFocusMode",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromSparky:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromSparky",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromWebInstallApi:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromWebInstallApi",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromMigration:
      base::UmaHistogramEnumeration("Apps.DefaultAppLaunch.FromMigration",
                                    default_app_name);
      break;
    case apps::LaunchSource::kFromCommandLine:
    case apps::LaunchSource::kFromBackgroundMode:
    case apps::LaunchSource::kFromAppHomePage:
    case apps::LaunchSource::kFromReparenting:
    case apps::LaunchSource::kFromProfileMenu:
    case apps::LaunchSource::kFromNavigationCapturing:
      NOTREACHED();
  }
}


}  // namespace

namespace apps {

std::optional<apps::DefaultAppName> AppIdToName(const std::string& app_id) {
  if (const std::optional<DefaultAppName> app_name =
          PreinstalledWebAppIdToName(app_id)) {
    return app_name;
  }


  if (app_id == extension_misc::kCalculatorAppId) {
    // The legacy calculator chrome app.
    return DefaultAppName::kCalculatorChromeApp;
  } else if (app_id == extension_misc::kTextEditorAppId) {
    return DefaultAppName::kText;
  } else if (app_id == app_constants::kChromeAppId) {
    return DefaultAppName::kChrome;
  } else if (app_id == extension_misc::kGoogleDocsAppId) {
    return DefaultAppName::kDocs;
  } else if (app_id == extension_misc::kGoogleDriveAppId) {
    return DefaultAppName::kDrive;
  } else if (app_id == extension_misc::kGoogleKeepAppId) {
    return DefaultAppName::kKeep;
  } else if (app_id == extension_misc::kGoogleSheetsAppId) {
    return DefaultAppName::kSheets;
  } else if (app_id == extension_misc::kGoogleSlidesAppId) {
    return DefaultAppName::kSlides;
  } else if (app_id == extensions::kWebStoreAppId) {
    return DefaultAppName::kWebStore;
  }

  return std::nullopt;
}

void RecordAppLaunch(const std::string& app_id,
                     apps::LaunchSource launch_source) {
  if (const std::optional<DefaultAppName> app_name = AppIdToName(app_id)) {
    RecordDefaultAppLaunch(app_name.value(), launch_source);
  }
}

const std::optional<apps::DefaultAppName> PreinstalledWebAppIdToName(
    const std::string& app_id) {
  if (app_id == ash::kCalculatorAppId) {
    return apps::DefaultAppName::kCalculator;
  } else if (app_id == ash::kCanvasAppId) {
    return apps::DefaultAppName::kChromeCanvas;
  } else if (app_id == ash::kCursiveAppId) {
    return apps::DefaultAppName::kCursive;
#if BUILDFLAG(GOOGLE_CHROME_BRANDING) && BUILDFLAG(IS_CHROMEOS)
  } else if (app_id == ash::kGeminiAppId) {
    return apps::DefaultAppName::kGemini;
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING) && BUILDFLAG(IS_CHROMEOS)
  } else if (app_id == ash::kGmailAppId) {
    return apps::DefaultAppName::kGmail;
  } else if (app_id == ash::kGoogleMoviesAppId) {
    return apps::DefaultAppName::kPlayMovies;
  } else if (app_id == ash::kGoogleCalendarAppId) {
    return apps::DefaultAppName::kGoogleCalendar;
  } else if (app_id == ash::kGoogleChatAppId ||
             app_id == ash::kOldGoogleChatAppId) {
    return apps::DefaultAppName::kGoogleChat;
  } else if (app_id == ash::kGoogleDocsAppId) {
    return apps::DefaultAppName::kDocs;
  } else if (app_id == ash::kGoogleDriveAppId) {
    return apps::DefaultAppName::kDrive;
  } else if (app_id == ash::kGoogleMeetAppId) {
    return apps::DefaultAppName::kGoogleMeet;
  } else if (app_id == ash::kGoogleSheetsAppId) {
    return apps::DefaultAppName::kSheets;
  } else if (app_id == ash::kGoogleSlidesAppId) {
    return apps::DefaultAppName::kSlides;
  } else if (app_id == ash::kGoogleKeepAppId) {
    return apps::DefaultAppName::kKeep;
  } else if (app_id == ash::kGoogleMapsAppId) {
    return apps::DefaultAppName::kGoogleMaps;
  } else if (app_id == ash::kMessagesAppId) {
    return apps::DefaultAppName::kGoogleMessages;
  } else if (app_id == ash::kPlayBooksAppId) {
    return apps::DefaultAppName::kPlayBooks;
  } else if (app_id == ash::kYoutubeAppId) {
    return apps::DefaultAppName::kYouTube;
  } else if (app_id == ash::kYoutubeMusicAppId) {
    return apps::DefaultAppName::kYouTubeMusic;
  } else {
    return std::nullopt;
  }
}


}  // namespace apps
