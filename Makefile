CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -I. -Isrc -Ivendor/lvgl -Ivendor/cjson \
           $(shell pkg-config --cflags libevdev libcurl 2>/dev/null)
LIBS    = -lm -lpthread \
           $(shell pkg-config --libs libevdev libcurl 2>/dev/null) \
           -lcurl

# Source files
SRCS = src/main.c \
       src/core/config.c \
       src/core/bridge.c \
       src/core/signer.c \
       src/core/state.c \
       src/screens/home.c \
       src/screens/channels.c \
       src/screens/send.c \
       src/screens/confirm.c \
       src/screens/receive.c \
       src/screens/signer_screen.c \
       src/widgets/wy_qr.c \
       src/widgets/wy_numpad.c \
       src/widgets/wy_status_bar.c \
       vendor/cjson/cJSON.c

# LVGL sources (subset — add more as needed)
LVGL_DIR = vendor/lvgl
LVGL_SRCS = $(wildcard $(LVGL_DIR)/src/core/*.c) \
             $(wildcard $(LVGL_DIR)/src/draw/*.c) \
             $(wildcard $(LVGL_DIR)/src/draw/sw/*.c) \
             $(wildcard $(LVGL_DIR)/src/font/*.c) \
             $(wildcard $(LVGL_DIR)/src/layouts/*.c) \
             $(wildcard $(LVGL_DIR)/src/misc/*.c) \
             $(wildcard $(LVGL_DIR)/src/others/*.c) \
             $(wildcard $(LVGL_DIR)/src/widgets/*.c) \
             $(wildcard $(LVGL_DIR)/src/drivers/display/*.c) \
             $(wildcard $(LVGL_DIR)/src/drivers/evdev/*.c)

ALL_SRCS = $(SRCS) $(LVGL_SRCS)
OBJS     = $(ALL_SRCS:.c=.o)
TARGET   = fiber-kiosk

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install:
	install -m755 $(TARGET) /usr/local/bin/
	install -m644 scripts/fiber-kiosk.service /etc/systemd/system/
	systemctl daemon-reload
	systemctl enable fiber-kiosk
