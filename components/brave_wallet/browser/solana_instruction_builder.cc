/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/solana_instruction_builder.h"

#include <optional>
#include <type_traits>
#include <utility>

#include "brave/components/brave_wallet/browser/simple_hash_client.h"
#include "brave/components/brave_wallet/browser/solana_account_meta.h"
#include "brave/components/brave_wallet/browser/solana_instruction.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/components/brave_wallet/common/brave_wallet_constants.h"
#include "brave/components/brave_wallet/common/brave_wallet_types.h"
#include "brave/components/brave_wallet/common/encoding_utils.h"
#include "build/build_config.h"
namespace {

// Solana uses bincode::serialize when encoding instruction data, which encodes
// unsigned numbers in little endian byte order.
template <typename T>
void UintToLEBytes(T val, std::vector<uint8_t>* bytes) {
  static_assert(
      std::is_same<T, uint64_t>::value || std::is_same<T, uint32_t>::value,
      "Incorrect type passed to function UintToLEBytes.");

  DCHECK(bytes);
  size_t vec_size = sizeof(T) / sizeof(uint8_t);
  *bytes = std::vector<uint8_t>(vec_size);

  uint8_t* ptr = reinterpret_cast<uint8_t*>(&val);
  for (size_t i = 0; i < vec_size; i++) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
    bytes->at(i) = *ptr++;
#else
    bytes->at(vec_size - 1 - i) = *ptr++;
#endif
  }
}

}  // namespace

namespace brave_wallet {

namespace solana {

namespace system_program {

// Transfer lamports from funding account (from) to recipient account (to).
// Account references:
//   0. Funding account [signer, writable].
//   1. Recipient account [non-signer, writable].
// Insturction data: u32 instruction index and u64 lamport.
std::optional<SolanaInstruction> Transfer(const std::string& from_pubkey,
                                          const std::string& to_pubkey,
                                          uint64_t lamport) {
  if (from_pubkey.empty() || to_pubkey.empty()) {
    return std::nullopt;
  }

  // Instruction data is consisted of u32 instruction index and u64 lamport.
  std::vector<uint8_t> instruction_data;
  UintToLEBytes(
      static_cast<uint32_t>(mojom::SolanaSystemInstruction::kTransfer),
      &instruction_data);

  std::vector<uint8_t> lamport_bytes;
  UintToLEBytes(lamport, &lamport_bytes);
  instruction_data.insert(instruction_data.end(), lamport_bytes.begin(),
                          lamport_bytes.end());

  return SolanaInstruction(
      mojom::kSolanaSystemProgramId,
      std::vector<SolanaAccountMeta>(
          {SolanaAccountMeta(from_pubkey, std::nullopt, true, true),
           SolanaAccountMeta(to_pubkey, std::nullopt, false, true)}),
      instruction_data);
}

}  // namespace system_program

namespace spl_token_program {

// Transfers amount of tokens from source account to destination either
// directly or via a delegate.
// Account references for single owner/delegate:
//   0. Source account [non-signer, writable].
//   1. Destination account [non-signer, writable].
//   2. Authority account (source account's owner/delegate) [signer, readonly]
// Account references for multisignature owner/delegate:
//   0. Source account [non-signer, writable].
//   1. Destination account [non-signer, writable].
//   2. Authority account (source account's multisignature owner/delegate)
//      [non-signer, readonly]
//   3~3+M. M signer accounts [signer, readonly].
// Insturction data: u8 instruction index and u64 amount.
std::optional<SolanaInstruction> Transfer(
    const std::string& token_program_id,
    const std::string& source_pubkey,
    const std::string& destination_pubkey,
    const std::string& authority_pubkey,
    const std::vector<std::string>& signer_pubkeys,
    uint64_t amount) {
  if (token_program_id.empty() || source_pubkey.empty() ||
      destination_pubkey.empty() || authority_pubkey.empty()) {
    return std::nullopt;
  }

  // Instruction data is consisted of u8 instruction index and u64 amount.
  std::vector<uint8_t> instruction_data = {
      static_cast<uint8_t>(mojom::SolanaTokenInstruction::kTransfer)};

  std::vector<uint8_t> amount_bytes;
  UintToLEBytes(amount, &amount_bytes);
  instruction_data.insert(instruction_data.end(), amount_bytes.begin(),
                          amount_bytes.end());

  std::vector<SolanaAccountMeta> account_metas = {
      SolanaAccountMeta(source_pubkey, std::nullopt, false, true),
      SolanaAccountMeta(destination_pubkey, std::nullopt, false, true),
      SolanaAccountMeta(authority_pubkey, std::nullopt, signer_pubkeys.empty(),
                        false)};

  for (const auto& signer_pubkey : signer_pubkeys) {
    account_metas.emplace_back(signer_pubkey, std::nullopt, true, false);
  }

  return SolanaInstruction(token_program_id, std::move(account_metas),
                           instruction_data);
}

}  // namespace spl_token_program

namespace spl_associated_token_account_program {

// Create an associated token account for the given wallet address and token
// mint.
// Account references:
// 0. Funding account (must be a system account) [signer, writeable].
// 1. Associated token account address to be created [non-signer, writable].
// 2. Wallet address for the new associated token account [non-signer,
//    readonly].
// 3. The token mint for the new associated token account [non-signer,
//    readonly].
// 4. System program [non-signer, readonly].
// 5. SPL Token program [non-signer, readonly].
// Ref:
// https://docs.rs/spl-associated-token-account/1.1.2/spl_associated_token_account/instruction/enum.AssociatedTokenAccountInstruction.html#variant.Create
std::optional<SolanaInstruction> CreateAssociatedTokenAccount(
    const std::string& funding_address,
    const std::string& wallet_address,
    const std::string& associated_token_account_address,
    const std::string& spl_token_mint_address) {
  if (funding_address.empty() || wallet_address.empty() ||
      associated_token_account_address.empty() ||
      spl_token_mint_address.empty()) {
    return std::nullopt;
  }

  std::vector<SolanaAccountMeta> account_metas = {
      SolanaAccountMeta(funding_address, std::nullopt, true, true),
      SolanaAccountMeta(associated_token_account_address, std::nullopt, false,
                        true),
      SolanaAccountMeta(wallet_address, std::nullopt, false, false),
      SolanaAccountMeta(spl_token_mint_address, std::nullopt, false, false),
      SolanaAccountMeta(mojom::kSolanaSystemProgramId, std::nullopt, false,
                        false),
      SolanaAccountMeta(mojom::kSolanaTokenProgramId, std::nullopt, false,
                        false)};
  return SolanaInstruction(mojom::kSolanaAssociatedTokenProgramId,
                           std::move(account_metas), std::vector<uint8_t>());
}

}  // namespace spl_associated_token_account_program

namespace bubblegum_program {

std::optional<SolanaInstruction> Transfer(
    uint32_t canopy_depth,
    const std::string& tree_authority,
    // const std::string& leaf_owner,
    // const std::string& leaf_delegate, // potentially from simple hash
    const std::string& new_leaf_owner,
    // const std::string& merkle_tree, // potentially from simple hash
    // const std::string& system_program,
    const SolCompressedNftProofData& proof) {
  const std::string compression_program =
      "cmtDvXumGCrqC1Age74AVPhSRVXJMd8PJS91L8KbNCK";
  const std::string log_wrapper = "noopb9bkMVfRPU8AsbpTUg8AQkHtKwMYZiFUjNRtMmV";

  // export type TransferInstructionArgs = {
  //   root: number[] /* size: 32 */;
  //   dataHash: number[] /* size: 32 */;
  //   creatorHash: number[] /* size: 32 */;
  //   nonce: beet.bignum;
  //   index: number;
  // };

  std::vector<uint8_t> instruction_data;

  // Transfer instruction discriminator
  std::vector<uint8_t> transfer_instruction_discriminator = {163, 52, 200, 231,
                                                             140, 3,  69,  186};
  instruction_data.insert(instruction_data.end(),
                          transfer_instruction_discriminator.begin(),
                          transfer_instruction_discriminator.end());

  // std::vector<uint8_t> instruction_data = {
  //     static_cast<uint8_t>(mojom::SolanaBubblegumInstruction::kTransfer)};

  // Root
  std::vector<uint8_t> root_bytes;
  if (!Base58Decode(proof.root, &root_bytes, 32)) {
    return std::nullopt;
  }
  instruction_data.insert(instruction_data.end(), root_bytes.begin(),
                          root_bytes.end());

  // Data hash
  std::vector<uint8_t> data_hash_bytes;
  if (!Base58Decode(proof.data_hash, &data_hash_bytes, 32)) {
    return std::nullopt;
  }
  instruction_data.insert(instruction_data.end(), data_hash_bytes.begin(),
                          data_hash_bytes.end());

  // Creator hash
  std::vector<uint8_t> creator_hash_bytes;
  if (!Base58Decode(proof.creator_hash, &creator_hash_bytes, 32)) {
    return std::nullopt;
  }
  instruction_data.insert(instruction_data.end(), creator_hash_bytes.begin(),
                          creator_hash_bytes.end());

  std::vector<uint8_t> tempVec;

  // Nonce
  tempVec.clear();  // Clear temporary vector for reuse
  UintToLEBytes(static_cast<uint64_t>(proof.leaf_index), &tempVec);
  instruction_data.insert(instruction_data.end(), tempVec.begin(),
                          tempVec.end());

  // Index
  tempVec.clear();  // Clear it again for the next use
  UintToLEBytes(static_cast<uint32_t>(proof.leaf_index), &tempVec);
  instruction_data.insert(instruction_data.end(), tempVec.begin(),
                          tempVec.end());

  // export type TransferInstructionAccounts = {
  //   treeAuthority: web3.PublicKey;
  //   leafOwner: web3.PublicKey;
  //   leafDelegate: web3.PublicKey;
  //   newLeafOwner: web3.PublicKey;
  //   merkleTree: web3.PublicKey;
  //   logWrapper: web3.PublicKey;
  //   compressionProgram: web3.PublicKey;
  //   systemProgram?: web3.PublicKey;
  //   anchorRemainingAccounts?: web3.AccountMeta[];
  // };

  // Create account metas.
  std::vector<SolanaAccountMeta> account_metas({
      SolanaAccountMeta(tree_authority, std::nullopt, false, false),
      SolanaAccountMeta(proof.owner, std::nullopt, false, false),
      SolanaAccountMeta(proof.owner, std::nullopt, false, false),
      // SolanaAccountMeta(proof.delegate, std::nullopt, false, false),
      SolanaAccountMeta(new_leaf_owner, std::nullopt, false, false),
      SolanaAccountMeta(proof.merkle_tree, std::nullopt, false, true),
      SolanaAccountMeta(log_wrapper, std::nullopt, false, false),
      SolanaAccountMeta(compression_program, std::nullopt, false, false),
      SolanaAccountMeta(mojom::kSolanaSystemProgramId, std::nullopt, false,
                        false),
  });

  // Add on the slice of our proof
  LOG(ERROR) << "proof.proof.size() is " << proof.proof.size();
  size_t end = proof.proof.size() - proof.canopy_depth;
  LOG(ERROR) << "end is " << end;
  for (size_t i = 0; i < end; ++i) {
    account_metas.push_back(
        SolanaAccountMeta(proof.proof[i], std::nullopt, false, false));
  }

  return SolanaInstruction(mojom::kSolanaBubbleGumProgramId,
                           std::move(account_metas), instruction_data);
}

}  // namespace bubblegum_program

}  // namespace solana

}  // namespace brave_wallet
