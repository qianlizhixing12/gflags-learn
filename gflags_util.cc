#include "gflags.h"

using std::string;

void (*gflags::gflags_exitfunc)(int) = &exit; // from stdlib.h

void gflags::ReportError(DieWhenReporting should_die, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fflush(stderr); // should be unnecessary, but cygwin's rxvt buffers stderr
  if (should_die == DieWhenReporting::DIE)
    gflags_exitfunc(1);
}

void gflags::InternalStringPrintf(std::string *output, const char *format,
                                  va_list ap) {
  char space[128]; // try a small buffer and hope it fits

  // It's possible for methods that use a va_list to invalidate
  // the data in it upon use.  The fix is to make a copy
  // of the structure before using it and use that copy instead.
  va_list backup_ap;
  va_copy(backup_ap, ap);
  int bytes_written = vsnprintf(space, sizeof(space), format, backup_ap);
  va_end(backup_ap);

  if ((bytes_written >= 0) &&
      (static_cast<size_t>(bytes_written) < sizeof(space))) {
    output->append(space, bytes_written);
    return;
  }

  // Repeatedly increase buffer size until it fits.
  int length = sizeof(space);
  while (true) {
    if (bytes_written < 0) {
      // Older snprintf() behavior. :-(  Just try doubling the buffer size
      length *= 2;
    } else {
      // We need exactly "bytes_written+1" characters
      length = bytes_written + 1;
    }
    char *buf = new char[length];

    // Restore the va_list before we use it again
    va_copy(backup_ap, ap);
    bytes_written = vsnprintf(buf, length, format, backup_ap);
    va_end(backup_ap);

    if ((bytes_written >= 0) && (bytes_written < length)) {
      output->append(buf, bytes_written);
      delete[] buf;
      return;
    }
    delete[] buf;
  }
}

string gflags::StringPrintf(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  std::string output;
  InternalStringPrintf(&output, format, ap);
  va_end(ap);
  return output;
}

void gflags::StringAppendF(std::string *output, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  InternalStringPrintf(output, format, ap);
  va_end(ap);
}

bool gflags::TryParseLocked(const CommandLineFlag *flag, FlagValue *flag_value,
                            const char *value, string *msg) {
  // Use tenative_value, not flag_value, until we know value is valid.
  FlagValue *tentative_value = flag_value->New();
  if (!tentative_value->ParseFrom(value)) {
    if (msg) {
      StringAppendF(msg, "%sillegal value '%s' specified for %s flag '%s'\n",
                    kError, value, flag->type_name(), flag->name());
    }
    delete tentative_value;
    return false;
  } else if (!flag->Validate(*tentative_value)) {
    if (msg) {
      StringAppendF(msg,
                    "%sfailed validation of new value '%s' for flag '%s'\n",
                    kError, tentative_value->ToString().c_str(), flag->name());
    }
    delete tentative_value;
    return false;
  } else {
    flag_value->CopyFrom(*tentative_value);
    if (msg) {
      StringAppendF(msg, "%s set to %s\n", flag->name(),
                    flag_value->ToString().c_str());
    }
    delete tentative_value;
    return true;
  }
}
