/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_ads/core/internal/account/utility/redeem_confirmation/redeem_confirmation_feature.h"

#include "base/test/scoped_feature_list.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

// npm run test -- brave_unit_tests --filter=BraveAds*

namespace brave_ads {

TEST(BraveAdsRedeemConfirmationFeatureTest, IsEnabled) {
  // Act & Assert
  EXPECT_TRUE(base::FeatureList::IsEnabled(kRedeemConfirmationFeature));
}

TEST(BraveAdsRedeemConfirmationFeatureTest, IsDisabled) {
  // Arrange
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(kRedeemConfirmationFeature);

  // Act & Assert
  EXPECT_FALSE(base::FeatureList::IsEnabled(kRedeemConfirmationFeature));
}

TEST(BraveAdsRedeemConfirmationFeatureTest, FetchPaymentTokenAfter) {
  // Arrange
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeatureWithParameters(
      kRedeemConfirmationFeature, {{"fetch_payment_token_after", "5s"}});

  // Act & Assert
  EXPECT_EQ(base::Seconds(5), kFetchPaymentTokenAfter.Get());
}

TEST(BraveAdsRedeemConfirmationFeatureTest, DefaultFetchPaymentTokenAfter) {
  // Act & Assert
  EXPECT_EQ(base::Seconds(15), kFetchPaymentTokenAfter.Get());
}

TEST(BraveAdsRedeemConfirmationFeatureTest,
     DefaultProcessConversionConfirmationAfterWhenDisabled) {
  // Arrange
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(kRedeemConfirmationFeature);

  // Act & Assert
  EXPECT_EQ(base::Seconds(15), kFetchPaymentTokenAfter.Get());
}

}  // namespace brave_ads
