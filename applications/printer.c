#include <rtthread.h>

#define THREAD_PRIORITY (rt_int8_t)25
#define THREAD_STACK_SIZE (rt_int32_t)512
#define THREAD_TIMESLICE (rt_int32_t)10
#define PRINT_CONTENT_SIZE (rt_size_t)128
#define PRINT_QUEUE_LENGTH (rt_size_t)4
#define SENDER_DELAY (rt_int32_t)100

typedef struct
{
    char *content;
    rt_thread_t tid;
} print_task;
typedef print_task *print_task_t;

static rt_mailbox_t print_queue = RT_NULL;

static rt_thread_t printer = RT_NULL;

#define TASK_COUNT 2

static rt_thread_t sender1 = RT_NULL;
static rt_thread_t sender2 = RT_NULL;

int task_barrier = TASK_COUNT;

static rt_mutex_t print = RT_NULL;
static rt_sem_t print_queue_full = RT_NULL;
static rt_sem_t print_queue_empty = RT_NULL;

static char *task1[] = {
    "First task of sender1",
    "Second task of sender1",
    "Third task of sender1",
    "Forth task of sender1",
    "Fifth task of sender1"};

static char *task2[] = {
    "First task of sender2",
    "Second task of sender2"};

static void sender1_sig_handler(int _Signal)
{
    rt_kprintf("Sender1: Print task done.\n");
}

static void sender2_sig_handler(int _Signal)
{
    rt_kprintf("Sender2: Print task done.\n");
}

static void printer_sig_handler(int _Signal)
{
    rt_kprintf("Printer: Suspend.\n");
    rt_thread_suspend(rt_thread_self());
    rt_schedule();
}

static void printerEntry(void *_Parameter)
{
    print_task_t task = RT_NULL;

    // 安装休眠打印机信号
    rt_signal_install(SIGUSR2, printer_sig_handler);
    rt_signal_unmask(SIGUSR2);

    // 工作循环
    while (1)
    {
        if (rt_sem_take(print_queue_full, RT_WAITING_FOREVER) != RT_EOK)
            rt_kprintf("Printer: Failed to receive print semaphore.\n");
        if (rt_mb_recv(print_queue, (rt_uint32_t *)&task, RT_WAITING_FOREVER) != RT_EOK)
            rt_kprintf("Printer: Failed to receive print task mail.\n");

        rt_sem_release(print_queue_empty);

        // 模拟打印
        rt_kprintf("-----Print Start-----\n");
        rt_kprintf("%s\n", task->content);
        rt_kprintf("-----Print Done------\n");

        // 回收内存
        // rt_free(task);

        // 发送打印完成信号
        rt_thread_kill(task->tid, SIGUSR1);
    }
}

static void senderEntry(void *_Parameter)
{
    int i = 0, task_len = 0;
    char **content_list = RT_NULL;
    rt_int32_t sleep_time = RT_NULL;
    print_task_t task_list = RT_NULL;
    rt_thread_t tid = rt_thread_self();

    // 唤醒打印机线程
    if ((printer->stat & RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
    {
        rt_kprintf("Sender: Resume printer.\n");
        rt_thread_resume(printer);
    }

    // 根据线程id初始化参数
    if (tid == sender1)
    {
        rt_signal_install(SIGUSR1, sender1_sig_handler);
        task_len = 5;
        content_list = task1;
        sleep_time = SENDER_DELAY;
    }
    else if (tid == sender2)
    {
        rt_signal_install(SIGUSR1, sender2_sig_handler);
        task_len = 2;
        content_list = task2;
        sleep_time = SENDER_DELAY * 2;
    }
    else
    {
        rt_kprintf("Error: Unknown sender tid.\n");
        return;
    }
    rt_signal_unmask(SIGUSR1);

    task_list = (print_task_t)rt_malloc(sizeof(print_task) * task_len);
    if (task_list == NULL)
    {
        rt_kprintf("Error: Unable to allocate memory.\n");
    }

    // 发送任务
    for (i = 0; i < task_len; ++i)
    {
        task_list[i].content = content_list[i];
        task_list[i].tid = tid;

        if (rt_mutex_take(print, RT_WAITING_FOREVER) != RT_EOK)
            rt_kprintf("Error: Deadlock.\n");
        rt_sem_take(print_queue_empty, RT_WAITING_FOREVER);

        rt_mb_send_wait(print_queue, (rt_uint32_t)(task_list + i), RT_WAITING_FOREVER);

        rt_sem_release(print_queue_full);
        rt_mutex_release(print);

    }
    --task_barrier;
    rt_thread_mdelay(sleep_time);
    // 若全部任务线程均到达屏障则休眠打印机
    if (task_barrier == 0)
    {
        rt_thread_kill(printer, SIGUSR2);
        rt_thread_delete(printer);
    }
}

int printer_sample(void)
{
    // 初始化信号量
    print_queue_empty = rt_sem_create("print_queue_empty",
                                      PRINT_QUEUE_LENGTH,
                                      RT_IPC_FLAG_FIFO);
    if (print_queue_empty == RT_NULL)
    {
        rt_kprintf("Error: Create semaphore failed.\n");
        return -1;
    }

    print_queue_full = rt_sem_create("print_queue_full",
                                     0,
                                     RT_IPC_FLAG_FIFO);
    if (print_queue_full == RT_NULL)
    {
        rt_kprintf("Error: Create semaphore failed.\n");
        return -1;
    }

    // 初始化互斥量
    print = rt_mutex_create("print", RT_IPC_FLAG_FIFO);
    if (print == RT_NULL)
    {
        rt_kprintf("Error: Create mutex failed.\n");
        return -1;
    }

    // 初始化邮箱
    print_queue = rt_mb_create("print_queue",
                               PRINT_QUEUE_LENGTH * sizeof(print_task_t),
                               RT_IPC_FLAG_FIFO);
    if (print_queue == RT_NULL)
    {
        rt_kprintf("Error: Create mailbox failed.\n");
        return -1;
    }

    // 初始化线程
    printer = rt_thread_create("printer",
                               printerEntry, RT_NULL,
                               THREAD_STACK_SIZE,
                               THREAD_PRIORITY + 1, THREAD_TIMESLICE);

    sender1 = rt_thread_create("sender1",
                               senderEntry, RT_NULL,
                               THREAD_STACK_SIZE,
                               THREAD_PRIORITY, THREAD_TIMESLICE);
    if (sender1 != RT_NULL)
        rt_thread_startup(sender1);

    sender2 = rt_thread_create("sender2",
                               senderEntry, RT_NULL,
                               THREAD_STACK_SIZE,
                               THREAD_PRIORITY, THREAD_TIMESLICE);
    if (sender2 != RT_NULL)
        rt_thread_startup(sender2);

    if (printer != RT_NULL)
        rt_thread_startup(printer);

    return 0;
}

// 导出到命令列表
MSH_CMD_EXPORT(printer_sample, printer sample);