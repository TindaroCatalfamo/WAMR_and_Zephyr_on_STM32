#include "api.h"

EXPORT_MAIN {
    int stato_led = 0;
    
    while(check_stop() == 0) {
        set_led_status(stato_led);
        stato_led = !stato_led; 
        delay(100);
    }
}
