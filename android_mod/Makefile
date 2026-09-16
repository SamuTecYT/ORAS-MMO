TARGET		:=	OrasMmo
BUILD		:=	build
SOURCES		:=	.
INCLUDES	:=	.

CXXFLAGS	:=	-Os -std=c++11 -mword-relocations \
				-fomit-frame-pointer -ffunction-sections \
				-fdata-sections -fno-exceptions -fno-rtti \
				-Wall -Wextra -Wno-unused-variable -Wno-unused-parameter \
				-I$(INCLUDES)

LDFLAGS		:=	-specs=3dsx.specs -g -arch=armv6k -mtune=mpcore -mfloat-abi=hard \
				-Wl,-Map,$(notdir $*.map)

LIBS		:=	-lctrpf -lctru -lm

all: $(BUILD)/$(TARGET).3gx

$(BUILD)/$(TARGET).3gx: $(SOURCES)/main.cpp
	@mkdir -p $(BUILD)
	arm-none-eabi-g++ $(CXXFLAGS) $(LDFLAGS) $(SOURCES)/main.cpp -o $@ $(LIBS)

clean:
	@rm -rf $(BUILD)
