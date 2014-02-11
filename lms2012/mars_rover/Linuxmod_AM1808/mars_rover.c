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

#define WATCHDOG_PERIOD_MS 5 
#define MOTOR_CONTROL_PERIOD_MS 20
#define COMPRESSION_PERIOD_MS 100
#define MAINTENANCE_PERIOD_MS 30

#define COMPRESSION_SPIN_MSEC 50
#define MAINTENANCE_SPIN_MSEC 15 

#define WATCHDOG_PRIORITY 90
#define MOTOR_CONTROL_PRIORITY 70
#define COMPRESSION_PRIORITY 50 
#define MAINTENANCE_PRIORITY 20 

static struct task_struct   *threadLow, 
                            *threadMiddle, 
                            *threadHigh,
                            *threadWatchdog;

DEFINE_MUTEX(sensorMutex);

int stopFlag = 0;
ktime_t last_motor_control_ktime;

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
    schedule_hrtimeout_range(&kmin, delta, HRTIMER_MODE_REL);
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

int sound_command(void) {

	int ret = 0;
	char *argv[] = {"/home/root/sound_control", NULL, NULL };
	char *envp[] = {"HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };

	/* last parameter: 1 -> wait until execution has finished, 0 go ahead without waiting*/
	/* returns 0 if usermode process was started successfully, errorvalue otherwise*/
	/* no possiblity to get return value of usermode process*/
	ret = call_usermodehelper("/home/root/sound_control", argv, envp, UMH_WAIT_EXEC);
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
    ssleep_range(3, 3);
    while(true && stopFlag == 0) {
        counter += 1;
    	now = gettimeNsec();

        /* Actual action */
        /* Acquire the mutex shared by the low and the high thread */
        mutex_lock(&sensorMutex);
        sensorResult = readSensor();
	    spin_for(MAINTENANCE_SPIN_MSEC);
        mutex_unlock(&sensorMutex);

        //printk("Finished Maintenance: Everything looks good.\n");
        printk("Maintenance took: %lu nsec.\n", gettimeNsec() - now);
        
        /* Put yourself to sleep */
        usleep_range(MAINTENANCE_PERIOD_MS*usecPerMsec,
                     MAINTENANCE_PERIOD_MS*usecPerMsec);

    }
    printk("All maintenance checks checked.\n");
         
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
    ssleep_range(3, 3);
    while(true && stopFlag == 0) {
        counter += 1;
    	now = gettimeNsec();

        /* Actual action */
	spin_for(COMPRESSION_SPIN_MSEC);

        //printk("Finished compressing sensordata.\n");
        printk("Compression took: %lu msec.\n", gettimeNsec() - now);
        
        /* Put yourself to sleep */
        usleep_range(COMPRESSION_PERIOD_MS*usecPerMsec, 
                     COMPRESSION_PERIOD_MS*usecPerMsec);

    }
    printk("Everything was compressed nicely.\n");

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

    counter = 0;
    while(true && stopFlag == 0) {
        counter += 1;
        if(counter == 6100) {
            break;
        }

        /* Check if woken up early enough */
        last_motor_control_ktime = ktime_get(); 

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
    if(stopFlag == 1) {
        printk("Stopped mission at %d!!\n", counter);
    } else {
        deltaNs = ktime_to_ns(ktime_get()) - ktime_to_ns(start_ktime);
        printk("Mission successful after %d cycles and %llu seconds!\n", 
                counter,
                deltaNs);
        stopFlag = 1;
    }

	return 0;
}

int threadWatchdog_fn(void* params) {

    struct sched_param priorityHigh;
    ktime_t now_ktime, last_ktime, start_ktime, end_ktime;
    unsigned long long deltaSum, sleepTimeDelta, deltaNs;

    priorityHigh.sched_priority = WATCHDOG_PRIORITY;
    sched_setscheduler(threadHigh, SCHED_FIFO, &priorityHigh); 
    
    // To avoid inconsistent state at startup
    last_motor_control_ktime = ktime_get(); 
    now_ktime = ktime_get(); 
    while(true && stopFlag == 0) {

        /* Check if woken up early enough */
        last_ktime = last_motor_control_ktime;
        now_ktime = ktime_get(); 
        deltaNs = ktime_to_ns(now_ktime) - ktime_to_ns(last_ktime);
        if(deltaNs > (MOTOR_CONTROL_PERIOD_MS*2*nsecPerMsec)) {
	        stopFlag = 1;
            motor_command(MOTOR_CMD_STOP);
            sound_command();

            printk("PANIC with %llu!!\n", deltaNs);
            return 1;
        } 
        
        /* Put yourself to sleep */
        usleep_range(WATCHDOG_PERIOD_MS*usecPerMsec, WATCHDOG_PERIOD_MS*usecPerMsec);
    }

	return 0;
}


int thread_init (void) {

	char lowThreadName[17]="threadMaintenance";
	char middleThreadName[13]= "threadMiddle";
	char highThreadName[18]= "threadMotorControl";
	char watchdogThreadName[9]= "watchdog";

	printk("Mars Rover build: ");

	motor_command(MOTOR_CMD_START);

	printk("Mars Rover build: ");
	printk(__DATE__);
	printk(" ");
	printk(__TIME__);
	printk("\n");

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
	threadWatchdog = kthread_create(
					threadWatchdog_fn,
					NULL,
					watchdogThreadName);
	if(!IS_ERR(threadWatchdog)) {
		wake_up_process(threadWatchdog);
	}

	return 0;
}

void thread_cleanup(void) {
}

MODULE_LICENSE("GPL");    
module_init(thread_init);
module_exit(thread_cleanup);
