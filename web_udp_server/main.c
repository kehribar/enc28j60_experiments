/*-----------------------------------------------------------------------------
/
/   Exploring tuxgraphics ethernet stack!
/
/------------------------------------------------------------------------------
/   In this example we are:
/   - using a static predefined IP.
/   - serving as a _very basic_ HTTP web server.
/   - replying to the UDP messages directly sent to us.
/   - toggling a LED inside the main loop in our _free_ time.
/------------------------------------------------------------------------------
/   ihsan Kehribar - 2013 
/----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
/*---------------------------------------------------------------------------*/
#include "./digital.h"
#include "./serial_lib/xitoa.h"
/*---------------------------------------------------------------------------*/
#include "../tuxlib/net.h"
#include "../tuxlib/dnslkup.h"
#include "../tuxlib/timeout.h"
#include "../tuxlib/enc28j60.h"
#include "../tuxlib/dhcp_client.h"
#include "../tuxlib/ip_arp_udp_tcp.h"
#include "../tuxlib/websrv_help_functions.h"
/*---------------------------------------------------------------------------*/
#define MYWWWPORT 80
#define MYUDPPORT 1200
#define BUFFER_SIZE 550
/*---------------------------------------------------------------------------*/
static uint8_t str[64];
static uint16_t ledCounter = 0;
static uint8_t buf[BUFFER_SIZE+1];
/*---------------------------------------------------------------------------*/
static uint8_t myip[4] = {192,168,1,73};
static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x29};
/*---------------------------------------------------------------------------*/
void init_serial();
void send_char(char c);
/*---------------------------------------------------------------------------*/
int main(void)
{   
    uint16_t rval;     
    uint16_t plen;
    uint16_t dat_p;
    uint16_t payloadlen;

    /* 19200 baud rate */
    init_serial();    

    /* init the ethernet chip */
    enc28j60Init(mymac);        
    enc28j60PhyWrite(PHLCON,0x476);
            
    /* static ip configuration */
    init_udp_or_www_server(mymac,myip);
    www_server_port(MYWWWPORT);        

    /* set LED pin as output */
    pinMode(B,1,OUTPUT);
    digitalWrite(B,1,LOW);
    
    /* greeting message */
    xprintf(PSTR("Hello World!\r\n"));    

    /* main loop */
    while(1)
    {    
        /* I guess we should do this as fast as we can? */
        plen = enc28j60PacketReceive(BUFFER_SIZE, buf);
    
        /* do the string termination for incoming message */
        buf[BUFFER_SIZE]='\0';                

        /* handle and analyse the packet slightly ... */
        dat_p = packetloop_arp_icmp_tcp(buf,plen);

        /*---------------------------------------------------------------------
        / We have a TCP message 
        /--------------------------------------------------------------------*/
        if(dat_p != 0)
        {
            /* print incoming TCP message for debug purposes */
            xprintf(PSTR("> %s\r\n"),buf+dat_p);
           
            /*-----------------------------------------------------------------
            / HTTP GET message
            /------------------------------------------------------------------
            / Example of a starting part of an HTTP GET message is: 
            /   GET /index.html HTTP/1.1
            /----------------------------------------------------------------*/
            if(strncmp("GET ",(char *)&(buf[dat_p]),4)==0)
            {            
                /* just one web page in the "root directory" of the web server */
                if (strncmp("/ ",(char *)&(buf[dat_p+4]),2)==0)
                {
                    /* this is basic header */
                    dat_p=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n"));
                    
                    /* this is our _website_ */
                    dat_p=fill_tcp_data_p(buf,dat_p,PSTR("<h2>Hello World!</h2>\n"));
                    dat_p=fill_tcp_data_p(buf,dat_p,PSTR("This is a test page.\n"));

                    /* send the data */
                    www_server_reply(buf,dat_p);
                }
                else
                {
                    /* we dont have any websites in this location */
                    dat_p=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 401 Unauthorized\r\nContent-Type: text/html\r\n\r\n<h1>401 Unauthorized</h1>"));            
                    
                    /* send the data */
                    www_server_reply(buf,dat_p);
                }
            }
            /*-----------------------------------------------------------------
            / Could be any other TCP message ...
            /----------------------------------------------------------------*/
            else
            {                            
                dat_p=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n"));
                dat_p=fill_tcp_data_p(buf,dat_p,PSTR("<h1>200 OK</h1>"));
                www_server_reply(buf,dat_p);
            }                        
        }
        /*---------------------------------------------------------------------
        / We might have a UDP message or we might have nothing ...
        /--------------------------------------------------------------------*/
        else
        {            
            /*-----------------------------------------------------------------
            / message is relevant to us!
            /----------------------------------------------------------------*/
            if(eth_type_is_ip_and_my_ip(buf,plen)!=0)
            {
                /* we have an incoming UDP message! */
                if((buf[IP_PROTO_P]==IP_PROTO_UDP_V) && (buf[UDP_DST_PORT_H_P]==(MYUDPPORT>>8)) && (buf[UDP_DST_PORT_L_P]==(MYUDPPORT&0xff)))
                {
                    /* calculate the udp message length */
                    payloadlen=buf[UDP_LEN_L_P]-UDP_HEADER_LEN;                            

                    /* replace the newline with string terminator */
                    buf[UDP_DATA_P+payloadlen-1] = '\0';

                    /* add some explanatory header to it */
                    sprintf(str,"> udp got: %s\r\n",buf+UDP_DATA_P);                    

                    /* print the message to the UART console */
                    xprintf(PSTR("%s"),str);            

                    /* send the same message with little modification to the sender */     
                    make_udp_reply_from_request(buf,str,strlen(str),MYUDPPORT);                                       
                }
            }
            /*-----------------------------------------------------------------
            / we have nothing ... do your other tasks in here 
            /----------------------------------------------------------------*/
            else            
            {                
                if(ledCounter++ == 0x5000)
                {
                    togglePin(B,1);
                    ledCounter = 0x0000;     
                }                
            }
        }     
    }    
    return 0;
}
/*---------------------------------------------------------------------------*/
void init_serial()
{
    /* 19200 baud rate with 16 MHz Xtal ... */
    const uint8_t ubrr = 51;

    pinMode(D,0,INPUT);
    pinMode(D,1,OUTPUT);

    /* Set baud rate */ 
    UBRR0H = (unsigned char)(ubrr>>8); 
    UBRR0L = (unsigned char)ubrr; 

    /* Enable receiver and transmitter and Receive interupt */ 
    UCSR0B = (1<<RXEN0)|(1<<TXEN0)|(1<<RXCIE0); 

    /* Set frame format: 8data, 1stop bit no parity */ 
    UCSR0C = (1<<UCSZ01)|(1<<UCSZ00); 

    /* Set the library ... */
    xfunc_out = send_char;
}
/*---------------------------------------------------------------------------*/
void send_char(char c)
{
    /* Wait for empty transmit buffer ... */
    while(!(UCSR0A & (1<<UDRE0)));

    /* Start sending the data! */
    UDR0 = c;

    /* Wait until the transmission is over ... */
    while(!(UCSR0A & (1<<TXC0)));
}
/*---------------------------------------------------------------------------*/