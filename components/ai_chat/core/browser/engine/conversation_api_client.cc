// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/ai_chat/core/browser/engine/conversation_api_client.h"

#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/types/expected.h"
#include "brave/brave_domains/service_domains.h"
#include "brave/components/ai_chat/core/common/buildflags/buildflags.h"
#include "brave/components/ai_chat/core/common/features.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/brave_service_keys/brave_service_key_utils.h"
#include "brave/components/constants/brave_services_key.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace ai_chat {

namespace {

using ConversationEvent = ConversationAPIClient::ConversationEvent;
using ConversationEventType = ConversationAPIClient::ConversationEventType;

constexpr char kRemotePath[] = "v1/conversation";
constexpr char kHttpMethod[] = "POST";

net::NetworkTrafficAnnotationTag GetNetworkTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation("ai_chat", R"(
      semantics {
        sender: "AI Chat"
        description:
          "This is used to communicate with Brave's AI Conversation API"
          "on behalf of the user interacting with different browser AI"
          "features."
        trigger:
          "Triggered by user interactions such as submitting an AI Chat"
          "conversation message, or requesting a text rewrite."
        data:
          "Conversational messages input by the user as well as associated"
          "content or user text to be rewritten. Can contain PII."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        policy_exception_justification:
          "Not implemented."
      }
    )");
}

base::Value::List ConversationEventsToList(
    const std::vector<ConversationEvent>& conversation) {
  base::Value::List events;
  for (const auto& event : conversation) {
    base::Value::Dict event_dict;
    // Set role
    switch (event.role) {
      case mojom::CharacterType::HUMAN:
        event_dict.Set("role", "user");
        break;
      case mojom::CharacterType::ASSISTANT:
        event_dict.Set("role", "assistant");
        break;
      default:
        NOTREACHED();
        break;
    }

    switch (event.type) {
      case ConversationEventType::ContextURL:
        event_dict.Set("type", "contextURL");
        break;
      case ConversationEventType::UserText:
        event_dict.Set("type", "userText");
        break;
      case ConversationEventType::PageText:
        event_dict.Set("type", "pageText");
        break;
      case ConversationEventType::PageExcerpt:
        event_dict.Set("type", "pageExcerpt");
        break;
      case ConversationEventType::VideoTranscriptXML:
        event_dict.Set("type", "videoTranscriptXML");
        break;
      case ConversationEventType::VideoTranscriptVTT:
        event_dict.Set("type", "videoTranscriptVTT");
        break;
      case ConversationEventType::ChatMessage:
        event_dict.Set("type", "chatMessage");
        break;
      case ConversationEventType::RequestRewrite:
        event_dict.Set("type", "requestRewrite");
        break;
      case ConversationEventType::RequestSummary:
        event_dict.Set("type", "requestSummary");
        break;
      case ConversationEventType::RequestSuggestedActions:
        event_dict.Set("type", "requestSuggestedActions");
        break;
      case ConversationEventType::SuggestedActions:
        event_dict.Set("type", "suggestedActions");
        break;
      default:
        NOTREACHED();
        break;
    }
    event_dict.Set("content", event.content);
    events.Append(std::move(event_dict));
  }
  return events;
}

GURL GetEndpointUrl(bool premium, const std::string& path) {
  DCHECK(!path.starts_with("/"));

  auto* prefix = premium ? "ai-chat-premium.bsg" : "ai-chat.bsg";
  auto hostname = brave_domains::GetServicesDomain(
      prefix, brave_domains::ServicesEnvironment::DEV);

  GURL url{base::StrCat(
      {url::kHttpsScheme, url::kStandardSchemeSeparator, hostname, "/", path})};

  DCHECK(url.is_valid()) << "Invalid API Url: " << url.spec();

  return url;
}

}  // namespace

ConversationAPIClient::ConversationAPIClient(
    const std::string& model_name,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    AIChatCredentialManager* credential_manager)
    : model_name_(model_name),
      api_request_helper_(GetNetworkTrafficAnnotationTag(), url_loader_factory),
      credential_manager_(credential_manager) {
  DCHECK(!model_name_.empty());
}

ConversationAPIClient::~ConversationAPIClient() = default;

void ConversationAPIClient::ClearAllQueries() {
  api_request_helper_.CancelAll();
}

void ConversationAPIClient::PerformRequest(
    const std::vector<ConversationEvent>& conversation,
    GenerationDataCallback data_received_callback,
    GenerationCompletedCallback completed_callback) {
  // Get credentials and then perform request
  auto callback = base::BindOnce(
      &ConversationAPIClient::PerformRequestWithCredentials,
      weak_ptr_factory_.GetWeakPtr(), std::move(conversation),
      std::move(data_received_callback), std::move(completed_callback));
  credential_manager_->FetchPremiumCredential(std::move(callback));
}

std::string ConversationAPIClient::CreateJSONRequestBody(
    const std::vector<ConversationEvent>& conversation,
    const bool is_sse_enabled) {
  DCHECK(!model_name_.empty());
  base::Value::Dict dict;

  dict.Set("events", ConversationEventsToList(conversation));
  dict.Set("model", model_name_);
  dict.Set("stream", is_sse_enabled);

  std::string json;
  base::JSONWriter::Write(dict, &json);
  return json;
}

void ConversationAPIClient::PerformRequestWithCredentials(
    const std::vector<ConversationEvent>& conversation,
    GenerationDataCallback data_received_callback,
    GenerationCompletedCallback completed_callback,
    std::optional<CredentialCacheEntry> credential) {
  bool premium_enabled = credential.has_value();
  const GURL api_url = GetEndpointUrl(premium_enabled, kRemotePath);
  const bool is_sse_enabled =
      ai_chat::features::kAIChatSSE.Get() && !data_received_callback.is_null();
  const std::string request_body =
      CreateJSONRequestBody(std::move(conversation), is_sse_enabled);

  base::flat_map<std::string, std::string> headers;
  const auto digest_header = brave_service_keys::GetDigestHeader(request_body);
  headers.emplace(digest_header.first, digest_header.second);
  auto result = brave_service_keys::GetAuthorizationHeader(
      BUILDFLAG(SERVICE_KEY_AICHAT), headers, api_url, kHttpMethod, {"digest"});
  if (result) {
    std::pair<std::string, std::string> authorization_header = result.value();
    headers.emplace(authorization_header.first, authorization_header.second);
  }

  if (premium_enabled) {
    // Add Leo premium SKU credential as a Cookie header.
    std::string cookie_header_value =
        "__Secure-sku#brave-leo-premium=" + credential->credential;
    headers.emplace("Cookie", cookie_header_value);
  }
  headers.emplace("x-brave-key", BUILDFLAG(BRAVE_SERVICES_KEY));
  headers.emplace("Accept", "text/event-stream");

  if (is_sse_enabled) {
    DVLOG(2) << "Making streaming AI Chat Conversation API Request";
    auto on_received = base::BindRepeating(
        &ConversationAPIClient::OnQueryDataReceived,
        weak_ptr_factory_.GetWeakPtr(), std::move(data_received_callback));
    auto on_complete =
        base::BindOnce(&ConversationAPIClient::OnQueryCompleted,
                       weak_ptr_factory_.GetWeakPtr(), credential,
                       std::move(completed_callback));

    api_request_helper_.RequestSSE(kHttpMethod, api_url, request_body,
                                   "application/json", std::move(on_received),
                                   std::move(on_complete), headers, {});
  } else {
    DVLOG(2) << "Making non-streaming AI Chat Conversation API Request";
    auto on_complete =
        base::BindOnce(&ConversationAPIClient::OnQueryCompleted,
                       weak_ptr_factory_.GetWeakPtr(), credential,
                       std::move(completed_callback));

    api_request_helper_.Request(kHttpMethod, api_url, request_body,
                                "application/json", std::move(on_complete),
                                headers, {});
  }
}

void ConversationAPIClient::OnQueryCompleted(
    std::optional<CredentialCacheEntry> credential,
    GenerationCompletedCallback callback,
    APIRequestResult result) {
  const bool success = result.Is2XXResponseCode();
  // Handle successful request
  if (success) {
    std::string completion = "";
    // We're checking for a value body in case for non-streaming API results.
    // TODO(petemill): server should provide parseable history events?
    if (result.value_body().is_dict()) {
      const std::string* value =
          result.value_body().GetDict().FindString("completion");
      if (value) {
        // Trimming necessary for Llama 2 which prepends responses with a " ".
        completion = base::TrimWhitespaceASCII(*value, base::TRIM_ALL);
      }
    }

    std::move(callback).Run(base::ok(std::move(completion)));
    return;
  }

  // If error code is not 401, put credential in cache
  if (result.response_code() != net::HTTP_UNAUTHORIZED && credential) {
    credential_manager_->PutCredentialInCache(std::move(*credential));
  }

  // Handle error
  mojom::APIError error;

  if (net::HTTP_TOO_MANY_REQUESTS == result.response_code()) {
    error = mojom::APIError::RateLimitReached;
  } else if (net::HTTP_REQUEST_ENTITY_TOO_LARGE == result.response_code()) {
    error = mojom::APIError::ContextLimitReached;
  } else {
    error = mojom::APIError::ConnectionIssue;
  }

  std::move(callback).Run(base::unexpected(std::move(error)));
}

void ConversationAPIClient::OnQueryDataReceived(
    GenerationDataCallback callback,
    base::expected<base::Value, std::string> result) {
  if (!result.has_value() || !result->is_dict()) {
    return;
  }
  // TODO(petemill): server should provide parseable history events?
  const std::string* completion = result->GetDict().FindString("completion");
  if (completion) {
    callback.Run(std::move(*completion));
  }
}

}  // namespace ai_chat
