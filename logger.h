#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

#define LOG_RING_SIZE 10
#define LOG_LINE_MAX 512

typedef enum { LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

// Initialize the logger.
// component : tag string – "SERVER", "WORKER", or "CLIENT"
// log_file  : path for the persistent log (NULL to skip file logging)
// to_stdout : if non-zero, also write each line to stdout
void logger_init(const char *component, const char *log_file, int to_stdout);

// Flush and close the log file.
void logger_close(void);

// Log a structured event (printf-style key=value body).
// Example: log_event(LOG_INFO, "event=task_queued size=%d", sz);
void log_event(LogLevel level, const char *fmt, ...);

// Lock / unlock for safe iteration of the ring buffer (dashboard use).
void logger_lock(void);
void logger_unlock(void);

// Return the i-th line in the ring buffer.
// 0 = oldest visible line, LOG_RING_SIZE-1 = newest.
const char *logger_get_line(int i);

#endif // LOGGER_H