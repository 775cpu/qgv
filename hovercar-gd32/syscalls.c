/* Minimal newlib syscall implementations for embedded use
 * Provides: _sbrk, _read, _close, _lseek, _fstat, _isatty, _kill, _getpid
 * Keeps _write in usart.c to avoid duplicate symbols.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include "usart.h"
#include <reent.h>

extern char _end; /* defined by the linker */

void* __malloc_sbrk_start = &_end;

/* _sbrk: increase program break (heap end) */
void* _sbrk(ptrdiff_t incr) {
    static char *heap_end = NULL;
    char *prev_heap_end;

    if (heap_end == NULL) {
        heap_end = &_end;
    }
    prev_heap_end = heap_end;
    heap_end += incr;
    return (void*)prev_heap_end;
}

/* _read: read from STDIN (0) — use USART input if available */
int _read(int file, char *ptr, int len) {
    (void)file;
    if (len <= 0) return 0;

    uint16_t got = USART_ReadData((uint8_t*)ptr, (uint16_t)len);
    return (int)got;
}

int _close(int file) {
    (void)file;
    errno = EBADF;
    return -1;
}

off_t _lseek(int file, off_t ptr, int dir) {
    (void)file; (void)ptr; (void)dir;
    errno = EBADF;
    return (off_t)-1;
}

int _fstat(int file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file) {
    (void)file;
    return 1;
}

int _kill(int pid, int sig) {
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void) {
    return 1;
}

/* Reentrant wrappers expected by newlib */
int _read_r(struct _reent *r, int file, char *ptr, int len) {
    (void)r;
    return _read(file, ptr, len);
}

int _close_r(struct _reent *r, int file) {
    (void)r;
    return _close(file);
}

off_t _lseek_r(struct _reent *r, int file, off_t ptr, int dir) {
    (void)r;
    return _lseek(file, ptr, dir);
}

int _fstat_r(struct _reent *r, int file, struct stat *st) {
    (void)r;
    return _fstat(file, st);
}

int _isatty_r(struct _reent *r, int file) {
    (void)r;
    return _isatty(file);
}

int _kill_r(struct _reent *r, int pid, int sig) {
    (void)r;
    return _kill(pid, sig);
}

int _getpid_r(struct _reent *r) {
    (void)r;
    return _getpid();
}

void* _sbrk_r(struct _reent *r, ptrdiff_t incr) {
    (void)r;
    return _sbrk(incr);
}
