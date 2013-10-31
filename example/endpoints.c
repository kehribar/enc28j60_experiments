/*-----------------------------------------------------------------------------
/   Obtained from: https://github.com/1248/microcoap
/----------------------------------------------------------------------------*/
#include <string.h>
#include <stdbool.h>
#include "../hal/hal.h"
#include "../microcoap/coap.h"

static char light = '0';
static char custom = 'a';

void endpoint_setup(void)
{

}

static int handle_get_well_known_core(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
    static char *rsp = "</light>,</custom>";

    return coap_make_response(
        scratch, 
        outpkt, 
        (const uint8_t *)rsp, 
        strlen(rsp), 
        id_hi, 
        id_lo, 
        COAP_RSPCODE_CONTENT, 
        COAP_CONTENTTYPE_APPLICATION_LINKFORMAT
    );
}

static int handle_get_custom(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
    return coap_make_response(
        scratch, 
        outpkt, 
        (const uint8_t *)&custom, 
        1, 
        id_hi, 
        id_lo, 
        COAP_RSPCODE_CONTENT, 
        COAP_CONTENTTYPE_TEXT_PLAIN
    );
}

static int handle_get_light(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
    return coap_make_response(
        scratch, 
        outpkt, 
        (const uint8_t *)&light, 
        1, 
        id_hi, 
        id_lo, 
        COAP_RSPCODE_CONTENT, 
        COAP_CONTENTTYPE_TEXT_PLAIN
    );
}

static int handle_put_light(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
    if (inpkt->payload.len == 0)
    {
        return coap_make_response(
            scratch, 
            outpkt, 
            NULL, 
            0, 
            id_hi, 
            id_lo, 
            COAP_RSPCODE_BAD_REQUEST, 
            COAP_CONTENTTYPE_TEXT_PLAIN
        );
    }

    if (inpkt->payload.p[0] == '1')
    {
        light = '1';
        led2_high();    
    }
    else
    {
        light = '0';
        led2_low();
    }

    return coap_make_response(
        scratch, 
        outpkt, 
        (const uint8_t *)&light, 
        1, 
        id_hi, 
        id_lo, 
        COAP_RSPCODE_CHANGED, 
        COAP_CONTENTTYPE_TEXT_PLAIN
    );
}

static int handle_put_custom(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt, coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo)
{
    if (inpkt->payload.len == 0)
    {
        return coap_make_response(
            scratch, 
            outpkt, 
            NULL, 
            0, 
            id_hi, 
            id_lo, 
            COAP_RSPCODE_BAD_REQUEST, 
            COAP_CONTENTTYPE_TEXT_PLAIN
        );
    }

    custom = inpkt->payload.p[0];

    uint16_t i;

    xprintf(PSTR("> "));
    for (i = 0; i < inpkt->payload.len; ++i)
    {
        xprintf(PSTR("%c"),inpkt->payload.p[i]);
    }    
    xprintf(PSTR("\r\n"));
    
    return coap_make_response(
        scratch, 
        outpkt, 
        (const uint8_t *)&custom, 
        1, 
        id_hi, 
        id_lo, 
        COAP_RSPCODE_CHANGED, 
        COAP_CONTENTTYPE_TEXT_PLAIN
    );
}

static const coap_endpoint_path_t path_light = {1, {"light"}};
static const coap_endpoint_path_t path_custom = {1, {"custom"}};
static const coap_endpoint_path_t path_well_known_core = {2, {".well-known", "core"}};

const coap_endpoint_t endpoints[] =
{
    {COAP_METHOD_GET, handle_get_well_known_core, &path_well_known_core},
    {COAP_METHOD_GET, handle_get_light, &path_light},
    {COAP_METHOD_PUT, handle_put_light, &path_light},
    {COAP_METHOD_GET, handle_get_custom, &path_custom},
    {COAP_METHOD_PUT, handle_put_custom, &path_custom},
    {(coap_method_t)0, NULL, NULL}
};
