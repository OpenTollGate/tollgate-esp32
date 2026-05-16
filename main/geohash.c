#include "geohash.h"
#include <string.h>

static const char BASE32[] = "0123456789bcdefghjkmnpqrstuvwxyz";

void geohash_encode(double lat, double lon, int precision, char *out)
{
    double lat_range[2] = { -90.0, 90.0 };
    double lon_range[2] = { -180.0, 180.0 };
    uint8_t hash_bytes[16];
    int bit_count = precision * 5;
    int byte_count = (bit_count + 7) / 8;
    memset(hash_bytes, 0, sizeof(hash_bytes));

    for (int i = 0; i < bit_count; i++) {
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);

        if (i % 2 == 0) {
            double mid = (lon_range[0] + lon_range[1]) / 2.0;
            if (lon >= mid) {
                hash_bytes[byte_idx] |= (1 << bit_idx);
                lon_range[0] = mid;
            } else {
                lon_range[1] = mid;
            }
        } else {
            double mid = (lat_range[0] + lat_range[1]) / 2.0;
            if (lat >= mid) {
                hash_bytes[byte_idx] |= (1 << bit_idx);
                lat_range[0] = mid;
            } else {
                lat_range[1] = mid;
            }
        }
    }

    for (int i = 0; i < precision; i++) {
        int byte_idx = (i * 5) / 8;
        int bit_offset = (i * 5) % 8;
        uint16_t val = (hash_bytes[byte_idx] << 8);
        if (byte_idx + 1 < (int)sizeof(hash_bytes))
            val |= hash_bytes[byte_idx + 1];
        val = (val >> (16 - 5 - bit_offset)) & 0x1F;
        out[i] = BASE32[val];
    }
    out[precision] = '\0';
}
