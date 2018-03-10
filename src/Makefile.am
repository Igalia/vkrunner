bin_PROGRAMS = vkrunner

AM_CFLAGS = \
	$(VULKAN_CFLAGS) \
	$(WARNING_CFLAGS) \
	$(NULL)

vkrunner_SOURCES = \
	vr-allocate-store.c \
	vr-allocate-store.h \
	vr-buffer.c \
	vr-buffer.h \
	vr-config.c \
	vr-config.h \
	vr-error-message.c \
	vr-error-message.h \
	vr-flush-memory.c \
	vr-flush-memory.h \
	vr-list.c \
	vr-list.h \
	vr-script.c \
	vr-script.h \
	vr-util.c \
	vr-util.h \
	vr-vk.c \
	vr-vk.h \
	vr-vk-core-funcs.h \
	vr-vk-device-funcs.h \
	vr-vk-instance-funcs.h \
	vr-pipeline.c \
	vr-pipeline.h \
	vr-subprocess.c \
	vr-subprocess.h \
	vr-test.c \
	vr-test.h \
	vr-window.c \
	vr-window.h \
	main.c \
	$(NULL)

vkrunner_LDADD = \
	$(EXTRA_LIBS) \
	$(NULL)
