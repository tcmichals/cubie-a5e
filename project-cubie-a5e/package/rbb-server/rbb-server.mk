##########################
# rbb-server Buildroot package
##########################

RBB_SERVER_VERSION = 1.0
RBB_SERVER_SITE = $(TOPDIR)/package/rbb-server
RBB_SERVER_SITE_METHOD = local
RBB_SERVER_LICENSE = GPL-2.0+
RBB_SERVER_LICENSE_FILES = LICENSE
RBB_SERVER_DEPENDENCIES = host-pkgconf
RBB_SERVER_INSTALL_STAGING = NO
RBB_SERVER_INSTALL_TARGET = YES

# Build steps
define RBB_SERVER_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++20 -pthread -O2 -Wall -o $(@D)/rbb_server $(TARGET_SOURCE_DIR)/rbb_server.cpp
endef

define RBB_SERVER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/rbb_server $(TARGET_DIR)/usr/bin/rbb_server
endef

$(eval $(generic-package))
