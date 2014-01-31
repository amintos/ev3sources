#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>  // for threads
#include <linux/sched.h>  // for task_struct
#include <linux/time.h>   // for using jiffies  
#include <linux/jiffies.h>   // for using jiffies  
#include <linux/timer.h>

#include <linux/semaphore.h>
#include <linux/mutex.h>


static struct task_struct         *threadLow, 
                                *threadMiddle, 
                                *threadHigh;

static atomic_t threadCount;

struct semaphore lowGo, midGo, highGo, highReady, midReady;

struct mutex sharedMutex;

const int msecPerSecond = 1000;
const int seconds = 1;
int gettimeMsec(void) {
        return jiffies_to_msecs(jiffies);
}

#define LOW_SPIN 2
#define MID_SPIN 5
#define HIGH_SPIN 1
void spin_for (/* msecs */ int n )
{
        int now = gettimeMsec();
        int counter=0;
        while ( ( gettimeMsec() - now) < n) { 
                counter++;
        }
}

int threadLow_fn(void* params) {

        int now;

        /* Waiting to be allowed to start */
        if(down_interruptible(&lowGo)) {
                return -1;
        };

        /* Acquire the mutex shared by the low and the high thread */
        mutex_lock(&sharedMutex);

        /* Wait for the others to get ready */
        if(down_interruptible(&midReady)) {
                return -1;
        };
        if(down_interruptible(&highReady)) {
                return -1;
        };

        /* Start the others */
        now = gettimeMsec();
        up(&highGo);
        up(&midGo);

        /* Be busy */
        spin_for(LOW_SPIN*seconds*msecPerSecond);

        /* Be gentle and release the shared mutex */
        mutex_unlock(&sharedMutex);
         
        printk ("Low took %d milliseconds wanted about %d (critical section + mid time)\n",gettimeMsec() - now,(LOW_SPIN+MID_SPIN)*1000);

        atomic_dec(&threadCount);

        return 0;
}

int threadMiddle_fn(void* params) {

        int now;

        /* Show I'm ready */
        up(&midReady);
        
        /* Wait to start */
        if(down_interruptible(&midGo)) {
                return -1;
        };

        /* Get busy */
            now = gettimeMsec();
        spin_for(MID_SPIN*seconds*msecPerSecond);
        printk("mid took %d milliseconds wanted about %d\n",gettimeMsec() - now,MID_SPIN*1000);

        atomic_dec(&threadCount);
        return 0;
}

int threadHigh_fn(void* params) {

        int now;

        /* Show I'm ready */
        up(&highReady);
        
        /* Wait to start */
        if(down_interruptible(&highGo)) {
                return -1;
        };

        /* Try to get the shared mutex */
        now = gettimeMsec();
        mutex_lock(&sharedMutex);
        spin_for(HIGH_SPIN*seconds*msecPerSecond);
        mutex_unlock(&sharedMutex);

        printk("high took %d milliseconds wanted about %d (low critical section)\n",gettimeMsec() - now,LOW_SPIN*1000);

        atomic_dec(&threadCount);
        return 0;
}

int thread_init (void) {

        printk("test_prioinv build no 2\n");

        char lowThreadName[10]="threadLow";
        char middleThreadName[13]= "threadMiddle";
        char highThreadName[11]= "threadHigh";

        struct sched_param priorityLow;
        struct sched_param priorityMiddle;
        struct sched_param priorityHigh;
        atomic_set(&threadCount,0);

        sema_init(&lowGo,0);
        sema_init(&midGo,0);
        sema_init(&highGo,0);
        sema_init(&highReady,0); 
        sema_init(&midReady,0); 

	    mutex_init(&sharedMutex);

        atomic_inc(&threadCount);
        threadLow = kthread_create(
                                        threadLow_fn,
                                        NULL,
                                        lowThreadName);
        priorityLow.sched_priority = 10;
        sched_setscheduler(threadLow, SCHED_FIFO, &priorityLow); 
        if(!IS_ERR(threadLow)) {
                wake_up_process(threadLow);
        }

        atomic_inc(&threadCount);
        threadMiddle = kthread_create(
                                        threadMiddle_fn,
                                        NULL,
                                        middleThreadName);
        priorityMiddle.sched_priority = 50;
        sched_setscheduler(threadMiddle, SCHED_FIFO, &priorityMiddle); 
        if(!IS_ERR(threadMiddle)) {
                wake_up_process(threadMiddle);
        }

        atomic_inc(&threadCount);
        threadHigh = kthread_create(
                                        threadHigh_fn,
                                        NULL,
                                        highThreadName);
        priorityHigh.sched_priority = 70;
        sched_setscheduler(threadHigh, SCHED_FIFO, &priorityHigh); 
        if(!IS_ERR(threadHigh)) {
                wake_up_process(threadHigh);
        }

        up(&lowGo);

        return 0;
}

void thread_cleanup(void) {
        while(atomic_read(&threadCount) > 0) {
                schedule();
        }
}

MODULE_LICENSE("GPL");    
module_init(thread_init);
module_exit(thread_cleanup);
