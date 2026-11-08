#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#include "service_manager.h"

extern int camera_service(void *arg);
extern int web_service(void *arg);
extern int sensor_service(void *arg);

const struct sockaddr_un camera_state_addr;
const struct sockaddr_un sensor_state_addr;
const struct sockaddr_un web_state_addr;

static volatile sig_atomic_t exit_requested = 0;

static void sigterm_handler (int sig) {
    (void)sig;
    exit_requested = 1;
}

int main(int argc, char **argv) {
    /* 注册 SIGTERM 处理 */
    struct sigaction sa = {0};
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);

    struct service_struct services[] = {
        {
            .name = "camera_service",
            .addr_un = camera_state_addr,
            .service_entry = camera_service,
            .state = SERVICE_STOPPED,
            .deps = {0}
        },
        {
            .name = "sensor_service",
            .addr_un = sensor_state_addr,
            .service_entry = sensor_service,
            .state = SERVICE_STOPPED,
            .deps = {0}
        },
        {
            .name = "web_service",
            .addr_un = web_state_addr,
            .service_entry = web_service,
            .state = SERVICE_STOPPED,
            .deps = {0}
        }
    };

    services[2].deps.services[0] = &services[0];
    services[2].deps.services[1] = &services[1];
    services[2].deps.count = 2;

    int i;
    for (i = 0; i < sizeof(services) / sizeof(services[0]); ++i) {
        register_service(&services[i]);
    }

    start_services();

    while(!exit_requested);

    stop_services();

    return 0;
}