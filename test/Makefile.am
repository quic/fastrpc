# Define the test program
bin_PROGRAMS = fastrpc_test

# Define the source files for the test program
fastrpc_test_SOURCES = fastrpc_test.c

# Define the compiler flags for the test program
fastrpc_test_CFLAGS = -I$(top_srcdir)/inc -DUSE_SYSLOG

if ANDROID_CC
USE_LOG = -llog
endif

# Define the linker flags for the test program
fastrpc_test_LDADD = -ldl $(USE_LOG)

# If compiling for Android, include the Android-specific flags
if ANDROID_CC
fastrpc_test_CFLAGS += -DANDROID
endif
