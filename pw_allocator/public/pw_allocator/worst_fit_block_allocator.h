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
#pragma once

#include "pw_allocator/block_allocator.h"
#include "pw_allocator/config.h"

namespace pw::allocator {

/// Block allocator that uses a "worst-fit" allocation strategy.
///
/// In this strategy, the allocator handles an allocation request by looking at
/// all unused blocks and finding the biggest one which can satisfy the
/// request.
///
/// This algorithm may lead to less fragmentation as any unused fragments are
/// more likely to be large enough to be useful to other requests.
template <typename OffsetType = uintptr_t,
          uint16_t kPoisonInterval = PW_ALLOCATOR_BLOCK_POISON_INTERVAL>
class WorstFitBlockAllocator
    : public BlockAllocator<OffsetType, kPoisonInterval> {
 public:
  using Base = BlockAllocator<OffsetType, kPoisonInterval>;
  using BlockType = typename Base::BlockType;

  /// Constexpr constructor. Callers must explicitly call `Init`.
  constexpr WorstFitBlockAllocator() : Base() {}

  /// Non-constexpr constructor that automatically calls `Init`.
  ///
  /// @param[in]  region  Region of memory to use when satisfying allocation
  ///                     requests. The region MUST be large enough to fit an
  ///                     aligned block with overhead. It MUST NOT be larger
  ///                     than what is addressable by `OffsetType`.
  explicit WorstFitBlockAllocator(ByteSpan region) : Base(region) {}

 private:
  /// @copydoc Allocator::Allocate
  BlockType* ChooseBlock(Layout layout) override {
    // Search backwards for the biggest block that can hold this allocation.
    BlockType* worst = nullptr;
    for (auto* block : Base::rblocks()) {
      if (!block->CanAlloc(layout).ok()) {
        continue;
      }
      if (worst == nullptr || block->OuterSize() > worst->OuterSize()) {
        worst = block;
      }
    }
    if (worst != nullptr && BlockType::AllocLast(worst, layout).ok()) {
      return worst;
    }
    return nullptr;
  }
};

}  // namespace pw::allocator
