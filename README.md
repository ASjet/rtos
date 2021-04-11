# RT-Thread [打印机队列实例](./applications/printer.c)<img src="https://img.shields.io/badge/Platform-STM32F103-yellowgreen"/><img src="https://img.shields.io/badge/build-passed-brightgreen"/>
- [摘要](#摘要)
- [开发环境](#开发环境)
  - [仿真平台](#仿真平台)
  - [硬件参数](#硬件参数)
  - [系统组件](#系统组件)
- [实例简介](#实例简介)
- [模块介绍](#模块介绍)
  - [任务批信息](#任务批信息)
  - [打印任务邮件](#打印任务邮件)
  - [printer线程工作流](#printer线程工作流)
  - [sender线程工作流](#sender线程工作流)
  - [收尾工作](#收尾工作)
  - [其他参数](#其他参数)

### 摘要

基于RT-Thread线程库的打印机队列模拟实例([applications/printer.c](applications/printer.c))

### 开发环境

##### 仿真平台

Keil MDK5

##### 硬件参数

STM32F103ZF

##### 系统组件

RT-Thread Kernel

CORTEX-M3

STM32_HAL

FinSH



### 实例简介

该打印机队列实例由两个发送打印任务的线程(sender)和一个打印机线程(printer)组成，sender间通过`semaphore`和`mutex`保持同步，同时通过`mailbox`和`signal`与printer进行双向异步通信

其中sender线程和printer线程使用**生产者-消费者IPC模型**代替邮箱阻塞同步，邮箱只用于传递信息

### 模块介绍

##### 任务批信息

```c
typedef struct
{
    rt_size_t amount; // 总任务数量
    rt_size_t remain; // 剩余任务数量
    char *tasks[];	  // 打印内容数组
} task_batch;
```

每个sender线程对应一个任务批信息，储存该线程需要打印内容的相关信息



##### 打印任务邮件

```c
static rt_mailbox_t print_queue;
typedef struct
{
    char *content;		// 打印内容
    rt_thread_t tid;	// 发送该任务的线程id
} print_task;
typedef print_task *print_task_t;
```

sender线程每次向打印队列print_queue发送一封打印任务邮件，描述本次打印任务的相关信息



##### printer线程工作流

```c
while (1)
{
    // 获取打印队列满信号量
    if (rt_sem_take(print_queue_full, RT_WAITING_FOREVER) != RT_EOK)
        continue;
    // 从打印队列中获取一个任务
    if (rt_mb_recv(print_queue, (rt_uint32_t *)&task, RT_WAITING_FOREVER) != RT_EOK)
        rt_kprintf("Printer: Failed to receive print task mail.\n");
	// 释放打印队列空信号量
    rt_sem_release(print_queue_empty);

    // 模拟打印
    rt_kprintf("-----Print Start-----\n");
    rt_kprintf("%s\n", task->content);
    rt_kprintf("-----Print Done------\n");

    // 发送打印完成信号
    rt_thread_kill(task->tid, SIGUSR1);
}
```

打印线程获取一个打印任务，打印完成后发送一个打印完成的信号通知发起该任务的线程，然后等待至下个任务到达



##### sender线程工作流

```c
for (i = 0; i < info->amount; ++i)
{
    task_list[i].content = info->tasks[i];
    task_list[i].tid = tid;

    if (rt_mutex_take(print, RT_WAITING_FOREVER) != RT_EOK)
    {
        rt_kprintf("Error: Deadlock.\n");
        goto _exit;
    }
    rt_sem_take(print_queue_empty, RT_WAITING_FOREVER);

    rt_mb_send(print_queue, (rt_uint32_t)(task_list + i));

    rt_sem_release(print_queue_full);
    rt_mutex_release(print);

#ifdef DELAY_AFTER_SEND
    rt_thread_mdelay(delay);
#endif

}
```

sender线程函数首先初始化`info`变量指向该sender线程对应的任务批信息，然后创建一个`task_list`数组将任务批信息转换成邮件列表

sender线程之间通过semaphore和mutex互斥访问邮箱，并可选择每次发送邮件后是否延迟一段时间



##### 收尾工作

```c
// 等待任务队列完成
while (info->remain > 0)
    rt_thread_mdelay(WAIT_TIME);

_exit:
rt_free(task_list);
--sender_barrier;
// 若全部任务线程均到达屏障则挂起printer线程
if (sender_barrier == 0)
{
    rt_thread_kill(printer, SIGUSR2);

#ifdef DELETE_AFTER_PRINT
    rt_thread_mdelay(WAIT_TIME);
    rt_thread_delete(printer);
#endif

}
```

sender线程发送完所有任务后，循环等待任务完成，完成后释放邮件列表的内存空间

所有sender线程通过屏障`sender_barrier`进行任务进度同步，在全部打印任务均完成后挂起printer线程

最后可选择是否删除printer线程



##### 其他参数

```c
// 发送完一个打印任务后是否延迟
#define DELAY_AFTER_SEND

// 打印完毕后是否删除打印机线程
#define DELETE_AFTER_PRINT

// 线程参数
#define THREAD_PRIORITY (rt_int8_t)25
#define THREAD_STACK_SIZE (rt_int32_t)512
#define THREAD_TIMESLICE (rt_int32_t)10

// 单次打印内容最大长度
#define PRINT_CONTENT_SIZE (rt_size_t)128

// 打印队列长度
#define PRINT_QUEUE_LENGTH (rt_size_t)4

// 发送任务延时长度
#define SENDER_DELAY (rt_int32_t)10

// 等待时间长度
#define WAIT_TIME (rt_int32_t)100
```

### 实例截图

在msh中运行实例，输入

```
msh>printer_sample
```

<img src="http://disk.itfs127.com/img/printer_sample_screenshot.png" align="left"/>

