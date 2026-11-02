#include <signal.h>
#include <stdio.h>

#include "app/config.h"
#include "app/pipeline.h"

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

int main(int argc, char **argv)
{
    AppConfig config;
    int result = app_config_parse(&config, argc, argv);
    if (result != 0)
        return result > 0 ? 0 : 1;

    struct sigaction action = {0};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "Publishing H.264 to %s\n", config.rtsp_url);

    return pipeline_run(&config, &stop_requested) == 0 ? 0 : 1;
}
