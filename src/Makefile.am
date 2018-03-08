bin_PROGRAMS = vkrunner

AM_CFLAGS = \
	$(VULKAN_CFLAGS) \
	$(WARNING_CFLAGS) \
	$(NULL)

vkrunner_SOURCES = \
	vr-error-message.c \
	vr-error-message.h \
	vr-util.c \
	vr-util.h \
	vr-vk.c \
	vr-vk.h \
	vr-vk-core-funcs.h \
	vr-vk-device-funcs.h \
	vr-vk-instance-funcs.h \
	vr-window.c \
	vr-window.h \
	main.c \
	$(NULL)

vkrunner_LDADD = \
	$(EXTRA_LIBS) \
	$(NULL)
