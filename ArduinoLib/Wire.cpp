/* Arduino Wire.cpp implemented for Linux and FreeBSD.
 * This is not meant to be comprehensive, only what we need for now.
 * Compiles on any UNIX but methods all return 0 unless _IIC_LINUX or _IIC_FREEBSD are defined.
 * linux info: https://www.kernel.org/doc/Documentation/i2c/dev-interface
 * freebsd info: man iic
 */


#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "Arduino.h"
#include "Wire.h"



/* the traditional global Wire object
 */
TwoWire Wire;




#if defined (_IIC_LINUX)

/*******************************************************************************************
 *
 * FreeBSD
 *
 *******************************************************************************************/


#include <linux/i2c.h>
#include <linux/i2c-dev.h>


static bool verbose = false;


/* constructor
 * _IIC_LINUX
 */
TwoWire::TwoWire()
{
        memset (rxdata, 0, sizeof(rxdata));
        memset (txdata, 0, sizeof(txdata));
        i2c_fd = -1;
        dev_addr = 0;
        n_txdata = 0;
        n_rxdata = 0;
        n_retdata = 0;
        transmitting = false;
}

/* destructor
 * _IIC_LINUX
 */
TwoWire::~TwoWire()
{
        closeConnection();
}


/* open connection if not already
 * _IIC_LINUX
 */
bool TwoWire::openConnection()
{
	const char filename[] = "/dev/i2c-1";
        if (i2c_fd < 0) {
            i2c_fd = ::open(filename, O_RDWR);
            if (i2c_fd < 0)
                printf ("I2C: %s: %s\n", filename, strerror(errno));
            else if (verbose)
                printf ("I2C: %s open ok\n", filename);
        } else if (verbose)
            printf ("I2C: %s already open\n", filename);

        return (i2c_fd >= 0);
}



/* close connection
 * _IIC_LINUX
 */
void TwoWire::closeConnection()
{
        if (i2c_fd >= 0) {
            if (verbose)
                printf ("I2C: close\n");
            ::close (i2c_fd);
            i2c_fd = -1;
        }
}

/* set addr if different
 * _IIC_LINUX
 */
void TwoWire::setAddr (uint8_t addr)
{
        if (addr == dev_addr)
            return;

        dev_addr = addr;

        if (verbose)
            printf ("I2C: setAddr(0x%02X)\n", addr);

        if (ioctl(i2c_fd, I2C_SLAVE, dev_addr) < 0) {
            printf ("I2C: setAddr(0x%02X): %s\n", addr, strerror(errno));
            // mark as failed for subsequent use
            closeConnection ();
        } else if (verbose)
            printf ("I2C: setAddr(0x%02X) ok\n", addr);
}

/* start an I2C session
 * _IIC_LINUX
 */
void TwoWire::begin()
{
        (void) openConnection();
}


/* prepare to send bytes to the I2C slave at the given address
 * _IIC_LINUX
 */
void TwoWire::beginTransmission(uint8_t addr)
{
        // check for ready
        if (!openConnection()) {
            printf ("I2C: beginTransmission(0x%02X): driver not open\n", addr);
            return;
        }

        // insure correct addr
        setAddr(addr);

        // init
        if (verbose)
            printf ("I2C: beginTransmission(0x%02X)\n", addr);
        transmitting = true;
        n_txdata = 0;
}


/* buffer another byte to send.
 * returns number so buffered.
 * _IIC_LINUX
 */
size_t TwoWire::write(uint8_t datum)
{
        if (!transmitting) {
            printf ("I2C: write called but not transmitting\n");
            return (1);         // yes, this is what the real Wire.cpp does
        }

        if (verbose)
            printf ("I2C: write 0x%02X\n", datum);

        // buffer if more room
        if (n_txdata < MAX_TXBUF) {
            txdata[n_txdata++] = datum;
            return (1);
        } else {
            printf ("I2C: write buffer full\n");
            return (0);
        }
}




/* buffer more bytes to send.
 * returns number so buffered
 * _IIC_LINUX
 */
size_t TwoWire::write(const uint8_t *data, size_t quantity)
{
        if (transmitting) {
            if (verbose)
                printf ("I2C: transmitting %d\n", quantity);
            for(size_t i = 0; i < quantity; i++) {
                if(!write(data[i])) {
                    return (i);
                }
            }
        }

        return (quantity);
}


/* if sendStop is true, don't do anything, requestFrom() will send n_txdata.
 * if !sendStop then send all buffered bytes to the I2C device specified in beginTransmission() then STOP.
 * see twi_writeTO() for return codes:
 * return 0: ok
 * return 1: ?
 * return 2: received NACK on transmit of address
 * return 3: received NACK on transmit of data
 * return 4: line busy
 * _IIC_LINUX
 */
uint8_t TwoWire::endTransmission(bool sendStop)
{

        // check for ready
        if (!openConnection()) {
            printf ("I2C: endTransmission(): driver not open\n");
            return (4);
        }

        if (verbose)
            printf ("I2C: endTransmission: %d bytes\n", n_txdata);

        // null case
        if (n_txdata == 0)
            return (0);

        if (!sendStop)
            return (0); // feign success for now

        // send
        int nw = ::write (i2c_fd, txdata, n_txdata);
        bool ok = (nw == n_txdata);

        // check return
        if (!ok) {
            if (nw == 0)
                printf ("I2C: endTransmission() EOF, n %d\n", n_txdata);
            else if (nw < 0) {
                // printf ("I2C: endTransmission() n %d: %s\n", n_txdata, strerror(errno));
                closeConnection ();         // might reset the bus
            } else
                printf ("I2C: endTransmission() short: %d of %d\n", nw, n_txdata);
        }

        // regardless, we tried
        n_txdata = 0;

        // done
        transmitting = false;

        return (ok ? 0 : 1);
}



/* ask the I2C slave at the given address to send n bytes.
 * returns the actual number received.
 * N.B. if n_txdata > 0, we send that first without a STOP, then read
 * _IIC_LINUX
 */
uint8_t TwoWire::requestFrom(uint8_t addr, uint8_t nbytes)
{
        // check for ready
        if (!openConnection()) {
            printf ("I2C: requestFrom(): driver not open\n");
            return (0);
        }

        if (verbose)
            printf ("I2C: requestFrom %d bytes\n", nbytes);

        // clamp size
        if (nbytes > MAX_RXBUF) {
            printf ("I2C: requestFrom(0x%02X,%d) too many, clamping to %d\n", addr, nbytes, MAX_RXBUF);
            nbytes = MAX_RXBUF;
        }

        // insure correct addr
        setAddr(addr);

        // n read
        int nr;

        // send then recv without intermediate STOP if txdata still not sent
        if (n_txdata > 0) {

            struct i2c_rdwr_ioctl_data work_queue;
            struct i2c_msg msg[2];

            work_queue.nmsgs = 2;
            work_queue.msgs = msg;

            work_queue.msgs[0].addr = addr;
            work_queue.msgs[0].len = n_txdata;
            work_queue.msgs[0].flags = 0;   // write
            work_queue.msgs[0].buf = txdata;

            work_queue.msgs[1].addr = addr;
            work_queue.msgs[1].len = nbytes;
            work_queue.msgs[1].flags = I2C_M_RD;
            work_queue.msgs[1].buf = rxdata;

            if (ioctl(i2c_fd,I2C_RDWR,&work_queue) < 0) {
                printf ("I2C_RDWR failed: %s\n", strerror(errno));
                nr = 0;
            } else {
                nr = nbytes;
            }

            // did our best to send
            n_txdata = 0;

        } else {

            // null case
            if (nbytes == 0)
                return (0);

            // rx
            nr = ::read (i2c_fd, rxdata, nbytes);

            // check return
            if (nr < 0) {
                // printf ("I2C: requestFrom(0x%02X,%d): %s\n", addr, nbytes, strerror(errno));
                nr = 0;
                closeConnection ();         // might reset the bus
            } else if (nr == 0)
                printf ("I2C: requestFrom(0x%02X,%d) EOF\n", addr, nbytes);
            else if (nr < nbytes)
                printf ("I2C: requestFrom(0x%02X,%d) short: %d\n", addr, nbytes, nr);

        }

        // save
        n_rxdata = nr;

        // prep for reading
        n_retdata = 0;

        // report actual
        return (nr);
}


/* returns number of bytes available to read
 * _IIC_LINUX
 */
int TwoWire::available(void)
{
        return (n_rxdata);
}


/* returns the next byte received from an earlier requestFrom()
 * _IIC_LINUX
 */
int TwoWire::read(void)
{
        if (verbose)
            printf ("I2C: read returning 0x%02X %d/%d\n", rxdata[n_retdata], n_retdata+1, n_rxdata);

        // return in read order
        if (n_retdata < n_rxdata)
            return (rxdata[n_retdata++]);
        else
            return (0x99);
}


#elif defined (_IIC_FREEBSD)


/*******************************************************************************************
 *
 * FreeBSD
 *
 *******************************************************************************************/


#include <dev/iicbus/iic.h>


static bool verbose = false;


/* constructor
 * _IIC_FREEBSD
 */
TwoWire::TwoWire()
{
        memset (rxdata, 0, sizeof(rxdata));
        memset (txdata, 0, sizeof(txdata));
        i2c_fd = -1;
        dev_addr = 0;
        n_txdata = 0;
        n_rxdata = 0;
        n_retdata = 0;
        transmitting = false;
}

/* destructor
 * _IIC_FREEBSD
 */
TwoWire::~TwoWire()
{
        closeConnection();
}


/* open connection if not already
 * _IIC_FREEBSD
 */
bool TwoWire::openConnection()
{
	const char filename[] = "/dev/iic0";
        if (i2c_fd < 0) {
            i2c_fd = ::open(filename, O_RDWR);
            if (i2c_fd < 0)
                printf ("I2C: %s: %s\n", filename, strerror(errno));
            else if (verbose)
                printf ("I2C: %s open ok\n", filename);
        } else if (verbose)
            printf ("I2C: %s already open\n", filename);

        return (i2c_fd >= 0);
}



/* close connection
 * _IIC_FREEBSD
 */
void TwoWire::closeConnection()
{
        if (i2c_fd >= 0) {
            if (verbose)
                printf ("I2C: close\n");
            ::close (i2c_fd);
            i2c_fd = -1;
        }
}

/* set addr
 * _IIC_FREEBSD
 */
void TwoWire::setAddr (uint8_t addr)
{
        if (verbose)
            printf ("I2C: setting addr %02X\n", addr);
        dev_addr = addr << 1;
}

/* start an I2C session
 * _IIC_FREEBSD
 */
void TwoWire::begin()
{
        (void) openConnection();
}


/* prepare to send bytes to the I2C slave at the given address
 * _IIC_FREEBSD
 */
void TwoWire::beginTransmission(uint8_t addr)
{
        // check for ready
        if (!openConnection()) {
            printf ("I2C: beginTransmission(0x%02X): driver not open\n", addr);
            return;
        }

        // insure correct addr
        setAddr(addr);

        // init
        if (verbose)
            printf ("I2C: beginTransmission(0x%02X)\n", addr);
        transmitting = true;
        n_txdata = 0;
}


/* buffer another byte to send.
 * returns number so buffered.
 * _IIC_FREEBSD
 */
size_t TwoWire::write(uint8_t datum)
{
        if (!transmitting) {
            printf ("I2C: write called but not transmitting\n");
            return (1);         // yes, this is what the real Wire.cpp does
        }

        if (verbose)
            printf ("I2C: write 0x%02X\n", datum);

        // buffer if more room
        if (n_txdata < MAX_TXBUF) {
            txdata[n_txdata++] = datum;
            return (1);
        } else {
            printf ("I2C: write buffer full\n");
            return (0);
        }
}




/* buffer more bytes to send.
 * returns number so buffered
 * _IIC_FREEBSD
 */
size_t TwoWire::write(const uint8_t *data, size_t quantity)
{
        if (transmitting) {
            if (verbose)
                printf ("I2C: transmitting %d\n", (int)quantity);
            for(size_t i = 0; i < quantity; i++) {
                if(!write(data[i])) {
                    return (i);
                }
            }
        }

        return (quantity);
}


/* if sendStop is true, don't do anything, requestFrom() will send n_txdata.
 * if !sendStop then send all buffered bytes to the I2C device specified in beginTransmission() then STOP.
 * see twi_writeTO() for return codes:
 * return 0: ok
 * return 1: ?
 * return 2: received NACK on transmit of address
 * return 3: received NACK on transmit of data
 * return 4: line busy
 * _IIC_FREEBSD
 */
uint8_t TwoWire::endTransmission(bool sendStop)
{

        // check for ready
        if (!openConnection()) {
            printf ("I2C: endTransmission(): driver not open\n");
            return (4);
        }

        // printf ("I2C: endTransmission: %d bytes\n", n_txdata);

        // null case
        if (n_txdata == 0)
            return (0);

        if (!sendStop)
            return (0); // feign success for now

        struct iic_msg msgs[1];
        msgs[0].slave = dev_addr;
        msgs[0].flags = IIC_M_WR;
        msgs[0].len = n_txdata;
        msgs[0].buf = txdata;

        struct iic_rdwr_data work_queue;
        work_queue.msgs = msgs;
        work_queue.nmsgs = 1;

        bool ok = false;
        if (ioctl (i2c_fd, I2CRDWR, &work_queue) < 0) {
            printf ("I2C: endTransmission write %d failed: %s\n", n_txdata, strerror(errno));
        } else {
            if (verbose)
                printf ("I2C: endTransmission write %d ok\n", n_txdata);
            ok = true;
        }

        // regardless, we tried
        n_txdata = 0;

        // done
        transmitting = false;

        return (ok ? 0 : 1);
}



/* ask the I2C slave at the given address to send n bytes.
 * returns the actual number received.
 * N.B. if n_txdata > 0, we send that first without a STOP, then read
 * _IIC_FREEBSD
 */
uint8_t TwoWire::requestFrom(uint8_t addr, uint8_t nbytes)
{
        // check for ready
        if (!openConnection()) {
            printf ("I2C: requestFrom(): driver not open\n");
            return (0);
        }

        if (verbose)
            printf ("I2C: requestFrom %d bytes\n", nbytes);

        // clamp size
        if (nbytes > MAX_RXBUF) {
            printf ("I2C: requestFrom(0x%02X,%d) too many, clamping to %d\n", addr, nbytes, MAX_RXBUF);
            nbytes = MAX_RXBUF;
        }

        // insure correct addr
        setAddr(addr);

        // n read
        int nr;

        // send then recv without intermediate STOP if txdata still not sent
        if (n_txdata > 0) {

            struct iic_rdwr_data work_queue;
            struct iic_msg msg[2];

            work_queue.nmsgs = 2;
            work_queue.msgs = msg;

            work_queue.msgs[0].slave = dev_addr;
            work_queue.msgs[0].len = n_txdata;
            work_queue.msgs[0].flags = IIC_M_NOSTOP|IIC_M_WR;
            work_queue.msgs[0].buf = txdata;

            work_queue.msgs[1].slave = dev_addr;
            work_queue.msgs[1].len = nbytes;
            work_queue.msgs[1].flags = IIC_M_RD;
            work_queue.msgs[1].buf = rxdata;

            if (ioctl (i2c_fd, I2CRDWR, &work_queue) < 0) {
                printf ("I2C: requestFrom %d failed: %s\n", nbytes, strerror(errno));
                nr = 0;
            } else {
                nr = nbytes;
            }

            // did our best to send
            n_txdata = 0;

        } else {

            // null case
            if (nbytes == 0)
                return (0);

            struct iic_msg msgs[1];
            msgs[0].slave = dev_addr;
            msgs[0].flags = IIC_M_RD;
            msgs[0].len = nbytes;
            msgs[0].buf = rxdata;

            struct iic_rdwr_data work;
            work.msgs = msgs;
            work.nmsgs = 1;

            if (ioctl (i2c_fd, I2CRDWR, &work) < 0) {
                printf ("I2C: requestFrom read %d failed: %s\n", nbytes, strerror(errno));
                nr = 0;
            } else {
                if (verbose)
                    printf ("I2C: requestFrom read %d ok\n", nbytes);
                nr = nbytes;
            }

        }

        // save
        n_rxdata = nr;

        // prep for reading
        n_retdata = 0;

        // report actual
        return (nr);
}


/* returns number of bytes available to read
 * _IIC_FREEBSD
 */
int TwoWire::available(void)
{
        return (n_rxdata);
}


/* returns the next byte received from an earlier requestFrom()
 * _IIC_FREEBSD
 */
int TwoWire::read(void)
{
        if (verbose)
            printf ("I2C: read returning 0x%02X %d/%d\n", rxdata[n_retdata], n_retdata+1, n_rxdata);

        // return in read order
        if (n_retdata < n_rxdata)
            return (rxdata[n_retdata++]);
        else
            return (0x99);
}


#else

/*******************************************************************************************
 *
 * dummies
 *
 *******************************************************************************************/

TwoWire::TwoWire(void){};
TwoWire::~TwoWire(void){};
void TwoWire::begin(void){};
void TwoWire::beginTransmission(uint8_t){};
size_t TwoWire::write(uint8_t){return 0;};
size_t TwoWire::write(const uint8_t *data, size_t quantity){return 0;};
uint8_t TwoWire::endTransmission(bool sendStop){return 0;};
uint8_t TwoWire::requestFrom(uint8_t, uint8_t){return 0;};
int TwoWire::available(void){return 0;};
int TwoWire::read(void){return 0;};

#endif
