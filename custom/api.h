#ifndef API_H
#define API_H

__attribute__((import_module("env"), import_name("set_led_status"))) void set_led_status(int status);
__attribute__((import_module("env"), import_name("delay"))) void delay(int ms);
__attribute__((import_module("env"), import_name("print_msg"))) void print_msg(int msg_id);
__attribute__((import_module("env"), import_name("check_stop"))) int check_stop();

#define EXPORT_MAIN __attribute__((export_name("app_main"))) void app_main()

#endif
