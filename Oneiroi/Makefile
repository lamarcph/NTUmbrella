
ifndef NT_API_PATH
	NT_API_PATH := ../distingNT_API/include
endif

INCLUDE_PATH := $(NT_API_PATH)

inputs := $(wildcard *cpp)
outputs := $(patsubst %.cpp,plugins/%.o,$(inputs))

all: $(outputs)

clean:
	rm -f $(outputs)

plugins/%.o: %.cpp
	mkdir -p $(@D)
	arm-none-eabi-c++ -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall -I$(INCLUDE_PATH) -I./include -c -o $@ $^
