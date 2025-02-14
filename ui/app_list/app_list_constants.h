// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_APP_LIST_APP_LIST_CONSTANTS_H_
#define UI_APP_LIST_APP_LIST_CONSTANTS_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/app_list/app_list_export.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/shadow_value.h"

namespace app_list {

APP_LIST_EXPORT extern const SkColor kContentsBackgroundColor;
APP_LIST_EXPORT extern const SkColor kSearchBoxBackground;

APP_LIST_EXPORT extern const SkColor kSearchTextColor;

APP_LIST_EXPORT extern const SkColor kLabelBackgroundColor;
APP_LIST_EXPORT extern const SkColor kTopSeparatorColor;
APP_LIST_EXPORT extern const SkColor kBottomSeparatorColor;

APP_LIST_EXPORT extern const SkColor kDialogSeparatorColor;

APP_LIST_EXPORT extern const SkColor kHighlightedColor;
APP_LIST_EXPORT extern const SkColor kSelectedColor;

APP_LIST_EXPORT extern const SkColor kPagerHoverColor;
APP_LIST_EXPORT extern const SkColor kPagerNormalColor;
APP_LIST_EXPORT extern const SkColor kPagerSelectedColor;

APP_LIST_EXPORT extern const SkColor kResultBorderColor;
APP_LIST_EXPORT extern const SkColor kResultDefaultTextColor;
APP_LIST_EXPORT extern const SkColor kResultDimmedTextColor;
APP_LIST_EXPORT extern const SkColor kResultURLTextColor;

APP_LIST_EXPORT extern const SkColor kGridTitleColor;
APP_LIST_EXPORT extern const SkColor kGridTitleHoverColor;

APP_LIST_EXPORT extern const SkColor kFolderTitleColor;
APP_LIST_EXPORT extern const SkColor kFolderTitleHintTextColor;
APP_LIST_EXPORT extern const SkColor kFolderBubbleColor;
APP_LIST_EXPORT extern const SkColor kFolderShadowColor;
APP_LIST_EXPORT extern const float kFolderBubbleRadius;
APP_LIST_EXPORT extern const float kFolderShadowRadius;
APP_LIST_EXPORT extern const float kFolderShadowOffsetY;

APP_LIST_EXPORT extern const SkColor kCardBackgroundColor;

APP_LIST_EXPORT extern const int kPageTransitionDurationInMs;
APP_LIST_EXPORT extern const int kOverscrollPageTransitionDurationMs;
APP_LIST_EXPORT extern const int kFolderTransitionInDurationMs;
APP_LIST_EXPORT extern const int kFolderTransitionOutDurationMs;
APP_LIST_EXPORT extern const int kCustomPageCollapsedHeight;
APP_LIST_EXPORT extern const gfx::Tween::Type kFolderFadeInTweenType;
APP_LIST_EXPORT extern const gfx::Tween::Type kFolderFadeOutTweenType;

APP_LIST_EXPORT extern const int kPreferredCols;
APP_LIST_EXPORT extern const int kPreferredRows;
APP_LIST_EXPORT extern const int kGridIconDimension;

APP_LIST_EXPORT extern const int kListIconSize;
APP_LIST_EXPORT extern const int kTileIconSize;

APP_LIST_EXPORT extern const int kCenteredPreferredCols;
APP_LIST_EXPORT extern const int kCenteredPreferredRows;

APP_LIST_EXPORT extern const int kExperimentalPreferredCols;
APP_LIST_EXPORT extern const int kExperimentalPreferredRows;

APP_LIST_EXPORT extern const int kReorderDroppingCircleRadius;

APP_LIST_EXPORT extern const int kExperimentalAppsGridPadding;
APP_LIST_EXPORT extern const int kExperimentalSearchBoxPadding;

APP_LIST_EXPORT extern size_t kMaxFolderItems;
APP_LIST_EXPORT extern const size_t kNumFolderTopItems;
APP_LIST_EXPORT extern const size_t kMaxFolderNameChars;

APP_LIST_EXPORT extern const ui::ResourceBundle::FontStyle kItemTextFontStyle;

APP_LIST_EXPORT extern const char kPageOpenedHistogram[];
APP_LIST_EXPORT extern const char kSearchResultOpenDisplayTypeHistogram[];

#if defined(OS_LINUX) || defined(OS_FREEBSD)
// The WM_CLASS name for the app launcher window on Linux/FreeBSD.
APP_LIST_EXPORT extern const char kAppListWMClass[];
#endif

// Returns the shadow values for a view at |z_height|.
gfx::ShadowValue APP_LIST_EXPORT GetShadowForZHeight(int z_height);

}  // namespace app_list

#endif  // UI_APP_LIST_APP_LIST_CONSTANTS_H_
