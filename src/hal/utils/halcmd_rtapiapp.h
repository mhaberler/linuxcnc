#ifndef HALCMD_RTAPIAPP_H
#define HALCMD_RTAPIAPP_H

#ifdef __cplusplus
extern "C" {
#endif

    int rtapi_connect(int instance, const char *uri);
    int rtapi_loadrt(int instance, const char *modname, const char **args);
    int rtapi_unloadrt(int instance, const char *modname);
    int rtapi_shutdown(int instance);
    int rtapi_ping(int instance);


#ifdef __cplusplus
}
#endif

#endif // HALCMD_RTAPIAPP_H
