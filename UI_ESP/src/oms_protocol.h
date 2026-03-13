#ifndef OMS_PROTOCOL_H
#define OMS_PROTOCOL_H

#include <Arduino.h>

void oms_init();
void oms_loop();

void oms_startVerify();
extern unsigned long verifyCooldown;
void oms_startEnroll();
void sendReset();
void oms_set_user(const char *name, uint8_t admin);

/* callback ke UI */
void ui_verify_success(const char *name);
void ui_verify_fail();
#endif