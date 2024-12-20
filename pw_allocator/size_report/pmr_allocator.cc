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

#include "pw_allocator/pmr_allocator.h"

#include <vector>

#include "pw_allocator/first_fit.h"
#include "pw_allocator/size_reporter.h"
#include "pw_assert/check.h"

int main() {
  using Bar = ::pw::allocator::SizeReporter::Bar;

  pw::allocator::SizeReporter reporter;
  reporter.SetBaseline();

  pw::allocator::FirstFitAllocator<> base(reporter.buffer());
  pw::allocator::PmrAllocator allocator(base);
  std::pmr::vector<Bar> vec(allocator);
  vec.emplace_back(1);
  PW_CHECK_UINT_EQ(vec.size(), 1U);
  vec.clear();

  return 0;
}
