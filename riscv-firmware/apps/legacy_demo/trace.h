#ifndef TRACE_H
#define TRACE_H

#ifdef __cplusplus
extern "C" {
#endif

void trace_init(void);
void trace_puts(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* TRACE_H */
