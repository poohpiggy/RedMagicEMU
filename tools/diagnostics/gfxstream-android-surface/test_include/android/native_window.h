#pragma once
struct ANativeWindow;
typedef struct ANativeWindow ANativeWindow;
#ifdef __cplusplus
extern "C" {
#endif
void ANativeWindow_acquire(ANativeWindow*);
void ANativeWindow_release(ANativeWindow*);
#ifdef __cplusplus
}
#endif
