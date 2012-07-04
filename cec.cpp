#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include <interface/vmcs_host/vc_cecservice.h>
#include <interface/vchiq_arm/vchiq_if.h>


#ifdef __cplusplus
}
#endif

#include <sys/socket.h>
#include "xbmcclient.h"
#include "config.h"
#include "Key.h"

#ifndef VC_TRUE
#define VC_TRUE 1
#endif

/* Remove this block when everyone is using the latest headers from 
 * RPi firmware github page
 */
#if 1
extern int32_t vchi_initialise( VCHI_INSTANCE_T *instance_handle );

extern int32_t vchi_exit( void );

extern int32_t vchi_connect( VCHI_CONNECTION_T **connections,
        const uint32_t num_connections,
        VCHI_INSTANCE_T instance_handle );
#endif

static CXBMCClient xbmc;
static bool repeating = false;

volatile int might_be_dimmed=0;
int port = 80;


static void xbmc_key(const char *key)
{
	xbmc.SendButton(key, "R1", BTN_NO_REPEAT);
}

static void xbmc_keyr(const char *key)
{
	xbmc.SendButton(key, "R1", BTN_DOWN);
}

static void xbmc_release_button()
{
	xbmc.SendButton(0x01, BTN_UP);
}

void button_pressed(uint32_t param)
{
    uint32_t initiator, follower, opcode, operand1, operand2;

    initiator = CEC_CB_INITIATOR(param);
    follower  = CEC_CB_FOLLOWER(param);
    opcode    = CEC_CB_OPCODE(param);
    operand1  = CEC_CB_OPERAND1(param);
    operand2  = CEC_CB_OPERAND2(param);

    if (opcode != CEC_Opcode_UserControlPressed && opcode != CEC_Opcode_VendorRemoteButtonDown) {
        printf("button_pressed: unknown operand operand1=0x%x: "
                "initiator=0x%x, follower=0x%x, opcode=0x%x, "
                "operand1=0x%x, operand2=0x%x\n",
                operand1, initiator, follower, opcode, operand1, operand2);
        return;
    }

	static const struct {
		uint32_t code;
		bool can_repeat;
		void (*key_fn) (const char *key);
		const char *key;
	} translation_table [] = {
		{ CEC_User_Control_Select, false, xbmc_key, "select"},
		{ CEC_User_Control_Up, true, xbmc_keyr, "up"},
		{ CEC_User_Control_Down, true, xbmc_keyr, "down"},
		{ CEC_User_Control_Left, true, xbmc_keyr, "left"},
		{ CEC_User_Control_Right, true, xbmc_keyr, "right"},
		{ CEC_User_Control_Exit, false, xbmc_key, "title"},
		{ CEC_User_Control_F2Red, false, xbmc_key, "red"},
		{ CEC_User_Control_F3Green, false, xbmc_key, "green"},
		{ CEC_User_Control_F4Yellow, false, xbmc_key, "yellow"},
		{ CEC_User_Control_F1Blue, false, xbmc_key, "blue"},
		{ 0x91, false, xbmc_key, "back"},
		{ CEC_User_Control_Number1, false, xbmc_key, "one"},
		{ CEC_User_Control_Number2, false, xbmc_key, "two"},
		{ CEC_User_Control_Number3, false, xbmc_key, "three"},
		{ CEC_User_Control_Number4, false, xbmc_key, "four"},
		{ CEC_User_Control_Number5, false, xbmc_key, "five"},
		{ CEC_User_Control_Number6, false, xbmc_key, "six"},
		{ CEC_User_Control_Number7, false, xbmc_key, "seven"},
		{ CEC_User_Control_Number8, false, xbmc_key, "eight"},
		{ CEC_User_Control_Number9, false, xbmc_key, "nine"},
		{ CEC_User_Control_Number0, false, xbmc_key, "zero"},
		{ CEC_User_Control_Play, false, xbmc_key, "play"},
		{ CEC_User_Control_Pause, false, xbmc_key, "pause"},
		{ CEC_User_Control_Stop, false, xbmc_key, "stop"},
		{ CEC_User_Control_Rewind, false, xbmc_key, "skipminus"},
		{ CEC_User_Control_FastForward, false, xbmc_key, "skipplus"},
		{ CEC_User_Control_RootMenu, false, xbmc_key, "previousmenu"},
		{ CEC_User_Control_SetupMenu, false, xbmc_key, "title"},
		{ CEC_User_Control_DisplayInformation, false, xbmc_key, "title"}
};
	for (unsigned int i = 0; i< sizeof(translation_table)/sizeof(translation_table[0]); i++)
	{
		if (translation_table[i].code == operand1) {
			translation_table[i].key_fn(translation_table[i].key);
			repeating = translation_table[i].can_repeat;
			return;
		}
	}

    printf("button_pressed: operand1=0x%x has no binding\n", operand1);
}

void cec_callback(void *callback_data, uint32_t param0,
        uint32_t param1, uint32_t param2,
        uint32_t param3, uint32_t param4)
{
    VC_CEC_NOTIFY_T reason;
    uint32_t len, retval;

    reason  = (VC_CEC_NOTIFY_T) CEC_CB_REASON(param0);
    len     = CEC_CB_MSG_LENGTH(param0);
    retval  = CEC_CB_RC(param0);

#ifdef DEBUG
    printf("cec_callback: debug: "
            "reason=0x%04x, len=0x%02x, retval=0x%02x, "
            "param1=0x%08x, param2=0x%08x, param3=0x%08x, param4=0x%08x\n",
            reason, len, retval, param1, param2, param3, param4);
#endif

    if ( reason == VC_CEC_BUTTON_PRESSED || reason == VC_CEC_REMOTE_PRESSED) {

        if ( len > 4 ) {
            printf("cec_callback: warning: len > 4, only using first parameter "
                    "reason=0x%04x, len=0x%02x, retval=0x%02x, "
                    "param1=0x%08x, param2=0x%08x, param3=0x%08x, param4=0x%08x\n",
                    reason, len, retval, param1, param2, param3, param4);
        }
        button_pressed(param1);
	} else if ((reason == VC_CEC_BUTTON_RELEASE || reason == VC_CEC_REMOTE_RELEASE) && repeating) {
		xbmc_release_button();
	} else if (reason == VC_CEC_RX && CEC_CB_OPCODE(param1) == CEC_Opcode_Play)
	{
		if (CEC_CB_OPERAND1(param1) == CEC_PLAY_FORWARD) {
     		xbmc_key("play");		
		} else if (CEC_CB_OPERAND1(param1) == CEC_PLAY_STILL) {
			xbmc_key("pause");
		}
	} else if (reason == VC_CEC_RX && CEC_CB_OPCODE(param1) == CEC_Opcode_DeckControl) {
		if (CEC_CB_OPERAND1(param1) == CEC_DECK_CTRL_STOP) {
			xbmc_key("stop");
		}
    } else if (reason == VC_CEC_RX && CEC_CB_OPCODE(param1) == CEC_Opcode_MenuRequest ) {
        if (CEC_CB_OPERAND1(param1) == CEC_MENU_STATE_QUERY ) {
            uint8_t msg[2];
            uint32_t initiator;
            initiator = CEC_CB_INITIATOR(param1);
            msg[0] = CEC_Opcode_MenuStatus;
            msg[1] = CEC_MENU_STATE_ACTIVATED;
            vc_cec_send_message(initiator, msg, 2, VC_TRUE);
        }
    } else if ( reason != VC_CEC_BUTTON_RELEASE ) {
        printf("cec_callback: unknown event: "
                "reason=0x%04x, len=0x%02x, retval=0x%02x, "
                "param1=0x%08x, param2=0x%08x, param3=0x%08x, param4=0x%08x\n",
                reason, len, retval, param1, param2, param3, param4);
    }
}

int main(int argc, char **argv)
{
    int res = 0;
    VCHI_INSTANCE_T vchiq_instance;
    VCHI_CONNECTION_T *vchi_connection;
    CEC_AllDevices_T logical_address;
    uint16_t physical_address;

    /* Make sure logs are written to disk */
    setlinebuf(stdout);
    setlinebuf(stderr);


    if (argc > 2) {
        printf("usage: %s [port]\n", argv[0]);
        return -1;
    }

    if (argc == 2) {
        port = atoi(argv[1]);
    }


    res = vchi_initialise(&vchiq_instance);
    if ( res != VCHIQ_SUCCESS ) {
        printf("failed to open vchiq instance\n");
        return -1;
    }

    res = vchi_connect( NULL, 0, vchiq_instance );
    if ( res != 0 ) {
        printf( "VCHI connection failed\n" );
        return -1;
    }

    vc_vchi_cec_init(vchiq_instance, &vchi_connection, 1);
    if ( res != 0 ) {
        printf( "VCHI CEC connection failed\n" );
        return -1;
    }

	xbmc.SendHELO("CEC Remote", ICON_NONE);

    vc_cec_register_callback(((CECSERVICE_CALLBACK_T) cec_callback), NULL);

#if 0
    vc_cec_register_all();
#endif

    vc_cec_register_command(CEC_Opcode_MenuRequest);
	vc_cec_register_command(CEC_Opcode_Play);
	vc_cec_register_command(CEC_Opcode_DeckControl);

    vc_cec_get_logical_address(&logical_address);
    printf("logical_address: 0x%x\n", logical_address);

    vc_cec_set_vendor_id(CEC_VENDOR_ID_BROADCOM);
    vc_cec_set_osd_name("XBMC");

    vc_cec_get_physical_address(&physical_address);
    printf("physical_address: 0x%x\n", physical_address);

    vc_cec_send_ActiveSource(physical_address, 0);


    while (1) {
        might_be_dimmed = 1;
        sleep(10);
    }

    vchi_exit();

    return 0;
}

