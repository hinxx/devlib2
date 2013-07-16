/*************************************************************************\
* Copyright (c) 2013 Brookhaven Science Associates, as Operator of
*     Brookhaven National Laboratory.
* Copyright (c) 2013 Los Alamos National Security LLC, as Operator of
      Los Alamos National Laboratory.
* devlib2 is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/
/*
 * Author: Michael Davidsaver <mdavisavero@bnl.gov>
 */

#include <stdlib.h>
#include <stdio.h>
#include <epicsAssert.h>
#include <epicsFindSymbol.h>

#include <vxWorks.h>
#include <types.h>
#include <sysLib.h>
#include <intLib.h>
#include <iv.h>
#include <drv/pci/pciIntLib.h>
#include <vxLib.h>

#include <dbDefs.h>

#include "devLibPCIImpl.h"
#include "osdPciShared.h"

#if defined(VXPCIINTOFFSET)
/* do nothing */

#elif defined(INT_NUM_IRQ0)
#define VXPCIINTOFFSET INT_NUM_IRQ0

#elif defined(INT_VEC_IRQ0)
#define VXPCIINTOFFSET INT_VEC_IRQ0

#else
#define VXPCIINTOFFSET 0

#endif


typedef int (*sysBusToLocal_t) (int, char*, char**);

static sysBusToLocal_t  CallSysBusToLocalAdrs;
static unsigned char*   inumTable     = NULL;
static int              inumTableSize = 0;


/*=====================
 * dummySysBusToLocalAdrs()
 *
 * Dummy routine for BSPs that do not implement sysBusToLocalAdrs.
 * Just returns the bus address as the local address.
 *
 */
static
int dummySysBusToLocalAdrs (
    int     adrsSpace,
    char*   busAdrs,
    char**  pLocalAdrs)
{
    *pLocalAdrs = busAdrs;
    return 0;

}/*end dummySysBusToLocalAdrs()*/

/*=====================
 * vxworksDevPCIInit()
 *
 * PCI library initialization for vxWorks
 *
 */
static
int vxworksDevPCIInit (void) {
    int              status;
    unsigned char**  pTable;
    int*             pTableSize;

   /*---------------------
    * Invoke the common PCI initialization code
    */
    if ((status = sharedDevPCIInit()))
        return status;

    CallSysBusToLocalAdrs = epicsFindSymbol ("sysBusToLocalAdrs");
    if (!CallSysBusToLocalAdrs)
        CallSysBusToLocalAdrs = &dummySysBusToLocalAdrs;

   /*---------------------
    * See if this BSP uses a translation table to convert IRQ values
    * into interrupt indices.
    */
    pTable = epicsFindSymbol ("sysInumTbl");
    if (!pTable || !*pTable)
        return 0;

    inumTable = *pTable;

   /*---------------------
    * If the BSP does use translation table, get the number of entries
    */
    pTableSize = epicsFindSymbol ("sysInumTblNumEnt");
    if (!pTableSize) {
        printf ("WARNING: Interrupt number translation table is empty\n");
        return 0;
    }

    inumTableSize = *pTableSize;
    return 0;
}

/*=====================
 * vxworksDevPCIConnectInterrupt()
 *
 * Connect ISR to its PCI interrupt vector.
 *
 */
static
int vxworksDevPCIConnectInterrupt(
  const epicsPCIDevice *dev,
  void (*pFunction)(void *),
  void  *parameter,
  unsigned int opt
)
{
    struct osdPCIDevice *osd=pcidev2osd(dev);
    int status;

   /*---------------------
    * Get the IRQ number from the PCI device structure
    * and use it to get the interrupt number from the translation table
    * (if there is one). If there is no translation table, or the table
    * is too small, just use the irq as the interrupt number.
    */
    unsigned char irq = osd->dev.irq;
    if (inumTableSize && (irq < inumTableSize))
        irq = inumTable[(int)irq];

   /*---------------------
    * Attempt to connect the interrupt vector.  Abort on failure.
    */
    status=pciIntConnect ((VOIDFUNCPTR *)INUM_TO_IVEC(VXPCIINTOFFSET+irq),
                          pFunction, (int)parameter);
    if(status)
        return S_dev_vecInstlFail;

    return 0;
}

/*=====================
 * vxworksDevPCIDisconnectInterrupt()
 *
 * Disconnect ISR from its PCI interrupt vector.
 *
 */
static
int vxworksDevPCIDisconnectInterrupt(
  const epicsPCIDevice *dev,
  void (*pFunction)(void *),
  void  *parameter
)
{
    int status;
    struct osdPCIDevice *osd=pcidev2osd(dev);

   /*---------------------
    * Get the IRQ number from the PCI device structure
    * and use it to get the interrupt number from the translation table
    * (if there is one). If there is no translation table, or the table
    * is too small, jut use the irq as the interrupt number.
    */
    unsigned char irq = osd->dev.irq;
    if (inumTableSize && (irq < inumTableSize))
        irq = inumTable[(int)irq];

#ifdef VXWORKS_PCI_OLD

    status=pciIntDisconnect((void*)INUM_TO_IVEC(VXPCIINTOFFSET + irq),
                            pFunction);

#else

    status=pciIntDisconnect2((void*)INUM_TO_IVEC(VXPCIINTOFFSET + irq),
                             pFunction, (int)parameter);

#endif

    if(status)
        return S_dev_intDisconnect;

    return 0;
}

/*=====================
 * vxworksPCIToLocalAddr
 *
 * Return base address for specified device.
 *
 */
static
int vxworksPCIToLocalAddr(const epicsPCIDevice* dev,
                          unsigned int bar, volatile void **loc,
                          unsigned int o)
{
  int ret, space=0;
  volatile void *pci;

  ret=sharedDevPCIToLocalAddr(dev, bar, &pci, o);

  if(ret)
    return ret;

#if defined(PCI_SPACE_MEMIO_PRI)
  if(!dev->bar[bar].ioport) {
    space = PCI_SPACE_MEMIO_PRI;
  }
#endif

#if defined(PCI_SPACE_IO_PRI)
  if(dev->bar[bar].ioport) {
    space = PCI_SPACE_IO_PRI;
  }
#endif

  if(space) {
    if(CallSysBusToLocalAdrs(space, (char*)pci, (char**)loc))
      return -1;
  } else {
    *loc=pci;
  }

  return 0;
}

/*
 *
 * Function dispatch table 
 *
 */
devLibPCI pvxworksPCI = {
  "native",
  vxworksDevPCIInit, NULL,
  sharedDevPCIFindCB,
  vxworksPCIToLocalAddr,
  sharedDevPCIBarLen,
  vxworksDevPCIConnectInterrupt,
  vxworksDevPCIDisconnectInterrupt
};
#include <epicsExport.h>

/*=====================
 *
 * Export the registration routine 
 *
 */
void devLibPCIRegisterBaseDefault(void)
{
    devLibPCIRegisterDriver(&pvxworksPCI);
}
epicsExportRegistrar(devLibPCIRegisterBaseDefault);
