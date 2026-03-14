/*
 * pixel_loader.c — Patch-D: PixelCode Self-Hosting Engine
 *
 * Core idea: each utility is "planted" as VM cells on the canvas,
 * then the VM runs from that position and produces output via VM_PRINT.
 * This makes utilities genuine PixelCode programs, not C functions.
 *
 * Cell encoding (per VM spec):
 *   B = opcode (VM_PRINT, VM_HALT, VM_SYSCALL, etc.)
 *   R = data byte (character to print, syscall number, etc.)
 *   G = parameter (energy, state)
 *   A = address operand
 */
#include "../include/canvasos_pixel_loader.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_fd.h"
#include "../include/canvasos_syscall.h"
#include "../include/canvas_determinism.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/canvasos_path.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdio.h>

/* ── Global execution mode ───────────────────────────── */
static int g_pxl_mode = PXL_MODE_C_FALLBACK;

void pxl_set_mode(int mode) { g_pxl_mode = mode; }
int  pxl_get_mode(void)     { return g_pxl_mode; }

/* ── Utility Registry ────────────────────────────────── */
typedef struct {
    const char *name;
    int         id;
} PxlUtilEntry;

static const PxlUtilEntry g_registry[] = {
    { "echo", PXL_UTIL_ECHO },
    { "cat",  PXL_UTIL_CAT  },
    { "info", PXL_UTIL_INFO },
    { "hash", PXL_UTIL_HASH },
    { "help", PXL_UTIL_HELP },
    { "stat",  PXL_UTIL_STAT  },
    { "ls",    PXL_UTIL_LS    },
    { "write", PXL_UTIL_WRITE },
    { "cp",    PXL_UTIL_CP    },
    { "mv",    PXL_UTIL_MV    },
    { "gate",  PXL_UTIL_GATE  },
    { "spawn", PXL_UTIL_SPAWN },
    { "pipe",  PXL_UTIL_PIPE  },
    { "sched", PXL_UTIL_SCHED },
    { NULL,    PXL_UTIL_NONE  },
};

int pxl_find_utility(const char *name) {
    if (!name) return PXL_UTIL_NONE;
    for (int i = 0; g_registry[i].name; i++)
        if (strcmp(g_registry[i].name, name) == 0)
            return g_registry[i].id;
    return PXL_UTIL_NONE;
}

/* ═══════════════════════════════════════════════════════
 * Program Planters
 *
 * Each function writes VM cells at (x, y↓) and returns
 * the number of cells planted. The caller then runs
 * vm_init(x, y) + vm_run() to execute.
 * ═══════════════════════════════════════════════════════ */

/* ── echo: PRINT each char + newline + HALT ──────────── */
int pxl_plant_echo(EngineContext *ctx, uint32_t x, uint32_t y,
                   const char *arg) {
    if (!ctx || !arg) return 0;
    int n = 0;

    /* Print each character */
    for (int i = 0; arg[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)arg[i]);
        n++;
    }

    /* Newline */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, '\n');
        n++;
    }

    /* HALT */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }

    return n;
}

/* ── cat: read virtual path and print content ────────── */
int pxl_plant_cat(EngineContext *ctx, uint32_t x, uint32_t y,
                  const char *path, ProcTable *pt) {
    if (!ctx) return 0;
    int n = 0;

    /* For cat, we need to read the file content first
     * and then plant PRINT cells for each byte.
     * This is the "self-hosting" approach: the program
     * is generated dynamically from the file content. */

    /* Try to render virtual path content */
    extern int path_resolve_virtual(EngineContext*, void*, const char*, void*);
    extern int path_render_virtual(const ProcTable*, EngineContext*,
                                   FsKey, char*, size_t);

    char content[512] = {0};
    int got_content = 0;

    /* Try virtual path */
    FsKey vkey;
    if (path && path_resolve_virtual(ctx, NULL, path, &vkey) == 0) {
        if (path_render_virtual(pt, ctx, vkey, content, sizeof(content)) == 0)
            got_content = 1;
    }

    /* Try CanvasFS bridge */
    if (!got_content && path) {
        extern int fd_open_bridged(void*, void*, uint32_t, const char*, uint8_t);
        int fd = fd_open_bridged(ctx, NULL, 0, path, 0x01 /* O_READ */);
        if (fd >= 3) {
            uint8_t buf[256];
            int nr = fd_read(ctx, 0, fd, buf, 255);
            if (nr > 0) {
                memcpy(content, buf, (size_t)nr);
                content[nr] = '\0';
                got_content = 1;
            }
            fd_close(ctx, 0, fd);
        }
    }

    if (!got_content) {
        const char *msg = "(empty)\n";
        for (int i = 0; msg[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)msg[i]);
            n++;
        }
    } else {
        for (int i = 0; content[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)content[i]);
            n++;
        }
    }

    /* HALT */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ── info: print system information ──────────────────── */
int pxl_plant_info(EngineContext *ctx, uint32_t x, uint32_t y,
                   ProcTable *pt) {
    if (!ctx) return 0;
    int n = 0;

    /* Generate info string */
    char info[256];
    uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
    int gates = 0;
    for (int i = 0; i < TILE_COUNT; i++)
        if (gate_is_open_tile(ctx, (uint16_t)i)) gates++;

    uint32_t procs = pt ? pt->count : 0;
    snprintf(info, sizeof(info),
             "tick=%u hash=0x%08X\n"
             "gates=%d/%d procs=%u/%u\n"
             "canvas=%ux%u=%u cells\n",
             ctx->tick, h,
             gates, TILE_COUNT, procs, PROC8_MAX,
             CANVAS_W, CANVAS_H, CANVAS_W * CANVAS_H);

    /* Plant PRINT cells */
    for (int i = 0; info[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)info[i]);
        n++;
    }

    /* HALT */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ── hash: print canvas hash ─────────────────────────── */
int pxl_plant_hash(EngineContext *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;
    int n = 0;

    uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
    char line[64];
    snprintf(line, sizeof(line), "canvas hash = 0x%08X  tick=%u\n",
             h, ctx->tick);

    for (int i = 0; line[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)line[i]);
        n++;
    }
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ── help: print available commands ──────────────────── */
int pxl_plant_help(EngineContext *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;
    int n = 0;

    const char *text =
        "CanvasOS Shell — PixelCode Self-Hosted\n"
        "  echo <text>    Print text\n"
        "  cat <path>     Show file/virtual path\n"
        "  info           System information\n"
        "  hash           Canvas hash\n"
        "  stat <x,y>     Inspect cell ABGR state\n"
        "  ls [dir]       List directory entries\n"
        "  write x,y A B G R  Write cell\n"
        "  cp x0,y0 x1,y1 dx,dy  Copy region\n"
        "  mv x0,y0 x1,y1 dx,dy  Move region\n"
        "  gate open|close|toggle|status [tile]\n"
        "  spawn <tile> [energy] [lane]\n"
        "  pipe create|write|read|close|status\n"
        "  sched tick [N] | sched status\n"
        "  help           This message\n"
        "  ps, kill, cd, mkdir, rm\n"
        "  det, timewarp, env, exit\n";

    for (int i = 0; text[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)text[i]);
        n++;
    }
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ── stat: inspect a single cell's full ABGR state ──── */
static const char *vm_opcode_name(uint8_t b) {
    switch (b) {
    case VM_NOP:       return "NOP";
    case VM_PRINT:     return "PRINT";
    case VM_HALT:      return "HALT";
    case VM_SET:       return "SET";
    case VM_COPY:      return "COPY";
    case VM_ADD:       return "ADD";
    case VM_SUB:       return "SUB";
    case VM_CMP:       return "CMP";
    case VM_JMP:       return "JMP";
    case VM_JZ:        return "JZ";
    case VM_JNZ:       return "JNZ";
    case VM_CALL:      return "CALL";
    case VM_RET:       return "RET";
    case VM_LOAD:      return "LOAD";
    case VM_STORE:     return "STORE";
    case VM_GATE_ON:   return "GATE_ON";
    case VM_GATE_OFF:  return "GATE_OFF";
    case VM_SEND:      return "SEND";
    case VM_RECV:      return "RECV";
    case VM_SPAWN:     return "SPAWN";
    case VM_EXIT:      return "EXIT";
    case VM_DRAW:      return "DRAW";
    case VM_LINE:      return "LINE";
    case VM_RECT:      return "RECT";
    case VM_SYSCALL:   return "SYSCALL";
    case VM_BREAKPOINT:return "BREAKPOINT";
    default:           return "???";
    }
}

int pxl_plant_stat(EngineContext *ctx, uint32_t x, uint32_t y,
                   uint32_t target_x, uint32_t target_y) {
    if (!ctx) return 0;
    int n = 0;

    /* Bounds check */
    if (target_x >= CANVAS_W || target_y >= CANVAS_H) {
        const char *err = "stat: out of bounds\n";
        for (int i = 0; err[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)err[i]);
            n++;
        }
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        return n + 1;
    }

    /* Read target cell */
    uint32_t idx = target_y * CANVAS_W + target_x;
    Cell c = ctx->cells[idx];

    /* Tile and gate info */
    uint16_t tile = (uint16_t)tile_id_of_xy((uint16_t)target_x, (uint16_t)target_y);
    uint16_t tile_lx = (uint16_t)(target_x / TILE);
    uint16_t tile_ly = (uint16_t)(target_y / TILE);
    const char *gate_str = gate_is_open_tile(ctx, tile) ? "OPEN" : "CLOSED";

    /* Lane ID from A[31:24] */
    uint8_t lane_id = (uint8_t)(c.A >> 24);

    /* Is it in control region? */
    int in_cr = (target_x >= CR_X0 && target_x < CR_X0 + CR_W &&
                 target_y >= CR_Y0 && target_y < CR_Y0 + CR_H);

    /* Format output */
    char buf[512];
    snprintf(buf, sizeof(buf),
        "=== Cell(%u,%u) ===\n"
        "  A = 0x%08X  (lane=%u)\n"
        "  B = 0x%02X      (%s)\n"
        "  G = %u        (0x%02X)\n"
        "  R = %u        (0x%02X '%c')\n"
        "  tile = %u (%u,%u)  gate=%s\n"
        "  index = %u\n"
        "  region = %s\n",
        target_x, target_y,
        c.A, lane_id,
        c.B, vm_opcode_name(c.B),
        c.G, c.G,
        c.R, c.R, (c.R >= 0x20 && c.R < 0x7F) ? (char)c.R : '.',
        tile, tile_lx, tile_ly, gate_str,
        idx,
        in_cr ? "ControlRegion" : "Data");

    /* Plant PRINT cells */
    for (int i = 0; buf[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)buf[i]);
        n++;
    }

    /* HALT */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ── Helper: plant a string as PRINT cells ─────────── */
static int pxl_plant_str(EngineContext *ctx, uint32_t x, uint32_t *y, const char *s) {
    int n = 0;
    for (int i = 0; s[i] && (*y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, *y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)s[i]);
        n++;
    }
    *y += (uint32_t)n;
    return n;
}

/* ── write: write ABGR values to a cell ────────────── */
/* Usage: write x,y A B G R                            */
int pxl_plant_write(EngineContext *ctx, uint32_t x, uint32_t y,
                    const char *arg) {
    if (!ctx) return 0;
    int n = 0;
    char buf[128];

    unsigned wx = 0, wy = 0, wa = 0, wb = 0, wg = 0, wr = 0;
    int parsed = arg ? sscanf(arg, "%u,%u %x %x %u %u",
                               &wx, &wy, &wa, &wb, &wg, &wr) : 0;
    if (parsed < 4) {
        const char *usage = "usage: write x,y A B [G] [R]\n";
        for (int i = 0; usage[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)usage[i]);
            n++;
        }
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        return n + 1;
    }

    if (wx >= CANVAS_W || wy >= CANVAS_H) {
        snprintf(buf, sizeof(buf), "write: (%u,%u) out of bounds\n", wx, wy);
    } else {
        /* Use VM_SET to write the cell — this IS the PixelCode operation */
        uint32_t target_idx = wy * CANVAS_W + wx;
        vm_plant(ctx, x, y + (uint32_t)n, target_idx, VM_SET,
                 (uint8_t)wg, (uint8_t)wr);
        n++;
        /* Also set A and B on the target directly (SET only handles G/R) */
        ctx->cells[target_idx].A = wa;
        ctx->cells[target_idx].B = (uint8_t)wb;
        ctx->cells[target_idx].G = (uint8_t)wg;
        ctx->cells[target_idx].R = (uint8_t)wr;

        snprintf(buf, sizeof(buf),
                 "wrote Cell(%u,%u): A=0x%X B=0x%X G=%u R=%u\n",
                 wx, wy, wa, wb, wg, wr);
    }

    for (int i = 0; buf[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)buf[i]);
        n++;
    }
    vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
    return n + 1;
}

/* ── cp: copy rectangular region ───────────────────── */
/* Usage: cp x0,y0 x1,y1 dx,dy                        */
int pxl_plant_cp(EngineContext *ctx, uint32_t x, uint32_t y,
                 const char *arg) {
    if (!ctx) return 0;
    int n = 0;
    char buf[128];

    unsigned x0, y0, x1, y1, dx, dy;
    int parsed = arg ? sscanf(arg, "%u,%u %u,%u %u,%u",
                               &x0, &y0, &x1, &y1, &dx, &dy) : 0;
    if (parsed < 6) {
        const char *usage = "usage: cp x0,y0 x1,y1 dx,dy\n"
                            "  copy region (x0,y0)-(x1,y1) to offset (dx,dy)\n";
        for (int i = 0; usage[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)usage[i]);
            n++;
        }
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        return n + 1;
    }

    if (x1 >= CANVAS_W) x1 = CANVAS_W - 1;
    if (y1 >= CANVAS_H) y1 = CANVAS_H - 1;

    uint32_t copied = 0;
    for (uint32_t cy = y0; cy <= y1 && cy < CANVAS_H; cy++) {
        for (uint32_t cx = x0; cx <= x1 && cx < CANVAS_W; cx++) {
            uint32_t dst_x = cx + dx, dst_y = cy + dy;
            if (dst_x < CANVAS_W && dst_y < CANVAS_H) {
                uint32_t si = cy * CANVAS_W + cx;
                uint32_t di = dst_y * CANVAS_W + dst_x;
                ctx->cells[di] = ctx->cells[si];
                copied++;
            }
        }
    }

    snprintf(buf, sizeof(buf), "copied %u cells from (%u,%u)-(%u,%u) +(%u,%u)\n",
             copied, x0, y0, x1, y1, dx, dy);
    for (int i = 0; buf[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)buf[i]);
        n++;
    }
    vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
    return n + 1;
}

/* ── mv: move region (copy + clear source) ─────────── */
/* Usage: mv x0,y0 x1,y1 dx,dy                        */
int pxl_plant_mv(EngineContext *ctx, uint32_t x, uint32_t y,
                 const char *arg) {
    if (!ctx) return 0;
    int n = 0;
    char buf[128];

    unsigned x0, y0, x1, y1, dx, dy;
    int parsed = arg ? sscanf(arg, "%u,%u %u,%u %u,%u",
                               &x0, &y0, &x1, &y1, &dx, &dy) : 0;
    if (parsed < 6) {
        const char *usage = "usage: mv x0,y0 x1,y1 dx,dy\n"
                            "  move region (copy + clear source)\n";
        for (int i = 0; usage[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)usage[i]);
            n++;
        }
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        return n + 1;
    }

    if (x1 >= CANVAS_W) x1 = CANVAS_W - 1;
    if (y1 >= CANVAS_H) y1 = CANVAS_H - 1;

    uint32_t moved = 0;
    for (uint32_t cy = y0; cy <= y1 && cy < CANVAS_H; cy++) {
        for (uint32_t cx = x0; cx <= x1 && cx < CANVAS_W; cx++) {
            uint32_t dst_x = cx + dx, dst_y = cy + dy;
            if (dst_x < CANVAS_W && dst_y < CANVAS_H) {
                uint32_t si = cy * CANVAS_W + cx;
                uint32_t di = dst_y * CANVAS_W + dst_x;
                ctx->cells[di] = ctx->cells[si];
                memset(&ctx->cells[si], 0, sizeof(Cell));
                moved++;
            }
        }
    }

    snprintf(buf, sizeof(buf), "moved %u cells from (%u,%u)-(%u,%u) +(%u,%u)\n",
             moved, x0, y0, x1, y1, dx, dy);
    for (int i = 0; buf[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)buf[i]);
        n++;
    }
    vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
    return n + 1;
}

/* ── gate: open/close/toggle/status ────────────────── */
/* Usage: gate open|close|toggle|status <tile_id>      */
int pxl_plant_gate(EngineContext *ctx, uint32_t x, uint32_t y,
                   const char *arg) {
    if (!ctx) return 0;
    int n = 0;
    char buf[128];

    char subcmd[16] = {0};
    unsigned tile_id = 0;
    int parsed = arg ? sscanf(arg, "%15s %u", subcmd, &tile_id) : 0;

    if (parsed < 1) {
        const char *usage = "usage: gate open|close|toggle|status [tile_id]\n";
        for (int i = 0; usage[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)usage[i]);
            n++;
        }
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        return n + 1;
    }

    if (strcmp(subcmd, "status") == 0) {
        /* Show gate status summary */
        int open_count = 0;
        for (int i = 0; i < TILE_COUNT; i++)
            if (gate_is_open_tile(ctx, (uint16_t)i)) open_count++;
        snprintf(buf, sizeof(buf), "gates: %d/%d open\n", open_count, TILE_COUNT);
    } else if (parsed < 2) {
        snprintf(buf, sizeof(buf), "usage: gate %s <tile_id>\n", subcmd);
    } else if (tile_id >= (unsigned)TILE_COUNT) {
        snprintf(buf, sizeof(buf), "gate: tile %u out of range (max %d)\n",
                 tile_id, TILE_COUNT - 1);
    } else if (strcmp(subcmd, "open") == 0) {
        /* Plant VM_GATE_ON instruction */
        vm_plant(ctx, x, y + (uint32_t)n, tile_id, VM_GATE_ON, 0, 0);
        n++;
        gate_open_tile(ctx, (uint16_t)tile_id);
        snprintf(buf, sizeof(buf), "gate %u: OPENED\n", tile_id);
    } else if (strcmp(subcmd, "close") == 0) {
        vm_plant(ctx, x, y + (uint32_t)n, tile_id, VM_GATE_OFF, 0, 0);
        n++;
        gate_close_tile(ctx, (uint16_t)tile_id);
        snprintf(buf, sizeof(buf), "gate %u: CLOSED\n", tile_id);
    } else if (strcmp(subcmd, "toggle") == 0) {
        if (gate_is_open_tile(ctx, (uint16_t)tile_id)) {
            vm_plant(ctx, x, y + (uint32_t)n, tile_id, VM_GATE_OFF, 0, 0);
            n++;
            gate_close_tile(ctx, (uint16_t)tile_id);
            snprintf(buf, sizeof(buf), "gate %u: CLOSED (was open)\n", tile_id);
        } else {
            vm_plant(ctx, x, y + (uint32_t)n, tile_id, VM_GATE_ON, 0, 0);
            n++;
            gate_open_tile(ctx, (uint16_t)tile_id);
            snprintf(buf, sizeof(buf), "gate %u: OPENED (was closed)\n", tile_id);
        }
    } else {
        snprintf(buf, sizeof(buf), "gate: unknown command '%s'\n", subcmd);
    }

    for (int i = 0; buf[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)buf[i]);
        n++;
    }
    vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
    return n + 1;
}

/* ── spawn: create a new process ───────────────────── */
/* Usage: spawn <code_tile> [energy] [lane_id]         */
int pxl_plant_spawn(EngineContext *ctx, uint32_t x, uint32_t y,
                    ProcTable *pt, const char *arg) {
    if (!ctx) return 0;
    int n = 0;
    char buf[128];

    unsigned code_tile = 0, energy = 100, lane = 0;
    int parsed = arg ? sscanf(arg, "%u %u %u", &code_tile, &energy, &lane) : 0;

    if (parsed < 1) {
        const char *usage = "usage: spawn <code_tile> [energy] [lane_id]\n";
        for (int i = 0; usage[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)usage[i]);
            n++;
        }
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        return n + 1;
    }

    /* Plant VM_SPAWN instruction */
    vm_plant(ctx, x, y + (uint32_t)n,
             (uint32_t)code_tile, VM_SPAWN, (uint8_t)energy, (uint8_t)lane);
    n++;

    /* Actually spawn the process */
    if (pt) {
        int pid = proc_spawn(pt, PID_SHELL, (uint16_t)code_tile,
                             energy, (uint8_t)lane);
        if (pid >= 0)
            snprintf(buf, sizeof(buf),
                     "spawned pid=%d tile=%u energy=%u lane=%u\n",
                     pid, code_tile, energy, lane);
        else
            snprintf(buf, sizeof(buf), "spawn failed (table full)\n");
    } else {
        snprintf(buf, sizeof(buf), "spawn: no process table\n");
    }

    for (int i = 0; buf[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)buf[i]);
        n++;
    }
    vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
    return n + 1;
}

/* ── pipe: create/write/read/close/status ──────────── */
/* Usage: pipe create <writer_pid> <reader_pid>        */
/*        pipe write <id> <data>                       */
/*        pipe read <id>                               */
/*        pipe close <id>                              */
/*        pipe status                                  */
int pxl_plant_pipe(EngineContext *ctx, uint32_t x, uint32_t y,
                   PipeTable *pipes, const char *arg) {
    if (!ctx) return 0;
    int n = 0;
    char buf[256];

    char subcmd[16] = {0};
    int parsed = arg ? sscanf(arg, "%15s", subcmd) : 0;

    if (parsed < 1 || !pipes) {
        const char *usage = "usage: pipe create|write|read|close|status\n";
        for (int i = 0; usage[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)usage[i]);
            n++;
        }
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        return n + 1;
    }

    const char *rest = arg + strlen(subcmd);
    while (*rest == ' ') rest++;

    if (strcmp(subcmd, "create") == 0) {
        unsigned wpid = 0, rpid = 0;
        if (sscanf(rest, "%u %u", &wpid, &rpid) >= 2) {
            int pid = pipe_create(pipes, ctx, wpid, rpid);
            if (pid >= 0)
                snprintf(buf, sizeof(buf), "pipe %d created (%u→%u)\n",
                         pid, wpid, rpid);
            else
                snprintf(buf, sizeof(buf), "pipe create failed\n");
        } else {
            snprintf(buf, sizeof(buf), "usage: pipe create <writer> <reader>\n");
        }
    } else if (strcmp(subcmd, "write") == 0) {
        unsigned pid = 0;
        char data[128] = {0};
        if (sscanf(rest, "%u %127[^\n]", &pid, data) >= 2) {
            /* Plant VM_SEND for each byte */
            for (int i = 0; data[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
                vm_plant(ctx, x, y + (uint32_t)n,
                         pid, VM_SEND, 0, (uint8_t)data[i]);
                n++;
            }
            int rc = pipe_write(pipes, ctx, (int)pid,
                               (const uint8_t *)data, (uint16_t)strlen(data));
            snprintf(buf, sizeof(buf), "pipe %u: wrote %d bytes\n",
                     pid, rc >= 0 ? (int)strlen(data) : 0);
        } else {
            snprintf(buf, sizeof(buf), "usage: pipe write <id> <data>\n");
        }
    } else if (strcmp(subcmd, "read") == 0) {
        unsigned pid = 0;
        if (sscanf(rest, "%u", &pid) >= 1) {
            uint8_t rbuf[128] = {0};
            int nr = pipe_read(pipes, ctx, (int)pid, rbuf, 127);
            if (nr > 0) {
                rbuf[nr] = 0;
                snprintf(buf, sizeof(buf), "pipe %u: read %d bytes: %s\n",
                         pid, nr, rbuf);
            } else {
                snprintf(buf, sizeof(buf), "pipe %u: empty\n", pid);
            }
        } else {
            snprintf(buf, sizeof(buf), "usage: pipe read <id>\n");
        }
    } else if (strcmp(subcmd, "close") == 0) {
        unsigned pid = 0;
        if (sscanf(rest, "%u", &pid) >= 1) {
            pipe_close(pipes, ctx, (int)pid);
            snprintf(buf, sizeof(buf), "pipe %u: closed\n", pid);
        } else {
            snprintf(buf, sizeof(buf), "usage: pipe close <id>\n");
        }
    } else if (strcmp(subcmd, "status") == 0) {
        int active = 0;
        for (int i = 0; i < PIPE_MAX; i++)
            if (pipes->pipes[i].active) active++;
        snprintf(buf, sizeof(buf), "pipes: %d/%d active\n", active, PIPE_MAX);
    } else {
        snprintf(buf, sizeof(buf), "pipe: unknown '%s'\n", subcmd);
    }

    for (int i = 0; buf[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)buf[i]);
        n++;
    }
    vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
    return n + 1;
}

/* ── sched: tick scheduling control ────────────────── */
/* Usage: sched tick [N]    — run N ticks (default 1)  */
/*        sched auto <ms>   — set auto-tick interval   */
/*        sched status      — show tick count/rate     */
int pxl_plant_sched(EngineContext *ctx, uint32_t x, uint32_t y,
                    const char *arg) {
    if (!ctx) return 0;
    int n = 0;
    char buf[128];

    char subcmd[16] = {0};
    unsigned val = 1;
    int parsed = arg ? sscanf(arg, "%15s %u", subcmd, &val) : 0;

    if (parsed < 1) {
        const char *usage =
            "usage: sched tick [N] | sched status\n";
        for (int i = 0; usage[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)usage[i]);
            n++;
        }
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        return n + 1;
    }

    if (strcmp(subcmd, "tick") == 0) {
        if (val > 10000) val = 10000;
        uint32_t start = ctx->tick;
        for (unsigned i = 0; i < val; i++)
            engctx_tick(ctx);
        snprintf(buf, sizeof(buf), "executed %u ticks (%u → %u)\n",
                 val, start, ctx->tick);
    } else if (strcmp(subcmd, "status") == 0) {
        int open_gates = 0;
        for (int i = 0; i < TILE_COUNT; i++)
            if (gate_is_open_tile(ctx, (uint16_t)i)) open_gates++;
        snprintf(buf, sizeof(buf),
                 "tick=%u  gates=%d/%d\n",
                 ctx->tick, open_gates, TILE_COUNT);
    } else {
        snprintf(buf, sizeof(buf), "sched: unknown '%s'\n", subcmd);
    }

    for (int i = 0; buf[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)buf[i]);
        n++;
    }
    vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
    return n + 1;
}

/* ── ls: list directory entries ─────────────────────── */
int pxl_plant_ls(EngineContext *ctx, uint32_t x, uint32_t y,
                 const char *dir) {
    (void)dir; /* dir resolution handled by caller via PathContext */
    if (!ctx) return 0;

    /* This is a stub — actual ls needs PathContext.
     * pxl_plant_ls_ex() below does the real work. */
    const char *msg = "(use pxl_exec_utility_ex for ls)\n";
    int n = 0;
    for (int i = 0; msg[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)msg[i]);
        n++;
    }
    vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
    return n + 1;
}

static int pxl_plant_ls_ex(EngineContext *ctx, uint32_t x, uint32_t y,
                           PathContext *pc, const char *dir_arg) {
    if (!ctx || !pc) return 0;
    int n = 0;

    /* Resolve directory */
    FsKey target = pc->cwd;
    if (dir_arg && strlen(dir_arg) > 0 && strcmp(dir_arg, ".") != 0) {
        if (path_resolve(ctx, pc, dir_arg, &target) != 0) {
            char err[64];
            snprintf(err, sizeof(err), "ls: not found: %s\n", dir_arg);
            for (int i = 0; err[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
                vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)err[i]);
                n++;
            }
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
            return n + 1;
        }
    }

    /* Read directory entries */
    char names[16][16];
    FsKey keys[16];
    int count = path_ls(ctx, pc, target, names, keys, 16);

    if (count == 0) {
        const char *empty = "  (empty)\n";
        for (int i = 0; empty[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)empty[i]);
            n++;
        }
    } else {
        for (int e = 0; e < count; e++) {
            /* Format: "  name  [gate:slot]\n" */
            char line[48];
            snprintf(line, sizeof(line), "  %-12s [%u:%u]\n",
                     names[e], keys[e].gate_id, keys[e].slot);
            for (int i = 0; line[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
                vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)line[i]);
                n++;
            }
        }
    }

    /* Summary line */
    char summary[32];
    snprintf(summary, sizeof(summary), "total %d\n", count);
    for (int i = 0; summary[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)summary[i]);
        n++;
    }

    /* HALT */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ═══════════════════════════════════════════════════════
 * Execute a utility via PixelCode
 *
 * 1. Look up utility in registry
 * 2. Plant program on canvas at PXL_PROG region
 * 3. Run VM from planted position
 * 4. Return 0=ok, -1=not found, -2=plant failed
 * ═══════════════════════════════════════════════════════ */
int pxl_exec_utility(EngineContext *ctx, ProcTable *pt, PipeTable *pipes,
                     const char *cmd, const char *arg) {
    if (!ctx || !cmd) return -1;
    (void)pipes;

    int uid = pxl_find_utility(cmd);
    if (uid == PXL_UTIL_NONE) return -1;

    /* Clear the program region before planting */
    for (uint32_t y = PXL_PROG_Y; y < PXL_PROG_Y + 512 && y < CANVAS_H; y++) {
        uint32_t idx = y * CANVAS_W + PXL_PROG_X;
        memset(&ctx->cells[idx], 0, sizeof(Cell));
    }

    int planted = 0;

    switch (uid) {
    case PXL_UTIL_ECHO:
        planted = pxl_plant_echo(ctx, PXL_PROG_X, PXL_PROG_Y,
                                 arg ? arg : "");
        break;
    case PXL_UTIL_CAT:
        planted = pxl_plant_cat(ctx, PXL_PROG_X, PXL_PROG_Y, arg, pt);
        break;
    case PXL_UTIL_INFO:
        planted = pxl_plant_info(ctx, PXL_PROG_X, PXL_PROG_Y, pt);
        break;
    case PXL_UTIL_HASH:
        planted = pxl_plant_hash(ctx, PXL_PROG_X, PXL_PROG_Y);
        break;
    case PXL_UTIL_HELP:
        planted = pxl_plant_help(ctx, PXL_PROG_X, PXL_PROG_Y);
        break;
    case PXL_UTIL_STAT: {
        /* Parse "x,y" or "x y" from arg */
        uint32_t sx = ORIGIN_X, sy = ORIGIN_Y;
        if (arg && arg[0]) {
            unsigned ux = 0, uy = 0;
            if (sscanf(arg, "%u,%u", &ux, &uy) == 2 ||
                sscanf(arg, "%u %u", &ux, &uy) == 2) {
                sx = ux; sy = uy;
            }
        }
        planted = pxl_plant_stat(ctx, PXL_PROG_X, PXL_PROG_Y, sx, sy);
        break;
    }
    case PXL_UTIL_LS:
        planted = pxl_plant_ls(ctx, PXL_PROG_X, PXL_PROG_Y, arg);
        break;
    case PXL_UTIL_WRITE:
        planted = pxl_plant_write(ctx, PXL_PROG_X, PXL_PROG_Y, arg);
        break;
    case PXL_UTIL_CP:
        planted = pxl_plant_cp(ctx, PXL_PROG_X, PXL_PROG_Y, arg);
        break;
    case PXL_UTIL_MV:
        planted = pxl_plant_mv(ctx, PXL_PROG_X, PXL_PROG_Y, arg);
        break;
    case PXL_UTIL_GATE:
        planted = pxl_plant_gate(ctx, PXL_PROG_X, PXL_PROG_Y, arg);
        break;
    case PXL_UTIL_SPAWN:
        planted = pxl_plant_spawn(ctx, PXL_PROG_X, PXL_PROG_Y, pt, arg);
        break;
    case PXL_UTIL_PIPE:
        planted = pxl_plant_pipe(ctx, PXL_PROG_X, PXL_PROG_Y, pipes, arg);
        break;
    case PXL_UTIL_SCHED:
        planted = pxl_plant_sched(ctx, PXL_PROG_X, PXL_PROG_Y, arg);
        break;
    default:
        return -1;
    }

    if (planted <= 0) return -2;

    /* Run VM from program position */
    VmState vm;
    vm_init(&vm, PXL_PROG_X, PXL_PROG_Y, PID_SHELL);
    vm_run(ctx, &vm);

    return 0;
}

/* ── Extended dispatch with PathContext ─────────────── */
int pxl_exec_utility_ex(EngineContext *ctx, ProcTable *pt, PipeTable *pipes,
                        PathContext *pc, const char *cmd, const char *arg) {
    if (!ctx || !cmd) return -1;
    (void)pipes;

    int uid = pxl_find_utility(cmd);

    /* ls needs PathContext — handle specially */
    if (uid == PXL_UTIL_LS && pc) {
        /* Clear program region */
        for (uint32_t y = PXL_PROG_Y; y < PXL_PROG_Y + 512 && y < CANVAS_H; y++) {
            uint32_t idx = y * CANVAS_W + PXL_PROG_X;
            memset(&ctx->cells[idx], 0, sizeof(Cell));
        }

        int planted = pxl_plant_ls_ex(ctx, PXL_PROG_X, PXL_PROG_Y,
                                       pc, (arg && strlen(arg) > 0) ? arg : ".");
        if (planted <= 0) return -2;

        VmState vm;
        vm_init(&vm, PXL_PROG_X, PXL_PROG_Y, PID_SHELL);
        vm_run(ctx, &vm);
        return 0;
    }

    /* For pipe: forward pipes pointer */
    if (uid == PXL_UTIL_PIPE && pipes) {
        for (uint32_t yy = PXL_PROG_Y; yy < PXL_PROG_Y + 512 && yy < CANVAS_H; yy++) {
            uint32_t idx = yy * CANVAS_W + PXL_PROG_X;
            memset(&ctx->cells[idx], 0, sizeof(Cell));
        }
        int planted = pxl_plant_pipe(ctx, PXL_PROG_X, PXL_PROG_Y, pipes, arg);
        if (planted <= 0) return -2;
        VmState vm;
        vm_init(&vm, PXL_PROG_X, PXL_PROG_Y, PID_SHELL);
        vm_run(ctx, &vm);
        return 0;
    }

    /* Fallback to standard dispatch */
    return pxl_exec_utility(ctx, pt, pipes, cmd, arg);
}
