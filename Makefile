
CFLAGS += -DJXL_STATIC_DEFINE -DJXL_THREADS_STATIC_DEFINE

SOURCES += dllmain.cpp
PROJECT_BASENAME = krglhjxl

RC_LEGALCOPYRIGHT ?= Copyright (C) 2022-2022 Julian Uy; See details of license at license.txt, or the source code location.

include external/tp_stubz/Rules.lib.make
