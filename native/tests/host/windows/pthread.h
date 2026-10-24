// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
//
// Windows only: the subset of POSIX threads and nanosleep that the C host
// consumers (native/tests/host/*.c) use, on Win32 threads, slim reader/writer
// locks, condition variables and synchronization barriers. targets.cmake puts
// this directory on the consumers' include path on Windows alone, so the
// consumer sources are the same on every platform.
#pragma once
#ifndef _WIN32
#error "native/tests/host/windows/pthread.h is for Windows builds only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#include <stdlib.h>
#include <time.h>

typedef struct { HANDLE handle; } pthread_t;
typedef struct { SRWLOCK lock; } pthread_mutex_t;
typedef struct { CONDITION_VARIABLE condition; } pthread_cond_t;
typedef struct { SYNCHRONIZATION_BARRIER barrier; } pthread_barrier_t;
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

typedef struct { void *(*start)(void *); void *argument; } cbc_thread_start;
static unsigned __stdcall cbc_thread_main(void *pointer) {
  cbc_thread_start start = *(cbc_thread_start *)pointer; free(pointer);
  start.start(start.argument); return 0;
}
static inline int pthread_create(pthread_t *thread, const void *attributes, void *(*start)(void *), void *argument) {
  (void)attributes;
  cbc_thread_start *pending = (cbc_thread_start *)malloc(sizeof(cbc_thread_start));
  if (!pending) return -1;
  pending->start = start; pending->argument = argument;
  thread->handle = (HANDLE)_beginthreadex(NULL, 0, cbc_thread_main, pending, 0, NULL);
  if (!thread->handle) { free(pending); return -1; }
  return 0;
}
static inline int pthread_join(pthread_t thread, void **result) {
  if (result) *result = NULL;
  if (WaitForSingleObject(thread.handle, INFINITE) != WAIT_OBJECT_0) return -1;
  CloseHandle(thread.handle); return 0;
}
static inline int pthread_mutex_init(pthread_mutex_t *mutex, const void *attributes) { (void)attributes; InitializeSRWLock(&mutex->lock); return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *mutex) { AcquireSRWLockExclusive(&mutex->lock); return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) { ReleaseSRWLockExclusive(&mutex->lock); return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) { (void)mutex; return 0; }
static inline int pthread_cond_init(pthread_cond_t *condition, const void *attributes) { (void)attributes; InitializeConditionVariable(&condition->condition); return 0; }
static inline int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex) {
  return SleepConditionVariableSRW(&condition->condition, &mutex->lock, INFINITE, 0) ? 0 : -1;
}
static inline int pthread_cond_broadcast(pthread_cond_t *condition) { WakeAllConditionVariable(&condition->condition); return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *condition) { (void)condition; return 0; }
static inline int pthread_barrier_init(pthread_barrier_t *barrier, const void *attributes, unsigned count) {
  (void)attributes; return InitializeSynchronizationBarrier(&barrier->barrier, (LONG)count, -1) ? 0 : -1;
}
static inline int pthread_barrier_wait(pthread_barrier_t *barrier) {
  return EnterSynchronizationBarrier(&barrier->barrier, 0) ? PTHREAD_BARRIER_SERIAL_THREAD : 0;
}
static inline int pthread_barrier_destroy(pthread_barrier_t *barrier) { DeleteSynchronizationBarrier(&barrier->barrier); return 0; }
static inline int nanosleep(const struct timespec *request, struct timespec *remaining) {
  (void)remaining;
  const long long ms = (long long)request->tv_sec * 1000 + (request->tv_nsec + 999999) / 1000000;
  Sleep((DWORD)(ms > 0 ? ms : 0)); return 0;
}
