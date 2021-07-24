#include "gflags.h"

using gflags::clstring;
using gflags::CommandLineFlag;
using gflags::FlagRegistry;
using gflags::FlagRegistryLock;
using gflags::FlagValue;
using gflags::int32;
using gflags::int64;
using gflags::uint32;
using gflags::uint64;
using std::pair;
using std::string;

// --------------------------------------------------------------------
// FlagRegistry
//    A FlagRegistry singleton object holds all flag objects indexed
//    by their names so that if you know a flag's name (as a C
//    string), you can access or set it.  If the function is named
//    FooLocked(), you must own the registry lock before calling
//    the function; otherwise, you should *not* hold the lock, and
//    the function will acquire it itself if needed.
// --------------------------------------------------------------------

// Get the singleton FlagRegistry object
FlagRegistry *FlagRegistry::global_registry_ = NULL;

FlagRegistry::FlagRegistry() {}

FlagRegistry::~FlagRegistry() {
  // Not using STLDeleteElements as that resides in util and this
  // class is base.
  for (FlagMap::iterator p = flags_.begin(), e = flags_.end(); p != e; ++p) {
    CommandLineFlag *flag = p->second;
    delete flag;
  }
}

FlagRegistry *FlagRegistry::GlobalRegistry() {
  static Mutex lock(Mutex::LINKER_INITIALIZED);
  MutexLock acquire_lock(&lock);
  if (!global_registry_) {
    global_registry_ = new FlagRegistry;
  }
  return global_registry_;
}

void FlagRegistry::DeleteGlobalRegistry() {
  delete global_registry_;
  global_registry_ = NULL;
}

void FlagRegistry::Lock() { lock_.Lock(); }

void FlagRegistry::Unlock() { lock_.Unlock(); }

void FlagRegistry::RegisterFlag(CommandLineFlag *flag) {
  Lock();
  pair<FlagIterator, bool> ins =
      flags_.insert(pair<const char *, CommandLineFlag *>(flag->name(), flag));
  if (ins.second == false) { // means the name was already in the map
    if (strcmp(ins.first->second->filename(), flag->filename()) != 0) {
      ReportError(DIE,
                  "ERROR: flag '%s' was defined more than once "
                  "(in files '%s' and '%s').\n",
                  flag->name(), ins.first->second->filename(),
                  flag->filename());
    } else {
      ReportError(DIE,
                  "ERROR: something wrong with flag '%s' in file '%s'.  "
                  "One possibility: file '%s' is being linked both statically "
                  "and dynamically into this executable.\n",
                  flag->name(), flag->filename(), flag->filename());
    }
  }
  // Also add to the flags_by_ptr_ map.
  flags_by_ptr_[flag->current_->value_buffer_] = flag;
  Unlock();
}

CommandLineFlag *FlagRegistry::FindFlagLocked(const char *name) {
  FlagConstIterator i = flags_.find(name);
  if (i == flags_.end()) {
    // If the name has dashes in it, try again after replacing with
    // underscores.
    if (strchr(name, '-') == NULL)
      return NULL;
    string name_rep = name;
    std::replace(name_rep.begin(), name_rep.end(), '-', '_');
    return FindFlagLocked(name_rep.c_str());
  } else {
    return i->second;
  }
}

CommandLineFlag *FlagRegistry::FindFlagViaPtrLocked(const void *flag_ptr) {
  FlagPtrMap::const_iterator i = flags_by_ptr_.find(flag_ptr);
  if (i == flags_by_ptr_.end()) {
    return NULL;
  } else {
    return i->second;
  }
}

CommandLineFlag *FlagRegistry::SplitArgumentLocked(const char *arg, string *key,
                                                   const char **v,
                                                   string *error_message) {
  // Find the flag object for this option
  const char *flag_name;
  const char *value = strchr(arg, '=');
  if (value == NULL) {
    key->assign(arg);
    *v = NULL;
  } else {
    // Strip out the "=value" portion from arg
    key->assign(arg, value - arg);
    *v = ++value; // advance past the '='
  }
  flag_name = key->c_str();

  CommandLineFlag *flag = FindFlagLocked(flag_name);

  if (flag == NULL) {
    // If we can't find the flag-name, then we should return an error.
    // The one exception is if 1) the flag-name is 'nox', 2) there
    // exists a flag named 'x', and 3) 'x' is a boolean flag.
    // In that case, we want to return flag 'x'.
    if (!(flag_name[0] == 'n' && flag_name[1] == 'o')) {
      // flag-name is not 'nox', so we're not in the exception case.
      *error_message = StringPrintf("%sunknown command line flag '%s'\n",
                                    kError, key->c_str());
      return NULL;
    }
    flag = FindFlagLocked(flag_name + 2);
    if (flag == NULL) {
      // No flag named 'x' exists, so we're not in the exception case.
      *error_message = StringPrintf("%sunknown command line flag '%s'\n",
                                    kError, key->c_str());
      return NULL;
    }
    if (flag->Type() != FV_BOOL) {
      // 'x' exists but is not boolean, so we're not in the exception case.
      *error_message = StringPrintf(
          "%sboolean value (%s) specified for %s command line flag\n", kError,
          key->c_str(), flag->type_name());
      return NULL;
    }
    // We're in the exception case!
    // Make up a fake value to replace the "no" we stripped out
    key->assign(flag_name + 2); // the name without the "no"
    *v = "0";
  }

  // Assign a value if this is a boolean flag
  if (*v == NULL && flag->Type() == FV_BOOL) {
    *v = "1"; // the --nox case was already handled, so this is the --x case
  }

  return flag;
}

bool FlagRegistry::SetFlagLocked(CommandLineFlag *flag, const char *value,
                                 FlagSettingMode set_mode, string *msg) {
  flag->UpdateModifiedBit();
  switch (set_mode) {
  case SET_FLAGS_VALUE: {
    // set or modify the flag's value
    if (!TryParseLocked(flag, flag->current_, value, msg))
      return false;
    flag->modified_ = true;
    break;
  }
  case SET_FLAG_IF_DEFAULT: {
    // set the flag's value, but only if it hasn't been set by someone else
    if (!flag->modified_) {
      if (!TryParseLocked(flag, flag->current_, value, msg))
        return false;
      flag->modified_ = true;
    } else {
      *msg = StringPrintf("%s set to %s", flag->name(),
                          flag->current_value().c_str());
    }
    break;
  }
  case SET_FLAGS_DEFAULT: {
    // modify the flag's default-value
    if (!TryParseLocked(flag, flag->defvalue_, value, msg))
      return false;
    if (!flag->modified_) {
      // Need to set both defvalue *and* current, in this case
      TryParseLocked(flag, flag->current_, value, NULL);
    }
    break;
  }
  default: {
    // unknown set_mode
    assert(false);
    return false;
  }
  }

  return true;
}

// --------------------------------------------------------------------
// FlagRegistryLock
// --------------------------------------------------------------------

FlagRegistryLock::FlagRegistryLock(FlagRegistry *fr) : fr_(fr) { fr_->Lock(); }

FlagRegistryLock::~FlagRegistryLock() { fr_->Unlock(); }
