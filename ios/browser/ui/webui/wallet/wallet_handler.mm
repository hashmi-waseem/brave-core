// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/ios/browser/ui/webui/wallet/wallet_handler.h"

#include <string>
#include <utility>
#include <vector>

#include "brave/components/brave_wallet/browser/keyring_service.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/components/brave_wallet/common/common_utils.h"
#include "brave/ios/browser/brave_wallet/keyring_service_factory.h"
#include "ios/chrome/browser/shared/model/browser_state/chrome_browser_state.h"

namespace brave_wallet {

WalletHandler::WalletHandler(
    mojo::PendingReceiver<mojom::WalletHandler> receiver,
    ChromeBrowserState* browser_state)
    : receiver_(this, std::move(receiver)),
      keyring_service_(
          KeyringServiceFactory::GetServiceForState(browser_state)) {}

WalletHandler::~WalletHandler() = default;

// TODO(apaymyshev): this is the only method in WalletHandler. Should be merged
// into BraveWalletService.
void WalletHandler::GetWalletInfo(GetWalletInfoCallback callback) {
  if (!keyring_service_) {
    std::move(callback).Run(nullptr);
    return;
  }

  std::move(callback).Run(mojom::WalletInfo::New(
      keyring_service_->IsWalletCreatedSync(), keyring_service_->IsLockedSync(),
      keyring_service_->IsWalletBackedUpSync(), IsBitcoinEnabled(),
      IsZCashEnabled(), IsNftPinningEnabled(), IsAnkrBalancesEnabled(),
      IsTransactionSimulationsEnabled()));
}

}  // namespace brave_wallet
