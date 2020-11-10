/**
 * @file
 * @brief ptpd-lwip entry code
 *
 * @author @htmlonly &copy; @endhtmlonly 2020 James Bennion-Pedley
 *
 * @date 1 Oct 2020
 */

#include "ptpd-lwip.h"

#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netbuf.h>

#include "def/datatypes_private.h"
#include "protocol.h"
#include "sys_time.h"

ptpClock_t ptpClock;
runTimeOpts_t rtOpts;
foreignMasterRecord_t ptpForeignRecords[DEFAULT_MAX_FOREIGN_RECORDS];


static void ptpd_thread(void *args __attribute((unused))) {

    // Initialize run-time options to default values.
    rtOpts.announceInterval = DEFAULT_ANNOUNCE_INTERVAL;
    rtOpts.syncInterval = DEFAULT_SYNC_INTERVAL;
    rtOpts.clockQuality.clockAccuracy = DEFAULT_CLOCK_ACCURACY;
    rtOpts.clockQuality.clockClass = DEFAULT_CLOCK_CLASS;
    rtOpts.clockQuality.offsetScaledLogVariance = DEFAULT_CLOCK_VARIANCE; /* 7.6.3.3 */
    rtOpts.priority1 = DEFAULT_PRIORITY1;
    rtOpts.priority2 = DEFAULT_PRIORITY2;
    rtOpts.domainNumber = DEFAULT_DOMAIN_NUMBER;
    rtOpts.slaveOnly = SLAVE_ONLY;
    rtOpts.currentUtcOffset = DEFAULT_UTC_OFFSET;
    rtOpts.servo.noResetClock = DEFAULT_NO_RESET_CLOCK;
    rtOpts.servo.noAdjust = NO_ADJUST;
    rtOpts.inboundLatency.nanoseconds = DEFAULT_INBOUND_LATENCY;
    rtOpts.outboundLatency.nanoseconds = DEFAULT_OUTBOUND_LATENCY;
    rtOpts.servo.sDelay = DEFAULT_DELAY_S;
    rtOpts.servo.sOffset = DEFAULT_OFFSET_S;
    rtOpts.servo.ap = DEFAULT_AP;
    rtOpts.servo.ai = DEFAULT_AI;
    rtOpts.maxForeignRecords = sizeof(ptpForeignRecords) / sizeof(ptpForeignRecords[0]);
    rtOpts.stats = PTP_TEXT_STATS;
    rtOpts.delayMechanism = DEFAULT_DELAY_MECHANISM;

    ptpClock.rtOpts = &rtOpts;
    ptpClock.foreignMasterDS.records = ptpForeignRecords;

    /* 9.2.2 */
    if (rtOpts.slaveOnly) rtOpts.clockQuality.clockClass = DEFAULT_CLOCK_CLASS_SLAVE_ONLY;

    /* No negative or zero attenuation */
    if (rtOpts.servo.ap < 1) rtOpts.servo.ap = 1;
    if (rtOpts.servo.ai < 1) rtOpts.servo.ai = 1;

    toState(&ptpClock, PTP_INITIALIZING);

    /// @todo probably need to check link status here too
    #if LWIP_DHCP
        // If DHCP, wait until the default interface has an IP address.
        while (!netif_default->ip_addr.addr) {
            // Sleep for 500 milliseconds.
            sys_msleep(500);
        }
    #endif

    for (;;) {
        void *msg;

        // Process the current state.
        doState(&ptpClock);

        /// @todo THIS NEEDS TO GO!!!
        // Wait up to 100ms for something to do, then do something anyway.
        if(sys_arch_mbox_tryfetch(&ptpClock.timerAlerts, &msg) == SYS_MBOX_EMPTY)
            sys_arch_mbox_fetch(&ptpClock.packetAlerts, &msg, 100); // THIS LOGIC IS FLAWED!!!
    }
}

/// @todo remember that the PTP timestamps need to be written into the pbuf when
// descriptors are processed!

void ptpdInit(ptpFunctions_t *functions, u8_t priority)
{
    DEBUG_MESSAGE(DEBUG_TYPE_INFO, "PTPd initialising...");

    /* Pass HAL function pointers to sys_time module */
    initTimeFunctions(functions);

    /* Pass NET semaphore to driver */
    ptpClock.netPath.ptpTxNotify = functions->ptpTxNotify;

    // Create the alert queue mailbox.
    if(sys_mbox_new(&ptpClock.timerAlerts, 16) != ERR_OK) {
        DEBUG_MESSAGE(DEBUG_TYPE_INFO, "Could not create alert queue");
        return;
    }
    if(sys_mbox_new(&ptpClock.packetAlerts, 16) != ERR_OK) {
        DEBUG_MESSAGE(DEBUG_TYPE_INFO, "Could not create alert queue");
        return;
    }

    // Create the PTP daemon thread.
    sys_thread_new("ptpd", ptpd_thread, NULL, 1024, priority);
}
