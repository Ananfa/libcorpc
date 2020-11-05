#ifndef corpc_crc_h
#define corpc_crc_h

#include "stdint.h"

namespace corpc {
    class CRC {
    public:
        static uint16_t MbTable[256];
        static uint16_t CheckSum(uint8_t *data, uint16_t crc16, int len);
        static uint16_t Reverse(uint16_t data);
        static void InitTable(uint16_t polynomial);
    };
}

#endif /* corpc_crc_h */
