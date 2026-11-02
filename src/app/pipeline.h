#ifndef IPC_PIPELINE_H
#define IPC_PIPELINE_H

#include <signal.h>

#include "app/config.h"

int pipeline_run(const AppConfig *config, const volatile sig_atomic_t *stop);

#endif
