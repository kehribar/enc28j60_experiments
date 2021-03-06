/*-----------------------------------------------------------------------------
/
/   Exploring tuxgraphics ethernet stack!
/
/------------------------------------------------------------------------------
/   DHCP client + web server + web client + UDP broadcast + UDP server 
/   + CoAP server
/------------------------------------------------------------------------------
/   Uses protothreads for psuedo multithreading
/------------------------------------------------------------------------------
/   Button1 triggers UDP broadcast
/   Button2 triggers web client action
/------------------------------------------------------------------------------
/   To test the CoAP server, use libcoap
/
/   ./coap-client -v 100 -m get coap://{device_ip}/.well-known/core
/   ./coap-client -v 100 -m get coap://{device_ip}/light
/   ./coap-client -e "1" -m put coap://{device_ip}/light
/   ./coap-client -e "0" -m put coap://{device_ip}/light
/
/   Or use copper (Firefox plugin)
/
/   coap://{device_ip}
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
#include "../microcoap/coap.h"
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
#define MYWWWPORT 80
#define MYUDPPORT 1200
#define COAPPORT 5683
#define BUFFER_SIZE 750
/*---------------------------------------------------------------------------*/
#define udp_server_flag 0
#define www_server_flag 1
#define coap_server_flag 2
/*---------------------------------------------------------------------------*/
#define set_flag(reg,bit) (reg|=(1<<bit))
#define check_flag(reg,bit) (reg&(1<<bit))
#define clear_flag(reg,bit) (reg&=~(1<<bit))
/*---------------------------------------------------------------------------*/
#define URL_base_address "rainycloud.kehribar.me"
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
static struct pt www_server_pt;
static struct pt udp_server_pt;
static struct pt www_client_pt;
static struct pt coap_server_pt;
static struct pt temperature_pt;
static struct pt udp_broadcast_pt;
/*---------------------------------------------------------------------------*/
static coap_packet_t pkt;
static uint8_t response[64];
static coap_packet_t rsppkt;
static uint8_t scratch_raw[32];
static coap_rw_buffer_t scratch_buf = {scratch_raw, sizeof(scratch_raw)};
/*---------------------------------------------------------------------------*/
static uint16_t plen;
static uint16_t dat_p;
static uint8_t retry_count;
volatile uint8_t new_packet;
static uint8_t dhcp_counter;
static uint16_t temp_result;
static uint8_t timer1_counter;
static uint8_t temp_sampl_cnt;
static uint8_t buf[BUFFER_SIZE+1];
volatile uint8_t www_callback_state;
volatile uint16_t www_client_counter;
volatile uint16_t temperature_counter;
/*---------------------------------------------------------------------------*/
static uint8_t gwip[4];
static uint8_t gwmac[6];
static uint8_t netmask[4];
static uint8_t otherside_www_ip[4];
static uint8_t myip[4] = {192,168,0,73};
static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x29};
/*---------------------------------------------------------------------------*/
static const uint16_t broadcastport = 0xFFFF;
static const uint8_t broadcastip[4] = {255,255,255,255};
static const uint8_t broadcastmac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
/*---------------------------------------------------------------------------*/
ISR(TIMER1_COMPA_vect) /* 100ms intervals */
{
    dhcp_counter++;
    timer1_counter++;
    www_client_counter++;  
    temperature_counter++;      
    if(dhcp_counter == 60)
    {
        dhcp_counter = 0;
        dhcp_6sec_tick();
    }
}
/*---------------------------------------------------------------------------*/
void browserresult_callback(uint16_t webstatuscode,uint16_t datapos,uint16_t len)
{  
    #if 1
        dbg(PSTR("> -------------------------------------\r\n"));
        dbg(PSTR("> Status code: %d\r\n"),webstatuscode);
        dbg(PSTR("> Datapos: %d\r\n"),datapos);
        dbg(PSTR("> Len: %d\r\n"),len);
        dbg(PSTR("> -------------------------------------\r\n"));
    #endif

    #if 1
        uint16_t q;        
        dbg(PSTR("> Complete HTTP message: \r\n"));    
        dbg(PSTR("> -------------------------------------\r\n"));        
        
        for(q=datapos;q<(datapos+len);q++)
        {
            dbg(PSTR("%c"),buf[q]);
        }    
        
        dbg(PSTR("\r\r> -------------------------------------\r\n"));        
    #endif

    #if 1
        uint16_t qx;
        uint16_t qxx;        

        dbg(PSTR("> -------------------------------------\r\n"));
        dbg(PSTR("> Returned actual message: \r\n"));
        dbg(PSTR("> -------------------------------------\r\n"));        

        /* This loop finds the location of actual data that comes with the HTTP response */
        for(qx=datapos;qx<(datapos+len-4);qx++)
        {
            if((buf[qx+0] == '\r') && (buf[qx+1] == '\n') && (buf[qx+2] == '\r') && (buf[qx+3] == '\n'))
            {        
                for (qxx = qx+4; qxx < datapos+len; ++qxx)
                {
                    /* print the actual contents */
                    dbg(PSTR("%c"),buf[qxx]);

                    /* break the loop */
                    qx = 0xFFF0;
                }
            }
        }

        dbg(PSTR("> -------------------------------------\r\n"));
    #endif

    www_callback_state = 1;   
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
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(www_server_thread(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        PT_WAIT_UNTIL(pt,check_flag(new_packet,www_server_flag));

        if(dat_p > 0)
        {         
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
                    /* we dont have any website in this location */
                    dat_p=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n<h1>404 Not Found</h1>"));            
                    
                    /* send the data */
                    www_server_reply(buf,dat_p);
                }
            }
        }

        clear_flag(new_packet,www_server_flag);
    }    

    PT_END(pt);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(temperature_thread(struct pt *pt))
{
    PT_BEGIN(pt);

    init_adc(5);
    disable_digital_buffer(5);

    while(1)
    {
        cli();
        
        temperature_counter = 0;
        
        sei();

        PT_WAIT_UNTIL(pt,temperature_counter > 750);

        temp_result = 0;
        temp_sampl_cnt = 0;
     
        for(temp_sampl_cnt = 0; temp_sampl_cnt < 64; temp_sampl_cnt++)
        {    
            start_conversion();
        
            PT_WAIT_UNTIL(pt,!is_adc_busy());

            temp_result += read_adc();
        }        

        dbg(PSTR("> ADC result: %u\r\n"),temp_result >> 6);
    }

    PT_END(pt);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(udp_server_thread(struct pt *pt))
{
    PT_BEGIN(pt);

    uint16_t payloadlen;
    static uint8_t str[64];

    while(1)
    {
        PT_WAIT_UNTIL(pt,check_flag(new_packet,udp_server_flag));

        if((buf[IP_PROTO_P]==IP_PROTO_UDP_V) && (buf[UDP_DST_PORT_H_P]==(MYUDPPORT>>8)) && (buf[UDP_DST_PORT_L_P]==(MYUDPPORT&0xff)))
        {
            /* calculate the udp message length */
            payloadlen=buf[UDP_LEN_L_P]-UDP_HEADER_LEN;                            

            /* replace the newline with string terminator */
            buf[UDP_DATA_P+payloadlen-1] = '\0';

            /* add some explanatory header to it */
            sprintf(str,"> udp got: %s\r\n",buf+UDP_DATA_P);                    

            /* send the same message with little modification to the sender */     
            make_udp_reply_from_request(buf,str,strlen(str),MYUDPPORT);                                       
        }

        clear_flag(new_packet,udp_server_flag);
    }

    PT_END(pt);
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

        temp = (data_array[2]<<8)+data_array[3];

        dbg(PSTR("> Batt: %u\r\n"),temp);

        counter = (data_array[4]<<24)+(data_array[5]<<16)+(data_array[6]<<8)+data_array[7]; 

        dbg(PSTR("> Counter: %u\r\n"),counter);

    }

    PT_END(pt);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(coap_server_thread(struct pt *pt))
{
    PT_BEGIN(pt);

    uint16_t xt;
    uint16_t tmp;

    while(1)
    {
        PT_WAIT_UNTIL(pt,check_flag(new_packet,coap_server_flag));

        if((buf[IP_PROTO_P]==IP_PROTO_UDP_V) && (buf[UDP_DST_PORT_H_P]==(COAPPORT>>8)) && (buf[UDP_DST_PORT_L_P]==(COAPPORT&0xff)))
        {
            /* UDP message length calculation */
            xt = ((buf[UDP_LEN_H_P]<<8)+buf[UDP_LEN_L_P])-8;

            #if 1

                if(coap_parse(&pkt,buf+UDP_DATA_P,xt) != 0)
                {
                    dbg(PSTR("\r\n> Bad coap packet\r\n"));             
                }
                else
                {            
                    coap_handle_req(&scratch_buf, &pkt, &rsppkt);
                    
                    xt = sizeof(response);

                    coap_build(response, &xt, &rsppkt);
                    
                    make_udp_reply_from_request(buf,response,xt,COAPPORT);                                       
                }

            #endif
        }

        clear_flag(new_packet,coap_server_flag);
    }

    PT_END(pt);
}/*---------------------------------------------------------------------------*/
static
PT_THREAD(udp_broadcast_thread(struct pt *pt))
{
    PT_BEGIN(pt);

    while(1)
    {
        /* TODO: Add debouncing ... */
        PT_WAIT_UNTIL(pt,btn1_pressed());
        PT_WAIT_UNTIL(pt,!btn1_pressed());
                
        send_udp_prepare(buf,broadcastport,broadcastip,broadcastport,broadcastmac);
            buf[UDP_DATA_P+0] = 'h';
            buf[UDP_DATA_P+1] = 'e';
            buf[UDP_DATA_P+2] = 'l';
            buf[UDP_DATA_P+3] = 'l';
            buf[UDP_DATA_P+4] = 'o';
        send_udp_transmit(buf,5);
    }

    PT_END(pt);
}
/*---------------------------------------------------------------------------*/
uint8_t return_counter()
{
    dbg(PSTR("> %d\r\n"),www_client_counter);
    return 0;
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(www_client_thread(struct pt *pt))
{
    PT_BEGIN(pt);    

    while(1)
    {
        /* TODO: Add debouncing ... */
        PT_WAIT_UNTIL(pt,btn2_pressed());
        PT_WAIT_UNTIL(pt,!btn2_pressed());

        dbg(PSTR("> Web client starts.\r\n"));

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
            PT_WAIT_UNTIL(pt,dnslkup_haveanswer()||www_client_timeout());

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
                
                client_browse_url(
                    PSTR("/ws.php?c="), /* constant part of the URL suffix */
                    "Samsun", /* variable part of the URL which comes after the constant suffix */
                    URL_base_address, /* base-name of the web page we want to browse */
                    &browserresult_callback, /* callback function for our URL browsing attempt */
                    otherside_www_ip, /* IP representation of the webpage host */
                    gwmac /* mac address of our gateway */
                );                

                /* wait ... */
                PT_WAIT_UNTIL(pt,www_client_failed() || www_client_timeout() || www_client_ok() || return_counter() );

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

            dbg(PSTR("> -------------------------------------\r\n"));
        }
        else
        {
            dbg(PSTR("> Browsing failed!\r\n"));     
            dbg(PSTR("> -------------------------------------\r\n"));
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
    PT_INIT(&www_client_pt);
    PT_INIT(&www_server_pt);
    PT_INIT(&udp_server_pt);
    PT_INIT(&coap_server_pt);    
    PT_INIT(&temperature_pt);
    PT_INIT(&udp_broadcast_pt);

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
    
    /* set WWW server port */
    www_server_port(MYWWWPORT);        
    
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

            new_packet = 0xFF;
            
            PT_SCHEDULE(blink_thread(&blink_pt));      
            PT_SCHEDULE(nrf24_thread(&nrf24_pt));                  
            PT_SCHEDULE(www_server_thread(&www_server_pt));
            PT_SCHEDULE(udp_server_thread(&udp_server_pt));
            PT_SCHEDULE(www_client_thread(&www_client_pt));
            PT_SCHEDULE(coap_server_thread(&coap_server_pt));
            PT_SCHEDULE(temperature_thread(&temperature_pt));
            PT_SCHEDULE(udp_broadcast_thread(&udp_broadcast_pt));
        }
    }    
    return 0;
}
/*---------------------------------------------------------------------------*/
