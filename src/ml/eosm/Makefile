MODULE_NAME = soup
TOP_DIR = ../..
ENGINE_NAMES = se wb cr2 color debayer lut stock grade jpeg_enc jpeg_dec pipeline look
MODULE_OBJS = $(BUILD_DIR)/soup.o $(BUILD_DIR)/develop.o $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(ENGINE_NAMES)))
MODULE_CFLAGS = -Iengine -Wno-missing-field-initializers -fno-builtin -mno-unaligned-access

include $(TOP_DIR)/modules/Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/module_complete

$(BUILD_DIR)/%.o: engine/%.c | $(BUILD_DIR)
	$(CC) $(filter-out -ffast-math -fsingle-precision-constant,$(CFLAGS)) -O2 $(CDEPS_FLAGS) -c -o $@ $<
