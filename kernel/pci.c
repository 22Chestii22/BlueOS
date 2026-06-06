#include "pci.h"
#include "io.h"
#include "screen.h"

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

int pci_scan_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out)
{
    for (int bus = 0; bus < 256; bus++)
    {
        for (int slot = 0; slot < 32; slot++)
        {
            for (int func = 0; func < 8; func++)
            {
                uint32_t id = pci_config_read(bus, slot, func, 0);
                if (id == 0xFFFFFFFF) continue;

                uint16_t v = (uint16_t)(id & 0xFFFF);
                uint16_t d = (uint16_t)(id >> 16);

                if (v == vendor_id && d == device_id)
                {
                    out->vendor_id = v;
                    out->device_id = d;
                    out->bus = bus;
                    out->slot = slot;
                    out->func = func;

                    uint32_t class_reg = pci_config_read(bus, slot, func, 8);
                    out->class_code = (uint8_t)(class_reg >> 24);
                    out->subclass = (uint8_t)(class_reg >> 16);
                    out->prog_if = (uint8_t)(class_reg >> 8);

                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    out->bar0 = bar0;
                    out->bar1 = pci_config_read(bus, slot, func, 0x14);

                    uint32_t irq_reg = pci_config_read(bus, slot, func, 0x3C);
                    out->irq = (uint8_t)(irq_reg & 0xFF);

                    screen_write("PCI: Found RTL8139 at ");
                    screen_write_dec(bus);
                    screen_write(":");
                    screen_write_dec(slot);
                    screen_write(".");
                    screen_write_dec(func);
                    screen_write(" BAR0=0x");
                    screen_write_hex(bar0);
                    screen_write(" IRQ=");
                    screen_write_dec(out->irq);
                    screen_write("\n");

                    return 1;
                }

                if (func == 0)
                {
                    uint32_t hdr = pci_config_read(bus, slot, func, 0x0C);
                    if (!(hdr & 0x800000)) break;
                }
            }
        }
    }
    return 0;
}

void pci_enable_bus_mastering(pci_device_t* dev)
{
    uint32_t cmd = pci_config_read(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= 0x04;
    cmd |= 0x02;
    pci_config_write(dev->bus, dev->slot, dev->func, 0x04, cmd);
}