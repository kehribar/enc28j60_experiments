/*-----------------------------------------------------------------------------
/
/   Exploring tuxgraphics ethernet stack!
/
/------------------------------------------------------------------------------
/   DHCP client + Xively client + nrf24 radio receiver
/------------------------------------------------------------------------------
/   Uses protothreads for psuedo multithreading
/------------------------------------------------------------------------------
/   F_OSC: 8 Mhz
/   Web server port: 80
/   UDP server port: 1200
/   Serial port baud rate: 38400
/------------------------------------------------------------------------------
/   Used hardware: http://kehribar.me/hardware/ethernetGateway
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
#include "../pt/pt.h"
#include "../hal/hal.h"
#include "../tuxlib/tuxlib.h"
/*---------------------------------------------------------------------------*/
#include "../nrf24l01_plus/nrf24.h"
#include "../nrf24l01_plus/nRF24L01.h"
/*---------------------------------------------------------------------------*/
#if 1
    #define dbg(...) xprintf(__VA_ARGS__)
#else
    #define dbg(...)
#endif
/*---------------------------------------------------------------------------*/
#define BUFFER_SIZE 750
/*---------------------------------------------------------------------------*/
#define URL_base_address "api.xively.com"
#define EXTRA_HTTP_HEADER "X-ApiKey: PUT_YOUR_SECRET_API_KEY_HERE"
/*---------------------------------------------------------------------------*/
#define www_client_ok() (www_callback_state == 1)
#define www_client_timeout() (www_client_counter > 60)
#define www_client_failed() (get_tcp_client_state() == 5)
/*---------------------------------------------------------------------------*/
uint8_t data_array[8];
uint8_t rx_address[5] = {0xE7,0xE7,0xE7,0xE7,0xE7};
uint8_t tx_address[5] = {0xD7,0xD7,0xD7,0xD7,0xD7};
/*---------------------------------------------------------------------------*/
static struct pt blink_pt;
static struct pt nrf24_pt;
static struct pt xively_client_pt;
/*---------------------------------------------------------------------------*/
static uint16_t plen;
static uint16_t batt;
static uint16_t dat_p;
static char xively_buf[32];
static uint8_t retry_count;
volatile uint8_t new_packet;
static uint8_t dhcp_counter;
static uint16_t temperature;
static uint8_t xively_trigger;
volatile uint8_t timer1_counter;
static uint8_t buf[BUFFER_SIZE+1];
volatile uint8_t www_callback_state;
volatile uint16_t www_client_counter;
/*---------------------------------------------------------------------------*/
static uint8_t myip[4];
static uint8_t gwip[4];
static uint8_t gwmac[6];
static uint8_t netmask[4];
static uint8_t otherside_www_ip[4];
static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x29};
/*---------------------------------------------------------------------------*/
ISR(TIMER1_COMPA_vect) /* 100ms intervals */
{
    dhcp_counter++;
    timer1_counter++;
    www_client_counter++;       
    if(dhcp_counter == 60)
    {
        dhcp_counter = 0;
        dhcp_6sec_tick();
    }
}
/*---------------------------------------------------------------------------*/
void browserresult_callback(uint16_t webstatuscode,uint16_t datapos,uint16_t len)
{  
    www_callback_state = 1;   
}
/*---------------------------------------------------------------------------*/
void arpresolver_result_callback(uint8_t *ip,uint8_t reference_number,uint8_t *mac)
{   
    /* we got the gateway's MAC address */
    gwmac[0] = mac[0];
    gwmac[1] = mac[1];
    gwmac[2] = mac[2];
    gwmac[3] = mac[3];
    gwmac[4] = mac[4];
    gwmac[5] = mac[5];
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(blink_thread(struct pt *pt))
{
    PT_BEGIN(pt);
    
    while(1)
    {        
        cli();
        
        timer1_counter = 0;
        
        sei();
        
        PT_WAIT_UNTIL(pt,timer1_counter > 5);

        led1_toggle();                
    }
    
    PT_END(pt);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(nrf24_thread(struct pt *pt))
{
    PT_BEGIN(pt);

    /* init hardware pins */
    nrf24_init();
    
    /* Channel #2 , payload length: 8 */
    nrf24_config(2,8);    

    /* Set the device addresses */
    nrf24_tx_address(tx_address);
    nrf24_rx_address(rx_address);

    uint32_t counter;
    uint16_t temp;
    int16_t realval;    

    while(1)
    {
        PT_WAIT_UNTIL(pt,nrf24_dataReady());

        dbg(PSTR("> -------------------------------------\r\n"));        
        
        nrf24_getData(data_array);

        temp = (data_array[0]<<8)+data_array[1];

        realval  = temp * 10;
        realval /= 11;             
        realval -= 500;

        dbg(PSTR("> RAW Temp: %u\r\n"),temp);
        dbg(PSTR("> Temp: %u\r\n"),realval);

        temperature = realval;

        temp = (data_array[2]<<8)+data_array[3];
        batt = 1149120 / temp;

        dbg(PSTR("> RAW Batt: %u\r\n"),temp);
        dbg(PSTR("> Batt: %u\r\n"),batt);                

        counter = (data_array[4]<<24)+(data_array[5]<<16)+(data_array[6]<<8)+data_array[7]; 

        dbg(PSTR("> Counter: %u\r\n"),counter);

        xively_trigger = 1;
    }

    PT_END(pt);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(xively_client_thread(struct pt *pt))
{
    PT_BEGIN(pt);    

    while(1)
    {       
        PT_WAIT_UNTIL(pt,xively_trigger); 
        
        xively_trigger = 0;       

        dbg(PSTR("> Xively client starts.\r\n"));

        retry_count = 0;
        
        do
        {
            /* disable interrupts */
            cli(); 

            /* reset timeout counter */
            www_client_counter = 0; 
            
            /* re-enable interrupts */
            sei(); 

            /* make a dns request */
            dnslkup_request(buf,URL_base_address,gwmac);

            /* wait an answer ... */
            PT_WAIT_UNTIL(pt,dnslkup_haveanswer() || www_client_timeout());

            if(dnslkup_haveanswer())
            {
                /* exit the loop */
                retry_count = 0xFF;              
            }            
            else
            {
                dbg(PSTR("> DNS lookup timeout!\r\n"));
                retry_count++;
            }

        }
        while(retry_count < 5);

        if(dnslkup_haveanswer())
        {
            /* save the received IP to your userspace variable */
            dnslkup_get_ip(otherside_www_ip);

            /* reset callback state */
            www_callback_state = 0;

            /* reset retry count */
            retry_count = 0;

            /* convert the temperature and battery information into strings */
            sprintf(xively_buf,"temp,%u\r\nbatt,%u",temperature,batt);

            /* browsing action */
            do
            {
                /* disable interrupts */
                cli(); 

                /* reset timeout counter */
                www_client_counter = 0; 
                
                /* re-enable interrupts */
                sei(); 

                /* increment the trial counter */
                retry_count++;                                

                /* send a PUT request to the Xively server */
                client_http_put(
                    PSTR("/v2/feeds/"), /* base url suffix */
                    "2050882912.csv", /* 2050882912 is my FEED_ID */
                    URL_base_address, /* URL base address ... */
                    PSTR(EXTRA_HTTP_HEADER), /* Additional HTTP header we are adding */
                    xively_buf, /* Actual message ... */
                    &browserresult_callback, /* callback function for our URL browsing attempt */                    
                    otherside_www_ip,
                    gwmac
                );

                /* wait ... */
                PT_WAIT_UNTIL(pt,www_client_failed() || www_client_timeout() || www_client_ok() );

                if(www_client_ok())
                {
                    /* exit the loop */
                    retry_count = 0xFF;              
                }
                else if(www_client_failed())
                {
                    dbg(PSTR("> Browsing problem!\r\n"));
                }
                else if(www_client_timeout())
                {
                    dbg(PSTR("> Browsing timeout!\r\n"));
                }
            }
            while(retry_count < 5);

            if(retry_count == 0xFF)
            {
                dbg(PSTR("> Browsing went ok!\r\n"));  
            }
            else
            {
                dbg(PSTR("> Browsing failed!\r\n"));     
            }
        }
        else
        {
            dbg(PSTR("> Browsing failed!\r\n"));     
        }
        
    }

    PT_END(pt);
}
/*---------------------------------------------------------------------------*/
int main(void)
{   
    uint16_t rval;     

    /* init hardware layer */
    init_hci();
    timer1_init();
    init_serial();    
    sei();

    led1_high();
    led2_high();

    /* init protothreads */
    PT_INIT(&blink_pt);    
    PT_INIT(&nrf24_pt);
    PT_INIT(&xively_client_pt);    

    /* greeting message */
    dbg(PSTR("> Hello World!\r\n"));   

    /* init the ethernet chip */
    enc28j60Init(mymac);        
    enc28j60PhyWrite(PHLCON,0x476);
    
    /* get automatic IP */
    rval=0;
    init_mac(mymac);        
    while(rval==0)
    {
        plen=enc28j60PacketReceive(BUFFER_SIZE, buf);
        rval=packetloop_dhcp_initial_ip_assignment(buf,plen,mymac[5]);
    }
    dhcp_get_my_ip(myip,netmask,gwip); 
    client_ifconfig(myip,netmask);
    
    /* learn the MAC address of the gateway */
    get_mac_with_arp(gwip,0,&arpresolver_result_callback);
    while(get_mac_with_arp_wait())
    {        
        plen=enc28j60PacketReceive(BUFFER_SIZE, buf);
        packetloop_arp_icmp_tcp(buf,plen);
    }        
    
    dbg(PSTR("> System is ready.\r\n"));   

    led1_low();
    led2_low();

    /* main loop */
    while(1)
    {    
        if(enc28j60linkup())
        {            
            /* poll hardware ethernet buffer */
            plen = enc28j60PacketReceive(BUFFER_SIZE,buf);     
            
            /* terminate the buffer */
            buf[BUFFER_SIZE]='\0';

            /* handle DHCP messages if neccessary */
            plen = packetloop_dhcp_renewhandler(buf,plen);

            /* handle and analyse the packet slightly */
            dat_p = packetloop_arp_icmp_tcp(buf,plen);

            if( dat_p == 0)
            {
                udp_client_check_for_dns_answer(buf,plen);
            } 
            
            PT_SCHEDULE(blink_thread(&blink_pt));      
            PT_SCHEDULE(nrf24_thread(&nrf24_pt));                  
            PT_SCHEDULE(xively_client_thread(&xively_client_pt));
        }
    }    
    return 0;
}
/*---------------------------------------------------------------------------*/
