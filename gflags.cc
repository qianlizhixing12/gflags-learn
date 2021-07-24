#include "gflags.h"

using gflags::clstring;
using gflags::CommandLineFlag;
using gflags::FlagRegistry;
using gflags::FlagRegistryLock;
using gflags::FlagSettingMode;
using gflags::Gflags;
using gflags::int32;
using gflags::int64;
using gflags::uint32;
using gflags::uint64;
using gflags::ValidateFnProto;
using std::string;
using std::vector;

Gflags::Gflags() {
  argv0 = "UNKNOWN";
  cmdline = "";
  // argvs;
  argv_sum = 0;
  program_usage = "";
  version_string = "";
}

void Gflags::SetUsageMessage(const string &usage) { program_usage = usage; }

const char *Gflags::ProgramUsage() {
  if (program_usage.empty()) {
    return "Warning: SetUsageMessage() never called";
  }
  return program_usage.c_str();
}

void Gflags::SetVersionString(const string &version) {
  version_string = version;
}

const char *Gflags::VersionString() { return version_string.c_str(); }

uint32 Gflags::ParseCommandLineFlags(int *argc, char ***argv,
                                     bool remove_flags) {
  return ParseCommandLineFlagsInternal(argc, argv, remove_flags, true);
}

const vector<string> &Gflags::GetArgvs() const { return argvs; }

const char *Gflags::GetArgv() const { return cmdline.c_str(); }

const char *Gflags::GetArgv0() const { return argv0.c_str(); }

uint32 Gflags::GetArgvSum() const { return argv_sum; }

const char *Gflags::ProgramInvocationName() const { // like the GNU libc fn
  return GetArgv0();
}

const char *Gflags::ProgramInvocationShortName() const { // like the GNU libc fn
  size_t pos = argv0.rfind('/');
  return (pos == string::npos ? argv0.c_str() : (argv0.c_str() + pos + 1));
}

void Gflags::SetArgv(int argc, const char **argv) {
  static bool called_set_argv = false;
  if (called_set_argv)
    return;
  called_set_argv = true;

  assert(argc > 0); // every program has at least a name
  argv0 = argv[0];

  cmdline.clear();
  for (int i = 0; i < argc; i++) {
    if (i != 0)
      cmdline += " ";
    cmdline += argv[i];
    argvs.push_back(argv[i]);
  }

  // Compute a simple sum of all the chars in argv
  argv_sum = 0;
  for (string::const_iterator c = cmdline.begin(); c != cmdline.end(); ++c) {
    argv_sum += *c;
  }
}

uint32 Gflags::ParseCommandLineFlagsInternal(int *argc, char ***argv,
                                             bool remove_flags,
                                             bool do_report) {
  SetArgv(*argc, const_cast<const char **>(*argv)); // save it for later

  FlagRegistry *const registry = FlagRegistry::GlobalRegistry();
  CommandLineFlagParser parser(this, registry);

  // Now get the flags specified on the commandline
  const int r = parser.ParseNewCommandLineFlags(argc, argv, remove_flags);

  // See if any of the unset flags fail their validation checks
  parser.ValidateUnmodifiedFlags();

  return r;
}

// Return true iff the flagname was found.
// OUTPUT is set to the flag's value, or unchanged if we return false.
bool Gflags::GetCommandLineOption(const char *name, string *value) {
  if (NULL == name)
    return false;
  assert(value);

  FlagRegistry *const registry = FlagRegistry::GlobalRegistry();
  FlagRegistryLock frl(registry);
  CommandLineFlag *flag = registry->FindFlagLocked(name);
  if (flag == NULL) {
    return false;
  } else {
    *value = flag->current_value();
    return true;
  }
}

// Clean up memory allocated by flags.  This is only needed to reduce
// the quantity of "potentially leaked" reports emitted by memory
// debugging tools such as valgrind.  It is not required for normal
// operation, or for the google perftools heap-checker.  It must only
// be called when the process is about to exit, and all threads that
// might access flags are quiescent.  Referencing flags after this is
// called will have unexpected consequences.  This is not safe to run
// when multiple threads might be running: the function is
// thread-hostile.
void Gflags::ShutDownCommandLineFlags() {
  FlagRegistry::DeleteGlobalRegistry();
}