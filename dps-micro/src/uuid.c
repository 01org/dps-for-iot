/*
 *******************************************************************
 *
 * Copyright 2016 Intel Corporation All rights reserved.
 *
 *-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 */

#define _CRT_RAND_S

#include <stdint.h>
#include <dps/dps.h>
#include <dps/dbg.h>
#include <dps/uuid.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if DPS_TARGET == DPS_TARGET_ZEPHYR
#include <rtc.h>
#include <entropy.h>
#endif

/*
 * Debug control for this module
 */
DPS_DEBUG_CONTROL(DPS_DEBUG_ON);

const char* DPS_UUIDToString(const DPS_UUID* uuid)
{
    static const char* hex = "0123456789abcdef";
    static char str[38];
    char* dst = str;
    const uint8_t *src = uuid->val;
    size_t i;

    for (i = 0; i < sizeof(uuid->val); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            *dst++ = '-';
        }
        *dst++ = hex[*src >> 4];
        *dst++ = hex[*src++ & 0xF];
    }
    *dst = 0;
    return str;
}

static struct {
    uint64_t nonce[2];
    uint32_t seeds[4];
} entropy;

#if DPS_TARGET == DPS_TARGET_WINDOWS
static void InitUUID(void)
{
    errno_t ret = 0;
    int i;
    uint32_t* n = (uint32_t*)&entropy;

    for (i = 0; i < (sizeof(entropy) / sizeof(uint32_t)); ++i) {
        rand_s(n++);
    }
}
#elif DPS_TARGET == DPS_TARGET_LINUX
/*
 * Linux specific implementation
 */
static const char* randPath = "/dev/urandom";

static void InitUUID()
{
    while (!entropy.nonce[0]) {
        FILE* f = fopen(randPath, "r");
        if (!f) {
            DPS_ERRPRINT("fopen(\"%s\", \"r\") failed\n", randPath);
            break;
        }
        size_t n = fread(&entropy, 1, sizeof(entropy), f);
        if (n != sizeof(entropy)) {
            DPS_ERRPRINT("fread(\"%s\", \"r\") failed\n", randPath);
        }
        fclose(f);
    }
}
#elif DPS_TARGET == DPS_TARGET_ZEPHYR

static void InitUUID()
{
    struct device* dev = device_get_binding(CONFIG_ENTROPY_NAME);
	if (!dev) {
		DPS_DBGPRINT("No entropy device\n");
    } else {
        int ret = entropy_get_entropy(dev, (void*)&entropy, sizeof(entropy));
        if (ret) {
            DPS_DBGPRINT("Failed to get entropy\n");
        }
    }
}

#else
#error "Unsupported target"
#endif


/*
 * Very simple linear congruational generator based PRNG (Lehmer/Park-Miller generator)
 */
#define LEPRNG(n)  (uint32_t)(((uint64_t)(n) * 279470273ull) % 4294967291ul)

/*
 * This is fast - not secure
 */
void DPS_GenerateUUID(DPS_UUID* uuid)
{
    static int once = 0;
    uint64_t* s = (uint64_t*)entropy.seeds;
    uint32_t s0;

    DPS_DBGTRACE();

    if (!once) {
        once = 1;
        InitUUID();
    }
    s0 = entropy.seeds[0];
    entropy.seeds[0] = LEPRNG(entropy.seeds[1]);
    entropy.seeds[1] = LEPRNG(entropy.seeds[2]);
    entropy.seeds[2] = LEPRNG(entropy.seeds[3]);
    entropy.seeds[3] = LEPRNG(s0);
    uuid->val64[0] = s[0] ^ entropy.nonce[0];
    uuid->val64[1] = s[1] ^ entropy.nonce[1];
}

int DPS_UUIDCompare(const DPS_UUID* a, const DPS_UUID* b)
{
    uint64_t al = a->val64[0];
    uint64_t ah = a->val64[1];
    uint64_t bl = b->val64[0];
    uint64_t bh = b->val64[1];
    return (ah < bh) ? -1 : ((ah > bh) ? 1 : ((al < bl) ? -1 : (al > bl) ? 1 : 0));
}

uint64_t DPS_Rand64(void)
{
    uint64_t s0;

    s0 = entropy.seeds[0];
    entropy.seeds[0] = LEPRNG(entropy.seeds[1]);
    entropy.seeds[1] = LEPRNG(entropy.seeds[2]);
    entropy.seeds[2] = LEPRNG(entropy.seeds[3]);
    entropy.seeds[3] = LEPRNG(s0);
    s0 = entropy.seeds[1];
    s0 = (s0 << 32) | entropy.seeds[0];
    return s0;
}

/*
 * Note that uuidIn and uuidIn out may be aliased.
 */
void DPS_RandUUIDLess(const DPS_UUID* uuidIn, DPS_UUID* uuidOut)
{
    /*
     * Effectively this just subtracts a random 64 bit uint from a 128 bit uint
     */
    uint64_t l = uuidIn->val64[0] - DPS_Rand64();
    uint64_t h = uuidIn->val64[1];

    if (l >= uuidIn->val64[0]) {
        --h;
    }
    uuidOut->val64[0] = l;
    uuidOut->val64[1] = h;
}

uint32_t DPS_Rand(void)
{
    return (uint32_t)DPS_Rand64();
}
