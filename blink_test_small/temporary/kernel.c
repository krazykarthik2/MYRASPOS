#define MMIO_BASE 0x3F000000

void delay(int count);
void gpio_init_uart(void);
void gpio_init_led(void);
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char* s);
void kernel_main(void);
int mbox_call(unsigned char ch, volatile unsigned int *buffer);
void set_uart_clock(void);
int framebuffer_init(void);
void fill_screen(unsigned int color);

#define GPIO_BASE   (MMIO_BASE + 0x200000)
#define GPFSEL1     (*(volatile unsigned int*)(GPIO_BASE + 0x04))
#define GPSET0      (*(volatile unsigned int*)(GPIO_BASE + 0x1C))
#define GPCLR0      (*(volatile unsigned int*)(GPIO_BASE + 0x28))

#define GPPUD       (*(volatile unsigned int*)(GPIO_BASE + 0x94))
#define GPPUDCLK0   (*(volatile unsigned int*)(GPIO_BASE + 0x98))

#define UART0_BASE  (MMIO_BASE + 0x201000)

#define UART0_DR    (*(volatile unsigned int*)(UART0_BASE + 0x00))
#define UART0_FR    (*(volatile unsigned int*)(UART0_BASE + 0x18))
#define UART0_IBRD  (*(volatile unsigned int*)(UART0_BASE + 0x24))
#define UART0_FBRD  (*(volatile unsigned int*)(UART0_BASE + 0x28))
#define UART0_LCRH  (*(volatile unsigned int*)(UART0_BASE + 0x2C))
#define UART0_CR    (*(volatile unsigned int*)(UART0_BASE + 0x30))
#define UART0_ICR   (*(volatile unsigned int*)(UART0_BASE + 0x44))

/* Mailbox */
#define MBOX_BASE   (MMIO_BASE + 0xB880)
#define MBOX_READ   (*(volatile unsigned int*)(MBOX_BASE + 0x00))
#define MBOX_STATUS (*(volatile unsigned int*)(MBOX_BASE + 0x18))
#define MBOX_WRITE  (*(volatile unsigned int*)(MBOX_BASE + 0x20))

#define MBOX_EMPTY  0x40000000
#define MBOX_FULL   0x80000000
#define MBOX_CH_PROP 8

volatile unsigned int __attribute__((aligned(16))) mbox[36];

unsigned int *framebuffer;
unsigned int fb_width;
unsigned int fb_height;
unsigned int fb_pitch;

void delay(int count) {
    while(count--) { asm volatile("nop"); }
}

int mbox_call(unsigned char ch, volatile unsigned int *buffer)
{
    unsigned int r = ((unsigned int)((unsigned long)buffer) & ~0xF) | (ch & 0xF);

    while (MBOX_STATUS & MBOX_FULL);
    MBOX_WRITE = r;

    while (1) {
        while (MBOX_STATUS & MBOX_EMPTY);
        if (MBOX_READ == r)
            return buffer[1] == 0x80000000;
    }
}

void set_uart_clock()
{
    mbox[0] = 9*4;
    mbox[1] = 0;
    mbox[2] = 0x38002;   // set clock rate tag
    mbox[3] = 12;
    mbox[4] = 8;
    mbox[5] = 2;         // UART clock id
    mbox[6] = 48000000;  // 48 MHz
    mbox[7] = 0;
    mbox[8] = 0;

    mbox_call(MBOX_CH_PROP, mbox);
}

int framebuffer_init()
{
    mbox[0] = 35 * 4;
    mbox[1] = 0;

    // Set physical width/height
    mbox[2]  = 0x48003;
    mbox[3]  = 8;
    mbox[4]  = 8;
    mbox[5]  = 1024;
    mbox[6]  = 768;

    // Set virtual width/height
    mbox[7]  = 0x48004;
    mbox[8]  = 8;
    mbox[9]  = 8;
    mbox[10] = 1024;
    mbox[11] = 768;

    // Set depth
    mbox[12] = 0x48005;
    mbox[13] = 4;
    mbox[14] = 4;
    mbox[15] = 32;

    // Allocate framebuffer
    mbox[16] = 0x40001;
    mbox[17] = 8;
    mbox[18] = 4;
    mbox[19] = 16;
    mbox[20] = 0;

    // Get pitch
    mbox[21] = 0x40008;
    mbox[22] = 4;
    mbox[23] = 4;
    mbox[24] = 0;

    mbox[25] = 0;

    if (!mbox_call(MBOX_CH_PROP, mbox))
        return 0;

    framebuffer = (unsigned int*)((unsigned long)mbox[19] & 0x3FFFFFFF);
    fb_pitch    = mbox[24];
    fb_width    = 1024;
    fb_height   = 768;

    return 1;
}

void fill_screen(unsigned int color)
{
    for (unsigned int y = 0; y < fb_height; y++)
    {
        unsigned int *row = (unsigned int*)((unsigned long)framebuffer + y * fb_pitch);
        for (unsigned int x = 0; x < fb_width; x++)
        {
            row[x] = color;
        }
    }
}

void gpio_init_uart()
{
    // GPIO14,15 ALT0 (PL011)
    GPFSEL1 &= ~((7 << 12) | (7 << 15));
    GPFSEL1 |=  (4 << 12) | (4 << 15);

    GPPUD = 0;
    delay(150);
    GPPUDCLK0 = (1 << 14) | (1 << 15);
    delay(150);
    GPPUDCLK0 = 0;
}

void gpio_init_led()
{
    // GPIO16 is in GPFSEL1 bits 18–20
    GPFSEL1 &= ~(7 << 18);
    GPFSEL1 |=  (1 << 18);   // set as output
}

void uart_init()
{
    UART0_CR = 0;
    UART0_ICR = 0x7FF;

    // 48MHz / (16 * 115200) = 26.041
    UART0_IBRD = 26;
    UART0_FBRD = 3;

    UART0_LCRH = (3 << 5);   // 8 bit, no parity, 1 stop
    UART0_CR = (1 << 9) | (1 << 8) | 1; // TX, RX, UART enable
}

void uart_putc(char c)
{
    while (UART0_FR & (1 << 5));
    UART0_DR = c;
}

void uart_puts(const char* s)
{
    while (*s)
    {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

void kernel_main()
{
    gpio_init_uart();
    gpio_init_led();
    set_uart_clock();
    uart_init();

    if (framebuffer_init())
    {
        fill_screen(0x00FFFFFF);   // White
    }

    uart_puts("PL011 AND FRAMEBUFFER READY\r\n");

    while (1)
    {
        GPSET0 = (1 << 16);   // LED ON
        delay(500000);

        GPCLR0 = (1 << 16);   // LED OFF
        delay(500000);
    }
}
