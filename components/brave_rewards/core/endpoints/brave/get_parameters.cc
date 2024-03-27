/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/core/endpoints/brave/get_parameters.h"

#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "brave/components/brave_rewards/core/common/environment_config.h"
<<<<<<< HEAD
#include "brave/components/brave_rewards/core/rewards_engine.h"
#include "net/http/http_status_code.h"

namespace brave_rewards::internal::endpoints {
using Error = GetParameters::Error;
using Result = GetParameters::Result;

namespace {

Result ParseBody(RewardsEngine& engine, const std::string& body) {
  const auto value = base::JSONReader::Read(body);
  if (!value || !value->is_dict()) {
    engine.LogError(FROM_HERE) << "Failed to parse body";
    return base::unexpected(Error::kFailedToParseBody);
  }

  const auto& dict = value->GetDict();

  const auto rate = dict.FindDouble("batRate");
  if (!rate) {
    engine.LogError(FROM_HERE) << "Failed to parse body";
    return base::unexpected(Error::kFailedToParseBody);
  }
  auto parameters = mojom::RewardsParameters::New();
  parameters->rate = *rate;

  const auto auto_contribute_choice =
      dict.FindDoubleByDottedPath("autocontribute.defaultChoice");
  if (!auto_contribute_choice) {
    engine.LogError(FROM_HERE) << "Failed to parse body";
    return base::unexpected(Error::kFailedToParseBody);
  }
  parameters->auto_contribute_choice = *auto_contribute_choice;

  const auto* auto_contribute_choices =
      dict.FindListByDottedPath("autocontribute.choices");
  if (!auto_contribute_choices || auto_contribute_choices->empty()) {
    engine.LogError(FROM_HERE) << "Failed to parse body";
    return base::unexpected(Error::kFailedToParseBody);
  }

  for (const auto& choice : *auto_contribute_choices) {
    if (choice.is_double() || choice.is_int()) {
      parameters->auto_contribute_choices.push_back(choice.GetDouble());
    }
  }

  const auto* tip_choices = dict.FindListByDottedPath("tips.defaultTipChoices");
  if (!tip_choices || tip_choices->empty()) {
    engine.LogError(FROM_HERE) << "Failed to parse body";
    return base::unexpected(Error::kFailedToParseBody);
  }

  for (const auto& choice : *tip_choices) {
    if (choice.is_double() || choice.is_int()) {
      parameters->tip_choices.push_back(choice.GetDouble());
    }
  }

  const auto* monthly_tip_choices =
      dict.FindListByDottedPath("tips.defaultMonthlyChoices");
  if (!monthly_tip_choices || monthly_tip_choices->empty()) {
    engine.LogError(FROM_HERE) << "Failed to parse body";
    return base::unexpected(Error::kFailedToParseBody);
  }

  for (const auto& choice : *monthly_tip_choices) {
    if (choice.is_double() || choice.is_int()) {
      parameters->monthly_tip_choices.push_back(choice.GetDouble());
    }
  }

  const auto* payout_status = dict.FindDict("payoutStatus");
  if (!payout_status) {
    engine.LogError(FROM_HERE) << "Failed to parse body";
    return base::unexpected(Error::kFailedToParseBody);
  }

  for (const auto [k, v] : *payout_status) {
    if (v.is_string()) {
      parameters->payout_status.emplace(k, v.GetString());
    }
  }

  const auto* custodian_regions = dict.Find("custodianRegions");
  if (!custodian_regions) {
    engine.LogError(FROM_HERE) << "Failed to parse body";
    return base::unexpected(Error::kFailedToParseBody);
  }

  auto wallet_provider_regions =
      GetParameters::ValueToWalletProviderRegions(*custodian_regions);
  if (!wallet_provider_regions) {
    engine.LogError(FROM_HERE) << "Failed to parse body";
    return base::unexpected(Error::kFailedToParseBody);
  }

  parameters->wallet_provider_regions = std::move(*wallet_provider_regions);

  const auto* vbat_deadline = dict.FindString("vbatDeadline");
  if (vbat_deadline) {
    if (base::Time time;
        base::Time::FromUTCString(vbat_deadline->c_str(), &time)) {
      parameters->vbat_deadline = time;
    }
  }

  const auto vbat_expired = dict.FindBool("vbatExpired");
  if (vbat_expired) {
    parameters->vbat_expired = *vbat_expired;
  }

  if (const auto tos_version = dict.FindInt("tosVersion")) {
    parameters->tos_version = *tos_version;
  }

  return parameters;
}

}  // namespace

// static
Result GetParameters::ProcessResponse(RewardsEngine& engine,
                                      const mojom::UrlResponse& response) {
  switch (response.status_code) {
    case net::HTTP_OK:  // HTTP 200
      return ParseBody(engine, response.body);
    case net::HTTP_INTERNAL_SERVER_ERROR:  // HTTP 500
      engine.LogError(FROM_HERE) << "Failed to get parameters";
      return base::unexpected(Error::kFailedToGetParameters);
    default:
      engine.LogError(FROM_HERE)
          << "Unexpected status code! (HTTP " << response.status_code << ')';
      return base::unexpected(Error::kUnexpectedStatusCode);
  }
}

GetParameters::GetParameters(RewardsEngine& engine) : RequestBuilder(engine) {}
=======
#include "brave/components/brave_rewards/core/common/url_loader.h"
#include "net/http/http_status_code.h"

namespace brave_rewards::internal::endpoints {

GetParameters::GetParameters(RewardsEngineImpl& engine)
    : RewardsEngineHelper(engine) {}
>>>>>>> 030783a814 (Refactor parameters endpoint class)

GetParameters::~GetParameters() = default;

void GetParameters::Request(RequestCallback callback) {
  Get<URLLoader>().Load(
      CreateRequest(), URLLoader::LogLevel::kDetailed,
      base::BindOnce(&GetParameters::OnResponse, weak_factory_.GetWeakPtr(),
                     std::move(callback)));
}

std::optional<GetParameters::ProviderRegionsMap>
GetParameters::ValueToWalletProviderRegions(const base::Value& value) {
  auto* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }

  auto get_list = [](const std::string& name, const base::Value::Dict& dict) {
    std::vector<std::string> countries;
    if (auto* list = dict.FindList(name)) {
      for (auto& country : *list) {
        if (country.is_string()) {
          countries.emplace_back(country.GetString());
        }
      }
    }
    return countries;
  };

  base::flat_map<std::string, mojom::RegionsPtr> regions_map;

  for (auto [wallet_provider, regions_value] : *dict) {
    if (auto* regions = regions_value.GetIfDict()) {
      regions_map.emplace(wallet_provider,
                          mojom::Regions::New(get_list("allow", *regions),
                                              get_list("block", *regions)));
    }
  }

  return regions_map;
}

mojom::UrlRequestPtr GetParameters::CreateRequest() {
  auto request = mojom::UrlRequest::New();
  request->method = mojom::UrlMethod::GET;
  request->url = Get<EnvironmentConfig>()
                     .rewards_api_url()
                     .Resolve("/v1/parameters")
                     .spec();
  return request;
}

GetParameters::Result GetParameters::MapResponse(
    const mojom::UrlResponse& response) {
  if (!URLLoader::IsSuccessCode(response.status_code)) {
    LogError(FROM_HERE) << "Unexpected status code: " << response.status_code;
    return base::unexpected(Error::kUnexpectedStatusCode);
  }

  auto value = base::JSONReader::Read(response.body);
  if (!value || !value->is_dict()) {
    LogError(FROM_HERE) << "Invalid JSON";
    return base::unexpected(Error::kFailedToParseBody);
  }

  auto& dict = value->GetDict();

  auto rate = dict.FindDouble("batRate");
  if (!rate) {
    LogError(FROM_HERE) << "Missing batRate";
    return base::unexpected(Error::kFailedToParseBody);
  }

  auto parameters = mojom::RewardsParameters::New();

  parameters->rate = *rate;

  auto auto_contribute_choice =
      dict.FindDoubleByDottedPath("autocontribute.defaultChoice");
  if (!auto_contribute_choice) {
    LogError(FROM_HERE) << "Missing autocontribute.defaultChoice";
    return base::unexpected(Error::kFailedToParseBody);
  }
  parameters->auto_contribute_choice = *auto_contribute_choice;

  auto* auto_contribute_choices =
      dict.FindListByDottedPath("autocontribute.choices");
  if (!auto_contribute_choices || auto_contribute_choices->empty()) {
    LogError(FROM_HERE) << "Missing autocontribute.choices";
    return base::unexpected(Error::kFailedToParseBody);
  }

  for (auto& choice : *auto_contribute_choices) {
    if (choice.is_double() || choice.is_int()) {
      parameters->auto_contribute_choices.push_back(choice.GetDouble());
    }
  }

  auto* tip_choices = dict.FindListByDottedPath("tips.defaultTipChoices");
  if (!tip_choices || tip_choices->empty()) {
    LogError(FROM_HERE) << "Missing tips.defaultTipChoices";
    return base::unexpected(Error::kFailedToParseBody);
  }

  for (auto& choice : *tip_choices) {
    if (choice.is_double() || choice.is_int()) {
      parameters->tip_choices.push_back(choice.GetDouble());
    }
  }

  auto* monthly_tip_choices =
      dict.FindListByDottedPath("tips.defaultMonthlyChoices");
  if (!monthly_tip_choices || monthly_tip_choices->empty()) {
    LogError(FROM_HERE) << "Missing tips.defaultMonthlyChoices";
    return base::unexpected(Error::kFailedToParseBody);
  }

  for (auto& choice : *monthly_tip_choices) {
    if (choice.is_double() || choice.is_int()) {
      parameters->monthly_tip_choices.push_back(choice.GetDouble());
    }
  }

  auto* payout_status = dict.FindDict("payoutStatus");
  if (!payout_status) {
    LogError(FROM_HERE) << "Missing payoutStatus";
    return base::unexpected(Error::kFailedToParseBody);
  }

  for (auto [k, v] : *payout_status) {
    if (v.is_string()) {
      parameters->payout_status.emplace(k, v.GetString());
    }
  }

  auto* custodian_regions = dict.Find("custodianRegions");
  if (!custodian_regions) {
    LogError(FROM_HERE) << "Missing custodianRegions";
    return base::unexpected(Error::kFailedToParseBody);
  }

  auto wallet_provider_regions =
      ValueToWalletProviderRegions(*custodian_regions);
  if (!wallet_provider_regions) {
    LogError(FROM_HERE) << "Invalid custodianRegions";
    return base::unexpected(Error::kFailedToParseBody);
  }

  parameters->wallet_provider_regions = std::move(*wallet_provider_regions);

  if (auto* vbat_deadline = dict.FindString("vbatDeadline")) {
    if (base::Time time;
        base::Time::FromUTCString(vbat_deadline->c_str(), &time)) {
      parameters->vbat_deadline = time;
    }
  }

  if (auto vbat_expired = dict.FindBool("vbatExpired")) {
    parameters->vbat_expired = *vbat_expired;
  }

  if (auto tos_version = dict.FindInt("tosVersion")) {
    parameters->tos_version = *tos_version;
  }

  return parameters;
}

void GetParameters::OnResponse(RequestCallback callback,
                               mojom::UrlResponsePtr response) {
  std::move(callback).Run(MapResponse(*response));
}

}  // namespace brave_rewards::internal::endpoints
