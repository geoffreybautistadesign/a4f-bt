// Run loop BTstack sur threads POSIX (pthread), identique sur Mac et PS4 (libkernel
// exporte pthread_* et clock_gettime). Calqué sur btstack_run_loop_freertos.c :
// la boucle dort sur une condition jusqu'au prochain timer, et les autres threads
// (lecture USB, son) la réveillent avec btstack_run_loop_execute_on_main_thread().

#include "btstack_run_loop_a4f.h"

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include "btstack_debug.h"
#include "btstack_linked_list.h"
#include "btstack_run_loop.h"

static pthread_mutex_t rl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  rl_cond  = PTHREAD_COND_INITIALIZER;
static bool rl_triggered;
static bool rl_exit_requested;

uint32_t btstack_run_loop_a4f_time_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) (ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
}

static void rl_set_timer(btstack_timer_source_t *ts, uint32_t timeout_in_ms){
    ts->timeout = btstack_run_loop_a4f_time_ms() + timeout_in_ms + 1;
}

static void rl_trigger(void){
    pthread_mutex_lock(&rl_mutex);
    rl_triggered = true;
    pthread_cond_signal(&rl_cond);
    pthread_mutex_unlock(&rl_mutex);
}

static void rl_trigger_exit(void){
    rl_exit_requested = true;
    rl_trigger();
}

// Attend un réveil ou timeout_ms (< 0 : sans limite).
static void rl_wait(int32_t timeout_ms){
    pthread_mutex_lock(&rl_mutex);
    if (!rl_triggered){
        if (timeout_ms < 0){
            pthread_cond_wait(&rl_cond, &rl_mutex);
        } else {
            // pthread_cond_timedwait attend une heure absolue sur CLOCK_REALTIME
            struct timespec abs;
            clock_gettime(CLOCK_REALTIME, &abs);
            abs.tv_sec  += timeout_ms / 1000;
            abs.tv_nsec += (long) (timeout_ms % 1000) * 1000000;
            if (abs.tv_nsec >= 1000000000){
                abs.tv_sec++;
                abs.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&rl_cond, &rl_mutex, &abs);
        }
    }
    rl_triggered = false;
    pthread_mutex_unlock(&rl_mutex);
}

static void rl_execute(void){
    rl_exit_requested = false;
    while (true){
        btstack_run_loop_base_poll_data_sources();

        // callbacks postés par les autres threads
        while (true){
            pthread_mutex_lock(&rl_mutex);
            btstack_context_callback_registration_t *reg =
                (btstack_context_callback_registration_t *) btstack_linked_list_pop(&btstack_run_loop_base_callbacks);
            pthread_mutex_unlock(&rl_mutex);
            if (reg == NULL) break;
            (*reg->callback)(reg->context);
        }

        uint32_t now = btstack_run_loop_a4f_time_ms();
        btstack_run_loop_base_process_timers(now);

        if (rl_exit_requested) break;
        rl_wait(btstack_run_loop_base_get_time_until_timeout(now));
    }
}

static void rl_execute_on_main_thread(btstack_context_callback_registration_t *reg){
    pthread_mutex_lock(&rl_mutex);
    btstack_run_loop_base_add_callback(reg);
    pthread_mutex_unlock(&rl_mutex);
    rl_trigger();
}

static void rl_init(void){
    // initialisation explicite : plus sûr que l'initialiseur statique avec la libkernel PS4
    pthread_mutex_init(&rl_mutex, NULL);
    pthread_cond_init(&rl_cond, NULL);
    rl_triggered = false;
    btstack_run_loop_base_init();
}

static const btstack_run_loop_t btstack_run_loop_a4f = {
    &rl_init,
    &btstack_run_loop_base_add_data_source,
    &btstack_run_loop_base_remove_data_source,
    &btstack_run_loop_base_enable_data_source_callbacks,
    &btstack_run_loop_base_disable_data_source_callbacks,
    &rl_set_timer,
    &btstack_run_loop_base_add_timer,
    &btstack_run_loop_base_remove_timer,
    &rl_execute,
    &btstack_run_loop_base_dump_timer,
    &btstack_run_loop_a4f_time_ms,
    &rl_trigger,                 // poll_data_sources_from_irq
    &rl_execute_on_main_thread,
    &rl_trigger_exit,
};

const btstack_run_loop_t *btstack_run_loop_a4f_get_instance(void){
    return &btstack_run_loop_a4f;
}
