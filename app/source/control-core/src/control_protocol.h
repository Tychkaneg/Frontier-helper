#ifndef FRONTIER_CONTROL_PROTOCOL_H
#define FRONTIER_CONTROL_PROTOCOL_H
#include "camera_core.h"
#define CONTROL_MAGIC UINT32_C(0x5442524f) /* ORBT in little endian */
#define CONTROL_VERSION 1
#define CONTROL_TIMEOUT_MS 2000
#define CONTROL_MAX_PACKET 256
#define CONTROL_REPLY_BIT 0x8000
enum ControlCommand {CONTROL_HELLO=1,CONTROL_STATUS,CONTROL_SETTINGS,
 CONTROL_ENABLE,CONTROL_DISABLE,CONTROL_RESET,CONTROL_PING};
enum ControlError {CONTROL_OK,CONTROL_BAD_PACKET,CONTROL_BAD_SETTINGS,
 CONTROL_NEED_HELLO,CONTROL_NEED_SETTINGS,CONTROL_ENGINE_UNAVAILABLE};
#pragma pack(push,4)
typedef struct {
 uint32_t magic;
 uint16_t version,command;
 uint32_t request_id,payload_size,reserved;
} ControlHeader;
typedef struct {
 uint32_t error;
 int32_t engine_status;
 uint32_t game_pid,peer_pid,enabled,scene,pad_present,in_flight;
 uint64_t process_created,accepted_revision,applied_revision,frames;
 uint64_t reset_accepted,reset_applied;
 CameraSettings settings;
 float requested_radius,actual_radius,reference_radius,reference_height,yaw,pitch;
} ControlReply;
#pragma pack(pop)
_Static_assert(sizeof(ControlHeader)==20,"header ABI");
_Static_assert(sizeof(ControlReply)==152,"reply ABI");
static inline int control_header_valid(const ControlHeader *h,size_t packet_size) {
 if(h->magic!=CONTROL_MAGIC||h->version!=CONTROL_VERSION||h->reserved!=0||
  h->request_id==0||h->command<CONTROL_HELLO||h->command>CONTROL_PING)return 0;
 uint32_t expected=h->command==CONTROL_SETTINGS?sizeof(CameraSettings):0;
 return h->payload_size==expected&&packet_size==sizeof(*h)+expected;
}
#endif
