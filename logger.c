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

/* Color codes */
#define CLR_RESET  "\033[0m"
#define CLR_INFO   "\033[32m" // Green
#define CLR_WARN   "\033[33m" // Yellow
#define CLR_ERROR  "\033[31m" // Red
#define CLR_TAMP   "\033[90m" // Gray
#define CLR_COMP   "\033[36m" // Cyan
#define CLR_SEP    "\033[90m" // Gray for separator

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

static const char *lvl_str(LogLevel l, int color) {
  if (color) {
    switch (l) {
    case LOG_INFO:  return CLR_INFO  "INFO " CLR_RESET;
    case LOG_WARN:  return CLR_WARN  "WARN " CLR_RESET;
    case LOG_ERROR: return CLR_ERROR "ERROR" CLR_RESET;
    default:        return "?????";
    }
  } else {
    switch (l) {
    case LOG_INFO:  return "INFO ";
    case LOG_WARN:  return "WARN ";
    case LOG_ERROR: return "ERROR";
    default:        return "?????";
    }
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

  char body[LOG_LINE_MAX - 200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  /* ── Build the output formats ───────────────────────────────── */

  // 1) File line: clean, readable, no ANSI codes
  char line_file[LOG_LINE_MAX];
  snprintf(line_file, sizeof(line_file),
           "  %-23s   %-5s   %-8s   %s",
           ts, lvl_str(level, 0), g_component, body);

  // 2) Ring buffer line: same clean format for TUI dashboard
  //    (stored without color so dashboards can add their own styling)
  char line_ring[LOG_LINE_MAX];
  snprintf(line_ring, sizeof(line_ring),
           "  %-23s   %-5s   %-8s   %s",
           ts, lvl_str(level, 0), g_component, body);

  // 3) Colorized line: for stdout / console (matches file format but with color)
  char line_color[LOG_LINE_MAX];
  snprintf(line_color, sizeof(line_color),
           "  %s%-23s%s   %s   %s%-8s%s   %s",
           CLR_TAMP, ts, CLR_RESET, lvl_str(level, 1),
           CLR_COMP, g_component, CLR_RESET, body);

  // Separator line
  static const char *sep_plain =
      "──────────────────────────────────────────────────"
      "──────────────────────────────";

  pthread_mutex_lock(&g_lock);

  /* ring buffer — clean column-aligned text for TUI dashboard */
  strncpy(g_ring[g_ring_head], line_ring, LOG_LINE_MAX - 1);
  g_ring[g_ring_head][LOG_LINE_MAX - 1] = '\0';
  g_ring_head = (g_ring_head + 1) % LOG_RING_SIZE;

  /* persistent file — clean plain text, NO ANSI codes */
  if (g_log_fp) {
    fprintf(g_log_fp, "%s\n", sep_plain);
    fprintf(g_log_fp, "%s\n", line_file);
    fflush(g_log_fp);
  }

  /* console — colorized, file-style format */
  if (g_to_stdout) {
    printf("%s%s%s\n", CLR_SEP, sep_plain, CLR_RESET);
    printf("%s\n", line_color);
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