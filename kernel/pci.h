#ifndef PCI_H
#define PCI_H

#include "types.h"

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

#define PCI_VENDOR_RTL8139   0x10EC
#define PCI_DEVICE_RTL8139   0x8139

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar0;
    uint32_t bar1;
    uint8_t irq;
} pci_device_t;

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
int pci_scan_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out);
void pci_enable_bus_mastering(pci_device_t* dev);

#endif