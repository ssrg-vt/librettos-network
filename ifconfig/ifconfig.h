#ifndef _IFCONFIG_H
#define _IFCONFIG_H

void ifconfigd_waker(int32_t lid);
int32_t ifconfigd_get_lid(void);
extern uint32_t ifconfigd_ipaddr;
extern uint32_t ifconfigd_netmask;
extern uint32_t ifconfigd_mtu;
extern void (*ifconfigd_up) (void);

#endif /* _IFCONFIG_H */
