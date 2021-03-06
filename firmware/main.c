/*
 * File:   main.c
 * Author: logical
 *
 * Created on December 7, 2014, 10:37 AM
 *   The byte format is:
 *
 * MSB channel 1/LSB channel 1/MSB channel 2/LSB channel 2/MSB channel 3/LSB channel 3/MSB channel 4/LSB channel 4/
 * the control character are:
 *
 * s : sample 256 samples and send 512 bytes per channel
 *
 * t :to transmitter by sending "hello\n"
 *
 * r XXX :set gain of amplifier by increasing and decreasing rf of non inverting opamp. 
 *
 *
 */

#include <string.h>
#include <stdlib.h>
#include <pic16f1788.h>
#include <xc.h>

#define NO_BIT_DEFINES
#define BOOTLOAD
#ifndef BOOTLOAD
#pragma config FOSC = INTOSC    // Oscillator Selection (INTOSC oscillator: I/O function on CLKIN pin)
#pragma config WDTE = OFF       // Watchdog Timer Enable (WDT disabled)
#pragma config PWRTE = OFF      // Power-up Timer Enable (PWRT disabled)
#pragma config MCLRE = ON       // MCLR Pin Function Select (MCLR/VPP pin function is MCLR)
#pragma config CP = OFF         // Flash Program Memory Code Protection (Program memory code protection is disabled)
#pragma config CPD = OFF        // Data Memory Code Protection (Data memory code protection is disabled)
#pragma config BOREN = OFF      // Brown-out Reset Enable (Brown-out Reset disabled)
#pragma config CLKOUTEN = OFF   // Clock Out Enable (CLKOUT function is disabled. I/O or oscillator function on the CLKOUT pin)
#pragma config IESO = ON      // Internal/External Switchover (Internal/External Switchover mode is disabled)
#pragma config FCMEN = OFF      // Fail-Safe Clock Monitor Enable (Fail-Safe Clock Monitor is disabled)

#pragma config WRT = OFF        // Flash Memory Self-Write Protection (Write protection off)
#pragma config VCAPEN = OFF     // Voltage Regulator Capacitor Enable bit (Vcap functionality is disabled on RA6.)
#pragma config PLLEN = OFF      // PLL Enable (4x PLL disabled)
#pragma config STVREN = OFF     // Stack Overflow/Underflow Reset Enable (Stack Overflow or Underflow will not cause a Reset)
#pragma config BORV = HI        // Brown-out Reset Voltage Selection (Brown-out Reset Voltage (Vbor), low trip point selected.)
#pragma config LPBOR = OFF      // Low Power Brown-Out Reset Enable Bit (Low power brown-out is disabled)
#pragma config LVP = OFF        // Low-Voltage Programming Enable (High-voltage on MCLR/VPP must be used for programming)

#endif


/*pin defines*/

#define ERROR_PIN PORTAbits.RA6

/*
 * MC4130 digital potentiometer 
 * 
 */

#define POT_CS_PIN PORTCbits.RC1
#define ADC_CONV_PIN PORTCbits.RC2
#define SCK_PIN PORTCbits.RC3
#define SDI_PIN PORTCbits.RC4
#define SDO_PIN PORTCbits.RC5

/*
 * AD409 analog multiplexer
 */

#define MPX_A_PIN PORTBbits.RB0
#define MPX_B_PIN PORTBbits.RB1

/*
 * These outputs ground unused multiplexer inputs to prevent crosstalk
 */

#define GND_IN_0 PORTBbits.RB2
#define GND_IN_1 PORTBbits.RB3
#define GND_IN_2 PORTBbits.RB4
#define GND_IN_3 PORTBbits.RB5


/*
 * UART for the HC-05
 */
#define CMD_PIN PORTCbits.RC0
#define TX_PIN	PORTCbits.RC6
#define RX_PIN	PORTCbits.RC7



#define BAUD_230400 2
#define BAUD_38400 1

#define PACKETSIZE 8
#define MAXSAMPLE 65535 

struct{
    unsigned char size;
    unsigned char elems[64];
    unsigned flag :1;
}rxbuffer,txbuffer;


#define XON 0x11
#define XOFF 0x13
#define START 0x12
#define STOP 0x14
#define TESTMODE 0x07

union{
    struct{
    unsigned FLOW    :1;
    unsigned DATA    :1; 
    unsigned TEST    :1;
    unsigned RX      :1;
    unsigned TX      :1;
    unsigned CH1     :1;
    unsigned CH2     :1;
    };

}controlbits;


unsigned char delay_count1;


void delay_100us(unsigned int count){
/*
 *  ; Delay = 0.0001 seconds
 *  ; Clock frequency = 8 MHz
 *  ;798 cycles
 * 
 */
    while(count--){
        asm("movlw	0x41");
        asm("movwf	_delay_count1");
        asm("Delay_1    ");
        asm("decfsz	_delay_count1, f");
        asm("goto	Delay_1");
    }

}

void flasherror(unsigned char flashes){
    for(unsigned char f=0;f<flashes;f++){
            ERROR_PIN=1;
            delay_100us(2000);
            ERROR_PIN=0;
            delay_100us(2000);
        }
            delay_100us(5000);
}
unsigned int bytecount=0;

void interrupt isr(){

    if(PIR1bits.RCIF==1) {
        unsigned char rx=RCREG;
        if(rx==START)
            controlbits.DATA = 1;
        else if(rx==XON)
            controlbits.FLOW = 1;
        else if(rx==STOP){
            controlbits.DATA = 0;
            controlbits.TEST = 0;
        }
        else if(rx==TESTMODE)
            controlbits.TEST=1;
        else {
            rxbuffer.elems[rxbuffer.size] = rx;

            if(rxbuffer.elems[rxbuffer.size]==10){//linefeed
                rxbuffer.elems[rxbuffer.size+1]='\0';
                rxbuffer.flag=1;
            }
            rxbuffer.size++;
        }
        PIR1bits.RCIF=0;
    }
}
/*
 * this resistor is the grounded resistor of a non inverting opamp
 * The ADC and digital potentiometer are on the SPI bus 
 * 
 */

void setresistor(unsigned char value){
    ADC_CONV_PIN=1;
    POT_CS_PIN=0;
    SSPBUF = 0;
    while(!SSPSTATbits.BF);
    SSPBUF = value;
    while(!SSPSTATbits.BF);
    POT_CS_PIN=1;
    ADC_CONV_PIN=0;
    delay_100us(1);
}



#define INTERVAL 120
void feedtransmitter(){
/*
 *    the loop counter will count the wait time to give an more accurate delay
 */  
    
    unsigned int counter=0;
    while(txbuffer.size>0){ 
        TXREG = txbuffer.elems[--txbuffer.size];
        while(!TXSTAbits.TRMT)counter++;
    }
    while(counter<INTERVAL)counter++;
}

/*
 * 256 sa/s is a good sample rate for eeg
 */
void senddata(void){
    unsigned int count=0;
    txbuffer.size=0;
    while(controlbits.DATA){
            txbuffer.elems[txbuffer.size++]=XOFF;
            count=0;
            PORTB=0b00111000;
            MPX_A_PIN=0;                    //set multiplexer channel
            MPX_B_PIN=0;
            delay_100us(1);                 //settling time
            ADC_CONV_PIN=1;                 //start conversion
            delay_100us(1);
            ADC_CONV_PIN=0;
            PIR1bits.SSP1IF=0;
             SSPBUF = 0x00;                 // Write dummy data byte to the buffer to initiate transmission
            while(!SSPSTATbits.BF)count++;  //receive MSB first
            txbuffer.elems[txbuffer.size++] =SSPBUF;
            PIR1bits.SSP1IF=0;          
            SSPBUF = 0x00;              
            while(!SSPSTATbits.BF)count++;
            txbuffer.elems[txbuffer.size++]=SSPBUF;
            while(count<INTERVAL)count++;


            count=0;
            PORTB=0b00110101;
            MPX_A_PIN=1;
            MPX_B_PIN=0;
            delay_100us(1);
            ADC_CONV_PIN=1;
            delay_100us(1);
            ADC_CONV_PIN=0;
            PIR1bits.SSP1IF=0;
            SSPBUF = 0x00;
            while(!SSPSTATbits.BF)count++;
            txbuffer.elems[txbuffer.size++]=SSPBUF;
            PIR1bits.SSP1IF=0;
            SSPBUF = 0x00;
            while(!SSPSTATbits.BF)count++;
            txbuffer.elems[txbuffer.size++]=SSPBUF;
            while(count<INTERVAL)count++;

            count=0;
            PORTB=0b00101110;
            MPX_A_PIN=0;
            MPX_B_PIN=1;
            delay_100us(1);//settling time
            ADC_CONV_PIN=1;
            delay_100us(1);
            ADC_CONV_PIN=0;
            PIR1bits.SSP1IF=0; 
            SSPBUF = 0x00;     
            while(!SSPSTATbits.BF)count++;

            txbuffer.elems[txbuffer.size++]=SSPBUF;
            PIR1bits.SSP1IF=0;         
            SSPBUF = 0x00;              
            while(!SSPSTATbits.BF)count++;
            txbuffer.elems[txbuffer.size++]=SSPBUF;
            while(count<INTERVAL)count++;

            count=0;
            PORTB=0b00011111;
            MPX_A_PIN=1;
            MPX_B_PIN=1;
            delay_100us(1);
            ADC_CONV_PIN=1;
            delay_100us(1);
            ADC_CONV_PIN=0;
            PIR1bits.SSP1IF=0;         
            SSPBUF = 0x00;             
            while(!SSPSTATbits.BF)count++;
            txbuffer.elems[txbuffer.size++]=SSPBUF;
            PIR1bits.SSP1IF=0;
            SSPBUF = 0x00;
            while(!SSPSTATbits.BF)count++;
            txbuffer.elems[txbuffer.size++]=SSPBUF;
            while(count<INTERVAL)count++;

            txbuffer.elems[txbuffer.size++]=XON;

            feedtransmitter();

    }
}

/*
 *  testmode sends 4 sawtooth waves
 */
void test(void){
    int speed=2;
    union {
        unsigned char ch[2];
        unsigned short n;
    } channel[4];

    while(controlbits.TEST){
        txbuffer.elems[txbuffer.size++]=XOFF;
        txbuffer.elems[txbuffer.size++]=channel[0].ch[0];
        txbuffer.elems[txbuffer.size++]=channel[0].ch[1];;
        txbuffer.elems[txbuffer.size++]=channel[1].ch[0];
        txbuffer.elems[txbuffer.size++]=channel[1].ch[1];;
        txbuffer.elems[txbuffer.size++]=channel[2].ch[0];
        txbuffer.elems[txbuffer.size++]=channel[2].ch[1];;
        txbuffer.elems[txbuffer.size++]=channel[3].ch[0];
        txbuffer.elems[txbuffer.size++]=channel[3].ch[1];;
        txbuffer.elems[txbuffer.size++]=XON;

        channel[0].n+=speed;
        channel[1].n+=speed*2;
        channel[2].n+=speed*3;
        channel[3].n+=speed*4;
        if(channel[0].n >= MAXSAMPLE)channel[0].n=0;
        if(channel[1].n >= MAXSAMPLE)channel[1].n=0;
        if(channel[2].n >= MAXSAMPLE)channel[2].n=0;
        if(channel[3].n >= MAXSAMPLE)channel[3].n=0;
        delay_100us(60);

        feedtransmitter();
    }
}



void SetupADC(void){
/*
 * The adc is a 16 bit spi ltc1864
 */
    SSPCON1bits.SSPM=0b0000; 
    SSPCON1bits.SSPEN=1;



}

void SetupUART(unsigned char speed){
/*
 *  38400bps = 8e6/(4*(51+1)) = 38461
 *  230400bps = 8e6/(4*(8+1)) = 222222
 */



    RCSTAbits.SPEN=0;
    if(speed==BAUD_230400){
        SPBRG=8;
        BAUDCTLbits.BRG16=1;
	TXSTAbits.BRGH=1;
    }
    else if(speed==BAUD_38400){
	SPBRG=51;
        BAUDCTLbits.BRG16=1;
	TXSTAbits.BRGH=1;
    }
    TXSTAbits.SYNC=0;
    RCSTAbits.SPEN=1;
    RCSTAbits.CREN=1;
    RCSTAbits.RX9=0; 

    PIE1bits.RCIE=1;
    INTCONbits.PEIE=1;

}




void TXstring(const char *stringtosend){
    unsigned char i=0;
    controlbits.FLOW=1;
    while(i<strlen(stringtosend)){
        if(controlbits.FLOW ){
            TXREG = stringtosend[i++];
            while(!TXSTAbits.TRMT);
        }
    }
}



void setHM10uart(void){
    SetupUART(BAUD_38400); 
    delay_100us(10);
    TXstring("AT+UART?\r\n");
    unsigned char response[32];
    ERROR_PIN=1;

    while(!rxbuffer.flag);
    rxbuffer.flag=0;
    ERROR_PIN=0;

    strcpy(response,rxbuffer.elems);
    rxbuffer.size=0;
    rxbuffer.flag=0;

    if(strcmp(response,"+UART:230400,1,0\r\nOK\r\n")!=0){
        TXstring("AT+UART=230400,1,0\r\n");

        ERROR_PIN=1;

        while(!rxbuffer.flag);

        ERROR_PIN=0;

        strcpy(response,rxbuffer.elems);
        if(strcmp(response,"OK\r\n")!=0)while(1)flasherror(5);
        rxbuffer.size=0;
        rxbuffer.flag=0;

    }
}

void resetHC05(){

    TXstring("AT+RESET\r\n");
    unsigned char response[32];

    ERROR_PIN=1;

    while(!rxbuffer.flag);

    ERROR_PIN=0;

    strcpy(response,rxbuffer.elems);
    if(strcmp(response,"OK\r\n")!=0)while(1)flasherror(2);
    rxbuffer.size=0;
    rxbuffer.flag=0;
}

void EEwrite(unsigned char addr,unsigned char c){
	EEADRL = addr;
    CFGS=0;
    EEPGD=0;
	EEDATL = c;
    GIE = 0;
    WREN = 1;
    EECON2 = 0x55;
    EECON2 = 0xAA;
    WR = 1;
    WREN = 0;
    GIE = 1;	
    WREN=0;
    while(WR);
	}

void processcommand(void){
    char *token;
    token=strtok(rxbuffer.elems," ");
    if(strcmp(rxbuffer.elems,"FLASH\n")==0){
        TXstring("ERASING PROGRAM\n");
        EEwrite(0, 0);
        asm("RESET");
    }
    else if(strcmp((const char *)token[0],"r")==0){
            token=strtok(NULL," ");
            if(token!=NULL){
                int value=strtol(token,NULL,10);
                setresistor(value);
            }
    }
    rxbuffer.size=0;
    rxbuffer.flag=0;

}

void main(void){

/*
 * set to 8mhz
 */
    OSCCONbits.SCS=00;
    OSCCONbits.IRCF=0b1110;

/*
 * set all pins to digital outputs
 * Open drain for the output to the hc-05 
 * 
 * 
 */
    ANSELA = 0b00000000;
    ANSELB = 0b00000000;
    ANSELC = 0b00000000;
    TRISA = 0b00000000; 
    TRISB = 0b00000000; 
    TRISC = 0b10010000; 
    PORTA=0b00000000;
    PORTB=0b00000000;
    PORTC=0b00000001;
    ODCONCbits.ODCONC0=1;
    ODCONCbits.ODCONC6=1;
    
/* cmd pin is used for hc-05 only
 * hm-10 is in setup mode until it connects
 * pullup resistor  on CMD_PIN will keep module in command mode until ready
 * 
 * 
 */
    CMD_PIN=1;

    SetupADC(); 

    TXSTAbits.SYNC=0;			
    RCSTAbits.SPEN=1;			
    RCSTAbits.CREN=1;
    RCSTAbits.RX9=0;                    
    TXSTAbits.TXEN=1;			
    
    PIE1bits.RCIE=1;
    INTCONbits.PEIE=1;
    INTCONbits.GIE=1;

    rxbuffer.size=0;
    rxbuffer.flag=0;
    
    delay_100us(10000);
    setHM10uart();

    CMD_PIN=0;

    resetHC05();
    delay_100us(10000);


    SetupUART(BAUD_230400);

    delay_100us(100);
    controlbits.CH1=0;
    controlbits.CH2=0;
    while(1){
        if(controlbits.DATA)senddata();
        if(controlbits.TEST)test();
        if(rxbuffer.flag)processcommand();
    }


}