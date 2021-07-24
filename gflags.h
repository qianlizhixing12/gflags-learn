#ifndef GFLAGS_H_
#define GFLAGS_H_

#include <stdlib.h>
#include <stdint.h> // the normal place uint32_t is defined
// #include <sys/types.h> // the normal place u_int32_t is defined
// #include <inttypes.h> // a third place for uint32_t or u_int32_t
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <fnmatch.h>
#include <stdarg.h> // For va_list and related operations

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "gflags_mutex.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

namespace gflags {

using std::cout;
using std::map;
using std::string;
using std::vector;

typedef signed char int8;
typedef unsigned char uint8;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef string clstring;

typedef bool (*ValidateFnProto)();

class FlagValue;

class CommandLineFlag;

class FlagRegistry;

class Gflags;

enum ValueType {
  FV_BOOL = 0,
  FV_INT32 = 1,
  FV_UINT32 = 2,
  FV_INT64 = 3,
  FV_UINT64 = 4,
  FV_DOUBLE = 5,
  FV_STRING = 6,
  FV_MAX_INDEX = 6,
};

enum FlagSettingMode {
  // update the flag's value (can call this multiple times).
  SET_FLAGS_VALUE,
  // update the flag's value, but *only if* it has not yet been updated
  // with SET_FLAGS_VALUE, SET_FLAG_IF_DEFAULT, or "FLAGS_xxx = nondef".
  SET_FLAG_IF_DEFAULT,
  // set the flag's default value to this.  If the flag has not yet updated
  // yet (via SET_FLAGS_VALUE, SET_FLAG_IF_DEFAULT, or "FLAGS_xxx = nondef")
  // change the flag's current value to the new default value as well.
  SET_FLAGS_DEFAULT
};

struct StringCmp { // Used by the FlagRegistry map class to compare char*'s
  bool operator()(const char *s1, const char *s2) const {
    return (strcmp(s1, s2) < 0);
  }
};

// Whether we should die when reporting an error.
enum DieWhenReporting { DIE, DO_NOT_DIE };

extern void (*gflags_exitfunc)(int); // from stdlib.h

static const char kError[] = "ERROR: ";

// Report Error and exit if requested.
extern void ReportError(DieWhenReporting should_die, const char *format, ...);

extern void InternalStringPrintf(std::string *output, const char *format,
                                 va_list ap);

extern string StringPrintf(const char *format, ...);

extern void StringAppendF(std::string *output, const char *format, ...);

// This could be a templated method of FlagValue, but doing so adds to the
// size of the .o.  Since there's no type-safety here anyway, macro is ok.

extern bool TryParseLocked(const CommandLineFlag *flag, FlagValue *flag_value,
                           const char *value, string *msg);

class FlagValue {
public:
  template <typename FlagType>
  FlagValue(FlagType *valbuf, bool transfer_ownership_of_value);
  ~FlagValue();

  bool ParseFrom(const char *spec);
  string ToString() const;

  ValueType Type() const { return static_cast<ValueType>(type_); }

private:
  friend class CommandLineFlag; // for many things, including Validate()
  // friend class FlagSaverImpl;   // calls New()
  friend class FlagRegistry; // checks value_buffer_ for flags_by_ptr_ map
  // template <typename T> friend T GetFromEnv(const char *, T);
  friend bool TryParseLocked(const CommandLineFlag *, FlagValue *, const char *,
                             string *); // for New(), CopyFrom()

  const char *TypeName() const;

  bool Equal(const FlagValue &x) const;
  FlagValue *New() const; // creates a new one with default value
  void CopyFrom(const FlagValue &x);

  // Calls the given validate-fn on value_buffer_, and returns
  // whatever it returns.  But first casts validate_fn_proto to a
  // function that takes our value as an argument (eg void
  // (*validate_fn)(bool) for a bool flag).
  bool Validate(const char *flagname, ValidateFnProto validate_fn_proto) const;

  void *const value_buffer_; // points to the buffer holding our data
  const int8 type_;          // how to interpret value_
  const bool owns_value_;    // whether to free value on destruct

  FlagValue(const FlagValue &); // no copying!
  void operator=(const FlagValue &);
};

class CommandLineFlag {
public:
  // Note: we take over memory-ownership of current_val and default_val.
  CommandLineFlag(const char *name, const char *help, const char *filename,
                  FlagValue *current_val, FlagValue *default_val);
  ~CommandLineFlag();

  const char *name() const;
  const char *help() const;
  const char *filename() const;
  const char *CleanFileName() const; // nixes irrelevant prefix such as homedir
  string current_value() const;
  string default_value() const;
  const char *type_name() const;
  ValidateFnProto validate_function() const;
  const void *flag_ptr() const;
  ValueType Type() const;
  // If validate_fn_proto_ is non-NULL, calls it on value, returns result.
  bool Validate(const FlagValue &value) const;
  bool ValidateCurrent() const;
  bool Modified() const;

private:
  // for SetFlagLocked() and setting flags_by_ptr_
  friend class FlagRegistry;
  // friend class FlagSaverImpl; // for cloning the values
  // set validate_fn
  friend class Gflags;

  // This copies all the non-const members: modified, processed, defvalue, etc.
  void CopyFrom(const CommandLineFlag &src);
  void UpdateModifiedBit();

  const char *const name_; // Flag name
  const char *const help_; // Help message
  const char *const file_; // Which file did this come from?
  bool modified_;          // Set after default assignment?
  FlagValue *defvalue_;    // Default value for flag
  FlagValue *current_;     // Current value for flag
  // This is a casted, 'generic' version of validate_fn, which actually
  // takes a flag-value as an arg (void (*validate_fn)(bool), say).
  // When we pass this to current_->Validate(), it will cast it back to
  // the proper type.  This may be NULL to mean we have no validate_fn.
  ValidateFnProto validate_fn_proto_;

  CommandLineFlag(const CommandLineFlag &); // no copying!
  void operator=(const CommandLineFlag &);
};

class CommandLineFlagParser {
public:
  // The argument is the flag-registry to register the parsed flags in
  explicit CommandLineFlagParser(Gflags *enter, FlagRegistry *reg);
  ~CommandLineFlagParser();

  // Stage 1: Every time this is called, it reads all flags in argv.
  // However, it ignores all flags that have been successfully set
  // before.  Typically this is only called once, so this 'reparsing'
  // behavior isn't important.  It can be useful when trying to
  // reparse after loading a dll, though.
  uint32 ParseNewCommandLineFlags(int *argc, char ***argv, bool remove_flags);

  // Stage 2: print reporting info and exit, if requested.
  // In gflags_reporting.cc:HandleCommandLineHelpFlags().

  // Stage 3: validate all the commandline flags that have validators
  // registered and were not set/modified by ParseNewCommandLineFlags.
  void ValidateFlags(bool all);
  void ValidateUnmodifiedFlags();

  // Set a particular command line option.  "newval" is a string
  // describing the new value that the option has been set to.  If
  // option_name does not specify a valid option name, or value is not
  // a valid value for option_name, newval is empty.  Does recursive
  // processing for --flagfile and --fromenv.  Returns the new value
  // if everything went ok, or empty-string if not.  (Actually, the
  // return-string could hold many flag/value pairs due to --flagfile.)
  // NB: Must have called registry_->Lock() before calling this function.
  string ProcessSingleOptionLocked(CommandLineFlag *flag, const char *value,
                                   FlagSettingMode set_mode);

private:
  const Gflags *const enter_;
  FlagRegistry *const registry_;
  map<string, string> error_flags_; // map from name to error message
  // This could be a set<string>, but we reuse the map to minimize the .o size
  map<string, string> undefined_names_; // --[flag] name was not registered
};

class FlagRegistry {
public:
  FlagRegistry();
  ~FlagRegistry();

  static FlagRegistry *GlobalRegistry(); // returns a singleton registry
  static void DeleteGlobalRegistry();

  void Lock();
  void Unlock();

  // Store a flag in this registry.  Takes ownership of the given pointer.
  void RegisterFlag(CommandLineFlag *flag);

  // Returns the flag object for the specified name, or NULL if not found.
  CommandLineFlag *FindFlagLocked(const char *name);

  // Returns the flag object whose current-value is stored at flag_ptr.
  // That is, for whom current_->value_buffer_ == flag_ptr
  CommandLineFlag *FindFlagViaPtrLocked(const void *flag_ptr);

  // A fancier form of FindFlag that works correctly if name is of the
  // form flag=value.  In that case, we set key to point to flag, and
  // modify v to point to the value (if present), and return the flag
  // with the given name.  If the flag does not exist, returns NULL
  // and sets error_message.
  CommandLineFlag *SplitArgumentLocked(const char *argument, string *key,
                                       const char **v, string *error_message);

  // Set the value of a flag.  If the flag was successfully set to
  // value, set msg to indicate the new flag-value, and return true.
  // Otherwise, set msg to indicate the error, leave flag unchanged,
  // and return false.  msg can be NULL.
  bool SetFlagLocked(CommandLineFlag *flag, const char *value,
                     FlagSettingMode set_mode, string *msg);

private:
  // friend class FlagSaverImpl;         // reads all the flags in order
  //                                     // to copy them
  friend class CommandLineFlagParser; // for ValidateUnmodifiedFlags
  friend class Gflags;                // for GetAllFlags

  typedef map<const char *, CommandLineFlag *, StringCmp> FlagMap;
  typedef FlagMap::iterator FlagIterator;
  typedef FlagMap::const_iterator FlagConstIterator;
  FlagMap flags_;

  // The map from current-value pointer to flag, fo FindFlagViaPtrLocked().
  typedef map<const void *, CommandLineFlag *> FlagPtrMap;
  FlagPtrMap flags_by_ptr_;

  static FlagRegistry *global_registry_; // a singleton registry

  Mutex lock_;

  static void InitGlobalRegistry();

  // Disallow
  FlagRegistry(const FlagRegistry &);
  FlagRegistry &operator=(const FlagRegistry &);
};

class FlagRegistryLock {
public:
  explicit FlagRegistryLock(FlagRegistry *fr);
  ~FlagRegistryLock();

private:
  FlagRegistry *const fr_;
};

// ------------------------------------------------------------------------
// 全局管理
// ------------------------------------------------------------------------
class Gflags {
public:
  // void RegisterCommandLineFlag(const char *name, const char *help,
  //                              const char *filename, FlagValue *current,
  //                              FlagValue *defvalue) {
  //   if (help == NULL)
  //     help = "";
  //   // Importantly, flag_ will never be deleted, so storage is always good.
  //   CommandLineFlag *flag =
  //       new CommandLineFlag(name, help, filename, current, defvalue);
  //   FlagRegistry::GlobalRegistry()->RegisterFlag(flag); // default registry
  // }
  // FlagRegisterer感觉没有存在必要，直接用GflagsRegisterCommandLineFlag模板函数替换
  // 由于全局作用域不能调用函数Gflags.RegisterCommandLineFlag，DEFINE_***(type)废掉
  // 对于string类型可能会有bug(DEFINE_string
  // http://code.google.com/p/google-gflags/issues/detail?id=20)
  template <typename FlagType>
  static bool RegisterCommandLineFlag(const char *name, const char *help,
                                      const char *filename,
                                      FlagType *current_storage,
                                      FlagType *defvalue_storage) {
    if (help == NULL)
      help = "";

    FlagValue *const current = new FlagValue(current_storage, false);
    FlagValue *const defvalue = new FlagValue(defvalue_storage, false);
    // Importantly, flag_ will never be deleted, so storage is always good.
    CommandLineFlag *flag =
        new CommandLineFlag(name, help, filename, current, defvalue);
    if (!flag)
      return false;
    FlagRegistry::GlobalRegistry()->RegisterFlag(flag); // default registry
    return true;
  }

  // --------------------------------------------------------------------
  // RegisterFlagValidator()
  //    RegisterFlagValidator() is the function that clients use to
  //    'decorate' a flag with a validation function.  Once this is
  //    done, every time the flag is set (including when the flag
  //    is parsed from argv), the validator-function is called.
  //       These functions return true if the validator was added
  //    successfully, or false if not: the flag already has a validator,
  //    (only one allowed per flag), the 1st arg isn't a flag, etc.
  //       This function is not thread-safe.
  // --------------------------------------------------------------------
  template <typename FlagType>
  static bool RegisterFlagValidator(const void *flag_ptr,
                                    bool (*validate_fn)(const char *,
                                                        FlagType)) {
    // We want a lock around this routine, in case two threads try to
    // add a validator (hopefully the same one!) at once.  We could use
    // our own thread, but we need to loook at the registry anyway, so
    // we just steal that one.
    ValidateFnProto validate_fn_proto =
        reinterpret_cast<ValidateFnProto>(validate_fn);
    FlagRegistry *const registry = FlagRegistry::GlobalRegistry();
    FlagRegistryLock frl(registry);
    // First, find the flag whose current-flag storage is 'flag'.
    // This is the CommandLineFlag whose current_->value_buffer_ == flag
    CommandLineFlag *flag = registry->FindFlagViaPtrLocked(flag_ptr);
    if (!flag) {
      cout << "Ignoring RegisterValidateFunction() for flag pointer "
           << flag_ptr << ": no flag found at that address";
      return false;
    } else if (validate_fn_proto == flag->validate_function()) {
      return true; // ok to register the same function over and over again
    } else if (validate_fn_proto != NULL && flag->validate_function() != NULL) {
      cout << "Ignoring RegisterValidateFunction() for flag '" << flag->name()
           << "': validate-fn already registered";
      return false;
    } else {
      flag->validate_fn_proto_ = validate_fn_proto;
      return true;
    }
  }

  Gflags();

  void SetUsageMessage(const std::string &usage);
  const char *ProgramUsage();
  void SetVersionString(const std::string &version);
  const char *VersionString();
  uint32 ParseCommandLineFlags(int *argc, char ***argv, bool remove_flags);
  const vector<string> &GetArgvs() const;
  const char *GetArgv() const;
  const char *GetArgv0() const;
  uint32 GetArgvSum() const;
  const char *ProgramInvocationName() const;
  const char *ProgramInvocationShortName() const;

  bool GetCommandLineOption(const char *name, string *value);

  void ShutDownCommandLineFlags();

private:
  void SetArgv(int argc, const char **argv);
  uint32 ParseCommandLineFlagsInternal(int *argc, char ***argv,
                                       bool remove_flags, bool do_report);
  // set only once during program startup.
  string argv0;   // just the program name
  string cmdline; // the entire command-line
  vector<string> argvs;
  uint32 argv_sum;
  string program_usage;
  string version_string;
};

// Each command-line flag has two variables associated with it: one
// with the current value, and one with the default value.  However,
// we have a third variable, which is where value is assigned; it's a
// constant.  This guarantees that FLAG_##value is initialized at
// static initialization time (e.g. before program-start) rather than
// than global construction time (which is after program-start but
// before main), at least when 'value' is a compile-time constant.  We
// use a small trick for the "default value" variable, and call it
// FLAGS_no<name>.  This serves the second purpose of assuring a
// compile error if someone tries to define a flag named no<name>
// which is illegal (--foo and --nofoo both affect the "foo" flag).
#define DEFINE_VARIABLE(type, name, value, help)                               \
  namespace gflags {                                                           \
  using gflags::Gflags;                                                        \
  /* We always want to export defined variables, dll or no */                  \
  type FLAGS_##name = value;                                                   \
  static type FLAGS_no##name = value;                                          \
  static const bool name##_flag_registered = Gflags::RegisterCommandLineFlag(  \
      #name, help, __FILE__, &FLAGS_##name, &FLAGS_no##name);                  \
  }                                                                            \
  using gflags::FLAGS_##name

// Here are the actual DEFINE_*-macros. The respective DECLARE_*-macros
// are in a separate include, gflags_declare.h, for reducing
// the physical transitive size for DECLARE use.
#define DEFINE_bool(name, val, help) DEFINE_VARIABLE(bool, name, val, help)

#define DEFINE_int32(name, val, help)                                          \
  DEFINE_VARIABLE(gflags::int32, name, val, help)

#define DEFINE_uint32(name, val, help)                                         \
  DEFINE_VARIABLE(gflags::uint32, name, val, help)

#define DEFINE_int64(name, val, help)                                          \
  DEFINE_VARIABLE(gflags::int64, name, val, help)

#define DEFINE_uint64(name, val, help)                                         \
  DEFINE_VARIABLE(gflags::uint64, name, val, help)

#define DEFINE_double(name, val, help) DEFINE_VARIABLE(double, name, val, help)

// We need to define a var named FLAGS_no##name so people don't define
// --string and --nostring.  And we need a temporary place to put val
// so we don't have to evaluate it twice.  Two great needs that go
// great together!
// The weird 'using' + 'extern' inside the fLS namespace is to work around
// an unknown compiler bug/issue with the gcc 4.2.1 on SUSE 10.  See
//    http://code.google.com/p/google-gflags/issues/detail?id=20
#define DEFINE_string(name, val, help)                                         \
  namespace gflags {                                                           \
  using gflags::Gflags;                                                        \
  using gflags::clstring;                                                      \
  clstring FLAGS_##name = clstring(val);                                       \
  static clstring FLAGS_no##name = clstring(val);                              \
  static const bool name##_flag_registered = Gflags::RegisterCommandLineFlag(  \
      #name, help, __FILE__, &FLAGS_##name, &FLAGS_no##name);                  \
  }                                                                            \
  using gflags::FLAGS_##name

// Convenience macro for the registration of a flag validator
#define DEFINE_validator(name, validator)                                      \
  namespace gflags {                                                           \
  using gflags::Gflags;                                                        \
  static const bool name##_validator_registered =                              \
      Gflags::RegisterFlagValidator(&FLAGS_##name, validator);                 \
  } // namespace gflags

} // namespace gflags

#endif