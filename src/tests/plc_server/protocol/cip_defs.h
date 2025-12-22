#pragma once

/* General Services (all objects) */
#define CIP_SRV_GET_ATTR_ALL 0x01
#define CIP_SRV_SET_ATTR_ALL 0x02
#define CIP_SRV_GET_ATTR_LIST 0x03
#define CIP_SRV_SET_ATTR_LIST 0x04
#define CIP_SRV_GET_ATTR_SINGLE 0x0E
#define CIP_SRV_SET_ATTR_SINGLE 0x10

/* Message Router Services */
#define CIP_SRV_MULTI_REQUEST 0x0A

/* Connection Manager Services */
#define CIP_SRV_FORWARD_CLOSE 0x4E
#define CIP_SRV_FORWARD_OPEN 0x54
#define CIP_SRV_LARGE_FORWARD_OPEN 0x5B

/* common tag services*/
#define CIP_SRV_READ_TAG 0x4C
#define CIP_SRV_WRITE_TAG 0x4D

/* Symbol Object Services (Rockwell/AB) */
#define CIP_SRV_READ_TAG_FRAG_AB 0x52
#define CIP_SRV_WRITE_TAG_FRAG_AB 0x53

/* Special CIP Services Rockwell/AB*/
#define CIP_SRV_INSTANCE_ATTRS_AB 0x55
#define CIP_SRV_EXEC_PCCC_AB 0x4B

/* Symbol Object Services (Omron) */
#define CIP_SRV_GET_INSTANCE_LIST_OMRON 0x5F
