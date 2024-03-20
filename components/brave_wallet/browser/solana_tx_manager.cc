/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/solana_tx_manager.h"

#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "base/base64.h"
#include "base/notreached.h"
#include "brave/components/brave_wallet/browser/account_resolver_delegate.h"
#include "brave/components/brave_wallet/browser/blockchain_registry.h"
#include "brave/components/brave_wallet/browser/brave_wallet_utils.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/browser/solana_block_tracker.h"
#include "brave/components/brave_wallet/browser/solana_instruction_builder.h"
#include "brave/components/brave_wallet/browser/solana_keyring.h"
#include "brave/components/brave_wallet/browser/solana_message.h"
#include "brave/components/brave_wallet/browser/solana_message_header.h"
#include "brave/components/brave_wallet/browser/solana_tx_meta.h"
#include "brave/components/brave_wallet/browser/solana_tx_state_manager.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/components/brave_wallet/common/brave_wallet_constants.h"
#include "brave/components/brave_wallet/common/brave_wallet_types.h"
#include "brave/components/brave_wallet/common/encoding_utils.h"
#include "brave/components/brave_wallet/common/solana_address.h"
#include "brave/components/brave_wallet/common/solana_utils.h"
#include "build/build_config.h"
#include "components/grit/brave_components_strings.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

std::optional<uint8_t> DecodeUint8(const std::vector<uint8_t>& input,
                                   size_t& offset) {
  if (offset >= input.size() || input.size() - offset < sizeof(uint8_t)) {
    return std::nullopt;
  }

  offset += sizeof(uint8_t);
  return input[offset - sizeof(uint8_t)];
}

std::optional<uint32_t> DecodeUint32(const std::vector<uint8_t>& input,
                                     size_t& offset) {
  if (offset >= input.size() || input.size() - offset < sizeof(uint32_t)) {
    return std::nullopt;
  }

  // Read bytes in little endian order.
  base::span<const uint8_t> s =
      base::make_span(input.begin() + offset, sizeof(uint32_t));
  uint32_t uint32_le = *reinterpret_cast<const uint32_t*>(s.data());

  offset += sizeof(uint32_t);

#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return uint32_le;
#else
  return base::ByteSwap(uint32_le);
#endif
}

// std::optional<uint64_t> DecodeUint64(const std::vector<uint8_t>& input,
//                                      size_t& offset) {
//   if (offset >= input.size() || input.size() - offset < sizeof(uint64_t)) {
//     return std::nullopt;
//   }

//   // Read bytes in little endian order.
//   base::span<const uint8_t> s =
//       base::make_span(input.begin() + offset, sizeof(uint64_t));
//   uint64_t uint64_le = *reinterpret_cast<const uint64_t*>(s.data());

//   offset += sizeof(uint64_t);

// #if defined(ARCH_CPU_LITTLE_ENDIAN)
//   return uint64_le;
// #else
//   return base::ByteSwap(uint64_le);
// #endif
// }

std::optional<std::string> DecodePublicKey(const std::vector<uint8_t>& input,
                                           size_t& offset) {
  if (offset >= input.size() ||
      input.size() - offset < brave_wallet::kSolanaPubkeySize) {
    return std::nullopt;
  }

  offset += brave_wallet::kSolanaPubkeySize;
  return brave_wallet::Base58Encode(std::vector<uint8_t>(
      input.begin() + offset - brave_wallet::kSolanaPubkeySize,
      input.begin() + offset));
}

}  // namespace

namespace brave_wallet {

// https://docs.rs/solana-program/1.18.10/src/solana_program/clock.rs.html#129-131
constexpr int kValidBlockHeightThreshold = 150;

SolanaTxManager::SolanaTxManager(
    TxService* tx_service,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    JsonRpcService* json_rpc_service,
    KeyringService* keyring_service,
    PrefService* prefs,
    TxStorageDelegate* delegate,
    AccountResolverDelegate* account_resolver_delegate)
    : TxManager(
          std::make_unique<SolanaTxStateManager>(prefs,
                                                 delegate,
                                                 account_resolver_delegate),
          std::make_unique<SolanaBlockTracker>(json_rpc_service),
          tx_service,
          keyring_service,
          prefs),
      json_rpc_service_(json_rpc_service),
      weak_ptr_factory_(this) {
  simple_hash_client_ = std::make_unique<SimpleHashClient>(url_loader_factory);
  GetSolanaBlockTracker()->AddObserver(this);
}

SolanaTxManager::~SolanaTxManager() {
  GetSolanaBlockTracker()->RemoveObserver(this);
}

void SolanaTxManager::AddUnapprovedTransaction(
    const std::string& chain_id,
    mojom::TxDataUnionPtr tx_data_union,
    const mojom::AccountIdPtr& from,
    const std::optional<url::Origin>& origin,
    AddUnapprovedTransactionCallback callback) {
  DCHECK(tx_data_union->is_solana_tx_data());

  auto tx = SolanaTransaction::FromSolanaTxData(
      std::move(tx_data_union->get_solana_tx_data()));
  if (!tx) {
    std::move(callback).Run(
        false, "",
        l10n_util::GetStringUTF8(IDS_WALLET_SEND_TRANSACTION_CONVERT_TX_DATA));
    return;
  }

  SolanaTxMeta meta(from, std::move(tx));
  meta.set_id(TxMeta::GenerateMetaID());
  meta.set_origin(
      origin.value_or(url::Origin::Create(GURL("chrome://wallet"))));
  meta.set_created_time(base::Time::Now());
  meta.set_status(mojom::TransactionStatus::Unapproved);
  meta.set_chain_id(chain_id);
  if (!tx_state_manager_->AddOrUpdateTx(meta)) {
    std::move(callback).Run(
        false, "", l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  std::move(callback).Run(true, meta.id(), "");
}

void SolanaTxManager::ApproveTransaction(const std::string& tx_meta_id,
                                         ApproveTransactionCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta) {
    DCHECK(false) << "Transaction should be found";
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }

  const std::string blockhash = meta->tx()->message()->recent_blockhash();
  auto chain_id = meta->chain_id();
  if (blockhash.empty()) {
    GetSolanaBlockTracker()->GetLatestBlockhash(
        chain_id,
        base::BindOnce(&SolanaTxManager::OnGetLatestBlockhash,
                       weak_ptr_factory_.GetWeakPtr(), std::move(meta),
                       std::move(callback)),
        true);
  } else {
    // No existing last valid block height info, use the current block height
    // + 150 as the last valid block height.
    json_rpc_service_->GetSolanaBlockHeight(
        chain_id,
        base::BindOnce(&SolanaTxManager::OnGetBlockHeightForBlockhash,
                       weak_ptr_factory_.GetWeakPtr(), std::move(meta),
                       std::move(callback), blockhash));
  }
}

void SolanaTxManager::OnGetBlockHeightForBlockhash(
    std::unique_ptr<SolanaTxMeta> meta,
    ApproveTransactionCallback callback,
    const std::string& blockhash,
    uint64_t block_height,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(
        false, mojom::ProviderErrorUnion::NewSolanaProviderError(error),
        error_message);
    return;
  }

  OnGetLatestBlockhash(std::move(meta), std::move(callback), blockhash,
                       block_height + kValidBlockHeightThreshold,
                       mojom::SolanaProviderError::kSuccess, "");
}

void SolanaTxManager::OnGetLatestBlockhash(std::unique_ptr<SolanaTxMeta> meta,
                                           ApproveTransactionCallback callback,
                                           const std::string& latest_blockhash,
                                           uint64_t last_valid_block_height,
                                           mojom::SolanaProviderError error,
                                           const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(
        false, mojom::ProviderErrorUnion::NewSolanaProviderError(error),
        error_message);
    return;
  }

  meta->set_status(mojom::TransactionStatus::Approved);
  meta->tx()->message()->set_recent_blockhash(latest_blockhash);
  meta->tx()->message()->set_last_valid_block_height(last_valid_block_height);
  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  json_rpc_service_->SendSolanaTransaction(
      meta->chain_id(),
      meta->tx()->GetSignedTransaction(keyring_service_, meta->from()),
      meta->tx()->send_options(),
      base::BindOnce(&SolanaTxManager::OnSendSolanaTransaction,
                     weak_ptr_factory_.GetWeakPtr(), meta->id(),
                     std::move(callback)));
}

void SolanaTxManager::OnGetLatestBlockhashHardware(
    std::unique_ptr<SolanaTxMeta> meta,
    GetTransactionMessageToSignCallback callback,
    const std::string& latest_blockhash,
    uint64_t last_valid_block_height,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(nullptr);
    return;
  }

  meta->tx()->message()->set_recent_blockhash(latest_blockhash);
  meta->tx()->message()->set_last_valid_block_height(last_valid_block_height);
  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(nullptr);
    return;
  }

  auto message_signers_pair = meta->tx()->GetSerializedMessage();
  if (!message_signers_pair) {
    std::move(callback).Run(nullptr);
    return;
  }

  auto& message_bytes = message_signers_pair->first;
  std::move(callback).Run(
      mojom::MessageToSignUnion::NewMessageBytes(std::move(message_bytes)));
}

void SolanaTxManager::OnSendSolanaTransaction(
    const std::string& tx_meta_id,
    ApproveTransactionCallback callback,
    const std::string& tx_hash,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  std::unique_ptr<TxMeta> meta = tx_state_manager_->GetTx(tx_meta_id);
  if (!meta) {
    DCHECK(false) << "Transaction should be found";
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }

  bool success = error == mojom::SolanaProviderError::kSuccess;

  if (success) {
    meta->set_status(mojom::TransactionStatus::Submitted);
    meta->set_submitted_time(base::Time::Now());
    meta->set_tx_hash(tx_hash);
  } else {
    meta->set_status(mojom::TransactionStatus::Error);
  }

  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  if (success) {
    UpdatePendingTransactions(meta->chain_id());
  }

  std::move(callback).Run(
      error_message.empty(),
      mojom::ProviderErrorUnion::NewSolanaProviderError(error), error_message);
}

void SolanaTxManager::UpdatePendingTransactions(
    const std::optional<std::string>& chain_id) {
  std::set<std::string> pending_chain_ids;
  if (chain_id.has_value()) {
    pending_chain_ids = pending_chain_ids_;
    pending_chain_ids.emplace(*chain_id);
    json_rpc_service_->GetSolanaBlockHeight(
        *chain_id, base::BindOnce(&SolanaTxManager::OnGetBlockHeight,
                                  weak_ptr_factory_.GetWeakPtr(), *chain_id));
  } else {
    auto pending_transactions = tx_state_manager_->GetTransactionsByStatus(
        std::nullopt, mojom::TransactionStatus::Submitted, std::nullopt);
    for (const auto& pending_transaction : pending_transactions) {
      const auto& pending_chain_id = pending_transaction->chain_id();
      // Skip already queried chain ids.
      if (pending_chain_ids.contains(pending_chain_id)) {
        continue;
      }

      json_rpc_service_->GetSolanaBlockHeight(
          pending_chain_id,
          base::BindOnce(&SolanaTxManager::OnGetBlockHeight,
                         weak_ptr_factory_.GetWeakPtr(), pending_chain_id));
      pending_chain_ids.emplace(pending_chain_id);
    }
  }
  CheckIfBlockTrackerShouldRun(pending_chain_ids);
}

void SolanaTxManager::OnGetBlockHeight(const std::string& chain_id,
                                       uint64_t block_height,
                                       mojom::SolanaProviderError error,
                                       const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    return;
  }

  auto pending_transactions = tx_state_manager_->GetTransactionsByStatus(
      chain_id, mojom::TransactionStatus::Submitted, std::nullopt);
  std::vector<std::string> tx_meta_ids;
  std::vector<std::string> tx_signatures;
  for (const auto& pending_transaction : pending_transactions) {
    tx_meta_ids.push_back(pending_transaction->id());
    tx_signatures.push_back(pending_transaction->tx_hash());
  }
  json_rpc_service_->GetSolanaSignatureStatuses(
      chain_id, tx_signatures,
      base::BindOnce(&SolanaTxManager::OnGetSignatureStatuses,
                     weak_ptr_factory_.GetWeakPtr(), chain_id, tx_meta_ids,
                     block_height));
}

void SolanaTxManager::OnGetSignatureStatuses(
    const std::string& chain_id,
    const std::vector<std::string>& tx_meta_ids,
    uint64_t block_height,
    const std::vector<std::optional<SolanaSignatureStatus>>& signature_statuses,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    return;
  }

  if (tx_meta_ids.size() != signature_statuses.size()) {
    return;
  }

  for (size_t i = 0; i < tx_meta_ids.size(); i++) {
    std::unique_ptr<SolanaTxMeta> meta =
        GetSolanaTxStateManager()->GetSolanaTx(tx_meta_ids[i]);
    if (!meta) {
      continue;
    }

    if (!signature_statuses[i]) {
      if (meta->tx()->message()->last_valid_block_height() &&
          meta->tx()->message()->last_valid_block_height() < block_height) {
        meta->set_status(mojom::TransactionStatus::Dropped);
        tx_state_manager_->AddOrUpdateTx(*meta);
      }
      continue;
    }

    if (!signature_statuses[i]->err.empty()) {
      meta->set_signature_status(*signature_statuses[i]);
      meta->set_status(mojom::TransactionStatus::Error);
      tx_state_manager_->AddOrUpdateTx(*meta);
      continue;
    }

    // Update SolanaTxMeta with signature status.
    if (!signature_statuses[i]->confirmation_status.empty()) {
      meta->set_signature_status(*signature_statuses[i]);

      if (signature_statuses[i]->confirmation_status == "finalized") {
        meta->set_status(mojom::TransactionStatus::Confirmed);
        meta->set_confirmed_time(base::Time::Now());
      }

      tx_state_manager_->AddOrUpdateTx(*meta);
    }
  }
}

void SolanaTxManager::SpeedupOrCancelTransaction(
    const std::string& tx_meta_id,
    bool cancel,
    SpeedupOrCancelTransactionCallback callback) {
  NOTIMPLEMENTED();
}

void SolanaTxManager::RetryTransaction(const std::string& tx_meta_id,
                                       RetryTransactionCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta || !meta->tx()) {
    std::move(callback).Run(
        false, "",
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }

  if (!meta->IsRetriable()) {
    std::move(callback).Run(
        false, "",
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_RETRIABLE));
    return;
  }

  if (!meta->tx()->message()->UsesDurableNonce()) {
    // Clear blockhash to trigger getting a new one when user approves.
    meta->tx()->message()->set_recent_blockhash("");

    // Clear sign_tx_param because they're no longer relevant for transactions
    // not using durable nonce, and clear this ensures we re-serialize the
    // message using the new blockhash in
    // SolanaTransacaction::GetSerializedMessage. sign_tx_param is not relevant
    // anymore because all existing signatures will be invalid if the blockhash
    // (message) changes, and we are the only one able to re-sign the new
    // message so we don't need to worry about having a different account order
    // than other implementations that dApp uses (Solana web3.js for example).
    meta->tx()->set_sign_tx_param(nullptr);
  }

  // Clear last valid block height for retried transaction, which will be
  // updated when user approves.
  meta->tx()->message()->set_last_valid_block_height(0);

  // Reset necessary fields for retried transaction.
  meta->set_id(TxMeta::GenerateMetaID());
  meta->set_status(mojom::TransactionStatus::Unapproved);
  meta->set_created_time(base::Time::Now());
  meta->set_submitted_time(base::Time());
  meta->set_confirmed_time(base::Time());
  meta->set_tx_hash("");
  meta->set_signature_status(SolanaSignatureStatus());

  meta->tx()->ClearRawSignatures();

  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(
        false, "", l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  std::move(callback).Run(true, meta->id(), "");
}

void SolanaTxManager::GetTransactionMessageToSign(
    const std::string& tx_meta_id,
    GetTransactionMessageToSignCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta || !meta->tx()) {
    VLOG(1) << __FUNCTION__ << "No transaction found with id:" << tx_meta_id;
    std::move(callback).Run(nullptr);
    return;
  }

  const std::string blockhash = meta->tx()->message()->recent_blockhash();
  auto chain_id = meta->chain_id();
  if (blockhash.empty()) {
    GetSolanaBlockTracker()->GetLatestBlockhash(
        chain_id,
        base::BindOnce(&SolanaTxManager::OnGetLatestBlockhashHardware,
                       weak_ptr_factory_.GetWeakPtr(), std::move(meta),
                       std::move(callback)),
        true);
  } else {
    json_rpc_service_->GetSolanaBlockHeight(
        chain_id,
        base::BindOnce(&SolanaTxManager::OnGetBlockHeightForBlockhashHardware,
                       weak_ptr_factory_.GetWeakPtr(), std::move(meta),
                       std::move(callback), blockhash));
  }
}

void SolanaTxManager::OnGetBlockHeightForBlockhashHardware(
    std::unique_ptr<SolanaTxMeta> meta,
    GetTransactionMessageToSignCallback callback,
    const std::string& blockhash,
    uint64_t block_height,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(nullptr);
    return;
  }

  OnGetLatestBlockhashHardware(std::move(meta), std::move(callback), blockhash,
                               block_height + kValidBlockHeightThreshold,
                               mojom::SolanaProviderError::kSuccess, "");
}

mojom::CoinType SolanaTxManager::GetCoinType() const {
  return mojom::CoinType::SOL;
}

void SolanaTxManager::MakeSystemProgramTransferTxData(
    const std::string& from,
    const std::string& to,
    uint64_t lamports,
    MakeSystemProgramTransferTxDataCallback callback) {
  if (BlockchainRegistry::GetInstance()->IsOfacAddress(to)) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInvalidParams,
        l10n_util::GetStringUTF8(IDS_WALLET_OFAC_RESTRICTION));
    return;
  }

  std::optional<SolanaInstruction> instruction =
      solana::system_program::Transfer(from, to, lamports);
  if (!instruction) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  std::vector<SolanaInstruction> vec;
  vec.emplace_back(std::move(instruction.value()));
  // recent_blockhash will be updated when we are going to send out the tx.
  auto msg = SolanaMessage::CreateLegacyMessage("" /* recent_blockhash*/, 0,
                                                from, std::move(vec));
  if (!msg) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  SolanaTransaction transaction(std::move(*msg));
  transaction.set_to_wallet_address(to);
  transaction.set_tx_type(mojom::TransactionType::SolanaSystemTransfer);
  transaction.set_lamports(lamports);

  auto tx_data = transaction.ToSolanaTxData();
  // This won't be null because we will always construct the mojo struct.
  DCHECK(tx_data);
  std::move(callback).Run(std::move(tx_data),
                          mojom::SolanaProviderError::kSuccess, "");
}

void SolanaTxManager::MakeTokenProgramTransferTxData(
    const std::string& chain_id,
    const std::string& spl_token_mint_address,
    const std::string& from_wallet_address,
    const std::string& to_wallet_address,
    uint64_t amount,
    MakeTokenProgramTransferTxDataCallback callback) {
  if (BlockchainRegistry::GetInstance()->IsOfacAddress(to_wallet_address)) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInvalidParams,
        l10n_util::GetStringUTF8(IDS_WALLET_OFAC_RESTRICTION));
    return;
  }

  std::optional<std::string> from_associated_token_account =
      SolanaKeyring::GetAssociatedTokenAccount(spl_token_mint_address,
                                               from_wallet_address);
  std::optional<std::string> to_associated_token_account =
      SolanaKeyring::GetAssociatedTokenAccount(spl_token_mint_address,
                                               to_wallet_address);
  if (!from_associated_token_account || !to_associated_token_account) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  // Check if the receiver's associated token account is existed or not.
  json_rpc_service_->GetSolanaAccountInfo(
      chain_id, *to_associated_token_account,
      base::BindOnce(
          &SolanaTxManager::OnGetAccountInfo, weak_ptr_factory_.GetWeakPtr(),
          spl_token_mint_address, from_wallet_address, to_wallet_address,
          *from_associated_token_account, *to_associated_token_account, amount,
          std::move(callback)));
}

void SolanaTxManager::MakeTxDataFromBase64EncodedTransaction(
    const std::string& encoded_transaction,
    const mojom::TransactionType tx_type,
    mojom::SolanaSendTransactionOptionsPtr send_options,
    MakeTxDataFromBase64EncodedTransactionCallback callback) {
  std::optional<std::vector<std::uint8_t>> transaction_bytes =
      base::Base64Decode(encoded_transaction);
  if (!transaction_bytes || transaction_bytes->empty() ||
      transaction_bytes->size() > kSolanaMaxTxSize) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  auto transaction =
      SolanaTransaction::FromSignedTransactionBytes(*transaction_bytes);
  if (!transaction) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  transaction->set_tx_type(std::move(tx_type));

  if (send_options) {
    const auto& options = SolanaTransaction::SendOptions::FromMojomSendOptions(
        std::move(send_options));
    transaction->set_send_options(std::move(options));
  }

  auto tx_data = transaction->ToSolanaTxData();
  // This won't be null because we will always construct the mojo struct.
  DCHECK(tx_data);
  std::move(callback).Run(std::move(tx_data),
                          mojom::SolanaProviderError::kSuccess, "");
}

void SolanaTxManager::MakeBubbleGumProgramTransferTxData(
    const std::string& chain_id,
    const std::string& token_address,
    const std::string& from_wallet_address,
    const std::string& to_wallet_address,
    MakeBubbleGumProgramTransferTxDataCallback callback) {
  // Get asset and proof data from SimpleHash
  auto internal_callback =
      base::BindOnce(&SolanaTxManager::OnFetchCompressedNftProof,
                     weak_ptr_factory_.GetWeakPtr(), from_wallet_address,
                     to_wallet_address, std::move(callback));

  simple_hash_client_->FetchSolCompressedNftProofData(
      token_address, std::move(internal_callback));
}

void SolanaTxManager::OnFetchCompressedNftProof(
    const std::string& from_wallet_address,
    const std::string& to_wallet_address,
    MakeBubbleGumProgramTransferTxDataCallback callback,
    std::optional<SolCompressedNftProofData> proof) {
  if (!proof) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  // If the from_wallet address does not match the proof.owner or delegate, we
  // done.
  // TODO

  // Get the Merkle tree account
  auto internal_callback =
      base::BindOnce(&SolanaTxManager::OnGetMerkleTreeAccountInfo,
                     weak_ptr_factory_.GetWeakPtr(), to_wallet_address, *proof,
                     std::move(callback));

  json_rpc_service_->GetSolanaAccountInfo(
      mojom::kSolanaMainnet, proof->merkle_tree, std::move(internal_callback));
}

void SolanaTxManager::OnGetMerkleTreeAccountInfo(
    const std::string& to_wallet_address,
    const SolCompressedNftProofData& proof,
    MakeBubbleGumProgramTransferTxDataCallback callback,
    std::optional<SolanaAccountInfo> account_info,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(nullptr, error, error_message);
    return;
  }

  auto account_data_bytes = base::Base64Decode(account_info->data);
  if (!account_data_bytes) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  auto result = DecodeMerkleTreeAuthorityAndDepth(*account_data_bytes);
  if (!result) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }
  std::optional<SolanaInstruction> instruction =
      solana::bubblegum_program::Transfer(
          result->first, result->second.ToBase58(), to_wallet_address, proof);

  if (!instruction) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  std::vector<SolanaInstruction> vec;
  vec.emplace_back(std::move(instruction.value()));

  // recent_blockhash will be updated when we are going to send out the tx.
  auto msg = SolanaMessage::CreateLegacyMessage("" /* recent_blockhash*/, 0,
                                                // from_wallet_address,
                                                proof.owner,  // Should probably
                                                std::move(vec));
  if (!msg) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  SolanaTransaction transaction(std::move(*msg));
  transaction.set_to_wallet_address(to_wallet_address);
  transaction.set_tx_type(mojom::TransactionType::SolanaCompressedNftTransfer);
  // transaction.set_lamports(lamports); // why not ?
  auto tx_data = transaction.ToSolanaTxData();
  DCHECK(tx_data);
  std::move(callback).Run(std::move(tx_data),
                          mojom::SolanaProviderError::kSuccess, "");
}

// Adapted from
// https://github.com/solana-labs/solana-program-library/blob/master/account-compression/sdk/src/accounts/ConcurrentMerkleTreeAccount.ts#L140
std::optional<std::pair<uint32_t, SolanaAddress>>
SolanaTxManager::DecodeMerkleTreeAuthorityAndDepth(
    const std::vector<uint8_t>& data) {
  size_t offset = 0;
  /* HEADER */

  // Decode the first byte, representing the compression account type .
  // The possible values are 0=Unitialized and 1=ConcurrentMerkleTree.
  // The value must be 1.
  auto compression_account_type = DecodeUint8(data, offset);
  if (!compression_account_type) {
    return std::nullopt;
  }
  if (*compression_account_type != 1) {
    return std::nullopt;
  }

  // Decode the version. 0=v1. The value must be 0.
  auto version = DecodeUint8(data, offset);
  if (!version) {
    return std::nullopt;
  }
  if (*version != 0) {
    return std::nullopt;
  }

  // Decode maxBufferSize
  auto max_buffer_size = DecodeUint32(data, offset);
  if (!max_buffer_size) {
    return std::nullopt;
  }

  // Decode maxDepth
  auto max_depth = DecodeUint32(data, offset);
  if (!max_depth) {
    return std::nullopt;
  }

  // Decode the next 32 bytes for authority
  auto authority = DecodePublicKey(data, offset);
  if (!authority) {
    return std::nullopt;
  }

  auto authority_address = SolanaAddress::FromBase58(*authority);
  if (!authority_address) {
    return std::nullopt;
  }

  // export const concurrentMerkleTreeHeaderDataV1Beet = new
  // beet.BeetArgsStruct<ConcurrentMerkleTreeHeaderDataV1>(
  //     [
  //         ['maxBufferSize', beet.u32],
  //         ['maxDepth', beet.u32],
  //         ['authority', beetSolana.publicKey],
  //         ['creationSlot', beet.u64],
  //         ['padding', beet.uniformFixedSizeArray(beet.u8, 6)],
  //     ],
  //     'ConcurrentMerkleTreeHeaderDataV1',
  // );

  offset += /* Skip uint64 creationSlot */ 8 + /* Skip 6 x uint8 padding */ 6;

  /* TREE */

  // return new beet.BeetArgsStruct<ConcurrentMerkleTree>(
  //     [
  //         ['sequenceNumber', beet.u64],
  //         ['activeIndex', beet.u64],
  //         ['bufferSize', beet.u64],
  //         ['changeLogs',
  //         beet.uniformFixedSizeArray(changeLogBeetFactory(maxDepth),
  //         maxBufferSize)],
  //         ['rightMostPath', pathBeetFactory(maxDepth)],
  //     ],
  //     'ConcurrentMerkleTree',
  // );

  offset += /* Skip uint64 sequence number */ 8 +
            /* Skip uint64 activeIndex */ 8 +
            /* Skip uint64 bufferSize */ 8;
  // auto sequence_number = DecodeUint64(data, offset);
  // if (!sequence_number) {
  //   return std::nullopt;
  // }
  // LOG(ERROR) << "buffer size " << *sequence_number;

  // const changeLogBeetFactory = (maxDepth: number) => {
  //     return new beet.BeetArgsStruct<ChangeLogInternal>(
  //         [
  //             ['root', beetSolana.publicKey],
  //             ['pathNodes', beet.uniformFixedSizeArray(beetSolana.publicKey,
  //             maxDepth)],
  //             ['index', beet.u32],
  //             ['_padding', beet.u32],
  //         ],
  //         'ChangeLog',
  //     );
  // };

  // auto root = DecodePublicKey(data, offset);
  // if (!root) {
  //   return std::nullopt;
  // }
  // LOG(ERROR) << "root is " << *root;

  for (size_t i = 0; i < max_buffer_size; i++) {
    // auto root = DecodePublicKey(data, offset);
    // if (!root) {
    //   return std::nullopt;
    // }
    // LOG(ERROR) << "root is " << *root;

    offset += /* Skip root public key */ 32 +
              /* Skip path nodes*/ 32 * *max_depth + /* Skip uint32 index */ 4 +
              /* Skip uint32 padding */ +4;
  }

  // export const pathBeetFactory = (maxDepth: number) => {
  //     return new beet.BeetArgsStruct<Path>(
  //         [
  //             ['proof', beet.uniformFixedSizeArray(beetSolana.publicKey,
  //             maxDepth)],
  //             ['leaf', beetSolana.publicKey],
  //             ['index', beet.u32],
  //             ['_padding', beet.u32],
  //         ],
  //         'Path',
  //     );
  // };

  offset += /* Skip proof */ 32 * *max_depth + /* Skip leaf public key */ 32 +
            /* Skip uint32 index */ 4 + /* Skip uint32 padding */ +4;

  auto canopy_byte_length = data.size() - offset;

  uint32_t canopy_depth;
  if (canopy_byte_length == 0) {
    canopy_depth = 0;
  } else {
    canopy_depth = std::log2(canopy_byte_length / 32.0 + 2) - 1;
  }

  return std::make_pair(canopy_depth, *authority_address);
}

void SolanaTxManager::OnGetAccountInfo(
    const std::string& spl_token_mint_address,
    const std::string& from_wallet_address,
    const std::string& to_wallet_address,
    const std::string& from_associated_token_account,
    const std::string& to_associated_token_account,
    uint64_t amount,
    MakeTokenProgramTransferTxDataCallback callback,
    std::optional<SolanaAccountInfo> account_info,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(nullptr, error, error_message);
    return;
  }

  bool create_associated_token_account = false;
  std::vector<SolanaInstruction> instructions;
  if (!account_info || account_info->owner != mojom::kSolanaTokenProgramId) {
    std::optional<SolanaInstruction> create_associated_token_instruction =
        solana::spl_associated_token_account_program::
            CreateAssociatedTokenAccount(from_wallet_address, to_wallet_address,
                                         to_associated_token_account,
                                         spl_token_mint_address);
    if (!create_associated_token_instruction) {
      std::move(callback).Run(
          nullptr, mojom::SolanaProviderError::kInternalError,
          l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
      return;
    }
    instructions.push_back(std::move(*create_associated_token_instruction));
    create_associated_token_account = true;
  }

  std::optional<SolanaInstruction> transfer_instruction =
      solana::spl_token_program::Transfer(
          mojom::kSolanaTokenProgramId, from_associated_token_account,
          to_associated_token_account, from_wallet_address,
          std::vector<std::string>(), amount);
  if (!transfer_instruction) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }
  instructions.push_back(std::move(*transfer_instruction));

  // recent_blockhash will be updated when we are going to send out the tx.
  auto msg = SolanaMessage::CreateLegacyMessage("" /* recent_blockhash*/, 0,
                                                from_wallet_address,
                                                std::move(instructions));
  if (!msg) {
    std::move(callback).Run(
        nullptr, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  SolanaTransaction transaction(std::move(*msg));
  transaction.set_to_wallet_address(to_wallet_address);
  transaction.set_spl_token_mint_address(spl_token_mint_address);
  transaction.set_amount(amount);
  transaction.set_tx_type(
      create_associated_token_account
          ? mojom::TransactionType::
                SolanaSPLTokenTransferWithAssociatedTokenAccountCreation
          : mojom::TransactionType::SolanaSPLTokenTransfer);

  auto tx_data = transaction.ToSolanaTxData();
  // This won't be null because we will always construct the mojo struct.
  DCHECK(tx_data);
  std::move(callback).Run(std::move(tx_data),
                          mojom::SolanaProviderError::kSuccess, "");
}

void SolanaTxManager::GetEstimatedTxFee(const std::string& tx_meta_id,
                                        GetEstimatedTxFeeCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta) {
    DCHECK(false) << "Transaction should be found";
    std::move(callback).Run(
        false, mojom::SolanaProviderError::kInternalError,
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }

  auto chain_id = meta->chain_id();
  GetSolanaBlockTracker()->GetLatestBlockhash(
      chain_id,
      base::BindOnce(&SolanaTxManager::OnGetLatestBlockhashForGetEstimatedTxFee,
                     weak_ptr_factory_.GetWeakPtr(), std::move(meta),
                     std::move(callback)),
      true);
}

void SolanaTxManager::OnGetLatestBlockhashForGetEstimatedTxFee(
    std::unique_ptr<SolanaTxMeta> meta,
    GetEstimatedTxFeeCallback callback,
    const std::string& latest_blockhash,
    uint64_t last_valid_block_height,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess) {
    std::move(callback).Run(0, error, error_message);
    return;
  }

  meta->tx()->message()->set_recent_blockhash(latest_blockhash);
  meta->tx()->message()->set_last_valid_block_height(last_valid_block_height);
  const std::string base64_encoded_message =
      meta->tx()->GetBase64EncodedMessage();
  json_rpc_service_->GetSolanaFeeForMessage(
      meta->chain_id(), base64_encoded_message,
      base::BindOnce(&SolanaTxManager::OnGetFeeForMessage,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void SolanaTxManager::OnGetFeeForMessage(GetEstimatedTxFeeCallback callback,
                                         uint64_t tx_fee,
                                         mojom::SolanaProviderError error,
                                         const std::string& error_message) {
  std::move(callback).Run(tx_fee, error, error_message);
}

void SolanaTxManager::OnLatestBlockhashUpdated(
    const std::string& chain_id,
    const std::string& blockhash,
    uint64_t last_valid_block_height) {
  UpdatePendingTransactions(chain_id);
}

SolanaTxStateManager* SolanaTxManager::GetSolanaTxStateManager() {
  return static_cast<SolanaTxStateManager*>(tx_state_manager_.get());
}

SolanaBlockTracker* SolanaTxManager::GetSolanaBlockTracker() {
  return static_cast<SolanaBlockTracker*>(block_tracker_.get());
}

std::unique_ptr<SolanaTxMeta> SolanaTxManager::GetTxForTesting(
    const std::string& tx_meta_id) {
  return GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
}

void SolanaTxManager::ProcessSolanaHardwareSignature(
    const std::string& tx_meta_id,
    const std::vector<uint8_t>& signature_bytes,
    ProcessSolanaHardwareSignatureCallback callback) {
  std::unique_ptr<SolanaTxMeta> meta =
      GetSolanaTxStateManager()->GetSolanaTx(tx_meta_id);
  if (!meta) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_BRAVE_WALLET_TRANSACTION_NOT_FOUND));
    return;
  }
  std::optional<std::vector<std::uint8_t>> transaction_bytes =
      meta->tx()->GetSignedTransactionBytes(keyring_service_, meta->from(),
                                            &signature_bytes);
  if (!transaction_bytes) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  meta->set_status(mojom::TransactionStatus::Approved);
  if (!tx_state_manager_->AddOrUpdateTx(*meta)) {
    std::move(callback).Run(
        false,
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInternalError),
        l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
    return;
  }

  json_rpc_service_->SendSolanaTransaction(
      meta->chain_id(), base::Base64Encode(*transaction_bytes),
      meta->tx()->send_options(),
      base::BindOnce(&SolanaTxManager::OnSendSolanaTransaction,
                     weak_ptr_factory_.GetWeakPtr(), meta->id(),
                     std::move(callback)));
}

}  // namespace brave_wallet
