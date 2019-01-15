#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/usart.h>

static void clock_setup(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	rcc_periph_clock_enable(RCC_GPIOA);

	/* Enable clocks for GPIO port B (for GPIO_USART3_TX) and USART3. */
	rcc_periph_clock_enable(RCC_USART1);
}

static void usart_setup(void)
{
	/* Setup GPIO pin GPIO_USART1_TX. */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART1_TX);

	/* Setup UART parameters. */
	usart_set_baudrate(USART1, 38400);
	usart_set_databits(USART1, 8);
	usart_set_stopbits(USART1, USART_STOPBITS_1);
	usart_set_mode(USART1, USART_MODE_TX);
	usart_set_parity(USART1, USART_PARITY_NONE);
	usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

	/* Finally enable the USART. */
	usart_enable(USART1);
}

static void spi_setup(void)
{
	spi_send_lsb_first(SPI1);
	/* TODO */
}

static void gpio_setup(void)
{
	// rcc_periph_clock_enable(RCC_GPIOA);
	/* TODO */
}

void sendcmd(char *cmd){
	int n = sizeof(cmd)/sizeof(cmd[0]);
	for(int i=0; i<n; i++){
		spi_write(SPI1, cmd[i]);
	}
}

int main(void)
{
	clock_setup();
	gpio_setup();
	usart_setup();
	spi_setup();

	while (1) {
		
		char cmd[] = "0x58 0x00";
		sendcmd(cmd);

		usart_send_blocking(USART1, spi_read(SPI1));
	}

	return 0;
}