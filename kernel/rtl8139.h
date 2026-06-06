#ifndef RTL8139_H
#define RTL8139_H

#include "types.h"
#include "pci.h"

#define RTL8139_MAC_OFFSET      0x00
#define RTL8139_MAR0            0x08
#define RTL8139_MAR4            0x0C
#define RTL8139_TSD0            0x10
#define RTL8139_TSD1            0x14
#define RTL8139_TSD2            0x18
#define RTL8139_TSD3            0x1C
#define RTL8139_TSAD0           0x20
#define RTL8139_TSAD1           0x24
#define RTL8139_TSAD2           0x28
#define RTL8139_TSAD3           0x2C
#define RTL8139_RBSTART         0x30
#define RTL8139_CR              0x37
#define RTL8139_CAPR            0x38
#define RTL8139_CBR             0x3A
#define RTL8139_IMR             0x3C
#define RTL8139_ISR             0x3E
#define RTL8139_TCR             0x40
#define RTL8139_RCR             0x44
#define RTL8139_9346CR          0x50
#define RTL8139_CONFIG1         0x52
#define RTL8139_MSR             0x58

#define RTL8139_CR_RST          0x10
#define RTL8139_CR_RE           0x08
#define RTL8139_CR_TE           0x04
#define RTL8139_CR_BUFE         0x01

#define RTL8139_RCR_AAP         0x01
#define RTL8139_RCR_APM         0x02
#define RTL8139_RCR_AM          0x04
#define RTL8139_RCR_AB          0x08
#define RTL8139_RCR_AR          0x10
#define RTL8139_RCR_WRAP        0x80

#define RTL8139_ISR_ROK         0x01
#define RTL8139_ISR_TOK         0x04
#define RTL8139_ISR_RER         0x10
#define RTL8139_ISR_TER         0x20

#define RX_BUF_SIZE             8192
#define RX_BUF_MASK             (RX_BUF_SIZE - 1)

typedef struct {
    uint16_t iobase;
    uint8_t mac[6];
    uint8_t* rx_buffer;
    int rx_buffer_phys;
    int rx_offset;
} rtl8139_t;

extern rtl8139_t rtl8139_dev;

int rtl8139_init(pci_device_t* pci);
int rtl8139_send(const void* data, int len);
int rtl8139_recv(void* buffer, int max_len);

#endif