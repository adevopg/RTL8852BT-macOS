# Makefile - compila RTL8852BT.kext con clang y Kernel.framework (solo en macOS)
#
#   make          -> build/RTL8852BT.kext
#   make clean
#
# Requiere Xcode Command Line Tools:  xcode-select --install

NAME     := RTL8852BT
BUILD    := build
KEXT     := $(BUILD)/$(NAME).kext
SDK      := $(shell xcrun --sdk macosx --show-sdk-path)
KHDRS    := $(SDK)/System/Library/Frameworks/Kernel.framework/Headers
MINVER   := 10.15
ARCH     := x86_64

SRCS     := $(wildcard src/*.cpp)
OBJS     := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))

CXX      := xcrun clang++
CXXFLAGS := -arch $(ARCH) -mmacosx-version-min=$(MINVER) \
            -mkernel -fno-builtin -fno-rtti -fno-exceptions -fno-common \
            -fapple-kext -std=gnu++17 -Wall -Wextra -Wno-unused-parameter \
            -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
            -isysroot $(SDK) -isystem $(KHDRS) -Isrc -I../reference/rtw89-linux
LDFLAGS  := -arch $(ARCH) -mmacosx-version-min=$(MINVER) -isysroot $(SDK) \
            -Xlinker -kext -nostdlib -lkmodc++ -lkmod -lcc_kext

all: $(KEXT)

$(BUILD)/%.o: src/%.cpp src/*.h src/*.hpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(KEXT): $(OBJS) Info.plist ../firmware/rtw8852bt_fw.bin
	@rm -rf $(KEXT)
	@mkdir -p $(KEXT)/Contents/MacOS $(KEXT)/Contents/Resources
	$(CXX) $(LDFLAGS) $(OBJS) -o $(KEXT)/Contents/MacOS/$(NAME)
	cp Info.plist $(KEXT)/Contents/
	cp ../firmware/rtw8852bt_fw.bin $(KEXT)/Contents/Resources/
	@echo "--- arquitectura del binario ---"
	@file $(KEXT)/Contents/MacOS/$(NAME)
	@echo "Nota: 'kextlibs' solo funciona si la arquitectura del kext coincide con"
	@echo "      la del Mac que compila. Al cruzar a x86_64 desde Apple Silicon"
	@echo "      reporta cientos de simbolos no encontrados; es esperable y no"
	@echo "      indica un problema. Los simbolos de IOKit los resuelve el kernel"
	@echo "      al cargar, segun OSBundleLibraries del Info.plist."
	@echo "Listo: $(KEXT)"

clean:
	rm -rf $(BUILD)

.PHONY: all clean
