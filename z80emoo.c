#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <z80ex.h>
#include <ctk.h>

#define CPU_MHZ 7.3728
#define NANOS_PER_CYCLE 136 // 1000 / 7.3728

struct z8mo_buffer_t {
    uint8_t write;
    uint8_t read;
    pthread_mutex_t lock;
    int buffer[256];
};

struct z8mo_t {
    Z80EX_CONTEXT* cpu;
    uint8_t* memory;
    int* screen;
    uint16_t width;
    uint16_t height;
    uint16_t cur_y;
    uint16_t cur_x;
    struct z8mo_buffer_t* in;
    struct z8mo_buffer_t* out;
};

static int read_queue(struct z8mo_buffer_t* buff, int* c) {
    if (buff->write == buff->read) {
        return 0;
    }
    *c = buff->buffer[buff->read++];
    return 1;
}

static uint8_t write_queue(struct z8mo_buffer_t* buff, int c) {
    uint8_t write = buff->write;
    write++;
    if (write == buff->read) {
        return 0;
    }
    buff->buffer[buff->write++] = c;
    return 1;
}

Z80EX_BYTE z8mo_mem_read(Z80EX_CONTEXT* cpu, Z80EX_WORD addr, int m1_state, void* user_data) {
    struct z8mo_t* z8mo = (struct z8mo_t*)user_data;
    return z8mo->memory[(uint16_t)addr];
}

void z8mo_mem_write(Z80EX_CONTEXT *cpu, Z80EX_WORD addr, Z80EX_BYTE value, void* user_data) {
    struct z8mo_t* z8mo = (struct z8mo_t*)user_data;
    z8mo->memory[(uint16_t)addr] = value;
}

Z80EX_BYTE z8mo_port_read(Z80EX_CONTEXT *cpu, Z80EX_WORD port, void* user_data) {
    struct z8mo_t* z8mo = (struct z8mo_t*)user_data;
    port &= 0xff;
    if (port == 0x80) {
        pthread_mutex_lock(&z8mo->in->lock);
        if (z8mo->in->read != z8mo->in->write) {
            pthread_mutex_unlock(&z8mo->in->lock);
            return 0xff;
        }
        pthread_mutex_unlock(&z8mo->in->lock);
        return 0x00;
    }
    if (port == 0x81) {
        pthread_mutex_lock(&z8mo->in->lock);
        int c = 0;
        read_queue(z8mo->in, &c);
        pthread_mutex_unlock(&z8mo->in->lock);
        return (uint8_t)c;
    }
    return 0;
}

void z8mo_port_write(Z80EX_CONTEXT *cpu, Z80EX_WORD port, Z80EX_BYTE value, void* user_data) {
    struct z8mo_t* z8mo = (struct z8mo_t*)user_data;
    port &= 0xff;
    if (port == 0x81) {
        pthread_mutex_lock(&z8mo->out->lock);
        write_queue(z8mo->out, (int)value);
        pthread_mutex_unlock(&z8mo->out->lock);
    }
}

Z80EX_BYTE z8mo_int_read(Z80EX_CONTEXT *cpu, void* user_data) {
    //struct z8mo_t* z8mo = (struct z8mo_t*)user_data;
    return 0;
}

Z80EX_BYTE z8mo_mem_read_dasm(Z80EX_WORD addr, void* user_data) {
    struct z8mo_t* z8mo = (struct z8mo_t*)user_data;
    return z8mo->memory[(uint16_t)addr];
}

void z8mo_load_rom(struct z8mo_t* z8mo, char* file) {
    unsigned long len;

    FILE* fh = fopen(file, "rb");
    if (fh == NULL) {
        fprintf(stderr, "Could not open rom file: %s\n", file);
        exit(1);
    }

    fseek(fh, 0, SEEK_END);
    len = ftell(fh);
    rewind(fh);

    if (len > 65536) {
        fprintf(stderr, "rom file %s longer than memory\n", file);
        len = 65536;
    }
    fread(z8mo->memory, len, 1, fh);

    fclose(fh);
}

void z8mo_args(struct z8mo_t* z8mo, int argc, char* argv[]) {
    int option_index = 0;
    static struct option long_options[] = {
        {"rom", optional_argument, 0, 'r'},
        {0, 0, 0, 0}
    };

    char c;
    while (1) {
        c = getopt_long(argc, argv, "r:", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
            case 'r':
                z8mo_load_rom(z8mo, optarg);
                break;
            default:
                exit(1);
        }
    }
}

void z8mo_init(struct z8mo_t* z8mo, int argc, char* argv[]) {
    memset(z8mo, 0, sizeof(struct z8mo_t));
    z8mo->memory = malloc(sizeof(uint8_t) * 65536);
    if (z8mo->memory == NULL) {
        return;
    }
    memset(z8mo->memory, 0, sizeof(uint8_t) * 65536);
    z8mo->cpu = z80ex_create(z8mo_mem_read, z8mo, z8mo_mem_write, z8mo, z8mo_port_read, z8mo, z8mo_port_write, z8mo, z8mo_int_read, z8mo);
    z8mo_args(z8mo, argc, argv);
}

void* z8mo_cpu_thread_start(void* user_data) {
    struct z8mo_t* z8mo = (struct z8mo_t*)user_data;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = NANOS_PER_CYCLE;
    while (1) {
        if (z80ex_doing_halt(z8mo->cpu)) {
            break;
        }
        int tstates = z80ex_step(z8mo->cpu);
        ts.tv_nsec = NANOS_PER_CYCLE * tstates;
        nanosleep(&ts, NULL);
    }
    return NULL;
}

void z8mo_serial_buffer_init(struct z8mo_buffer_t* serial_buffer) {
    memset(serial_buffer, 0, sizeof(struct z8mo_buffer_t));
    pthread_mutex_init(&serial_buffer->lock, NULL);
}

static uint8_t key_event_handler(struct z8mo_t* z8mo, ctk_event_t* event) {
    if (event->key > 0) {
        if (pthread_mutex_trylock(&z8mo->in->lock) != 0) {
            return 1;
        }
        write_queue(z8mo->in, event->key);
        pthread_mutex_unlock(&z8mo->in->lock);
        z80ex_int(z8mo->cpu);
    }
    return 1;
}

static uint8_t draw_event_handler(struct z8mo_t* z8mo, ctk_event_t* event) {
    int c;
    uint16_t idx;
    for (uint16_t y = 0; y < event->widget->height; y++) {
        for (uint16_t x = 0; x < event->widget->width; x++) {
            idx = (y * event->widget->width) + x;
            c = z8mo->screen[idx];
            if (c > 0) {
                ctk_addch(event->widget, x, y, CTK_COLOR_WINDOW, c);
            }
        }
    }
    return 1;
}

static uint8_t main_event_handler(ctk_event_t* event, void* user_data) {
    struct z8mo_t* z8mo = (struct z8mo_t*)user_data;
    if (event->type == CTK_EVENT_KEY) {
        return key_event_handler(z8mo, event);
    }
    if (event->type == CTK_EVENT_DRAW) {
        return draw_event_handler(z8mo, event);
    }
    return 1;
}

static uint8_t loop_event_handler(ctk_ctx_t* ctx, void* user_data) {
    struct z8mo_t* z8mo = (struct z8mo_t*)user_data;
    if (pthread_mutex_trylock(&z8mo->out->lock) != 0) {
        return 0;
    }
    int c = 0;
    while (read_queue(z8mo->out, &c)) {
        if (c == 0) {
            continue;
        }
        ctx->redraw = 1;
        if (c == 10) {
            z8mo->cur_x = 0;
            z8mo->cur_y++;
            continue;
        }
        uint16_t idx = (z8mo->cur_y * z8mo->width) + z8mo->cur_x;
        z8mo->screen[idx] = c;
        z8mo->cur_x++;
        if (z8mo->cur_x == z8mo->width) {
            z8mo->cur_x = 0;
            z8mo->cur_y++;
        }
    }
    pthread_mutex_unlock(&z8mo->out->lock);
    return 1;
}

void z8mo_create_screen_buffer(struct z8mo_t* z8mo) {
    z8mo->screen = malloc(sizeof(int) * z8mo->width * z8mo->height);
    if (z8mo->screen == NULL) {
        return;
    }
    memset(z8mo->screen, 0, sizeof(int) * z8mo->width * z8mo->height);
}

int main(int argc, char* argv[]) {
    struct z8mo_t z8mo;
    z8mo_init(&z8mo, argc, argv);

    struct z8mo_buffer_t in_buffer;
    struct z8mo_buffer_t out_buffer;
    z8mo_serial_buffer_init(&in_buffer);
    z8mo_serial_buffer_init(&out_buffer);
    z8mo.in = &in_buffer;
    z8mo.out = &out_buffer;

    ctk_ctx_t ctx;
    ctk_init(&ctx, NULL, 0);
    z8mo.width = ctx.mainwin.width;
    z8mo.height = ctx.mainwin.height;
    ctk_widget_event_handler(&ctx.mainwin, main_event_handler, &z8mo);
    ctk_loop_callback(&ctx, loop_event_handler, &z8mo);
    z8mo_create_screen_buffer(&z8mo);

    pthread_t cpu_thread;
    pthread_create(&cpu_thread, NULL, z8mo_cpu_thread_start, (void*)&z8mo);

    ctk_main_loop(&ctx);
    ctk_end(&ctx);

    pthread_join(cpu_thread, NULL);

    pthread_mutex_destroy(&in_buffer.lock);
    pthread_mutex_destroy(&out_buffer.lock);

    return 0;
}
