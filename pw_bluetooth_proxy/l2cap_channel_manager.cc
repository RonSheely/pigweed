// Copyright 2024 The Pigweed Authors
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

#include "pw_bluetooth_proxy/internal/l2cap_channel_manager.h"

#include <mutex>

#include "pw_containers/algorithm.h"
#include "pw_log/log.h"
#include "pw_status/status.h"

namespace pw::bluetooth::proxy {

L2capChannelManager::L2capChannelManager(AclDataChannel& acl_data_channel)
    : acl_data_channel_(acl_data_channel),
      lrd_channel_(channels_.end()),
      round_robin_terminus_(channels_.end()) {}

void L2capChannelManager::RegisterChannel(L2capChannel& channel) {
  std::lock_guard lock(channels_mutex_);
  // Insert new channels before `lrd_channel_`.
  IntrusiveForwardList<L2capChannel>::iterator before_it =
      channels_.before_begin();
  for (auto it = channels_.begin(); it != lrd_channel_; ++it) {
    ++before_it;
  }
  channels_.insert_after(before_it, channel);
  if (lrd_channel_ == channels_.end()) {
    lrd_channel_ = channels_.begin();
  }
}

void L2capChannelManager::DeregisterChannel(L2capChannel& channel) {
  std::lock_guard lock(channels_mutex_);
  if (&channel == &(*lrd_channel_)) {
    Advance(lrd_channel_);
  }
  if (&channel == &(*round_robin_terminus_)) {
    Advance(round_robin_terminus_);
  }

  // Channel will only be removed once, but DeregisterChannel() may be called
  // multiple times on the same channel so it's ok for this to return false.
  channels_.remove(channel);

  // If `channel` was the only element in `channels_`, advancing channels just
  // wrapped them back on itself, so we reset it here.
  if (channels_.empty()) {
    lrd_channel_ = channels_.end();
    round_robin_terminus_ = channels_.end();
  }
}

void L2capChannelManager::DeregisterAndCloseChannels(L2capChannelEvent event) {
  std::lock_guard lock(channels_mutex_);
  while (!channels_.empty()) {
    L2capChannel& front = channels_.front();
    channels_.pop_front();
    front.InternalClose(event);
  }
  lrd_channel_ = channels_.end();
  round_robin_terminus_ = channels_.end();
}

pw::Result<H4PacketWithH4> L2capChannelManager::GetAclH4Packet(uint16_t size) {
  if (size > GetH4BuffSize()) {
    PW_LOG_ERROR(
        "Requested packet is too large for H4 buffer. So will not send.");
    return pw::Status::InvalidArgument();
  }

  std::optional<span<uint8_t>> h4_buff = h4_storage_.ReserveH4Buff();
  if (!h4_buff) {
    PW_LOG_WARN("No H4 buffers available.");
    return pw::Status::Unavailable();
  }

  H4PacketWithH4 h4_packet(span(h4_buff->data(), size),
                           /*release_fn=*/[this](const uint8_t* buffer) {
                             this->h4_storage_.ReleaseH4Buff(buffer);
                             DrainChannelQueues();
                           });
  h4_packet.SetH4Type(emboss::H4PacketType::ACL_DATA);

  return h4_packet;
}

uint16_t L2capChannelManager::GetH4BuffSize() const {
  return H4Storage::GetH4BuffSize();
}

void L2capChannelManager::DrainChannelQueues() {
  for (;;) {
    std::optional<AclDataChannel::SendCredit> credit;
    std::optional<H4PacketWithH4> packet;
    {
      std::lock_guard lock(channels_mutex_);
      if (lrd_channel_ == channels_.end()) {
        // This means the container is empty.
        return;
      }
      if (round_robin_terminus_ == channels_.end()) {
        round_robin_terminus_ = lrd_channel_;
      }
      credit = acl_data_channel_.ReserveSendCredit(lrd_channel_->transport());
      if (credit) {
        packet = lrd_channel_->DequeuePacket();
      }
      Advance(lrd_channel_);
      if (packet) {
        // Round robin should continue until we have done a full loop with no
        // packets dequeued.
        round_robin_terminus_ = lrd_channel_;
      }
    }

    if (packet) {
      // Send while unlocked. This can trigger a recursive round robin once
      // `packet` is released, but this is fine because `lrd_channel_` has
      // been adjusted so the recursive call will start where this one left off,
      // and `round_robin_terminus_` will be updated to point to channels with
      // dequeued packets.
      PW_CHECK_OK(
          acl_data_channel_.SendAcl(std::move(*packet), std::move(*credit)));
      continue;
    }

    std::lock_guard lock(channels_mutex_);
    if (lrd_channel_ == round_robin_terminus_) {
      break;
    }
  }
}

L2capChannel* L2capChannelManager::FindChannelByLocalCid(
    uint16_t connection_handle, uint16_t local_cid) {
  std::lock_guard lock(channels_mutex_);
  auto connection_it = containers::FindIf(
      channels_, [connection_handle, local_cid](const L2capChannel& channel) {
        return channel.connection_handle() == connection_handle &&
               channel.local_cid() == local_cid;
      });
  return connection_it == channels_.end() ? nullptr : &(*connection_it);
}

L2capChannel* L2capChannelManager::FindChannelByRemoteCid(
    uint16_t connection_handle, uint16_t remote_cid) {
  std::lock_guard lock(channels_mutex_);
  auto connection_it = containers::FindIf(
      channels_, [connection_handle, remote_cid](const L2capChannel& channel) {
        return channel.connection_handle() == connection_handle &&
               channel.remote_cid() == remote_cid;
      });
  return connection_it == channels_.end() ? nullptr : &(*connection_it);
}

void L2capChannelManager::Advance(
    IntrusiveForwardList<L2capChannel>::iterator& it) {
  if (++it == channels_.end()) {
    it = channels_.begin();
  }
}

void L2capChannelManager::RegisterStatusDelegate(
    L2capStatusDelegate& delegate) {
  status_tracker_.RegisterDelegate(delegate);
}

void L2capChannelManager::UnregisterStatusDelegate(
    L2capStatusDelegate& delegate) {
  status_tracker_.UnregisterDelegate(delegate);
}

void L2capChannelManager::HandleConnectionComplete(
    const L2capChannelConnectionInfo& info) {
  status_tracker_.HandleConnectionComplete(info);
}

void L2capChannelManager::HandleDisconnectionComplete(
    uint16_t connection_handle) {
  PW_LOG_INFO(
      "btproxy: L2capChannelManager::HandleDisconnectionComplete - "
      "connection_handle: %u",
      connection_handle);
  for (;;) {
    IntrusiveForwardList<L2capChannel>::iterator channel_it;
    {
      std::lock_guard lock(channels_mutex_);
      channel_it = containers::FindIf(
          channels_, [connection_handle](L2capChannel& channel) {
            return channel.connection_handle() == connection_handle &&
                   channel.state() == L2capChannel::State::kRunning;
          });
      if (channel_it == channels_.end()) {
        break;
      }

      // We do not need to worry about `channel_it` invalidating after unlocking
      // because an L2CAP_DISCONNECTION_RSP cannot be sent on this ACL
      // connection which has already been closed, so this channel will not be
      // closed elsewhere before we close it below.
    }

    channel_it->Close();
  }

  status_tracker_.HandleDisconnectionComplete(connection_handle);
}

void L2capChannelManager::HandleDisconnectionComplete(
    const L2capStatusTracker::DisconnectParams& params) {
  L2capChannel* channel =
      FindChannelByLocalCid(params.connection_handle, params.local_cid);
  if (channel) {
    channel->Close();
  }
  status_tracker_.HandleDisconnectionComplete(params);
}

}  // namespace pw::bluetooth::proxy
