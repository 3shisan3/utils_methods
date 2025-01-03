#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "threadpool/method_2/c/threadpool.h"  // 确保包含线程池的头文件

#define NUM_THREADS 4   // 线程池中的线程数量
#define NUM_TASKS 10    // 要调度的任务数量

// 任务的执行函数
void task_function(void *arg) {
    int task_id = *((int *)arg);
    printf("Task %d is being executed by thread %ld\n", task_id, pthread_self());
    free(arg);  // 释放任务参数内存
    sleep(1);   // 模拟任务执行时间
}

int main() {
    threadpool_t *pool = threadpool_create(NUM_THREADS, 0); // 创建线程池
    if (pool == NULL) {
        fprintf(stderr, "Failed to create thread pool\n");
        return EXIT_FAILURE;
    }

    // 调度任务
    for (int i = 0; i < NUM_TASKS; i++) {
        int *task_id = (int *)malloc(sizeof(int));  // 为任务ID分配内存并进行类型转换
        if (task_id == NULL) {
            fprintf(stderr, "Failed to allocate memory for task ID\n");
            threadpool_destroy(nullptr, pool); // 清理线程池
            return EXIT_FAILURE;
        }
        *task_id = i;  // 设置任务ID

        // 使用一个临时变量来避免取地址错误
        struct threadpool_task task = {.routine = task_function, .data = task_id};  // 确保使用正确的结构体类型
        if (threadpool_schedule(&task, pool) != 0) {
            fprintf(stderr, "Failed to schedule task %d\n", i);
            free(task_id);  // 释放任务ID内存
        }
    }

    // 等待任务完成并销毁线程池
    sleep(5);  // 等待一段时间以确保所有任务完成
    threadpool_destroy(nullptr, pool);
    return EXIT_SUCCESS;
}