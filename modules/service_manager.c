#include "service_manager.h"
#include "ipc_utils.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

static size_t services_count = 0;
static struct service_struct *services[MAX_SERVICES_COUNT] = {0};
static int dependency_graph[MAX_SERVICES_COUNT][MAX_SERVICES_COUNT] = {{0}};

static int get_available_service_slot() {
    int i;
    for (i = 0; i < MAX_SERVICES_COUNT; ++i) {
        if (dependency_graph[i][i] == 0)
            return i;
    }

    return -1;
}

static inline int service_slot_available(size_t index) {
    if (index >= MAX_SERVICES_COUNT)
        return 0;

    return !dependency_graph[index][index];
}

static int named_service_index(const char *name) {
    size_t i;
    for (i = 0; i < MAX_SERVICES_COUNT; ++i) {
        if (service_slot_available(i))
            continue;

        if (!strcmp(services[i]->name, name))
            return i;
    }

    return -1;
}

int register_service(struct service_struct *serv) {
    if (serv == NULL || services_count >= MAX_SERVICES_COUNT)
        return -1;

    size_t i;
    // 检查是否所有依赖都已注册
    for (i = 0; i < serv->deps.count; ++i) {
        int dep = named_service_index(serv->deps.services[i]->name);
        if (dep < 0) {
            fprintf(stderr, "dependency %s is not registered\r\n", serv->deps.services[i]->name);
            return -1;
        }
    }

    int index = get_available_service_slot();
    if (index < 0) {
        fprintf(stderr, "registered services count hit the maximum\r\n");
        return -1;
    }

    serv->pid = -1;
    serv->server_fd = -1;
    serv->state_fd = -1;
    services[index] = serv;
    ++services_count;

    for (i = 0; i < serv->deps.count; ++i) {
        int dep = named_service_index(serv->deps.services[i]->name);

        dependency_graph[index][dep] = 1;
    }

    // 用来表示第 index 服务已被注册
    dependency_graph[index][index] = 1;

    return 0;
}

void unregister_service(struct service_struct *serv) {
    int index = named_service_index(serv->name);
    if (index < 0)
        return;

    size_t i;
    for (i = 0; i < MAX_SERVICES_COUNT; ++i) {
        dependency_graph[index][i] = 0;
    }
    services[index] = NULL;
    --services_count;
}

void calc_graph(int (*graph)[MAX_SERVICES_COUNT], int *out, int *in) {
    if (!graph || (!out && !in))
        return;

    int i, j;

    for (i = 0; i < MAX_SERVICES_COUNT; ++i) {
        if (out)
            out[i] = -1;    // 未注册的为 -1
        if (in)
            in[i] = -1;

        for (j = 0; j < MAX_SERVICES_COUNT; ++j) {
            if (out)
                out[i] += graph[i][j];
            if (in)
                in[i] += graph[j][i];
        }
    }
}

int sort_by_graph(int sorted[MAX_SERVICES_COUNT]) {
    int out[MAX_SERVICES_COUNT];
    int i, j;
    int updated = 1, total = 0;
    int graph[MAX_SERVICES_COUNT][MAX_SERVICES_COUNT];

    memcpy(graph, dependency_graph, sizeof(dependency_graph));

    while (updated) {
        updated = 0;

        calc_graph(graph, out, NULL);

        for (i = 0; i < MAX_SERVICES_COUNT; ++i) {
            if (out[i] == 0) {
                updated = 1;
                sorted[total++] = i;
                // 去掉图中对应依赖
                for (j = 0; j < MAX_SERVICES_COUNT; ++j) {
                    graph[i][j] = 0;
                    graph[j][i] = 0;
                }
                break;
            }
        }
    }

    if (total != services_count)
        return -1;
    return 0;
}

static void get_service_state(struct service_struct *serv) {
    if (serv->state_fd < 0)
        return;

    int ret;
    struct pollfd pfd = {0};
    pfd.fd = serv->state_fd;
    pfd.events = POLLIN;

    ret = poll(&pfd, 1, 10000);
    if (ret <= 0 || !(pfd.revents & POLLIN)) {
        serv->state = SERVICE_UNKNOWN;
        return;
    }
    
    int state;
    if (recv_all(serv->state_fd, &state, sizeof(state)) != sizeof(state)) {
        serv->state = SERVICE_UNKNOWN;
        return;
    }
    serv->state = state;
}

static int start_service(struct service_struct *serv) {
    /* 创建 USD */
    serv->server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serv->server_fd < 0) {
        perror("socket");
        return -1;
    }

    unlink(serv->addr_un.sun_path);
    if (bind(serv->server_fd, (struct sockaddr *)&serv->addr_un, sizeof(struct sockaddr_un)) < 0) {
        perror("bind");
        goto error;
    }

    if (listen(serv->server_fd, 1) < 0) {
        perror("listen");
        goto error1;
    }

    /* 创建新进程运行服务程序 */
    pid_t pid;
    pid = fork();

    if (pid < 0) {
        perror("fork");
        goto error1;
    }
    if (pid == 0) {
        // 这里不能使用 return，因为 return 只是从函数返回，然后接着从函数调用处执行
        int ret = serv->service_entry(NULL);
        _exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    serv->pid = pid;
    printf("%s pid: %d\r\n", serv->name, pid);

    serv->state_fd = accept(serv->server_fd, NULL, NULL);
    if (serv->state_fd < 0) {
        perror("accept");
        goto error1;
    }

    while (1) {
        get_service_state(serv);
        // printf("state %d received\r\n", serv->state);
        switch (serv->state)
        {
        case SERVICE_UNKNOWN:
        case SERVICE_STARTED:
            continue;
        case SERVICE_RUNNING:
            return pid;
        case SERVICE_FAILED:
        case SERVICE_STOPPED:
        default:
            goto error2;
        }
    }

    return pid;

error2:
    close(serv->state_fd);
error1:
    unlink(serv->addr_un.sun_path);
error:
    close(serv->server_fd);
    return -1;
}

static void stop_service(struct service_struct *serv) {
    printf("%s stopped\r\n", serv->name);

    if (serv->pid < 0)
        return;

    kill(serv->pid, SIGTERM);
    waitpid(serv->pid, NULL, 0);
    serv->pid = -1;

    close(serv->state_fd);
    unlink(serv->addr_un.sun_path);
    close(serv->server_fd);

    serv->state_fd = -1;
    serv->server_fd = -1;

    printf("%s stopped\r\n", serv->name);
}

int start_services() {
    int sorted[MAX_SERVICES_COUNT];

    if (sort_by_graph(sorted)) {
        fprintf(stderr, "start_services failed, please make sure no services in circular dependency\r\n");
        return -1;
    }

    int i;
    for (i = 0; i < services_count; ++i) {
        struct service_struct *serv = services[sorted[i]];
        if (start_service(serv) < 0) {
            fprintf(stderr, "start_service failed\r\n");
            goto rollback;
        }
    }

    return 0;

rollback:
    while (i >= 0) {
        stop_service(services[sorted[i]]);
        --i;
    }
    return -1;
}

void stop_services() {
    int sorted[MAX_SERVICES_COUNT];

    sort_by_graph(sorted);

    int i;
    for (i = services_count - 1; i >= 0; --i) {
        struct service_struct *serv = services[sorted[i]];
        stop_service(serv);
    }
}
