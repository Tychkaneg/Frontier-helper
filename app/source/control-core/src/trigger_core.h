#ifndef FRONTIER_TRIGGER_CORE_H
#define FRONTIER_TRIGGER_CORE_H
#include <stdint.h>
#include <stddef.h>
/* Xbox triggers are independent analog values, not wButtons flags. */
static inline int trigger_apply(uint8_t *buttons,size_t count,unsigned left,unsigned right,
 uint8_t lt,uint8_t rt,unsigned threshold){
 if(!buttons||left>=count||right>=count||left==right||threshold>=255)return 0;
 buttons[left]=lt>threshold?0x80:0;
 buttons[right]=rt>threshold?0x80:0;
 return 1;
}
#endif
