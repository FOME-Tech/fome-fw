/**
 * BMC Mini R52 (e.g.) CAN dash implementation.
 *
 * @date 2024-05-16
 * @author Nathan Schulte, (c) 2024
 */

#pragma once

// CAN frame IDs; BMW terms
//#define CAN_MINI_ASC1_ID 0x153
#define CAN_MINI_DME1_ID 0x316
//#define CAN_MINI_DME2_ID 0x329
// DME4: CEL, cruise light, EML, fuel consumption, oil temp+overheat
//#define CAN_MINI_DME4_ID 0x545
// Instrument Cluster,_1: A/C status
//#define CAN_MINI_IC_1_ID 0x615
// Instrument Cluster, 2: odometer, fuel level, clock (since battery)
//#define CAN_MINI_IC_2_ID 0x613
// Instrument Cluster, 3: trip, temp, speed, consumption, range
#define CAN_MINI_IC_3_ID 0x61a
// Instrument Cluster, 4: lights: RPM, high beams, cruise, service engine soon, brake, ABS, turn signals
#define CAN_MINI_IC_4_ID 0x61f

// R52 tach has sent back frames like:
//  can0  630   [8]  02 00 00 00 00 00 00 00
//  can0  630   [8]  02 50 00 00 00 00 00 00
//  can0  630   [8]  02 54 00 00 00 00 00 00
//  can0  630   [8]  02 70 00 00 00 00 00 00
//  can0  630   [8]  02 74 00 00 00 00 00 00
//  can0  630   [8]  02 7C 00 00 00 00 00 00

// 0x316 -- needs at least every 500ms
// 0x61a -- needs at least every 1000ms?
// 0x61f -- needs at least every 1000ms?
