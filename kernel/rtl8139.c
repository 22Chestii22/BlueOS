#include "rtl8139.h"
#include "pci.h"
#include "io.h"
#include "mem.h"
#include "string.h"
#include "screen.h"
#include "paging.h"

rtl8139_t rtl8139_dev;

static uint8_t rtl_inb(uint16_t reg)
{
    return inb(rtl8139_dev.iobase + reg);
}

static uint16_t rtl_inw(uint16_t reg)
{
    return inw(rtl8139_dev.iobase + reg);
}

static void rtl_outb(uint16_t reg, uint8_t val)
{
    outb(rtl8139_dev.iobase + reg, val);
}

static void rtl_outw(uint16_t reg, uint16_t val)
{
    outw(rtl8139_dev.iobase + reg, val);
}

static void rtl_outl(uint16_t reg, uint32_t val)
{
    outl(rtl8139_dev.iobase + reg, val);
}

int rtl8139_init(pci_device_t* pci)
{
    memset(&rtl8139_dev, 0, sizeof(rtl8139_t));

    uint32_t bar0 = pci->bar0;
    if (bar0 & 1)
        rtl8139_dev.iobase = (uint16_t)(bar0 & ~3);
    else
        rtl8139_dev.iobase = (uint16_t)(bar0 & ~0xF);

    screen_write("RTL8139: IOBASE=0x");
    screen_write_hex(rtl8139_dev.iobase);
    screen_write("\n");

    pci_enable_bus_mastering(pci);

    rtl_outb(RTL8139_CR, RTL8139_CR_RST);
    while (rtl_inb(RTL8139_CR) & RTL8139_CR_RST);

    for (int i = 0; i < 6; i++)
    {
        rtl8139_dev.mac[i] = rtl_inb(RTL8139_MAC_OFFSET + i);
    }

    screen_write("RTL8139: MAC ");
    for (int i = 0; i < 6; i++)
    {
        screen_write_hex(rtl8139_dev.mac[i]);
        if (i < 5) screen_write(":");
    }
    screen_write("\n");

    {
        int rx_pages = (RX_BUF_SIZE + 16 + 0xFFF) / 0x1000;
        static uint64_t rx_vaddr = 0x4000000;
        uint64_t first_paddr = 0;
        for (int i = 0; i < rx_pages; i++)
        {
            uint64_t paddr = paging_alloc_frame();
            if (paddr == 0xFFFFFFFF)
            {
                screen_write("RTL8139: RX page alloc failed\n");
                return -1;
            }
            if (i == 0) first_paddr = paddr;
            map_page_cr3(kernel_cr3, rx_vaddr + i * 0x1000, paddr, 0x03);
        }
        rtl8139_dev.rx_buffer = (uint8_t*)rx_vaddr;
        rx_vaddr += rx_pages * 0x1000;
        uint32_t rx_phys = (uint32_t)first_paddr;
        screen_write("RTL8139: RX buf phys=0x");
        screen_write_hex(rx_phys);
        screen_write(" virt=0x");
        screen_write_hex((uint32_t)(unsigned long)rtl8139_dev.rx_buffer);
        screen_write("\n");
        rtl_outl(RTL8139_RBSTART, rx_phys);
    }

    rtl_outw(RTL8139_IMR, RTL8139_ISR_ROK | RTL8139_ISR_TOK | RTL8139_ISR_RER | RTL8139_ISR_TER);

    rtl_outl(RTL8139_RCR, RTL8139_RCR_AAP | RTL8139_RCR_APM | RTL8139_RCR_AM | RTL8139_RCR_AB);

    rtl_outb(RTL8139_CR, RTL8139_CR_RE | RTL8139_CR_TE);

    screen_write("RTL8139: Init complete\n");
    return 0;
}

int rtl8139_send(const void* data, int len)
{
    if (!rtl8139_dev.iobase) return -1;
    if (len < 14 || len > 1792) return -1;

    uint32_t phys = (uint32_t)(unsigned long)data;
    rtl_outl(RTL8139_TSAD0, phys);
    rtl_outl(RTL8139_TSD0, len);

    while (!(rtl_inw(RTL8139_ISR) & RTL8139_ISR_TOK));

    return len;
}

int rtl8139_recv(void* buffer, int max_len)
{
    if (!rtl8139_dev.iobase) return -1;

    uint16_t rx_status = *(volatile uint16_t*)(rtl8139_dev.rx_buffer + rtl8139_dev.rx_offset);
    if (!(rx_status & 0x8000)) return 0;

    uint16_t rx_len = *(volatile uint16_t*)(rtl8139_dev.rx_buffer + rtl8139_dev.rx_offset + 2) - 4;

    int copy_len = rx_len < max_len ? rx_len : max_len;
    memcpy(buffer, rtl8139_dev.rx_buffer + rtl8139_dev.rx_offset + 4, copy_len);

    rtl8139_dev.rx_offset = (rtl8139_dev.rx_offset + rx_len + 4 + 3) & ~3;
    rtl8139_dev.rx_offset &= RX_BUF_MASK;

    rtl_outw(RTL8139_CAPR, rtl8139_dev.rx_offset - 16);

    return copy_len;
}