
#ifndef REDIRECT_LOG_H
#define REDIRECT_LOG_H

#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

    extern void to_syslog(const char *tag, FILE **pfp);


#ifdef __cplusplus
}
#endif

#endif // REDIRECT_LOG_H
