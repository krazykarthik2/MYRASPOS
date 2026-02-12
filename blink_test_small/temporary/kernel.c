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
void fill_screen(unsigned int color1,unsigned int color2);

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

    // Tag 1: Set physical width/height
    mbox[2]  = 0x48003;
    mbox[3]  = 8;
    mbox[4]  = 0;
    mbox[5]  = 1024;
    mbox[6]  = 768;

    // Tag 2: Set virtual width/height
    mbox[7]  = 0x48004;
    mbox[8]  = 8;
    mbox[9]  = 0;
    mbox[10] = 1024;
    mbox[11] = 768;

    // Tag 3: Set depth
    mbox[12] = 0x48005;
    mbox[13] = 4;
    mbox[14] = 0;
    mbox[15] = 32;

    // Tag 4: Set pixel order (optional but good for consistency)
    mbox[16] = 0x48006;
    mbox[17] = 4;
    mbox[18] = 0;
    mbox[19] = 1; // RGB

    // Tag 5: Allocate framebuffer
    mbox[20] = 0x40001;
    mbox[21] = 8;
    mbox[22] = 0;
    mbox[23] = 16; // Alignment
    mbox[24] = 0;

    // Tag 6: Get pitch
    mbox[25] = 0x40008;
    mbox[26] = 4;
    mbox[27] = 0;
    mbox[28] = 0;

    mbox[29] = 0; // End tag

    if (!mbox_call(MBOX_CH_PROP, mbox))
        return 0;

    // Use returned values
    fb_width    = mbox[5];
    fb_height   = mbox[6];
    fb_pitch    = mbox[28];
    framebuffer = (unsigned int*)((unsigned long)mbox[23] & 0x3FFFFFFF);

    return (framebuffer != 0);
}
void fill_screen(unsigned int color1,unsigned int color2){
    unsigned int block = 32; // size of each square (32x32 pixels)

    for (unsigned int y = 0; y < fb_height; y++)
    {
        unsigned int *row = (unsigned int*)((unsigned long)framebuffer + y * fb_pitch);

        for (unsigned int x = 0; x < fb_width; x++)
        {
            unsigned int x_block = x / block;
            unsigned int y_block = y / block;

            if ((x_block + y_block) % 2 == 0)
                row[x] = color1;  // White (ARGB)
            else
                row[x] = color2;  // Black (ARGB)
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

#define COLOR_RED 0xFF0000FF
#define COLOR_GREEN 0xFF00FF00
#define COLOR_BLUE 0xFFFF0000
#define COLOR_WHITE 0xFFFFFFFF
#define COLOR_BLACK 0x00000000
void kernel_main()
{
    gpio_init_uart();
    gpio_init_led();
    set_uart_clock();
    uart_init();

     for(int i=0;i<3;i++)// three blinks to indicate the kernel is loaded
    {
        GPSET0 = (1 << 16);
        delay(1000000); //1 sec
        GPCLR0 = (1 << 16);
        delay(1000000); //1 sec
    }
if (framebuffer_init())
{
    
        fill_screen(COLOR_RED,COLOR_GREEN); // repeating pattern 
    // fast blink = success
    for(int i=20;i>0;i--)
    {
        GPSET0 = (1 << 16);
        delay(500000); //half sec
        GPCLR0 = (1 << 16);
        delay(500000); //half sec
    }
    fill_screen(COLOR_RED,COLOR_GREEN);
     // fast blink = success
    for(int i=20;i>0;i--)
    {
        GPSET0 = (1 << 16);
        delay(500000); //half sec
        GPCLR0 = (1 << 16);
        delay(500000); //half sec
    }
    fill_screen(COLOR_GREEN,COLOR_BLUE);
     // fast blink = success
    for(int i=20;i>0;i--)
    {
        GPSET0 = (1 << 16);
        delay(500000); //half sec 
        GPCLR0 = (1 << 16);
        delay(500000); //half sec
    }
    fill_screen(COLOR_BLACK,COLOR_WHITE);
    
}
else
{
    // slow blink = failure
    while (1)
    {
        GPSET0 = (1 << 16);
        delay(1000000); //1 sec
        GPCLR0 = (1 << 16);
        delay(1000000); //1 sec
    }
}

}
