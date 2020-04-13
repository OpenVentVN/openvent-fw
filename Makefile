#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_VER := $(shell cat ./version.txt)
IDF_PATH := esp-idf
PROJECT_NAME := openvent-v$(PROJECT_VER)
include $(IDF_PATH)/make/project.mk
