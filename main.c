#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/usart.h>
#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

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

// void sendcmd(char *cmd){
// 	int n = sizeof(cmd)/sizeof(cmd[0]);
// 	for(int i=0; i<n; i++){
// 		spi_write(SPI1, cmd[i]);
// 	}
// }

int debug=0;
int fd;
void sendpacket(unsigned char * payload, int len);
void sendcmd(char * payload);
int main(void)
{
	clock_setup();
	gpio_setup();
	usart_setup();
	spi_setup();

    printf("Welcome!\n");
    int waitfor;
#define PACKET 1
#define ACK 2
    sendcmd("0x58 0x00");waitfor=ACK; //RFRegulationTest -- This command is used for radio regulation test. 
    int cstate=0;   //Chat state, config state, whatever..
    int lc=0;
    while(1)
    {
        lc++;   //Send a scan command every second or so.
        if(lc>1000)
        {
            lc=0;
            sendcmd("0x4A 0x02 0x00");  //InListPassiveTarget -- The goal of this command is to detect as many targets (maximum MaxTg) as possible (max two) in passive mode.
        }

        if(cstate==1)   //ACK's set the bottom bit in state, allowing it to go onto the next state.
        {
            sendcmd("0x02");waitfor=PACKET; //Read the version out of the PN532 chip.
            cstate++;
        }
        if(cstate==3)   //ACK's set the bottom bit in state, allowing it to go onto the next state.
        {
            sendcmd("0x04");waitfor=PACKET; //Get current status.
            cstate++;
        }
        if(cstate==5)
        {
            waitfor=PACKET;
            sendcmd("0x32 0x05 0xFF 0x01 0x10");//Max retries - last byte is for passive: 0=1 try, 1=2 tries, 254=255 tries, 0xFF=infinite retries.
            //If last byte is 0xFF, then unit starts scanning for cards indefinitely. As soon as it detects a card, it stops scanning and returns info.
            //If last byte is less than 0xFF, it tries scans and as soon as it finds a card returns info on it and stops trying, but
            //if it never finds a card in the specified number of retries, it gives up and returns 0x4B, 0x00 (Cards found: Zero.)
            cstate++;
        }
//Alternative way to send a new scan command each time the previous one gives up or finds a card:
//      if(cstate==7)
//      {
//          waitfor=PACKET;
//          sendcmd("0x4A 0x02 0x00");      //InListPassiveTarget
//          cstate--;
//      }


        static unsigned char buffin [1000024];
        static unsigned char buf[1],bufo[1];
        int n;
        static int bi=0;
        volatile unsigned char len,lcs,tfi,dcs;

        bufo[0]=buf[0];
        n=read(fd,buf,sizeof(buf));
        if(n!=0)
        {
            if(n>0)
            {
                if(bi<1000000)
                {
                    static int state=0;

                    if(state==0)    //Waiting for preamble..
                    {
                        if((bufo[0]==0)&&(buf[0]==0xFF)){state=10;}
                    }
                    else if(state==10)  //Waiting for Len
                    {
                        len=buf[0];
                        state=20;
                    }
                    else if(state==20)  //Waiting for len checksum
                    {
                        if((len==0xFF)&&(buf[0]==0xFF)){printf("ERROR: BIG PACKET. Bye.\n");}
                        if((len==0x00)&&(buf[0]==0xFF)){state=21;}
                        else if((len==0xFF)&&(buf[0]==0x00)){state=21;}
                        else
                        {
                            lcs=buf[0]+len;
                            if(lcs){printf("ERROR: len checksum failed! 0x%02X\n",buf[0]);}
                            state=30;
                        }
                    }
                    else if(state==21)  //Waiting for the 0x00 after ack/nack..
                    {
                        state=0;
                        if(buf[0]==0x00)
                        {
                            if(bufo[0]==0xFF)
                            {
                                if(debug){printf("ACK!\n");}
                                if(waitfor==ACK){cstate=cstate|1;}
                            }
                            if(bufo[0]==0x00){printf("NACK!\n");}

                        }else{
                            printf("ERROR: Invalid length, or ack/nack missing postamble...\n");
                        }
                    }
                    else if(state==30)  //Waiting for tfi..
                    {
                        tfi=buf[0];
                        //printf("tfi=0x%02X\n",tfi);
                        dcs=tfi;
                        bi=0;
                        state=40;
                    }
                    else if(state==40)  //Saving bytes...
                    {
                        //printf("Saving payload byte 0x%02X\n",buf[0]);
                        buffin[bi++]=buf[0];
                        dcs=dcs+buf[0];
                        if(bi>=len){state=50;}
                    }
                    else if(state==50)  //Calculating the checksum..
                    {
                        state=0;
                        dcs=dcs+buf[0];
                        if(dcs)
                        {
                            printf("ERROR: Data Checksum Failed! (0x%02X)\n",dcs);
                        }else{
                            if(waitfor==PACKET){cstate=cstate|1;}
                            //printf("Good Packet: tfi=0x%02X, len=%d\n",tfi,len-1);
                            if(tfi==0xD5)
                            {
                                if(buffin[0]==0x03){printf("PN532 Version: %d.%d, features:%d\n",buffin[2],buffin[3],buffin[4]);}
                                if(buffin[0]==0x05)
                                {

                                    printf("Status: Last Error:%d, Field:%d, Targets:%d, SAM Status:0x%02X\n",buffin[1],buffin[2],buffin[3],buffin[len-2]);
                                    static char bitrates[255][10]={"106kbps","212kbps","424kbps"};
                                    static char modtypes[255][100];
                                    strcpy(modtypes[0x00],"Mifare, ISO/IEC14443-3 Type A, ISO/IEC14443-3 Type B, ISO/IEC18092 passive 106 kbps");
                                    strcpy(modtypes[0x10],"FeliCa, ISO/IEC18092 passive 212/424 kbps");
                                    strcpy(modtypes[0x01],"ISO/IEC18092 Active mode");
                                    strcpy(modtypes[0x02],"Innovision Jewel tag");
                                    if(buffin[3]==1){printf("Target %d: rx bps:%s, tx bps:%s, modulation type: %s.\n",buffin[4],bitrates[buffin[5]],bitrates[buffin[6]],modtypes[buffin[7]]);}
                                    if(buffin[3]==2){printf("Target %d: rx bps:%s, tx bps:%s, modulation type: %s.\n",buffin[8],bitrates[buffin[9]],bitrates[buffin[10]],modtypes[buffin[11]]);}
                                }
                                if(buffin[0]==0x4B)
                                {
                                    printf("FOUND %d CARDS!\n",buffin[1]);
                                    //ONLY VALID FOR Mifare/ ISO type A 106KBPS:
                                    int i,ii,iii;
                                    i=0;ii=2;
                                    while(i<buffin[1])
                                    {
                                        printf("Target # %d:", buffin[ii++]);
                                        printf("SENS_RES=0x%02X%02X, ",buffin[ii],buffin[ii+1]);ii++;ii++;
                                        printf("SEL_RES=0x%02X, ",buffin[ii++]);
                                        printf("NFCIDLength=%d, ",buffin[ii++]);
                                        printf("NFCID=");
                                        iii=0;
                                        while(iii<buffin[ii-1])
                                        {
                                            printf("%02X",buffin[ii+iii]);
                                            iii++;
                                            if(iii<buffin[ii-1]){printf(":");}
                                        }
                                        ii=ii+iii;
                                        printf("\n");
                                        i++;
                                    }

                                }
                                //Just a debugging thing for printing out the contents of valid packets.
                                //int i=0;while(i<(len-1)){printf("0x%02X, ",buffin[i++]);}printf("\n");
                            }
                            else if(tfi==0x7F)
                            {
                                printf("Received error packet 0x7F with zero size.\n");
                            }else{
                                printf("ERROR: Got unknown %d byte packet with tfi=0x%02X!\n",len-1,tfi);
                            }

                        }
                    }
                    else
                    {
                        printf("Uhoh!\n");
                    }
                    //printf("Got byte 0x%02X, now state is %d\n",(unsigned char)buf[0],state);

                }else{
                    printf("ERROR: bi=%d which is too big.. Starting over.\n",bi);
                    bi=0;
                }
            }else{
                printf("ERROR %d while reading serial port: %s\n",errno,strerror(errno));
            }
        }

        // usleep(1000);

    }



    return(0);
}



void sendpacket(unsigned char * payload, int len)
{
    int tfi;
    static unsigned char buffer[66000];
    int i,bo;
    unsigned char lcs,dcs;
    tfi=0xD4;
    i=0;
    bo=0;
    while(i<=8){i++;buffer[bo++]=0xFF;} //Pre-padding.. 8-800 OK, 900 too much, 7 too little. Needs to be 0xFF I guess.. Probably wakes it up.
    buffer[bo++]=0x00;          //Preamble.
    buffer[bo++]=0xFF;              //Preamble.
    len++;
    lcs=-len;               //Length Checksum.. (yes...)
    buffer[bo++]=len;
    buffer[bo++]=lcs;
    buffer[bo++]=tfi;
    dcs=tfi;
    i=0;
    while((i<65900)&&(i<(len-1)))
    {
        buffer[bo]=payload[i];
        dcs=dcs+buffer[bo];
        bo++;
        i++;
    }
    dcs=(-dcs);
    buffer[bo++]=dcs;
    write(fd,buffer,bo);
    //printf("Sent %d bytes\n",bo);
    //printf("Whole packet: ");
    //i=0;
    //while(i<bo)
    //{
    //  printf("0x%02X ",buffer[i]);
    //  i++;
    //}
    //printf("\n");
}
void sendcmd(char * payload)    //Accepts space separated argument list like "0xFF 0x0A 255 'USERID' 0 0"
{               //strings are quoted in half quotes. half quotes inside a string are escaped \\'
    int i,v;        //full quotes inside a string are escaped like \"
    static unsigned char buffer[1024];  //back slashes inside a string are escaped like \\\\  .
    static int bo;      //(The first escape or escape pair is for the C compiler, the second for this function:
    bo=0;           // The actual contents of the string are just \' and \\)
    i=0;            // Numeric formats supported are hex (0xNN), base ten (123), and octal (0377).
    if(debug){printf("sendcmd: ");}
    while(payload[i])
    {
        if((payload[i]!='\'')&&(payload[i]!=' '))
        {
            v=strtoul(&payload[i],NULL,0);
            buffer[bo++]=v;
            if(debug){printf("0x%02X, ",v);}
            while(payload[i]>' '){i++;}
        }
        else if(payload[i]=='\'')
        {
            i++;
            int keeprun;
            keeprun=1;
            while(keeprun)
            {
                if(payload[i]=='\\')
                {
                    i++;
                }else{
                    if(payload[i]=='\''){keeprun=0;}
                }

                if((keeprun)&&(payload[i]))
                {
                    buffer[bo++]=payload[i];
                    if(debug){printf("%c",payload[i]);}

                }
                i++;
            }
            if(debug){printf(", ");}

        }
        else
        {
            i++;
        }
    }
    if(debug){printf("\n");}
    sendpacket(buffer,bo);
}