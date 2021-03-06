// Copyright (c) 2016 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "opt/optimizer.hpp"
#include "spirv/1.1/spirv.h"

namespace {

using namespace spvtools;
using ::testing::ContainerEq;

TEST(CppInterface, SuccessfulRoundTrip) {
  const std::string input_text = "%2 = OpSizeOf %1 %3\n";
  SpvTools t(SPV_ENV_UNIVERSAL_1_1);

  std::vector<uint32_t> binary;
  EXPECT_TRUE(t.Assemble(input_text, &binary));
  EXPECT_TRUE(binary.size() > 5u);
  EXPECT_EQ(SpvMagicNumber, binary[0]);
  EXPECT_EQ(SpvVersion, binary[1]);

  // This cannot pass validation since %1 is not defined.
  t.SetMessageConsumer([](MessageLevel level, const char* source,
                          const spv_position_t& position, const char* message) {
    EXPECT_EQ(MessageLevel::Error, level);
    EXPECT_STREQ("input", source);
    EXPECT_EQ(0u, position.line);
    EXPECT_EQ(0u, position.column);
    EXPECT_EQ(1u, position.index);
    EXPECT_STREQ("ID 1 has not been defined", message);
  });
  EXPECT_FALSE(t.Validate(binary));

  std::string output_text;
  EXPECT_TRUE(t.Disassemble(binary, &output_text));
  EXPECT_EQ(input_text, output_text);
}

TEST(CppInterface, AssembleEmptyModule) {
  std::vector<uint32_t> binary(10, 42);
  SpvTools t(SPV_ENV_UNIVERSAL_1_1);
  EXPECT_TRUE(t.Assemble("", &binary));
  // We only have the header.
  EXPECT_EQ(5u, binary.size());
  EXPECT_EQ(SpvMagicNumber, binary[0]);
  EXPECT_EQ(SpvVersion, binary[1]);
}

TEST(CppInterface, AssembleWithWrongTargetEnv) {
  const std::string input_text = "%r = OpSizeOf %type %pointer";
  SpvTools t(SPV_ENV_UNIVERSAL_1_0);
  int invocation_count = 0;
  t.SetMessageConsumer(
      [&invocation_count](MessageLevel level, const char* source,
                          const spv_position_t& position, const char* message) {
        ++invocation_count;
        EXPECT_EQ(MessageLevel::Error, level);
        EXPECT_STREQ("input", source);
        EXPECT_EQ(0u, position.line);
        EXPECT_EQ(5u, position.column);
        EXPECT_EQ(5u, position.index);
        EXPECT_STREQ("Invalid Opcode name 'OpSizeOf'", message);
      });

  std::vector<uint32_t> binary = {42, 42};
  EXPECT_FALSE(t.Assemble(input_text, &binary));
  EXPECT_THAT(binary, ContainerEq(std::vector<uint32_t>{42, 42}));
  EXPECT_EQ(1, invocation_count);
}

TEST(CppInterface, DisassembleEmptyModule) {
  std::string text(10, 'x');
  SpvTools t(SPV_ENV_UNIVERSAL_1_1);
  int invocation_count = 0;
  t.SetMessageConsumer(
      [&invocation_count](MessageLevel level, const char* source,
                          const spv_position_t& position, const char* message) {
        ++invocation_count;
        EXPECT_EQ(MessageLevel::Error, level);
        EXPECT_STREQ("input", source);
        EXPECT_EQ(0u, position.line);
        EXPECT_EQ(0u, position.column);
        EXPECT_EQ(0u, position.index);
        EXPECT_STREQ("Missing module.", message);
      });
  EXPECT_FALSE(t.Disassemble({}, &text));
  EXPECT_EQ("xxxxxxxxxx", text);  // The original string is unmodified.
  EXPECT_EQ(1, invocation_count);
}

TEST(CppInterface, DisassembleWithWrongTargetEnv) {
  const std::string input_text = "%r = OpSizeOf %type %pointer";
  SpvTools t11(SPV_ENV_UNIVERSAL_1_1);
  SpvTools t10(SPV_ENV_UNIVERSAL_1_0);
  int invocation_count = 0;
  t10.SetMessageConsumer(
      [&invocation_count](MessageLevel level, const char* source,
                          const spv_position_t& position, const char* message) {
        ++invocation_count;
        EXPECT_EQ(MessageLevel::Error, level);
        EXPECT_STREQ("input", source);
        EXPECT_EQ(0u, position.line);
        EXPECT_EQ(0u, position.column);
        EXPECT_EQ(5u, position.index);
        EXPECT_STREQ("Invalid opcode: 321", message);
      });

  std::vector<uint32_t> binary;
  EXPECT_TRUE(t11.Assemble(input_text, &binary));

  std::string output_text(10, 'x');
  EXPECT_FALSE(t10.Disassemble(binary, &output_text));
  EXPECT_EQ("xxxxxxxxxx", output_text);  // The original string is unmodified.
}

TEST(CppInterface, SuccessfulValidation) {
  const std::string input_text =
      "OpCapability Shader\nOpMemoryModel Logical GLSL450";
  SpvTools t(SPV_ENV_UNIVERSAL_1_1);
  int invocation_count = 0;
  t.SetMessageConsumer(
      [&invocation_count](MessageLevel, const char*, const spv_position_t&,
                          const char*) { ++invocation_count; });

  std::vector<uint32_t> binary;
  EXPECT_TRUE(t.Assemble(input_text, &binary));
  EXPECT_TRUE(t.Validate(binary));
  EXPECT_EQ(0, invocation_count);
}

TEST(CppInterface, ValidateEmptyModule) {
  SpvTools t(SPV_ENV_UNIVERSAL_1_1);
  int invocation_count = 0;
  t.SetMessageConsumer(
      [&invocation_count](MessageLevel level, const char* source,
                          const spv_position_t& position, const char* message) {
        ++invocation_count;
        EXPECT_EQ(MessageLevel::Error, level);
        EXPECT_STREQ("input", source);
        EXPECT_EQ(0u, position.line);
        EXPECT_EQ(0u, position.column);
        EXPECT_EQ(0u, position.index);
        EXPECT_STREQ("Invalid SPIR-V magic number.", message);
      });
  EXPECT_FALSE(t.Validate({}));
  EXPECT_EQ(1, invocation_count);
}

// Checks that after running the given optimizer |opt| on the given |original|
// source code, we can get the given |optimized| source code.
void CheckOptimization(const char* original, const char* optimized,
                       const Optimizer& opt) {
  SpvTools t(SPV_ENV_UNIVERSAL_1_1);
  std::vector<uint32_t> original_binary;
  ASSERT_TRUE(t.Assemble(original, &original_binary));

  std::vector<uint32_t> optimized_binary;
  EXPECT_TRUE(opt.Run(original_binary.data(), original_binary.size(),
                      &optimized_binary));

  std::string optimized_text;
  EXPECT_TRUE(t.Disassemble(optimized_binary, &optimized_text));
  EXPECT_EQ(optimized, optimized_text);
}

TEST(CppInterface, OptimizeEmptyModule) {
  SpvTools t(SPV_ENV_UNIVERSAL_1_1);
  std::vector<uint32_t> binary;
  EXPECT_TRUE(t.Assemble("", &binary));

  Optimizer o(SPV_ENV_UNIVERSAL_1_1);
  o.RegisterPass(CreateStripDebugInfoPass());
  EXPECT_TRUE(o.Run(binary.data(), binary.size(), &binary));
}

TEST(CppInterface, OptimizeModifiedModule) {
  Optimizer o(SPV_ENV_UNIVERSAL_1_1);
  o.RegisterPass(CreateStripDebugInfoPass());
  CheckOptimization("OpSource GLSL 450", "", o);
}

TEST(CppInterface, OptimizeMulitplePasses) {
  const char* original_text =
      "OpSource GLSL 450 "
      "OpDecorate %true SpecId 1 "
      "%bool = OpTypeBool "
      "%true = OpSpecConstantTrue %bool";

  Optimizer o(SPV_ENV_UNIVERSAL_1_1);
  o.RegisterPass(CreateStripDebugInfoPass())
      .RegisterPass(CreateFreezeSpecConstantValuePass());

  const char* expected_text =
      "%bool = OpTypeBool\n"
      "%1 = OpConstantTrue %bool\n";

  CheckOptimization(original_text, expected_text, o);
}

TEST(CppInterface, OptimizeDoNothingWithPassToken) {
  CreateFreezeSpecConstantValuePass();
  auto token = CreateUnifyConstantPass();
}

TEST(CppInterface, OptimizeReassignPassToken) {
  auto token = CreateNullPass();
  token = CreateStripDebugInfoPass();

  CheckOptimization(
      "OpSource GLSL 450", "",
      Optimizer(SPV_ENV_UNIVERSAL_1_1).RegisterPass(std::move(token)));
}

TEST(CppInterface, OptimizeMoveConstructPassToken) {
  auto token1 = CreateStripDebugInfoPass();
  Optimizer::PassToken token2(std::move(token1));

  CheckOptimization(
      "OpSource GLSL 450", "",
      Optimizer(SPV_ENV_UNIVERSAL_1_1).RegisterPass(std::move(token2)));
}

TEST(CppInterface, OptimizeMoveAssignPassToken) {
  auto token1 = CreateStripDebugInfoPass();
  auto token2 = CreateNullPass();
  token2 = std::move(token1);

  CheckOptimization(
      "OpSource GLSL 450", "",
      Optimizer(SPV_ENV_UNIVERSAL_1_1).RegisterPass(std::move(token2)));
}

TEST(CppInterface, OptimizeSameAddressForOriginalOptimizedBinary) {
  SpvTools t(SPV_ENV_UNIVERSAL_1_1);
  std::vector<uint32_t> binary;
  ASSERT_TRUE(t.Assemble("OpSource GLSL 450", &binary));

  EXPECT_TRUE(Optimizer(SPV_ENV_UNIVERSAL_1_1)
                  .RegisterPass(CreateStripDebugInfoPass())
                  .Run(binary.data(), binary.size(), &binary));

  std::string optimized_text;
  EXPECT_TRUE(t.Disassemble(binary, &optimized_text));
  EXPECT_EQ("", optimized_text);
}

// TODO(antiagainst): tests for SetMessageConsumer().

}  // anonymous namespace
