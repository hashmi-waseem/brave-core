/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/bitcoin/bitcoin_imported_keyring.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/check.h"
#include "brave/components/brave_wallet/browser/internal/hd_key.h"
#include "brave/components/brave_wallet/common/bitcoin_utils.h"

namespace brave_wallet {

BitcoinImportedKeyring::BitcoinImportedKeyring(bool testnet)
    : testnet_(testnet) {}

BitcoinImportedKeyring::~BitcoinImportedKeyring() = default;

bool BitcoinImportedKeyring::AddAccount(uint32_t account,
                                        const std::string& payload) {
  if (accounts_.contains(account)) {
    return false;
  }
  auto hd_key = HDKey::GenerateFromExtendedKey(payload);
  if (!hd_key) {
    return false;
  }

  accounts_[account] = std::move(hd_key);
  return true;
}

bool BitcoinImportedKeyring::RemoveAccount(uint32_t account) {
  return accounts_.erase(account) > 0;
}

std::optional<std::string> BitcoinImportedKeyring::GetAddress(
    uint32_t account,
    const mojom::BitcoinKeyId& key_id) {
  auto hd_key = DeriveKey(account, key_id);
  if (!hd_key) {
    return std::nullopt;
  }

  return PubkeyToSegwitAddress(hd_key->GetPublicKeyBytes(), testnet_);
}

std::optional<std::vector<uint8_t>> BitcoinImportedKeyring::GetPubkey(
    uint32_t account,
    const mojom::BitcoinKeyId& key_id) {
  auto hd_key = DeriveKey(account, key_id);
  if (!hd_key) {
    return std::nullopt;
  }

  return hd_key->GetPublicKeyBytes();
}

std::optional<std::vector<uint8_t>> BitcoinImportedKeyring::SignMessage(
    uint32_t account,
    const mojom::BitcoinKeyId& key_id,
    base::span<const uint8_t, 32> message) {
  auto hd_key = DeriveKey(account, key_id);
  if (!hd_key) {
    return std::nullopt;
  }

  return hd_key->SignDer(message);
}

HDKey* BitcoinImportedKeyring::GetAccountByIndex(uint32_t account) {
  if (!accounts_.contains(account)) {
    return nullptr;
  }
  return accounts_[account].get();
}

// std::string BitcoinImportedKeyring::GetAddressInternal(HDKey* hd_key) const {
//   if (!hd_key) {
//     return std::string();
//   }
//   return hd_key->GetSegwitAddress(testnet_);
// }

// std::unique_ptr<HDKey> BitcoinImportedKeyring::DeriveAccount(
//     uint32_t index) const {
//   // Mainnet - m/84'/0'/{index}'
//   // Testnet - m/84'/1'/{index}'
//   return root_->DeriveHardenedChild(index);
// }

std::unique_ptr<HDKey> BitcoinImportedKeyring::DeriveKey(
    uint32_t account,
    const mojom::BitcoinKeyId& key_id) {
  // TODO(apaymyshev): keep local cache of keys: key_id->key
  auto* account_key = GetAccountByIndex(account);
  if (!account_key) {
    return nullptr;
  }

  DCHECK(key_id.change == 0 || key_id.change == 1);

  auto key = account_key->DeriveNormalChild(key_id.change);
  if (!key) {
    return nullptr;
  }

  // Mainnet - m/84'/0'/{account}'/{key_id.change}/{key_id.index}
  // Testnet - m/84'/1'/{account}'/{key_id.change}/{key_id.index}
  return key->DeriveNormalChild(key_id.index);
}

}  // namespace brave_wallet
