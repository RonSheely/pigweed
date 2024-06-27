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

#include "pw_bluetooth_proxy/proxy_host.h"

#include <cstdint>
#include <numeric>

#include "pw_bluetooth/att.emb.h"
#include "pw_bluetooth/hci_commands.emb.h"
#include "pw_bluetooth/hci_common.emb.h"
#include "pw_bluetooth/hci_events.emb.h"
#include "pw_bluetooth/hci_h4.emb.h"
#include "pw_bluetooth_proxy/emboss_util.h"
#include "pw_bluetooth_proxy/h4_packet.h"
#include "pw_function/function.h"
#include "pw_unit_test/framework.h"  // IWYU pragma: keep

namespace pw::bluetooth::proxy {

namespace {

// ########## Util functions

// Populate passed H4 command buffer and return Emboss view on it.
template <typename EmbossT>
EmbossT CreateAndPopulateToControllerView(H4PacketWithH4& h4_packet,
                                          emboss::OpCode opcode) {
  std::iota(h4_packet.GetHciSpan().begin(), h4_packet.GetHciSpan().end(), 100);
  h4_packet.SetH4Type(emboss::H4PacketType::COMMAND);
  EmbossT view = MakeEmboss<EmbossT>(h4_packet.GetHciSpan());
  EXPECT_TRUE(view.IsComplete());
  view.header().opcode_enum().Write(opcode);
  return view;
}

// Return a populated H4 command buffer of a type that proxy host doesn't
// interact with.
void PopulateNoninteractingToControllerBuffer(H4PacketWithH4& h4_packet) {
  CreateAndPopulateToControllerView<emboss::InquiryCommandWriter>(
      h4_packet, emboss::OpCode::LINK_KEY_REQUEST_REPLY);
}

// Populate passed H4 event buffer and return Emboss view on it.
template <typename EmbossT>
EmbossT CreateAndPopulateToHostEventView(H4PacketWithHci& h4_packet,
                                         emboss::EventCode event_code) {
  std::iota(h4_packet.GetHciSpan().begin(), h4_packet.GetHciSpan().end(), 0x10);
  h4_packet.SetH4Type(emboss::H4PacketType::EVENT);
  EmbossT view = MakeEmboss<EmbossT>(h4_packet.GetHciSpan());
  view.header().event_code_enum().Write(event_code);
  view.status().Write(emboss::StatusCode::SUCCESS);
  EXPECT_TRUE(view.IsComplete());
  return view;
}

// Send an LE_Read_Buffer_Size (V2) CommandComplete event to `proxy` to request
// the reservation of a number of LE ACL send credits.
void SendReadBufferResponseFromController(ProxyHost& proxy,
                                          uint8_t num_credits_to_reserve) {
  std::array<
      uint8_t,
      emboss::LEReadBufferSizeV2CommandCompleteEventWriter::SizeInBytes()>
      hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::UNKNOWN, hci_arr};
  emboss::LEReadBufferSizeV2CommandCompleteEventWriter view =
      CreateAndPopulateToHostEventView<
          emboss::LEReadBufferSizeV2CommandCompleteEventWriter>(
          h4_packet, emboss::EventCode::COMMAND_COMPLETE);
  view.command_complete().command_opcode_enum().Write(
      emboss::OpCode::LE_READ_BUFFER_SIZE_V2);
  view.total_num_le_acl_data_packets().Write(num_credits_to_reserve);

  proxy.HandleH4HciFromController(std::move(h4_packet));
}

// Return a populated H4 event buffer of a type that proxy host doesn't interact
// with.

void CreateNonInteractingToHostBuffer(H4PacketWithHci& h4_packet) {
  CreateAndPopulateToHostEventView<emboss::InquiryCompleteEventWriter>(
      h4_packet, emboss::EventCode::INQUIRY_COMPLETE);
}

// ########## Examples

// Example for docs.rst.
TEST(Example, ExampleUsage) {
  // Populate H4 buffer to send towards controller.
  std::array<uint8_t, emboss::InquiryCommandView::SizeInBytes() + 1>
      h4_array_from_host;
  H4PacketWithH4 h4_packet_from_host{emboss::H4PacketType::UNKNOWN,
                                     h4_array_from_host};
  PopulateNoninteractingToControllerBuffer(h4_packet_from_host);

  // Populate H4 buffer to send towards host.
  std::array<uint8_t, emboss::InquiryCompleteEventView::SizeInBytes() + 1>
      hci_array_from_controller;
  H4PacketWithHci h4_packet_from_controller{emboss::H4PacketType::UNKNOWN,
                                            hci_array_from_controller};

  CreateNonInteractingToHostBuffer(h4_packet_from_controller);

  pw::Function<void(H4PacketWithHci && packet)> container_send_to_host_fn(
      []([[maybe_unused]] H4PacketWithHci&& packet) {});

  pw::Function<void(H4PacketWithH4 && packet)> container_send_to_controller_fn(
      ([]([[maybe_unused]] H4PacketWithH4&& packet) {}));

  // DOCSTAG: [pw_bluetooth_proxy-examples-basic]

#include "pw_bluetooth_proxy/proxy_host.h"

  // Container creates ProxyHost .
  ProxyHost proxy = ProxyHost(std::move(container_send_to_host_fn),
                              std::move(container_send_to_controller_fn),
                              2);

  // Container passes H4 packets from host through proxy. Proxy will in turn
  // call the container-provided `container_send_to_controller_fn` to pass them
  // on to the controller. Some packets may be modified, added, or removed.
  proxy.HandleH4HciFromHost(std::move(h4_packet_from_host));

  // Container passes H4 packets from controller through proxy. Proxy will in
  // turn call the container-provided `container_send_to_host_fn` to pass them
  // on to the controller. Some packets may be modified, added, or removed.
  proxy.HandleH4HciFromController(std::move(h4_packet_from_controller));

  // DOCSTAG: [pw_bluetooth_proxy-examples-basic]
}

// ########## PassthroughTest

// Verify buffer is properly passed (contents unaltered and zero-copy).
TEST(PassthroughTest, ToControllerPassesEqualBuffer) {
  std::array<uint8_t, emboss::InquiryCommandView::SizeInBytes() + 1> h4_arr;
  H4PacketWithH4 h4_packet{emboss::H4PacketType::UNKNOWN, h4_arr};
  PopulateNoninteractingToControllerBuffer(h4_packet);

  // Struct for capturing because `pw::Function` can't fit multiple captures.
  struct {
    // Use a copy for comparison to catch if proxy incorrectly changes the
    // passed buffer.
    std::array<uint8_t, emboss::InquiryCommandView::SizeInBytes() + 1> h4_arr;
    H4PacketWithH4* h4_packet;
    bool send_called;
  } send_capture = {h4_arr, &h4_packet, false};

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      [&send_capture](H4PacketWithH4&& packet) {
        send_capture.send_called = true;
        EXPECT_EQ(packet.GetH4Type(),
                  emboss::H4PacketType(send_capture.h4_arr[0]));
        EXPECT_TRUE(std::equal(send_capture.h4_packet->GetHciSpan().begin(),
                               send_capture.h4_packet->GetHciSpan().end(),
                               send_capture.h4_arr.begin() + 1,
                               send_capture.h4_arr.end()));
        // Verify no copy by verifying buffer is at the same memory location.
        EXPECT_EQ(packet.GetHciSpan().data(),
                  send_capture.h4_packet->GetHciSpan().data());
      });

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      []([[maybe_unused]] H4PacketWithHci&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromHost(std::move(h4_packet));

  // Verify to controller callback was called.
  EXPECT_EQ(send_capture.send_called, true);
}

// Verify buffer is properly passed (contents unaltered and zero-copy).
TEST(PassthroughTest, ToHostPassesEqualBuffer) {
  std::array<uint8_t, emboss::InquiryCompleteEventView::SizeInBytes()> hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::UNKNOWN, hci_arr};
  CreateNonInteractingToHostBuffer(h4_packet);

  // Struct for capturing because `pw::Function` can't fit multiple captures.
  struct {
    // Use a copy for comparison to catch if proxy incorrectly changes the
    // passed buffer.
    std::array<uint8_t, emboss::InquiryCompleteEventView::SizeInBytes()>
        hci_arr;
    H4PacketWithHci* h4_packet;
    bool send_called;
  } send_capture = {hci_arr, &h4_packet, false};

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_capture](H4PacketWithHci&& packet) {
        send_capture.send_called = true;
        EXPECT_EQ(packet.GetH4Type(), send_capture.h4_packet->GetH4Type());
        EXPECT_TRUE(std::equal(send_capture.h4_packet->GetHciSpan().begin(),
                               send_capture.h4_packet->GetHciSpan().end(),
                               send_capture.h4_packet->GetHciSpan().begin(),
                               send_capture.h4_packet->GetHciSpan().end()));
        // Verify no copy by verifying buffer is at the same memory location.
        EXPECT_EQ(packet.GetHciSpan().data(),
                  send_capture.h4_packet->GetHciSpan().data());
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  // Verify to controller callback was called.
  EXPECT_EQ(send_capture.send_called, true);
}

// Verify a command complete event (of a type that proxy doesn't act on) is
// properly passed (contents unaltered and zero-copy).
TEST(PassthroughTest, ToHostPassesEqualCommandComplete) {
  std::array<
      uint8_t,
      emboss::ReadLocalVersionInfoCommandCompleteEventWriter::SizeInBytes()>
      hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::UNKNOWN, hci_arr};
  emboss::ReadLocalVersionInfoCommandCompleteEventWriter view =
      CreateAndPopulateToHostEventView<
          emboss::ReadLocalVersionInfoCommandCompleteEventWriter>(
          h4_packet, emboss::EventCode::COMMAND_COMPLETE);
  view.command_complete().command_opcode_enum().Write(
      emboss::OpCode::READ_LOCAL_VERSION_INFO);

  // Struct for capturing because `pw::Function` can't fit multiple captures.
  struct {
    std::array<
        uint8_t,
        emboss::ReadLocalVersionInfoCommandCompleteEventWriter::SizeInBytes()>
        hci_arr;
    H4PacketWithHci* h4_packet;
    bool send_called;
  } send_capture = {hci_arr, &h4_packet, false};

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_capture](H4PacketWithHci&& packet) {
        send_capture.send_called = true;
        EXPECT_EQ(packet.GetH4Type(), send_capture.h4_packet->GetH4Type());
        EXPECT_TRUE(std::equal(send_capture.h4_packet->GetHciSpan().begin(),
                               send_capture.h4_packet->GetHciSpan().end(),
                               send_capture.h4_packet->GetHciSpan().begin(),
                               send_capture.h4_packet->GetHciSpan().end()));
        // Verify no copy by verifying buffer is at the same memory location.
        EXPECT_EQ(packet.GetHciSpan().data(),
                  send_capture.h4_packet->GetHciSpan().data());
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  // Verify to controller callback was called.
  EXPECT_EQ(send_capture.send_called, true);
}

// ########## BadPacketTest
// The proxy should not affect buffers it can't process (it should just pass
// them on).

TEST(BadPacketTest, BadH4TypeToControllerIsPassedOn) {
  std::array<uint8_t, emboss::InquiryCommandView::SizeInBytes() + 1> h4_arr;
  H4PacketWithH4 h4_packet{emboss::H4PacketType::UNKNOWN, h4_arr};
  PopulateNoninteractingToControllerBuffer(h4_packet);
  // Set back to an invalid type (after
  // PopulateNoninteractingToControllerBuffer).
  h4_packet.SetH4Type(emboss::H4PacketType::UNKNOWN);

  // Struct for capturing because `pw::Function` can't fit multiple captures.
  struct {
    // Use a copy for comparison to catch if proxy incorrectly changes the
    // passed buffer.
    std::array<uint8_t, emboss::InquiryCommandView::SizeInBytes() + 1> h4_arr;
    H4PacketWithH4* h4_packet;
    bool send_called;
  } send_capture = {h4_arr, &h4_packet, false};

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      [&send_capture](H4PacketWithH4&& packet) {
        send_capture.send_called = true;
        EXPECT_EQ(packet.GetH4Type(),
                  emboss::H4PacketType(send_capture.h4_arr[0]));
        EXPECT_TRUE(std::equal(send_capture.h4_packet->GetHciSpan().begin(),
                               send_capture.h4_packet->GetHciSpan().end(),
                               send_capture.h4_arr.begin() + 1,
                               send_capture.h4_arr.end()));
        // Verify no copy by verifying buffer is at the same memory location.
        EXPECT_EQ(packet.GetHciSpan().data(),
                  send_capture.h4_packet->GetHciSpan().data());
      });

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      []([[maybe_unused]] H4PacketWithHci&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromHost(std::move(h4_packet));

  // Verify to controller callback was called.
  EXPECT_EQ(send_capture.send_called, true);
}

TEST(PBadPacketTest, BadH4TypeToHostIsPassedOn) {
  std::array<uint8_t, emboss::InquiryCompleteEventView::SizeInBytes()> hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::UNKNOWN, hci_arr};
  CreateNonInteractingToHostBuffer(h4_packet);

  // Set back to an invalid type.
  h4_packet.SetH4Type(emboss::H4PacketType::UNKNOWN);

  // Struct for capturing because `pw::Function` can't fit multiple captures.
  struct {
    // Use a copy for comparison to catch if proxy incorrectly changes the
    // passed buffer.
    std::array<uint8_t, emboss::InquiryCompleteEventView::SizeInBytes()>
        hci_arr;
    H4PacketWithHci* h4_packet;
    bool send_called;
  } send_capture = {hci_arr, &h4_packet, false};

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_capture](H4PacketWithHci&& packet) {
        send_capture.send_called = true;
        EXPECT_EQ(packet.GetH4Type(), emboss::H4PacketType::UNKNOWN);
        EXPECT_TRUE(std::equal(send_capture.h4_packet->GetHciSpan().begin(),
                               send_capture.h4_packet->GetHciSpan().end(),
                               send_capture.h4_packet->GetHciSpan().begin(),
                               send_capture.h4_packet->GetHciSpan().end()));
        // Verify no copy by verifying buffer is at the same memory location.
        EXPECT_EQ(packet.GetHciSpan().data(),
                  send_capture.h4_packet->GetHciSpan().data());
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  // Verify to controller callback was called.
  EXPECT_EQ(send_capture.send_called, true);
}

TEST(BadPacketTest, EmptyBufferToControllerIsPassedOn) {
  std::array<uint8_t, 0> h4_arr;
  H4PacketWithH4 h4_packet{emboss::H4PacketType::COMMAND, h4_arr};
  // H4PacketWithH4 use the underlying h4 buffer to store type. Since its length
  // is zero, it can't store it and will always return UNKNOWN.
  EXPECT_EQ(h4_packet.GetH4Type(), emboss::H4PacketType::UNKNOWN);

  bool send_called = false;
  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      [&send_called](H4PacketWithH4&& packet) {
        send_called = true;
        EXPECT_EQ(packet.GetH4Type(), emboss::H4PacketType::UNKNOWN);
        EXPECT_TRUE(packet.GetHciSpan().empty());
      });

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      []([[maybe_unused]] H4PacketWithHci&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromHost(std::move(h4_packet));

  // Verify callback was called.
  EXPECT_EQ(send_called, true);
}

TEST(BadPacketTest, EmptyBufferToHostIsPassedOn) {
  std::array<uint8_t, 0> hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::EVENT, hci_arr};

  bool send_called = false;
  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_called](H4PacketWithHci&& packet) {
        send_called = true;
        EXPECT_EQ(packet.GetH4Type(), emboss::H4PacketType::EVENT);
        EXPECT_TRUE(packet.GetHciSpan().empty());
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  // Verify callback was called.
  EXPECT_EQ(send_called, true);
}

TEST(BadPacketTest, TooShortEventToHostIsPassOn) {
  std::array<uint8_t, emboss::InquiryCompleteEventView::SizeInBytes()>
      valid_hci_arr;
  H4PacketWithHci valid_packet{emboss::H4PacketType::UNKNOWN, valid_hci_arr};
  CreateNonInteractingToHostBuffer(valid_packet);

  // Create packet for sending whose span size is one less than a valid command
  // complete event.
  H4PacketWithHci h4_packet{valid_packet.GetH4Type(),
                            valid_packet.GetHciSpan().subspan(
                                0, emboss::EventHeaderView::SizeInBytes() - 1)};

  // Struct for capturing because `pw::Function` can't fit multiple captures.
  struct {
    std::array<uint8_t, emboss::EventHeaderView::SizeInBytes() - 1> hci_arr;
    bool send_called;
  } send_capture;
  // Copy valid event into a short_array whose size is one less than a valid
  // EventHeader.
  std::copy(h4_packet.GetHciSpan().begin(),
            h4_packet.GetHciSpan().end(),
            send_capture.hci_arr.begin());
  send_capture.send_called = false;

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_capture](H4PacketWithHci&& packet) {
        send_capture.send_called = true;
        EXPECT_TRUE(std::equal(packet.GetHciSpan().begin(),
                               packet.GetHciSpan().end(),
                               send_capture.hci_arr.begin(),
                               send_capture.hci_arr.end()));
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  // Verify callback was called.
  EXPECT_EQ(send_capture.send_called, true);
}

TEST(BadPacketTest, TooShortCommandCompleteEventToHost) {
  std::array<
      uint8_t,
      emboss::ReadLocalVersionInfoCommandCompleteEventWriter::SizeInBytes()>
      valid_hci_arr;
  H4PacketWithHci valid_packet{emboss::H4PacketType::UNKNOWN, valid_hci_arr};
  emboss::ReadLocalVersionInfoCommandCompleteEventWriter view =
      CreateAndPopulateToHostEventView<
          emboss::ReadLocalVersionInfoCommandCompleteEventWriter>(
          valid_packet, emboss::EventCode::COMMAND_COMPLETE);
  view.command_complete().command_opcode_enum().Write(
      emboss::OpCode::READ_LOCAL_VERSION_INFO);

  // Create packet for sending whose span size is one less than a valid command
  // complete event.
  H4PacketWithHci h4_packet{
      valid_packet.GetH4Type(),
      valid_packet.GetHciSpan().subspan(
          0,
          emboss::ReadLocalVersionInfoCommandCompleteEventWriter::
                  SizeInBytes() -
              1)};

  // Struct for capturing because `pw::Function` capture can't fit multiple
  // fields .
  struct {
    std::array<
        uint8_t,
        emboss::ReadLocalVersionInfoCommandCompleteEventWriter::SizeInBytes() -
            1>
        hci_arr;
    bool send_called;
  } send_capture;
  std::copy(h4_packet.GetHciSpan().begin(),
            h4_packet.GetHciSpan().end(),
            send_capture.hci_arr.begin());
  send_capture.send_called = false;

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_capture](H4PacketWithHci&& packet) {
        send_capture.send_called = true;
        EXPECT_TRUE(std::equal(packet.GetHciSpan().begin(),
                               packet.GetHciSpan().end(),
                               send_capture.hci_arr.begin(),
                               send_capture.hci_arr.end()));
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  // Verify callback was called.
  EXPECT_EQ(send_capture.send_called, true);
}

// ########## ReserveLeAclCredits Tests

// Proxy Host should reserve requested ACL LE credits from controller's ACL LE
// credits when using LEReadBufferSizeV1 command.
TEST(ReserveLeAclCredits, ProxyCreditsReserveCreditsWithLEReadBufferSizeV1) {
  std::array<
      uint8_t,
      emboss::LEReadBufferSizeV1CommandCompleteEventWriter::SizeInBytes()>
      hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::UNKNOWN, hci_arr};
  emboss::LEReadBufferSizeV1CommandCompleteEventWriter view =
      CreateAndPopulateToHostEventView<
          emboss::LEReadBufferSizeV1CommandCompleteEventWriter>(
          h4_packet, emboss::EventCode::COMMAND_COMPLETE);
  view.command_complete().command_opcode_enum().Write(
      emboss::OpCode::LE_READ_BUFFER_SIZE_V1);
  view.total_num_le_acl_data_packets().Write(10);

  bool send_called = false;
  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_called](H4PacketWithHci&& h4_packet) {
        send_called = true;
        emboss::LEReadBufferSizeV1CommandCompleteEventWriter view =
            MakeEmboss<emboss::LEReadBufferSizeV1CommandCompleteEventWriter>(
                h4_packet.GetHciSpan());
        // Should reserve 2 credits from original total of 10 (so 8 left for
        // host).
        EXPECT_EQ(view.total_num_le_acl_data_packets().Read(), 8);
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 2);

  EXPECT_TRUE(proxy.HasSendAclCapability());

  // Verify to controller callback was called.
  EXPECT_EQ(send_called, true);
}

// Proxy Host should reserve requested ACL LE credits from controller's ACL LE
// credits when using LEReadBufferSizeV2 command.
TEST(ReserveLeAclCredits, ProxyCreditsReserveCreditsWithLEReadBufferSizeV2) {
  std::array<
      uint8_t,
      emboss::LEReadBufferSizeV2CommandCompleteEventWriter::SizeInBytes()>
      hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::UNKNOWN, hci_arr};
  emboss::LEReadBufferSizeV2CommandCompleteEventWriter view =
      CreateAndPopulateToHostEventView<
          emboss::LEReadBufferSizeV2CommandCompleteEventWriter>(
          h4_packet, emboss::EventCode::COMMAND_COMPLETE);
  view.command_complete().command_opcode_enum().Write(
      emboss::OpCode::LE_READ_BUFFER_SIZE_V2);
  view.total_num_le_acl_data_packets().Write(10);

  bool send_called = false;
  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_called](H4PacketWithHci&& h4_packet) {
        send_called = true;
        emboss::LEReadBufferSizeV2CommandCompleteEventWriter view =
            MakeEmboss<emboss::LEReadBufferSizeV2CommandCompleteEventWriter>(
                h4_packet.GetHciSpan());
        // Should reserve 2 credits from original total of 10 (so 8 left for
        // host).
        EXPECT_EQ(view.total_num_le_acl_data_packets().Read(), 8);
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 2);

  EXPECT_TRUE(proxy.HasSendAclCapability());

  // Verify to controller callback was called.
  EXPECT_EQ(send_called, true);
}

// If controller provides less than wanted credits, we should reserve that
// smaller amount.
TEST(ReserveLeAclCredits, ProxyCreditsCappedByControllerCredits) {
  std::array<
      uint8_t,
      emboss::LEReadBufferSizeV1CommandCompleteEventWriter::SizeInBytes()>
      hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::UNKNOWN, hci_arr};
  emboss::LEReadBufferSizeV1CommandCompleteEventWriter view =
      CreateAndPopulateToHostEventView<
          emboss::LEReadBufferSizeV1CommandCompleteEventWriter>(
          h4_packet, emboss::EventCode::COMMAND_COMPLETE);
  view.command_complete().command_opcode_enum().Write(
      emboss::OpCode::LE_READ_BUFFER_SIZE_V1);
  view.total_num_le_acl_data_packets().Write(5);

  bool send_called = false;
  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_called](H4PacketWithHci&& h4_packet) {
        send_called = true;
        // We want 7, but can reserve only 5 from original 5 (so 0 left for
        // host).
        emboss::LEReadBufferSizeV1CommandCompleteEventWriter view =
            MakeEmboss<emboss::LEReadBufferSizeV1CommandCompleteEventWriter>(
                h4_packet.GetHciSpan());
        EXPECT_EQ(view.total_num_le_acl_data_packets().Read(), 0);
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 7);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 5);

  // Verify to controller callback was called.
  EXPECT_EQ(send_called, true);
}

// Proxy Host can reserve zero credits from controller's ACL LE credits.
TEST(ReserveLeAclCredits, ProxyCreditsReserveZeroCredits) {
  std::array<
      uint8_t,
      emboss::LEReadBufferSizeV1CommandCompleteEventWriter::SizeInBytes()>
      hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::UNKNOWN, hci_arr};
  emboss::LEReadBufferSizeV1CommandCompleteEventWriter view =
      CreateAndPopulateToHostEventView<
          emboss::LEReadBufferSizeV1CommandCompleteEventWriter>(
          h4_packet, emboss::EventCode::COMMAND_COMPLETE);
  view.command_complete().command_opcode_enum().Write(
      emboss::OpCode::LE_READ_BUFFER_SIZE_V1);
  view.total_num_le_acl_data_packets().Write(10);

  bool send_called = false;
  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_called](H4PacketWithHci&& h4_packet) {
        send_called = true;
        emboss::LEReadBufferSizeV1CommandCompleteEventWriter view =
            MakeEmboss<emboss::LEReadBufferSizeV1CommandCompleteEventWriter>(
                h4_packet.GetHciSpan());
        // Should reserve 0 credits from original total of 10 (so 10 left for
        // host).
        EXPECT_EQ(view.total_num_le_acl_data_packets().Read(), 10);
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 0);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 0);

  EXPECT_FALSE(proxy.HasSendAclCapability());

  // Verify to controller callback was called.
  EXPECT_EQ(send_called, true);
}

// If controller has no credits, proxy should reserve none.
TEST(ReserveLeAclPackets, ProxyCreditsZeroWhenHostCreditsZero) {
  std::array<
      uint8_t,
      emboss::LEReadBufferSizeV1CommandCompleteEventWriter::SizeInBytes()>
      hci_arr;
  H4PacketWithHci h4_packet{emboss::H4PacketType::UNKNOWN, hci_arr};
  emboss::LEReadBufferSizeV1CommandCompleteEventWriter view =
      CreateAndPopulateToHostEventView<
          emboss::LEReadBufferSizeV1CommandCompleteEventWriter>(
          h4_packet, emboss::EventCode::COMMAND_COMPLETE);
  view.command_complete().command_opcode_enum().Write(
      emboss::OpCode::LE_READ_BUFFER_SIZE_V1);
  view.total_num_le_acl_data_packets().Write(0);

  bool send_called = false;
  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&send_called](H4PacketWithHci&& h4_packet) {
        send_called = true;
        emboss::LEReadBufferSizeV1CommandCompleteEventWriter view =
            MakeEmboss<emboss::LEReadBufferSizeV1CommandCompleteEventWriter>(
                h4_packet.GetHciSpan());
        // Should reserve 0 credit from original total of 0 (so 0 left for
        // host).
        EXPECT_EQ(view.total_num_le_acl_data_packets().Read(), 0);
      });

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  proxy.HandleH4HciFromController(std::move(h4_packet));

  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 0);

  EXPECT_TRUE(proxy.HasSendAclCapability());

  // Verify to controller callback was called.
  EXPECT_EQ(send_called, true);
}

TEST(ReserveLeAclPackets, ProxyCreditsZeroWhenNotInitialized) {
  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      []([[maybe_unused]] H4PacketWithHci&& packet) {});

  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);

  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 0);

  EXPECT_TRUE(proxy.HasSendAclCapability());
}

// ########## GattNotifyTest

TEST(GattNotifyTest, SendGattNotify1ByteAttribute) {
  struct {
    int sends_called = 0;
    // First four bits 0x0 encode PB & BC flags
    uint16_t handle = 0x0ACB;
    // Length of L2CAP PDU
    uint16_t data_total_length = 0x0008;
    // Length of ATT PDU
    uint16_t pdu_length = 0x0004;
    // Attribute protocol channel ID (0x0004)
    uint16_t channel_id = 0x0004;
    // ATT_HANDLE_VALUE_NTF opcode 0x1B
    uint8_t attribute_opcode = 0x1B;
    uint16_t attribute_handle = 0x4321;
    std::array<uint8_t, 1> attribute_value = {0xFA};

    // Built from the preceding values in little endian order.
    std::array<uint8_t, 12> expected_gatt_notify_packet = {
        0xCB, 0x0A, 0x08, 0x00, 0x04, 0x00, 0x04, 0x00, 0x1B, 0x21, 0x43, 0xFA};
  } capture;

  pw::Function<void(H4PacketWithHci && packet)>&& send_to_host_fn(
      []([[maybe_unused]] H4PacketWithHci&& packet) {});

  pw::Function<void(H4PacketWithH4 && packet)>&& send_to_controller_fn(
      [&capture](H4PacketWithH4&& packet) {
        ++capture.sends_called;
        EXPECT_EQ(packet.GetH4Type(), emboss::H4PacketType::ACL_DATA);
        EXPECT_EQ(packet.GetHciSpan().size(),
                  capture.expected_gatt_notify_packet.size());
        EXPECT_TRUE(std::equal(packet.GetHciSpan().begin(),
                               packet.GetHciSpan().end(),
                               capture.expected_gatt_notify_packet.begin(),
                               capture.expected_gatt_notify_packet.end()));
        auto gatt_notify = emboss::AttNotifyOverAclView(
            capture.attribute_value.size(),
            packet.GetHciSpan().data(),
            capture.expected_gatt_notify_packet.size());
        EXPECT_EQ(gatt_notify.acl_header().handle().Read(), capture.handle);
        EXPECT_EQ(gatt_notify.acl_header().packet_boundary_flag().Read(),
                  emboss::AclDataPacketBoundaryFlag::FIRST_NON_FLUSHABLE);
        EXPECT_EQ(gatt_notify.acl_header().broadcast_flag().Read(),
                  emboss::AclDataPacketBroadcastFlag::POINT_TO_POINT);
        EXPECT_EQ(gatt_notify.acl_header().data_total_length().Read(),
                  capture.data_total_length);
        EXPECT_EQ(gatt_notify.l2cap_header().pdu_length().Read(),
                  capture.pdu_length);
        EXPECT_EQ(gatt_notify.l2cap_header().channel_id().Read(),
                  capture.channel_id);
        EXPECT_EQ(gatt_notify.att_handle_value_ntf().attribute_opcode().Read(),
                  static_cast<emboss::AttOpcode>(capture.attribute_opcode));
        EXPECT_EQ(gatt_notify.att_handle_value_ntf().attribute_handle().Read(),
                  capture.attribute_handle);
        EXPECT_EQ(
            gatt_notify.att_handle_value_ntf().attribute_value()[0].Read(),
            capture.attribute_value[0]);
      });

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 1);
  // Allow proxy to reserve 1 credit.
  SendReadBufferResponseFromController(proxy, 1);

  EXPECT_TRUE(proxy
                  .sendGattNotify(capture.handle,
                                  capture.attribute_handle,
                                  pw::span(capture.attribute_value))
                  .ok());
  EXPECT_EQ(capture.sends_called, 1);
}

TEST(GattNotifyTest, SendGattNotify2ByteAttribute) {
  struct {
    int sends_called = 0;
    // Max connection_handle value; first four bits 0x0 encode PB & BC flags
    uint16_t handle = 0x0EFF;
    // Length of L2CAP PDU
    uint16_t data_total_length = 0x0009;
    // Length of ATT PDU
    uint16_t pdu_length = 0x0005;
    // Attribute protocol channel ID (0x0004)
    uint16_t channel_id = 0x0004;
    // ATT_HANDLE_VALUE_NTF opcode 0x1B
    uint8_t attribute_opcode = 0x1B;
    uint16_t attribute_handle = 0x1234;
    std::array<uint8_t, 2> attribute_value = {0xAB, 0xCD};

    // Built from the preceding values in little endian order.
    std::array<uint8_t, 13> expected_gatt_notify_packet = {0xFF,
                                                           0x0E,
                                                           0x09,
                                                           0x00,
                                                           0x05,
                                                           0x00,
                                                           0x04,
                                                           0x00,
                                                           0x1B,
                                                           0x34,
                                                           0x12,
                                                           0xAB,
                                                           0XCD};
  } capture;

  pw::Function<void(H4PacketWithHci && packet)>&& send_to_host_fn(
      []([[maybe_unused]] H4PacketWithHci&& packet) {});

  pw::Function<void(H4PacketWithH4 && packet)>&& send_to_controller_fn(
      [&capture](H4PacketWithH4&& packet) {
        ++capture.sends_called;
        EXPECT_EQ(packet.GetH4Type(), emboss::H4PacketType::ACL_DATA);
        EXPECT_EQ(packet.GetHciSpan().size(),
                  capture.expected_gatt_notify_packet.size());
        EXPECT_TRUE(std::equal(packet.GetHciSpan().begin(),
                               packet.GetHciSpan().end(),
                               capture.expected_gatt_notify_packet.begin(),
                               capture.expected_gatt_notify_packet.end()));
        auto gatt_notify = emboss::AttNotifyOverAclView(
            capture.attribute_value.size(),
            packet.GetHciSpan().data(),
            capture.expected_gatt_notify_packet.size());
        EXPECT_EQ(gatt_notify.acl_header().handle().Read(), capture.handle);
        EXPECT_EQ(gatt_notify.acl_header().packet_boundary_flag().Read(),
                  emboss::AclDataPacketBoundaryFlag::FIRST_NON_FLUSHABLE);
        EXPECT_EQ(gatt_notify.acl_header().broadcast_flag().Read(),
                  emboss::AclDataPacketBroadcastFlag::POINT_TO_POINT);
        EXPECT_EQ(gatt_notify.acl_header().data_total_length().Read(),
                  capture.data_total_length);
        EXPECT_EQ(gatt_notify.l2cap_header().pdu_length().Read(),
                  capture.pdu_length);
        EXPECT_EQ(gatt_notify.l2cap_header().channel_id().Read(),
                  capture.channel_id);
        EXPECT_EQ(gatt_notify.att_handle_value_ntf().attribute_opcode().Read(),
                  static_cast<emboss::AttOpcode>(capture.attribute_opcode));
        EXPECT_EQ(gatt_notify.att_handle_value_ntf().attribute_handle().Read(),
                  capture.attribute_handle);
        EXPECT_EQ(
            gatt_notify.att_handle_value_ntf().attribute_value()[0].Read(),
            capture.attribute_value[0]);
        EXPECT_EQ(
            gatt_notify.att_handle_value_ntf().attribute_value()[1].Read(),
            capture.attribute_value[1]);
      });

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 1);
  // Allow proxy to reserve 1 credit.
  SendReadBufferResponseFromController(proxy, 1);

  EXPECT_TRUE(proxy
                  .sendGattNotify(capture.handle,
                                  capture.attribute_handle,
                                  pw::span(capture.attribute_value))
                  .ok());
  EXPECT_EQ(capture.sends_called, 1);
}

TEST(GattNotifyTest, SendGattNotifyUnavailableWhenPending) {
  struct {
    int sends_called = 0;
    H4PacketWithH4 released_packet{{}};
  } capture;

  pw::Function<void(H4PacketWithHci && packet)>&& send_to_host_fn(
      []([[maybe_unused]] H4PacketWithHci&& packet) {});
  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      [&capture](H4PacketWithH4&& packet) {
        ++capture.sends_called;
        capture.released_packet = std::move(packet);
      });

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);
  // Allow proxy to reserve 2 credit.
  SendReadBufferResponseFromController(proxy, 2);

  std::array<uint8_t, 2> attribute_value = {0xAB, 0xCD};
  EXPECT_TRUE(proxy.sendGattNotify(123, 345, pw::span(attribute_value)).ok());
  // Only one send is allowed at a time, so PW_STATUS_UNAVAILABLE will be
  // returned until the pending packet is destructed.
  EXPECT_EQ(proxy.sendGattNotify(123, 345, pw::span(attribute_value)),
            PW_STATUS_UNAVAILABLE);
  capture.released_packet.~H4PacketWithH4();
  EXPECT_TRUE(proxy.sendGattNotify(123, 345, pw::span(attribute_value)).ok());
  EXPECT_EQ(proxy.sendGattNotify(123, 345, pw::span(attribute_value)),
            PW_STATUS_UNAVAILABLE);
  EXPECT_EQ(capture.sends_called, 2);
}

TEST(GattNotifyTest, SendGattNotifyReturnsErrorForInvalidArgs) {
  pw::Function<void(H4PacketWithHci && packet)>&& send_to_host_fn(
      []([[maybe_unused]] H4PacketWithHci&& packet) {});
  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4 packet) { FAIL(); });

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 0);

  std::array<uint8_t, 2> attribute_value = {0xAB, 0xCD};
  // connection_handle too large
  EXPECT_EQ(proxy.sendGattNotify(0x0FFF, 345, pw::span(attribute_value)),
            PW_STATUS_INVALID_ARGUMENT);
  // attribute_handle is 0
  EXPECT_EQ(proxy.sendGattNotify(123, 0, pw::span(attribute_value)),
            PW_STATUS_INVALID_ARGUMENT);
  // attribute_value too large
  std::array<uint8_t, 3> attribute_value_too_large = {0xAB, 0xCD, 0xEF};
  EXPECT_EQ(proxy.sendGattNotify(123, 345, pw::span(attribute_value_too_large)),
            PW_STATUS_INVALID_ARGUMENT);
}

// ########## NumberOfCompletedPacketsTest

TEST(NumberOfCompletedPacketsTest, TwoOfThreeSentPacketsComplete) {
  constexpr size_t kNumConnections = 3;
  struct {
    int sends_called = 0;
    std::array<uint16_t, kNumConnections> connection_handles = {
        0x123, 0x456, 0x789};
  } capture;

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&capture](H4PacketWithHci&& packet) {
        auto event_header =
            MakeEmboss<emboss::EventHeaderView>(packet.GetHciSpan().subspan(
                0, emboss::EventHeader::IntrinsicSizeInBytes()));
        if (event_header.event_code_enum().Read() !=
            emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS) {
          return;
        }
        ++capture.sends_called;

        auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventView>(
            packet.GetHciSpan());
        EXPECT_EQ(packet.GetHciSpan().size(), 15ul);
        EXPECT_EQ(view.num_handles().Read(), capture.connection_handles.size());
        EXPECT_EQ(view.header().event_code_enum().Read(),
                  emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);

        // Proxy should have reclaimed 1 credit from Connection 0 (leaving 0
        // credits in packet), no credits from Connection 1 (meaning 0 will be
        // unchanged), and 1 credit from Connection 2 (leaving 0).
        EXPECT_EQ(view.nocp_data()[0].connection_handle().Read(),
                  capture.connection_handles[0]);
        EXPECT_EQ(view.nocp_data()[0].num_completed_packets().Read(), 0);

        EXPECT_EQ(view.nocp_data()[1].connection_handle().Read(),
                  capture.connection_handles[1]);
        EXPECT_EQ(view.nocp_data()[1].num_completed_packets().Read(), 0);

        EXPECT_EQ(view.nocp_data()[2].connection_handle().Read(),
                  capture.connection_handles[2]);
        EXPECT_EQ(view.nocp_data()[2].num_completed_packets().Read(), 0);
      });
  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(std::move(send_to_host_fn),
                              std::move(send_to_controller_fn),
                              kNumConnections);
  SendReadBufferResponseFromController(proxy, kNumConnections);

  std::array<uint8_t, 1> attribute_value = {0};

  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 3);

  // Send packet; num free packets should decrement.
  EXPECT_TRUE(proxy
                  .sendGattNotify(capture.connection_handles[0],
                                  1,
                                  pw::span(attribute_value))
                  .ok());
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 2);

  // Send packet over Connection 1, which will not have a packet completed in
  // the Number_of_Completed_Packets event.
  EXPECT_TRUE(proxy
                  .sendGattNotify(capture.connection_handles[1],
                                  1,
                                  pw::span(attribute_value))
                  .ok());
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 1);

  // Send third packet; num free packets should decrement again.
  EXPECT_TRUE(proxy
                  .sendGattNotify(capture.connection_handles[2],
                                  1,
                                  pw::span(attribute_value))
                  .ok());
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 0);

  // At this point, proxy has used all 3 credits, 1 on each Connection, so send
  // should fail.
  EXPECT_EQ(proxy.sendGattNotify(
                capture.connection_handles[0], 1, pw::span(attribute_value)),
            PW_STATUS_UNAVAILABLE);

  std::array<
      uint8_t,
      emboss::NumberOfCompletedPacketsEvent::MinSizeInBytes() +
          kNumConnections *
              emboss::NumberOfCompletedPacketsEventData::IntrinsicSizeInBytes()>
      hci_arr;
  H4PacketWithHci nocp_packet{emboss::H4PacketType::EVENT, hci_arr};
  auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventWriter>(
      nocp_packet.GetHciSpan());
  view.header().event_code_enum().Write(
      emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);
  view.num_handles().Write(kNumConnections);

  // Number_of_Completed_Packets event that reports 1 packet on Connections 0 &
  // 1, and no packets on Connection 2.
  view.nocp_data()[0].connection_handle().Write(capture.connection_handles[0]);
  view.nocp_data()[0].num_completed_packets().Write(1);
  view.nocp_data()[1].connection_handle().Write(capture.connection_handles[1]);
  view.nocp_data()[1].num_completed_packets().Write(0);
  view.nocp_data()[2].connection_handle().Write(capture.connection_handles[2]);
  view.nocp_data()[2].num_completed_packets().Write(1);

  // Checks in send_to_host_fn will ensure we have reclaimed 2 of 3 credits.
  proxy.HandleH4HciFromController(std::move(nocp_packet));
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 2);
  EXPECT_EQ(capture.sends_called, 1);
}

TEST(NumberOfCompletedPacketsTest, ManyMorePacketsCompletedThanPacketsPending) {
  constexpr size_t kNumConnections = 2;
  struct {
    int sends_called = 0;
    std::array<uint16_t, kNumConnections> connection_handles = {0x123, 0x456};
  } capture;

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&capture](H4PacketWithHci&& packet) {
        auto event_header =
            MakeEmboss<emboss::EventHeaderView>(packet.GetHciSpan().subspan(
                0, emboss::EventHeader::IntrinsicSizeInBytes()));
        if (event_header.event_code_enum().Read() !=
            emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS) {
          return;
        }
        ++capture.sends_called;

        auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventView>(
            packet.GetHciSpan());
        EXPECT_EQ(packet.GetHciSpan().size(), 11ul);
        EXPECT_EQ(view.num_handles().Read(), capture.connection_handles.size());
        EXPECT_EQ(view.header().event_code_enum().Read(),
                  emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);

        // Proxy should have reclaimed 1 credit from Connection 0 (leaving
        // 9 credits in packet) and 1 credit from Connection 2 (leaving 14).
        EXPECT_EQ(view.nocp_data()[0].connection_handle().Read(),
                  capture.connection_handles[0]);
        EXPECT_EQ(view.nocp_data()[0].num_completed_packets().Read(), 9);

        EXPECT_EQ(view.nocp_data()[1].connection_handle().Read(),
                  capture.connection_handles[1]);
        EXPECT_EQ(view.nocp_data()[1].num_completed_packets().Read(), 14);
      });
  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 2);
  SendReadBufferResponseFromController(proxy, 2);

  std::array<uint8_t, 1> attribute_value = {0};

  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 2);

  // Send packet over Connection 0; num free packets should decrement.
  EXPECT_TRUE(proxy
                  .sendGattNotify(capture.connection_handles[0],
                                  1,
                                  pw::span(attribute_value))
                  .ok());
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 1);

  // Send packet over Connection 1; num free packets should decrement again.
  EXPECT_TRUE(proxy
                  .sendGattNotify(capture.connection_handles[1],
                                  1,
                                  pw::span(attribute_value))
                  .ok());
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 0);

  // At this point proxy, has used both credits, so send should fail.
  EXPECT_EQ(proxy.sendGattNotify(
                capture.connection_handles[1], 1, pw::span(attribute_value)),
            PW_STATUS_UNAVAILABLE);

  std::array<
      uint8_t,
      emboss::NumberOfCompletedPacketsEvent::MinSizeInBytes() +
          kNumConnections *
              emboss::NumberOfCompletedPacketsEventData::IntrinsicSizeInBytes()>
      hci_arr;
  H4PacketWithHci nocp_event{emboss::H4PacketType::EVENT, hci_arr};
  auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventWriter>(
      nocp_event.GetHciSpan());
  view.header().event_code_enum().Write(
      emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);
  view.num_handles().Write(kNumConnections);

  // Number_of_Completed_Packets event that reports 10 packets on Connection 0
  // and 15 packets on Connection 1.
  for (size_t i = 0; i < kNumConnections; ++i) {
    view.nocp_data()[i].connection_handle().Write(
        capture.connection_handles[i]);
    view.nocp_data()[i].num_completed_packets().Write(10 + 5 * i);
  }

  // Checks in send_to_host_fn will ensure we have reclaimed exactly 2 credits,
  // 1 from each Connection.
  proxy.HandleH4HciFromController(std::move(nocp_event));
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 2);
  EXPECT_EQ(capture.sends_called, 1);
}

TEST(NumberOfCompletedPacketsTest, ProxyReclaimsOnlyItsUsedCredits) {
  constexpr size_t kNumConnections = 2;
  struct {
    int sends_called = 0;
    std::array<uint16_t, kNumConnections> connection_handles = {0x123, 0x456};
  } capture;

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&capture](H4PacketWithHci&& packet) {
        auto event_header =
            MakeEmboss<emboss::EventHeaderView>(packet.GetHciSpan().subspan(
                0, emboss::EventHeader::IntrinsicSizeInBytes()));
        if (event_header.event_code_enum().Read() !=
            emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS) {
          return;
        }
        ++capture.sends_called;

        auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventView>(
            packet.GetHciSpan());
        EXPECT_EQ(packet.GetHciSpan().size(), 11ul);
        EXPECT_EQ(view.num_handles().Read(), 2);
        EXPECT_EQ(view.header().event_code_enum().Read(),
                  emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);

        // Proxy has 4 credits it wants to reclaim, but it should have only
        // reclaimed the 2 credits it used on Connection 0.
        EXPECT_EQ(view.nocp_data()[0].connection_handle().Read(),
                  capture.connection_handles[0]);
        EXPECT_EQ(view.nocp_data()[0].num_completed_packets().Read(), 8);
        EXPECT_EQ(view.nocp_data()[1].connection_handle().Read(),
                  capture.connection_handles[1]);
        EXPECT_EQ(view.nocp_data()[1].num_completed_packets().Read(), 15);
      });
  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 4);
  SendReadBufferResponseFromController(proxy, 4);

  std::array<uint8_t, 1> attribute_value = {0};

  // Use 2 credits on Connection 0 and 2 credits on random connections that will
  // not be included in the NOCP event.
  EXPECT_TRUE(proxy
                  .sendGattNotify(capture.connection_handles[0],
                                  1,
                                  pw::span(attribute_value))
                  .ok());
  EXPECT_TRUE(proxy
                  .sendGattNotify(capture.connection_handles[0],
                                  1,
                                  pw::span(attribute_value))
                  .ok());
  EXPECT_TRUE(proxy.sendGattNotify(0xABC, 1, pw::span(attribute_value)).ok());
  EXPECT_TRUE(proxy.sendGattNotify(0xBCD, 1, pw::span(attribute_value)).ok());
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 0);

  std::array<
      uint8_t,
      emboss::NumberOfCompletedPacketsEvent::MinSizeInBytes() +
          kNumConnections *
              emboss::NumberOfCompletedPacketsEventData::IntrinsicSizeInBytes()>
      hci_arr;
  H4PacketWithHci nocp_event{emboss::H4PacketType::EVENT, hci_arr};
  auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventWriter>(
      nocp_event.GetHciSpan());
  view.header().event_code_enum().Write(
      emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);
  view.num_handles().Write(kNumConnections);

  // Number_of_Completed_Packets event that reports 10 packets on Connection 0
  // and 15 packets on Connection 1.
  for (size_t i = 0; i < kNumConnections; ++i) {
    view.nocp_data()[i].connection_handle().Write(
        capture.connection_handles[i]);
    view.nocp_data()[i].num_completed_packets().Write(10 + 5 * i);
  }

  // Checks in send_to_host_fn will ensure we have reclaimed only 2 credits.
  proxy.HandleH4HciFromController(std::move(nocp_event));
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 2);
  EXPECT_EQ(capture.sends_called, 1);
}

TEST(NumberOfCompletedPacketsTest, EventUnmodifiedIfNoCreditsInUse) {
  constexpr size_t kNumConnections = 2;
  struct {
    int sends_called = 0;
    std::array<uint16_t, kNumConnections> connection_handles = {0x123, 0x456};
  } capture;

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&capture](H4PacketWithHci&& packet) {
        auto event_header =
            MakeEmboss<emboss::EventHeaderView>(packet.GetHciSpan().subspan(
                0, emboss::EventHeader::IntrinsicSizeInBytes()));
        if (event_header.event_code_enum().Read() !=
            emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS) {
          return;
        }
        ++capture.sends_called;

        auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventView>(
            packet.GetHciSpan());
        EXPECT_EQ(packet.GetHciSpan().size(), 11ul);
        EXPECT_EQ(view.num_handles().Read(), 2);
        EXPECT_EQ(view.header().event_code_enum().Read(),
                  emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);

        // Event should be unmodified.
        EXPECT_EQ(view.nocp_data()[0].connection_handle().Read(),
                  capture.connection_handles[0]);
        EXPECT_EQ(view.nocp_data()[0].num_completed_packets().Read(), 10);
        EXPECT_EQ(view.nocp_data()[1].connection_handle().Read(),
                  capture.connection_handles[1]);
        EXPECT_EQ(view.nocp_data()[1].num_completed_packets().Read(), 15);
      });
  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 10);
  SendReadBufferResponseFromController(proxy, 10);

  std::array<
      uint8_t,
      emboss::NumberOfCompletedPacketsEvent::MinSizeInBytes() +
          kNumConnections *
              emboss::NumberOfCompletedPacketsEventData::IntrinsicSizeInBytes()>
      hci_arr;
  H4PacketWithHci nocp_event{emboss::H4PacketType::EVENT, hci_arr};
  auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventWriter>(
      nocp_event.GetHciSpan());
  view.header().event_code_enum().Write(
      emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);
  view.num_handles().Write(kNumConnections);

  // Number_of_Completed_Packets event that reports 10 packets on Connection 0
  // and 15 packets on Connection 1.
  for (size_t i = 0; i < kNumConnections; ++i) {
    view.nocp_data()[i].connection_handle().Write(
        capture.connection_handles[i]);
    view.nocp_data()[i].num_completed_packets().Write(10 + 5 * i);
  }

  // Checks in send_to_host_fn will ensure we have not modified the NOCP event.
  proxy.HandleH4HciFromController(std::move(nocp_event));
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 10);
  EXPECT_EQ(capture.sends_called, 1);
}

TEST(NumberOfCompletedPacketsTest, HandlesUnusualEvents) {
  constexpr size_t kNumConnections = 5;
  struct {
    int sends_called = 0;
    std::array<uint16_t, kNumConnections> connection_handles = {
        0x123, 0x234, 0x345, 0x456, 0x567};
  } capture;

  pw::Function<void(H4PacketWithHci && packet)> send_to_host_fn(
      [&capture](H4PacketWithHci&& packet) {
        auto event_header =
            MakeEmboss<emboss::EventHeaderView>(packet.GetHciSpan().subspan(
                0, emboss::EventHeader::IntrinsicSizeInBytes()));
        if (event_header.event_code_enum().Read() !=
            emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS) {
          return;
        }
        ++capture.sends_called;

        auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventView>(
            packet.GetHciSpan());
        if (view.num_handles().Read() == 0) {
          return;
        }

        EXPECT_EQ(packet.GetHciSpan().size(), 23ul);
        EXPECT_EQ(view.num_handles().Read(), 5);
        EXPECT_EQ(view.header().event_code_enum().Read(),
                  emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);

        // Event should be unmodified.
        for (int i = 0; i < 5; ++i) {
          EXPECT_EQ(view.nocp_data()[i].connection_handle().Read(),
                    capture.connection_handles[i]);
          EXPECT_EQ(view.nocp_data()[i].num_completed_packets().Read(), 0);
        }
      });
  pw::Function<void(H4PacketWithH4 && packet)> send_to_controller_fn(
      []([[maybe_unused]] H4PacketWithH4&& packet) {});

  ProxyHost proxy = ProxyHost(
      std::move(send_to_host_fn), std::move(send_to_controller_fn), 10);
  SendReadBufferResponseFromController(proxy, 10);

  std::array<uint8_t, emboss::NumberOfCompletedPacketsEvent::MinSizeInBytes()>
      hci_arr_empty_event;
  // Number_of_Completed_Packets event with no entries.
  H4PacketWithHci empty_nocp_event{emboss::H4PacketType::EVENT,
                                   hci_arr_empty_event};
  auto view = MakeEmboss<emboss::NumberOfCompletedPacketsEventWriter>(
      empty_nocp_event.GetHciSpan());
  view.header().event_code_enum().Write(
      emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);
  view.num_handles().Write(0);

  std::array<
      uint8_t,
      emboss::NumberOfCompletedPacketsEvent::MinSizeInBytes() +
          kNumConnections *
              emboss::NumberOfCompletedPacketsEventData::IntrinsicSizeInBytes()>
      hci_arr_zeros_event;
  H4PacketWithHci zeros_nocp_event{emboss::H4PacketType::EVENT,
                                   hci_arr_zeros_event};
  view = MakeEmboss<emboss::NumberOfCompletedPacketsEventWriter>(
      zeros_nocp_event.GetHciSpan());
  view.header().event_code_enum().Write(
      emboss::EventCode::NUMBER_OF_COMPLETED_PACKETS);
  view.num_handles().Write(kNumConnections);
  // Number_of_Completed_Packets event that reports 0 packets for various
  // connections.
  for (size_t i = 0; i < kNumConnections; ++i) {
    view.nocp_data()[i].connection_handle().Write(
        capture.connection_handles[i]);
    view.nocp_data()[i].num_completed_packets().Write(0);
  }

  proxy.HandleH4HciFromController(std::move(empty_nocp_event));
  proxy.HandleH4HciFromController(std::move(zeros_nocp_event));
  EXPECT_EQ(proxy.GetNumFreeLeAclPackets(), 10);
  EXPECT_EQ(capture.sends_called, 2);
}

}  // namespace
}  // namespace pw::bluetooth::proxy
