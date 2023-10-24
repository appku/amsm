#ifndef HOOK_H
#define HOOK_H

#define AMSM_HOOK_EVENT_START _T("Start")
#define AMSM_HOOK_EVENT_STOP _T("Stop")
#define AMSM_HOOK_EVENT_EXIT _T("Exit")
#define AMSM_HOOK_EVENT_POWER _T("Power")
#define AMSM_HOOK_EVENT_ROTATE _T("Rotate")

#define AMSM_HOOK_ACTION_PRE _T("Pre")
#define AMSM_HOOK_ACTION_POST _T("Post")
#define AMSM_HOOK_ACTION_CHANGE _T("Change")
#define AMSM_HOOK_ACTION_RESUME _T("Resume")

/* Hook name will be "<service> (<event>/<action>)" */
#define HOOK_NAME_LENGTH SERVICE_NAME_LENGTH * 2

#define AMSM_HOOK_VERSION 1

/* Hook ran successfully. */
#define AMSM_HOOK_STATUS_SUCCESS 0
/* No hook configured. */
#define AMSM_HOOK_STATUS_NOTFOUND 1
/* Hook requested abort. */
#define AMSM_HOOK_STATUS_ABORT 99
/* Internal error launching hook. */
#define AMSM_HOOK_STATUS_ERROR 100
/* Hook was not run. */
#define AMSM_HOOK_STATUS_NOTRUN 101
/* Hook timed out. */
#define AMSM_HOOK_STATUS_TIMEOUT 102
/* Hook returned non-zero. */
#define AMSM_HOOK_STATUS_FAILED 111

/* Version 1. */
#define AMSM_HOOK_ENV_VERSION _T("AMSM_HOOK_VERSION")
#define AMSM_HOOK_ENV_IMAGE_PATH _T("AMSM_EXE")
#define AMSM_HOOK_ENV_AMSM_CONFIGURATION _T("AMSM_CONFIGURATION")
#define AMSM_HOOK_ENV_AMSM_VERSION _T("AMSM_VERSION")
#define AMSM_HOOK_ENV_BUILD_DATE _T("AMSM_BUILD_DATE")
#define AMSM_HOOK_ENV_PID _T("AMSM_PID")
#define AMSM_HOOK_ENV_DEADLINE _T("AMSM_DEADLINE")
#define AMSM_HOOK_ENV_SERVICE_NAME _T("AMSM_SERVICE_NAME")
#define AMSM_HOOK_ENV_SERVICE_DISPLAYNAME _T("AMSM_SERVICE_DISPLAYNAME")
#define AMSM_HOOK_ENV_COMMAND_LINE _T("AMSM_COMMAND_LINE")
#define AMSM_HOOK_ENV_APPLICATION_PID _T("AMSM_APPLICATION_PID")
#define AMSM_HOOK_ENV_EVENT _T("AMSM_EVENT")
#define AMSM_HOOK_ENV_ACTION _T("AMSM_ACTION")
#define AMSM_HOOK_ENV_TRIGGER _T("AMSM_TRIGGER")
#define AMSM_HOOK_ENV_LAST_CONTROL _T("AMSM_LAST_CONTROL")
#define AMSM_HOOK_ENV_START_REQUESTED_COUNT _T("AMSM_START_REQUESTED_COUNT")
#define AMSM_HOOK_ENV_START_COUNT _T("AMSM_START_COUNT")
#define AMSM_HOOK_ENV_THROTTLE_COUNT _T("AMSM_THROTTLE_COUNT")
#define AMSM_HOOK_ENV_EXIT_COUNT _T("AMSM_EXIT_COUNT")
#define AMSM_HOOK_ENV_EXITCODE _T("AMSM_EXITCODE")
#define AMSM_HOOK_ENV_RUNTIME _T("AMSM_RUNTIME")
#define AMSM_HOOK_ENV_APPLICATION_RUNTIME _T("AMSM_APPLICATION_RUNTIME")

typedef struct {
  TCHAR name[HOOK_NAME_LENGTH];
  HANDLE thread_handle;
} hook_thread_data_t;

typedef struct {
  hook_thread_data_t *data;
  int num_threads;
} hook_thread_t;

bool valid_hook_name(const TCHAR *, const TCHAR *, bool);
void await_hook_threads(hook_thread_t *, SERVICE_STATUS_HANDLE, SERVICE_STATUS *, unsigned long);
int nssm_hook(hook_thread_t *, nssm_service_t *, TCHAR *, TCHAR *, unsigned long *, unsigned long, bool);
int nssm_hook(hook_thread_t *, nssm_service_t *, TCHAR *, TCHAR *, unsigned long *, unsigned long);
int nssm_hook(hook_thread_t *, nssm_service_t *, TCHAR *, TCHAR *, unsigned long *);

#endif
