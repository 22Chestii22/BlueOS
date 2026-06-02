#include "types.h"
#include "string.h"
#include "screen.h"
#include "idt.h"
#include "serial.h"
#include "io.h"
#include "fb.h"

extern void timer_isr(void);

void* irq_routines[16];

extern uint64_t isr_stub_table[];

typedef struct
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

#define IDT_FLAG_PRESENT    0x80
#define IDT_FLAG_RING0      0x00
#define IDT_FLAG_RING3      0x60
#define IDT_FLAG_INTERRUPT  0x0E
#define IDT_FLAG_TRAP       0x0F

static void idt_set_entry(int num, uint64_t base, uint16_t selector, uint8_t flags)
{
    idt[num].offset_low = base & 0xFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].offset_mid = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].reserved = 0;
}

static const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
};

static uint64_t read_cr2(void)
{
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}



static uint64_t read_cr0(void)
{
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}
static uint64_t read_cr3(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}
static uint64_t read_cr4(void)
{
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    return cr4;
}
static uint64_t read_cs(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cs, %0" : "=r"(v));
    return v;
}
static uint64_t read_ss(void)
{
    uint64_t v;
    __asm__ volatile("mov %%ss, %0" : "=r"(v));
    return v;
}
static uint64_t read_ds(void)
{
    uint64_t v;
    __asm__ volatile("mov %%ds, %0" : "=r"(v));
    return v;
}
static uint64_t read_es(void)
{
    uint64_t v;
    __asm__ volatile("mov %%es, %0" : "=r"(v));
    return v;
}

void isr_handler(int num, uint64_t error_code, uint64_t rip)
{
    serial_write("EXC ");
    serial_dec(num);
    serial_write(" err=");
    serial_hex(error_code);
    serial_write(" rip=");
    serial_hex(rip);
    serial_write(" cs=");
    serial_hex(read_cs());
    serial_write(" ss=");
    serial_hex(read_ss());
    serial_write(" ds=");
    serial_hex(read_ds());
    serial_write(" es=");
    serial_hex(read_es());
    serial_write(" cr0=");
    serial_hex(read_cr0());
    serial_write(" cr3=");
    serial_hex(read_cr3());
    serial_write(" cr4=");
    serial_hex(read_cr4());
    if (num == 14)
    {
        serial_write(" cr2=");
        serial_hex(read_cr2());
    }
    serial_write("\n");

    if (num == 14)
    {
        uint64_t cr2 = read_cr2();
        uint64_t cr3_val = read_cr3();
        serial_write("PML4 dump for CR2:\n");
        uint64_t* pml4 = (uint64_t*)cr3_val;
        uint64_t pml4e = (cr2 >> 39) & 0x1FF;
        serial_write("  PML4["); serial_dec(pml4e); serial_write("]="); serial_hex(pml4[pml4e]); serial_write("\n");
        if (pml4[pml4e] & 1) {
            uint64_t* pdpt = (uint64_t*)(pml4[pml4e] & ~0xFFF);
            uint64_t pdpte = (cr2 >> 30) & 0x1FF;
            serial_write("  PDPT["); serial_dec(pdpte); serial_write("]="); serial_hex(pdpt[pdpte]); serial_write("\n");
            if (pdpt[pdpte] & 1) {
                uint64_t* pd = (uint64_t*)(pdpt[pdpte] & ~0xFFF);
                uint64_t pde = (cr2 >> 21) & 0x1FF;
                serial_write("  PD["); serial_dec(pde); serial_write("]="); serial_hex(pd[pde]); serial_write("\n");
                if ((pd[pde] & 1) && !(pd[pde] & (1ULL << 7))) {
                    uint64_t* pt = (uint64_t*)(pd[pde] & ~0xFFF);
                    uint64_t pte_i = (cr2 >> 12) & 0x1FF;
                    serial_write("  PT["); serial_dec(pte_i); serial_write("]="); serial_hex(pt[pte_i]); serial_write("\n");
                } else if (pd[pde] & (1ULL << 7)) {
                    serial_write("  (2MB huge page)\n");
                }
            }
        }
    }

    fb_bsod_panic(num, error_code, rip);

    screen_set_color(COLOR_WHITE, COLOR_BLUE);
    screen_clear();
    screen_set_color(COLOR_WHITE, COLOR_BLUE);

    int y = 2;

    screen_set_cursor(VGA_WIDTH / 2 - 1, y);
    printf(":(");

    y = 5;
    screen_set_cursor(2, y);
    printf("Your PC ran into a problem and needs to restart.");

    y = 6;
    screen_set_cursor(2, y);
    printf("We'll restart for you after collecting some error info.");

    y = 8;
    screen_set_cursor(2, y);
    printf("*** STOP: 0x000000%02x", num);
    if (num < 22)
    {
        screen_set_cursor(2, y);
        printf("*** STOP: 0x000000%02x  (%s)", num, exception_messages[num]);
    }

    y = 10;
    if (num == 14)
    {
        screen_set_cursor(2, y);
        printf("***       CR2=0x%x  Error=0x%x", read_cr2(), error_code);
    }
    else
    {
        screen_set_cursor(2, y);
        printf("***       Error=0x%x", error_code);
    }

    y = 11;
    screen_set_cursor(2, y);
    printf("***       RIP=0x%x  CS=0x%x  SS=0x%x", rip, read_cs(), read_ss());

    y = 12;
    screen_set_cursor(2, y);
    printf("***       RFLAGS=0x%x  CR3=0x%x", read_cr0() & 0xFFFF, read_cr3());

    y = 14;
    screen_set_cursor(2, y);
    printf("For more information, visit: blueos.local/stop");

    y = VGA_HEIGHT - 2;
    screen_set_cursor(VGA_WIDTH / 2 - 13, y);
    printf("Press any key to restart...");

    __asm__ volatile("cli; hlt");
    for (;;)
        __asm__ volatile("cli; hlt");
}

void irq_handler(int num)
{
    if (num == 39)
    {
        outb(0x20, 0x0B);
        if (!(inb(0x20) & 0x80))
            return;
    }
    if (num == 47)
    {
        outb(0xA0, 0x0B);
        if (!(inb(0xA0) & 0x80))
        {
            outb(0x20, 0x20);
            return;
        }
    }

    if (num >= 40)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void irq_install_handler(int irq, void (*handler)(void))
{
    extern void* irq_routines[16];
    irq_routines[irq] = (void*)handler;
}

void idt_init(void)
{
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base = (uint64_t)&idt;

    for (int i = 0; i < 256; i++)
        idt_set_entry(i, isr_stub_table[i], 0x08, 0x8E);

    idt[8].ist = 1;
    // idt[13].ist = 2;   // #GP uses IST - masks fault-time SS, keep default
    idt[14].ist = 3;   // #PF uses tss.ist[2]
    idt_set_entry(32, (uint64_t)timer_isr, 0x08, 0x8E);

    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x00);
    outb(0xA1, 0x00);

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
    __asm__ volatile("sti");
}
