TARGET = rd-count

CC = gcc
CFLAGS = -Wall -Wextra -O3

CFLAGS += $(shell pkg-config --cflags libcurl)
LDFLAGS =
LDLIBS += $(shell pkg-config --libs libcurl)

BUILD_DIR = build
SRC_DIR = src

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

CFLAGS += -MMD -MP
DEPS = $(OBJS:.o=.d)


all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)


run: $(TARGET)
	./$(TARGET)


clean:
	rm -rf $(BUILD_DIR) $(TARGET)


rebuild: clean $(TARGET)


-include $(DEPS)


.PHONY: all clean rebuild run
