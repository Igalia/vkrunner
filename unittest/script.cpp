// Copyright (c) 2018 Google LLC.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "config.h"

#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>

#include "gtest/gtest.h"

extern "C" {
#include "vr-config.h"
#include "vr-list.h"
#include "vr-script.h"
#include "vr-source-private.h"
}

namespace {

void PrintError(const char *message, void *user_data) {
  std::cout << message << std::endl;
}

vr_config config = {
  .show_disassembly = false,
  .error_cb = PrintError,
  .inspect_cb = nullptr,
  .user_data = NULL,
};

class ScriptBuilder
{
public:
  ScriptBuilder(const std::string &commands) {
    source = static_cast<vr_source *>(malloc(sizeof *source + commands.size() + 1));
    source->type = VR_SOURCE_TYPE_STRING;
    vr_list_init(&source->token_replacements);
    memcpy(source->string, commands.c_str(), commands.size() + 1);
  }

  vr_source *GetSource() { return source; }

  ~ScriptBuilder() {
    if(source) delete source;
  }

private:
  vr_source *source;
};

std::string test_section_header = "[test]\n";

// Tests factorial of positive numbers.
TEST(Script, Clear) {
  std::stringstream ss;
  ss << test_section_header;
  ss << "    clear     " << std::endl;

  ScriptBuilder builder(ss.str());
  auto script = vr_script_load(&config, builder.GetSource());
  EXPECT_EQ(1, script->n_commands);
  EXPECT_EQ(VR_SCRIPT_OP_CLEAR, script->commands[0].op);
}

}  // namespace
