# SPDX-License-Identifier: GPL-2.0
##################################################
#
# Makefile for 68000 core based cpus
#
# 2012.10.21, Luis Alves <ljalvs@gmail.com>
#             Merged all 68000 based cpu's config
#             files into a single directory.
#

# 68328, 68EZ328, 68VZ328

obj-y			+= entry.o
ifdef CONFIG_ROSCO_M68K
obj-y			+= rosco.o
else
obj-y			+= ints.o timers.o m68328.o
endif
obj-$(CONFIG_ROM)	+= romvec.o

obj-$(CONFIG_DRAGEN2)	+= dragen2.o
obj-$(CONFIG_UCSIMM)	+= ucsimm.o
obj-$(CONFIG_UCDIMM)	+= ucsimm.o

obj-y			+= head.o
