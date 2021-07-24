#include "gflags.h"

using gflags::clstring;
using gflags::FlagValue;
using gflags::int32;
using gflags::int64;
using gflags::uint32;
using gflags::uint64;
using gflags::ValidateFnProto;
using gflags::ValueType;
using std::string;

/***********************FlagValue***********************/

template <typename FlagType> struct FlagValueTraits;

// Map the given C++ type to a value of the ValueType enum at compile time.
#define DEFINE_FLAG_TRAITS(type, value)                                        \
  template <> struct FlagValueTraits<type> {                                   \
    static const ValueType kValueType = value;                                 \
  }

DEFINE_FLAG_TRAITS(bool, ValueType::FV_BOOL);
DEFINE_FLAG_TRAITS(int32, ValueType::FV_INT32);
DEFINE_FLAG_TRAITS(uint32, ValueType::FV_UINT32);
DEFINE_FLAG_TRAITS(int64, ValueType::FV_INT64);
DEFINE_FLAG_TRAITS(uint64, ValueType::FV_UINT64);
DEFINE_FLAG_TRAITS(double, ValueType::FV_DOUBLE);
DEFINE_FLAG_TRAITS(std::string, ValueType::FV_STRING);

#undef DEFINE_FLAG_TRAITS

#define strto64 strtoll
#define strtou64 strtoull

#define VALUE_AS(type) *reinterpret_cast<type *>(value_buffer_)
#define OTHER_VALUE_AS(fv, type) *reinterpret_cast<type *>(fv.value_buffer_)
#define SET_VALUE_AS(type, value) VALUE_AS(type) = (value)

// --------------------------------------------------------------------
// FlagValue
//    This represent the value a single flag might have.  The major
//    functionality is to convert from a string to an object of a
//    given type, and back.  Thread-compatible.
// --------------------------------------------------------------------

template <typename FlagType>
FlagValue::FlagValue(FlagType *valbuf, bool transfer_ownership_of_value)
    : value_buffer_(valbuf), type_(FlagValueTraits<FlagType>::kValueType),
      owns_value_(transfer_ownership_of_value) {}

FlagValue::~FlagValue() {
  if (!owns_value_) {
    return;
  }
  switch (type_) {
  case FV_BOOL:
    delete reinterpret_cast<bool *>(value_buffer_);
    break;
  case FV_INT32:
    delete reinterpret_cast<int32 *>(value_buffer_);
    break;
  case FV_UINT32:
    delete reinterpret_cast<uint32 *>(value_buffer_);
    break;
  case FV_INT64:
    delete reinterpret_cast<int64 *>(value_buffer_);
    break;
  case FV_UINT64:
    delete reinterpret_cast<uint64 *>(value_buffer_);
    break;
  case FV_DOUBLE:
    delete reinterpret_cast<double *>(value_buffer_);
    break;
  case FV_STRING:
    delete reinterpret_cast<string *>(value_buffer_);
    break;
  }
}

bool FlagValue::ParseFrom(const char *value) {
  if (type_ == FV_BOOL) {
    const char *kTrue[] = {"1", "t", "true", "y", "yes"};
    const char *kFalse[] = {"0", "f", "false", "n", "no"};
    for (size_t i = 0; i < sizeof(kTrue) / sizeof(*kTrue); ++i) {
      if (strcasecmp(value, kTrue[i]) == 0) {
        SET_VALUE_AS(bool, true);
        return true;
      } else if (strcasecmp(value, kFalse[i]) == 0) {
        SET_VALUE_AS(bool, false);
        return true;
      }
    }
    return false; // didn't match a legal input

  } else if (type_ == FV_STRING) {
    SET_VALUE_AS(string, value);
    return true;
  }

  // OK, it's likely to be numeric, and we'll be using a strtoXXX method.
  if (value[0] == '\0') // empty-string is only allowed for string type.
    return false;
  char *end;
  // Leading 0x puts us in base 16.  But leading 0 does not put us in base 8!
  // It caused too many bugs when we had that behavior.
  int base = 10; // by default
  if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
    base = 16;
  errno = 0;

  switch (type_) {
  case FV_INT32: {
    const int64 r = strto64(value, &end, base);
    if (errno || end != value + strlen(value))
      return false;                 // bad parse
    if (static_cast<int32>(r) != r) // worked, but number out of range
      return false;
    SET_VALUE_AS(int32, static_cast<int32>(r));
    return true;
  }
  case FV_UINT32: {
    while (*value == ' ')
      value++;
    if (*value == '-')
      return false; // negative number
    const uint64 r = strtou64(value, &end, base);
    if (errno || end != value + strlen(value))
      return false;                  // bad parse
    if (static_cast<uint32>(r) != r) // worked, but number out of range
      return false;
    SET_VALUE_AS(uint32, static_cast<uint32>(r));
    return true;
  }
  case FV_INT64: {
    const int64 r = strto64(value, &end, base);
    if (errno || end != value + strlen(value))
      return false; // bad parse
    SET_VALUE_AS(int64, r);
    return true;
  }
  case FV_UINT64: {
    while (*value == ' ')
      value++;
    if (*value == '-')
      return false; // negative number
    const uint64 r = strtou64(value, &end, base);
    if (errno || end != value + strlen(value))
      return false; // bad parse
    SET_VALUE_AS(uint64, r);
    return true;
  }
  case FV_DOUBLE: {
    const double r = strtod(value, &end);
    if (errno || end != value + strlen(value))
      return false; // bad parse
    SET_VALUE_AS(double, r);
    return true;
  }
  default: {
    assert(false); // unknown type
    return false;
  }
  }
}

string FlagValue::ToString() const {
  char intbuf[64]; // enough to hold even the biggest number
  switch (type_) {
  case FV_BOOL:
    return VALUE_AS(bool) ? "true" : "false";
  case FV_INT32:
    snprintf(intbuf, sizeof(intbuf), "%" PRId32, VALUE_AS(int32));
    return intbuf;
  case FV_UINT32:
    snprintf(intbuf, sizeof(intbuf), "%" PRIu32, VALUE_AS(uint32));
    return intbuf;
  case FV_INT64:
    snprintf(intbuf, sizeof(intbuf), "%" PRId64, VALUE_AS(int64));
    return intbuf;
  case FV_UINT64:
    snprintf(intbuf, sizeof(intbuf), "%" PRIu64, VALUE_AS(uint64));
    return intbuf;
  case FV_DOUBLE:
    snprintf(intbuf, sizeof(intbuf), "%.17g", VALUE_AS(double));
    return intbuf;
  case FV_STRING:
    return VALUE_AS(string);
  default:
    assert(false);
    return ""; // unknown type
  }
}

bool FlagValue::Validate(const char *flagname,
                         ValidateFnProto validate_fn_proto) const {
  switch (type_) {
  case FV_BOOL:
    return reinterpret_cast<bool (*)(const char *, bool)>(validate_fn_proto)(
        flagname, VALUE_AS(bool));
  case FV_INT32:
    return reinterpret_cast<bool (*)(const char *, int32)>(validate_fn_proto)(
        flagname, VALUE_AS(int32));
  case FV_UINT32:
    return reinterpret_cast<bool (*)(const char *, uint32)>(validate_fn_proto)(
        flagname, VALUE_AS(uint32));
  case FV_INT64:
    return reinterpret_cast<bool (*)(const char *, int64)>(validate_fn_proto)(
        flagname, VALUE_AS(int64));
  case FV_UINT64:
    return reinterpret_cast<bool (*)(const char *, uint64)>(validate_fn_proto)(
        flagname, VALUE_AS(uint64));
  case FV_DOUBLE:
    return reinterpret_cast<bool (*)(const char *, double)>(validate_fn_proto)(
        flagname, VALUE_AS(double));
  case FV_STRING:
    return reinterpret_cast<bool (*)(const char *, const string &)>(
        validate_fn_proto)(flagname, VALUE_AS(string));
  default:
    assert(false); // unknown type
    return false;
  }
}

const char *FlagValue::TypeName() const {
  static const char types[] = "bool\0xx"
                              "int32\0x"
                              "uint32\0"
                              "int64\0x"
                              "uint64\0"
                              "double\0"
                              "string";
  if (type_ > FV_MAX_INDEX) {
    assert(false);
    return "";
  }
  // Directly indexing the strings in the 'types' string, each of them is 7
  // bytes long.
  return &types[type_ * 7];
}

bool FlagValue::Equal(const FlagValue &x) const {
  if (type_ != x.type_)
    return false;
  switch (type_) {
  case FV_BOOL:
    return VALUE_AS(bool) == OTHER_VALUE_AS(x, bool);
  case FV_INT32:
    return VALUE_AS(int32) == OTHER_VALUE_AS(x, int32);
  case FV_UINT32:
    return VALUE_AS(uint32) == OTHER_VALUE_AS(x, uint32);
  case FV_INT64:
    return VALUE_AS(int64) == OTHER_VALUE_AS(x, int64);
  case FV_UINT64:
    return VALUE_AS(uint64) == OTHER_VALUE_AS(x, uint64);
  case FV_DOUBLE:
    return VALUE_AS(double) == OTHER_VALUE_AS(x, double);
  case FV_STRING:
    return VALUE_AS(string) == OTHER_VALUE_AS(x, string);
  default:
    assert(false);
    return false; // unknown type
  }
}

FlagValue *FlagValue::New() const {
  switch (type_) {
  case FV_BOOL:
    return new FlagValue(new bool(false), true);
  case FV_INT32:
    return new FlagValue(new int32(0), true);
  case FV_UINT32:
    return new FlagValue(new uint32(0), true);
  case FV_INT64:
    return new FlagValue(new int64(0), true);
  case FV_UINT64:
    return new FlagValue(new uint64(0), true);
  case FV_DOUBLE:
    return new FlagValue(new double(0.0), true);
  case FV_STRING:
    return new FlagValue(new string, true);
  default:
    assert(false);
    return NULL; // unknown type
  }
}

void FlagValue::CopyFrom(const FlagValue &x) {
  assert(type_ == x.type_);
  switch (type_) {
  case FV_BOOL:
    SET_VALUE_AS(bool, OTHER_VALUE_AS(x, bool));
    break;
  case FV_INT32:
    SET_VALUE_AS(int32, OTHER_VALUE_AS(x, int32));
    break;
  case FV_UINT32:
    SET_VALUE_AS(uint32, OTHER_VALUE_AS(x, uint32));
    break;
  case FV_INT64:
    SET_VALUE_AS(int64, OTHER_VALUE_AS(x, int64));
    break;
  case FV_UINT64:
    SET_VALUE_AS(uint64, OTHER_VALUE_AS(x, uint64));
    break;
  case FV_DOUBLE:
    SET_VALUE_AS(double, OTHER_VALUE_AS(x, double));
    break;
  case FV_STRING:
    SET_VALUE_AS(string, OTHER_VALUE_AS(x, string));
    break;
  default:
    assert(false); // unknown type
  }
}
