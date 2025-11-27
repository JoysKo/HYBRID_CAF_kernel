dts-dirs += arm
dts-dirs += broadcom
dts-dirs += qcom
dts-dirs += renesas
dts-dirs += socionext

subdir-y	:= $(dts-dirs)

dtstree		:= $(srctree)/$(src)

dtb-$(CONFIG_OF_ALL_DTBS) := $(patsubst $(dtstree)/%.dts,%.dtb, $(foreach d,$(dts-dirs), $(wildcard $(dtstree)/$(d)/*.dts)))

always		:= $(dtb-y)
targets += dtbs
