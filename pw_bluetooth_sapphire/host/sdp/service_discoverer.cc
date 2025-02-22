// Copyright 2023 The Pigweed Authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "pw_bluetooth_sapphire/internal/host/sdp/service_discoverer.h"

#include <pw_assert/check.h>

#include <cinttypes>
#include <functional>

namespace bt::sdp {

ServiceDiscoverer::ServiceDiscoverer() : next_id_(1) {}

ServiceDiscoverer::SearchId ServiceDiscoverer::AddSearch(
    const UUID& uuid,
    std::unordered_set<AttributeId> attributes,
    ResultCallback callback) {
  Search s;
  s.uuid = uuid;
  s.attributes = std::move(attributes);
  s.callback = std::move(callback);
  PW_DCHECK(next_id_ < std::numeric_limits<ServiceDiscoverer::SearchId>::max());
  ServiceDiscoverer::SearchId id = next_id_++;
  auto [it, placed] = searches_.emplace(id, std::move(s));
  PW_DCHECK(placed, "Should always be able to place new search");
  return id;
}

bool ServiceDiscoverer::RemoveSearch(SearchId id) {
  auto it = sessions_.begin();
  while (it != sessions_.end()) {
    if (it->second.active.erase(id) && it->second.active.empty()) {
      it = sessions_.erase(it);
    } else {
      it++;
    }
  }
  return searches_.erase(id);
}

void ServiceDiscoverer::SingleSearch(SearchId search_id,
                                     PeerId peer_id,
                                     std::unique_ptr<Client> client) {
  auto session_iter = sessions_.find(peer_id);
  if (session_iter == sessions_.end()) {
    if (client == nullptr) {
      // Can't do a search if we don't have an open channel
      bt_log(WARN,
             "sdp",
             "Can't start a new session without a channel (peer_id %s)",
             bt_str(peer_id));
      return;
    }
    // Setup the session.
    DiscoverySession session;
    session.client = std::move(client);
    auto placed = sessions_.emplace(peer_id, std::move(session));
    PW_DCHECK(placed.second);
    session_iter = placed.first;
  }
  PW_DCHECK(session_iter != sessions_.end());
  auto search_it = searches_.find(search_id);
  if (search_it == searches_.end()) {
    bt_log(INFO, "sdp", "Couldn't find search with id %" PRIu64, search_id);
    return;
  }
  Search& search = search_it->second;
  Client::SearchResultFunction result_cb =
      [this, peer_id, search_id](
          fit::result<
              Error<>,
              std::reference_wrapper<const std::map<AttributeId, DataElement>>>
              attributes_result) {
        auto it = searches_.find(search_id);
        if (it == searches_.end() || attributes_result.is_error()) {
          FinishPeerSearch(peer_id, search_id);
          return false;
        }
        it->second.callback(peer_id, attributes_result.value());
        return true;
      };
  session_iter->second.active.emplace(search_id);
  session_iter->second.client->ServiceSearchAttributes(
      {search.uuid}, search.attributes, std::move(result_cb));
}

bool ServiceDiscoverer::StartServiceDiscovery(PeerId peer_id,
                                              std::unique_ptr<Client> client) {
  // If discovery is already happening on this peer, then we can't start it
  // again.
  if (sessions_.count(peer_id)) {
    return false;
  }
  // If there aren't any searches to do, we're done.
  if (searches_.empty()) {
    return true;
  }
  for (auto& [search_id, _] : searches_) {
    SingleSearch(search_id, peer_id, std::move(client));
    client = nullptr;
  }
  return true;
}

size_t ServiceDiscoverer::search_count() const { return searches_.size(); }

void ServiceDiscoverer::FinishPeerSearch(PeerId peer_id, SearchId search_id) {
  auto it = sessions_.find(peer_id);
  if (it == sessions_.end()) {
    bt_log(INFO,
           "sdp",
           "Couldn't find session to finish search for peer %s",
           bt_str(peer_id));
    return;
  }
  if (it->second.active.erase(search_id) && it->second.active.empty()) {
    // This peer search is over.
    sessions_.erase(it);
  }
}

}  // namespace bt::sdp
