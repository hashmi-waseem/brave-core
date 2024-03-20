/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_SIMPLE_HASH_CLIENT_H_
#define BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_SIMPLE_HASH_CLIENT_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/barrier_callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/components/brave_wallet/common/brave_wallet_types.h"
#include "brave/components/brave_wallet/common/solana_address.h"

namespace brave_wallet {

struct SolCompressedNftProofData {
  std::string root;
  std::string data_hash;
  std::string creator_hash;
  std::string owner;
  std::vector<std::string> proof;
  std::string merkle_tree;
  std::string delegate;
  uint64_t leaf_index;
  uint64_t canopy_depth;

  SolCompressedNftProofData();
  SolCompressedNftProofData(const SolCompressedNftProofData& data);
  ~SolCompressedNftProofData();
};

class SimpleHashClient {
 public:
  using APIRequestHelper = api_request_helper::APIRequestHelper;
  using APIRequestResult = api_request_helper::APIRequestResult;

  SimpleHashClient(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);

  SimpleHashClient(const SimpleHashClient&) = delete;
  SimpleHashClient& operator=(SimpleHashClient&) = delete;
  ~SimpleHashClient();

  // For discovering NFTs on Solana and Ethereum
  using FetchNFTsFromSimpleHashCallback =
      base::OnceCallback<void(std::vector<mojom::BlockchainTokenPtr> nfts,
                              const std::optional<std::string>& cursor)>;

  using FetchAllNFTsFromSimpleHashCallback =
      base::OnceCallback<void(std::vector<mojom::BlockchainTokenPtr> nfts)>;

  using FetchSolCompressedNftProofDataCallback =
      base::OnceCallback<void(std::optional<SolCompressedNftProofData>)>;

  using GetNftBalanceCallback =
      base::OnceCallback<void(std::optional<uint64_t> balance)>;

  void FetchNFTsFromSimpleHash(const std::string& account_address,
                               const std::vector<std::string>& chain_ids,
                               mojom::CoinType coin,
                               const std::optional<std::string>& cursor,
                               bool skip_spam,
                               bool only_spam,
                               FetchNFTsFromSimpleHashCallback callback);

  void FetchAllNFTsFromSimpleHash(const std::string& account_address,
                                  const std::vector<std::string>& chain_ids,
                                  mojom::CoinType coin,
                                  FetchAllNFTsFromSimpleHashCallback callback);

  void FetchSolCompressedNftProofData(
      const std::string& token_address,
      FetchSolCompressedNftProofDataCallback callback);

  void GetNftBalance(const std::string& wallet_address,
                     const std::string& chain_id,
                     const std::string& contract_address,
                     const std::string& token_id,
                     mojom::CoinType coin,
                     GetNftBalanceCallback callback);

 private:
  FRIEND_TEST_ALL_PREFIXES(SimpleHashClientUnitTest, DecodeMintAddress);
  FRIEND_TEST_ALL_PREFIXES(SimpleHashClientUnitTest,
                           GetSimpleHashNftsByWalletUrl);
  FRIEND_TEST_ALL_PREFIXES(SimpleHashClientUnitTest, ParseNFTsFromSimpleHash);
  FRIEND_TEST_ALL_PREFIXES(SimpleHashClientUnitTest,
                           ParseSolCompressedNftProofData);
  FRIEND_TEST_ALL_PREFIXES(SimpleHashClientUnitTest, ParseNftOwners);

  void OnFetchNFTsFromSimpleHash(mojom::CoinType coin,
                                 bool skip_spam,
                                 bool only_spam,
                                 FetchNFTsFromSimpleHashCallback callback,
                                 APIRequestResult api_request_result);

  void OnFetchAllNFTsFromSimpleHash(
      std::vector<mojom::BlockchainTokenPtr> nfts_so_far,
      const std::string& account_address,
      const std::vector<std::string>& chain_ids,
      mojom::CoinType coin,
      FetchAllNFTsFromSimpleHashCallback callback,
      std::vector<mojom::BlockchainTokenPtr> nfts,
      const std::optional<std::string>& next_cursor);

  void OnGetNftBalance(const std::string& wallet_address,
                       GetNftBalanceCallback callback,
                       APIRequestResult api_request_result);

  void OnFetchSolCompressedNftProofData(
      FetchSolCompressedNftProofDataCallback callback,
      APIRequestResult api_request_result);

  std::optional<std::pair<std::optional<std::string>,
                          std::vector<mojom::BlockchainTokenPtr>>>
  ParseNFTsFromSimpleHash(const base::Value& json_value,
                          mojom::CoinType coin,
                          bool skip_spam,
                          bool only_spam);

  std::optional<std::vector<std::pair<std::string, uint64_t>>> ParseNftOwners(
      const base::Value& json_value);

  std::optional<SolCompressedNftProofData> ParseSolCompressedNftProofData(
      const base::Value& json_value);

  static GURL GetSimpleHashNftsByWalletUrl(
      const std::string& account_address,
      const std::vector<std::string>& chain_ids,
      const std::optional<std::string>& cursor);

  static GURL GetNftUrl(const std::string& token_id,
                        const std::string& contract_address,
                        const std::string& chain_id,
                        mojom::CoinType coin);

  std::unique_ptr<APIRequestHelper> api_request_helper_;
  base::WeakPtrFactory<SimpleHashClient> weak_ptr_factory_;
};

}  // namespace brave_wallet

#endif  // BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_SIMPLE_HASH_CLIENT_H_
