#include "amsm.h"

typedef struct {
  TCHAR *name;
  HANDLE process_handle;
  unsigned long pid;
  unsigned long deadline;
  FILETIME creation_time;
  kill_t k;
} hook_t;

const TCHAR *hook_event_strings[] = { AMSM_HOOK_EVENT_START, AMSM_HOOK_EVENT_STOP, AMSM_HOOK_EVENT_EXIT, AMSM_HOOK_EVENT_POWER, AMSM_HOOK_EVENT_ROTATE, NULL };
const TCHAR *hook_action_strings[] = { AMSM_HOOK_ACTION_PRE, AMSM_HOOK_ACTION_POST, AMSM_HOOK_ACTION_CHANGE, AMSM_HOOK_ACTION_RESUME, NULL };

static unsigned long WINAPI await_hook(void *arg) {
  hook_t *hook = (hook_t *) arg;
  if (! hook) return AMSM_HOOK_STATUS_ERROR;

  int ret = 0;
  if (WaitForSingleObject(hook->process_handle, hook->deadline) == WAIT_TIMEOUT) ret = AMSM_HOOK_STATUS_TIMEOUT;

  /* Tidy up hook process tree. */
  if (hook->name) hook->k.name = hook->name;
  else hook->k.name = _T("hook");
  hook->k.process_handle = hook->process_handle;
  hook->k.pid = hook->pid;
  hook->k.stop_method = ~0;
  hook->k.kill_console_delay = AMSM_KILL_CONSOLE_GRACE_PERIOD;
  hook->k.kill_window_delay = AMSM_KILL_WINDOW_GRACE_PERIOD;
  hook->k.kill_threads_delay = AMSM_KILL_THREADS_GRACE_PERIOD;
  hook->k.creation_time = hook->creation_time;
  GetSystemTimeAsFileTime(&hook->k.exit_time);
  kill_process_tree(&hook->k, hook->pid);

  if (ret) {
    CloseHandle(hook->process_handle);
    if (hook->name) HeapFree(GetProcessHeap(), 0, hook->name);
    HeapFree(GetProcessHeap(), 0, hook);
    return ret;
  }

  unsigned long exitcode;
  GetExitCodeProcess(hook->process_handle, &exitcode);
  CloseHandle(hook->process_handle);

  if (hook->name) HeapFree(GetProcessHeap(), 0, hook->name);
  HeapFree(GetProcessHeap(), 0, hook);

  if (exitcode == AMSM_HOOK_STATUS_ABORT) return AMSM_HOOK_STATUS_ABORT;
  if (exitcode) return AMSM_HOOK_STATUS_FAILED;

  return AMSM_HOOK_STATUS_SUCCESS;
}

static void set_hook_runtime(TCHAR *v, FILETIME *start, FILETIME *now) {
  if (start && now) {
    ULARGE_INTEGER s;
    s.LowPart = start->dwLowDateTime;
    s.HighPart = start->dwHighDateTime;
    if (s.QuadPart) {
      ULARGE_INTEGER t;
      t.LowPart = now->dwLowDateTime;
      t.HighPart = now->dwHighDateTime;
      if (t.QuadPart && t.QuadPart >= s.QuadPart) {
        t.QuadPart -= s.QuadPart;
        t.QuadPart /= 10000LL;
        TCHAR number[16];
        _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%llu"), t.QuadPart);
        SetEnvironmentVariable(v, number);
        return;
      }
    }
  }
  SetEnvironmentVariable(v, _T(""));
}

static void add_thread_handle(hook_thread_t *hook_threads, HANDLE thread_handle, TCHAR *name) {
  if (! hook_threads) return;

  int num_threads = hook_threads->num_threads + 1;
  hook_thread_data_t *data = (hook_thread_data_t *) HeapAlloc(GetProcessHeap(), 0, num_threads * sizeof(hook_thread_data_t));
  if (! data) {
    log_event(EVENTLOG_ERROR_TYPE, AMSM_EVENT_OUT_OF_MEMORY, _T("hook_thread_t"), _T("add_thread_handle()"), 0);
    return;
  }

  int i;
  for (i = 0; i < hook_threads->num_threads; i++) memmove(&data[i], &hook_threads->data[i], sizeof(data[i]));
  memmove(data[i].name, name, sizeof(data[i].name));
  data[i].thread_handle = thread_handle;

  if (hook_threads->data) HeapFree(GetProcessHeap(), 0, hook_threads->data);
  hook_threads->data = data;
  hook_threads->num_threads = num_threads;
}

bool valid_hook_name(const TCHAR *hook_event, const TCHAR *hook_action, bool quiet) {
  bool valid_event = false;
  bool valid_action = false;

  /* Exit/Post */
  if (str_equiv(hook_event, AMSM_HOOK_EVENT_EXIT)) {
    if (str_equiv(hook_action, AMSM_HOOK_ACTION_POST)) return true;
    if (quiet) return false;
    print_message(stderr, AMSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_ACTION_POST);
    return false;
  }

  /* Power/{Change,Resume} */
  if (str_equiv(hook_event, AMSM_HOOK_EVENT_POWER)) {
    if (str_equiv(hook_action, AMSM_HOOK_ACTION_CHANGE)) return true;
    if (str_equiv(hook_action, AMSM_HOOK_ACTION_RESUME)) return true;
    if (quiet) return false;
    print_message(stderr, AMSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_ACTION_CHANGE);
    _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_ACTION_RESUME);
    return false;
  }

  /* Rotate/{Pre,Post} */
  if (str_equiv(hook_event, AMSM_HOOK_EVENT_ROTATE)) {
    if (str_equiv(hook_action, AMSM_HOOK_ACTION_PRE)) return true;
    if (str_equiv(hook_action, AMSM_HOOK_ACTION_POST)) return true;
    if (quiet) return false;
    print_message(stderr, AMSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_ACTION_PRE);
    _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_ACTION_POST);
    return false;
  }

  /* Start/{Pre,Post} */
  if (str_equiv(hook_event, AMSM_HOOK_EVENT_START)) {
    if (str_equiv(hook_action, AMSM_HOOK_ACTION_PRE)) return true;
    if (str_equiv(hook_action, AMSM_HOOK_ACTION_POST)) return true;
    if (quiet) return false;
    print_message(stderr, AMSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_ACTION_PRE);
    _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_ACTION_POST);
    return false;
  }

  /* Stop/Pre */
  if (str_equiv(hook_event, AMSM_HOOK_EVENT_STOP)) {
    if (str_equiv(hook_action, AMSM_HOOK_ACTION_PRE)) return true;
    if (quiet) return false;
    print_message(stderr, AMSM_MESSAGE_INVALID_HOOK_ACTION, hook_event);
    _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_ACTION_PRE);
    return false;
  }

  if (quiet) return false;
  print_message(stderr, AMSM_MESSAGE_INVALID_HOOK_EVENT);
  _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_EVENT_EXIT);
  _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_EVENT_POWER);
  _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_EVENT_ROTATE);
  _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_EVENT_START);
  _ftprintf(stderr, _T("%s\n"), AMSM_HOOK_EVENT_STOP);
  return false;
}

void await_hook_threads(hook_thread_t *hook_threads, SERVICE_STATUS_HANDLE status_handle, SERVICE_STATUS *status, unsigned long deadline) {
  if (! hook_threads) return;
  if (! hook_threads->num_threads) return;

  int *retain = (int *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, hook_threads->num_threads * sizeof(int));
  if (! retain) {
    log_event(EVENTLOG_ERROR_TYPE, AMSM_EVENT_OUT_OF_MEMORY, _T("retain"), _T("await_hook_threads()"), 0);
    return;
  }

  /*
    We could use WaitForMultipleObjects() but await_single_object() can update
    the service status as well.
  */
  int num_threads = 0;
  int i;
  for (i = 0; i < hook_threads->num_threads; i++) {
    if (deadline) {
      if (await_single_handle(status_handle, status, hook_threads->data[i].thread_handle, hook_threads->data[i].name, _T(__FUNCTION__), deadline) != 1) {
        CloseHandle(hook_threads->data[i].thread_handle);
        continue;
      }
    }
    else if (WaitForSingleObject(hook_threads->data[i].thread_handle, 0) != WAIT_TIMEOUT) {
      CloseHandle(hook_threads->data[i].thread_handle);
      continue;
    }

    retain[num_threads++]= i;
  }

  if (num_threads) {
    hook_thread_data_t *data = (hook_thread_data_t *) HeapAlloc(GetProcessHeap(), 0, num_threads * sizeof(hook_thread_data_t));
    if (! data) {
    log_event(EVENTLOG_ERROR_TYPE, AMSM_EVENT_OUT_OF_MEMORY, _T("data"), _T("await_hook_threads()"), 0);
      HeapFree(GetProcessHeap(), 0, retain);
      return;
    }

    for (i = 0; i < num_threads; i++) memmove(&data[i], &hook_threads->data[retain[i]], sizeof(data[i]));

    HeapFree(GetProcessHeap(), 0, hook_threads->data);
    hook_threads->data = data;
    hook_threads->num_threads = num_threads;
  }
  else {
    HeapFree(GetProcessHeap(), 0, hook_threads->data);
    ZeroMemory(hook_threads, sizeof(*hook_threads));
  }

  HeapFree(GetProcessHeap(), 0, retain);
}

/*
   Returns:
   AMSM_HOOK_STATUS_SUCCESS  if the hook ran successfully.
   AMSM_HOOK_STATUS_NOTFOUND if no hook was found.
   AMSM_HOOK_STATUS_ABORT    if the hook failed and we should cancel service start.
   AMSM_HOOK_STATUS_ERROR    on error.
   AMSM_HOOK_STATUS_NOTRUN   if the hook didn't run.
   AMSM_HOOK_STATUS_TIMEOUT  if the hook timed out.
   AMSM_HOOK_STATUS_FAILED   if the hook failed.
*/
int nssm_hook(hook_thread_t *hook_threads, nssm_service_t *service, TCHAR *hook_event, TCHAR *hook_action, unsigned long *hook_control, unsigned long deadline, bool async) {
  int ret = 0;

  hook_t *hook = (hook_t *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(hook_t));
  if (! hook) {
    log_event(EVENTLOG_ERROR_TYPE, AMSM_EVENT_OUT_OF_MEMORY, _T("hook"), _T("nssm_hook()"), 0);
    return AMSM_HOOK_STATUS_ERROR;
  }

  FILETIME now;
  GetSystemTimeAsFileTime(&now);

  EnterCriticalSection(&service->hook_section);

  /* Set the environment. */
  set_service_environment(service);

  /* ABI version. */
  TCHAR number[16];
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), AMSM_HOOK_VERSION);
  SetEnvironmentVariable(AMSM_HOOK_ENV_VERSION, number);

  /* Event triggering this action. */
  SetEnvironmentVariable(AMSM_HOOK_ENV_EVENT, hook_event);

  /* Hook action. */
  SetEnvironmentVariable(AMSM_HOOK_ENV_ACTION, hook_action);

  /* Control triggering this action.  May be empty. */
  if (hook_control) SetEnvironmentVariable(AMSM_HOOK_ENV_TRIGGER, service_control_text(*hook_control));
  else SetEnvironmentVariable(AMSM_HOOK_ENV_TRIGGER, _T(""));

  /* Last control handled. */
  SetEnvironmentVariable(AMSM_HOOK_ENV_LAST_CONTROL, service_control_text(service->last_control));

  /* Path to AMSM, unquoted for the environment. */
  SetEnvironmentVariable(AMSM_HOOK_ENV_IMAGE_PATH, nssm_unquoted_imagepath());

  /* AMSM version. */
  SetEnvironmentVariable(AMSM_HOOK_ENV_AMSM_CONFIGURATION, AMSM_CONFIGURATION);
  SetEnvironmentVariable(AMSM_HOOK_ENV_AMSM_VERSION, AMSM_VERSION);
  SetEnvironmentVariable(AMSM_HOOK_ENV_BUILD_DATE, AMSM_DATE);

  /* AMSM PID. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), GetCurrentProcessId());
  SetEnvironmentVariable(AMSM_HOOK_ENV_PID, number);

  /* AMSM runtime. */
  set_hook_runtime(AMSM_HOOK_ENV_RUNTIME, &service->nssm_creation_time, &now);

  /* Application PID. */
  if (service->pid) {
    _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->pid);
    SetEnvironmentVariable(AMSM_HOOK_ENV_APPLICATION_PID, number);
    /* Application runtime. */
    set_hook_runtime(AMSM_HOOK_ENV_APPLICATION_RUNTIME, &service->creation_time, &now);
    /* Exit code. */
    SetEnvironmentVariable(AMSM_HOOK_ENV_EXITCODE, _T(""));
  }
  else {
    SetEnvironmentVariable(AMSM_HOOK_ENV_APPLICATION_PID, _T(""));
    if (str_equiv(hook_event, AMSM_HOOK_EVENT_START) && str_equiv(hook_action, AMSM_HOOK_ACTION_PRE)) {
      SetEnvironmentVariable(AMSM_HOOK_ENV_APPLICATION_RUNTIME, _T(""));
      SetEnvironmentVariable(AMSM_HOOK_ENV_EXITCODE, _T(""));
    }
    else {
      set_hook_runtime(AMSM_HOOK_ENV_APPLICATION_RUNTIME, &service->creation_time, &service->exit_time);
      /* Exit code. */
      _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->exitcode);
      SetEnvironmentVariable(AMSM_HOOK_ENV_EXITCODE, number);
    }
  }

  /* Deadline for this script. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), deadline);
  SetEnvironmentVariable(AMSM_HOOK_ENV_DEADLINE, number);

  /* Service name. */
  SetEnvironmentVariable(AMSM_HOOK_ENV_SERVICE_NAME, service->name);
  SetEnvironmentVariable(AMSM_HOOK_ENV_SERVICE_DISPLAYNAME, service->displayname);

  /* Times the service was asked to start. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->start_requested_count);
  SetEnvironmentVariable(AMSM_HOOK_ENV_START_REQUESTED_COUNT, number);

  /* Times the service actually did start. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->start_count);
  SetEnvironmentVariable(AMSM_HOOK_ENV_START_COUNT, number);

  /* Times the service exited. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->exit_count);
  SetEnvironmentVariable(AMSM_HOOK_ENV_EXIT_COUNT, number);

  /* Throttled count. */
  _sntprintf_s(number, _countof(number), _TRUNCATE, _T("%lu"), service->throttle);
  SetEnvironmentVariable(AMSM_HOOK_ENV_THROTTLE_COUNT, number);

  /* Command line. */
  TCHAR app[CMD_LENGTH];
  _sntprintf_s(app, _countof(app), _TRUNCATE, _T("\"%s\" %s"), service->exe, service->flags);
  SetEnvironmentVariable(AMSM_HOOK_ENV_COMMAND_LINE, app);

  TCHAR cmd[CMD_LENGTH];
  if (get_hook(service->name, hook_event, hook_action, cmd, sizeof(cmd))) {
    log_event(EVENTLOG_ERROR_TYPE, AMSM_EVENT_GET_HOOK_FAILED, hook_event, hook_action, service->name, 0);
    unset_service_environment(service);
    LeaveCriticalSection(&service->hook_section);
    HeapFree(GetProcessHeap(), 0, hook);
    return AMSM_HOOK_STATUS_ERROR;
  }

  /* No hook. */
  if (! _tcslen(cmd)) {
    unset_service_environment(service);
    LeaveCriticalSection(&service->hook_section);
    HeapFree(GetProcessHeap(), 0, hook);
    return AMSM_HOOK_STATUS_NOTFOUND;
  }

  /* Run the command. */
  STARTUPINFO si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));
  if (service->hook_share_output_handles) (void) use_output_handles(service, &si);
  bool inherit_handles = false;
  if (si.dwFlags & STARTF_USESTDHANDLES) inherit_handles = true;
  unsigned long flags = 0;
#ifdef UNICODE
  flags |= CREATE_UNICODE_ENVIRONMENT;
#endif
  ret = AMSM_HOOK_STATUS_NOTRUN;
  if (CreateProcess(0, cmd, 0, 0, inherit_handles, flags, 0, service->dir, &si, &pi)) {
    close_output_handles(&si);
    hook->name = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, HOOK_NAME_LENGTH * sizeof(TCHAR));
    if (hook->name) _sntprintf_s(hook->name, HOOK_NAME_LENGTH, _TRUNCATE, _T("%s (%s/%s)"), service->name, hook_event, hook_action);
    hook->process_handle = pi.hProcess;
    hook->pid = pi.dwProcessId;
    hook->deadline = deadline;
    if (get_process_creation_time(hook->process_handle, &hook->creation_time)) GetSystemTimeAsFileTime(&hook->creation_time);

    unsigned long tid;
    HANDLE thread_handle = CreateThread(NULL, 0, await_hook, (void *) hook, 0, &tid);
    if (thread_handle) {
      if (async) {
        ret = 0;
        await_hook_threads(hook_threads, service->status_handle, &service->status, 0);
        add_thread_handle(hook_threads, thread_handle, hook->name);
      }
      else {
        await_single_handle(service->status_handle, &service->status, thread_handle, hook->name, _T(__FUNCTION__), deadline + AMSM_SERVICE_STATUS_DEADLINE);
        unsigned long exitcode;
        GetExitCodeThread(thread_handle, &exitcode);
        ret = (int) exitcode;
        CloseHandle(thread_handle);
      }
    }
    else {
      log_event(EVENTLOG_ERROR_TYPE, AMSM_EVENT_CREATETHREAD_FAILED, error_string(GetLastError()), 0);
      await_hook(hook);
      if (hook->name) HeapFree(GetProcessHeap(), 0, hook->name);
      HeapFree(GetProcessHeap(), 0, hook);
    }
  }
  else {
    log_event(EVENTLOG_ERROR_TYPE, AMSM_EVENT_HOOK_CREATEPROCESS_FAILED, hook_event, hook_action, service->name, cmd, error_string(GetLastError()), 0);
    HeapFree(GetProcessHeap(), 0, hook);
    close_output_handles(&si);
  }

  /* Restore our environment. */
  unset_service_environment(service);

  LeaveCriticalSection(&service->hook_section);

  return ret;
}

int nssm_hook(hook_thread_t *hook_threads, nssm_service_t *service, TCHAR *hook_event, TCHAR *hook_action, unsigned long *hook_control, unsigned long deadline) {
  return nssm_hook(hook_threads, service, hook_event, hook_action, hook_control, deadline, true);
}

int nssm_hook(hook_thread_t *hook_threads, nssm_service_t *service, TCHAR *hook_event, TCHAR *hook_action, unsigned long *hook_control) {
  return nssm_hook(hook_threads, service, hook_event, hook_action, hook_control, AMSM_HOOK_DEADLINE);
}
