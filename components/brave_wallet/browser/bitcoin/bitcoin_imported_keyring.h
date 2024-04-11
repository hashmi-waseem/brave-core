/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_BITCOIN_BITCOIN_IMPORTED_KEYRING_H_
#define BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_BITCOIN_BITCOIN_IMPORTED_KEYRING_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "brave/components/brave_wallet/browser/internal/hd_key.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"

namespace brave_wallet {

class BitcoinImportedKeyring {
 public:
  explicit BitcoinImportedKeyring(bool testnet);
  ~BitcoinImportedKeyring();
  BitcoinImportedKeyring(const BitcoinImportedKeyring&) = delete;
  BitcoinImportedKeyring& operator=(const BitcoinImportedKeyring&) = delete;

  bool AddAccount(uint32_t account, const std::string& payload);
  bool RemoveAccount(uint32_t account);

  std::optional<std::string> GetAddress(uint32_t account,
                                        const mojom::BitcoinKeyId& key_id);

  std::optional<std::vector<uint8_t>> GetPubkey(
      uint32_t account,
      const mojom::BitcoinKeyId& key_id);

  std::optional<std::vector<uint8_t>> SignMessage(
      uint32_t account,
      const mojom::BitcoinKeyId& key_id,
      base::span<const uint8_t, 32> message);

 private:
  std::map<uint32_t, std::unique_ptr<HDKey>> accounts_;
  HDKey* GetAccountByIndex(uint32_t account);

  //   std::string GetAddressInternal(HDKey* hd_key_base) const;
  //   std::unique_ptr<HDKeyBase> DeriveAccount(uint32_t index) const override;
  std::unique_ptr<HDKey> DeriveKey(uint32_t account,
                                   const mojom::BitcoinKeyId& key_id);

  bool testnet_ = false;
};

}  // namespace brave_wallet

#endif  // BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_BITCOIN_BITCOIN_IMPORTED_KEYRING_H_
