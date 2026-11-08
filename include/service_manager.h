#ifndef __SERVICE_MANAGER_H
#define __SERVICE_MANAGER_H

#include <stddef.h>
#include <sys/un.h>

#define MAX_SERVICES_COUNT      20
#define MAX_NAME_LENGTH         32
#define MAX_DEPENDENCIES        MAX_SERVICES_COUNT - 1

enum service_state {
    SERVICE_UNKNOWN,
    SERVICE_STARTED,
    SERVICE_RUNNING,
    SERVICE_STOPPED,
    SERVICE_ALIVE,
    SERVICE_FAILED
};

struct service_struct;

struct dependency {
    struct service_struct *services[MAX_DEPENDENCIES];
    size_t count;
};

struct service_struct {
    char name[MAX_NAME_LENGTH];         // 服务名称
    struct sockaddr_un addr_un;         // 服务 UDS 地址，用于状态告知
    int (*service_entry)(void *);       // 服务程序入口
    enum service_state state;           // 服务状态
    struct dependency deps;             // 服务依赖

    /* 私有数据 */
    int server_fd;
    int state_fd;
    int pid;
};


int register_service(struct service_struct *serv);
void unregister_service(struct service_struct *serv);
int start_services();
void stop_services();


#endif