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

#include "pw_clock_tree_mcuxpresso/clock_tree.h"

// Test headers
#include "pw_unit_test/framework.h"

// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeElementDefs-Flexcomm0]

// Define FRO_DIV_4 clock source
PW_CONSTINIT pw::clock_tree::ClockMcuxpressoFro fro_div_4(kCLOCK_FroDiv4OutEn);

// Define FRG0 configuration
const clock_frg_clk_config_t g_frg0Config_BOARD_BOOTCLOCKRUN = {
    .num = 0,
    .sfg_clock_src = _clock_frg_clk_config::kCLOCK_FrgFroDiv4,
    .divider = 255U,
    .mult = 144};

PW_CONSTINIT pw::clock_tree::ClockMcuxpressoFrgNonBlocking frg_0(
    fro_div_4, g_frg0Config_BOARD_BOOTCLOCKRUN);

// Define clock source selector FLEXCOMM0
PW_CONSTINIT pw::clock_tree::ClockMcuxpressoSelectorNonBlocking flexcomm_0(
    frg_0, kFRG_to_FLEXCOMM0, kNONE_to_FLEXCOMM0);

// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeElementDefs-Flexcomm0]

// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeElementDefs-i3c0]

// Define FRO_DIV_8 clock source
PW_CONSTINIT pw::clock_tree::ClockMcuxpressoFro fro_div_8(kCLOCK_FroDiv8OutEn);

// Define clock source selector I3C01FCLKSEL
PW_CONSTINIT pw::clock_tree::ClockMcuxpressoSelectorNonBlocking i3c0_selector(
    fro_div_8, kFRO_DIV8_to_I3C_CLK, kNONE_to_I3C_CLK);

// Define clock divider I3C01FCLKDIV
PW_CONSTINIT pw::clock_tree::ClockMcuxpressoDividerNonBlocking i3c0_divider(
    i3c0_selector, kCLOCK_DivI3cClk, 12);

// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeElementDefs-i3c0]

// inclusive-language: disable
// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeElementDefs-Ctimer0]

// Define Master clock
PW_CONSTINIT pw::clock_tree::ClockMcuxpressoMclk mclk(19200000);

// Define clock selector CTIMER0
PW_CONSTINIT pw::clock_tree::ClockMcuxpressoSelectorNonBlocking ctimer_0(
    mclk, kMASTER_CLK_to_CTIMER0, kNONE_to_CTIMER0);

// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeElementDefs-Ctimer0]
// inclusive-language: enable

// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeElementDefs-LpOsc]

// Define Low-Power Oscillator
PW_CONSTINIT pw::clock_tree::ClockMcuxpressoLpOsc lp_osc_clk;

// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeElementDefs-LpOsc]

// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeDef]

// Create the clock tree
pw::clock_tree::ClockTree clock_tree;

// DOCSTAG: [pw_clock_tree_mcuxpresso-examples-ClockTreeDef]

TEST(ClockTreeMcuxpresso, UseExample) {
  // DOCSTAG: [pw_clock_tree_mcuxpresso-examples-UseExample]

  // Enable the low-power oscillator
  clock_tree.Acquire(lp_osc_clk);

  // Enable the i3c0_divider
  clock_tree.Acquire(i3c0_divider);

  // Change the i3c0_divider value
  clock_tree.SetDividerValue(i3c0_divider, 24);

  // Disable the low-power oscillator
  clock_tree.Release(lp_osc_clk);

  // DOCSTAG: [pw_clock_tree_mcuxpresso-examples-UseExample]
}
