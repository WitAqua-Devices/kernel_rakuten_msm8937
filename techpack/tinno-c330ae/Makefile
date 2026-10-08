ifeq ($(CONFIG_MACH_TINNO_C330AE),y)
obj-$(CONFIG_NFC_CXD224X)	+= cxd224x/
obj-$(CONFIG_C330AE_FASTBOOT_KEY) += fastboot_key/
obj-$(CONFIG_SND_SOC) += foursemi/
obj-$(CONFIG_INPUT_TOUCHSCREEN) += ft8006p/
obj-$(CONFIG_HALL_SENSOR) += hall_sensor/
else
ccflags-y := -Wno-unused-function
obj-y := stub.o
endif
