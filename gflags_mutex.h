#ifndef GFLAGS_MUTEX_H_
#define GFLAGS_MUTEX_H_

#include <stdlib.h>
#include <pthread.h>

namespace gflags {

typedef pthread_rwlock_t MutexType;

#define SAFE_PTHREAD(fncall)                                                   \
  do { /* run fncall if is_safe_ is true */                                    \
    if (is_safe_ && fncall(&mutex_) != 0)                                      \
      abort();                                                                 \
  } while (0)

class Mutex {
public:
  // This is used for the single-arg constructor
  enum LinkerInitialized { LINKER_INITIALIZED };

  // Create a Mutex that is not held by anybody.  This constructor is
  // typically used for Mutexes allocated on the heap or the stack.
  inline Mutex() : destroy_(true) {
    SetIsSafe();
    if (is_safe_ && pthread_rwlock_init(&mutex_, NULL) != 0)
      abort();
  }

  // This constructor should be used for global, static Mutex objects.
  // It inhibits work being done by the destructor, which makes it
  // safer for code that tries to acqiure this mutex in their global
  // destructor.
  explicit inline Mutex(LinkerInitialized) : destroy_(false) {
    SetIsSafe();
    if (is_safe_ && pthread_rwlock_init(&mutex_, NULL) != 0)
      abort();
  }

  // Destructor
  inline ~Mutex() {
    if (destroy_)
      SAFE_PTHREAD(pthread_rwlock_destroy);
  }

  // Block if needed until free then acquire exclusively
  inline void Lock() { SAFE_PTHREAD(pthread_rwlock_wrlock); }

  // Release a lock acquired via Lock()
  inline void Unlock() { SAFE_PTHREAD(pthread_rwlock_unlock); }

  // Block until free or shared then acquire a share
  inline void ReaderLock() { SAFE_PTHREAD(pthread_rwlock_rdlock); }

  // Release a read share of this Mutex
  inline void ReaderUnlock() { SAFE_PTHREAD(pthread_rwlock_unlock); }

  // Acquire an exclusive lock
  inline void WriterLock() { Lock(); }

  // Release a lock from WriterLock()
  inline void WriterUnlock() { Unlock(); }

private:
  pthread_rwlock_t mutex_;
  // We want to make sure that the compiler sets is_safe_ to true only
  // when we tell it to, and never makes assumptions is_safe_ is
  // always true.  volatile is the most reliable way to do that.
  volatile bool is_safe_;
  // This indicates which constructor was called.
  bool destroy_;

  inline void SetIsSafe() { is_safe_ = true; }

  // Catch the error of writing Mutex when intending MutexLock.
  explicit Mutex(Mutex * /*ignored*/) {}
  // Disallow "evil" constructors
  Mutex(const Mutex &);
  void operator=(const Mutex &);
};

#undef SAFE_PTHREAD

// MutexLock(mu) acquires mu when constructed and releases it when destroyed.
class MutexLock {
public:
  explicit MutexLock(Mutex *mu) : mu_(mu) {
    //
    mu_->Lock();
  }
  ~MutexLock() { mu_->Unlock(); }

private:
  Mutex *const mu_;
  // Disallow "evil" constructors
  MutexLock(const MutexLock &);
  void operator=(const MutexLock &);
};

// ReaderMutexLock and WriterMutexLock do the same, for rwlocks
class ReaderMutexLock {
public:
  explicit ReaderMutexLock(Mutex *mu) : mu_(mu) { mu_->ReaderLock(); }
  ~ReaderMutexLock() { mu_->ReaderUnlock(); }

private:
  Mutex *const mu_;
  // Disallow "evil" constructors
  ReaderMutexLock(const ReaderMutexLock &);
  void operator=(const ReaderMutexLock &);
};

class WriterMutexLock {
public:
  explicit WriterMutexLock(Mutex *mu) : mu_(mu) { mu_->WriterLock(); }
  ~WriterMutexLock() { mu_->WriterUnlock(); }

private:
  Mutex *const mu_;
  // Disallow "evil" constructors
  WriterMutexLock(const WriterMutexLock &);
  void operator=(const WriterMutexLock &);
};

#define COMPILE_ASSERT(msg) !##msg

// Catch bug where variable name is omitted, e.g. MutexLock (&mu);
#define MutexLock(x) COMPILE_ASSERT(mutex_lock_decl_missing_var_name)
#define ReaderMutexLock(x) COMPILE_ASSERT(rmutex_lock_decl_missing_var_name)
#define WriterMutexLock(x) COMPILE_ASSERT(wmutex_lock_decl_missing_var_name)

} // namespace gflags

#endif