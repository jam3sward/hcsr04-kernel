/*
  * Experimental Kernel module for HC-SR04 Ultrasonic Range Finder
  *
  * Copyright (C) 2014 James Ward
  *
  * Released under the GPL
  *
  */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/irq.h>

/*---------------------------------------------------------------------------*/

/* The Kernel object */
static struct kobject *s_kernelObject = NULL;

/* GPIO pin used for trigger out */
static int trig = 23;
module_param(trig, int, S_IRUGO);

/* GPIO pin used for echo in */
static int echo = 22;
module_param(echo, int, S_IRUGO);

/*---------------------------------------------------------------------------*/

void timersub( struct timeval *a, struct timeval *b, struct timeval *res )
{
	/* Perform the carry for the later subtraction by updating b */
	if ( a->tv_usec < b->tv_usec ) {
    	int nsec = (b->tv_usec - a->tv_usec) / 1000000 + 1;
        b->tv_usec -= 1000000 * nsec;
        b->tv_sec += nsec;
	}

	if ( a->tv_usec - b->tv_usec > 1000000 ) {
        int nsec = (a->tv_usec - b->tv_usec) / 1000000;
        b->tv_usec += 1000000 * nsec;
        b->tv_sec -= nsec;
    }

    /* Compute the elapsed time */
    res->tv_sec  = a->tv_sec  - b->tv_sec;
    res->tv_usec = a->tv_usec - b->tv_usec;
}

/*---------------------------------------------------------------------------*/

/* This counts the number of interrupts received */
static int irqCount = 0;

/* This stores the time-stamp of the first two interrupts received */
static struct timeval irqTime[2];

/* Interrupt handler: called on rising/falling edge */
static irqreturn_t gpioInterruptHandler( int irq, void *dev_id )
{
	/* For the first two interrupts received, store the time-stamp */
	if ( irqCount < 2 )
		do_gettimeofday( &irqTime[irqCount] );

	/* Count the number of interrupts received */
	++irqCount;

    return IRQ_HANDLED;
}

/*---------------------------------------------------------------------------*/

/* Measures the range, returning the time period in microseconds, and the
 * range in millimetres. The return value is 1 for success, and 0 for failure.
 */
int measureRange(
	long *us,	/* out: time in us */
	long *mm	/* out: range in mm */
) {
	/* The speed of sound in mm/s */
	const long speedSound_mms = 340270;

	struct timeval elapsed;		/* used to store elapsed time */
	int irq = 0;				/* the IRQ number */

	unsigned long timeout = 0;	/* the timeout end in jiffies */

	/* Initialise variables used by interrupt handler */
	irqCount = 0;
	memset( &irqTime, 0, sizeof(irqTime) );

	/* Request an IRQ for the echo GPIO pin, so that we can measure the rising
	 * and falling edge of the pulse from the ranger.
	 */
    irq = gpio_to_irq( echo );
    if ( request_irq(
		irq, gpioInterruptHandler,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		"hcsr04_irq", NULL
	) )
        return -1;

	/* Transmit trigger pulse, lasting at least 10us */
	gpio_set_value(trig, 0);
	gpio_set_value(trig, 1);
	udelay(10);
	gpio_set_value(trig, 0);

	/* Calculate the end time in jiffies when we should time out */
	timeout = jiffies + msecs_to_jiffies(60);

	/* Wait until we have received two interrupts, or timed out */
	while ( (irqCount < 2) && time_before(jiffies,timeout) ) {
		/* Sleep for 1ms */
		msleep(1);
	}

	/* Free the interrupt */
	free_irq( irq, NULL );

	if ( irqCount == 2 ) {
		/* Calculate pulse length */
		timersub( &irqTime[1], &irqTime[0], &elapsed );

		/* Return the time period in microseconds. We ignore the tv_sec,
		 * because the maximum delay should be less than 60ms
		 */
		if ( us != NULL )
			*us = elapsed.tv_usec;

		/* Return the distance, based on speed of sound and the elapsed
		 * time in microseconds.
		 */
		if ( mm != NULL )
			*mm = elapsed.tv_usec * speedSound_mms / 2000000;

		/* Success */
		return 1;
	} else {
		/* Failure */
		if ( us != NULL ) *us = 0;
		if ( mm != NULL ) *mm = 0;
		return 0;
	}
}

/*---------------------------------------------------------------------------*/

/* This function is called when the 'range' kernel object is read */
static ssize_t range_show(
    struct kobject *object,
    struct kobj_attribute *attribute,
	char *buffer
) {
	long us = 0;
	long mm = 0;

	/* Outputs: <mm> <us> <good> where:
	 * <mm> = Distance in millimetres
	 * <us> = Time in microseconds
	 * <good> = 1 for success, 0 for failure
     */
	if ( measureRange( &us, &mm ) != 0 )
	    return sprintf( buffer, "%ld %ld 1\n", mm, us );
	else
		return sprintf( buffer, "0 0 0\n" );
}

/*---------------------------------------------------------------------------*/

/* Attribute representing the 'range' kernel object, which is read only */
static struct kobj_attribute rangeAttribute = __ATTR_RO(range);

/*---------------------------------------------------------------------------*/

/* List of all attributes */
static struct attribute *attrs[] = {
	&rangeAttribute.attr,
	NULL    /* terminate the list */
};

/*---------------------------------------------------------------------------*/

/* Attribute group */
static struct attribute_group attributeGroup = {
	.attrs = attrs
};

/*---------------------------------------------------------------------------*/

/* Initialise GPIO */
static int gpioInit( void )
{
    /* check that trigger GPIO is valid */
    if ( !gpio_is_valid(trig) ){
        printk( KERN_ALERT "trig GPIO %d is not valid\n", trig );
        return -EINVAL;
    }

    /* request the GPIO pin */
    if( gpio_request(trig, "hcsr04") != 0 ) {
        printk( KERN_ALERT "Unable to request trig GPIO %d\n", trig );
        return -EINVAL;
    }

    /* make GPIO an output */
    if( gpio_direction_output(trig, 0) != 0 ) {
        printk( KERN_ALERT "Failed to make trig GPIO %d an output\n", trig );
        return -EINVAL;
    }

    /* check that echo GPIO is valid */
    if ( !gpio_is_valid(echo) ){
        printk( KERN_ALERT "echo GPIO %d is not valid\n", echo );
        return -EINVAL;
    }

    /* request the GPIO pin */
    if( gpio_request(echo, "hcsr04") != 0 ) {
        printk( KERN_ALERT "Unable to request echo GPIO %d\n", echo );
        return -EINVAL;
    }

    /* make GPIO an input */
    if( gpio_direction_input(echo) != 0 ) {
        printk( KERN_ALERT "Failed to make echo GPIO %d an input\n", echo );
        return -EINVAL;
    }

    return 0;
}

/*---------------------------------------------------------------------------*/

/* Free GPIO */
static void gpioFree( void )
{
    gpio_free(trig);
    gpio_free(echo);
}

/*---------------------------------------------------------------------------*/

/* Module initialisation function */
static int __init moduleInit( void )
{
	int result = -1;

	/* Create /sys/kernel/tsic representing the TSIC sensor */
	s_kernelObject = kobject_create_and_add( "hcsr04", kernel_kobj );
	if ( s_kernelObject == NULL )
		return -ENOMEM;

	/* Create the files associated with this kobject */
	result = sysfs_create_group( s_kernelObject, &attributeGroup );
	if ( result ) {
	    /* Failed: clean up */
		kobject_put( s_kernelObject );
		return result;
	}

	/* Set up the GPIO */
	if ( gpioInit() < 0 )
	    return -EINVAL;

	return result;
}

/*---------------------------------------------------------------------------*/

/* Module exit function */
static void __exit moduleExit( void )
{
    /* Decrement refcount and clean up if zero */
    kobject_put( s_kernelObject );

    /* Free GPIO */
    gpioFree();
}

/*---------------------------------------------------------------------------*/

module_init(moduleInit);
module_exit(moduleExit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Ward");

/*---------------------------------------------------------------------------*/
