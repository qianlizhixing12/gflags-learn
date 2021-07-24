#include <iostream>
#include <string>
#include "gflags.h"

using gflags::FlagRegistry;
using gflags::Gflags;
using std::cout;
using std::endl;
using std::string;

DEFINE_int32(timeout, 30, "The message to print");

static bool ValidateTimeout(const char *flagname, int32_t timeout) {
  return timeout > 0;
}
DEFINE_validator(timeout, ValidateTimeout);

int main(int argc, char **argv) {
  string val;
  Gflags parse;
  parse.SetUsageMessage("Test gflags");
  parse.SetVersionString("0.1");
  parse.ParseCommandLineFlags(&argc, &argv, true);
  cout << parse.GetCommandLineOption("timeout", &val) << endl;
  cout << val << endl;
  cout << FLAGS_timeout << endl;
  parse.ShutDownCommandLineFlags();
  return 0;
}
