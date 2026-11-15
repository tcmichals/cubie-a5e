include $(sort $(wildcard $(BR2_EXTERNAL_CUBIE_A5E_PATH)/package/*/*.mk))

ifeq ($(BR2_PACKAGE_LIBCAMERA_PIPELINE_SUN60I_ISP),y)
LIBCAMERA_PIPELINES-y += sun60i-isp
LIBCAMERA_CONF_OPTS := $(filter-out -Dpipelines=%,$(LIBCAMERA_CONF_OPTS))
LIBCAMERA_CONF_OPTS += -Dpipelines=$(subst $(space),$(comma),$(LIBCAMERA_PIPELINES-y))
LIBCAMERA_CONF_OPTS += -Dipas=sun60i-isp
endif
