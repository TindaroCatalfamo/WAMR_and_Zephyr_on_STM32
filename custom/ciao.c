#include "api.h"

EXPORT_MAIN {
    while(check_stop() == 0) { 
        
        print_msg(1); 
        delay(500); 
    }
}
