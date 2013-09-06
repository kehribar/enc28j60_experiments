/*-----------------------------------------------------------------------------
/
/   Exploring tuxgraphics ethernet stack!
/
/------------------------------------------------------------------------------
/   In this example we are:
/   - obtaining an IP from a DHCP server.
/   - sending a tweet via supertweet.net V1.1 API.
/------------------------------------------------------------------------------
/   ihsan Kehribar - 2013 
/----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
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
#if 1
    #define dbg(...) xprintf(__VA_ARGS__)
    #define dbg_print_ip(ip_buf) print_ip(ip_buf)
    #define dbg_print_mac(mac_buf) print_mac(mac_buf)
#else
    #define dbg(...)
    #define dbg_print_ip(ip_buf) 
    #define dbg_print_mac(mac_buf)
#endif
/*---------------------------------------------------------------------------*/
#define BUFFER_SIZE 750
#define URL_base_address "api.supertweet.net"
/*-----------------------------------------------------------------------------
/ Replace XXXXXXXXXXXXXXXXX with the base64 encoded version of your 
/   username:password string. Username is the twitter username but the password
/   is supertweet.net account password.
/----------------------------------------------------------------------------*/
#define AUTH_CODE "Authorization: Basic XXXXXXXXXXXXXXXXX"
/*---------------------------------------------------------------------------*/
static uint8_t myip[4];
static uint8_t gwip[4];
static uint8_t gwmac[6];
static uint8_t netmask[4];
static uint8_t buf[BUFFER_SIZE+1];
static uint8_t otherside_www_ip[4];
static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x29};
/*---------------------------------------------------------------------------*/
void init_serial();
void send_char(char c);
void print_ip(uint8_t* ip_buf);
void print_mac(uint8_t* mac_buf);
void ethernet_poll(uint8_t* ethbuf,uint16_t* dataP,uint16_t* len);
void browserresult_callback(uint16_t webstatuscode,uint16_t datapos,uint16_t len);
void arpresolver_result_callback(uint8_t *ip,uint8_t reference_number,uint8_t *mac);
/*---------------------------------------------------------------------------*/
int main(void)
{   
    /* local variables */
    uint8_t rval;
    uint16_t plen;
    uint16_t dat_p; 
    uint8_t mainState = 1;
    uint16_t ledCounter = 0;

    /* set LED pin as output */
    pinMode(B,1,OUTPUT);
    digitalWrite(B,1,LOW);

    /*-------------------------------------------------------------------------
    / System init.
    /------------------------------------------------------------------------*/
    init_serial();
    enc28j60Init(mymac);        
    enc28j60PhyWrite(PHLCON,0x476);    
    init_mac(mymac);
    dbg(PSTR("> Hello!\r\n"));
    /*-------------------------------------------------------------------------
    / Obtain an appropriate IP via DHCP protocol.
    /------------------------------------------------------------------------*/
    rval=0;
    while(rval==0)
    {
        plen=enc28j60PacketReceive(BUFFER_SIZE, buf);
        buf[BUFFER_SIZE]='\0';
        rval=packetloop_dhcp_initial_ip_assignment(buf,plen,mymac[5]);
    }
    dhcp_get_my_ip(myip,netmask,gwip); 
    client_ifconfig(myip,netmask);
    /*-------------------------------------------------------------------------
    / Print out the obtained IP.
    /------------------------------------------------------------------------*/
    dbg_print_ip(myip);
    /*-------------------------------------------------------------------------
    / Learn the MAC address of the gateway.
    /------------------------------------------------------------------------*/    
    get_mac_with_arp(gwip,0,&arpresolver_result_callback);
    while(get_mac_with_arp_wait())
    {        
        plen=enc28j60PacketReceive(BUFFER_SIZE, buf);
        packetloop_arp_icmp_tcp(buf,plen);
    }
    /*-------------------------------------------------------------------------
    / Main loop!
    /------------------------------------------------------------------------*/    
    while(1)        
    {
        /* main ethernet poll function */
        ethernet_poll(buf,&dat_p,&plen);

        /* application state machine */
        switch(mainState)
        {
            case 0: /* idle state */
            {
                if(ledCounter++ == 0x5000)
                {
                    togglePin(B,1);
                    ledCounter = 0x0000;     
                }   
                break;
            }
            case 1: /* make a dns request */
            {
                dnslkup_request(buf,URL_base_address,gwmac);
                mainState = 2;
                break;
            }
            case 2: /* wait until you get an answer */
            {
                if(dnslkup_haveanswer())
                {
                    mainState = 3;
                }
                break;
            }
            case 3: /* save the received IP to your userspace variable */
            {
                dnslkup_get_ip(otherside_www_ip);
                dbg_print_ip(otherside_www_ip);              
                mainState = 4;
                break;
            }
            case 4: /* try to send a tweet! */
            {                
                client_http_post(
                    PSTR("/1.1/statuses/update.json"), /* constant part of the URL suffix */
                    NULL, /* variable part of the URL which comes after the constant suffix */
                    URL_base_address, /* base-name of the web page we want to browse */
                    PSTR(AUTH_CODE), /* additional HTTP header line */
                    "status=Hello World!\0", /* POST message */
                    browserresult_callback, /* callback function for twitting attempt */
                    otherside_www_ip, /* IP representation of the webpage */
                    gwmac /* mac address of our gateway */
                );

                mainState = 0;
                break;
            }
        }
    }    
    return (0);
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

    /* Enable receiver and transmitter */
    UCSR0B = (1<<RXEN0)|(1<<TXEN0);

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
void ethernet_poll(uint8_t* ethbuf,uint16_t* dataP,uint16_t* len)
{
    /* I guess we should do this as fast as we can? */
    *len=enc28j60PacketReceive(BUFFER_SIZE, ethbuf);
    
    /* do the string termination for incoming message */
    ethbuf[BUFFER_SIZE]='\0';
    
    /* handle DHCP messages if neccessary */
    *len=packetloop_dhcp_renewhandler(ethbuf,*len);

    /* handle and analyse the incoming packet slightly ... */        
    *dataP=packetloop_arp_icmp_tcp(ethbuf,*len);

    /* check for dns messages */
    if(*dataP==0)
    {
        udp_client_check_for_dns_answer(ethbuf,*len);
    }
}
/*---------------------------------------------------------------------------*/
void print_ip(uint8_t* ip_buf)
{
    uint8_t i = 0;
    dbg(PSTR("> "));
    while(i<3)
    {     
        dbg(PSTR("%d."),ip_buf[i]);
        i++;
    }    
    dbg(PSTR("%d\r\n"),ip_buf[i]);
}
/*---------------------------------------------------------------------------*/
void print_mac(uint8_t* mac_buf)
{
    uint8_t i = 0;
    dbg(PSTR("> "));
    while(i<5)
    {     
        dbg(PSTR("%2X:"),mac_buf[i]);
        i++;
    }    
    dbg(PSTR("%2X\r\n"),mac_buf[i]);
}
/*---------------------------------------------------------------------------*/
void browserresult_callback(uint16_t webstatuscode,uint16_t datapos,uint16_t len)
{
    uint16_t q = 0;    
    dbg(PSTR("> Status code: %d\r\n"),webstatuscode);
    dbg(PSTR("> Datapos: %d\r\n"),datapos);
    dbg(PSTR("> Len: %d\r\n"),len);
    for(q=datapos;q<(datapos+len);q++)
    {
        dbg(PSTR("%c"),buf[q]);
    }
}
/*---------------------------------------------------------------------------*/
void arpresolver_result_callback(uint8_t *ip,uint8_t reference_number,uint8_t *mac)
{   
    /* we got the gateway's MAC address */
    gwmac[0]=mac[0];
    gwmac[1]=mac[1];
    gwmac[2]=mac[2];
    gwmac[3]=mac[3];
    gwmac[4]=mac[4];
    gwmac[5]=mac[5];

    /* print gateway IP address */
    dbg_print_ip(gwip);

    /* print gateway MAC address */
    dbg_print_mac(gwmac);    
}
/*---------------------------------------------------------------------------*/