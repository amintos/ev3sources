#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>  // for threads
#include <linux/sched.h>    // for task_struct
#include <linux/time.h>     // for using jiffies  
#include <linux/jiffies.h>  // for using jiffies  
#include <linux/timer.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>

#include <linux/semaphore.h>
#include <linux/mutex.h>

#include <linux/module.h>

#define MOTOR_CONTROL_PERIOD_MS 10
#define COMPRESSION_PERIOD_S 5
#define MAINTENANCE_PERIOD_MS 500

#define COMPRESSION_SPIN_MSEC 500
#define MAINTENANCE_SPIN_MSEC 3

#define MOTOR_CONTROL_PRIORITY 1
#define COMPRESSION_PRIORITY 102
#define MAINTENANCE_PRIORITY 105

static struct task_struct   *threadLow, 
                            *threadMiddle, 
                            *threadHigh;

DEFINE_MUTEX(sensorMutex);

const unsigned long msecPerSecond = 1000;
const unsigned long usecPerMsec = 1000;
const unsigned long nsecPerUsec = 1000;
const unsigned long nsecPerMsec = 1000000;
unsigned long gettimeNsec(void) {
	return ktime_to_ns(ktime_get());
}

void spin_for (/* msecs */ int n )
{
	unsigned long now = gettimeNsec();
	int counter=0;
	while ( ( gettimeNsec() - now) < n*nsecPerMsec) { 
		counter++;
	}
} 

static int __sched do_usleep_range(unsigned long min, unsigned long max)
{
    ktime_t kmin;
    unsigned long delta;

    kmin = ktime_set(0,0);
    kmin = ktime_add_ns(kmin, min * nsecPerUsec);
    delta = (max - min) * nsecPerUsec;
    return schedule_hrtimeout_range(&kmin, delta, HRTIMER_MODE_REL);
}

/**
 * ssleep_range 
 * @min: Minimum time in secs to sleep
 * @max: Maximum time in secs to sleep
 */
void ssleep_range(unsigned long min, unsigned long max)
{
    ktime_t kmin;
    unsigned long delta;
    __set_current_state(TASK_UNINTERRUPTIBLE);

    kmin = ktime_set(min,0);
    delta = (max - min) * 1000 * nsecPerMsec;
    return schedule_hrtimeout_range(&kmin, delta, HRTIMER_MODE_REL);
}

/**
 * usleep_range - Drop in replacement for udelay where wakeup is flexible
 * @min: Minimum time in usecs to sleep
 * @max: Maximum time in usecs to sleep
 */
void usleep_range(unsigned long min, unsigned long max)
{
    __set_current_state(TASK_UNINTERRUPTIBLE);
    do_usleep_range(min, max);
}

#define MOTOR_CMD_START 0
#define MOTOR_CMD_STOP  1
int motor_command(int command) {

	int ret = 0;
	char *argv[] = {"/home/root/motor_control", NULL, NULL };
	char *envp[] = {"HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
    if(command == MOTOR_CMD_START) {
        argv[1] = "start";
    } else {
        argv[1] = "stop";
    }

	/* last parameter: 1 -> wait until execution has finished, 0 go ahead without waiting*/
	/* returns 0 if usermode process was started successfully, errorvalue otherwise*/
	/* no possiblity to get return value of usermode process*/
	ret = call_usermodehelper("/home/root/motor_control", argv, envp, UMH_WAIT_EXEC);
	if (ret != 0) {
		printk("Error in call to usermodehelper: %i\n", ret);
        return 0;
	}

    return 1;
}

/* Credits due to: http://www.novickscode.com/code/162.elapsed.c_txt */
struct timespec* diff_timespec(struct timespec *result, struct timespec start, struct timespec end)
{
    if (end.tv_nsec < start.tv_nsec){ // peform carry like in normal subtraction
        //                123456789
        result->tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;        
        result->tv_sec = end.tv_sec - 1 - start.tv_sec;
    }
    else{
        result->tv_nsec = end.tv_nsec - start.tv_nsec;        
        result->tv_sec = end.tv_sec - start.tv_sec;
    }

    return result;
}

int readSensor(void) {
    return 4;
}

unsigned long long timespecToNsec(struct timespec source) {
    return source.tv_sec * msecPerSecond * nsecPerMsec + source.tv_nsec; 
}

int threadLow_fn(void* params) {

    struct sched_param priorityLow;
    int counter, sensorResult;
    unsigned long now;
    ktime_t now_ktime, target_ktime;
    struct timespec now_timespec;
    int items;

    priorityLow.sched_priority = MAINTENANCE_PRIORITY;
    sched_setscheduler(threadLow, SCHED_FIFO, &priorityLow); 

    counter = 0;
    while(true) {
        counter += 1;
        if(counter == 60) {
            break;
        }
    	now = gettimeNsec();

        /* Actual action */
        /* Acquire the mutex shared by the low and the high thread */
        mutex_lock(&sensorMutex);
        sensorResult = readSensor();
	    spin_for(MAINTENANCE_SPIN_MSEC);
        mutex_unlock(&sensorMutex);

        //printk("Finished Maintenance: Everything looks good.\n");
        //printk("Maintenance took: %lu nsec.\n", gettimeNsec() - now);
        
        /* Put yourself to sleep */
        usleep_range(MAINTENANCE_PERIOD_MS*usecPerMsec,
                     MAINTENANCE_PERIOD_MS*usecPerMsec);

    }
    //printk("All maintenance checks checked.\n");
         
	return 0;
}

int threadMiddle_fn(void* params) {

    struct sched_param priorityMiddle;
    int counter, sensorResult;
    unsigned long now;
    ktime_t now_ktime, target_ktime;
    struct timespec now_timespec;

    priorityMiddle.sched_priority = COMPRESSION_PRIORITY;
	sched_setscheduler(threadMiddle, SCHED_FIFO, &priorityMiddle); 

    counter = 0;
    while(true) {
        counter += 1;
        if(counter == 5) {
            break;
        }
    	now = gettimeNsec();

        /* Actual action */
	    spin_for(COMPRESSION_SPIN_MSEC);

        //printk("Finished compressing sensordata.\n");
        //printk("Compression took: %lu msec.\n", gettimeNsec() - now);
        
        /* Put yourself to sleep */
        ssleep_range(COMPRESSION_PERIOD_S, 
                     COMPRESSION_PERIOD_S);

    }
    //printk("Everything was compressed nicely.\n");

	return 0;
}

int threadHigh_fn(void* params) {

    struct sched_param priorityHigh;
    int i, counter, sensorResult;
    ktime_t now_ktime, target_ktime, last_ktime, start_ktime, end_ktime;
    struct timespec now_timespec, last_timespec, tmp_timespec;
    unsigned long long deltaSum, sleepTimeDelta, deltaNs;

    priorityHigh.sched_priority = MOTOR_CONTROL_PRIORITY;
    sched_setscheduler(threadHigh, SCHED_FIFO, &priorityHigh); 

    /*
    deltaSum = 0;
    printk("Start da loopa!\n");
    now_timespec = current_kernel_time();
    usleep_range(2000, 2000);
    for(i = 0; i < 1000; i++) {
        last_timespec = now_timespec;
        now_timespec = current_kernel_time();
        diff_timespec(&tmp_timespec, last_timespec, now_timespec);
        deltaSum += timespecToNsec(tmp_timespec);

        now_timespec = current_kernel_time();
        usleep_range(2000, 2000);
    }
    printk("RESULT!!!!! %lld\n", deltaSum);*/

    start_ktime = ktime_get();
    counter = 0;
    now_ktime = ktime_get(); 
    while(true) {
        counter += 1;
        if(counter == 6100) {
            break;
        }

        /* Check if woken up early enough */
        last_ktime = now_ktime;
        now_ktime = ktime_get(); 
        deltaNs = ktime_to_ns(now_ktime) - ktime_to_ns(last_ktime);
        if(deltaNs > (MOTOR_CONTROL_PERIOD_MS*2*nsecPerMsec)) {
            motor_command(MOTOR_CMD_STOP);

            printk("PANIC in %d with %llu!!\n", counter,
                                                deltaNs);
            //return 1;
        } 

        /* Actual action */
        mutex_lock(&sensorMutex);
        sensorResult = readSensor();
        mutex_unlock(&sensorMutex);
        if(sensorResult > 4) {
            motor_command(MOTOR_CMD_STOP);
        }
        
        /* Put yourself to sleep */
        usleep_range(MOTOR_CONTROL_PERIOD_MS*usecPerMsec, MOTOR_CONTROL_PERIOD_MS*usecPerMsec);
    }
    deltaNs = ktime_to_ns(ktime_get()) - ktime_to_ns(start_ktime);
    printk("Mission successful after %d cycles and %llu seconds!\n", 
            counter,
            deltaNs);

	return 0;
}


int thread_init (void) {

	char lowThreadName[17]="threadMaintenance";
	char middleThreadName[13]= "threadMiddle";
	char highThreadName[18]= "threadMotorControl";

	motor_command(MOTOR_CMD_STOP);
	motor_command(MOTOR_CMD_START);

	threadLow = kthread_create(
					threadLow_fn,
					NULL,
					lowThreadName);
	if(!IS_ERR(threadLow)) {
		wake_up_process(threadLow);
	}
	threadMiddle = kthread_create(
					threadMiddle_fn,
					NULL,
					middleThreadName);
	if(!IS_ERR(threadMiddle)) {
		wake_up_process(threadMiddle);
	}
	threadHigh = kthread_create(
					threadHigh_fn,
					NULL,
					highThreadName);
	if(!IS_ERR(threadHigh)) {
		wake_up_process(threadHigh);
	}

	return 0;
}

void thread_cleanup(void) {
}

MODULE_LICENSE("GPL");    
module_init(thread_init);
module_exit(thread_cleanup);
