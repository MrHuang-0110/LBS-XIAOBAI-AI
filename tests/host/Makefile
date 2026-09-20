# 宿主 C 测试（gcc）：协议/执行器/显示数字的纯逻辑校验
# 用法（任选）：
#   make -C tests/host test
#   make -f tests/host/Makefile test
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
CC      ?= gcc
CFLAGS  ?= -std=c99 -O1 -g -Wall -Wextra -Wno-unused-parameter
INC      = -I$(ROOT)/tests/host -I$(ROOT)/tests/host/include \
           -I$(ROOT)/Protocol -I$(ROOT)/User -I$(ROOT)/BSP_Drivers
BUILD    = $(ROOT)/tests/host/build
OUT      = $(BUILD)/run_tests

SRC = \
    $(ROOT)/tests/host/test_main.c \
    $(ROOT)/tests/host/test_proto_ble.c \
    $(ROOT)/tests/host/test_program.c \
    $(ROOT)/tests/host/test_display.c \
    $(ROOT)/tests/host/host_stub.c \
    $(ROOT)/Protocol/Proto_Ble.c \
    $(ROOT)/User/App_Program.c \
    $(ROOT)/User/App_Mode.c \
    $(ROOT)/User/App_Display.c \
    $(ROOT)/User/App_Eye.c \
    $(ROOT)/User/App_Vehicle.c \
    $(ROOT)/User/Eye_Data.c

.PHONY: test clean golden

test: $(OUT)
	$(OUT)

$(OUT): $(SRC) $(ROOT)/tests/host/golden_frames.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@

golden:
	python3 $(ROOT)/tools/gen_golden.py
	python3 $(ROOT)/tools/gen_golden.py --check
	python3 $(ROOT)/tools/gen_eye_data.py --check

clean:
	rm -rf $(BUILD)
