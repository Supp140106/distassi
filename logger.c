#include "logger.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── private state ─────────────────────────────────────────────── */

static char g_component[32];
static FILE *g_log_fp = NULL;
static int g_to_stdout = 0;

static char g_ring[LOG_RING_SIZE][LOG_LINE_MAX];
static int g_ring_head = 0; /* next write slot */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── public API ────────────────────────────────────────────────── */

void logger_init(const char *component, const char *log_file, int to_stdout) {
  strncpy(g_component, component, sizeof(g_component) - 1);
  g_component[sizeof(g_component) - 1] = '\0';
  g_to_stdout = to_stdout;

  if (log_file) {
    g_log_fp = fopen(log_file, "a");
    if (!g_log_fp)
      perror("logger: fopen");
  }

  memset(g_ring, 0, sizeof(g_ring));
  g_ring_head = 0;
}

void logger_close(void) {
  pthread_mutex_lock(&g_lock);
  if (g_log_fp) {
    fclose(g_log_fp);
    g_log_fp = NULL;
  }
  pthread_mutex_unlock(&g_lock);
}

/* ── helpers ───────────────────────────────────────────────────── */

static const char *lvl_str(LogLevel l) {
  switch (l) {
  case LOG_INFO:
    return "INFO ";
  case LOG_WARN:
    return "WARN ";
  case LOG_ERROR:
    return "ERROR";
  default:
    return "?????";
  }
}

static void iso_now(char *buf, int len) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm;
  localtime_r(&ts.tv_sec, &tm);
  int ms = (int)(ts.tv_nsec / 1000000);
  snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
           tm.tm_min, tm.tm_sec, ms);
}

/* ── core ──────────────────────────────────────────────────────── */

void log_event(LogLevel level, const char *fmt, ...) {
  char ts[64];
  iso_now(ts, sizeof(ts));

  char body[LOG_LINE_MAX - 100];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  char line[LOG_LINE_MAX];
  snprintf(line, sizeof(line), "[%s] %s %-8s| %s", ts, lvl_str(level),
           g_component, body);

  pthread_mutex_lock(&g_lock);

  /* ring buffer */
  strncpy(g_ring[g_ring_head], line, LOG_LINE_MAX - 1);
  g_ring[g_ring_head][LOG_LINE_MAX - 1] = '\0';
  g_ring_head = (g_ring_head + 1) % LOG_RING_SIZE;

  /* persistent file */
  if (g_log_fp) {
    fprintf(g_log_fp, "%s\n", line);
    fflush(g_log_fp);
  }

  /* console (client only, typically) */
  if (g_to_stdout) {
    printf("%s\n", line);
    fflush(stdout);
  }

  pthread_mutex_unlock(&g_lock);
}

void logger_lock(void) { pthread_mutex_lock(&g_lock); }

void logger_unlock(void) { pthread_mutex_unlock(&g_lock); }

const char *logger_get_line(int i) {
  /* 0 → oldest visible, LOG_RING_SIZE-1 → newest */
  return g_ring[(g_ring_head + i) % LOG_RING_SIZE];
}
