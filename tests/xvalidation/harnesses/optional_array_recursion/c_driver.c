/* Optional self-referential array: Employee struct contains
 * `Employee teamMembers[] if title == TEAM_LEAD`. Self-ref via pointer
 * to dynamically-allocated array, so no incomplete-type gap. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "optional_members/optional_array_recursion/employee.h"
#include "optional_members/optional_array_recursion/title.h"

static char name_buffers[16][32];
static int names_used = 0;

static void set_name(optional_members_optional_array_recursion_employee_t *e, const char *prefix, unsigned seed)
{
    int idx = names_used++;
    if (idx >= 16) idx = 0; /* reuse if we run out */
    int n = snprintf(name_buffers[idx], sizeof(name_buffers[idx]), "%s-%u", prefix, seed);
    e->name.bytes = name_buffers[idx];
    e->name.length = (size_t)n;
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    optional_members_optional_array_recursion_employee_t pkg;
    optional_members_optional_array_recursion_employee_init(&pkg);

    set_name(&pkg, "lead", seed);
    pkg.salary = (uint16_t)(seed * 7u);
    pkg.title = optional_members_optional_array_recursion_TITLE_TEAM_LEAD;
    pkg.team_members_count = 3u;
    pkg.team_members = (optional_members_optional_array_recursion_employee_t *)calloc(
            pkg.team_members_count, sizeof(*pkg.team_members));
    for (size_t i = 0; i < pkg.team_members_count; ++i) {
        optional_members_optional_array_recursion_employee_init(&pkg.team_members[i]);
        set_name(&pkg.team_members[i], "dev", seed + (unsigned)i);
        pkg.team_members[i].salary = (uint16_t)(seed + (unsigned)i * 11u);
        pkg.team_members[i].title = optional_members_optional_array_recursion_TITLE_DEVELOPER;
    }
    pkg.team_members_present = true;

    uint8_t buf[256];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = optional_members_optional_array_recursion_employee_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}
