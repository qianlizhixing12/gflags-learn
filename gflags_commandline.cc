#include "gflags.h"

using gflags::clstring;
using gflags::CommandLineFlag;
using gflags::CommandLineFlagParser;
using gflags::DieWhenReporting;
using gflags::FlagRegistry;
using gflags::int32;
using gflags::int64;
using gflags::uint32;
using gflags::uint64;
using gflags::ValidateFnProto;
using gflags::ValueType;
using std::cout;
using std::string;

// --------------------------------------------------------------------
// CommandLineFlag
//    This represents a single flag, including its name, description,
//    default value, and current value.  Mostly this serves as a
//    struct, though it also knows how to register itself.
//       All CommandLineFlags are owned by a (exactly one)
//    FlagRegistry.  If you wish to modify fields in this class, you
//    should acquire the FlagRegistry lock for the registry that owns
//    this flag.
// --------------------------------------------------------------------

CommandLineFlag::CommandLineFlag(const char *name, const char *help,
                                 const char *filename, FlagValue *current_val,
                                 FlagValue *default_val)
    : name_(name), help_(help), file_(filename), modified_(false),
      defvalue_(default_val), current_(current_val), validate_fn_proto_(NULL) {}

CommandLineFlag::~CommandLineFlag() {
  delete current_;
  delete defvalue_;
}

const char *CommandLineFlag::name() const { return name_; }

const char *CommandLineFlag::help() const { return help_; }

const char *CommandLineFlag::filename() const { return file_; }

const char *CommandLineFlag::CleanFileName() const {
  // This function has been used to strip off a common prefix from
  // flag source file names. Because flags can be defined in different
  // shared libraries, there may not be a single common prefix.
  // Further, this functionality hasn't been active for many years.
  // Need a better way to produce more user friendly help output or
  // "anonymize" file paths in help output, respectively.
  // Follow issue at: https://github.com/gflags/gflags/issues/86
  return filename();
}

string CommandLineFlag::current_value() const { return current_->ToString(); }

string CommandLineFlag::default_value() const { return defvalue_->ToString(); }

const char *CommandLineFlag::type_name() const { return defvalue_->TypeName(); }

ValidateFnProto CommandLineFlag::validate_function() const {
  return validate_fn_proto_;
}

const void *CommandLineFlag::flag_ptr() const {
  return current_->value_buffer_;
}

ValueType CommandLineFlag::Type() const { return defvalue_->Type(); }

bool CommandLineFlag::Validate(const FlagValue &value) const {
  if (validate_function() == NULL)
    return true;
  else
    return value.Validate(name(), validate_function());
}

bool CommandLineFlag::ValidateCurrent() const { return Validate(*current_); }

bool CommandLineFlag::Modified() const { return modified_; }

void CommandLineFlag::CopyFrom(const CommandLineFlag &src) {
  // Note we only copy the non-const members; others are fixed at construct time
  if (modified_ != src.modified_)
    modified_ = src.modified_;
  if (!current_->Equal(*src.current_))
    current_->CopyFrom(*src.current_);
  if (!defvalue_->Equal(*src.defvalue_))
    defvalue_->CopyFrom(*src.defvalue_);
  if (validate_fn_proto_ != src.validate_fn_proto_)
    validate_fn_proto_ = src.validate_fn_proto_;
}

void CommandLineFlag::UpdateModifiedBit() {
  // Update the "modified" bit in case somebody bypassed the
  // Flags API and wrote directly through the FLAGS_name variable.
  if (!modified_ && !current_->Equal(*defvalue_)) {
    modified_ = true;
  }
}

// --------------------------------------------------------------------
// CommandLineFlagParser
//    Parsing is done in two stages.  In the first, we go through
//    argv.  For every flag-like arg we can make sense of, we parse
//    it and set the appropriate FLAGS_* variable.  For every flag-
//    like arg we can't make sense of, we store it in a vector,
//    along with an explanation of the trouble.  In stage 2, we
//    handle the 'reporting' flags like --help and --mpm_version.
//    (This is via a call to HandleCommandLineHelpFlags(), in
//    gflags_reporting.cc.)
//    An optional stage 3 prints out the error messages.
//       This is a bit of a simplification.  For instance, --flagfile
//    is handled as soon as it's seen in stage 1, not in stage 2.
// --------------------------------------------------------------------

CommandLineFlagParser::CommandLineFlagParser(Gflags *enter, FlagRegistry *reg)
    : enter_(enter), registry_(reg) {}

CommandLineFlagParser::~CommandLineFlagParser() {}

uint32 CommandLineFlagParser::ParseNewCommandLineFlags(int *argc, char ***argv,
                                                       bool remove_flags) {
  int first_nonopt = *argc; // for non-options moved to the end

  registry_->Lock();
  for (int i = 1; i < first_nonopt; i++) {
    char *arg = (*argv)[i];

    // Like getopt(), we permute non-option flags to be at the end.
    if (arg[0] != '-' || arg[1] == '\0') { // must be a program argument: "-" is
                                           // an argument, not a flag
      memmove((*argv) + i, (*argv) + i + 1,
              (*argc - (i + 1)) * sizeof((*argv)[i]));
      (*argv)[*argc - 1] = arg; // we go last
      first_nonopt--;           // we've been pushed onto the stack
      i--;                      // to undo the i++ in the loop
      continue;
    }
    arg++; // skip leading '-'
    if (arg[0] == '-')
      arg++; // or leading '--'

    // -- alone means what it does for GNU: stop options parsing
    if (*arg == '\0') {
      first_nonopt = i + 1;
      break;
    }

    // Find the flag object for this option
    string key;
    const char *value;
    string error_message;
    CommandLineFlag *flag =
        registry_->SplitArgumentLocked(arg, &key, &value, &error_message);
    if (flag == NULL) {
      undefined_names_[key] = ""; // value isn't actually used
      error_flags_[key] = error_message;
      continue;
    }

    if (value == NULL) {
      // Boolean options are always assigned a value by SplitArgumentLocked()
      assert(flag->Type() != FV_BOOL);
      if (i + 1 >= first_nonopt) {
        // This flag needs a value, but there is nothing available
        error_flags_[key] = (string(kError) + "flag '" + (*argv)[i] + "'" +
                             " is missing its argument");
        if (flag->help() && flag->help()[0] > '\001') {
          // Be useful in case we have a non-stripped description.
          error_flags_[key] += string("; flag description: ") + flag->help();
        }
        error_flags_[key] += "\n";
        break; // we treat this as an unrecoverable error
      } else {
        value = (*argv)[++i]; // read next arg for value

        // Heuristic to detect the case where someone treats a string arg
        // like a bool:
        // --my_string_var --foo=bar
        // We look for a flag of string type, whose value begins with a
        // dash, and where the flag-name and value are separated by a
        // space rather than an '='.
        // To avoid false positives, we also require the word "true"
        // or "false" in the help string.  Without this, a valid usage
        // "-lat -30.5" would trigger the warning.  The common cases we
        // want to solve talk about true and false as values.
        if (value[0] == '-' && flag->Type() == FV_STRING &&
            (strstr(flag->help(), "true") || strstr(flag->help(), "false"))) {
          cout << "Did you really mean to set flag '" << flag->name()
               << "' to the value '" << value << "'?";
        }
      }
    }

    // TODO(csilvers): only set a flag if we hadn't set it before here
    ProcessSingleOptionLocked(flag, value, SET_FLAGS_VALUE);
  }
  registry_->Unlock();

  if (remove_flags) { // Fix up argc and argv by removing command line flags
    (*argv)[first_nonopt - 1] = (*argv)[0];
    (*argv) += (first_nonopt - 1);
    (*argc) -= (first_nonopt - 1);
    first_nonopt = 1; // because we still don't count argv[0]
  }

  return first_nonopt;
}

string CommandLineFlagParser::ProcessSingleOptionLocked(
    CommandLineFlag *flag, const char *value, FlagSettingMode set_mode) {
  string msg;
  if (value && !registry_->SetFlagLocked(flag, value, set_mode, &msg)) {
    error_flags_[flag->name()] = msg;
    return "";
  }

  return msg;
}

void CommandLineFlagParser::ValidateFlags(bool all) {
  FlagRegistryLock frl(registry_);
  for (FlagRegistry::FlagConstIterator i = registry_->flags_.begin();
       i != registry_->flags_.end(); ++i) {
    if ((all || !i->second->Modified()) && !i->second->ValidateCurrent()) {
      // only set a message if one isn't already there.  (If there's
      // an error message, our job is done, even if it's not exactly
      // the same error.)
      if (error_flags_[i->second->name()].empty()) {
        error_flags_[i->second->name()] = string(kError) + "--" +
                                          i->second->name() +
                                          " must be set on the commandline";
        if (!i->second->Modified()) {
          error_flags_[i->second->name()] +=
              " (default value fails validation)";
        }
        error_flags_[i->second->name()] += "\n";
      }
    }
  }
}

void CommandLineFlagParser::ValidateUnmodifiedFlags() { ValidateFlags(false); }
