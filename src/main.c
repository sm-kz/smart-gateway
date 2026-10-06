#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

extern int camera_service(int pipe);
extern int web_service(int pipe);

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

    /**
     * 必须等待 camera service 初始化完毕后，才能开启 web service
     * 这里使用匿名管道进行同步
     */
    int ready_pipe[2];

    if (pipe(ready_pipe) < 0) {
        perror("pipe");
        return -1;
    }

    pid_t camera_pid;
    pid_t web_pid;

    camera_pid = fork();

    if (camera_pid < 0) {
        perror("fork camera");
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        return -1;
    }
    if (camera_pid == 0) {
        /* camera service 进程 */
        close(ready_pipe[0]);   // 子进程拷贝了所有管道，但不需要读
        return camera_service(ready_pipe[1]);
    }

    close(ready_pipe[1]);   // 父进程不需要写
    char ready;
    ssize_t ret = read(ready_pipe[0], &ready, 1);
    close(ready_pipe[0]);

    if (ret != 1 || ready != 1) {
        fprintf(stderr, "camera service startup failed\n");
        kill(camera_pid, SIGTERM);
        waitpid(camera_pid, NULL, 0);
        return -1;
    }

    web_pid = fork();
    if (web_pid < 0) {
        perror("fork web");
        kill(camera_pid, SIGTERM);
        waitpid(camera_pid, NULL, 0);
        return -1;
    }
    if (web_pid == 0) {
        /* web service 进程 */
        return web_service(-1);
    }

    /* 主进程 */
    printf("camera service pid: %d\r\n", camera_pid);
    printf("web service pid: %d\r\n", web_pid);

    while(!exit_requested) {
        // 主进程阻塞检查僵尸进程
        pid_t ret = waitpid(-1, NULL, 0);

        if (ret < 0) {
            if (errno == EINTR)
                continue;

            perror("waitpid");
            break;
        }
    }

    kill(web_pid, SIGTERM);
    kill(camera_pid, SIGTERM);

    waitpid(web_pid, NULL, 0);
    waitpid(camera_pid, NULL, 0);

    return 0;
}