#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <stdio.h>
#include <string.h>
#include "wasm_export.h"

#define MAX_CONCURRENT_MODULES 2    
#define MAX_WASM_FILE_SIZE     2048    
#define WASM_APP_STACK_SIZE    2048    
#define WASM_APP_HEAP_SIZE     2048    
#define THREAD_STACK_SIZE      12288   
#define GLOBAL_HEAP_SIZE       32768

const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

K_MUTEX_DEFINE(led_mutex);
K_MUTEX_DEFINE(console_mutex);

typedef struct {
    int id;
    bool in_use;
    volatile bool should_stop; 
    int file_size;
    uint8_t wasm_buf[MAX_WASM_FILE_SIZE];
    struct k_thread thread_data; 
    wasm_exec_env_t env;
} wasm_task_ctx;

K_THREAD_STACK_ARRAY_DEFINE(task_stacks, MAX_CONCURRENT_MODULES, THREAD_STACK_SIZE);
static wasm_task_ctx wasm_tasks[MAX_CONCURRENT_MODULES];

void set_led_status_wrapper(wasm_exec_env_t exec_env, int status) {
    k_mutex_lock(&led_mutex, K_FOREVER); 
    gpio_pin_set_dt(&led, status);
    k_mutex_unlock(&led_mutex);          
}

void delay_wrapper(wasm_exec_env_t exec_env, int ms) {
    k_msleep(ms);
}

void print_msg_wrapper(wasm_exec_env_t exec_env, int msg_id) {
    k_mutex_lock(&console_mutex, K_FOREVER); 
    
    if (msg_id == 1) {
        printf("Hello!\n");
    } else if (msg_id == 2) {
        printf("Hey!\n");
    } else {
        printf("Unknown message\n");
    }
    
    k_mutex_unlock(&console_mutex);          
}

int check_stop_wrapper(wasm_exec_env_t exec_env) {
    for (int i = 0; i < MAX_CONCURRENT_MODULES; i++) {
        if (wasm_tasks[i].in_use && wasm_tasks[i].env == exec_env) {
            if (wasm_tasks[i].should_stop) {
                return 1;
            } else {
                return 0;
            }
        }
    }
    return 0;
}

static NativeSymbol native_symbols[] = {
    { "set_led_status", set_led_status_wrapper, "(i)", NULL },
    { "delay", delay_wrapper, "(i)", NULL },
    { "print_msg", print_msg_wrapper, "(i)", NULL },
    { "check_stop", check_stop_wrapper, "()i", NULL } 
};

void wasm_executor_entry(void *arg1, void *arg2, void *arg3) {
    wasm_task_ctx *ctx = (wasm_task_ctx *)arg1; 
    printf("\n[TASK %d] Started! (Size: %d bytes)\n", ctx->id, ctx->file_size);

    char error_buf[128];
    wasm_module_t module = wasm_runtime_load(ctx->wasm_buf, ctx->file_size, error_buf, sizeof(error_buf));
    
    if (module) {
        wasm_module_inst_t module_inst = wasm_runtime_instantiate(module, WASM_APP_STACK_SIZE, WASM_APP_HEAP_SIZE, error_buf, sizeof(error_buf));
        if (module_inst) {
            wasm_function_inst_t func = wasm_runtime_lookup_function(module_inst, "app_main");
            if (func) {
                wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(module_inst, WASM_APP_STACK_SIZE);
                
                ctx->env = exec_env; 
                
                if (!wasm_runtime_call_wasm(exec_env, func, 0, NULL)) {
                    printf("[TASK %d ERROR] Crash: %s\n", ctx->id, wasm_runtime_get_exception(module_inst));
                }
                
                wasm_runtime_destroy_exec_env(exec_env);
            } else {
                 printf("[TASK %d ERROR] 'app_main' function not found!\n", ctx->id);
            }
            wasm_runtime_deinstantiate(module_inst);
        } else {
            printf("[TASK %d ERROR] Instantiate: %s\n", ctx->id, error_buf);
        }
        wasm_runtime_unload(module);
    } else {
        printf("[TASK %d ERROR] Load: %s\n", ctx->id, error_buf);
    }

    printf("[TASK %d] Module terminated and destroyed. Slot free!\n", ctx->id);
    gpio_pin_set_dt(&led, 0);
    ctx->in_use = false; 
}


void listener_thread(void *a, void *b, void *c) {
    if (!device_is_ready(uart_dev) || !gpio_is_ready_dt(&led)) {
        return;
    }
    
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    uart_irq_rx_disable(uart_dev);

    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    static char global_heap_buf[GLOBAL_HEAP_SIZE];
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);

    if (!wasm_runtime_full_init(&init_args)) {
        return;
    }
    
    wasm_runtime_register_natives("env", native_symbols, sizeof(native_symbols) / sizeof(NativeSymbol));

    for (int i = 0; i < MAX_CONCURRENT_MODULES; i++) {
        wasm_tasks[i].id = i;
        wasm_tasks[i].in_use = false;
        wasm_tasks[i].should_stop = false;
    }

    

    uint8_t temp_buf[MAX_WASM_FILE_SIZE]; 

    while (1) {
        uint8_t dummy;
        
        while (uart_poll_in(uart_dev, &dummy) == 0) {
            
        }
        
        int bin_size = 0, hex_count = 0;
        bool receiving_file = false;
        bool receiving_kill = false;
        char hex_pair[2];
        uint8_t ch;

        while (1) {
            if (uart_poll_in(uart_dev, &ch) == 0) {
                
                if (!receiving_file && !receiving_kill) {
                    if (ch == 'G') { 
                        receiving_file = true; 
                        bin_size = 0; 
                        hex_count = 0; 
                    } else if (ch == 'K') { 
                        receiving_kill = true; 
                    }
                } else if (receiving_kill) {
                    int slot = ch - '0';
                    if (slot >= 0 && slot < MAX_CONCURRENT_MODULES && wasm_tasks[slot].in_use) {
                        printf("\n[LISTENER] KILL signal received. Shutting down Slot %d...\n", slot);
                        wasm_tasks[slot].should_stop = true;
                    }
                    receiving_kill = false;
                    break;
                } else if (receiving_file) {
                    if (ch == 'H') {
                        break;
                    }
                    
                    if (ch >= '0' && ch <= '9') {
                        ch -= '0';
                    } else if (ch >= 'A' && ch <= 'F') {
                        ch = ch - 'A' + 10;
                    } else if (ch >= 'a' && ch <= 'f') {
                        ch = ch - 'a' + 10;
                    } else {
                        continue;
                    }

                    hex_pair[hex_count++] = ch;
                    if (hex_count == 2) {
                        if (bin_size < MAX_WASM_FILE_SIZE) {
                            temp_buf[bin_size++] = (hex_pair[0] << 4) | hex_pair[1];
                        }
                        hex_count = 0;
                    }
                }
            } else {
                if (!receiving_file) {
                    k_msleep(10); 
                }
            }
        }

        if (bin_size > 0) {
            int free_slot = -1;
            for (int i = 0; i < MAX_CONCURRENT_MODULES; i++) {
                if (!wasm_tasks[i].in_use) { 
                    free_slot = i; 
                    break; 
                }
            }

            if (free_slot != -1) {
                wasm_tasks[free_slot].in_use = true;
                wasm_tasks[free_slot].should_stop = false; 
                wasm_tasks[free_slot].file_size = bin_size;
                memcpy(wasm_tasks[free_slot].wasm_buf, temp_buf, bin_size);
                
                printf("[LISTENER] File loaded. Assigning Slot %d\n", free_slot);
                k_thread_create(&wasm_tasks[free_slot].thread_data, task_stacks[free_slot], K_THREAD_STACK_SIZEOF(task_stacks[free_slot]), 
                                wasm_executor_entry, &wasm_tasks[free_slot], NULL, NULL, 5, 0, K_NO_WAIT);
            } else {
                printf("[LISTENER] All slots full!\n");
            }
        }
    }
}
K_THREAD_DEFINE(listener_id, 4096, listener_thread, NULL, NULL, NULL, 6, 0, 0);
