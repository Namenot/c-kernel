#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <mqueue.h>
#include <sys/stat.h> // struct stat
#include <fcntl.h>    // for O_ constants
#include <dlfcn.h>    // for dlsym

#define USE_C11 (__STDC_VERSION__ >= 201112L)
#define USE_BOUNDS_CHECKING ((defined(__STDC_LIB_EXT1__)) && (__STDC_WANT_LIB_EXT1__ >= 1))
#define NEED_gets (!(USE_C11))
#define NEED_gets_s ((USE_C11) && USE_BOUNDS_CHECKING)
#define NEED_getdelim (defined(_GNU_SOURCE) || (defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 200809L)))
#define NEED_FN(fn) NEED_##fn

#define THIRD_ARG(a, b, c, ...) c
#define VA_OPT_AVAIL_I(...) THIRD_ARG(__VA_OPT__(, ), 1, 0, )
#define VA_OPT_AVAIL VA_OPT_AVAIL_I(?)
#if VA_OPT_AVAIL
#define PASS_ARGS_I(...) __VA_OPT__(, ) __VA_ARGS__
#else
#define PASS_ARGS_I(...) , ##__VA_ARGS__
#endif
#undef THIRD_ARG
#undef VA_OPT_AVAIL_I
#undef VA_OPT_AVAIL
#define PASS_ARGS(...) PASS_ARGS_I(__VA_ARGS__)

#define CKERROR(fmt, n, s, ...) \
    fprintf(stderr, "%s:%d in %s: [Error %d: %s] " fmt "\n", __FILE__, __LINE__, __func__, (n), ((s) ? (s) : "(none)")PASS_ARGS(__VA_ARGS__))

#if defined CKERNEL_WITH_DEBUG
#define CKDEBUG(fmt, ...) \
    fprintf(stdout, "[D] %s:%d in %s: " fmt "\n", __FILE__, __LINE__, __func__ PASS_ARGS(__VA_ARGS__))
#else
#define CKDEBUG(fmt, ...)
#endif

#define FP(name) name##_fp
#define ATTACH_FP(s, name)                                       \
    do                                                           \
    {                                                            \
        if ((s.name = dlsym(RTLD_NEXT, #name)) == NULL)          \
            CKERROR("failed to find symbol %s", 0, NULL, #name); \
        else                                                     \
        {                                                        \
            CKDEBUG("attached symbol %s", #name);                \
        }                                                        \
    } while (0)

static bool request_input = false;
static mqd_t stdin_mq = -1;

// pointers to real input functions
struct input_fp
{
    int (*fgetc)(FILE *);
    char *(*fgets)(char *, int, FILE *);
#if NEED_FN(gets_s)
    char *(*gets_s)(char *, rsize_t);
#endif
#if NEED_FN(gets)
    char *(*gets)(char *);
#endif
    int (*getchar)(void);
    int (*vfscanf)(FILE *, const char *, va_list);
#if USE_C11 && USE_BOUNDS_CHECKING
    int (*vfscanf_s)(FILE *, const char *, va_list);
#endif
#if NEED_getdelim
    ssize_t (*getdelim)(char **, size_t *, int, FILE *);
#endif
};

static struct input_fp ifp = {0};

static void ck_request_input(FILE *stream);

static void __attribute__((constructor)) ck_setup(void)
{
    struct stat stdin_stat;
    fstat(fileno(stdin), &stdin_stat);

    // if stdin is FIFO (i.e. from subprocess.PIPE), use message queue for input request
    CKDEBUG("%s", S_ISFIFO(stdin_stat.st_mode) ? "stdin is FIFO" : "stdin is not FIFO");
    request_input = S_ISFIFO(stdin_stat.st_mode) ? true : false;

    if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
    {
        CKERROR("failed to set stdout to unbuffered", errno, strerror(errno));
    }

    // point to actual input functions
    ATTACH_FP(ifp, fgetc);
    ATTACH_FP(ifp, fgets);
#if NEED_FN(gets_s)
    ATTACH_FP(ifp, gets_s);
#endif
#if NEED_FN(gets)
    ATTACH_FP(ifp, gets);
#endif
    ATTACH_FP(ifp, getchar);
    ATTACH_FP(ifp, vfscanf);
#if USE_C11 && USE_BOUNDS_CHECKING
    ATTACH_FP(ifp, vfscanf_s);
#endif
#if NEED_getdelim
    ATTACH_FP(ifp, getdelim);
#endif

    // attempt to connect to message queue
    const char *mq_name = NULL;
    if ((mq_name = getenv("CK_MQNAME")) == NULL)
        mq_name = "NONE";
    CKDEBUG("connect to queue %s", mq_name);
    if ((stdin_mq = mq_open(mq_name, O_WRONLY)) == -1)
    {
        CKDEBUG("failed to open message queue, not using input wrappers [Error %d: %s]", errno, strerror(errno));
        request_input = false;
    }
    return;
}

static void ck_request_input(FILE *stream)
{
    if (!(request_input && (stream == stdin)))
        return;
    static const char *msg = "READY";
    CKDEBUG("signal waiting for input");
    mq_send(stdin_mq, msg, strlen(msg), 0);
    CKDEBUG("ready for input");
}

int getc(FILE *stream)
{
    ck_request_input(stream);
    return ifp.fgetc(stream);
}

int fgetc(FILE *stream)
{
    ck_request_input(stream);
    return ifp.fgetc(stream);
}

#if NEED_FN(gets_s)
char *gets_s(char *str, rsize_t n)
{
    ck_request_input(stdin);
    return ifp.gets_s(str, n);
}
#endif

#if NEED_FN(gets)
char *gets(char *str)
{
    ck_request_input(stdin);
    return ifp.gets(str);
}
#endif

char *fgets(char *s, int size, FILE *stream)
{
    ck_request_input(stream);
    return ifp.fgets(s, size, stream);
}

int getchar(void)
{
    ck_request_input(stdin);
    return ifp.getchar();
}

int scanf(const char *format, ...)
{
    ck_request_input(stdin);
    va_list args;
    va_start(args, format);
    int result = ifp.vfscanf(stdin, format, args);
    va_end(args);
    return result;
}

int fscanf(FILE *stream, const char *format, ...)
{
    ck_request_input(stream);
    va_list args;
    va_start(args, format);
    int result = ifp.vfscanf(stream, format, args);
    va_end(args);
    return result;
}

int vscanf(const char *format, va_list args)
{
    ck_request_input(stdin);
    return ifp.vfscanf(stdin, format, args);
}

int vfscanf(FILE *stream, const char *format, va_list args)
{
    ck_request_input(stream);
    return ifp.vfscanf(stream, format, args);
}

#if USE_C11 && USE_BOUNDS_CHECKING
int scanf_s(const char *format, ...)
{
    ck_request_input(stdin);
    va_list args;
    va_start(args, format);
    int result = ifp.vfscanf_s(stdin, format, args);
    va_end(args);
    return result;
}

int fscanf_s(FILE *restrict stream, const char *restrict format, ...)
{
    ck_request_input(stream);
    va_list args;
    va_start(args, format);
    int result = ifp.vfscanf_s(stream, format, args);
    va_end(args);
    return result;
}

int vscanf_s(const char *format, va_list args)
{
    ck_request_input(stdin);
    return ifp.vfscanf_s(stdin, format, args);
}

int vfscanf_s(FILE *stream, const char *format, va_list args)
{
    ck_request_input(stream);
    return ifp.vfscanf_s(stream, format, args);
}
#endif

#if NEED_FN(getdelim)
ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    ck_request_input(stream);
    return ifp.getdelim(lineptr, n, '\n', stream);
}

ssize_t getdelim(char **lineptr, size_t *n, int delimiter, FILE *stream)
{
    ck_request_input(stream);
    return ifp.getdelim(lineptr, n, delimiter, stream);
}
#endif
