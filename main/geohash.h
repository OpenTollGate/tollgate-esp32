#ifndef GEOHASH_H
#define GEOHASH_H

#include <stddef.h>

void geohash_encode(double lat, double lon, int precision, char *out);

#endif
