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
    { "stat", PXL_UTIL_STAT },
    { NULL,   PXL_UTIL_NONE },
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
        "  help           This message\n"
        "  ps, kill, ls, cd, mkdir, rm\n"
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
