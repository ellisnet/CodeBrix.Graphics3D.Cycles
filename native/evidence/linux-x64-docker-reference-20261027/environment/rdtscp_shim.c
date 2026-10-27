// Test-environment shim for the Docker Desktop Linux VM on an Intel Mac, which does not expose
// RDTSCP (the host CPU has it). Cycles' ccl::time_fast_tick (profiling timer only; never part of a
// pixel value) executes RDTSCP; this LD_PRELOAD library catches the resulting SIGILL, emulates the
// instruction with RDTSC (EDX:EAX = time stamp, ECX = 0 = TSC_AUX) and resumes after it. Any other
// illegal instruction is re-raised unchanged. Copyright (c) 2026 Jeremy Ellis and contributors.
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>
static void handler(int sig, siginfo_t *info, void *context){
  (void)info; ucontext_t *uc = context; const uint8_t *ip = (const uint8_t *)uc->uc_mcontext.gregs[REG_RIP];
  if(ip[0] == 0x0f && ip[1] == 0x01 && ip[2] == 0xf9){
    uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uc->uc_mcontext.gregs[REG_RAX] = lo; uc->uc_mcontext.gregs[REG_RDX] = hi; uc->uc_mcontext.gregs[REG_RCX] = 0;
    uc->uc_mcontext.gregs[REG_RIP] += 3; return;
  }
  signal(sig, SIG_DFL); raise(sig);
}
__attribute__((constructor)) static void install(void){
  struct sigaction sa = {0}; sa.sa_sigaction = handler; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigaction(SIGILL, &sa, 0);
}
