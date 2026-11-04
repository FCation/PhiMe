// Minimal freestanding declarations so the mod builds without any platform SDK.
// Works for iOS (arm64 dylib) and Android (arm64 .so via NDK).
#pragma once

typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef int int32_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
typedef uint16_t char16_t_;

extern "C" {
void* dlopen(const char* path, int mode);
void* dlsym(void* handle, const char* symbol);
int pthread_create(void** thread, const void* attr, void* (*fn)(void*), void* arg);
int pthread_detach(void* thread);
int usleep(unsigned int usec);
void* malloc(size_t n);
void* calloc(size_t n, size_t sz);
void free(void* p);
void* memcpy(void* d, const void* s, size_t n);
void* memset(void* d, int c, size_t n);
int strcmp(const char* a, const char* b);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
char* getenv(const char* name);
int vsnprintf(char* buf, size_t n, const char* fmt, __builtin_va_list ap);
int snprintf(char* buf, size_t n, const char* fmt, ...);
int open(const char* path, int flags, ...);
ssize_t write(int fd, const void* buf, size_t n);
ssize_t read(int fd, void* buf, size_t n);
ssize_t pread(int fd, void* buf, size_t n, long offset);  // 64-bit off_t on arm64 iOS / Android
long lseek(int fd, long offset, int whence);
int mkdir(const char* path, unsigned int mode);
int close(int fd);
int rename(const char* from, const char* to);
long time(long* t);
#if defined(__APPLE__)
void syslog(int pri, const char* fmt, ...);
#else
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#endif
}

#if defined(__APPLE__)
#define PM_RTLD_DEFAULT ((void*)-2)
#define PM_RTLD_NOLOAD 0x10
#define PM_O_FLAGS (0x0001 | 0x0200 | 0x0008)  // O_WRONLY|O_CREAT|O_APPEND
#define PM_O_REPLACE (0x0001 | 0x0200 | 0x0400)  // O_WRONLY|O_CREAT|O_TRUNC
#else
#define PM_RTLD_DEFAULT ((void*)0)
#define PM_RTLD_NOLOAD 0x4
#define PM_O_FLAGS (0x0001 | 0x0040 | 0x0400)
#define PM_O_REPLACE (0x0001 | 0x0040 | 0x0200)
#endif
#define PM_RTLD_NOW 0x2

void pm_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) pm_log(__VA_ARGS__)
